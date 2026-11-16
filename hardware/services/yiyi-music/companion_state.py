"""Persistent reminders, named playlists and acknowledged device commands."""
import json
import logging
import random
import sqlite3
import threading
import time
import uuid
from contextlib import contextmanager
from datetime import datetime, timedelta
from zoneinfo import ZoneInfo

from music_library import ROOT, catalog

LOG = logging.getLogger('yiyi-companion')
TZ = ZoneInfo('Asia/Shanghai')

_SCHEMA = '''
  CREATE TABLE IF NOT EXISTS commands (
    id INTEGER PRIMARY KEY AUTOINCREMENT, action TEXT NOT NULL,
    payload TEXT NOT NULL, created REAL NOT NULL, expires REAL NOT NULL,
    acked REAL, result TEXT);
  CREATE TABLE IF NOT EXISTS settings (key TEXT PRIMARY KEY, value TEXT NOT NULL);
  CREATE TABLE IF NOT EXISTS playlists (name TEXT PRIMARY KEY, songs TEXT NOT NULL, updated REAL NOT NULL);
  CREATE TABLE IF NOT EXISTS reminders (
    id TEXT PRIMARY KEY, text TEXT NOT NULL, due REAL NOT NULL,
    repeat TEXT NOT NULL, interval_seconds INTEGER NOT NULL DEFAULT 0,
    audio TEXT NOT NULL DEFAULT '', enabled INTEGER NOT NULL DEFAULT 1,
    last_command INTEGER, created REAL NOT NULL);
'''
_SCHEMA_LOCK = threading.Lock()
_schema_ready = set()  # database paths whose schema/WAL setup is complete
CONNECTION_TIMEOUT = 20.0


def _ensure_schema(con):
    """Create the schema and enable WAL once per process.

    The old code ran CREATE TABLE + BEGIN IMMEDIATE for *every* call, including
    read-only status/catalog lookups.  With the gateway, MCP bridge and the
    independent music app all sharing this file, that produced real
    "database is locked" errors in the journal.  Schema setup now happens once
    and read-only callers use a deferred transaction.
    """
    path = str(ROOT / 'data/control.sqlite3')
    if path in _schema_ready:
        return
    with _SCHEMA_LOCK:
        if path in _schema_ready:
            return
        try:
            con.execute('PRAGMA journal_mode=WAL')
        except sqlite3.OperationalError:
            LOG.warning('WAL mode unavailable for control.sqlite3; using rollback journal')
        con.executescript(_SCHEMA)
        con.commit()
        _schema_ready.add(path)


@contextmanager
def db(readonly=False):
    (ROOT / 'data').mkdir(parents=True, exist_ok=True)
    con = sqlite3.connect(ROOT / 'data/control.sqlite3', timeout=CONNECTION_TIMEOUT)
    con.row_factory = sqlite3.Row
    con.execute('PRAGMA busy_timeout=20000')
    try:
        _ensure_schema(con)
        # Read-only callers must not serialize behind writers; writers still take
        # the immediate lock so read-modify-write sequences stay atomic.
        con.execute('BEGIN' if readonly else 'BEGIN IMMEDIATE')
        yield con
        con.commit()
    except Exception:
        con.rollback()
        raise
    finally:
        con.close()


def get(con, key, default=None):
    row = con.execute('SELECT value FROM settings WHERE key=?', (key,)).fetchone()
    return json.loads(row[0]) if row else default


def put(con, key, value):
    con.execute('INSERT OR REPLACE INTO settings VALUES (?,?)', (key, json.dumps(value, ensure_ascii=False)))


def enqueue(con, action, payload=None, ttl=120, now=None):
    now = time.time() if now is None else now
    return con.execute('INSERT INTO commands(action,payload,created,expires) VALUES (?,?,?,?)',
                       (action, json.dumps(payload or {}, ensure_ascii=False), now, now + ttl)).lastrowid


def player_status():
    now = time.time()
    with db(readonly=True) as con:
        device = get(con, 'device', {})
        age = now - device.get('seen_at', 0)
        return {'online': age < 20, 'last_seen_seconds_ago': round(age) if device else None,
                'device': device.get('status', {}), 'queue': get(con, 'queue'),
                'pending_commands': con.execute('SELECT count(*) FROM commands WHERE acked IS NULL AND expires>?', (now,)).fetchone()[0]}


def control(action, seconds=0):
    allowed = {'pause', 'resume', 'stop', 'next', 'previous', 'sleep', 'cancel_sleep', 'status'}
    if action not in allowed:
        return {'ok': False, 'message': '支持 pause/resume/stop/next/previous/sleep/cancel_sleep/status'}
    if action == 'status':
        return dict(ok=True, **player_status())
    if action == 'sleep' and not 1 <= int(seconds) <= 86400:
        return {'ok': False, 'message': '定时停止范围为 1 秒至 24 小时'}
    now = time.time()
    with db() as con:
        if action in ('next', 'previous'):
            q = get(con, 'queue')
            if not q or not q.get('active'):
                return {'ok': False, 'message': '当前没有正在运行的歌单'}
            q['index'] = (q['index'] + (1 if action == 'next' else -1)) % len(q['songs'])
            command_id = queue_track(con, q, now)
        else:
            if action == 'stop':
                put(con, 'queue', None)
                put(con, 'sleep_deadline', None)
                # Explicit stop supersedes pending automatic play commands.
                con.execute("UPDATE commands SET acked=?,result='superseded' WHERE acked IS NULL AND action='play'", (now,))
            payload = {'deadline': now + int(seconds)} if action == 'sleep' else {}
            if action == 'sleep':
                put(con, 'sleep_deadline', payload['deadline'])
            elif action == 'cancel_sleep':
                put(con, 'sleep_deadline', None)
            command_id = enqueue(con, action, payload, now=now)
    state = player_status()
    return {'ok': True, 'queued': True, 'command_id': command_id, 'device_online': state['online'],
            'message': '已提交设备，执行结果请查询播放状态；设备需联网并处于音乐小智应用。'}


def command_result(command_id):
    with db(readonly=True) as con:
        row = con.execute('SELECT id,action,acked,result,expires FROM commands WHERE id=?', (int(command_id),)).fetchone()
        if not row:
            return {'ok': False, 'message': '没有这条指令'}
        return {'ok': True, **dict(row), 'expired': row['expires'] < time.time() and row['acked'] is None}


def save_playlist(name, songs, append=False):
    name = name.strip()
    if not name or len(name) > 80 or not songs:
        return {'ok': False, 'message': '请提供歌单名称和歌曲列表'}
    with db() as con:
        old = con.execute('SELECT songs FROM playlists WHERE name=?', (name,)).fetchone()
        items = json.loads(old[0]) if append and old else []
        for song in songs:
            entry = {k: song.get(k, '') for k in ('id', 'title', 'artist')}
            if not any(s['id'] == entry['id'] for s in items):
                items.append(entry)
        if len(items) > 200:
            return {'ok': False, 'message': '每个歌单最多 200 首歌曲'}
        con.execute('INSERT OR REPLACE INTO playlists VALUES (?,?,?)',
                    (name, json.dumps(items, ensure_ascii=False), time.time()))
    return {'ok': True, 'name': name, 'songs': items, 'count': len(items)}


def list_playlists(name=''):
    with db(readonly=True) as con:
        rows = con.execute('SELECT * FROM playlists WHERE name=?' if name else 'SELECT * FROM playlists ORDER BY name',
                           (name,) if name else ()).fetchall()
        return {'ok': True, 'playlists': [{'name': r['name'], 'songs': json.loads(r['songs'])} for r in rows]}


def playlist_summaries():
    with db(readonly=True) as con:
        rows = con.execute('SELECT name,songs FROM playlists ORDER BY name LIMIT 20').fetchall()
        total = con.execute('SELECT count(*) FROM playlists').fetchone()[0]
        return {'ok': True, 'total': total,
                'playlists': [{'name': row['name'], 'count': len(json.loads(row['songs']))}
                              for row in rows]}


def delete_playlist(name):
    with db() as con:
        removed = con.execute('DELETE FROM playlists WHERE name=?', (name,)).rowcount
    return {'ok': bool(removed)}


def queue_track(con, q, now):
    s = q['songs'][q['index']]
    q['token'] = uuid.uuid4().hex
    con.execute("UPDATE commands SET acked=?,result='superseded' WHERE acked IS NULL AND action='play'", (now,))
    put(con, 'queue', q)
    return enqueue(con, 'play', {'song': s['id'], 'title': s['title'], 'artist': s.get('artist', ''), 'token': q['token']},
                   ttl=300, now=now)


def play_playlist(name, mode='sequence'):
    if mode not in ('sequence', 'loop', 'shuffle', 'repeat_one'):
        return {'ok': False, 'message': '模式支持 sequence 顺序一次、loop 列表循环、shuffle 随机、repeat_one 单曲循环'}
    with db() as con:
        row = con.execute('SELECT songs FROM playlists WHERE name=?', (name,)).fetchone()
        if not row:
            return {'ok': False, 'message': '没有找到这个歌单'}
        # Scan cached media once. Looking up every entry separately rescans the
        # whole library and can exceed the device's HTTP timeout on large lists.
        available = {song['id'] for song in catalog()}
        songs = [s for s in json.loads(row[0]) if s['id'] in available]
        if not songs:
            return {'ok': False, 'message': '歌单歌曲的音频缓存已不可用，请重新准备'}
        if mode == 'shuffle':
            random.shuffle(songs)
        q = {'name': name, 'songs': songs, 'index': 0, 'mode': mode, 'active': True}
        command_id = queue_track(con, q, time.time())
    return {'ok': True, 'queued': True, 'command_id': command_id, 'name': name, 'count': len(songs), 'mode': mode}


def create_reminder(text, when='', delay_seconds=0, repeat='once', interval_minutes=0, audio='', validate_only=False):
    text = text.strip()
    if not text or len(text) > 200:
        return {'ok': False, 'message': '提醒内容需为 1–200 字'}
    if repeat not in ('once', 'daily', 'weekdays', 'interval'):
        return {'ok': False, 'message': '重复方式支持 once/daily/weekdays/interval'}
    if repeat == 'interval' and not 1 <= int(interval_minutes) <= 525600:
        return {'ok': False, 'message': '重复间隔为 1 分钟至 1 年'}
    now = time.time()
    try:
        if delay_seconds:
            if not 5 <= int(delay_seconds) <= 365 * 86400:
                raise ValueError
            due = now + int(delay_seconds)
        else:
            dt = datetime.fromisoformat(when)
            due = (dt.replace(tzinfo=TZ) if dt.tzinfo is None else dt).timestamp()
        if due <= now or due > now + 366 * 86400:
            raise ValueError
    except (ValueError, TypeError, OverflowError):
        return {'ok': False, 'message': '请提供未来一年内的明确时间（北京时间 ISO 格式），或至少 5 秒后的延时。'}
    if repeat == 'weekdays':
        dt = datetime.fromtimestamp(due, TZ)
        while dt.weekday() >= 5:
            dt += timedelta(days=1)
        due = dt.timestamp()
    ident = uuid.uuid4().hex[:12]
    with db() as con:
        if con.execute('SELECT count(*) FROM reminders WHERE enabled=1').fetchone()[0] >= 100:
            return {'ok': False, 'message': '最多同时启用100个提醒，请先取消旧提醒'}
        if validate_only:
            return {'ok': True}
        con.execute('INSERT INTO reminders(id,text,due,repeat,interval_seconds,audio,created) VALUES (?,?,?,?,?,?,?)',
                    (ident, text, due, repeat, int(interval_minutes) * 60, audio, now))
    return {'ok': True, 'id': ident, 'text': text, 'due': datetime.fromtimestamp(due, TZ).isoformat(),
            'repeat': repeat, 'voice_ready': bool(audio),
            'delivery': '设备联网并停留在音乐小智应用时，响铃、显示内容，有语音缓存则朗读；对话中延后到待机。'}


def reminders():
    with db(readonly=True) as con:
        rows = con.execute('SELECT * FROM reminders ORDER BY enabled DESC,due LIMIT 100').fetchall()
        return {'ok': True, 'reminders': [{**dict(r), 'due': datetime.fromtimestamp(r['due'], TZ).isoformat()} for r in rows]}


def cancel_reminder(ident):
    with db() as con:
        exists = con.execute('SELECT id FROM reminders WHERE id=?', (ident,)).fetchone()
        con.execute('UPDATE reminders SET enabled=0 WHERE id=?', (ident,))
        # Cancel an alarm that is due but has not yet been delivered.
        con.execute("UPDATE commands SET acked=?,result='cancelled' WHERE acked IS NULL AND action='remind' AND json_extract(payload,'$.reminder_id')=?",
                    (time.time(), ident))
    return {'ok': bool(exists), 'id': ident}


def tick_reminders(con, now, idle=True):
    for r in con.execute('SELECT * FROM reminders WHERE enabled=1 AND due<=?', (now,)).fetchall():
        if not idle and now - r['due'] <= 900:
            continue
        # Deliver once after a short outage. Old reminders remain visibly expired
        # rather than ringing hours late on the next boot.
        if now - r['due'] <= 900:
            command = enqueue(con, 'remind', {'reminder_id': r['id'], 'text': r['text'], 'audio': r['audio']},
                              ttl=max(1, r['due'] + 900 - now), now=now)
        else:
            command = None
        if r['repeat'] == 'once':
            con.execute('UPDATE reminders SET enabled=0,last_command=? WHERE id=?', (command, r['id']))
            continue
        if r['repeat'] == 'interval':
            interval = r['interval_seconds']
            due = r['due'] + (int((now - r['due']) // interval) + 1) * interval
        else:
            dt = datetime.fromtimestamp(r['due'], TZ)
            while dt.timestamp() <= now or (r['repeat'] == 'weekdays' and dt.weekday() >= 5):
                dt += timedelta(days=1)
            due = dt.timestamp()
        con.execute('UPDATE reminders SET due=?,last_command=? WHERE id=?', (due, command, r['id']))


def sync_device(status, now=None):
    now = time.time() if now is None else now
    # One private gateway controls one ESP32; no inbound device ports needed.
    with db() as con:
        previous = get(con, 'device', {})
        ack = int(status.get('ack', 0))
        if ack:
            con.execute('UPDATE commands SET acked=?,result=? WHERE id=? AND acked IS NULL',
                        (now, str(status.get('result', 'ok'))[:200], ack))
        old = previous.get('status', {})
        event = status.get('event')
        fresh = status.get('boot') != old.get('boot') or status.get('event_seq') != old.get('event_seq')
        q = get(con, 'queue')
        if (q and q.get('active') and old.get('boot') and status.get('boot') != old['boot']
                and old.get('token') == q.get('token') and not status.get('playing')):
            # Progress exists only in device RAM. Keep the named playlist but
            # do not claim its old playback session survived an app restart.
            q['active'] = False
            put(con, 'queue', q)
            con.execute("UPDATE commands SET acked=?,result='device_restarted' WHERE acked IS NULL AND action='play' AND json_extract(payload,'$.token')=?",
                        (now, q.get('token')))
        deadline = get(con, 'sleep_deadline')
        if deadline and now >= deadline:
            put(con, 'sleep_deadline', None)
            put(con, 'queue', None)
            q = None
            con.execute("UPDATE commands SET acked=?,result='superseded' WHERE acked IS NULL AND action='play'", (now,))
            enqueue(con, 'stop', now=now)
        # Failed or expired starts must not leave an apparently active queue.
        if q and q.get('active'):
            failed = con.execute("SELECT 1 FROM commands WHERE action='play' AND json_extract(payload,'$.token')=? AND (result IN ('play_failed','no_player') OR (acked IS NULL AND expires<=?))",
                                 (q.get('token'), now)).fetchone()
            if failed:
                q['active'] = False
                put(con, 'queue', q)
        if fresh and event in ('ended', 'error', 'sleep_stopped', 'stopped'):
            if q and q.get('active') and status.get('token') == q.get('token'):
                if event == 'ended':
                    if q['mode'] != 'repeat_one':
                        q['index'] += 1
                    if q['index'] >= len(q['songs']):
                        if q['mode'] == 'sequence':
                            q['active'] = False
                            put(con, 'queue', q)
                        else:
                            q['index'] = 0
                            if q['mode'] == 'shuffle':
                                random.shuffle(q['songs'])
                    if q['active']:
                        queue_track(con, q, now)
                else:
                    q['active'] = False
                    put(con, 'queue', q)
        # Manual point-play supersedes automatic queue traversal.
        elif q and q.get('active') and status.get('playing') and not status.get('token'):
            q['active'] = False
            put(con, 'queue', q)
        put(con, 'device', {'seen_at': now, 'status': status})
        tick_reminders(con, now, bool(status.get('idle')))
        con.execute('DELETE FROM commands WHERE created<?', (now - 7 * 86400,))
        # An alert deferred during speech must not block a later pause/stop.
        # Move it behind the control with a new ID so device high-watermark
        # deduplication remains valid after that control has been acknowledged.
        if not status.get('idle'):
            control_row = con.execute("SELECT id FROM commands WHERE acked IS NULL AND expires>? AND action!='remind' ORDER BY id LIMIT 1", (now,)).fetchone()
            if control_row:
                held = con.execute("SELECT * FROM commands WHERE acked IS NULL AND expires>? AND action='remind' AND id<?", (now, control_row['id'])).fetchall()
                for r in held:
                    con.execute("UPDATE commands SET acked=?,result='postponed' WHERE id=?", (now, r['id']))
                    ident = enqueue(con, 'remind', json.loads(r['payload']), ttl=r['expires']-now, now=now)
                    con.execute('UPDATE reminders SET last_command=? WHERE last_command=?', (ident, r['id']))
        row = con.execute('SELECT * FROM commands WHERE acked IS NULL AND expires>? ORDER BY id LIMIT 1', (now,)).fetchone()
        command = {'id': row['id'], 'action': row['action'], 'payload': json.loads(row['payload'])} if row else None
        # Playback/alerts wait until the voice conversation has finished. Basic
        # controls can still pause/stop music during a conversation.
        if command and command['action'] == 'remind' and not status.get('idle'):
            command = None
        return {'server_time': now, 'command': command}
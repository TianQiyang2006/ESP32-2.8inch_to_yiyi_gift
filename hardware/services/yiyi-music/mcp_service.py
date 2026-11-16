#!/usr/bin/env python3
"""Real catalog, favorites and memory tools. Device actions stay on the device."""
import json
import sqlite3
from contextlib import contextmanager
from datetime import datetime
from zoneinfo import ZoneInfo

from mcp.server.fastmcp import FastMCP

from music_library import ROOT, catalog, find_song, public_song, search
from kugou_client import (KugouError, playlist_tracks as kugou_playlist_tracks,
                          playlists as kugou_playlists, status as kugou_status)
from kugou_source import online_search, prepare, prepare_track
import companion_state as companion
from information import weather, weather_at, hot_news
from reminder_voice import prepare_voice

mcp = FastMCP('依依音乐陪伴站', instructions=(
    '提供真实曲库、酷狗搜索与音源准备、收藏和专属记忆。点歌先调用 yiyi_music_resolve '
    '或 yiyi_kugou_prepare，成功后调用设备工具 self.music.play_song，'
    '这里的查询或收藏操作本身不会让设备出声。酷狗账号歌单可只读查看并分批导入VPS歌单，'
    '导入完毕后调用yiyi_playlist_play。播放控制、定时停止、歌单和主动提醒已实现，'
    '请实际调用工具；queued 表示已提交，执行情况用播放状态或指令结果查询。'
    '天气必须先询问用户城市，不默认推断位置；新闻从热搜读取并注明平台及更新时间。'
    '提醒时间使用北京时间，时间不明确先询问；提醒需要设备联网并处于音乐小智应用。'))


@contextmanager
def database():
    con = sqlite3.connect(ROOT / 'data/companion.sqlite3', timeout=8)
    con.row_factory = sqlite3.Row
    con.execute('CREATE TABLE IF NOT EXISTS favorites (id TEXT PRIMARY KEY, song TEXT NOT NULL)')
    con.execute('CREATE TABLE IF NOT EXISTS memories (key TEXT PRIMARY KEY, value TEXT NOT NULL, updated TEXT NOT NULL)')
    try:
        with con:
            yield con
    finally:
        con.close()


def device_instruction(song):
    return {'tool': 'self.music.play_song',
            'arguments': {'song_name': song['title'], 'artist_name': song.get('artist', '')}}


@mcp.tool()
def yiyi_music_search(query: str = '', limit: int = 10) -> dict:
    """查询 VPS 上实际可播放的歌曲。返回精确歌名和歌手；点歌随后调用设备 self.music.play_song。"""
    songs = search(query, limit)
    return {'songs': [{'id': s['id'], 'title': s['title'], 'artist': s.get('artist', ''),
                       'tags': s.get('tags', []), 'device_call': device_instruction(s)} for s in songs],
            'total_available': len(catalog()), 'playing': False}


@mcp.tool()
def yiyi_music_resolve(song_name: str, artist_name: str = '') -> dict:
    """点歌先用此工具：查本地曲库，未命中则从酷狗准备音源。成功后调用返回的 device_call 播放。"""
    song = find_song(song_name, artist_name)
    if song is None:
        try:
            song = prepare(song_name, artist_name)
        except KugouError as error:
            return {'found': False, 'message': str(error), 'device_playback_started': False}
    return {'found': True, 'song': public_song(song), 'device_call': device_instruction(song),
            'device_playback_started': False}


@mcp.tool()
def yiyi_kugou_search(query: str, limit: int = 5) -> dict:
    """搜索酷狗曲库，返回准确歌名、歌手、hash。搜索结果尚未可播放，随后调用 yiyi_kugou_prepare。"""
    try:
        return {'songs': online_search(query, limit), 'device_playback_started': False}
    except KugouError as error:
        return {'songs': [], 'message': str(error)}


@mcp.tool()
def yiyi_kugou_prepare(song_name: str, artist_name: str = '', song_hash: str = '') -> dict:
    """准备一首酷狗授权音源，转为设备可播放的 MP3 并获取歌词。可用搜索返回的 hash 精确选择；成功后调用 device_call。"""
    try:
        song = prepare(song_name, artist_name, song_hash)
        return {'ready': True, 'title': song['title'], 'artist': song['artist'],
                'device_call': device_instruction(song), 'device_playback_started': False}
    except KugouError as error:
        return {'ready': False, 'message': str(error), 'device_playback_started': False}


@mcp.tool()
def yiyi_kugou_status() -> dict:
    """检查酷狗服务及是否保存登录凭据；不会暴露账号、令牌或链接。"""
    return kugou_status()


@mcp.tool()
def yiyi_kugou_playlists() -> dict:
    """读取当前酷狗登录账号的歌单名称和歌曲数，只读，不修改酷狗账号。"""
    try:
        return kugou_playlists()
    except KugouError as error:
        return {'playlists': [], 'message': str(error)}


@mcp.tool()
def yiyi_kugou_playlist_import(source_playlist: str = '我喜欢', playlist_name: str = '酷狗我喜欢',
                               page: int = 1, batch_size: int = 5) -> dict:
    """把当前酷狗账号歌单按页追加导入VPS本地可播放歌单，自动去重。每次最多准备5首；固定batch_size，从page=1开始逐页调用，直到has_more=false。已导入部分随时可用yiyi_playlist_play播放。只下载账号有权播放的完整音源，失败歌曲会跳过并说明。"""
    if not source_playlist.strip() or len(source_playlist) > 80:
        return {'ok': False, 'message': '请提供有效的酷狗歌单名称'}
    if not playlist_name.strip() or len(playlist_name) > 80:
        return {'ok': False, 'message': '请提供有效的本地歌单名称'}
    if int(page) < 1 or not 1 <= int(batch_size) <= 5:
        return {'ok': False, 'message': 'page需从1开始，batch_size范围为1–5'}
    try:
        listings = kugou_playlists()['playlists']
        matches = [item for item in listings if item['name'] == source_playlist]
        if not matches:
            return {'ok': False, 'message': '酷狗账号里没有找到这个歌单'}
        source = next((item for item in matches if item['is_default_favorites']), matches[0])
        tracks = kugou_playlist_tracks(source['list_id'], page, int(batch_size), source.get('type', 0))
        ready, errors = [], []
        for track in tracks['tracks']:
            try:
                ready.append(prepare_track(track))
            except KugouError as error:
                errors.append({'title': track['title'], 'artist': track['artist'], 'message': str(error)})
        existing = companion.list_playlists(playlist_name)['playlists']
        saved = companion.save_playlist(playlist_name, ready, append=True) if ready else {
            'ok': True, 'count': len(existing[0]['songs']) if existing else 0}
        if not saved['ok']:
            saved['failed'] = errors
            return saved
        has_more = int(page) * int(batch_size) < tracks['total']
        return {'ok': True, 'source_playlist': source_playlist, 'playlist_name': playlist_name,
                'page': int(page), 'source_total': tracks['total'], 'prepared': len(ready),
                'failed': errors, 'imported_count': saved['count'], 'has_more': has_more,
                'next_page': int(page) + 1 if has_more else None,
                'message': '本页处理完成；可播放已导入部分，继续导入时保持batch_size不变并使用next_page。' if
                           has_more else '已处理到歌单末页，请查看prepared和failed确认可播放歌曲。'}
    except KugouError as error:
        return {'ok': False, 'message': str(error)}


@mcp.tool()
def yiyi_favorite_set(song_name: str, favorite: bool = True, artist_name: str = '') -> dict:
    """收藏或取消收藏曲库歌曲，跨重启保存。"""
    song = find_song(song_name, artist_name)
    if song is None:
        return {'ok': False, 'message': '曲库没有匹配歌曲'}
    with database() as db:
        if favorite:
            db.execute('INSERT OR REPLACE INTO favorites VALUES (?, ?)',
                       (song['id'], json.dumps(song, ensure_ascii=False)))
        else:
            db.execute('DELETE FROM favorites WHERE id = ?', (song['id'],))
    return {'ok': True, 'favorite': favorite, 'title': song['title']}


@mcp.tool()
def yiyi_favorites_list() -> dict:
    """读取依依收藏的歌曲。选中歌曲后调用设备 self.music.play_song。"""
    with database() as db:
        songs = [json.loads(row['song']) for row in db.execute('SELECT song FROM favorites ORDER BY id')]
    return {'songs': songs, 'count': len(songs)}


@mcp.tool()
def yiyi_memory_save(key: str, value: str) -> dict:
    """用户说记住偏好、纪念日、专属留言时保存，跨重启保留。仅存储，不会设置主动定时播报。"""
    if not key.strip() or len(key) > 120 or len(value) > 4000:
        return {'ok': False, 'message': '名称需为 1–120 字，内容不超过 4000 字'}
    timestamp = datetime.now(ZoneInfo('Asia/Shanghai')).isoformat()
    with database() as db:
        db.execute('INSERT OR REPLACE INTO memories VALUES (?, ?, ?)', (key.strip(), value, timestamp))
    return {'ok': True, 'key': key.strip(), 'updated': timestamp}


@mcp.tool()
def yiyi_memory_recall(query: str = '') -> dict:
    """查询已保存的偏好、纪念日与留言。空查询列出最近 30 项。"""
    with database() as db:
        rows = db.execute('SELECT * FROM memories WHERE instr(key, ?) > 0 OR instr(value, ?) > 0 ORDER BY updated DESC LIMIT 30',
                          (query, query)).fetchall()
    return {'memories': [dict(row) for row in rows]}


@mcp.tool()
def yiyi_scene_music(scene: str) -> dict:
    """为学习、放松、睡眠场景选择真实曲库歌曲；定时停止请另调用 yiyi_player_control 的 sleep。"""
    tags = {'学习': ['学习', '纯音乐'], '放松': ['放松', '温柔'], '睡眠': ['睡眠', '雨声']}
    selected = [s for s in catalog() if set(tags.get(scene, [scene])) & set(s.get('tags', []))]
    return {'scene': scene, 'songs': [dict(s, device_call=device_instruction(s)) for s in selected],
            'timer_started': False, 'note': '选择歌曲后调用设备工具；定时停止使用 yiyi_player_control(action="sleep",seconds=秒数)。'}


@mcp.tool()
def yiyi_current_time() -> dict:
    """返回中国标准时间和日期，用于纪念日、天气或日常陪伴对话。"""
    now = datetime.now(ZoneInfo('Asia/Shanghai'))
    return {'time': now.isoformat(), 'date': now.date().isoformat(), 'timezone': 'Asia/Shanghai',
            'weekday': ['星期一', '星期二', '星期三', '星期四', '星期五', '星期六', '星期日'][now.weekday()]}


@mcp.tool()
def yiyi_weather(city: str = '', days: int = 3, latitude: float | None = None,
                 longitude: float | None = None) -> dict:
    """查询指定城市天气和未来1–7天预报。必须先询问用户城市；同名地点请确认候选后传经纬度。"""
    if latitude is not None and longitude is not None and city.strip():
        return weather_at(latitude, longitude, city, days)
    return weather(city, days)


@mcp.tool()
def yiyi_hot_news(platform: str = '综合', limit: int = 5) -> dict:
    """从微博、百度、知乎热搜获取新闻线索；综合读取微博和百度。返回排名、标题、原链接及更新时间，不编造新闻正文。"""
    return hot_news(platform, limit)


@mcp.tool()
def yiyi_player_control(action: str = 'status', seconds: int = 0) -> dict:
    """真实控制设备：pause暂停保留进度、resume续播、stop停止、next下一首、previous上一首、sleep在seconds秒后停止、cancel_sleep取消定时、status状态。需设备联网并停留在音乐小智应用；queued代表待设备执行。"""
    return companion.control(action, seconds)


@mcp.tool()
def yiyi_command_result(command_id: int) -> dict:
    """查询设备控制指令是否已经执行。acked非空代表设备已确认，result给出实际结果；超时不宣称成功。"""
    return companion.command_result(command_id)


@mcp.tool()
def yiyi_playlist_manage(action: str = 'list', name: str = '',
                         songs: list[dict[str, str]] | None = None,
                         from_favorites: bool = False) -> dict:
    """持久歌单：list查看；save覆盖创建；append追加；delete删除。songs项包含song_name、artist_name，可填已缓存id。单次最多准备5首；更多可分批append。from_favorites可直接用本地收藏。保存不会自动播放。"""
    if action == 'list':
        return companion.list_playlists(name)
    if action == 'delete':
        return companion.delete_playlist(name)
    if action not in ('save', 'append'):
        return {'ok': False, 'message': '支持 list/save/append/delete'}
    if from_favorites:
        items = yiyi_favorites_list()['songs']
    else:
        if not songs or len(songs) > 5:
            return {'ok': False, 'message': '单次请提供 1–5 首歌曲，更多可分批追加'}
        items, errors = [], []
        for item in songs:
            song_name = item.get('song_name', item.get('id', ''))
            artist = item.get('artist_name', '')
            song = find_song(song_name, artist)
            if song is None:
                try:
                    song = prepare(song_name, artist)
                except KugouError as error:
                    errors.append({'song': song_name, 'message': str(error)})
                    continue
            items.append(song)
        if errors:
            return {'ok': False, 'message': '部分歌曲未准备完成，歌单未修改；可重试或排除失败歌曲。', 'errors': errors}
    return companion.save_playlist(name, items, append=action == 'append')


@mcp.tool()
def yiyi_playlist_play(name: str, mode: str = 'sequence') -> dict:
    """开始自动连续播放已保存歌单。sequence顺序播放一次，loop列表循环，shuffle随机循环，repeat_one单曲循环；下一首/上一首/暂停/续播用yiyi_player_control。"""
    return companion.play_playlist(name, mode)


@mcp.tool()
def yiyi_reminder_create(text: str, when: str = '', delay_seconds: int = 0,
                         repeat: str = 'once', interval_minutes: int = 0) -> dict:
    """创建真实主动提醒，先查询北京时间。when为明确ISO日期时间；delay_seconds为多久后；repeat支持once一次、daily每天、weekdays工作日、interval每interval_minutes分钟。到时设备响铃、显示文字并尝试朗读；对话中延后。只确认工具实际返回的结果。"""
    # Validate before making any external TTS request or committing a reminder.
    validation = companion.create_reminder(text, when, delay_seconds, repeat, interval_minutes, validate_only=True)
    if not validation['ok']:
        return validation
    voice = prepare_voice(text)
    return companion.create_reminder(text, when, delay_seconds, repeat, interval_minutes, voice)


@mcp.tool()
def yiyi_reminders_list() -> dict:
    """列出持久保存的提醒，包含北京时间、重复规则、启用状态和最近投递指令。"""
    return companion.reminders()


@mcp.tool()
def yiyi_reminder_cancel(reminder_id: str) -> dict:
    """按提醒id取消提醒及尚未投递的响铃。先列出提醒以确定id。"""
    return companion.cancel_reminder(reminder_id)


if __name__ == '__main__':
    mcp.run(transport='stdio')

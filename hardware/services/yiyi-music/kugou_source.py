"""Search Kugou and prepare standard audio for the ESP32's MP3 decoder."""
import html
import json
import os
import re
import subprocess
import tempfile
import threading
import time
from pathlib import Path
from urllib.error import URLError
from urllib.parse import urlsplit
from urllib.request import HTTPRedirectHandler, Request, build_opener

from kugou_client import api, KugouError
from music_library import ROOT, find_song

LOCK = threading.Lock()
CACHE_LIMIT = 300 * 1024 * 1024


def clean(value):
    return html.unescape(re.sub(r'<[^>]*>', '', str(value or ''))).strip()


def online_search(query, limit=5):
    if not query.strip() or len(query) > 160:
        raise KugouError('请提供不超过 160 字的歌名或歌手')
    result = api('search', {'keywords': query, 'pagesize': max(1, min(int(limit), 10))})
    if result.get('status') != 1:
        raise KugouError('酷狗搜索暂时不可用')
    songs = []
    for item in result.get('data', {}).get('lists', []):
        song_hash = item.get('FileHash', '')
        if not re.fullmatch(r'[A-Fa-f0-9]{32}', song_hash):
            continue
        songs.append({'hash': song_hash.lower(), 'title': clean(item.get('SongName')),
                      'artist': clean(item.get('SingerName')), 'album_id': str(item.get('AlbumID', '0')),
                      'album_audio_id': str(item.get('MixSongID', '0')),
                      'duration': int(item.get('Duration') or 0), 'ready': False})
    return songs


def validate_url(url):
    parsed = urlsplit(url)
    host = (parsed.hostname or '').lower()
    domains = ('kugou.com', 'kugou.net', 'kugoumusic.com', 'kgimg.com')
    if parsed.scheme not in ('https', 'http') or parsed.username or parsed.password or \
            not any(host == d or host.endswith('.' + d) for d in domains):
        raise KugouError('酷狗返回了不支持的音频地址')


class SafeRedirect(HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        validate_url(newurl)
        return super().redirect_request(req, fp, code, msg, headers, newurl)


def download(url, destination):
    validate_url(url)
    deadline = time.monotonic() + 50
    try:
        with build_opener(SafeRedirect()).open(Request(url, headers={
                'User-Agent': 'Mozilla/5.0'}), timeout=15) as response, destination.open('wb') as out:
            total = 0
            while True:
                chunk = response.read(65536)
                if not chunk:
                    break
                total += len(chunk)
                if total > 32 * 1024 * 1024 or time.monotonic() > deadline:
                    raise KugouError('音频过大或下载超时，请换一首歌')
                out.write(chunk)
            if total < 1024:
                raise KugouError('酷狗未返回有效音频')
    except (URLError, TimeoutError, OSError):
        raise KugouError('酷狗音频下载失败，请稍后重试') from None


def lyric(song):
    try:
        found = api('search/lyric', {'hash': song['hash'], 'keywords': song['title'],
                                    'duration': song['duration'] * 1000})
        candidates = found.get('candidates') or found.get('data', {}).get('candidates', [])
        if not candidates:
            return ''
        item = candidates[0]
        result = api('lyric', {'id': item['id'], 'accesskey': item['accesskey'],
                               'fmt': 'lrc', 'decode': True})
        return str(result.get('decodeContent', ''))[:200000]
    except (KugouError, KeyError, TypeError, ValueError):
        return ''


def prepare(song_name, artist_name='', song_hash=''):
    ready = find_song('kg-' + song_hash.lower() if song_hash else song_name, artist_name)
    if ready:
        return ready
    choices = online_search(' '.join([song_name, artist_name]).strip(), 10)
    if song_hash:
        choices = [s for s in choices if s['hash'] == song_hash.lower()]
    if not choices:
        raise KugouError('酷狗没有找到匹配歌曲，请换个歌名搜索')
    return prepare_track(choices[0])


def prepare_track(song):
    """Prepare the exact recording returned by a search or account playlist."""
    song = dict(song)
    song['hash'] = str(song.get('hash', '')).lower()
    if not re.fullmatch(r'[a-f0-9]{32}', song['hash']) or not song.get('title'):
        raise KugouError('酷狗歌单歌曲信息不完整，未改用其他版本')
    ready = find_song('kg-' + song['hash'])
    if ready:
        return ready
    if song['duration'] <= 0 or song['duration'] > 1200:
        raise KugouError('当前支持 20 分钟以内的音乐')
    key = 'kg-' + song['hash']
    cache = ROOT / 'data/kugou'
    for name in ('media', 'lyrics', 'catalog', 'work'):
        (cache / name).mkdir(parents=True, exist_ok=True)
    # One encoder at a time, including across MCP child-process restarts.
    with LOCK:
        import fcntl
        with (cache / 'prepare.lock').open('a') as lock:
            fcntl.flock(lock, fcntl.LOCK_EX)
            ready = find_song(key)
            if ready:
                return ready
            used = sum(p.stat().st_size for p in (cache / 'media').glob('*.mp3'))
            if used >= CACHE_LIMIT:
                raise KugouError('音乐缓存已达 300MB，请先整理曲库')
            response = api('song/url', {'hash': song['hash'], 'album_id': song['album_id'],
                                       'album_audio_id': song['album_audio_id'],
                                       'quality': 128, 'free_part': False})
            urls = response.get('url') or response.get('data', {}).get('url')
            if isinstance(urls, str):
                urls = [urls]
            if not urls:
                code = response.get('errcode', response.get('error_code', 'unknown'))
                if code == 20028:
                    raise KugouError('酷狗要求账号安全验证；当前无法准备这首歌，请在酷狗 App 检查账号后重试')
                raise KugouError('酷狗没有授权可用音源，请先扫码登录并检查此歌曲的播放权限')
            if response.get('is_free_part') or response.get('isFreePart'):
                raise KugouError('该音源只有试听片段，未加入完整歌曲曲库')
            with tempfile.TemporaryDirectory(dir=cache / 'work') as work:
                raw, encoded = Path(work) / 'input.audio', Path(work) / 'output.mp3'
                download(urls[0], raw)
                try:
                    subprocess.run([str(ROOT / 'bin/ffmpeg'), '-nostdin', '-v', 'error', '-y',
                                    '-threads', '1', '-protocol_whitelist', 'file,pipe',
                                    '-i', str(raw), '-vn', '-ac', '1', '-ar', '16000',
                                    '-codec:a', 'libmp3lame', '-b:a', '48k', '-threads', '1',
                                    '-map_metadata', '-1', str(encoded)],
                                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                                   timeout=55, check=True)
                except (subprocess.SubprocessError, OSError):
                    raise KugouError('音源格式无法转换为设备支持的 MP3') from None
                if encoded.stat().st_size < 1024:
                    raise KugouError('转换后的音频无效')
                if used + encoded.stat().st_size > CACHE_LIMIT:
                    raise KugouError('加入此歌曲将超过300MB缓存上限，请先整理曲库')
                os.replace(encoded, cache / 'media' / (key + '.mp3'))
            lrc = lyric(song)
            if lrc:
                (cache / 'lyrics' / (key + '.lrc')).write_text(lrc, encoding='utf-8')
            item = {'id': key, 'title': song['title'], 'artist': song['artist'],
                    'media': key + '.mp3', 'lyrics': key + '.lrc' if lrc else '',
                    'duration': song['duration'], 'source': 'kugou', 'tags': [],
                    'prepared_at': int(time.time())}
            target = cache / 'catalog' / (key + '.json')
            temporary = target.with_suffix('.tmp')
            temporary.write_text(json.dumps(item, ensure_ascii=False), encoding='utf-8')
            os.replace(temporary, target)
            return item

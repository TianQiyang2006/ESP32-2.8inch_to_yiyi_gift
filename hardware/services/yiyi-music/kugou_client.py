"""Private loopback requests. Credentials and signed URLs never leave this module."""
import json
import re
import secrets
import time
import uuid
from urllib.error import HTTPError, URLError
from urllib.request import Request, urlopen

from music_library import ROOT


class KugouError(RuntimeError):
    pass


def new_cookie():
    guid = uuid.uuid4().hex
    return {'KUGOU_API_GUID': guid, 'KUGOU_API_MID': str(secrets.randbelow(10**18)),
            'KUGOU_API_DEV': secrets.token_hex(5).upper(),
            'KUGOU_API_MAC': '02:00:00:00:00:00',
            'KUGOU_API_WEBGL': str(secrets.randbits(64))}


def saved_cookie():
    path = ROOT / 'secrets/kugou-credential.json'
    if path.exists():
        return json.loads(path.read_text(encoding='utf-8'))['cookie']
    return new_cookie()


def api(route, params=None, cookie=None):
    cookie = saved_cookie() if cookie is None else cookie
    body = json.dumps(dict(params or {}, cookie=cookie)).encode()
    request = Request('http://127.0.0.1:2235/' + route, data=body,
                      headers={'Content-Type': 'application/json'})
    try:
        with urlopen(request, timeout=23) as response:
            result = json.load(response)
    except (HTTPError, URLError, TimeoutError, ValueError):
        raise KugouError('酷狗接口暂时不可用，请稍后重试') from None
    for value in result.get('cookie', []):
        first = value.split(';', 1)[0]
        if '=' in first:
            key, value = first.split('=', 1)
            cookie[key] = value
    return result.get('body', {})


def status():
    cookie = saved_cookie()
    try:
        with urlopen('http://127.0.0.1:2235/health', timeout=2) as response:
            healthy = bool(json.load(response).get('ok'))
    except (URLError, TimeoutError, ValueError):
        healthy = False
    return {'source': 'kugou', 'available': healthy,
            'credential_saved': bool(cookie.get('token') and cookie.get('userid')),
            'note': '保存登录不代表所有歌曲都有播放权限；准备歌曲时会实际检查。'}


def playlists():
    """Return a small, read-only view of the authenticated account's playlists."""
    result = api('user/playlist', {'page': 1, 'pagesize': 100})
    if result.get('status') != 1:
        raise KugouError('酷狗暂时无法读取账号歌单，请检查登录状态')
    data = result.get('data') or {}
    rows = data.get('info') or []
    return {
        'playlists': [
            {'name': str(row.get('name') or ''),
             'count': int(row.get('count') or row.get('m_count') or 0),
             'list_id': str(row.get('listid') or ''),
             'type': int(row.get('type') or 0),
             'is_default_favorites': str(row.get('is_def')) == '2'}
            for row in rows if row.get('listid') is not None and row.get('name')
        ],
        'total': int(data.get('list_count') or len(rows)),
    }


def playlist_tracks(list_id, page=1, page_size=5, playlist_type=0):
    """Read one bounded page of tracks from an account playlist."""
    result = api('playlist/track/all/new', {
        'listid': str(list_id), 'page': int(page), 'pagesize': int(page_size),
        'type': int(playlist_type),
    })
    if result.get('status') != 1:
        raise KugouError('酷狗暂时无法读取这个歌单的歌曲')
    data = result.get('data') or {}
    tracks = []
    for row in data.get('info') or []:
        title = str(row.get('songname') or row.get('filename') or row.get('name') or '').strip()
        title = re.sub(r'\.(mp3|flac|wav|ogg|m4a)$', '', title, flags=re.IGNORECASE)
        singers = row.get('singerinfo') or []
        artist = '、'.join(str(s.get('name') or '').strip() for s in singers
                           if isinstance(s, dict) and s.get('name'))
        if not artist:
            artist = str(row.get('singername') or '').strip()
        if artist and title.casefold().startswith((artist + ' - ').casefold()):
            title = title[len(artist) + 3:]
        elif not artist and ' - ' in title:
            artist, title = title.split(' - ', 1)
        song_hash = str(row.get('hash') or '').lower()
        if not title or not re.fullmatch(r'[a-f0-9]{32}', song_hash):
            continue
        tracks.append({'hash': song_hash, 'title': title, 'artist': artist,
                       'album_id': str(row.get('album_id') or (row.get('albuminfo') or {}).get('id') or '0'),
                       'album_audio_id': str(row.get('mixsongid') or '0'),
                       'duration': max(0, int(row.get('timelen') or 0) // 1000)})
    return {'tracks': tracks, 'total': int(data.get('count') or 0),
            'page': int(data.get('page') or page), 'page_size': int(data.get('pagesize') or page_size)}

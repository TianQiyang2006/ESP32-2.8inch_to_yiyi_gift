"""Shared access to local tracks and fully prepared online-source tracks."""
import json
import os
import random
import re
import unicodedata
from pathlib import Path
from urllib.parse import quote

ROOT = Path(os.environ.get('YIYI_MUSIC_ROOT', '/opt/yiyi-music'))


def config():
    return json.loads((ROOT / 'secrets/config.json').read_text(encoding='utf-8'))


def norm(text):
    return re.sub(r'\s+', '', unicodedata.normalize('NFKC', str(text)).casefold())


def catalog():
    items = json.loads((ROOT / 'library/catalog.json').read_text(encoding='utf-8'))
    for index in sorted((ROOT / 'data/kugou/catalog').glob('kg-*.json')):
        if index.is_symlink():
            continue
        try:
            items.append(json.loads(index.read_text(encoding='utf-8')))
        except (OSError, ValueError):
            continue
    valid = []
    for item in items:
        name = item.get('media', '')
        if not name or Path(name).name != name or not name.endswith('.mp3'):
            continue
        path = media_file('media', name)
        if path is not None:
            valid.append(item)
    return valid


def media_file(folder, name):
    if folder not in ('media', 'lyrics') or not name or Path(name).name != name:
        return None
    for base in (ROOT / 'library', ROOT / 'data/kugou'):
        root = base / folder
        path = root / name
        if path.is_file() and not path.is_symlink() and path.resolve().parent == root.resolve():
            return path
    return None


def search(query='', limit=10):
    q = norm(query)
    return [s for s in catalog() if not q or q in norm(' '.join(
        [s['title'], s.get('artist', ''), *s.get('aliases', []), *s.get('tags', [])]
    ))][:max(1, min(int(limit), 30))]


def find_song(query, artist=''):
    songs = catalog()
    q, a = norm(query), norm(artist)
    if not q:
        return None
    if q in ('随机', '随机播放', 'random'):
        return random.choice(songs) if songs else None
    songs = [s for s in songs if not a or a in norm(s.get('artist', ''))]
    exact = [s for s in songs if q in [norm(s['title']), norm(s['id']),
                                     *map(norm, s.get('aliases', []))]]
    matches = exact or [s for s in songs if q in norm(s['title'])]
    return matches[0] if matches else None


def public_song(song):
    item = dict(song)
    base = config()['public_base'].rstrip('/')
    item['audio_url'] = base + '/media/' + quote(song['media'])
    item['lyric_url'] = base + '/lyrics/' + quote(song['lyrics']) if song.get('lyrics') else ''
    return item

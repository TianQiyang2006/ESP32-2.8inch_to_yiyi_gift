"""Bounded, timestamped weather and hot-search readers; no account cookies."""
import html
import json
import threading
import time
from datetime import datetime
from urllib.error import URLError
from urllib.parse import urlencode, urlsplit
from urllib.request import Request, urlopen
from zoneinfo import ZoneInfo

TZ = ZoneInfo('Asia/Shanghai')
LOCK = threading.Lock()
CACHE = {}
WEATHER_CODES = {
    0: '晴', 1: '晴间多云', 2: '多云', 3: '阴', 45: '雾', 48: '雾凇',
    51: '小毛毛雨', 53: '毛毛雨', 55: '强毛毛雨', 56: '冻毛毛雨', 57: '强冻毛毛雨',
    61: '小雨', 63: '中雨', 65: '大雨', 66: '冻雨', 67: '强冻雨',
    71: '小雪', 73: '中雪', 75: '大雪', 77: '米雪',
    80: '小阵雨', 81: '阵雨', 82: '强阵雨', 85: '阵雪', 86: '强阵雪',
    95: '雷雨', 96: '雷雨伴冰雹', 99: '强雷雨伴冰雹',
}


def stamp():
    return datetime.now(TZ).isoformat(timespec='seconds')


def fetch_json(url, ttl=300):
    with LOCK:
        cached = CACHE.get(url)
        if cached and time.time() - cached[0] < ttl:
            return cached[1]
    req = Request(url, headers={'User-Agent': 'YiyiCompanion/0.4', 'Accept': 'application/json'})
    with urlopen(req, timeout=12) as response:
        raw = response.read(2 * 1024 * 1024 + 1)
    if len(raw) > 2 * 1024 * 1024:
        raise ValueError('response too large')
    data = json.loads(raw)
    with LOCK:
        if len(CACHE) >= 64:
            CACHE.clear()
        CACHE[url] = (time.time(), data)
    return data


def weather(city='', days=3):
    city = city.strip()
    if not city:
        return {'ok': False, 'needs_city': True, 'message': '请先询问用户要查询哪个城市；不要猜测位置。'}
    if len(city) > 100:
        return {'ok': False, 'message': '城市名称过长'}
    try:
        normalize = lambda s: str(s).removesuffix('市').removesuffix('县').casefold()
        places = fetch_json('https://geocoding-api.open-meteo.com/v1/search?' +
                            urlencode({'name': normalize(city), 'count': 10, 'language': 'zh', 'format': 'json'}), 86400).get('results', [])
        # Match Chinese administrative suffixes without silently choosing a
        # different city. Same-name places are returned for user selection.
        # The request asks for a city. Prefer administrative city records over
        # same-name villages; keep multiple cities as explicit candidates.
        cities = [p for p in places if p.get('feature_code', '').startswith(('PPLC', 'PPLA'))]
        if cities:
            places = cities
        exact = [p for p in places if normalize(p.get('name', '')) == normalize(city)]
        if exact:
            places = exact
        if not places:
            return {'ok': False, 'needs_city': True, 'message': '未找到城市，请补充省份或城市的英文名称。'}
        if len(places) > 1:
            return {'ok': False, 'needs_city_selection': True,
                    'candidates': [{k: p.get(k) for k in ('name', 'admin1', 'country', 'latitude', 'longitude')} for p in places],
                    'message': '存在同名地点，请向用户确认后，用经纬度查询。'}
        p = places[0]
        return weather_at(p['latitude'], p['longitude'],
                          '，'.join(str(p[k]) for k in ('name', 'admin1', 'country') if p.get(k)), days)
    except (URLError, TimeoutError, ValueError, KeyError, OSError):
        return {'ok': False, 'message': '天气数据源暂时不可用，请稍后重试。'}


def weather_at(latitude, longitude, place, days=3):
    if not (-90 <= float(latitude) <= 90 and -180 <= float(longitude) <= 180):
        return {'ok': False, 'message': '经纬度无效'}
    args = {
        'latitude': latitude, 'longitude': longitude, 'timezone': 'Asia/Shanghai',
        'forecast_days': max(1, min(int(days), 7)),
        'current': 'temperature_2m,relative_humidity_2m,apparent_temperature,precipitation,weather_code,wind_speed_10m',
        'daily': 'weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max',
    }
    try:
        data = fetch_json('https://api.open-meteo.com/v1/forecast?' + urlencode(args), 600)
        c, d = data['current'], data['daily']
        daily = [{'date': day, 'condition': WEATHER_CODES.get(d['weather_code'][i], '未知'),
                  'high_c': d['temperature_2m_max'][i], 'low_c': d['temperature_2m_min'][i],
                  'rain_probability_percent': d['precipitation_probability_max'][i]}
                 for i, day in enumerate(d['time'])]
        return {'ok': True, 'place': place, 'source': 'Open-Meteo', 'source_url': 'https://open-meteo.com/',
                'fetched_at': stamp(), 'timezone': 'Asia/Shanghai',
                'current': {'time': c['time'], 'temperature_c': c['temperature_2m'],
                            'feels_like_c': c['apparent_temperature'], 'humidity_percent': c['relative_humidity_2m'],
                            'precipitation_mm': c['precipitation'], 'wind_kmh': c['wind_speed_10m'],
                            'condition': WEATHER_CODES.get(c['weather_code'], '未知')}, 'daily': daily}
    except (URLError, TimeoutError, ValueError, KeyError, OSError):
        return {'ok': False, 'message': '天气预报暂时不可用，请稍后重试。'}


def hot_news(platform='综合', limit=8):
    sources = {'微博': 'weibo', '百度': 'baidu', '知乎': 'zhihu'}
    aliases = {'weibo': '微博', 'baidu': '百度', 'zhihu': '知乎', '综合': '综合', 'all': '综合'}
    platform = aliases.get(platform, platform)
    selected = list(sources)[:2] if platform == '综合' else [platform]
    if any(p not in sources for p in selected):
        return {'ok': False, 'message': '支持综合、微博、百度、知乎热搜'}
    results, errors = [], []
    limit = max(1, min(int(limit), 20))
    for name in selected:
        try:
            data = fetch_json('https://uapis.cn/api/v1/misc/hotboard?' + urlencode({'type': sources[name]}), 300)
            items = []
            for row in data.get('list', [])[:limit]:
                title, url = html.unescape(str(row.get('title', '')))[:300], str(row.get('url', ''))
                if not title or urlsplit(url).scheme not in ('http', 'https'):
                    continue
                items.append({'rank': row.get('index', len(items) + 1), 'title': title,
                              'url': url, 'hot': str(row.get('hot_value', row.get('hot', '')))[:60]})
            if not items:
                raise ValueError('empty hotboard')
            results.append({'platform': name, 'updated_at': data.get('update_time'), 'items': items})
        except (URLError, TimeoutError, ValueError, KeyError, TypeError, OSError):
            errors.append(name)
    return {'ok': bool(results), 'fetched_at': stamp(), 'source': '各平台热搜，经 UApi 聚合接口读取',
            'boards': results, 'unavailable': errors,
            'note': '这是热搜标题及原链接，不等于已核实的完整新闻；播报时注明平台，不编造正文。'}

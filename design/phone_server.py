"""LAN companion for the HTML designer. Run: python design/phone_server.py.

Phone changes are authenticated, persisted and polled by the main preview.
The preview remains on port 8765; this service uses 8766 (optional TLS 8767).
"""
from pathlib import Path
from http.server import ThreadingHTTPServer, BaseHTTPRequestHandler
from urllib.parse import urlparse, parse_qs, urlencode
from urllib.request import Request, urlopen
from urllib.error import HTTPError
import argparse, copy, datetime, io, json, math, re, secrets, socket, ssl, sys, threading, time

ROOT = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT / '.local' / 'python'))
STORE = ROOT / '.local' / 'phone-state.json'
LOCK = threading.RLock()
STATE = {'token': '', 'config': {}, 'revision': 0, 'lastSeen': 0, 'created': 0, 'location': None}
LOCATION_GENERATION = 0
FIELDS = ['letter', 'keepsake', 'clockMode', 'deviceSettings', 'daily', 'holiday', 'reading', 'colors', 'radius', 'countdown', 'weather']

def lan_ip():
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
            sock.connect(('8.8.8.8', 80))
            return sock.getsockname()[0]
    except OSError:
        return '127.0.0.1'

def save():
    STORE.parent.mkdir(exist_ok=True)
    temporary = STORE.with_suffix('.tmp')
    temporary.write_text(json.dumps(STATE, ensure_ascii=False), encoding='utf-8')
    temporary.replace(STORE)

def number(value, lo, hi):
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value) or not lo <= value <= hi:
        raise ValueError('数值超出范围')
    return value

def validate_patch(patch):
    if not isinstance(patch, dict) or not patch or set(patch) - set(FIELDS):
        raise ValueError('设置格式无效')
    for key, value in patch.items():
        if key == 'countdown' and value is None:
            continue
        if key == 'radius':
            number(value, 0, 32)
            continue
        if not isinstance(value, dict):
            raise ValueError('设置格式无效')
        if key in ['letter', 'keepsake']:
            for field, limit in [('title', 80), ('text', 3000), ('signature', 80)]:
                if field in value and (not isinstance(value[field], str) or len(value[field]) > limit):
                    raise ValueError('留言超出长度限制')
        if key == 'keepsake' and 'date' in value and not isinstance(value['date'], str): raise ValueError('纪念日期无效')
        if key == 'keepsake' and value.get('date'):
            if not isinstance(value['date'], str) or not re.fullmatch(r'\d{4}-\d{2}-\d{2}', value['date']): raise ValueError('纪念日期无效')
            target = datetime.datetime.strptime(value['date'], '%Y-%m-%d')
            if not 1900 <= target.year <= 2099: raise ValueError('纪念日期无效')
        if key == 'clockMode' and value.get('preset', 'landscape') not in ['landscape', 'matrix', 'flip']:
            raise ValueError('请选择有效时钟预设')
        if key == 'deviceSettings':
            if 'brightness' in value: number(value['brightness'], 10, 100)
            if 'volume' in value: number(value['volume'], 0, 100)
        if key == 'daily' and 'waterGoal' in value: number(value['waterGoal'], 100, 10000)
        if key == 'holiday' and 'weekday' in value: number(value['weekday'], 0, 6)
        if key == 'countdown':
            target = datetime.datetime.strptime(value.get('date', ''), '%Y-%m-%dT%H:%M')
            if not 1900 <= target.year <= 2099 or not isinstance(value.get('name'), str) or not 1 <= len(value['name']) <= 40:
                raise ValueError('倒计时名称或日期无效')
        if key == 'reading':
            for field in ['quoteMode', 'poemMode']:
                if field in value and value[field] not in ['api', 'manual']: raise ValueError('内容来源无效')
        for field, item in value.items():
            if field in ['autoHome', 'seconds', 'sound', 'autoUpdate'] and not isinstance(item, bool):
                raise ValueError('开关设置无效')
            if field.endswith('Color') or field.endswith('Ink') or field in ['background', 'bg', 'header', 'ink', 'accent', 'muted', 'surface']:
                if not isinstance(item, str) or not re.fullmatch(r'#[0-9a-fA-F]{6}', item): raise ValueError('颜色格式无效')
            if isinstance(item, str) and len(item) > (24000 if field == 'poemText' else 3000): raise ValueError('文字过长')
    return patch

def merge(patch):
    for key, value in patch.items():
        if isinstance(value, dict) and isinstance(STATE['config'].get(key), dict):
            STATE['config'][key].update(value)
        else:
            STATE['config'][key] = value
    STATE['revision'] += 1
    save()

def remote_json(url):
    with urlopen(Request(url, headers={'User-Agent': 'YiyiDesk/1.0 (personal weather display)'}), timeout=15) as response:
        return json.load(response)

def fetch_weather(latitude=None, longitude=None, city=''):
    if not isinstance(city, str) or len(city.strip()) > 40:
        raise ValueError('请输入 1～40 字的城市名称')
    city = city.strip()
    if not city:
        number(latitude, -90, 90); number(longitude, -180, 180)
        place = remote_json('https://nominatim.openstreetmap.org/reverse?' + urlencode({'lat': latitude, 'lon': longitude, 'format': 'jsonv2', 'accept-language': 'zh-CN', 'zoom': 10}))
        address = place.get('address', {})
        city = next((address[key] for key in ['city', 'town', 'county', 'state'] if address.get(key)), '')
        if not city:
            raise ValueError('无法解析当前位置，请直接输入中文城市名称')
    query = {'city': city, 'extended': 'true', 'forecast': 'true', 'lang': 'zh'}
    payload = remote_json('https://uapis.cn/api/v1/misc/weather?' + urlencode(query))
    return parse_weather(payload, city)


def parse_weather(payload, city):
    def optional(value, low, high):
        return value if isinstance(value, (int, float)) and not isinstance(value, bool) and math.isfinite(value) and low <= value <= high else None
    def text(value):
        return value.strip() if isinstance(value, str) else ''
    def clock(value):
        return value if isinstance(value, str) and re.fullmatch(r'([01]\d|2[0-3]):[0-5]\d', value) else None
    if not isinstance(payload, dict) or optional(payload.get('temperature'), -99, 99) is None or not text(payload.get('weather')):
        raise ValueError('天气接口未返回有效的温度或天气描述')
    days = []
    for day in payload.get('forecast', []) if isinstance(payload.get('forecast'), list) else []:
        if not isinstance(day, dict): continue
        date = day.get('date')
        try:
            if not isinstance(date, str) or not re.fullmatch(r'\d{4}-\d{2}-\d{2}', date): continue
            datetime.date.fromisoformat(date)
        except ValueError: continue
        days.append(day)
    today = datetime.datetime.now(datetime.timezone(datetime.timedelta(hours=8))).date().isoformat()
    current = next((day for day in days if day['date'] == today), {})
    low = optional(payload.get('temp_min'), -99, 99)
    high = optional(payload.get('temp_max'), -99, 99)
    uv = optional(payload.get('uv'), 0, 30)
    return {'city': text(payload.get('city')) or city, 'condition': text(payload.get('weather')), 'temperature': payload['temperature'],
            'feelsLike': optional(payload.get('feels_like'), -99, 99), 'humidity': optional(payload.get('humidity'), 0, 100),
            'wind': ' '.join(filter(None, [text(payload.get('wind_direction')), text(payload.get('wind_power'))])),
            'low': low if low is not None else optional(current.get('temp_min'), -99, 99),
            'high': high if high is not None else optional(current.get('temp_max'), -99, 99),
            'sunrise': clock(current.get('sunrise')), 'sunset': clock(current.get('sunset')),
            'pressure': optional(payload.get('pressure'), 0, 2000), 'uv': uv if uv is not None else optional(current.get('uv_index'), 0, 30),
            'visibility': optional(payload.get('visibility'), 0, 1000), 'aqi': optional(payload.get('aqi'), 0, 500),
            'aqiLabel': text(payload.get('aqi_category')),
            'forecast': [{'date': day['date'], 'condition': text(day.get('weather_day')), 'low': optional(day.get('temp_min'), -99, 99), 'high': optional(day.get('temp_max'), -99, 99)} for day in days[:7]],
            'updatedAt': int(time.time() * 1000), 'source': 'device'}

class Handler(BaseHTTPRequestHandler):
    def log_message(self, *_):
        pass  # Pairing tokens and phone coordinates are not logged.

    def send_body(self, code, body, mime='application/json; charset=utf-8', headers=None):
        raw = body if isinstance(body, bytes) else json.dumps(body, ensure_ascii=False).encode('utf-8')
        self.send_response(code)
        self.send_header('Content-Type', mime)
        self.send_header('Content-Length', str(len(raw)))
        self.send_header('Cache-Control', 'no-store')
        for name, value in (headers or {}).items():
            self.send_header(name, value)
        origin = self.headers.get('Origin')
        if origin:
            self.send_header('Access-Control-Allow-Origin', origin)
            self.send_header('Access-Control-Expose-Headers', 'Retry-After')
            self.send_header('Vary', 'Origin')
        self.end_headers()
        self.wfile.write(raw)

    def do_OPTIONS(self):
        self.send_response(204)
        self.send_header('Access-Control-Allow-Origin', self.headers.get('Origin', '*'))
        self.send_header('Access-Control-Allow-Methods', 'GET, POST, OPTIONS')
        self.send_header('Access-Control-Allow-Headers', 'Content-Type, X-Yiyi-Token')
        self.end_headers()

    def authorized(self, query):
        token = self.headers.get('X-Yiyi-Token', '') or query.get('token', [''])[0]
        with LOCK:
            return bool(token and STATE['token'] and secrets.compare_digest(token, STATE['token']) and time.time() - STATE['created'] < 43200)

    def do_GET(self):
        target = urlparse(self.path); query = parse_qs(target.query)
        if target.path == '/api/reading/quote':
            # Fixed public endpoint for local file previews; no phone session or
            # arbitrary destination URL is involved.
            if self.client_address[0] not in ['127.0.0.1', '::1']:
                self.send_body(403, {'error': '请在本机设计预览中刷新一言'}); return
            try:
                request = Request('https://uapis.cn/api/v1/saying', headers={'Accept': 'application/json', 'User-Agent': 'YiyiDesk/1.0'})
                with urlopen(request, timeout=8) as response:
                    raw = response.read(32769)
                if len(raw) > 32768:
                    raise ValueError('一言响应过长')
                payload = json.loads(raw)
                if not isinstance(payload, dict) or not isinstance(payload.get('text'), str) or not payload['text'].strip():
                    raise ValueError('一言接口未返回有效内容')
                self.send_body(200, payload)
            except HTTPError as error:
                retry = error.headers.get('Retry-After')
                self.send_body(error.code, {'error': '一言服务暂不可用'}, headers={'Retry-After': retry} if retry else None)
            except Exception as error:
                self.send_body(502, {'error': str(error)})
            return
        if target.path == '/trust.cer' and (ROOT / '.local' / 'phone-root.cer').is_file():
            self.send_body(200, (ROOT / '.local' / 'phone-root.cer').read_bytes(), 'application/x-x509-ca-cert'); return
        if target.path in ['/', '/phone', '/phone.html', '/phone.css', '/phone.js'] or re.fullmatch(r'/assets/clock-[a-z-]+\.jpg', target.path):
            name = 'phone.html' if target.path in ['/', '/phone'] else target.path.lstrip('/')
            file = (ROOT / name).resolve()
            if not file.is_relative_to(ROOT) or not file.is_file():
                self.send_body(404, {'error': '页面不存在'}); return
            import mimetypes
            self.send_body(200, file.read_bytes(), mimetypes.guess_type(file.name)[0] or 'application/octet-stream'); return
        if not self.authorized(query):
            self.send_body(401, {'error': '连接已失效，请重新从设备连接手机'}); return
        if target.path == '/api/qr':
            try:
                import qrcode, qrcode.image.svg
                url = self.server.phone_base + '/phone?token=' + STATE['token']
                output = io.BytesIO(); qrcode.make(url, image_factory=qrcode.image.svg.SvgPathImage).save(output)
                self.send_body(200, output.getvalue(), 'image/svg+xml')
            except ImportError:
                self.send_body(503, {'error': '请使用显示的连接地址'})
            return
        if target.path in ['/api/phone-state', '/api/device-state']:
            with LOCK:
                if target.path == '/api/phone-state': STATE['lastSeen'] = time.time()
                snapshot = {'revision': STATE['revision'], 'connected': time.time() - STATE['lastSeen'] < 20,
                            'hasLocation': bool(STATE.get('location')),
                            'config': copy.deepcopy(STATE['config']) if str(STATE['revision']) != query.get('since', [''])[0] else None}
                if self.server.secure_base:
                    snapshot['secureUrl'] = self.server.secure_base + '/phone?token=' + STATE['token']
                    snapshot['certificateUrl'] = self.server.phone_base + '/trust.cer'
            self.send_body(200, snapshot); return
        if target.path == '/api/search':
            try:
                text = query.get('city', [''])[0].strip()
                if not 1 <= len(text) <= 50: raise ValueError('请输入城市名称')
                result = remote_json('https://geocoding-api.open-meteo.com/v1/search?' + urlencode({'name': text, 'count': 10, 'language': 'zh', 'format': 'json'}))
                self.send_body(200, {'results': result.get('results', [])})
            except Exception as error: self.send_body(502, {'error': str(error)})
            return
        self.send_body(404, {'error': '接口不存在'})

    def do_POST(self):
        global LOCATION_GENERATION
        target = urlparse(self.path); query = parse_qs(target.query)
        try:
            length = int(self.headers.get('Content-Length', '0'))
            if not 0 < length <= 6000000: raise ValueError('请求大小无效')
            data = json.loads(self.rfile.read(length))
            if target.path == '/api/pair':
                origin = self.headers.get('Origin', '')
                if self.client_address[0] not in ['127.0.0.1', '::1'] or origin not in ['', 'null', 'http://127.0.0.1:8765', 'http://localhost:8765']:
                    self.send_body(403, {'error': '请从设计工作台创建连接'}); return
                seed = {key: data['config'][key] for key in FIELDS if key in data.get('config', {})}
                validate_patch(seed)
                with LOCK:
                    LOCATION_GENERATION += 1
                    STATE.update(token=secrets.token_urlsafe(24), config=seed, revision=1, lastSeen=0, created=time.time(), location=None); save()
                    self.send_body(200, {'token': STATE['token'], 'url': self.server.phone_base + '/phone?token=' + STATE['token'], 'revision': 1})
                return
            if not self.authorized(query): self.send_body(401, {'error': '连接已失效，请重新连接'}); return
            if target.path == '/api/disconnect':
                with LOCK:
                    STATE['token'] = ''; STATE['lastSeen'] = 0; STATE['created'] = 0; save()
                self.send_body(200, {'message': '连接已结束'}); return
            if target.path in ['/api/phone-save', '/api/device-sync']:
                patch = validate_patch(data['patch'])
                with LOCK:
                    if target.path == '/api/device-sync' and data.get('revision') != STATE['revision']:
                        self.send_body(409, {'error': '手机设置已更新，请先同步'}); return
                    merge(patch)
                    if target.path == '/api/phone-save': STATE['lastSeen'] = time.time()
                    self.send_body(200, {'message': '已保存到设备', 'revision': STATE['revision']})
                return
            if target.path in ['/api/location', '/api/weather-refresh']:
                with LOCK:
                    if target.path == '/api/weather-refresh':
                        if not STATE.get('location'): raise ValueError('请先在手机上获取定位或选择地区')
                        data = copy.deepcopy(STATE['location'])
                    LOCATION_GENERATION += 1
                    generation, token = LOCATION_GENERATION, STATE['token']
                result = fetch_weather(data.get('latitude'), data.get('longitude'), data.get('city', ''))
                with LOCK:
                    if generation != LOCATION_GENERATION or token != STATE['token']:
                        self.send_body(409, {'error': '位置已变更，旧请求已忽略'}); return
                    merge({'weather': result}); STATE['location'] = {'latitude': data.get('latitude'), 'longitude': data.get('longitude'), 'city': data.get('city') or result['city']}
                    if target.path == '/api/location': STATE['lastSeen'] = time.time()
                    save()
                self.send_body(200, {'weather': result, 'revision': STATE['revision'], 'message': '已更新共享天气'}); return
            self.send_body(404, {'error': '接口不存在'})
        except (ValueError, KeyError, TypeError) as error: self.send_body(400, {'error': str(error)})
        except Exception as error: self.send_body(502, {'error': '更新失败，已有内容保留：' + str(error)})

def main():
    parser = argparse.ArgumentParser(); parser.add_argument('--port', type=int, default=8766)
    parser.add_argument('--cert'); parser.add_argument('--key'); parser.add_argument('--https-port', type=int, default=8767)
    parser.add_argument('--store', type=Path)
    args = parser.parse_args()
    global STORE
    if args.store: STORE = args.store
    if STORE.exists():
        try: STATE.update(json.loads(STORE.read_text(encoding='utf-8')))
        except (ValueError, OSError): pass
    ip = lan_ip(); phone_base = f'http://{ip}:{args.port}'; secure_base = ''
    server = ThreadingHTTPServer(('0.0.0.0', args.port), Handler)
    if args.cert and args.key:
        secure = ThreadingHTTPServer(('0.0.0.0', args.https_port), Handler)
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER); context.load_cert_chain(args.cert, args.key)
        secure.socket = context.wrap_socket(secure.socket, server_side=True)
        secure_base = f'https://{ip}:{args.https_port}'; secure.phone_base = phone_base; secure.secure_base = secure_base
        threading.Thread(target=secure.serve_forever, daemon=True).start()
    server.phone_base = phone_base
    server.secure_base = secure_base
    print(f'Yiyi phone companion: {phone_base} (pair from designer)', flush=True)
    server.serve_forever()

if __name__ == '__main__': main()

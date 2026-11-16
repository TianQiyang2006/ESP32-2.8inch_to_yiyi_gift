#!/usr/bin/env python3
"""Dedicated HTTP MP3/LRC service for the existing ESP32 firmware."""
import hmac
import json
import logging
import os
import re
import socket
import threading
from concurrent.futures import ThreadPoolExecutor
from http.server import BaseHTTPRequestHandler, HTTPServer
from pathlib import Path
from urllib.parse import parse_qs, unquote, urlsplit

from music_library import ROOT, catalog, config, find_song, public_song, media_file
from companion_state import control as player_control
from companion_state import play_playlist, playlist_summaries, sync_device

VERSION = '0.4.4'
LOG = logging.getLogger('yiyi-music')
# Media transfers hold a worker for their whole duration.  Cap them so slow
# readers cannot consume every worker and starve device sync/JSON routes.
STREAM_SLOTS = threading.BoundedSemaphore(int(os.environ.get('YIYI_GATEWAY_MAX_STREAMS', '8')))


class BoundedServer(HTTPServer):
    """Thread-pooled HTTP server with bounded concurrency.

    Long-lived media streams can occupy a worker for minutes.  A reasonably wide
    pool plus a bounded queue keeps a slow client from starving the lightweight
    JSON requests (device sync / health / catalog) used by the device.
    """
    request_queue_size = 32
    max_workers = 12
    max_pending = 24

    def __init__(self, *args, **kwargs):
        self.executor = ThreadPoolExecutor(max_workers=self.max_workers,
                                           thread_name_prefix='yiyi-http')
        self._pending = 0
        self._pending_lock = threading.Lock()
        super().__init__(*args, **kwargs)

    def process_request(self, request, address):
        with self._pending_lock:
            if self._pending >= self.max_pending:
                overloaded = True
            else:
                self._pending += 1
                overloaded = False
        if overloaded:
            LOG.warning('http overload peer=%s; rejecting connection', address[0])
            try:
                request.sendall(b'HTTP/1.0 503 Service Unavailable\r\n'
                                b'Content-Length: 0\r\nConnection: close\r\n\r\n')
            except OSError:
                pass
            self.shutdown_request(request)
            return
        try:
            self.executor.submit(self._handle, request, address)
        except RuntimeError:  # executor already shut down
            with self._pending_lock:
                self._pending -= 1
            self.shutdown_request(request)

    def _handle(self, request, address):
        try:
            try:
                request.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
            except OSError:
                pass
            # The ESP32 writes its request line immediately; a shorter header
            # timeout frees workers sooner when scanners open silent sockets.
            request.settimeout(5)
            self.finish_request(request, address)
        except (ConnectionError, TimeoutError, OSError):
            pass
        except Exception:
            self.handle_error(request, address)
        finally:
            self.shutdown_request(request)
            with self._pending_lock:
                self._pending -= 1

    def handle_error(self, request, client_address):
        LOG.exception('http handler error peer=%s', client_address[0])

    def server_close(self):
        super().server_close()
        self.executor.shutdown(wait=True)


class Handler(BaseHTTPRequestHandler):
    server_version = 'YiyiMusic/' + VERSION

    def log_message(self, fmt, *args):
        # Only request lines are logged this way; the URL contains the private
        # device path and must never be written to the journal.
        status = args[1] if len(args) > 1 else '-'
        LOG.info('http method=%s status=%s peer=%s', getattr(self, 'command', '-'),
                 status, self.client_address[0])

    def log_error(self, fmt, *args):
        # A timeout can happen before parse_request() sets self.command.  The old
        # override raised AttributeError here, aborting the handler and flooding
        # the journal with tracebacks instead of logging the real cause.
        try:
            message = fmt % args
        except (TypeError, ValueError):
            message = str(fmt)
        LOG.warning('http error method=%s peer=%s: %s', getattr(self, 'command', '-'),
                    self.client_address[0], message)

    def send_json(self, value, status=200):
        body = json.dumps(value, ensure_ascii=False).encode('utf-8')
        self.send_response(status)
        self.send_header('Content-Type', 'application/json; charset=utf-8')
        self.send_header('Content-Length', str(len(body)))
        self.send_header('Cache-Control', 'no-store')
        self.send_header('X-Content-Type-Options', 'nosniff')
        self.end_headers()
        if self.command != 'HEAD':
            self.wfile.write(body)

    def do_HEAD(self):
        self.do_GET()

    def do_POST(self):
        parsed = urlsplit(self.path)
        parts = parsed.path.split('/', 2)
        if len(parts) != 3 or not hmac.compare_digest(parts[1], config()['path_key']) or \
                parts[2] not in ('device/sync', 'device/ui/playlists', 'device/ui/control'):
            self.send_json({'error': 'not found'}, 404)
            return
        try:
            length = int(self.headers.get('Content-Length', '0'))
            if not 1 <= length <= 8192:
                raise ValueError
            state = json.loads(self.rfile.read(length))
            if not isinstance(state, dict):
                raise ValueError
            if parts[2] == 'device/sync':
                if not isinstance(state.get('boot'), str):
                    raise ValueError
                self.send_json(sync_device(state))
            elif parts[2] == 'device/ui/playlists':
                name, mode = state.get('name'), state.get('mode', 'sequence')
                if not isinstance(name, str) or not name.strip() or len(name) > 80 or not isinstance(mode, str):
                    raise ValueError
                self.send_json(play_playlist(name.strip(), mode))
            else:
                action = state.get('action')
                if action not in ('next', 'previous'):
                    raise ValueError
                self.send_json(player_control(action))
        except (ValueError, TypeError, OverflowError):
            self.send_json({'error': 'invalid device status'}, 400)
        except Exception:
            LOG.exception('device control failed peer=%s', self.client_address[0])
            self.send_json({'error': 'internal error'}, 500)

    def do_GET(self):
        parsed = urlsplit(self.path)
        if parsed.path == '/health':
            self.send_json({'ok': True, 'service': 'yiyi-music', 'version': VERSION})
            return
        parts = parsed.path.split('/', 2)
        expected = config()['path_key']
        if len(parts) < 3 or not hmac.compare_digest(parts[1], expected):
            self.send_json({'error': 'not found'}, 404)
            return
        path = '/' + unquote(parts[2])
        if path == '/device/ui/playlists':
            self.send_json(playlist_summaries())
        elif path in ('/', '/api/catalog'):
            self.send_json({'songs': [public_song(s) for s in catalog()],
                            'note': '设备播放请调用 self.music.play_song；这里提供曲库与媒体地址。'})
        elif path == '/stream_pcm':
            args = parse_qs(parsed.query)
            song = find_song(args.get('song', [''])[0], args.get('artist', [''])[0])
            if song is None:
                self.send_json({'error': 'song_not_found', 'message': '曲库中没有这首歌'}, 404)
                return
            item = public_song(song)
            self.send_json({k: item.get(k, '') for k in ('artist', 'title', 'audio_url', 'lyric_url')})
        elif path.startswith('/media/'):
            self.serve_file('media', path[7:], 'audio/mpeg')
        elif path.startswith('/lyrics/'):
            self.serve_file('lyrics', path[8:], 'text/plain; charset=utf-8')
        elif path.startswith('/alerts/'):
            name = path[8:]
            if not re.fullmatch(r'[a-f0-9]{32}\.p3', name):
                self.send_json({'error': 'not found'}, 404)
                return
            self.serve_file('alerts', name, 'application/octet-stream')
        else:
            self.send_json({'error': 'not found'}, 404)

    def serve_file(self, folder, name, content_type):
        if folder == 'alerts':
            target = ROOT / 'data/alerts' / name
            if not target.is_file() or target.is_symlink():
                target = None
        else:
            target = media_file(folder, name)
        if target is None:
            self.send_json({'error': 'file not found'}, 404)
            return
        slot = None
        if self.command != 'HEAD':
            slot = STREAM_SLOTS.acquire(blocking=False)
            if not slot:
                LOG.warning('media stream busy peer=%s', self.client_address[0])
                self.send_json({'error': 'busy'}, 503)
                return
        try:
            try:
                handle = target.open('rb')
            except OSError:
                self.send_json({'error': 'file not found'}, 404)
                return
            with handle:
                size = os.fstat(handle.fileno()).st_size
                start, end, status = 0, size - 1, 200
                value = self.headers.get('Range')
                if value:
                    match = re.fullmatch(r'bytes=(\d*)-(\d*)', value)
                    try:
                        if not match or not any(match.groups()):
                            raise ValueError
                        left, right = match.groups()
                        if left:
                            start, end = int(left), int(right) if right else size - 1
                        else:
                            suffix = int(right)
                            if suffix <= 0:
                                raise ValueError
                            start = max(0, size - suffix)
                        if start < 0 or start >= size or end < start:
                            raise ValueError
                        end, status = min(end, size - 1), 206
                    except ValueError:
                        self.send_response(416)
                        self.send_header('Content-Range', f'bytes */{size}')
                        self.send_header('Content-Length', '0')
                        self.end_headers()
                        return
                length = max(0, end - start + 1)
                self.send_response(status)
                self.send_header('Content-Type', content_type)
                self.send_header('Content-Length', str(length))
                self.send_header('Accept-Ranges', 'bytes')
                self.send_header('X-Content-Type-Options', 'nosniff')
                if status == 206:
                    self.send_header('Content-Range', f'bytes {start}-{end}/{size}')
                self.end_headers()
                if self.command == 'HEAD':
                    return
                # Slow ESP32 links need a longer timeout than request headers
                # allow.  Small writes bound each send's deadline.
                self.connection.settimeout(60)
                handle.seek(start)
                remaining = length
                while remaining:
                    chunk = handle.read(min(65536, remaining))
                    if not chunk:
                        break
                    self.wfile.write(chunk)
                    remaining -= len(chunk)
        finally:
            if slot:
                STREAM_SLOTS.release()


if __name__ == '__main__':
    logging.basicConfig(level=logging.INFO, format='%(asctime)s %(message)s')
    cfg = config()
    LOG.info('HTTP music gateway %s starting', VERSION)
    with BoundedServer((cfg.get('bind', '0.0.0.0'), int(cfg.get('port', 2233))), Handler) as server:
        server.serve_forever()
#!/usr/bin/env python3
"""Independent desktop music API. Existing gateway and MCP services stay intact.

Improvements over 1.0.0:
  * catalog is cached (short TTL + cheap signature) instead of re-reading the
    whole library on every request;
  * ffmpeg conversions stop early when the requesting client is already gone;
  * PCM cache never scans/evicts on the hot path and its limit is configurable;
  * full RFC 7233 Range parsing (open/bounded/suffix) with proper 416 headers;
  * HEAD requests never trigger a transcode;
  * every request is error-isolated (a broken request cannot kill the connection
    without a JSON error) and logged with route/duration for diagnosis.
"""
import hashlib
import hmac
import json
import logging
import os
import re
import socket
import subprocess
import threading
import time
from pathlib import Path
from urllib.parse import quote, unquote, urlsplit, urlunsplit, parse_qs

from server import Handler, BoundedServer
from music_library import ROOT, catalog, config, media_file
from companion_state import list_playlists

LOG = logging.getLogger('yiyi-music-app')
VERSION = '1.4.0'
RATE = 8000               # 8 kHz band-limited linear PCM: 16 KB/s, which fits
AUDIO_CODEC = 's16le'     # the measured sustainable Wi-Fi throughput without
BYTES_PER_SAMPLE = 2      # companding distortion (mu-law sounded hoarse).
BYTES_PER_SECOND = RATE * BYTES_PER_SAMPLE
BYTES_PER_MS = BYTES_PER_SECOND // 1000
HQ_RATE, HQ_BLOCK_BYTES, HQ_BLOCK_SAMPLES = 32000, 1024, 2041
CACHE = ROOT / 'data/desktop-pcm'
CACHE_LIMIT = int(os.environ.get('YIYI_PCM_CACHE_BYTES', str(2 * 1024 ** 3)))
CATALOG_TTL = 2.0
CONVERT = threading.Lock()
# Media streams hold a worker thread for their whole duration.  Capping them
# separately keeps JSON routes (health/catalog/track) responsive even when a
# stalled or misbehaving client keeps sockets open.
STREAM_SLOTS = threading.BoundedSemaphore(int(os.environ.get('YIYI_MAX_STREAMS', '3')))
STREAM_WRITE_TIMEOUT = float(os.environ.get('YIYI_STREAM_WRITE_TIMEOUT', '30'))
_catalog_lock = threading.Lock()
_catalog_cache = {'stamp': 0.0, 'signature': None, 'items': None}
_cache_lock = threading.Lock()
_cache_bytes = None


class ClientGone(Exception):
    """The HTTP peer closed its socket while we were still preparing audio."""


class Superseded(Exception):
    """The device moved on to another song while this conversion was running."""


_latest_lock = threading.Lock()
_latest_track = {}  # client IP -> newest uncached song id requested by that client


def _track_requests_stale(client_ip, song_id):
    with _latest_lock:
        return _latest_track.get(client_ip) != song_id


def _catalog_signature():
    """Cheap change detector for the local catalogue files."""
    try:
        main = (ROOT / 'library/catalog.json').stat()
        folder = ROOT / 'data/kugou/catalog'
        entries = list(folder.glob('kg-*.json'))
        newest = 0
        for entry in entries:
            try:
                newest = max(newest, entry.stat().st_mtime_ns)
            except OSError:
                pass
        return (main.st_mtime_ns, len(entries), newest)
    except OSError:
        return None


def songs():
    """Cached catalog view used by every request (was O(134 files) per call)."""
    now = time.monotonic()
    with _catalog_lock:
        if _catalog_cache['items'] is not None and now - _catalog_cache['stamp'] < CATALOG_TTL:
            return _catalog_cache['items']
    signature = _catalog_signature()
    with _catalog_lock:
        cached = _catalog_cache['items']
        if cached is not None and signature is not None and signature == _catalog_cache['signature']:
            _catalog_cache['stamp'] = now
            return cached
    items = catalog()
    with _catalog_lock:
        _catalog_cache.update(stamp=time.monotonic(), signature=signature, items=items)
    return items


def _cache_bytes_now():
    global _cache_bytes
    with _cache_lock:
        if _cache_bytes is None:
            total = 0
            try:
                for path in CACHE.glob('*.pcm'):
                    try:
                        total += path.stat().st_size
                    except OSError:
                        pass
            except OSError:
                pass
            _cache_bytes = total
        return _cache_bytes


def _add_cache_bytes(delta):
    global _cache_bytes
    with _cache_lock:
        if _cache_bytes is not None:
            _cache_bytes = max(0, _cache_bytes + delta)


def _trim_cache(keep):
    """Evict least-recently-used PCM files until the cache fits the budget."""
    total = _cache_bytes_now()
    if total <= CACHE_LIMIT:
        return
    try:
        entries = sorted((path.stat().st_mtime_ns, path) for path in CACHE.glob('*.pcm'))
    except OSError:
        return
    removed = 0
    for _, path in entries:
        if total - removed <= CACHE_LIMIT:
            break
        if path == keep:
            continue
        try:
            size = path.stat().st_size
            path.unlink()
            removed += size
        except OSError:
            pass
    if removed:
        _add_cache_bytes(-removed)


def source_of(song):
    source = media_file('media', song['media'])
    if source is None:
        raise FileNotFoundError(song.get('media', ''))
    return source


def target_of(song, audio_format='pcm8k'):
    """Stable PCM cache path for a song; no conversion happens here."""
    source = source_of(song)
    stat = source.stat()
    prefix = 'ima32v1' if audio_format == 'ima32' else 'pcm8k1'
    key = hashlib.sha256((prefix + song['id'] + str(stat.st_size) + str(stat.st_mtime_ns)).encode()).hexdigest()
    return CACHE / (key + '.pcm')


def convert(source, temporary, should_abort=None, audio_format='pcm8k'):
    """Run ffmpeg, aborting promptly when the requesting client disconnects."""
    command = [str(ROOT / 'bin/ffmpeg'), '-v', 'error', '-nostdin', '-y', '-threads', '1',
               '-i', str(source), '-vn', '-map', '0:a:0', '-ac', '1']
    if audio_format == 'ima32':
        # The data muxer writes independent IMA WAV packets without a RIFF header.
        # Each packet carries its own predictor/index, permitting safe seeks.
        command += ['-ar', str(HQ_RATE), '-c:a', 'adpcm_ima_wav', '-block_size',
                    str(HQ_BLOCK_BYTES), '-f', 'data']
    else:
        command += ['-ar', str(RATE), '-f', 's16le']
    command.append(str(temporary))
    process = subprocess.Popen(command, stdin=subprocess.DEVNULL,
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    deadline = time.monotonic() + 90
    try:
        while process.poll() is None:
            verdict = should_abort() if should_abort is not None else None
            if verdict:
                # Callbacks may return an exception instance to distinguish a
                # superseded request (503 to the stale client) from a vanished
                # client (connection just closed).
                raise verdict if isinstance(verdict, BaseException) else ClientGone
            if time.monotonic() > deadline:
                raise subprocess.TimeoutExpired(command, 90)
            time.sleep(0.04)
    except BaseException:
        if process.poll() is None:
            process.kill()
            process.wait()
        raise
    if process.returncode:
        raise subprocess.CalledProcessError(process.returncode, command)


def prepared(song, should_abort=None, audio_format='pcm8k'):
    """Return cached audio; the legacy PCM and high quality IMA caches coexist."""
    CACHE.mkdir(parents=True, exist_ok=True)
    target = target_of(song, audio_format)
    if target.is_file():
        try:
            os.utime(target, None)
        except OSError:
            pass
        return target
    with CONVERT:
        if target.is_file():
            try:
                os.utime(target, None)
            except OSError:
                pass
            return target
        _cache_bytes_now()
        temporary = target.with_suffix('.tmp')
        try:
            if audio_format == 'ima32':
                convert(source_of(song), temporary, should_abort, audio_format)
                size = temporary.stat().st_size
                if not size or size % HQ_BLOCK_BYTES:
                    raise ValueError('invalid IMA block stream')
            else:
                convert(source_of(song), temporary, should_abort)
            temporary.replace(target)
            _add_cache_bytes(target.stat().st_size)
        finally:
            try:
                temporary.unlink()
            except OSError:
                pass
        try:
            os.utime(target, None)
        except OSError:
            pass
        _trim_cache(target)
    return target


def parse_range(value, size):
    """Parse one RFC 7233 byte range for a 16-bit PCM stream."""
    match = re.fullmatch(r'\s*bytes\s*=\s*(\d*)\s*-\s*(\d*)\s*', value or '')
    if not match or not any(match.groups()):
        return None
    left, right = match.groups()
    if left:
        start = int(left)
        end = int(right) if right else size - 1
    else:
        suffix = int(right)
        if suffix <= 0:
            return None
        start = max(0, size - suffix)
        end = size - 1
    if start < 0 or start >= size or end < start:
        return None
    end = min(end, size - 1)
    if start % BYTES_PER_SAMPLE:  # do not start mid sample of the 16-bit stream
        return None
    return start, end


def precheck_range(value):
    """Range shapes that can be rejected without knowing the PCM size.

    This keeps syntactically invalid or mid-sample offsets from triggering an
    ffmpeg transcode just to compute Content-Range.
    """
    match = re.fullmatch(r'\s*bytes\s*=\s*(\d*)\s*-\s*(\d*)\s*', value or '')
    if not match or not any(match.groups()):
        return False
    left, right = match.groups()
    if left:
        if int(left) % BYTES_PER_SAMPLE:
            return False
    elif int(right) <= 0:
        return False
    return True


class AppHandler(Handler):
    server_version = 'YiyiMusicApp/' + VERSION

    def send_response(self, code, message=None):
        self._responded = True
        self._status = code
        super().send_response(code, message)

    def client_closed(self):
        """True when the peer already closed its socket (non-destructive check)."""
        connection = self.connection
        timeout = connection.gettimeout()
        try:
            connection.setblocking(False)
            try:
                data = connection.recv(1, socket.MSG_PEEK)
            finally:
                connection.settimeout(timeout)
        except (BlockingIOError, InterruptedError):
            return False
        except OSError:
            return True
        return data == b''

    def do_GET(self):
        self._responded = False
        self._status = '-'
        route = '-'
        started = time.monotonic()
        try:
            parsed = urlsplit(self.path)
            if parsed.path == '/health':
                route = 'health'
                return self.send_json({'ok': True, 'service': 'yiyi-music-app', 'version': VERSION,
                                       'rate': RATE, 'codec': AUDIO_CODEC,
                                       'cache_bytes': _cache_bytes_now()})
            parts = parsed.path.split('/', 2)
            cfg = config()
            if len(parts) != 3 or not hmac.compare_digest(parts[1], cfg['path_key']):
                route = 'not-found'
                return self.send_json({'error': 'not found'}, 404)
            route = unquote(parts[2])
            if route == 'app/catalog':
                songs_payload = [{'id': s.get('id', ''), 'title': s.get('title', ''),
                                  'artist': s.get('artist', '')} for s in songs()]
                try:
                    playlists = list_playlists()['playlists']
                except Exception:
                    LOG.exception('playlist read failed; serving empty playlist list')
                    playlists = []
                return self.send_json({'songs': songs_payload, 'playlists': playlists})
            match = re.fullmatch(r'app/(track|pcm)/([^/]+)', route)
            if not match:
                return self.send_json({'error': 'not found'}, 404)
            kind, song_id = match.group(1), match.group(2)
            self.audio_format = parse_qs(parsed.query).get('format', ['pcm8k'])[0]
            if self.audio_format not in ('pcm8k', 'ima32'):
                return self.send_json({'error': 'unsupported audio format'}, 400)
            song = next((s for s in songs() if s['id'] == song_id), None)
            if song is None:
                return self.send_json({'error': 'song not found'}, 404)
            if kind == 'track':
                try:
                    cached_target = target_of(song, self.audio_format)
                except FileNotFoundError:
                    return self.send_json({'error': 'audio unavailable'}, 503)
                if cached_target.is_file():
                    # A cached track request must never cancel someone else's
                    # in-flight transcode.
                    target = prepared(song, lambda: ClientGone() if self.client_closed() else None, self.audio_format)
                else:
                    client_ip = self.client_address[0]
                    with _latest_lock:
                        _latest_track[client_ip] = song_id

                    def abort():
                        if _track_requests_stale(client_ip, song_id):
                            return Superseded()
                        if self.client_closed():
                            return ClientGone()
                        return None

                    target = prepared(song, abort, self.audio_format)
                size = target.stat().st_size
                origin = urlsplit(cfg['public_base'])
                host = origin.hostname or ''
                base = urlunsplit((origin.scheme, host + ':2244', origin.path, '', '')).rstrip('/')
                lyrics = (cfg['public_base'].rstrip('/') + '/lyrics/' + quote(song['lyrics'])
                          if song.get('lyrics') else '')
                hq = self.audio_format == 'ima32'
                payload = {'title': song['title'], 'artist': song.get('artist', ''),
                           'codec': 'ima_adpcm' if hq else AUDIO_CODEC,
                           'duration_ms': (size // HQ_BLOCK_BYTES * HQ_BLOCK_SAMPLES * 1000 // HQ_RATE
                                           if hq else size * 1000 // BYTES_PER_SECOND),
                           'rate': HQ_RATE if hq else RATE, 'bytes': size,
                           'pcm_url': base + '/app/pcm/' + quote(song['id'], safe='') + ('?format=ima32' if hq else ''),
                           'lyric_url': lyrics}
                if hq:
                    payload.update(block_bytes=HQ_BLOCK_BYTES, block_samples=HQ_BLOCK_SAMPLES)
                return self.send_json(payload)
            if self.command == 'HEAD':
                return self.head_pcm(song)
            return self.stream_pcm(song)
        except Superseded:
            self.close_connection = True
            LOG.info('app request superseded route=%s peer=%s', route, self.client_address[0])
            if not getattr(self, '_responded', False):
                try:
                    self.send_json({'error': 'superseded', 'message': '已切换到新的歌曲'}, 503)
                except OSError:
                    pass
        except ClientGone:
            self.close_connection = True
            LOG.info('app request abandoned by client route=%s peer=%s', route, self.client_address[0])
        except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
            self.close_connection = True
        except Exception:
            LOG.exception('app request failed route=%s peer=%s', route, self.client_address[0])
            if not getattr(self, '_responded', False):
                try:
                    self.send_json({'error': 'internal error'}, 500)
                except OSError:
                    self.close_connection = True
        finally:
            LOG.info('app method=%s route=%s peer=%s status=%s ms=%.1f', self.command, route,
                     self.client_address[0], getattr(self, '_status', '-'),
                     (time.monotonic() - started) * 1000)

    def pcm_headers(self, status, size, start=0, end=0, ranged=False):
        self.send_response(status)
        self.send_header('Content-Type', 'application/octet-stream')
        self.send_header('Content-Length', str(max(0, end - start + 1)))
        self.send_header('Accept-Ranges', 'bytes')
        self.send_header('Cache-Control', 'no-store')
        if ranged:
            self.send_header('Content-Range', f'bytes {start}-{end}/{size}')
        self.end_headers()

    def head_pcm(self, song):
        try:
            target = target_of(song, self.audio_format)
        except FileNotFoundError:
            return self.send_json({'error': 'audio unavailable'}, 503)
        if not target.is_file():
            # Avoid a transcode just for HEAD; the device always uses GET.
            self.send_response(200)
            self.send_header('Accept-Ranges', 'bytes')
            self.send_header('X-Cache', 'miss')
            self.end_headers()
            return
        size = target.stat().st_size
        header = self.headers.get('Range')
        if header is not None:
            parsed = parse_range(header, size)
            if parsed is None:
                return self.range_not_satisfiable(size)
            start, end = parsed
            self.send_response(206)
            self.send_header('Content-Type', 'application/octet-stream')
            self.send_header('Content-Length', str(end - start + 1))
            self.send_header('Accept-Ranges', 'bytes')
            self.send_header('Content-Range', f'bytes {start}-{end}/{size}')
            self.end_headers()
            return
        self.pcm_headers(200, size, 0, size - 1)

    def stream_pcm(self, song):
        if not STREAM_SLOTS.acquire(blocking=False):
            LOG.warning('pcm stream busy peer=%s', self.client_address[0])
            return self.send_json({'error': 'busy', 'message': '音源连接繁忙，请重试'}, 503)
        try:
            self._stream_pcm_locked(song)
        finally:
            STREAM_SLOTS.release()

    def _stream_pcm_locked(self, song):
        header = self.headers.get('Range')
        if header is not None and not precheck_range(header):
            self.close_connection = True
            return self.range_not_satisfiable()
        abort = lambda: ClientGone() if self.client_closed() else None
        try:
            target = prepared(song, abort, self.audio_format)
        except (OSError, subprocess.SubprocessError):
            return self.send_json({'error': 'audio unavailable'}, 503)
        try:
            handle = target.open('rb')
        except FileNotFoundError:
            # Another request may have evicted the file between prepare/open.
            try:
                target = prepared(song, abort, self.audio_format)
                handle = target.open('rb')
            except (OSError, subprocess.SubprocessError):
                return self.send_json({'error': 'audio unavailable'}, 503)
        with handle:
            size = os.fstat(handle.fileno()).st_size
            header = self.headers.get('Range')
            start, end, status, ranged = 0, size - 1, 200, False
            if header is not None:
                parsed = parse_range(header, size)
                if parsed is None:
                    return self.range_not_satisfiable(size)
                start, end = parsed
                status, ranged = 206, True
            length = max(0, end - start + 1)
            self.pcm_headers(status, size, start, end, ranged)
            self.connection.settimeout(STREAM_WRITE_TIMEOUT)
            handle.seek(start)
            remaining = length
            sent = 0
            started = time.monotonic()
            try:
                while remaining:
                    chunk = handle.read(min(65536, remaining))
                    if not chunk:
                        break
                    self.wfile.write(chunk)
                    sent += len(chunk)
                    remaining -= len(chunk)
            except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError, TimeoutError):
                self.close_connection = True
                LOG.info('app pcm aborted peer=%s sent=%d/%d', self.client_address[0], sent, length)
                return
            LOG.info('app pcm done peer=%s bytes=%d ms=%.1f', self.client_address[0], sent,
                     (time.monotonic() - started) * 1000)

    def range_not_satisfiable(self, size=None):
        self.send_response(416)
        if size is not None:
            self.send_header('Content-Range', f'bytes */{size}')
        self.send_header('Content-Length', '0')
        self.send_header('Accept-Ranges', 'bytes')
        self.end_headers()


def main():
    logging.basicConfig(level=logging.INFO, format='%(asctime)s %(message)s')
    LOG.info('independent music app %s starting', VERSION)
    port = int(os.environ.get('YIYI_APP_PORT', '2244'))
    with BoundedServer(('0.0.0.0', port), AppHandler) as server:
        server.serve_forever()


if __name__ == '__main__':
    main()

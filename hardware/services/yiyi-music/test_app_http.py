"""HTTP contract and robustness checks for the independent desktop music API."""
import importlib
import json
import logging
import os
import tempfile
import threading
import time
import unittest
from pathlib import Path
from urllib.error import HTTPError
from urllib.request import Request, urlopen


class AppHttpTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.root = Path(cls.temp.name)
        os.environ['YIYI_MUSIC_ROOT'] = cls.temp.name
        for folder in ['secrets', 'library/media', 'library/lyrics', 'data']:
            (cls.root / folder).mkdir(parents=True)
        (cls.root / 'secrets/config.json').write_text(json.dumps({
            'path_key': 'test-secret', 'public_base': 'http://example.test/test-secret'}))
        (cls.root / 'library/media/test.mp3').write_bytes(bytes(range(256)) * 400)
        (cls.root / 'library/media/test2.mp3').write_bytes(bytes(range(255, -1, -1)) * 400)
        (cls.root / 'library/lyrics/test.lrc').write_text('[00:01]hello\n', encoding='utf-8')
        (cls.root / 'library/catalog.json').write_text(json.dumps([
            {'id': 'test', 'title': 'Test Song', 'artist': 'Yiyi', 'media': 'test.mp3',
             'lyrics': 'test.lrc'},
            {'id': 'test2', 'title': 'Second Song', 'artist': 'Yiyi', 'media': 'test2.mp3'},
            {'id': 'bad', 'title': 'Missing', 'artist': '', 'media': 'missing.mp3'},
        ]), encoding='utf-8')
        import music_library
        import companion_state
        import server
        import app_http
        importlib.reload(music_library)
        importlib.reload(companion_state)
        importlib.reload(server)
        importlib.reload(app_http)
        logging.getLogger('yiyi-music-app').setLevel(logging.CRITICAL)
        cls.app = app_http
        cls.companion = companion_state
        cls.real_convert = app_http.convert
        cls.calls = []

        def fake_convert(source, temporary, should_abort=None, audio_format='pcm8k'):
            cls.calls.append(str(temporary))
            if should_abort is not None and should_abort():
                raise app_http.ClientGone
            Path(temporary).write_bytes(b'\x00' * 32768 if audio_format == 'ima32' else b'\x11' * 32000)

        cls.fake_convert = staticmethod(fake_convert)
        app_http.convert = fake_convert
        cls.http = server.BoundedServer(('127.0.0.1', 0), app_http.AppHandler)
        cls.base = f'http://127.0.0.1:{cls.http.server_port}'
        cls.worker = threading.Thread(target=cls.http.serve_forever, daemon=True)
        cls.worker.start()
        cls.prefix = cls.base + '/test-secret'

    @classmethod
    def tearDownClass(cls):
        cls.http.shutdown()
        cls.http.server_close()
        cls.worker.join()
        cls.app.convert = cls.real_convert
        cls.temp.cleanup()

    def setUp(self):
        self.app.convert = self.fake_convert
        self.app.CACHE = self.root / 'data/desktop-pcm'
        self.app._cache_bytes = None
        self.app._catalog_cache['items'] = None
        self.app.CACHE.mkdir(parents=True, exist_ok=True)
        for path in self.app.CACHE.glob('*'):
            path.unlink()
        type(self).calls.clear()

    def tearDown(self):
        self.app.convert = self.real_convert
        for path in (self.root / 'data/desktop-pcm').glob('*'):
            path.unlink()

    def get(self, path, headers=None, method='GET'):
        return urlopen(Request(self.prefix + path, headers=headers or {}, method=method), timeout=10)

    def test_health_catalog_and_missing_media(self):
        with urlopen(self.base + '/health', timeout=5) as response:
            health = json.load(response)
        self.assertTrue(health['ok'])
        self.assertEqual(health['service'], 'yiyi-music-app')
        self.assertEqual(health['rate'], 8000)
        self.assertGreaterEqual(health['cache_bytes'], 0)
        with self.get('/app/catalog') as response:
            catalog = json.load(response)
        self.assertEqual([s['id'] for s in catalog['songs']], ['test', 'test2'])
        self.assertEqual(catalog['playlists'], [])
        for path in ['/wrong/app/catalog', '/app/unknown', '/app/pcm/missing']:
            with self.assertRaises(HTTPError) as error:
                self.get(path)
            self.assertEqual(error.exception.code, 404)

    def test_catalog_survives_playlist_db_failure(self):
        original = self.app.list_playlists
        self.app.list_playlists = lambda: (_ for _ in ()).throw(RuntimeError('db down'))
        try:
            with self.get('/app/catalog') as response:
                catalog = json.load(response)
        finally:
            self.app.list_playlists = original
        self.assertEqual(catalog['playlists'], [])
        self.assertEqual([s['id'] for s in catalog['songs']], ['test', 'test2'])

    def test_track_metadata_and_pcm_stream(self):
        with self.get('/app/track/test') as response:
            track = json.load(response)
        self.assertEqual(track['title'], 'Test Song')
        self.assertEqual(track['rate'], 8000)
        self.assertEqual(track['bytes'], 32000)
        self.assertEqual(track['duration_ms'], 2000)
        self.assertEqual(track['codec'], 's16le')
        self.assertTrue(track['pcm_url'].startswith('http://example.test:2244/test-secret/app/pcm/'))
        self.assertTrue(track['lyric_url'].endswith('/lyrics/test.lrc'))
        with self.get('/app/pcm/test') as response:
            self.assertEqual(response.status, 200)
            self.assertEqual(len(response.read()), 32000)
        with self.get('/app/pcm/test', {'Range': 'bytes=0-1023'}) as response:
            self.assertEqual(response.status, 206)
            self.assertEqual(response.headers['Content-Range'], 'bytes 0-1023/32000')
            self.assertEqual(len(response.read()), 1024)
        with self.get('/app/pcm/test', {'Range': 'bytes=-1024'}) as response:
            self.assertEqual(response.status, 206)
            self.assertEqual(response.headers['Content-Range'], 'bytes 30976-31999/32000')
            self.assertEqual(len(response.read()), 1024)
        with self.get('/app/pcm/test', {'Range': 'bytes=1024-'}) as response:
            self.assertEqual(response.status, 206)
            self.assertEqual(len(response.read()), 32000 - 1024)

    def test_range_errors_and_head(self):
        # HEAD of an uncached track must not transcode.
        with self.get('/app/pcm/test', method='HEAD') as response:
            self.assertEqual(response.status, 200)
            self.assertEqual(response.headers['X-Cache'], 'miss')
            self.assertEqual(response.read(), b'')
        self.assertEqual(type(self).calls, [])
        # Sizeless-invalid ranges are rejected before any conversion.
        for spec in ['bytes=1-', 'bytes=0-1,4-5', 'garbage', 'bytes=-0']:
            with self.assertRaises(HTTPError) as error:
                self.get('/app/pcm/test', {'Range': spec})
            self.assertEqual(error.exception.code, 416)
        self.assertEqual(type(self).calls, [])
        # A range beyond EOF needs the PCM size and therefore reports it.
        with self.assertRaises(HTTPError) as error:
            self.get('/app/pcm/test', {'Range': 'bytes=32000-'})
        self.assertEqual(error.exception.code, 416)
        self.assertEqual(error.exception.headers['Content-Range'], 'bytes */32000')
        # Once cached, HEAD reports the real length without a new conversion.
        with self.get('/app/track/test') as response:
            json.load(response)
        with self.get('/app/pcm/test', method='HEAD') as response:
            self.assertEqual(response.headers['Content-Length'], '32000')
            self.assertEqual(response.read(), b'')
        self.assertEqual(len(type(self).calls), 1)

    def test_high_quality_metadata_cache_and_seek_range(self):
        with self.get('/app/track/test?format=ima32') as response:
            track = json.load(response)
        self.assertEqual((track['rate'], track['codec']), (32000, 'ima_adpcm'))
        self.assertEqual((track['block_bytes'], track['block_samples']), (1024, 2041))
        self.assertEqual(track['duration_ms'], 2041)
        self.assertTrue(track['pcm_url'].endswith('/app/pcm/test?format=ima32'))
        with self.get('/app/pcm/test?format=ima32', {'Range': 'bytes=1024-2047'}) as response:
            self.assertEqual(response.status, 206)
            self.assertEqual(response.headers['Content-Range'], 'bytes 1024-2047/32768')
            self.assertEqual(response.read(), bytes(1024))
        with self.get('/app/pcm/test?format=ima32', method='HEAD') as response:
            self.assertEqual(response.headers['Content-Length'], '32768')
        # Installing the new server must not change an old device's PCM protocol.
        with self.get('/app/track/test') as response:
            legacy = json.load(response)
        self.assertEqual((legacy['rate'], legacy['codec'], legacy['bytes']), (8000, 's16le', 32000))
        self.assertEqual(len(type(self).calls), 2)
        with self.assertRaises(HTTPError) as error:
            self.get('/app/track/test?format=unknown')
        self.assertEqual(error.exception.code, 400)

    def test_conversion_happens_once_for_concurrent_requests(self):
        errors = []

        def fetch():
            try:
                with urlopen(self.prefix + '/app/track/test', timeout=15) as response:
                    json.load(response)
            except Exception as exc:  # pragma: no cover - asserted below
                errors.append(exc)

        threads = [threading.Thread(target=fetch) for _ in range(6)]
        for thread in threads:
            thread.start()
        for thread in threads:
            thread.join()
        self.assertEqual(errors, [])
        self.assertEqual(len(type(self).calls), 1)

    def test_client_gone_aborts_conversion(self):
        seen = []

        def aborting_convert(source, temporary, should_abort=None):
            seen.append(1)
            if should_abort is None or not should_abort():
                raise AssertionError('client-closed probe was not honoured')
            raise self.app.ClientGone

        self.app.convert = aborting_convert
        with self.assertRaises(self.app.ClientGone):
            self.app.prepared({'id': 'test', 'media': 'test.mp3'}, lambda: True)
        self.assertEqual(seen, [1])

    def test_superseded_conversion_aborts_and_returns_fast(self):
        started = threading.Event()
        original = self.app.convert

        def slow_convert(source, temporary, should_abort=None):
            if not str(source).endswith('test.mp3'):
                Path(temporary).write_bytes(b'\x11' * 32000)
                return
            started.set()
            deadline = time.monotonic() + 5
            while time.monotonic() < deadline:
                if should_abort is not None and should_abort():
                    raise self.app.Superseded
                time.sleep(0.02)
            raise AssertionError('conversion was never superseded')

        self.app.convert = slow_convert
        results = {}

        def stale_request():
            results['first'] = self._track_status('test')

        stale = threading.Thread(target=stale_request)
        stale.start()
        self.assertTrue(started.wait(3))
        second = self._track_status('test2')
        stale.join(10)
        self.assertIn(second, (200, 503))
        self.assertEqual(results.get('first'), 503)
        self.app.convert = original

    def _track_status(self, song_id):
        try:
            with urlopen(self.prefix + '/app/track/' + song_id, timeout=10) as response:
                return response.status
        except HTTPError as exc:
            return exc.code

    def test_cached_track_does_not_cancel_other_conversion(self):
        release = threading.Event()
        slow_calls = []
        original = self.app.convert

        def selective_convert(source, temporary, should_abort=None):
            if str(source).endswith('test.mp3'):
                slow_calls.append('test')
                started = threading.Event()
                slow_calls.append(started)
                started.set()
                while not release.is_set():
                    verdict = should_abort() if should_abort is not None else None
                    if verdict:
                        raise verdict
                    time.sleep(0.02)
                Path(temporary).write_bytes(b'\x11' * 32000)
                return
            Path(temporary).write_bytes(b'\x11\x22' * 16000)

        # Pre-cache the second song while the normal converter is active.
        self.app.convert = self.fake_convert
        self.assertEqual(self._track_status('test2'), 200)
        self.app.convert = selective_convert
        results = {}
        stale = threading.Thread(target=lambda: results.__setitem__('first', self._track_status('test')))
        stale.start()
        deadline = time.monotonic() + 3
        while 'test' not in slow_calls and time.monotonic() < deadline:
            time.sleep(0.02)
        self.assertIn('test', slow_calls)
        # A cached track request must not supersede the in-flight conversion.
        self.assertEqual(self._track_status('test2'), 200)
        time.sleep(0.2)
        self.assertTrue(stale.is_alive(), 'stale conversion was cancelled by a cached request')
        release.set()
        stale.join(10)
        self.assertEqual(results.get('first'), 200)
        self.app.convert = original

    def test_stream_slots_reject_extra_streams(self):
        original = self.app.STREAM_SLOTS
        self.app.STREAM_SLOTS = threading.BoundedSemaphore(0)
        try:
            with self.assertRaises(HTTPError) as error:
                self.get('/app/pcm/test', {'Range': 'bytes=0-1'})
            self.assertEqual(error.exception.code, 503)
        finally:
            self.app.STREAM_SLOTS = original

    def test_catalog_cache_reused_then_invalidated(self):
        scans = []
        original = self.app.catalog
        self.app.catalog = lambda: scans.append(1) or original()
        try:
            self.get('/app/catalog').close()
            self.get('/app/catalog').close()
            self.assertEqual(len(scans), 1)
            os.utime(self.root / 'library/catalog.json', None)  # signature change
            self.app._catalog_cache['stamp'] = 0
            self.get('/app/catalog').close()
            self.assertEqual(len(scans), 2)
        finally:
            self.app.catalog = original


if __name__ == '__main__':
    unittest.main(verbosity=2)

import importlib
import json
import os
import tempfile
import threading
import unittest
from pathlib import Path
from urllib.error import HTTPError
from urllib.request import Request, urlopen


class GatewayTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.root = Path(cls.temp.name)
        os.environ['YIYI_MUSIC_ROOT'] = cls.temp.name
        for folder in ['secrets', 'library/media', 'library/lyrics', 'data']:
            (cls.root / folder).mkdir(parents=True)
        (cls.root / 'secrets/config.json').write_text(json.dumps({
            'path_key': 'test-secret', 'public_base': 'http://example.test/test-secret'}))
        cls.audio = bytes(range(256)) * 20
        (cls.root / 'library/media/test.mp3').write_bytes(cls.audio)
        (cls.root / 'library/catalog.json').write_text(json.dumps([
            {'id': 'test', 'title': '测试曲', 'artist': '依依', 'media': 'test.mp3'},
            {'id': 'bad', 'title': '不存在', 'media': 'missing.mp3'},
        ]), encoding='utf-8')
        import music_library
        import companion_state
        import server
        importlib.reload(music_library)
        importlib.reload(companion_state)
        importlib.reload(server)
        cls.library = music_library
        cls.server = server
        cls.http = server.BoundedServer(('127.0.0.1', 0), server.Handler)
        cls.base = f'http://127.0.0.1:{cls.http.server_port}'
        cls.worker = threading.Thread(target=cls.http.serve_forever, daemon=True)
        cls.worker.start()

    @classmethod
    def tearDownClass(cls):
        cls.http.shutdown()
        cls.http.server_close()
        cls.worker.join()
        cls.temp.cleanup()

    def request(self, path, headers=None, method='GET'):
        return urlopen(Request(self.base + path, headers=headers or {}, method=method), timeout=5)

    def test_health_and_private_paths(self):
        with self.request('/health') as response:
            health = json.load(response)
            self.assertTrue(health['ok'])
            self.assertEqual(health['version'], self.server.VERSION)
        for path in ['/api/catalog', '/stream_pcm?song=test', '/wrong/api/catalog', '/mcp']:
            with self.assertRaises(HTTPError) as error:
                self.request(path)
            self.assertEqual(error.exception.code, 404)

    def test_catalog_excludes_missing_media_and_unknown_has_no_fallback(self):
        self.assertEqual(len(self.library.catalog()), 1)
        self.assertIsNone(self.library.find_song('完全不存在的歌'))
        self.assertIsNone(self.library.find_song(''))
        self.assertIsNone(self.library.find_song('测试曲', '错误歌手'))
        self.assertEqual(self.library.find_song('test')['title'], '测试曲')

    def test_legacy_contract(self):
        with self.request('/test-secret/stream_pcm?song=test') as response:
            data = json.load(response)
        self.assertEqual(data['title'], '测试曲')
        self.assertEqual(data['audio_url'], 'http://example.test/test-secret/media/test.mp3')
        self.assertEqual(data['lyric_url'], '')
        with self.assertRaises(HTTPError) as error:
            self.request('/test-secret/stream_pcm?song=unknown')
        self.assertEqual(error.exception.code, 404)

    def test_prepared_source_is_visible_only_after_audio_exists(self):
        cache = self.root / 'data/kugou'
        for name in ('catalog', 'media', 'lyrics'):
            (cache / name).mkdir(parents=True, exist_ok=True)
        item = {'id': 'kg-test', 'title': '已准备歌曲', 'artist': '测试歌手',
                'media': 'kg-test.mp3', 'lyrics': 'kg-test.lrc', 'source': 'kugou'}
        index = cache / 'catalog/kg-test.json'
        index.write_text(json.dumps(item), encoding='utf-8')
        try:
            self.assertIsNone(self.library.find_song('kg-test'))
            (cache / 'media/kg-test.mp3').write_bytes(self.audio)
            (cache / 'lyrics/kg-test.lrc').write_text('[00:01]测试歌词', encoding='utf-8')
            self.assertEqual(self.library.find_song('kg-test')['artist'], '测试歌手')
            with self.request('/test-secret/media/kg-test.mp3', {'Range': 'bytes=0-15'}) as r:
                self.assertEqual(r.status, 206)
                self.assertEqual(r.read(), self.audio[:16])
            with self.request('/test-secret/lyrics/kg-test.lrc') as r:
                self.assertIn('测试歌词', r.read().decode('utf-8'))
        finally:
            index.unlink()
            for p in (cache / 'media/kg-test.mp3', cache / 'lyrics/kg-test.lrc'):
                if p.exists():
                    p.unlink()

    def test_range_and_head(self):
        path = '/test-secret/media/test.mp3'
        for spec, expected in [('bytes=0-31', self.audio[:32]),
                               ('bytes=32-', self.audio[32:]),
                               ('bytes=-16', self.audio[-16:])]:
            with self.request(path, {'Range': spec}) as response:
                self.assertEqual(response.status, 206)
                self.assertEqual(response.read(), expected)
        with self.request(path, method='HEAD') as response:
            self.assertEqual(int(response.headers['Content-Length']), len(self.audio))
            self.assertEqual(response.read(), b'')

    def test_invalid_ranges_and_traversal(self):
        for spec in ['bytes=99999-', 'bytes=-0', 'bytes=2-1', 'bytes=0-1,2-3', 'garbage']:
            with self.assertRaises(HTTPError) as error:
                self.request('/test-secret/media/test.mp3', {'Range': spec})
            self.assertEqual(error.exception.code, 416)
        with self.assertRaises(HTTPError) as error:
            self.request('/test-secret/media/%2e%2e%2fsecrets%2fconfig.json')
        self.assertEqual(error.exception.code, 404)

    def test_private_device_sync_and_alerts(self):
        def post(path, data):
            return urlopen(Request(self.base + path, data=data, headers={'Content-Type': 'application/json'}), timeout=5)
        with self.assertRaises(HTTPError) as error:
            post('/device/sync', b'{"boot":"fixture"}')
        self.assertEqual(error.exception.code, 404)
        for data in (b'[]', b'{"boot":5}', b'{"boot":"fixture","ack":{}}', b'x' * 8193):
            with self.assertRaises(HTTPError) as error:
                post('/test-secret/device/sync', data)
            self.assertEqual(error.exception.code, 400)
        with post('/test-secret/device/sync', b'{"boot":"fixture","idle":true}') as r:
            self.assertIn('server_time', json.load(r))
        alerts = self.root / 'data/alerts'
        alerts.mkdir(exist_ok=True)
        filename = 'a' * 32 + '.p3'
        (alerts / filename).write_bytes(b'alert fixture')
        with self.request('/test-secret/alerts/' + filename) as r:
            self.assertEqual(r.read(), b'alert fixture')
        for path in ['/alerts/' + filename, '/test-secret/alerts/../../secrets/config.json']:
            with self.assertRaises(HTTPError) as error:
                self.request(path)
            self.assertEqual(error.exception.code, 404)

    def test_media_stream_slots_reject_extra_streams(self):
        original = self.server.STREAM_SLOTS
        self.server.STREAM_SLOTS = threading.BoundedSemaphore(0)
        try:
            with self.assertRaises(HTTPError) as error:
                self.request('/test-secret/media/test.mp3')
            self.assertEqual(error.exception.code, 503)
        finally:
            self.server.STREAM_SLOTS = original

    def test_private_playlist_controls(self):
        import companion_state
        companion_state.save_playlist('酷狗我喜欢', [
            {'id': 'test', 'title': '测试曲', 'artist': '依依'},
        ])
        with self.assertRaises(HTTPError) as error:
            self.request('/device/ui/playlists')
        self.assertEqual(error.exception.code, 404)
        with self.request('/test-secret/device/ui/playlists') as response:
            self.assertEqual(json.load(response)['playlists'], [
                {'name': '酷狗我喜欢', 'count': 1},
            ])

        def post(path, value):
            data = json.dumps(value, ensure_ascii=False).encode('utf-8')
            request = Request(self.base + path, data=data,
                              headers={'Content-Type': 'application/json'})
            with urlopen(request, timeout=5) as response:
                return json.load(response)

        started = post('/test-secret/device/ui/playlists', {'name': '酷狗我喜欢'})
        self.assertTrue(started['ok'])
        self.assertTrue(started['queued'])
        following = post('/test-secret/device/ui/control', {'action': 'next'})
        self.assertTrue(following['ok'])
        for value in ({'action': 'stop'}, {'name': ''}, []):
            with self.assertRaises(HTTPError) as error:
                data = json.dumps(value).encode('utf-8')
                urlopen(Request(self.base + '/test-secret/device/ui/control', data=data,
                                headers={'Content-Type': 'application/json'}), timeout=5)
            self.assertEqual(error.exception.code, 400)


if __name__ == '__main__':
    unittest.main(verbosity=2)
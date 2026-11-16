"""Check the local one-liner relay without making external API requests."""
from http.server import ThreadingHTTPServer
from urllib.error import HTTPError
from urllib.request import Request, urlopen
from unittest.mock import patch
import io
import json
import threading
import unittest
import phone_server


class ReadingRelayTest(unittest.TestCase):
    def setUp(self):
        self.server = ThreadingHTTPServer(('127.0.0.1', 0), phone_server.Handler)
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()
        self.url = f'http://127.0.0.1:{self.server.server_port}/api/reading/quote'

    def tearDown(self):
        self.server.shutdown()
        self.server.server_close()
        self.thread.join()

    def request(self):
        return urlopen(Request(self.url, headers={'Origin': 'null'}), timeout=3)

    def test_file_origin_and_fixed_upstream(self):
        value = {'text': '达人远见，不与物争。'}
        with patch.object(phone_server, 'urlopen', return_value=io.BytesIO(json.dumps(value).encode())) as remote:
            with self.request() as response:
                self.assertEqual(json.load(response), value)
                self.assertEqual(response.headers['Access-Control-Allow-Origin'], 'null')
            self.assertEqual(remote.call_args.args[0].full_url, 'https://uapis.cn/api/v1/saying')

    def test_invalid_upstream_cannot_return_success(self):
        for raw in [b'{}', b'{"text":" "}', b'<html>not json</html>', b'x' * 32769]:
            with self.subTest(raw=raw[:30]), patch.object(phone_server, 'urlopen', return_value=io.BytesIO(raw)):
                with self.assertRaises(HTTPError) as caught:
                    self.request()
                self.assertEqual(caught.exception.code, 502)
                caught.exception.close()

    def test_rate_limit_status_and_retry_after_reach_the_browser(self):
        error = HTTPError('https://uapis.cn/api/v1/saying', 429, 'limit', {'Retry-After': '120'}, None)
        with patch.object(phone_server, 'urlopen', side_effect=error):
            with self.assertRaises(HTTPError) as caught:
                self.request()
            self.assertEqual(caught.exception.code, 429)
            self.assertEqual(caught.exception.headers['Retry-After'], '120')
            self.assertIn('Retry-After', caught.exception.headers['Access-Control-Expose-Headers'])
            caught.exception.close()


if __name__ == '__main__':
    unittest.main()

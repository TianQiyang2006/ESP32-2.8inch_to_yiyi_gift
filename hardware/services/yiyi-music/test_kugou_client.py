"""Focused checks for the read-only account-playlist adapter."""
import unittest

import kugou_client


class KugouPlaylistTests(unittest.TestCase):
    def test_playlist_view_keeps_only_public_metadata(self):
        original = kugou_client.api
        try:
            kugou_client.api = lambda route, params: {
                'status': 1,
                'data': {'list_count': 1, 'info': [{
                    'name': '我喜欢', 'count': 131, 'listid': 2, 'is_def': 2,
                    'token': 'must-not-escape',
                }]},
            }
            result = kugou_client.playlists()
        finally:
            kugou_client.api = original
        self.assertEqual(result['total'], 1)
        self.assertEqual(result['playlists'], [{
            'name': '我喜欢', 'count': 131, 'list_id': '2', 'type': 0,
            'is_default_favorites': True,
        }])

    def test_track_page_normalizes_name_and_artist(self):
        original = kugou_client.api
        try:
            kugou_client.api = lambda route, params: {
                'status': 1,
                'data': {'page': 1, 'pagesize': 5, 'count': 131, 'info': [{
                    'name': '周杰伦 - 晴天.mp3', 'hash': 'A' * 32,
                    'singerinfo': [{'name': '周杰伦'}], 'timelen': 269856,
                }]},
            }
            result = kugou_client.playlist_tracks('2', 1, 5)
        finally:
            kugou_client.api = original
        self.assertEqual(result['total'], 131)
        self.assertEqual(result['tracks'], [{
            'hash': 'a' * 32, 'title': '晴天', 'artist': '周杰伦', 'duration': 269,
            'album_id': '0', 'album_audio_id': '0',
        }])

    def test_unavailable_upstream_is_reported(self):
        original = kugou_client.api
        try:
            kugou_client.api = lambda route, params: {'status': 0}
            with self.assertRaises(kugou_client.KugouError):
                kugou_client.playlists()
            with self.assertRaises(kugou_client.KugouError):
                kugou_client.playlist_tracks('2')
        finally:
            kugou_client.api = original


if __name__ == '__main__':
    unittest.main()

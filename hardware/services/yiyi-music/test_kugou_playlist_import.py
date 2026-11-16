"""Account playlist import keeps recording identity and existing local songs."""
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

import mcp_service as service
import companion_state as companion
from kugou_client import KugouError


class PlaylistImportTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        root_patch = patch.object(companion, 'ROOT', Path(self.temp.name))
        root_patch.start()
        self.addCleanup(root_patch.stop)
        self.source = {'name': '我喜欢', 'list_id': '2', 'type': 0,
                       'count': 131, 'is_default_favorites': True}
        self.track = {'hash': 'a' * 32, 'title': 'Exact version', 'artist': 'Artist',
                      'album_id': '10', 'album_audio_id': '20', 'duration': 240}

    def test_exact_recording_append_and_retry_keep_existing_songs(self):
        companion.save_playlist('酷狗我喜欢', [{'id': 'existing', 'title': 'Existing'}])
        with patch.object(service, 'kugou_playlists', return_value={'playlists': [self.source]}), \
             patch.object(service, 'kugou_playlist_tracks', return_value={
                 'tracks': [self.track], 'total': 131}) as tracks, \
             patch.object(service, 'prepare_track', return_value={
                 'id': 'kg-' + self.track['hash'], 'title': 'Exact version'}) as prepare:
            result = service.yiyi_kugou_playlist_import()
            again = service.yiyi_kugou_playlist_import()
        prepare.assert_called_with(self.track)
        tracks.assert_called_with('2', 1, 5, 0)
        self.assertTrue(result['ok'])
        self.assertEqual(result['imported_count'], 2)
        self.assertEqual(again['imported_count'], 2)
        self.assertEqual(result['next_page'], 2)
        songs = companion.list_playlists('酷狗我喜欢')['playlists'][0]['songs']
        self.assertEqual(songs[0]['id'], 'existing')

    def test_unavailable_songs_report_errors_and_preserve_playlist(self):
        companion.save_playlist('酷狗我喜欢', [{'id': 'existing', 'title': 'Existing'}])
        with patch.object(service, 'kugou_playlists', return_value={'playlists': [self.source]}), \
             patch.object(service, 'kugou_playlist_tracks', return_value={
                 'tracks': [self.track], 'total': 131}), \
             patch.object(service, 'prepare_track', side_effect=KugouError('没有完整播放权限')):
            result = service.yiyi_kugou_playlist_import()
        self.assertTrue(result['ok'])
        self.assertEqual(result['prepared'], 0)
        self.assertEqual(result['imported_count'], 1)
        self.assertEqual(result['failed'][0]['message'], '没有完整播放权限')
        self.assertEqual(result['next_page'], 2)


if __name__ == '__main__':
    unittest.main()

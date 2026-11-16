"""Behavioral checks for durable controls, playback events and reminder delivery."""
import json
import tempfile
import unittest
from datetime import datetime
from pathlib import Path
from unittest.mock import patch

import companion_state as c
import information as info
from reminder_voice import ogg_to_p3


class CompanionTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = patch.object(c, 'ROOT', Path(self.temp.name))
        self.root.start()
        self.addCleanup(self.root.stop)
        self.clock = patch.object(c.time, 'time', return_value=1800000000.)
        self.clock.start()
        self.addCleanup(self.clock.stop)
        self.song = patch.object(c, 'catalog', return_value=[{'id': 'one'}, {'id': 'two'}])
        self.song.start()
        self.addCleanup(self.song.stop)
        self.now = 1800000000.
        self.seq = 0
        self.ack = 0

    def poll(self, **changes):
        self.seq += 1
        state = dict(boot='boot-a', event_seq=self.seq, event='', idle=True, ack=self.ack)
        state.update(changes)
        return c.sync_device(state, self.now)

    def playlist(self, mode='sequence'):
        c.save_playlist('mix', [{'id': 'one', 'title': 'One'}, {'id': 'two', 'title': 'Two'}])
        return c.play_playlist('mix', mode)

    def test_playlist_exactly_once_end_and_sequence_stop(self):
        self.playlist()
        cmd = self.poll()['command']
        self.ack = cmd['id']
        token = cmd['payload']['token']
        second = self.poll(event='ended', event_seq=10, token=token)['command']
        self.assertEqual(second['payload']['song'], 'two')
        duplicate = self.poll(event='ended', event_seq=10, token=token)['command']
        self.assertEqual(duplicate['id'], second['id'])
        self.ack = second['id']
        self.assertIsNone(self.poll(event='ended', token=second['payload']['token'])['command'])
        self.assertFalse(c.player_status()['queue']['active'])

    def test_large_playlist_uses_one_media_snapshot_and_skips_missing_audio(self):
        songs = [{'id': f'song-{i}', 'title': f'Song {i}', 'artist': ''} for i in range(131)]
        c.save_playlist('favorites', songs)
        available = songs[::2]
        with patch.object(c, 'catalog', return_value=available) as scan:
            started = c.play_playlist('favorites')
        self.assertTrue(started['ok'])
        self.assertEqual(started['count'], len(available))
        self.assertEqual(c.player_status()['queue']['songs'], available)
        scan.assert_called_once_with()

    def test_playlist_with_no_cached_audio_does_not_enqueue_playback(self):
        c.save_playlist('missing', [{'id': 'missing', 'title': 'Missing'}])
        with patch.object(c, 'catalog', return_value=[]):
            self.assertFalse(c.play_playlist('missing')['ok'])
        self.assertIsNone(self.poll()['command'])

    def test_foreign_event_and_failed_start_do_not_advance(self):
        self.playlist()
        cmd = self.poll()['command']
        self.assertEqual(self.poll(event='ended', token='stale')['command']['id'], cmd['id'])
        self.ack = cmd['id']
        self.assertIsNone(self.poll(result='play_failed')['command'])
        self.assertFalse(c.player_status()['queue']['active'])

    def test_modes_and_skip_supersede_pending_play(self):
        for mode, expected in [('loop', 'two'), ('repeat_one', 'one')]:
            self.playlist(mode)
            cmd = self.poll()['command']
            self.ack = cmd['id']
            following = self.poll(event='ended', token=cmd['payload']['token'])['command']
            self.assertEqual(following['payload']['song'], expected)
            c.control('next')
            newest = self.poll()['command']
            self.assertGreater(newest['id'], following['id'])
            self.assertEqual(c.command_result(following['id'])['result'], 'superseded')
            self.ack = newest['id']

    def test_stop_and_sleep_cancel_pending_play(self):
        self.playlist()
        first = self.poll()['command']
        stop = c.control('stop')
        self.assertEqual(self.poll()['command']['id'], stop['command_id'])
        self.assertEqual(c.command_result(first['id'])['result'], 'superseded')
        self.ack = stop['command_id']
        self.playlist('loop')
        c.control('sleep', 10)
        self.now += 11
        response = self.poll(event='ended', token=c.player_status()['queue']['token'])
        self.assertEqual(response['command']['action'], 'sleep')
        self.ack = response['command']['id']
        self.assertEqual(self.poll()['command']['action'], 'stop')
        self.assertIsNone(c.player_status()['queue'])

    def test_reminder_survives_connection_and_deferred_control(self):
        reminder = c.create_reminder('Drink water', delay_seconds=5)
        self.now += 6
        self.assertIsNone(self.poll(idle=False)['command'])
        first = self.poll()['command']
        self.assertEqual(first['action'], 'remind')
        pause = c.control('pause')
        delivered = self.poll(idle=False)['command']
        self.assertEqual(delivered['id'], pause['command_id'])
        self.ack = delivered['id']
        later = self.poll()['command']
        self.assertEqual(later['payload']['reminder_id'], reminder['id'])
        self.assertGreater(later['id'], self.ack)
        self.assertTrue(c.cancel_reminder(reminder['id'])['ok'])
        self.assertIsNone(self.poll()['command'])

    def test_expired_reminder_not_burst_and_repeat_next_due(self):
        one = c.create_reminder('Old', delay_seconds=5)
        interval = c.create_reminder('Repeat', delay_seconds=5, repeat='interval', interval_minutes=1)
        self.now += 3601
        self.assertIsNone(self.poll()['command'])
        rows = {r['id']: r for r in c.reminders()['reminders']}
        self.assertEqual(rows[one['id']]['enabled'], 0)
        self.assertGreater(datetime.fromisoformat(rows[interval['id']]['due']).timestamp(), self.now)
        self.assertLess(datetime.fromisoformat(rows[interval['id']]['due']).timestamp(), self.now + 61)

    def test_pause_does_not_advance_queue_and_ack_is_idempotent(self):
        self.playlist()
        first = self.poll()['command']
        self.ack = first['id']
        pause = c.control('pause')
        self.poll(token=first['payload']['token'], playing=True, paused=True)
        self.assertEqual(c.player_status()['queue']['index'], 0)
        self.ack = pause['command_id']
        self.poll(result='paused')
        self.poll(result='changed')
        self.assertEqual(c.command_result(self.ack)['result'], 'paused')

    def test_device_reboot_preserves_playlist_without_claiming_continuation(self):
        self.playlist('loop')
        first = self.poll()['command']
        self.ack = first['id']
        self.poll(playing=True, token=first['payload']['token'])
        self.assertIsNone(self.poll(boot='boot-b', playing=False, token='')['command'])
        self.assertFalse(c.player_status()['queue']['active'])
        self.assertEqual(len(c.list_playlists('mix')['playlists'][0]['songs']), 2)


class InformationTest(unittest.TestCase):
    def test_city_must_be_asked_and_ambiguous_requires_confirmation(self):
        with patch.object(info, 'fetch_json') as fetch:
            self.assertTrue(info.weather()['needs_city'])
            fetch.assert_not_called()
            fetch.return_value = {'results': [{'name': 'Springfield', 'latitude': 1, 'longitude': 2},
                                               {'name': 'Springfield', 'latitude': 3, 'longitude': 4}]}
            self.assertTrue(info.weather('Springfield')['needs_city_selection'])

    def test_hot_search_partial_failure_preserves_source_and_time(self):
        data = {'update_time': '2026-10-30T10:00:00+08:00', 'list': [
            {'index': 1, 'title': 'A &amp; B', 'url': 'https://example.test/news', 'hot_value': '123'},
            {'title': 'bad', 'url': 'javascript:invalid'}]}
        with patch.object(info, 'fetch_json', side_effect=[data, OSError('offline')]):
            result = info.hot_news()
        self.assertTrue(result['ok'])
        self.assertEqual(result['boards'][0]['items'][0]['title'], 'A & B')
        self.assertEqual(len(result['boards'][0]['items']), 1)
        self.assertEqual(result['boards'][0]['updated_at'], data['update_time'])
        self.assertEqual(len(result['unavailable']), 1)

    def test_voice_rejects_truncated_ogg(self):
        for value in (b'', b'OggS', b'OggS' + bytes(22) + bytes([1, 100]) + b'short'):
            with self.assertRaises(ValueError):
                ogg_to_p3(value)


if __name__ == '__main__':
    unittest.main(verbosity=2)

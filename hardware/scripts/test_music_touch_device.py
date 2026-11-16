"""Exercise the real LVGL music buttons through the board's USB touch probe.

Runs on an attached device, starts a playlist and leaves it playing at volume 40.
The tap commands pass through LVGL input handling, rather than calling player APIs.
"""
import argparse
import json
import re
import sys
import time
from pathlib import Path

import serial

sys.stdout.reconfigure(encoding='utf-8')


class Device:
    def __init__(self, name, log):
        self.port = serial.Serial(port=None, baudrate=115200, timeout=0.15, write_timeout=3)
        self.port.dtr = self.port.rts = False
        self.port.port = name
        self.port.open()
        self.log = log.open('a', encoding='utf-8', buffering=1)

    def line(self):
        line = self.port.readline().decode('utf-8', 'replace').strip()
        if line:
            clean = re.sub(r'https?://[^\s"<>]+', '[private-url]', line)
            self.log.write(clean + '\n')
            if any(key in clean for key in ('MUSIC_UI', 'MUSIC_STATUS', 'Playlist ', 'Touch player',
                                            'paused:', 'resumed:', 'output volume', 'panic', 'Error')):
                print(clean, flush=True)
        return line

    def send(self, command):
        print('SEND', command, flush=True)
        self.log.write('SEND ' + command + '\n')
        self.port.write((command + '\n').encode('utf-8'))
        self.port.flush()

    def drain(self, seconds):
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            self.line()

    def query(self, command, prefix):
        for _ in range(3):
            self.send(command)
            end = time.monotonic() + 3
            while time.monotonic() < end:
                line = self.line()
                index = line.find(prefix)
                if index < 0:
                    continue
                try:
                    offset = index if prefix == '{' else index + len(prefix)
                    return json.JSONDecoder().raw_decode(line[offset:])[0]
                except json.JSONDecodeError:
                    continue
        raise RuntimeError('No valid response to ' + command)

    def ui(self):
        return self.query('musicui', 'MUSIC_UI ')

    def music(self):
        return self.query('musicstatus', 'MUSIC_STATUS ')

    def status(self):
        return self.query('status', '{')

    def tap(self, name, ui=None):
        ui = ui or self.ui()
        button = next(item for item in ui['buttons'] if item['name'] == name)
        self.send(f'tap {button["x"]} {button["y"]}')
        self.drain(0.5)

    def wait_playing(self, previous_title=None):
        deadline = time.monotonic() + 35
        while time.monotonic() < deadline:
            self.drain(1)
            state = self.music()
            if (state.get('playing') and not state.get('paused') and state.get('position_ms', 0) > 1500
                    and (previous_title is None or state['title'] != previous_title)):
                return state
        raise RuntimeError('Playback did not advance: ' + json.dumps(state, ensure_ascii=False))

    def close(self):
        self.port.close()
        self.log.close()


def run(args):
    args.out.mkdir(parents=True, exist_ok=True)
    d = Device(args.port, args.out / 'serial.log')
    checks = []

    def check(name, valid, detail=None):
        checks.append({'name': name, 'passed': bool(valid), 'detail': detail})
        print(('PASS ' if valid else 'FAIL ') + name, flush=True)
        if not valid:
            raise AssertionError(name)

    try:
        d.drain(0.5)
        # A normal reset may return to app0. Enter only the music application.
        status = d.status()
        if status.get('implementation') != 'music-2.5.0':
            if not status.get('wifi'):
                raise RuntimeError('Desktop Wi-Fi is disconnected; connect Wi-Fi and enter the music app first')
            d.send('app voice')
            d.drain(22)
        if not d.status().get('wifi'):
            raise RuntimeError('Music app Wi-Fi is disconnected')
        # Wi-Fi can be ready while cloud setup is still activating. Companion
        # initialization and scheduled volume commands follow that setup.
        deadline = time.monotonic() + 45
        status = d.status()
        while status.get('state') != 3 and time.monotonic() < deadline:
            d.drain(2)
            status = d.status()
        if status.get('state') != 3:
            raise RuntimeError('Music application has not reached idle')
        d.drain(1)
        d.send('volume 40')
        d.send('musicstop')
        d.drain(2)
        initial = d.ui()
        # Make reruns independent of an open chooser left by a failed attempt.
        if initial['playlistVisible']:
            d.tap('closePlaylist', initial)
            initial = d.ui()
        check('playlist hidden until tapped', not initial['playlistVisible'], initial)
        check('idle playlist entry visible', initial['playlistEntryVisible'] and not initial['controlsVisible'])
        d.tap('playlistEntry', initial)
        d.drain(5)
        chooser = d.ui()
        check('tap opens real playlist list', chooser['playlistVisible'] and bool(chooser['playlists']), chooser)
        d.tap('closePlaylist', chooser)
        check('close hides list', not d.ui()['playlistVisible'])
        d.tap('playlistEntry')
        d.drain(3)
        chooser = d.ui()
        target = next(name for name in chooser['playlists'] if args.playlist in name)
        d.tap(target, chooser)
        first = d.wait_playing()
        d.drain(1)
        playing = d.ui()
        check('select playlist starts playback and folds list', playing['controlsVisible'] and not playing['playlistVisible'], first)
        d.tap('pause', playing)
        d.drain(1)
        paused = d.music()
        check('touch pauses playback', paused['playing'] and paused['paused'])
        d.drain(3)
        frozen = d.music()
        check('pause holds position', frozen['position_ms'] == paused['position_ms'], frozen)
        d.tap('pause')
        resumed = d.wait_playing()
        check('touch resumes from saved position', resumed['position_ms'] >= paused['position_ms'])
        d.tap('next')
        following = d.wait_playing(first['title'])
        check('next changes track', following['title'] != first['title'], following)
        d.tap('previous')
        previous = d.wait_playing(following['title'])
        check('previous returns to first track', previous['title'] == first['title'], previous)
        d.tap('playlist')
        d.drain(3)
        chooser = d.ui()
        check('playlist opens while music continues', chooser['playlistVisible'] and not d.music()['paused'])
        d.tap('closePlaylist', chooser)
        d.drain(2)
        check('closing playlist keeps playback', not d.ui()['playlistVisible'] and d.music()['playing'])
    finally:
        d.close()
        (args.out / 'result.json').write_text(json.dumps({'checks': checks}, ensure_ascii=False, indent=2), encoding='utf-8')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', default='COM8')
    parser.add_argument('--playlist', default='酷狗我喜欢')
    parser.add_argument('--out', type=Path, default=Path('build/deploy/music-touch-test-20261102'))
    run(parser.parse_args())

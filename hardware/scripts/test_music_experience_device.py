"""Validate playlist touch playback, lyrics, shuffle history and reboot resume."""
import argparse
import hashlib
import json
import subprocess
import sys
import time
from pathlib import Path

from inspect_device import capture, command, open_device, read_status, warmup


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', default='COM8')
    parser.add_argument('--image', type=Path, required=True)
    parser.add_argument('--out', type=Path, default=Path('build/music-experience-device'))
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    report = {'checks': [], 'samples': []}
    port = open_device(args.port)

    def save():
        (args.out / 'report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')

    def state():
        return read_status(port)

    def wait(predicate, seconds=50):
        end = time.monotonic() + seconds
        while True:
            value = state()
            if predicate(value):
                assert not value['audioError'], value['audioError']
                return value
            if time.monotonic() > end:
                raise RuntimeError('Timeout: ' + json.dumps(value['music'], ensure_ascii=False))
            time.sleep(.5)

    def tap(x, y):
        command(port, f'tap {x} {y}')
        time.sleep(.7)

    def passed(name):
        report['checks'].append(name)
        save()
        print('PASS:', name, flush=True)

    def pick_mode(mode):
        tap(36, 214)
        tap(150, 88 + mode * 43)
        assert state()['music']['mode'] == mode

    try:
        warmup(port)
        boot = state()
        assert boot['sketchMd5'] == hashlib.md5(args.image.read_bytes()).hexdigest()
        wait(lambda s: s['wifi'], 70)
        command(port, 'app music')
        initial = wait(lambda s: s['app'] == 16 and s['music']['ready'])
        report['initial'] = initial
        tap(284, 214)
        tap(250, 56)  # Existing three-song demo playlist; no server mutation.
        tap(108, 174)
        music = wait(lambda s: s['music']['position_ms'] > 2000 and not s['music']['loading'])['music']
        selected = music['track_id']
        assert music['playlist'] == 1 and not music['paused']
        passed('one touch on a playlist song starts it directly')
        tap(160, 214)
        wait(lambda s: s['music']['paused'])
        tap(284, 214)
        tap(108, 174)
        wait(lambda s: s['music']['track_id'] == selected and not s['music']['paused'] and s['music']['position_ms'] > 1000)
        passed('playlist song selection also starts while previously paused')
        wait(lambda s: s['music']['has_lyrics'])
        tap(160, 149)
        capture(port, args.out / 'lyrics.png')
        command(port, 'back')
        time.sleep(.6)
        passed('lyrics download independently and the lyrics page renders')
        pick_mode(2)
        tap(224, 214)
        wait(lambda s: s['music']['track_id'] != selected and s['music']['position_ms'] > 1000)
        tap(96, 214)
        wait(lambda s: s['music']['track_id'] == selected and s['music']['position_ms'] > 1000)
        passed('shuffle changes songs and previous returns to the actual last song')
        tap(160, 214)
        wait(lambda s: s['music']['paused'])
        tap(110, 170)
        checkpoint = wait(lambda s: s['music']['paused'] and not s['music']['loading'] and s['music']['position_ms'] > 50000)
        position = checkpoint['music']['position_ms']
        time.sleep(1)
        assert state()['music']['position_ms'] == position
        wait(lambda s: not s['music']['memory_pending'] and not s['music']['memory_save_failed'])
        report['checkpoint'] = state()
        passed('seeking while paused stays paused and saves its position')
        tap(160, 149)
        capture(port, args.out / 'lyrics-synced.png')
        command(port, 'back')
        time.sleep(.6)
        save()
        port.close()
        reset = subprocess.run([sys.executable, '-m', 'esptool', '--chip', 'esp32s3',
                                '--port', args.port, '--before', 'usb_reset', '--no-stub',
                                'chip_id'], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=30)
        (args.out / 'reset.log').write_bytes(reset.stdout)
        assert reset.returncode == 0
        time.sleep(3)
        port = open_device(args.port)
        # Open the player explicitly: reboot must not auto-start music.
        warmup(port)
        command(port, 'app music')
        restored = wait(lambda s: s['music']['ready'], 70)
        music = restored['music']
        assert music['remembered'] and not music['playing'] and music['paused'], music
        assert music['track_id'] == selected and music['mode'] == 2 and music['playlist'] == 1
        assert abs(music['position_ms'] - position) < 64 and music['volume'] == initial['music']['volume']
        report['restored'] = restored
        time.sleep(.7)  # Allow the newly loaded catalog to refresh the widgets.
        capture(port, args.out / 'remembered.png')
        passed('device reboot restores song, playlist, mode, volume and progress, awaiting play')
        wait(lambda s: s['wifi'], 70)
        tap(160, 214)
        wait(lambda s: s['music']['playing'] and not s['music']['paused'] and s['music']['position_ms'] > position + 1500)
        passed('play resumes from the saved breakpoint after reboot')
        physical_touches = state()['physicalTouchPresses']
        start = time.monotonic()
        while time.monotonic() - start < 30:
            value = state()
            music = value['music']
            assert music['playing'] and not music['paused'] and not music['error'], music
            assert music['track_id'] == selected, 'Track changed during measurement'
            report['samples'].append(dict(music, elapsed=time.monotonic() - start,
                                          uptime_ms=value['uptimeMs'],
                                          physical_touches=value['physicalTouchPresses']))
            save()
            assert value['physicalTouchPresses'] == physical_touches, 'Physical touch interrupted playback measurement'
            time.sleep(3)
        first, last = report['samples'][0], report['samples'][-1]
        rate = (last['position_ms'] - first['position_ms']) / (last['elapsed'] - first['elapsed'])
        assert 950 < rate < 1050 and last['underruns'] == first['underruns'], rate
        report['playback_ms_per_second'] = rate
        passed('playback stays at normal speed while checkpoints and lyrics run')
        tap(160, 214)
        pick_mode(0)
        # Return to the user's pre-update song (third row of their main playlist)
        # and checkpoint, when this installation's known fixture still matches.
        before_path = args.out.parent / 'before-status.json'
        if before_path.exists():
            before = json.loads(before_path.read_text(encoding='utf-8'))
            original = before['music']
            tap(284, 214)
            tap(90, 56)
            tap(108, 174)
            current = wait(lambda s: s['music']['playing'] and s['music']['position_ms'] > 1000)
            if current['music']['title'] == original['title']:
                tap(160, 214)
                music = state()['music']
                coordinate = 18 + round(284 * original['position_ms'] / music['duration_ms'])
                tap(coordinate, 170)
                wait(lambda s: s['music']['paused'] and not s['music']['loading'])
            else:
                tap(160, 214)
        final = wait(lambda s: s['music']['paused'] and not s['music']['loading']
                     and not s['music']['memory_pending'] and not s['music']['memory_save_failed'])
        assert final['music']['volume'] == initial['music']['volume']
        for key in ['brightness', 'volume', 'touchCalibrated', 'batteryInstalled']:
            assert final[key] == initial[key], key
        assert final['timers']['alarmCount'] == initial['timers']['alarmCount']
        assert final['tetris']['best'] == initial['tetris']['best']
        report['final'] = final
        report['passed'] = True
        passed('other saved settings preserved; music left paused')
    finally:
        port.close()
        save()


if __name__ == '__main__':
    main()

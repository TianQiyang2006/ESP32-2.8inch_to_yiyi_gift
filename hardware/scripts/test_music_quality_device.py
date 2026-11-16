"""Check music playback on the attached board using the real touch controls."""
import argparse
import hashlib
import json
import time
from pathlib import Path

from inspect_device import command, open_device, read_status


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', default='COM8')
    parser.add_argument('--seconds', type=float, default=60)
    parser.add_argument('--out', type=Path, default=Path('build/music-quality-device'))
    parser.add_argument('--image', type=Path, help='Require the board to run this exact firmware image')
    args = parser.parse_args()
    if args.seconds < 10:
        parser.error('--seconds must be at least 10')
    args.out.mkdir(parents=True, exist_ok=True)
    report = {'checks': [], 'samples': []}

    def save():
        (args.out / 'playback.json').write_text(
            json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')

    with open_device(args.port) as port:
        initial = read_status(port)
        if args.image:
            assert initial['sketchMd5'] == hashlib.md5(args.image.read_bytes()).hexdigest(), 'Unexpected firmware image'
        report['initial'] = initial
        last_uptime = initial['uptimeMs']

        def status():
            nonlocal last_uptime
            state = read_status(port)
            assert state['uptimeMs'] >= last_uptime, 'Device restarted'
            last_uptime = state['uptimeMs']
            assert not state['audioError'], state['audioError']
            return state

        def wait_for(predicate, seconds=45):
            end = time.monotonic() + seconds
            state = status()
            while not predicate(state):
                if time.monotonic() >= end:
                    raise RuntimeError('Timeout: ' + json.dumps(state['music'], ensure_ascii=False))
                time.sleep(.5)
                state = status()
            return state

        def tap(x, y):
            command(port, f'tap {x} {y}')
            time.sleep(.6)

        def passed(name):
            report['checks'].append(name)
            save()
            print('PASS:', name, flush=True)

        try:
            wait_for(lambda s: s['wifi'], 60)
            command(port, 'musicstop')
            command(port, 'app music')
            state = wait_for(lambda s: s['app'] == 16 and s['music']['ready'])
            assert state['music']['songs'] > 0 and state['music']['playlists'] > 0
            passed('connected catalog and music page')
            # Play the current playlist through its first row, without assuming
            # particular song names, counts, saved settings, or database order.
            tap(284, 214)
            tap(108, 96)
            state = wait_for(lambda s: s['music']['position_ms'] > 2000 and not s['music']['loading'])
            first_track = state['music']['track']
            passed('touch selection starts playback')
            tap(160, 214)
            state = wait_for(lambda s: s['music']['paused'])
            position = state['music']['position_ms']
            time.sleep(1.5)
            assert status()['music']['position_ms'] == position
            tap(160, 214)
            wait_for(lambda s: not s['music']['paused'] and s['music']['position_ms'] > position + 1000)
            passed('pause holds position and resume advances')
            duration = status()['music']['duration_ms']
            target = duration * (110 - 18) / 284
            tap(110, 170)
            wait_for(lambda s: not s['music']['loading'] and abs(s['music']['position_ms'] - target) < 2500)
            passed('block-aligned seek resumes at requested position')
            tap(224, 214)
            wait_for(lambda s: s['music']['track'] != first_track and s['music']['position_ms'] > 1000)
            tap(96, 214)
            wait_for(lambda s: s['music']['track'] == first_track and s['music']['position_ms'] > 1000)
            passed('next and previous resume valid audio')
            start = time.monotonic()
            while time.monotonic() - start <= args.seconds:
                state = status()
                music = state['music']
                report['samples'].append(dict(music, elapsed=time.monotonic() - start))
                save()
                assert music['playing'] and not music['paused'] and not music['loading'], music
                assert not music['error'], music['error']
                assert music['track'] == first_track, 'Track changed during playback measurement'
                print('PLAY:', music['position_ms'], 'ms, buffer', music['buffered'],
                      ', underruns', music['underruns'], flush=True)
                time.sleep(min(3, max(.1, args.seconds - (time.monotonic() - start))))
            first, last = report['samples'][0], report['samples'][-1]
            rate = (last['position_ms'] - first['position_ms']) / (last['elapsed'] - first['elapsed'])
            report['position_ms_per_second'] = rate
            assert 950 < rate < 1050, rate
            assert last['underruns'] == first['underruns'], 'Audio buffer underrun'
            passed(f'continuous playback at {rate:.1f} ms/s with no new underruns')
            final = status()
            for key in ['brightness', 'volume', 'touchCalibrated', 'batteryInstalled']:
                assert final[key] == initial[key], key
            assert final['music']['volume'] == initial['music']['volume']
            assert final['timers'] == initial['timers']
            assert final['tetris']['best'] == initial['tetris']['best']
            report['final'] = final
            passed('saved volumes, reminders, touch calibration and high score preserved')
            report['passed'] = True
        finally:
            save()


if __name__ == '__main__':
    main()

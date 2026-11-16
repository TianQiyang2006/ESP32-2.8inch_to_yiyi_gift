"""Verify music navigation through the device's real LVGL touch input.

Works without a loaded catalog. Online song selection is covered separately by
test_music_experience_device.py and the native LVGL music runner.
"""
import argparse
import hashlib
import json
import time
from pathlib import Path

from PIL import Image, ImageChops
from inspect_device import capture, command, open_device, read_status, warmup


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', default='COM8')
    parser.add_argument('--image', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--playback', action='store_true', help='Also select and play a song when Wi-Fi is ready')
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    report = {'checks': [], 'passed': False}
    expected = hashlib.md5(args.image.read_bytes()).hexdigest()

    with open_device(args.port) as port:
        warmup(port)

        def send(text):
            command(port, text)
            time.sleep(.65)

        def state():
            value = read_status(port)
            assert value['sketchMd5'] == expected, value['sketchMd5']
            assert value['audioReady'] and not value['audioError']
            baseline = report.setdefault('physical_touch_baseline', value['physicalTouchPresses'])
            assert value['physicalTouchPresses'] == baseline, 'Physical touch interrupted the automatic test'
            return value

        def wait_music(predicate, seconds=45):
            end = time.monotonic() + seconds
            while time.monotonic() < end:
                music = state()['music']
                if predicate(music):
                    return music
                time.sleep(.3)
            raise AssertionError('Music did not reach expected state: ' + json.dumps(music, ensure_ascii=False))

        def shot(name):
            path = args.out / (name + '.png')
            capture(port, path)
            return Image.open(path).convert('RGB')

        def check(name, valid):
            assert valid, name
            report['checks'].append(name)
            print('PASS:', name, flush=True)

        try:
            initial = state()
            report['initial'] = initial
            # Enter from the launcher: app-level back follows its actual origin.
            send('page 6')
            send('app music')
            player = shot('device-player')
            check('music opens', state()['app'] == 16)
            if not state()['wifi']:
                check('offline state explains Wi-Fi requirement',
                      '网络未连接' in state()['music']['error'])
                send('tap 160 149')
                check('offline retry stays responsive without indefinite loading',
                      state()['app'] == 16 and not state()['music']['loading'])
            # x=41 was outside the original 0..39 back hit area.
            send('tap 284 214')
            listing = shot('device-playlist')
            check('queue opens a different view',
                  ImageChops.difference(player, listing).getbbox() is not None)
            if not state()['wifi']:
                send('tap 150 130')
                check('empty catalog retry explains the network state',
                      '网络未连接' in state()['music']['error'] and not state()['music']['loading'])
            send('tap 41 13')
            returned = shot('device-returned')
            check('back edge returns from playlist to music', state()['app'] == 16)
            if not state()['music']['playing']:
                check('playlist return restores the player',
                      ImageChops.difference(player, returned).getbbox() is None)
            for name, position in [('volume', '284 16'), ('mode', '36 214')]:
                send('tap ' + position)
                overlay = shot('device-' + name)
                check(name + ' opens', ImageChops.difference(returned, overlay).getbbox() is not None)
                send('tap 41 13')
                closed = shot('device-' + name + '-closed')
                if not state()['music']['playing']:
                    check(name + ' closes to player',
                          ImageChops.difference(returned, closed).getbbox() is None)
            send('tap 41 13')
            check('back edge exits music to launcher', state()['app'] == 0 and state()['page'] == 6)
            shot('device-launcher')
            send('app music')
            send('swipe 5 110 160 110')
            check('edge swipe exits music', state()['app'] == 0 and state()['page'] == 6)
            send('app music')
            if args.playback:
                wait_music(lambda m: m['ready'])
                send('tap 284 214')
                send('tap 250 56')
                send('tap 108 174')
                played = wait_music(lambda m: m['playlist'] == 1 and m['playing'] and not m['paused']
                                   and not m['loading'] and not m['buffering'] and m['position_ms'] > 2500)
                selected = played['track_id']
                check('one playlist tap starts real playback', bool(selected) and not played['error'])
                shot('device-playing')
                send('tap 160 214')
                paused = wait_music(lambda m: m['paused'])
                time.sleep(.8)
                check('pause holds actual audio position', state()['music']['position_ms'] == paused['position_ms'])
                send('tap 284 214')
                send('tap 108 174')
                replayed = wait_music(lambda m: m['track_id'] == selected and m['playing'] and not m['paused']
                                     and not m['loading'] and not m['buffering'] and m['position_ms'] > 500)
                check('selecting a song while paused starts it again', replayed['position_ms'] < paused['position_ms'])
                send('tap 284 214')
                send('tap 41 13')
                check('returning from playlist keeps actual playback', state()['app'] == 16
                      and state()['music']['playing'] and not state()['music']['paused'])
                shot('device-playing-returned')
                send('tap 160 214')
                wait_music(lambda m: m['paused'] and not m['memory_pending'])
                report['online_playback_tested'] = True
            final = state()
            for key in ['brightness', 'volume', 'touchMode', 'touchCalibrated', 'batteryInstalled']:
                check('preserved ' + key, final[key] == initial[key])
            check('preserved music volume', final['music']['volume'] == initial['music']['volume'])
            check('preserved alarm count', final['timers']['alarmCount'] == initial['timers']['alarmCount'])
            check('preserved countdown settings', final['countdownTimer'] == initial['countdownTimer'])
            report['final'] = final
            report.setdefault('online_playback_tested', False)
            report['passed'] = True
            shot('device-final')
        finally:
            (args.out / 'report.json').write_text(
                json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')


if __name__ == '__main__':
    main()

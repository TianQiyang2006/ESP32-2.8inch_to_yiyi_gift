"""Verify the three clock styles on COM8 via real touch/gesture routing.

Uses CRC-checked framebuffer captures; does not set the clock, Wi-Fi, display,
calibration, battery configuration or firmware partitions. Clock preset selection
is the only saved UI change. Physical finger/panel inspection remains separate.
"""
from pathlib import Path
import argparse
import hashlib
import json
import time
import serial
from PIL import Image
import inspect_device as device


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', default='COM8')
    parser.add_argument('--bin', type=Path, required=True)
    parser.add_argument('--out', type=Path, default=Path('build/clock-style-update-20261030/device'))
    parser.add_argument('--host', type=Path, default=Path('build/clock-style-update-20261030/host-clocks'))
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    md5 = hashlib.md5(args.bin.read_bytes()).hexdigest()
    stage = (0, 87, 320, 240)
    samples = []
    manifest = json.loads(Path('hardware/arduino/yiyi_desk/src/pages/assets/clock_gifs/manifest.json').read_text(encoding='utf-8'))
    clip_count = len(manifest)
    frames = [Image.open(args.host/f'gif-clock-{i}-still.png').crop(stage).tobytes() for i in range(clip_count)]
    picker_bottom = Image.open(args.host/'clock-picker.png').crop((0, 160, 320, 230)).tobytes()
    deadline = time.monotonic()+120
    while True:
        try:
            port = device.open_device(args.port)
            break
        except serial.SerialException:
            if time.monotonic() >= deadline:
                raise
            print('Serial port busy; waiting for the active device operation to finish', flush=True)
            time.sleep(3)
    with port:
        device.warmup(port, timeout=15)

        def status(page=None, app=None):
            until = time.monotonic()+5
            while True:
                state = device.read_status(port)
                if (not state.get('transitionActive') and
                    (page is None or state['page'] == page) and
                    (app is None or state['app'] == app)):
                    break
                if time.monotonic() > until:
                    raise AssertionError('Navigation failed to settle')
                time.sleep(.08)
            assert state['sketchMd5'] == md5, 'Firmware artifact mismatch'
            if samples:
                assert state['uptimeMs'] > samples[-1]['uptimeMs'], 'Device restarted'
            keys = ['uptimeMs', 'page', 'app', 'heapFree', 'psramFree', 'minHeapFree',
                    'largestFreeBlock', 'timeValid', 'resetReason', 'sketchMd5',
                    'touchMode', 'touchCalibrated', 'bgr', 'inverted', 'brightness',
                    'volume', 'batteryInstalled', 'externalPower', 'animFps']
            samples.append({key: state.get(key) for key in keys})
            (args.out/'status-samples.json').write_text(json.dumps(samples, indent=2), encoding='utf-8')
            return state

        def send(command, wait=.8, page=None, app=None):
            device.command(port, command)
            time.sleep(wait)
            return status(page, app)

        def capture(name):
            path = args.out/(name+'.png')
            device.capture(port, path)
            time.sleep(.35)
            return Image.open(path).copy()

        def still(name, clip):
            image = capture(name)
            assert image.crop(stage).tobytes() == frames[clip], f'Wrong still/clip: {name}, expected {clip}'
            return image

        def picker(name):
            image = capture(name)
            # A selected card has a 2px border rather than 1px; LVGL shifts
            # that thumbnail's content origin by a pixel. Check the stable
            # picker background plus the preview instead of that border offset.
            black = sum(max(pixel)<30 for pixel in image.crop((15,49,99,112)).get_flattened_data())
            assert image.crop((0,160,320,230)).tobytes() == picker_bottom and black>2000, 'Edge-back did not reach the picker'

        def double_tap():
            # Each synthetic press lasts 220 ms; a 290 ms onset interval gives
            # LVGL a real release and stays inside the 320 ms double-tap window.
            device.command(port, 'tap 160 170')
            time.sleep(.29)
            device.command(port, 'tap 160 170')
            time.sleep(.9)
            status(app=12)

        send('page 6', page=6, app=0)
        send('app clock', app=12)
        picker('clock-picker')
        send('tap 55 90', wait=1, app=12)
        # The selection stays in RAM across page visits; normalize it so a
        # rerun after a test interruption does not need a device reboot.
        present = capture('gif-initial-selection').crop(stage).tobytes()
        current = next((i for i, frame in enumerate(frames) if frame==present), None)
        assert current is not None, 'GIF face did not open, or the still frame differs from the host rendering'
        for _ in range((clip_count-current)%clip_count):
            double_tap()
        initial = still('gif-0-still', 0)
        time.sleep(1.3)
        paused = still('gif-0-idle', 0)
        assert initial.crop((0, 0, 320, 62)).tobytes() != paused.crop((0, 0, 320, 62)).tobytes(), 'Live clock stopped while GIF was idle'
        gif_mem = status()
        for clip in range(clip_count):
            if clip:
                double_tap()
                still(f'gif-{clip}-still', clip)
            started = time.monotonic()
            send('tap 160 170', wait=1.5, app=12)
            moving = capture(f'gif-{clip}-playing')
            assert moving.crop(stage).tobytes() != frames[clip], f'GIF {clip} did not animate'
            state = status()
            assert state['heapFree'] > 35000 and state['psramFree'] > 6*1024*1024
            time.sleep(max(0, started+manifest[clip]['durationMs']/1000+4-time.monotonic()))
            still(f'gif-{clip}-finished', clip)
            print(f'PASS: GIF {clip+1}/{clip_count} single playback, double selection, first-frame reset', flush=True)
        double_tap()
        still('gif-wrap-to-zero', 0)
        # Switching while a clip plays must cancel it and preview the next.
        send('tap 160 170', wait=1, app=12)
        double_tap()
        still('gif-switch-during-playback', 1)
        # A pending single tap must be cancelled on exit.
        device.command(port, 'tap 160 170')
        time.sleep(.29)
        send('swipe 5 120 160 120', app=12)
        picker('picker-after-gif-edge-back')
        send('tap 158 90', wait=1, app=12)
        pixel = capture('pixel-no-header')
        expected = Image.open(args.host/'pixel-clock.png')
        assert pixel.crop((0, 0, 320, 40)).tobytes() == expected.crop((0, 0, 320, 40)).tobytes()
        send('tap 20 18', app=12)
        after = capture('pixel-top-tap')
        assert after.crop((0, 0, 320, 40)).tobytes() == pixel.crop((0, 0, 320, 40)).tobytes()
        send('swipe 5 120 160 120', app=12)
        picker('picker-after-pixel-edge-back')
        send('tap 263 90', wait=1, app=12)
        flip = capture('flip-no-header')
        expected = Image.open(args.host/'flip-before.png')
        assert flip.crop((0, 0, 320, 40)).tobytes() == expected.crop((0, 0, 320, 40)).tobytes()
        time.sleep(1.1)
        flip_next = capture('flip-next-second')
        assert flip.crop((0, 58, 320, 174)).tobytes() != flip_next.crop((0, 58, 320, 174)).tobytes()
        send('swipe 5 120 160 120', app=12)
        picker('picker-after-flip-edge-back')
        baseline = send('page 6', page=6, app=0)
        for cycle in range(9):
            send('app clock', app=12)
            face = cycle % 3
            live = send(f'tap {[55,158,263][face]} 90', wait=1, app=12)
            if face in (0, 2):
                assert live['psramFree'] < baseline['psramFree']-100000, 'Clock face did not allocate its PSRAM buffers'
            if face == 0:
                send('tap 160 170', wait=.9, app=12)
            end = send('page 6', page=6, app=0)
            assert end['psramFree'] >= baseline['psramFree']-8192, 'Clock PSRAM retained after destruction'
            assert end['heapFree'] >= baseline['heapFree']-8192, 'Internal heap declined across clock re-entry'
        print('PASS: clean headers, edge-back, flip motion and 9 three-style lifecycle cycles', flush=True)
        for page in range(7):
            send(f'page {page}', wait=.5, page=page, app=0)
        for name, app in [('weather', 3), ('settings', 6)]:
            send('app '+name, app=app)
        send('app clock', app=12)
        send('tap 55 90', wait=1, app=12)
        for _ in range(clip_count-1):
            double_tap()
        still('gif-clock-final', 0)
        device.command(port, 'selftest')
        lines = []
        until = time.monotonic()+8
        while time.monotonic() < until:
            line = port.readline().decode('utf-8', errors='replace').strip()
            if line:
                lines.append(line)
            if line.startswith('SELFTEST '):
                break
        assert 'SELFTEST PASS' in lines, lines
        (args.out/'selftest.txt').write_text('\n'.join(lines)+'\n', encoding='utf-8')
        result = dict(expectedMd5=md5, gifMemory={k:gif_mem[k] for k in ['heapFree', 'psramFree']},
                      baseline={k:baseline[k] for k in ['heapFree', 'psramFree']}, samples=samples, selftest=lines)
        (args.out/'checks.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
        print('PASS: all seven pages, settings/weather and device SELFTEST', flush=True)


if __name__ == '__main__':
    main()

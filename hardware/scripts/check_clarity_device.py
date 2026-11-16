"""Inspect the final UI through real LVGL input and CRC-checked LCD frames.

Does not change mood records, Wi-Fi, volume, brightness or touch calibration.
Clock selection is the same action as picking a preset on the device.
"""
import json
import time
from pathlib import Path
from PIL import Image, ImageChops
from inspect_device import open_device, warmup, command, read_status, capture

OUT = Path('build/clarity-review/device')

def main():
    OUT.mkdir(parents=True, exist_ok=True)
    samples = []
    with open_device('COM8') as port:
        warmup(port)
        initial = read_status(port)
        assert initial['version'] == '0.2.4-clarity-ui', initial['version']
        assert initial['touchCalibrated'] and initial['audioReady']
        assert initial['displayDriver'] == 'TFT_eSPI' and initial['spiMHz'] == 20

        def go(view):
            command(port, view)
            time.sleep(.7)
            state = read_status(port)
            deadline = time.monotonic() + 2
            while state['transitionActive'] and time.monotonic() < deadline:
                time.sleep(.04)
                state = read_status(port)
            assert not state['transitionActive']
            assert state['uptimeMs'] > initial['uptimeMs']
            samples.append({'view': view, **state})
            return state

        def shot(name):
            capture(port, OUT / (name + '.png'))

        go('page 4'); shot('daily')
        go('tap 77 85'); shot('mood-picker'); go('back')
        go('page 5'); shot('message-board')
        go('tap 159 134'); shot('message-detail'); go('back')
        go('page 3'); shot('reading')
        go('tap 120 70'); shot('quote-detail'); go('back')
        go('tap 120 175'); shot('poem-detail'); go('back')
        go('app settings'); shot('settings')
        go('app voice'); shot('voice-idle'); go('back')

        for index, preset in enumerate(('landscape', 'matrix', 'flip')):
            go('app clock'); shot('clock-picker')
            assert go(f'tap {57 + index * 103} 95')['app'] == 12
            shot('clock-' + preset)
            if preset == 'matrix':
                time.sleep(1.2); shot('clock-matrix-next')
                first = Image.open(OUT / 'clock-matrix.png').crop((218, 165, 278, 209))
                next_frame = Image.open(OUT / 'clock-matrix-next.png').crop((218, 165, 278, 209))
                assert ImageChops.difference(first, next_frame).getbbox(), 'Seconds did not refresh'
            # Both directions return to the parent rather than switching desktop pages.
            gesture = 'swipe 40 120 280 120' if index == 1 else 'swipe 280 120 40 120'
            assert go(gesture)['app'] == 12
            shot('clock-picker-return-' + preset)
            assert go('back')['app'] == 0
        go('page 0'); shot('home')
        final = read_status(port)
        for key in ('brightness', 'volume', 'touchCalibrated', 'touchMode', 'bgr', 'inverted',
                    'displayDriver', 'spiMHz', 'externalPower', 'batteryInstalled'):
            assert final[key] == initial[key], key
    report = {'passed': True, 'initial': initial, 'final': final, 'samples': samples,
              'scope': 'Serial LVGL input and LCD frame verification; physical speech and glass quality require user feedback.'}
    (OUT / 'report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    print('CLARITY DEVICE PASS ' + str(OUT.resolve()))

if __name__ == '__main__':
    main()

"""Legacy device smoke check for the current hand-written UI firmware.

Kept for the saved-design review scripts; the board no longer runs the saved
design as its main UI (only pages 2/5/6 reuse the generated screens), so this
check now verifies version/status facts, all seven pages, selftest and settings
navigation. Full visual verification lives in build/review/device_*.py.
"""
import argparse
import json
import time
from pathlib import Path

from PIL import Image, ImageChops
from inspect_device import open_device, warmup, command, read_status, capture


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--port', default='COM8')
    parser.add_argument('--out', type=Path, default=Path('build/saved-design-review'))
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    manifest = json.loads(Path('hardware/arduino/yiyi_desk/src/saved_design/manifest.json').read_text(encoding='utf-8'))
    samples = []
    with open_device(args.port) as port:
        warmup(port, timeout=15)
        initial = read_status(port)
        assert initial['version'] == '0.5.0-handwritten-ui', initial
        assert initial['uiSource'] == 'src/pages (hand-written)', initial
        assert initial['uiDesignHash'] in (manifest['sha256'][:16], 'guiguider-full-sync'), initial
        assert initial['savedDesignPages'] == 3
        assert initial['voiceConnection']['installed']

        def go(view, page=None, app=None):
            command(port, view)
            time.sleep(.65)
            deadline = time.monotonic() + 5
            state = read_status(port)
            while state['transitionActive'] and time.monotonic() < deadline:
                time.sleep(.05)
                state = read_status(port)
            assert not state['transitionActive'], view
            assert state['uptimeMs'] > initial['uptimeMs'], view
            assert state['version'] == initial['version'], view
            if page is not None:
                assert state['page'] == page, (view, state)
            if app is not None:
                assert state['app'] == app, (view, state)
            samples.append({'view': view, **state})
            return state

        command(port, 'selftest')
        lines = []
        deadline = time.monotonic() + 6
        while time.monotonic() < deadline:
            line = port.readline().decode('utf-8', errors='replace').strip()
            if line:
                lines.append(line)
            if line == 'SELFTEST PASS':
                break
            assert 'FAIL' not in line, line
        assert 'SELFTEST PASS' in lines, lines
        (args.out / 'selftest.log').write_text('\n'.join(lines) + '\n', encoding='utf-8')

        for page in range(7):
            go(f'page {page}', page=page, app=0)
            capture(port, args.out / f'page-{page}.png')

        go('page 6', page=6, app=0)

        go('app settings', app=6)
        capture(port, args.out / 'settings.png')
        go('back', page=6, app=0)

        go('page 0', page=0, app=0)
        final = read_status(port)
        baseline_path = args.out / 'before-status.json'
        baseline = json.loads(baseline_path.read_text(encoding='utf-8')) if baseline_path.exists() else initial
        for key in ('brightness', 'volume', 'touchCalibrated', 'touchMode', 'bgr',
                    'inverted', 'displayDriver', 'spiMHz'):
            assert final[key] == baseline[key], (key, baseline[key], final[key])

    sheet = Image.new('RGB', (1280, 480), '#e6eee0')
    for page in range(7):
        sheet.paste(Image.open(args.out / f'page-{page}.png'), ((page % 4) * 320, (page // 4) * 240))
    sheet.paste(Image.open(args.out / 'settings.png'), (960, 240))
    sheet.save(args.out / 'seven-pages-contact.png')
    report = {'passed': True, 'initial': initial, 'final': final, 'samples': samples,
              'selftest': lines, 'scope': 'Actual device firmware, LVGL input and CRC-checked framebuffer capture.'}
    (args.out / 'device-verification.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    print('SAVED DESIGN DEVICE PASS: ' + str(args.out.resolve()), flush=True)


if __name__ == '__main__':
    main()

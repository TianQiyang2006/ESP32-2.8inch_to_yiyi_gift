"""Capture six real-device clock periods and restore the current time afterward.

Run after flashing the clock repair: python hardware/scripts/check_clock_clarity.py
The serial interface must support CRC-checked screenshots and timeset.
"""
import argparse
from datetime import datetime, timedelta, timezone
import json
from pathlib import Path
import time

from PIL import Image
from inspect_device import open_device, warmup, command, capture, read_status


def show_landscape(port):
    command(port, 'app clock')
    # App construction and the page transition can outlast the simulated tap.
    # Wait for the picker to finish before starting that tap's 350 ms lifetime.
    time.sleep(1)
    deadline = time.monotonic() + 5
    while read_status(port).get('transitionActive'):
        assert time.monotonic() < deadline, 'Clock picker transition did not finish'
        time.sleep(.1)
    command(port, 'tap 57 95')
    time.sleep(1)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', default='COM8')
    parser.add_argument('--out', type=Path, default=Path('build/clock-clarity-review'))
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    china = timezone(timedelta(hours=8))
    day = datetime.now(china).replace(minute=28, second=58, microsecond=0)
    samples = []
    with open_device(args.port) as port:
        warmup(port)
        before = read_status(port)
        try:
            show_landscape(port)
            for name, hour in [('predawn', 0), ('morning', 6), ('day', 11),
                               ('sunset', 16), ('dusk', 18), ('night', 21)]:
                command(port, 'timeset ' + str(int(day.replace(hour=hour).timestamp())))
                time.sleep(1)
                state = read_status(port)
                assert state['app'] == 12 and state['timeValid'], state
                capture(port, args.out / f'device-clock-{name}.png')
                actual = Image.open(args.out / f'device-clock-{name}.png').convert('RGB')
                expected = Image.open(Path(__file__).resolve().parents[2] /
                                      f'design/assets/clock-{name}.png').convert('RGB')
                # Compare landscape pixels below the live labels, using the same
                # RGB565 quantization as the firmware and serial capture.
                area = (0, 110, 320, 210)
                original = expected.crop(area).tobytes()
                pixels = bytes(value for i in range(0, len(original), 3)
                               for value in ((original[i] >> 3) * 255 // 31,
                                             (original[i + 1] >> 2) * 255 // 63,
                                             (original[i + 2] >> 3) * 255 // 31))
                assert actual.crop(area).tobytes() == pixels, f'Wrong background: {name}'
                samples.append({'period': name, 'backgroundMatchesSource': True, 'status': state})
        finally:
            # Always undo the inspection time, including on a failed screenshot.
            command(port, 'timeset ' + str(int(time.time())))
            time.sleep(1)
        capture(port, args.out / 'after-landscape.png')
        time.sleep(1.15)
        capture(port, args.out / 'after-landscape-next-second.png')
        command(port, 'selftest')
        result = []
        deadline = time.monotonic() + 8
        while time.monotonic() < deadline:
            line = port.readline().decode('utf-8', errors='replace').strip()
            if line:
                result.append(line)
            if line.startswith('SELFTEST '):
                break
        assert 'SELFTEST PASS' in result, result
        after = read_status(port)
        for field in ['brightness', 'volume', 'bgr', 'inverted', 'touchCalibrated']:
            assert before[field] == after[field], (field, before[field], after[field])
        assert after['uptimeMs'] > before['uptimeMs'], 'Device rebooted during capture'
    first = Image.open(args.out / 'after-landscape.png').convert('RGB')
    second = Image.open(args.out / 'after-landscape-next-second.png').convert('RGB')
    assert first.crop((210, 16, 314, 78)).tobytes() != second.crop((210, 16, 314, 78)).tobytes(), 'Seconds did not advance'
    contact = Image.new('RGB', (960, 480))
    for index, sample in enumerate(samples):
        contact.paste(Image.open(args.out / f'device-clock-{sample["period"]}.png'),
                      ((index % 3) * 320, (index // 3) * 240))
    contact.save(args.out / 'device-periods-contact.png')
    report = {'passed': True, 'before': before, 'after': after,
              'samples': samples, 'selftest': result, 'timeRestored': True}
    (args.out / 'device-clock-checks.json').write_text(
        json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    print('PASS: real-device six periods, advancing seconds, settings, selftests; current time restored.')


if __name__ == '__main__':
    main()

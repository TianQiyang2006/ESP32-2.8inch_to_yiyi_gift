"""Real USB checks for the desktop / official firmware bridge and audio counters."""
import argparse
import io
import json
import time
from pathlib import Path
from PIL import Image
import serial
from inspect_device import open_device, command, read_exact, capture, warmup, read_status

ROOT = Path(__file__).resolve().parents[2]

def wait_official(name, output, seconds=80):
    deadline = time.monotonic() + seconds
    logs = []
    while time.monotonic() < deadline:
        try:
            with open_device(name) as port:
                while time.monotonic() < deadline:
                    command(port, 'status')
                    end = time.monotonic() + 1.5
                    while time.monotonic() < end:
                        line = port.readline().decode('utf-8', 'replace').strip()
                        if line:
                            logs.append(line)
                        if line.startswith('{'):
                            try:
                                state = json.loads(line)
                            except ValueError:
                                continue
                            if state.get('implementation') == 'official-2.5.0':
                                output.write_text('\n'.join(logs), encoding='utf-8')
                                return state
        except (serial.SerialException, OSError):
            time.sleep(0.3)
    output.write_text('\n'.join(logs), encoding='utf-8')
    raise RuntimeError('Official application did not answer status')

def official_capture(port, output):
    warmup(port)
    command(port, 'screenshot')
    deadline = time.monotonic() + 15
    while time.monotonic() < deadline:
        line = port.readline()
        if line.startswith(b'JPEG '):
            size = int(line.split()[1])
            if not 100 < size < 200000:
                raise RuntimeError('Invalid JPEG length')
            data = read_exact(port, size)
            image = Image.open(io.BytesIO(data))
            image.load()
            if image.size != (320, 240):
                raise RuntimeError('Invalid official screen dimensions')
            image.save(output)
            return
    raise RuntimeError('Official screenshot missing')

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('action', choices=['enter', 'monitor', 'capture', 'exit'])
    parser.add_argument('--port', default='COM8')
    parser.add_argument('--seconds', type=float, default=45)
    parser.add_argument('--out', type=Path, default=ROOT / 'build/official-device-review')
    args = parser.parse_args()
    out = args.out
    out.mkdir(parents=True, exist_ok=True)
    if args.action == 'enter':
        with open_device(args.port) as port:
            warmup(port)
            state = read_status(port)
            assert state['version'] == '0.3.0-official-xiaozhi', state
            assert state['audioReady'] and state['touchCalibrated'], state
            assert state['voiceConnection']['installed'], state
            assert state['wifi'], 'Connect Wi-Fi in desktop settings before entering Xiaozhi'
            (out / 'desktop-status.json').write_text(json.dumps(state, ensure_ascii=False, indent=2), encoding='utf-8')
            command(port, 'page 0'); time.sleep(0.8); capture(port, out / 'home.png')
            command(port, 'page 6'); time.sleep(0.8); capture(port, out / 'apps.png')
            command(port, 'tap 58 161')
        state = wait_official(args.port, out / 'entry.log')
        (out / 'official-entry.json').write_text(json.dumps(state, indent=2), encoding='utf-8')
        print('Official application entered from the existing launcher icon', flush=True)
        print(json.dumps(state), flush=True)
    elif args.action == 'monitor':
        with open_device(args.port) as port:
            deadline = time.monotonic() + args.seconds
            next_status = 0
            logs = []
            samples = []
            while time.monotonic() < deadline:
                if time.monotonic() >= next_status:
                    command(port, 'status'); next_status = time.monotonic() + 2
                line = port.readline().decode('utf-8', 'replace').strip()
                if not line:
                    continue
                logs.append(line)
                if line.startswith('{'):
                    try:
                        state = json.loads(line)
                    except ValueError:
                        continue
                    samples.append(state)
                    print(json.dumps(state), flush=True)
                elif 'Application:' in line or 'ESP_ERROR_CHECK' in line or 'Guru Meditation' in line:
                    print(line, flush=True)
            (out / 'monitor.log').write_text('\n'.join(logs), encoding='utf-8')
            (out / 'audio-samples.json').write_text(json.dumps(samples, indent=2), encoding='utf-8')
    elif args.action == 'capture':
        with open_device(args.port) as port:
            official_capture(port, out / 'official.png')
        print(out / 'official.png')
    else:
        with open_device(args.port) as port:
            # Exercise the actual LVGL exit button, using the same input callback.
            command(port, 'tap 284 48')
        deadline = time.monotonic() + 25
        while time.monotonic() < deadline:
            try:
                with open_device(args.port) as port:
                    warmup(port)
                    state = read_status(port)
                    if state.get('version') == '0.3.0-official-xiaozhi':
                        assert state['page'] == 6 and state['app'] == 0, state
                        assert state['touchCalibrated'] and state['audioReady'], state
                        (out / 'desktop-return.json').write_text(json.dumps(state, ensure_ascii=False, indent=2), encoding='utf-8')
                        capture(port, out / 'returned-apps.png')
                        print('Official exit button returned to all features', flush=True)
                        return
            except (serial.SerialException, OSError, RuntimeError):
                time.sleep(0.3)
        raise RuntimeError('Official exit button did not return to the launcher')

if __name__ == '__main__':
    main()

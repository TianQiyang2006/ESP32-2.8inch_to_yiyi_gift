"""Verify desktop-only Wi-Fi entry and official reconnection on the real board."""
import argparse
import json
import time
from pathlib import Path
from inspect_device import open_device, warmup, command, read_status, capture
from check_official_device import official_capture

ROOT = Path(__file__).resolve().parents[2]

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('action', choices=['offline-entry', 'desktop-online', 'official-offline', 'official-online', 'official-stable'])
    parser.add_argument('--port', default='COM8')
    parser.add_argument('--seconds', type=float, default=80)
    parser.add_argument('--out', type=Path, default=ROOT / 'build/desktop-wifi-review')
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    samples = []
    with open_device(args.port) as port:
        warmup(port)
        initial = read_status(port)
        if args.action == 'official-offline':
            # A station can retain its link briefly after the AP is switched off.
            deadline = time.monotonic() + 45
            while initial['wifi'] and time.monotonic() < deadline:
                assert initial.get('implementation') == 'official-2.5.0' and not initial['wifiConfigMode'], initial
                time.sleep(1)
                initial = read_status(port)
            assert not initial['wifi'], 'Switch off the saved Wi-Fi AP before this check'
        if args.action == 'official-stable':
            assert initial['wifi'], 'Wait for official-online before running the stability check'
        if args.action == 'offline-entry':
            assert not initial['wifi'] and initial['voiceConnection']['installed'], initial
            command(port, 'page 6'); time.sleep(0.8)
            command(port, 'tap 58 161'); time.sleep(1)
            state = read_status(port)
            assert state.get('version') == initial['version'] and not state['wifi'], state
            assert state['uptimeMs'] > initial['uptimeMs'] and state['app'] == 10, state
            assert state['voiceStage'] == 6 and '桌面设置' in state['voiceMessage'], state
            capture(port, args.out / 'offline-entry.png')
            command(port, 'tap 26 22'); time.sleep(0.8)
            returned = read_status(port)
            assert returned['page'] == 6 and returned['app'] == 0, returned
            assert returned['touchCalibrated'] and returned['volume'] == initial['volume'] and returned['brightness'] == initial['brightness'], returned
            samples = [initial, state, returned]
        else:
            deadline = time.monotonic() + args.seconds
            while time.monotonic() < deadline:
                state = read_status(port)
                if args.action.startswith('official'):
                    assert state.get('implementation') == 'official-2.5.0', state
                    assert state['desktopWifi'] and not state['wifiConfigMode'] and state['state'] != 2, state
                    assert state['touchCalibrated'], state
                    if args.action == 'official-stable':
                        assert state['wifi'], 'Connected Wi-Fi dropped during the stability check'
                else:
                    assert state.get('version') == '0.3.0-official-xiaozhi', state
                if samples:
                    assert state['uptimeMs'] > samples[-1]['uptimeMs'], 'Unexpected restart'
                samples.append(state)
                if args.action.endswith('online') and state['wifi']:
                    break
                time.sleep(min(2, max(0, deadline - time.monotonic())))
            if args.action.endswith('online'):
                assert samples[-1]['wifi'], 'Saved desktop Wi-Fi did not connect'
            elif args.action == 'official-offline':
                assert args.seconds >= 65 and len(samples) >= 30, 'Need to cover the old 60-second timeout'
                assert all(not sample['wifi'] for sample in samples), 'Offline fixture was connected'
                official_capture(port, args.out / 'official-offline.png')
            else:
                assert samples[-1]['state'] in [3, 4, 5, 6], 'Official activation did not reach the conversation states'
                official_capture(port, args.out / 'official-connected.png')
        (args.out / (args.action + '.json')).write_text(json.dumps(samples, ensure_ascii=False, indent=2), encoding='utf-8')
        print('PASS ' + args.action + ': ' + json.dumps(samples[-1], ensure_ascii=False), flush=True)

if __name__ == '__main__':
    main()

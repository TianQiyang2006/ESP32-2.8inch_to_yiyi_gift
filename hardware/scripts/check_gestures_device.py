"""Device acceptance for V2 gesture routing (audit 01 section 6B, cases D1-D22).

Every case drives the real firmware over USB-CDC with the same serial helpers as
``inspect_device.py`` (``open_device``/``warmup``/``command``/``read_status``); the
``check()`` below mirrors the one nested in ``inspect_device.stress()``: it waits
for ``transitionActive`` to become false, requires a monotonic ``uptimeMs`` (no
restart) and then asserts the observable ``status.page``/``status.app``.

Simulated gestures are asynchronous, so every ``swipe`` is followed by >=0.5 s of
settling (contract section 8) and a status check. ``Touch::simulate`` now forces the
first down sample to the command start point, which is what makes the edge cases
(x0=0, dx=46) deterministic; if D2/D13/D14 ever flap, check that fix first.

Fixture requirements:
- normal run: no special fixture; the script never saves Wi-Fi credentials and
  never taps 保存/连接.
- D21 needs the saved AP switched off (offline desktop fixture).
- D22 needs a connected network (wifi + timeValid).
- D19 is skipped when the official Xiaozhi voice image is installed, because
  entering voice reboots into the official app; that path is covered by
  ``check_desktop_wifi_device.py offline-entry`` (D21).
- D5/D8/D10 normalise ``page`` before the serial ``app`` command, because the
  serial entry records origin = (current app, current page); without that the
  return target would depend on the previous case.
- D12 must drag the brightness slider (its track starts at x=16, inside the edge
  zone). The drag changes the persisted level, so the case restores the original
  percentage afterwards and fails if the restore drifts more than 6%.
The script always finishes on page 0.

Usage:
    python hardware/scripts/check_gestures_device.py
    python hardware/scripts/check_gestures_device.py --skip-regression   # D1-D19 only
    python hardware/scripts/check_gestures_device.py --port COM8 --out build/device-gestures/gestures.json
"""
import argparse
import json
import subprocess
import sys
import time
import traceback
from pathlib import Path

from inspect_device import open_device, warmup, command, read_status

if hasattr(sys.stdout, 'reconfigure'):
    sys.stdout.reconfigure(encoding='utf-8')

ROOT = Path(__file__).resolve().parents[2]
SCRIPTS = Path(__file__).resolve().parent
SETTLE = 0.5          # contract section 8: every simulated swipe waits >= 0.5 s
TAP_SETTLE = 0.6
TRANSITION_TIMEOUT = 2.0
STATUS_KEYS = ('page', 'app', 'transitionActive', 'uptimeMs', 'wifi', 'timeValid',
               'brightness', 'version', 'voiceConnection')


def compact(state):
    """Keep only the fields worth archiving in the JSON report."""
    return {key: state.get(key) for key in STATUS_KEYS if key in state}


class Device:
    """Serial helper + the transition/uptime assertions shared by every case."""

    def __init__(self, port):
        self.port = port
        self.steps = 0
        self.samples = []
        self.last_uptime = None

    def check(self, page=None, app=None):
        state = read_status(self.port)
        deadline = time.monotonic() + TRANSITION_TIMEOUT
        while state.get('transitionActive') and time.monotonic() < deadline:
            time.sleep(0.04)
            state = read_status(self.port)
        if state.get('transitionActive'):
            raise AssertionError('Page transition did not complete within 2 seconds')
        if self.last_uptime is not None and state['uptimeMs'] <= self.last_uptime:
            raise AssertionError('Device uptime did not increase (possible restart)')
        self.last_uptime = state['uptimeMs']
        if page is not None:
            assert state['page'] == page, f'Expected page {page}, got {state}'
        if app is not None:
            assert state['app'] == app, f'Expected app {app}, got {state}'
        self.steps += 1
        self.samples.append(compact(state))
        return state

    def settle(self, page=None, app=None, delay=SETTLE):
        time.sleep(delay)
        return self.check(page, app)

    def page(self, index, app=0):
        command(self.port, f'page {index}')
        return self.settle(page=index, app=app)

    def tap(self, x, y, page=None, app=None, delay=TAP_SETTLE):
        command(self.port, f'tap {x} {y}')
        return self.settle(page=page, app=app, delay=delay)

    def swipe(self, x1, y1, x2, y2, page=None, app=None):
        command(self.port, f'swipe {x1} {y1} {x2} {y2}')
        return self.settle(page=page, app=app)

    def open_app(self, name, from_page, app_number):
        """Serial ``app`` entry.

        Like the UI it records origin = (current app, current page), so the caller
        normalises the page first to keep the expected return target deterministic.
        """
        self.page(from_page)
        command(self.port, f'app {name}')
        return self.settle(app=app_number)


def run_script(identifier, argv, result, cwd=ROOT):
    print('+', ' '.join(str(part) for part in argv[2:]), flush=True)
    subprocess.run(argv, cwd=cwd, check=True)
    result['regressions'].append({'id': identifier, 'passed': True})


def run_regressions(args, result):
    """D20-D22 reuse the existing device scripts instead of re-implementing them."""
    python = sys.executable or 'python'
    stress_out = args.out.parent / 'stress.json'
    wifi_out = args.out.parent / 'desktop-wifi'
    voice_out = args.out.parent / 'voice-connection.json'
    initial = result.get('initial') or {'wifi': None, 'timeValid': None}
    if args.skip_regression:
        result['skipped'].append('D20-D22 (--skip-regression)')
        print('SKIP D20-D22 (--skip-regression)', flush=True)
        return
    # D20: navigation + App return regression (existing stress cycle).
    run_script('D20 inspect_device stress', [python, str(SCRIPTS / 'inspect_device.py'), 'stress',
                                             '--port', args.port, '--duration', str(args.stress_seconds),
                                             '--out', str(stress_out)], result)
    # D21: offline voice entry returns to page 6 / app 0 (needs the AP switched off).
    if not initial.get('wifi'):
        run_script('D21 desktop-wifi offline-entry', [python, str(SCRIPTS / 'check_desktop_wifi_device.py'),
                                                      'offline-entry', '--port', args.port,
                                                      '--out', str(wifi_out)], result)
    else:
        result['skipped'].append('D21 (saved Wi-Fi is connected; offline fixture required)')
        print('SKIP D21 (switch the saved Wi-Fi AP off before the full run)', flush=True)
    # D22: serial `back` cleanup in the voice connection check still works.
    if initial.get('wifi') and initial.get('timeValid'):
        run_script('D22 check_voice_connection', [python, str(SCRIPTS / 'check_voice_connection.py'),
                                                  '--port', args.port, '--out', str(voice_out)], result)
    else:
        result['skipped'].append('D22 (wifi and timeValid required)')
        print('SKIP D22 (needs a connected network with a valid clock)', flush=True)


def main():
    parser = argparse.ArgumentParser(description='V2 gesture acceptance (D1-D22) on the real board')
    parser.add_argument('--port', default='COM8')
    parser.add_argument('--out', type=Path, default=ROOT / 'build/device-gestures/gestures.json')
    parser.add_argument('--skip-regression', action='store_true',
                        help='skip D20-D22 (they reopen the port and take minutes / need fixtures)')
    parser.add_argument('--stress-seconds', type=float, default=60,
                        help='D20 inspect_device.py stress duration')
    args = parser.parse_args()
    args.out.parent.mkdir(parents=True, exist_ok=True)

    result = {'passed': False, 'cases': [], 'steps': 0, 'initial': None,
              'regressions': [], 'skipped': []}
    deck = None

    def run_case(identifier, case):
        before = deck.steps
        case()
        result['cases'].append({'id': identifier, 'passed': True, 'steps': deck.steps - before})
        print(f'PASS {identifier}', flush=True)

    try:
        with open_device(args.port) as port:
            warmup(port)
            deck = Device(port)
            initial = deck.check()
            result['initial'] = compact(initial)
            print('initial status: ' + json.dumps(result['initial'], ensure_ascii=False), flush=True)
            official_voice = str(initial.get('voiceConnection', {}).get('implementation', '')).startswith('official-')

            # D1: ordinary right swipe on a root page still advances (no regression).
            def d1():
                deck.page(0)
                deck.swipe(60, 120, 260, 120, page=1, app=0)
            run_case('D1 root page ordinary right swipe', d1)

            # D2: edge right swipe on the root page falls through to the next page.
            def d2():
                deck.page(0)
                deck.swipe(0, 120, 46, 120, page=1, app=0)
            run_case('D2 root page edge fallthrough', d2)

            # D3: left swipe is the previous page.
            def d3():
                deck.page(6)
                deck.swipe(260, 120, 60, 120, page=5, app=0)
            run_case('D3 root page left swipe previous', d3)

            # D4: page 6 edge right swipe wraps to page 0.
            def d4():
                deck.page(6)
                deck.swipe(0, 120, 46, 120, page=0, app=0)
            run_case('D4 page cycle boundary 6->0', d4)

            # D5: edge back inside an app returns to its origin (Calendar from page 6).
            def d5():
                deck.open_app('calendar', 6, 4)
                deck.swipe(0, 120, 46, 120, page=6, app=0)
            run_case('D5 edge back leaves Calendar to origin', d5)

            # D6: non-edge horizontal swipe inside an app is a no-op (decision A).
            def d6():
                deck.open_app('calendar', 6, 4)
                deck.swipe(60, 120, 260, 120, page=6, app=4)
            run_case('D6 non-edge swipe in app no action', d6)

            # D7: Clock decision A - a non-edge swipe must not dismiss the clock.
            def d7():
                deck.open_app('clock', 6, 12)
                deck.swipe(160, 120, 260, 120, page=6, app=12)
            run_case('D7 clock non-edge swipe no action', d7)

            # D8: edge back dismisses the Clock app to its origin.
            def d8():
                deck.open_app('clock', 6, 12)
                deck.swipe(0, 120, 46, 120, page=6, app=0)
            run_case('D8 clock edge back to origin', d8)

            # D9: the home-page clock card records page 0 as origin (P1-3 fix).
            def d9():
                deck.page(0)
                deck.tap(263, 71, page=0, app=12)   # home clock card (213,8,100x126)
                deck.swipe(0, 120, 46, 120, page=0, app=0)
            run_case('D9 home clock card returns to page 0', d9)

            # D10: Countdown opened from page 0 returns there (not Calendar/page 6).
            def d10():
                deck.open_app('countdown', 0, 5)
                deck.swipe(0, 120, 46, 120, page=0, app=0)
            run_case('D10 countdown returns to opening page', d10)

            # D11: two-level nesting page6 -> Settings -> Wi-Fi -> back -> back.
            def d11():
                deck.page(6)
                deck.tap(262, 161, page=6, app=6)   # Apps page Settings card
                deck.tap(100, 139, page=6, app=11)  # Settings row "Wi-Fi 配网" (y=126..153)
                deck.tap(26, 22, page=6, app=6)     # Wi-Fi -> Settings
                deck.tap(26, 22, page=6, app=0)     # Settings -> Apps root (no self-loop)
            run_case('D11 nested settings/wifi back', d11)

            # D12: the settings brightness slider starts at x=16, inside the edge
            # zone; a right drag from there is inputActive and must not leave the
            # app (P1-4). The drag itself changes the slider value, so restore it.
            def d12():
                original = initial.get('brightness')
                deck.open_app('settings', 6, 6)
                deck.swipe(16, 60, 120, 60, page=6, app=6)
                if isinstance(original, int) and 10 <= original <= 100:
                    restore_x = min(302, 16 + round((original - 10) * 288 / 90))
                    state = deck.tap(restore_x, 60, page=6, app=6)
                    assert abs((state.get('brightness') or 0) - original) <= 6, (
                        f'brightness restore failed: {state.get("brightness")} != {original}')
            run_case('D12 slider edge drag keeps settings', d12)

            # D13: two swipes issued back-to-back, the second while the return
            # transition runs, must not stack returns or restart the device. (The
            # status can only prove "at most one level + uptime", not which branch
            # dropped the gesture - see audit P2-4.)
            def d13():
                deck.open_app('calendar', 6, 4)
                before = deck.last_uptime
                command(port, 'swipe 0 120 46 120')
                time.sleep(0.1)                      # "immediately": still transitioning
                command(port, 'swipe 0 120 46 120')
                time.sleep(1.2)
                state = deck.check(page=6, app=0)
                assert state['uptimeMs'] > before, 'Device restarted during the double swipe'
            run_case('D13 double edge swipe during transition', d13)

            # D14: edge swipe on the generated holiday page turns the page only.
            def d14():
                deck.page(2)
                deck.swipe(0, 120, 46, 120, page=3, app=0)
            run_case('D14 holiday page edge swipe no click', d14)

            # D15: edge back first closes the reading quote detail, page unchanged;
            # the next swipe turns the page (detail must not stack a page turn).
            def d15():
                deck.page(3)
                deck.tap(160, 70, page=3, app=0)          # quote card (6,5,309x131)
                deck.swipe(0, 120, 46, 120, page=3, app=0)  # close detail only
                deck.swipe(0, 120, 46, 120, page=4, app=0)  # now the page turns
            run_case('D15 reading detail closes before page turn', d15)

            # D16: edge back closes the daily wood detail without turning the page.
            def d16():
                deck.page(4)
                deck.tap(88, 152, page=4, app=0)          # wood "更多 >" hit (72..108,144..166)
                deck.swipe(0, 120, 46, 120, page=4, app=0)
            run_case('D16 daily wood detail closes first', d16)

            # D17: Wi-Fi keyboard hierarchy - back exits editing, then Wi-Fi, then
            # Settings (requires the K2 keyboard + nav-back levels).
            def d17():
                deck.page(6)
                deck.tap(262, 161, page=6, app=6)    # Apps -> Settings
                deck.tap(100, 139, page=6, app=11)   # Settings -> Wi-Fi list
                deck.tap(160, 122, page=6, app=11)   # "手动输入网络名称" (list y=107..138)
                deck.tap(26, 22, page=6, app=11)     # leaves the keyboard only
                deck.tap(26, 22, page=6, app=6)      # leaves Wi-Fi
                deck.tap(26, 22, page=6, app=0)      # leaves Settings
            run_case('D17 wifi keyboard hierarchy', d17)

            # D18: serial `back` on a root page is a no-op (new root semantics).
            def d18():
                deck.page(3)
                command(port, 'back')
                deck.settle(page=3, app=0)
            run_case('D18 root page back no-op', d18)

            # D19: Voice entry still exits to page 6 / app 0. The official voice
            # image reboots into app 1, so skip it and rely on D21 there.
            def d19():
                if official_voice:
                    result['skipped'].append('D19 (official Xiaozhi voice implementation)')
                    print('SKIP D19 (official voice implementation; covered by D21)', flush=True)
                    return
                deck.open_app('voice', 6, 10)
                deck.tap(26, 22, page=6, app=0)
            run_case('D19 voice entry return', d19)

            # Contract section 8: finish on the root page.
            deck.page(0)
            result['steps'] = deck.steps
            result['passed'] = True
    except Exception:
        result['steps'] = deck.steps if deck is not None else 0
        result['error'] = traceback.format_exc()
        print('GESTURE DEVICE FAIL', flush=True)
        raise
    finally:
        result['out'] = str(args.out.resolve())
        result['status'] = 'PASS' if result['passed'] else 'FAIL'
        args.out.write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
        print('report: ' + result['out'], flush=True)

    # Existing device scripts are reused for the regression cases; they reopen the
    # port, so run them only after our session has closed it.
    time.sleep(0.5)
    try:
        run_regressions(args, result)
        result['skipped'] = result.get('skipped', [])
        args.out.write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
    except Exception:
        result['passed'] = False
        result['status'] = 'FAIL'
        result['error'] = traceback.format_exc()
        args.out.write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
        print('GESTURE DEVICE FAIL (regression stage)', flush=True)
        raise
    print(('GESTURE DEVICE PASS ' if result['passed'] else 'GESTURE DEVICE FAIL ') + result['out'], flush=True)


if __name__ == '__main__':
    main()

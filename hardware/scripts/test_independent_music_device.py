"""Exercise the flashed independent music App through its real touch callbacks."""
import json
import sys
import time
from pathlib import Path
from inspect_device import open_device, command, capture, read_status

OUT = Path('build/independent-music-device')
OUT.mkdir(parents=True, exist_ok=True)
if hasattr(sys.stdout, 'reconfigure'):
    sys.stdout.reconfigure(encoding='utf-8')


def status(port):
    state = read_status(port)
    (OUT / 'last-status.json').write_text(json.dumps(state, ensure_ascii=False, indent=2), encoding='utf-8')
    return state


def wait_for(port, predicate, seconds=30):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        state = status(port)
        if predicate(state):
            return state
        time.sleep(1)
    raise RuntimeError('Device state timeout: ' + json.dumps(state.get('music', {}), ensure_ascii=False))


def tap(port, x, y):
    command(port, f'tap {x} {y}')
    time.sleep(.8)


def main():
    results = []
    with open_device('COM8') as port:
        state = status(port)
        assert state['version'] == '0.9.0-music-app', state['version']
        assert state['timers']['alarmCount'] == 2 and state['tetris']['best'] == 145
        assert state['brightness'] == 30 and state['volume'] == 16
        command(port, 'musicstop')
        command(port, 'page 6')
        time.sleep(1)
        # Normalize to sheet 0 even if an earlier run left the launcher on sheet 1.
        # A right swipe either selects sheet 0 or leaves the launcher; page 6 then
        # reopens it without changing that sheet selection.
        command(port, 'swipe 60 120 260 120')
        time.sleep(1)
        command(port, 'page 6')
        time.sleep(1)
        command(port, 'swipe 260 120 60 120')
        time.sleep(1)
        capture(port, OUT / 'launcher-2.png')
        tap(port, 104, 170)
        state = wait_for(port, lambda s: s['app'] == 16)
        capture(port, OUT / 'music-before-play.png')
        state = wait_for(port, lambda s: s['music']['ready'], 45)
        assert state['music']['songs'] == 134 and state['music']['playlists'] == 2
        tap(port, 284, 18)
        tap(port, 160, 218)
        command(port, 'back')
        time.sleep(.8)
        state = status(port)
        assert state['music']['volume'] == 40
        results.append('catalog 134, two playlists, volume 40, original settings preserved')
        print('PASS', results[-1], flush=True)
        tap(port, 284, 214)
        capture(port, OUT / 'music-playlist.png')
        command(port, 'swipe 160 182 160 94')
        time.sleep(1)
        capture(port, OUT / 'music-playlist-scrolled.png')
        tap(port, 250, 56)
        capture(port, OUT / 'music-playlist-demo.png')
        # VPS demo order: Little Starlight, rain, Qing Tian.
        tap(port, 108, 174)
        state = wait_for(port, lambda s: s['music']['position_ms'] > 1500 and not s['music']['loading'], 45)
        assert state['music']['title'] == '晴天'
        first = state['music']['track']
        capture(port, OUT / 'music-playing.png')
        print('PLAYING', state['music']['title'], flush=True)
        tap(port, 160, 214)
        state = status(port)
        assert state['music']['paused']
        position = state['music']['position_ms']
        time.sleep(2)
        assert status(port)['music']['position_ms'] == position
        tap(port, 160, 214)
        wait_for(port, lambda s: s['music']['position_ms'] > position + 1000)
        results.append('touch pause freezes position and resume advances from same position')
        print('PASS', results[-1], flush=True)
        tap(port, 110, 170)
        wait_for(port, lambda s: not s['music']['loading'] and 80000 < s['music']['position_ms'] < 105000)
        results.append('ADPCM block-aligned seek resumes around the requested position')
        print('PASS', results[-1], flush=True)
        tap(port, 160, 149)
        capture(port, OUT / 'music-lyrics.png')
        command(port, 'back')
        time.sleep(.8)
        tap(port, 284, 18)
        capture(port, OUT / 'music-volume.png')
        tap(port, 160, 218)
        assert status(port)['music']['volume'] == 40
        tap(port, 294, 134)
        tap(port, 224, 214)
        state = wait_for(port, lambda s: s['music']['track'] != first and s['music']['position_ms'] > 1000, 45)
        tap(port, 96, 214)
        wait_for(port, lambda s: s['music']['track'] == first and s['music']['position_ms'] > 1000, 45)
        start = time.monotonic()
        samples = []
        while time.monotonic() - start < 60:
            state = status(port)
            sample = dict(state['music'], elapsed=round(time.monotonic()-start, 3))
            samples.append(sample)
            assert state['music']['error'] == '' and state['audioError'] == ''
            assert not state['music']['paused'], 'Playback was paused during rate sampling'
            time.sleep(3)
        rate = (samples[-1]['position_ms']-samples[0]['position_ms'])/(samples[-1]['elapsed']-samples[0]['elapsed'])
        assert 950 < rate < 1050, rate
        assert samples[-1]['underruns'] == 0, samples[-1]
        results.append(f'lyrics, volume, next/previous and 60 seconds playback: {rate:.1f} ms/s, zero buffer underruns')
        print('PASS', results[-1], flush=True)
        tap(port, 300, 170)
        wait_for(port, lambda s: s['music']['track'] != first and s['music']['position_ms'] > 1000, 30)
        tap(port, 96, 214)
        wait_for(port, lambda s: s['music']['track'] == first and s['music']['position_ms'] > 1500, 45)
        results.append('end of track advances automatically; returned to Qing Tian at volume 40')
        print('PASS', results[-1], flush=True)
        capture(port, OUT / 'music-final.png')
        (OUT / 'validation.json').write_text(json.dumps({'checks': results, 'samples': samples}, ensure_ascii=False, indent=2), encoding='utf-8')


if __name__ == '__main__':
    main()

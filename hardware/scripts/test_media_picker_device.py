"""Check FM/music pickers through the board's production touch pipeline.

Requires the user-authorized firmware to be installed. Serial taps/swipes are
LVGL pointer input, not direct events or player calls. This script owns the port.
It selects content and stops it afterwards; saved volumes/settings are preserved.
"""
from pathlib import Path
import argparse
import hashlib
import json
import time
import traceback

from PIL import Image
from inspect_device import open_device, warmup, command, read_status, capture


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', default='COM8')
    parser.add_argument('--image', required=True, type=Path)
    parser.add_argument('--out', required=True, type=Path)
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    expected = hashlib.md5(args.image.read_bytes()).hexdigest()
    report = {'passed': False, 'expected_image_md5': expected, 'checks': [], 'statuses': [],
              'serial_simulated_input': True, 'physical_finger_tested': False,
              'speaker_listening_tested': False, 'screenshots': []}
    log = (args.out / 'commands.log').open('w', encoding='utf-8', buffering=1)
    started = time.monotonic()
    last_uptime = 0

    def note(value):
        line = f'{time.monotonic()-started:.3f}s {value}'
        print(line, flush=True)
        log.write(line+'\n')

    def save():
        (args.out / 'verification.json').write_text(json.dumps(report, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')

    def check(name, result):
        if not result:
            raise AssertionError(name)
        report['checks'].append(name)
        note('PASS '+name)
        save()

    with open_device(args.port) as port:
        def send(value, delay=.6):
            note('COMMAND '+value)
            command(port, value)
            time.sleep(delay)

        def state(label):
            nonlocal last_uptime
            port.reset_input_buffer()
            result = read_status(port)
            assert result['sketchMd5'] == expected, 'Running image differs'
            assert result['uptimeMs'] >= last_uptime, 'Device restarted'
            assert not result['audioError'], result['audioError']
            last_uptime = result['uptimeMs']
            report['statuses'].append({'label': label, 'status': result})
            save()
            return result

        def wait(predicate, label, seconds=45):
            end = time.monotonic()+seconds
            while True:
                result=state(label)
                if predicate(result):
                    return result
                if time.monotonic() >= end:
                    raise RuntimeError('Timeout: '+label)
                time.sleep(.3)

        def shot(name):
            wait(lambda s:not s['transitionActive'], 'view settled', 10)
            path=args.out/(name+'.png')
            capture(port, path)
            report['screenshots'].append(str(path.resolve()))
            save()
            return Image.open(path).convert('RGB')

        try:
            warmup(port)
            initial = state('initial')
            # Ignore queued boot-time status before settings have been loaded.
            initial = wait(lambda s:s['uptimeMs'] >= 5000, 'boot complete', 15)
            send('radiostop')
            send('musicstop')
            # Each player lazily restores its independent saved volume at first
            # open. Compare loaded settings, not diagnostic startup defaults.
            send('app radio')
            send('app music')
            wait(lambda s:s['music']['ready'], 'real music catalog ready', 50)
            initial=state('loaded baseline')
            report['initial']=initial
            for app, app_id, key, entry in [('radio',28,'radio','276 18'), ('music',16,'music','284 214')]:
                send('page 6')
                send('app '+app)
                wait(lambda s:s['app']==app_id and not s['transitionActive'], app+' opened', 12)
                if app=='music':
                    wait(lambda s:s['music']['ready'], 'real music catalog ready', 50)
                base=shot(app+'-01-player')
                region=(43,3,125,33) if app=='radio' else (50,3,225,34)
                signature=base.crop(region).tobytes()

                def is_player(name):
                    current=shot(name)
                    return current.crop(region).tobytes()==signature

                list_open_count=0
                def open_list():
                    nonlocal list_open_count
                    list_open_count+=1
                    send('tap '+entry)
                    check(app+' stays in application when opening picker',state('picker open')['app']==app_id)
                    check(app+f' picker visibly opened #{list_open_count}',
                          not is_player(app+f'-list-open-{list_open_count}'))

                open_list()
                check(app+' picker is visibly different from player',not is_player(app+'-02-list'))
                send('tap 20 18')
                check(app+' immediately entered picker exits by header',is_player(app+'-03-immediate-back'))

                open_list()
                send('tap 140 101')
                check(app+' first row selection returns to player',is_player(app+'-04-first-selected'))
                if app=='radio':
                    selected=wait(lambda s:s['radio']['station']==0 and s['radio']['pcm_samples']>32000,
                                  'selected FM row produces PCM', 55)
                    check('FM selected the first displayed station', selected['radio']['station_id']=='beijing_music')
                else:
                    selected=wait(lambda s:s['music']['playing'] and not s['music']['paused'] and
                                  not s['music']['buffering'] and s['music']['position_ms']>1500,
                                  'selected music row produces playback', 45)
                    check('music selected a real catalog song',bool(selected['music']['track_id']))
                report[app+'_selected_playback']=selected[key]
                send('radiostop' if app=='radio' else 'musicstop')

                for cycle in range(2):
                    open_list()
                    before=state('before rapid back')[key]
                    # The old firmware reproduces a permanent input freeze with
                    # this exact release/next-press interval. Keep it unchanged.
                    send('swipe 140 177 140 90', .4)
                    send('tap 20 18')
                    check(app+f' rapid header after scroll #{cycle+1}',is_player(app+f'-rapid-back-{cycle+1}'))
                    after=state('after rapid back')[key]
                    field='station' if app=='radio' else 'track_id'
                    check(app+f' scroll/back does not select content #{cycle+1}',before[field]==after[field])

                    open_list()
                    send('swipe 140 177 140 90', .4)
                    send('tap 140 141')
                    content=state('rapid selection')[key]
                    check(app+f' row can be selected during scroll throw #{cycle+1}',
                          is_player(app+f'-rapid-selected-{cycle+1}'))
                    check(app+f' rapid selection requests playback #{cycle+1}',
                          (content['playing'] or content.get('connecting') or content.get('loading')) and
                          not content.get('paused',False))
                    send('radiostop' if app=='radio' else 'musicstop')

                open_list()
                send('swipe 140 177 140 90', .4)
                send('tap 140 219')
                check(app+' mini player exits picker during inertia',is_player(app+'-mini-return'))
                open_list()
                send('swipe 140 177 140 90', .4)
                send('swipe 5 110 160 110')
                check(app+' edge swipe exits picker',is_player(app+'-edge-return'))
                send('tap 20 18')
                check(app+' second back exits app to launcher',state('app exited')['app']==0)

            send('radiostop')
            send('musicstop')
            send('page 6')
            send('app radio')
            # Restore the prior station using stable player controls; a list
            # swipe's exact final offset depends on momentum and input timing.
            target=initial['radio']['station']
            if 0 <= target < 6:
                current=state('restore station start')['radio']['station']
                steps=(target-current)%6
                if steps > 3:
                    steps,coordinate=6-steps,'94 214'
                else:
                    coordinate='224 214'
                for _ in range(steps):
                    send('tap '+coordinate)
                check('restored previous FM station',state('station restored')['radio']['station']==target)
            send('radiostop')
            send('musicstop')
            final=wait(lambda s:not any(s['radio'][k] for k in ('playing','connecting','buffering','worker_active')),
                       'FM worker exited', 25)
            shot('final-fm-stopped')
            for field in ('brightness','volume','touchMode','touchCalibrated','bgr','inverted','panelRotation','batteryInstalled'):
                check('preserved '+field,final[field]==initial[field])
            for key in ('music','radio'):
                check('preserved '+key+' volume',final[key]['volume']==initial[key]['volume'])
            check('preserved reminders',final['timers']['alarmCount']==initial['timers']['alarmCount'] and
                  all(final['countdownTimer'][k]==initial['countdownTimer'][k] for k in ('durationSeconds','tone','volume')))
            check('players stopped',not final['radio']['playing'] and not final['music']['playing'])
            report['final']=final
            report['passed']=True
            note('PASS all picker device checks')
        except BaseException as error:
            report['failure']={'message':str(error),'traceback':traceback.format_exc()}
            note('FAIL '+str(error))
            try:
                report['failure_status']=state('failure')
                shot('failure')
                send('radiostop')
                send('musicstop')
            except Exception:
                pass
            raise
        finally:
            report['elapsed_seconds']=time.monotonic()-started
            save()
            log.close()


if __name__=='__main__':
    main()

"""Check the flashed desktop's clock faces over USB, without changing its time.

Uses simulated touches and CRC-checked LVGL captures. This verifies the firmware
rendering/input path; physical panel appearance is a separate visual check.
"""
from pathlib import Path
import argparse
import hashlib
import json
import time
from PIL import Image
import inspect_device as device


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port',default='COM8')
    parser.add_argument('--bin',type=Path,required=True)
    parser.add_argument('--out',type=Path,default=Path('build/clock-port/device'))
    args=parser.parse_args();args.out.mkdir(parents=True,exist_ok=True)
    expected=hashlib.md5(args.bin.read_bytes()).hexdigest()
    samples=[]
    with device.open_device(args.port) as port:
        device.warmup(port,timeout=12)

        def status(page=None,app=None):
            until=time.monotonic()+4
            while True:
                s=device.read_status(port)
                good=(not s.get('transitionActive') and
                      (page is None or s['page']==page) and
                      (app is None or s['app']==app))
                if good:break
                if time.monotonic()>until:raise AssertionError('Navigation did not settle')
                time.sleep(.08)
            assert s['sketchMd5']==expected,'Device is running another firmware artifact'
            if samples:assert s['uptimeMs']>samples[-1]['uptimeMs'],'Device restarted'
            samples.append({k:s.get(k) for k in ['uptimeMs','page','app','heapFree','psramFree','minHeapFree','largestFreeBlock','timeValid','resetReason','sketchMd5']})
            (args.out/'status-samples.json').write_text(json.dumps(samples,indent=2),encoding='utf-8')
            return s

        def send(text,wait=.4,page=None,app=None):
            device.command(port,text);time.sleep(wait);return status(page,app)

        def capture(name):
            path=args.out/(name+'.png');device.capture(port,path);return Image.open(path).copy()

        send('page 0',page=0,app=0);initial=status()
        send('page 6',page=6,app=0);capture('apps-no-pet')
        send('tap 58 77',app=12);capture('clock-picker')
        entered=time.monotonic();send('tap 158 90',wait=1,app=12);capture('pixel-clock')
        pixel=status()
        assert pixel['heapFree']>35000 and pixel['psramFree']>6*1024*1024
        send('tap 170 70',wait=.08,app=12);capture('pixel-clock-tap')
        time.sleep(max(0,entered+19.4-time.monotonic()))
        image=capture('pixel-clock-snake')
        green=sum(g>180 and r<150 and b<150 for r,g,b in image.crop((8,47,312,191)).get_flattened_data())
        assert green>50,'The automatic Pixel snake did not render'
        send('tap 20 18',wait=.8,app=12);capture('clock-picker-after-back')
        send('tap 263 90',wait=1,app=12);first_flip=capture('flip-clock')
        flip=status()
        assert flip['psramFree']>6*1024*1024
        time.sleep(1.15);next_flip=capture('flip-clock-next-second')
        assert first_flip.tobytes()!=next_flip.tobytes(),'Flip clock did not advance'
        send('tap 20 18',wait=.8,app=12)
        baseline=send('tap 20 18',wait=.8,page=6,app=0)
        # Compare the same fully built Apps page after warming both faces and
        # fonts. The cached-home shortcut intentionally retains its hidden layer.
        for cycle in range(10):
            send('app clock',wait=.7,app=12)
            face=send('tap 158 90' if cycle%2 else 'tap 263 90',wait=1,app=12)
            if cycle%2:
                assert face['heapFree']<baseline['heapFree']-15000,'Pixel face did not open'
            else:
                assert face['psramFree']<baseline['psramFree']-100000,'Flip face did not open'
            # The Apps page destroys the previous layer, including live flaps.
            send('page 6',wait=.7,page=6,app=0)
        after=status()
        assert after['psramFree']>=baseline['psramFree']-8192,'Clock buffers retained after exit'
        assert after['heapFree']>=baseline['heapFree']-8192,'Heap declined across clock re-entry'
        print('PASS: Pixel, automatic snake, split-flap, header back and 10 device lifecycle cycles',flush=True)
        for page in range(7):send(f'page {page}',wait=.3,page=page,app=0)
        for name,app in [('weather',3),('settings',6)]:send('app '+name,app=app)
        # Capture the changed clock as the reviewable final view. Voice app1 is
        # deliberately not booted during this desktop rendering check.
        send('app clock',wait=.8,app=12);send('tap 158 90',wait=1,app=12);capture('pixel-clock-final')
        device.command(port,'selftest');lines=[];until=time.monotonic()+6
        while time.monotonic()<until:
            line=port.readline().decode('utf-8',errors='replace').strip()
            if line:lines.append(line)
            if line.startswith('SELFTEST '):break
        assert 'SELFTEST PASS' in lines,lines
        (args.out/'selftest.txt').write_text('\n'.join(lines)+'\n',encoding='utf-8')
        (args.out/'checks.json').write_text(json.dumps({'expectedMd5':expected,'before':samples[0],'samples':samples,'pixel':{k:pixel[k] for k in ['heapFree','psramFree']},'flip':{k:flip[k] for k in ['heapFree','psramFree']},'snakeGreenPixels':green,'selftest':lines},ensure_ascii=False,indent=2),encoding='utf-8')
        print('PASS: all seven pages, weather/settings and board selftest',flush=True)


if __name__=='__main__':main()

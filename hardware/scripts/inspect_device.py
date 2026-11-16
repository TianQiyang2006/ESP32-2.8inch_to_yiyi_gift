"""Inspect the real device over USB CDC, or save its LVGL RGB565 framebuffer.

Examples: python inspect_device.py status
          python inspect_device.py capture --view 'page 0' --out build/home.png
The pixel capture verifies rendering; panel colour still needs visual inspection.
"""
import argparse
import json
import sys
import time
import zlib
from pathlib import Path
import serial
from PIL import Image

if hasattr(sys.stdout, 'reconfigure'):
    sys.stdout.reconfigure(encoding='utf-8')

def open_device(name):
    port = serial.Serial(port=None, baudrate=115200, timeout=0.1, write_timeout=3)
    # Set control lines before opening: default DTR/RTS can reset the S3.
    port.dtr = False
    port.rts = False
    port.port = name
    port.open()
    if hasattr(port, 'set_buffer_size'):
        port.set_buffer_size(rx_size=262144, tx_size=4096)
    return port

def command(port, text):
    port.write((text+'\n').encode())
    port.flush()

def read_exact(port, size):
    data = bytearray()
    deadline = time.monotonic()+20
    while len(data)<size and time.monotonic()<deadline:
        data.extend(port.read(min(8192,size-len(data))))
    if len(data)!=size:
        raise RuntimeError(f'Incomplete frame: {len(data)} / {size} bytes')
    return data

def capture(port, out, attempts=3):
    """Fetch one CRC-checked frame.

    Transfers occasionally arrive short or corrupt, and a view command sent right
    after the port opens can be swallowed before the sketch is listening. Retrying
    keeps a genuine firmware fault distinguishable from a flaky single attempt.
    """
    last=None
    for attempt in range(attempts):
        try:
            _capture_once(port, out); return
        except RuntimeError as error:
            last=error
            port.reset_input_buffer()
            time.sleep(0.4)
    raise last


def _capture_once(port, out):
    command(port,'screenshot')
    deadline=time.monotonic()+10
    while time.monotonic()<deadline:
        line=port.readline()
        if line.startswith(b'FRAME '):
            fields=line.split()
            if len(fields)!=5:
                raise RuntimeError('Firmware must support CRC-checked frames')
            _,width,height,size,checksum=fields
            width,height,size=int(width),int(height),int(size)
            if (width,height,size)!=(320,240,153600):
                raise RuntimeError(f'Unexpected frame dimensions: {fields}')
            data=read_exact(port,size)
            terminator=b''
            for _ in range(3):
                terminator=port.readline().strip()
                if terminator:
                    break
            if terminator!=b'END':
                raise RuntimeError(f'Invalid frame terminator: {terminator!r}')
            if zlib.crc32(data)!=int(checksum,16):
                raise RuntimeError('Frame CRC mismatch; refusing to save corrupt pixels')
            pixels=[]
            for i in range(0,size,2):
                c=data[i] | data[i+1]<<8
                r,g,b=c>>11,(c>>5)&63,c&31
                pixels.append((r*255//31,g*255//63,b*255//31))
            im=Image.new('RGB',(width,height)); im.putdata(pixels)
            out.parent.mkdir(parents=True,exist_ok=True); im.save(out)
            print(out.resolve()); return
    raise RuntimeError('Device did not return a framebuffer')


def warmup(port, timeout=6):
    """Wait until the sketch answers, so the first real command is not lost."""
    deadline=time.monotonic()+timeout
    while time.monotonic()<deadline:
        port.reset_input_buffer()
        command(port,'status')
        end=time.monotonic()+1.5
        while time.monotonic()<end:
            if port.readline().startswith(b'{'):
                return True
    raise RuntimeError('Device did not become responsive on the serial port')

def read_status(port):
    # The USB CDC link occasionally drops a byte, which corrupts one JSON line.
    # Re-send `status` instead of surfacing a JSONDecodeError to every caller.
    deadline=time.monotonic()+6
    command(port,'status')
    while time.monotonic()<deadline:
        line=port.readline()
        if line.startswith(b'{'):
            try:
                state=json.loads(line)
            except ValueError:
                command(port,'status')
                continue
            if 'uptimeMs' not in state:
                raise RuntimeError('Firmware must report uptime for stress checks')
            return state
        if b'Guru Meditation' in line or b'YIYI DESK boot' in line:
            raise RuntimeError(f'Device restarted during checks: {line!r}')
    raise RuntimeError('No device status response')

def stress(port, out, duration):
    samples=[]
    def check(page=None, app=None):
        state=read_status(port)
        deadline=time.monotonic()+2
        while state.get('transitionActive') and time.monotonic()<deadline:
            time.sleep(0.04);state=read_status(port)
        if state.get('transitionActive'):
            raise RuntimeError('Page transition did not complete within 2 seconds')
        # A simulated swipe is queued by the firmware loop, so the expected page/app
        # can land a few frames after the first status read. Poll briefly instead of
        # failing on the very first sample (this was a real 0.5 s-cadence flake).
        expect_deadline=time.monotonic()+1.0
        while ((page is not None and state['page']!=page) or
               (app is not None and state['app']!=app)) and time.monotonic()<expect_deadline:
            time.sleep(0.05);state=read_status(port)
        if samples and state['uptimeMs']<=samples[-1]['uptimeMs']:
            raise RuntimeError('Device uptime did not increase')
        if page is not None and state['page']!=page:
            raise RuntimeError(f'Expected page {page}, got {state}')
        if app is not None and state['app']!=app:
            raise RuntimeError(f'Expected app {app}, got {state}')
        samples.append(state)
        return state
    initial=check()
    for cycle in range(2):
        command(port,'page 0'); time.sleep(0.2); check(0,0)
        for gesture, pages in [('60 120 260 120',[1,2,3,4,5,6,0]),('260 120 60 120',[6,5,4,3,2,1,0])]:
            for page in pages:
                command(port,'swipe '+gesture); time.sleep(0.7); check(page,0)
        for x,y,expected_page,app in [(58,77,6,12),(160,77,5,0),(262,77,1,0),(58,161,6,10),(160,161,6,4),(262,161,6,6)]:
            # Every tile here enters the App from the Apps page (page 6). V2 Core
            # records that origin per App, so tapping the header back arrow must
            # return to page 6 / app 0 (tap Calendar or Settings -> tap 26 22 ->
            # page6/app0). The old single return pair only got the first level right.
            # The official voice app reboots into app1; its round trip is checked
            # separately by check_official_device.py, outside this desktop run.
            if app == 10 and initial.get('voiceConnection', {}).get('implementation', '').startswith('official-'):
                continue
            command(port,'page 6'); time.sleep(0.2); check(6,0)
            command(port,f'tap {x} {y}'); time.sleep(0.5); check(expected_page,app)
            if app:
                command(port,'tap 26 22'); time.sleep(0.5); check(6,0)
        print(f'Navigation cycle {cycle+1} PASS',flush=True)
    # Second-level App return (audit P0-1): page6 -> Settings -> Wi-Fi must keep the
    # Settings origin. Wi-Fi back -> Settings, Settings back -> page 6 / app 0.
    command(port,'page 6'); time.sleep(0.2); check(6,0)
    command(port,'tap 262 161'); time.sleep(0.5); check(6,6)   # Apps page -> Settings
    command(port,'tap 100 139'); time.sleep(0.8); check(app=11)  # Settings Wi-Fi row -> Wi-Fi
    command(port,'tap 26 22'); time.sleep(0.5); check(app=6)   # Wi-Fi back -> Settings (not page 6)
    command(port,'tap 26 22'); time.sleep(0.5); check(6,0)     # Settings back -> Apps page
    print('Settings -> Wi-Fi two-level return PASS',flush=True)
    command(port,'app calendar'); time.sleep(0.2); check(app=4)
    command(port,'tap 246 178'); time.sleep(0.4); check(app=4)
    # The confirm view is the calendar flow's sub-state, so back returns to the
    # calendar view itself (not the Calendar origin recorded when it was entered).
    command(port,'tap 253 225'); time.sleep(0.5); check(app=5)
    command(port,'tap 26 22'); time.sleep(0.5); check(app=4)
    command(port,'app settings'); time.sleep(0.2); check(app=6)
    command(port,'app phone'); time.sleep(0.2); check(app=7)
    command(port,'tap 26 22'); time.sleep(0.5); check(app=6)
    for view in ['about','diagnostics']:
        # Settings -> About/Diagnostics is the other second-level return: the App
        # origin recorded for both is Settings, so one back press must land there.
        command(port,'app '+view); time.sleep(0.2); check()
        command(port,'tap 26 22'); time.sleep(0.5); check(app=6)
    print('Calendar and nested return paths PASS',flush=True)
    deadline=time.monotonic()+duration
    while time.monotonic()<deadline:
        time.sleep(min(5,max(0,deadline-time.monotonic())))
        check(app=6)
    print(f'Continuous runtime for {duration:g}s PASS',flush=True)
    command(port,'page 6'); time.sleep(0.2); check(6,0)
    # No status commands here: they count as activity and would reset idle time.
    deadline=time.monotonic()+65
    while time.monotonic()<deadline:
        line=port.readline()
        if b'Guru Meditation' in line or b'YIYI DESK boot' in line:
            raise RuntimeError(f'Device restarted during idle check: {line!r}')
    final=check(0,0)
    report={'passed':True,'runtimeSeconds':(final['uptimeMs']-initial['uptimeMs'])/1000,
            'swipes':28,'appLaunches':12 if initial.get('voiceConnection', {}).get('implementation', '').startswith('official-') else 14,'idleReturnSeconds':65,'samples':samples}
    out.parent.mkdir(parents=True,exist_ok=True)
    out.write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    print('STRESS PASS '+str(out.resolve()),flush=True)

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('action',choices=['status','capture','selftest','command','sweep','stress'])
    parser.add_argument('--port',default='COM8')
    parser.add_argument('--view',default='')
    parser.add_argument('--out',type=Path)
    parser.add_argument('--duration',type=float,default=60)
    args=parser.parse_args()
    if args.duration<0:
        parser.error('--duration must be nonnegative')
    if args.out is None:
        args.out=Path('build/device-stress.json' if args.action=='stress' else 'build/device-capture.png')
    with open_device(args.port) as port:
        time.sleep(0.3); port.reset_input_buffer()
        warmup(port)
        if args.action=='sweep':
            views=['page '+str(i) for i in range(7)]+['app '+x for x in ['clock','weather','calendar','settings','phone','about','diagnostics']]
            # `app voice` starts the official Xiaozhi image in app1 and reboots the
            # board, so it must not be part of a desktop sweep. The voice entry is
            # covered by check_official_device.py instead.
            status=read_status(port)
            official=str(status.get('voiceConnection',{}).get('implementation','')).startswith('official-')
            if not official:
                views.append('app voice')
            for view in views:
                command(port,view); time.sleep(0.6)
                capture(port,args.out/(view.replace(' ','-')+'.png'))
            command(port,'page 0'); command(port,'status')
        elif args.action=='stress':
            stress(port,args.out,args.duration); return
        elif args.action=='capture':
            if args.view:
                command(port,args.view); time.sleep(0.8)
            capture(port,args.out); return
        else:
            command(port,args.view if args.action=='command' else args.action)
        deadline=time.monotonic()+3
        while time.monotonic()<deadline:
            line=port.readline()
            if line: print(line.decode('utf-8',errors='replace').rstrip())

if __name__=='__main__': main()

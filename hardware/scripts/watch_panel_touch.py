"""Record real CTP samples for physical verification, without injecting any taps."""
import argparse
import json
import time
from pathlib import Path
from inspect_device import open_device, warmup, command, read_status

def raw_mode(port):
    for _ in range(3):
        command(port, 'touchraw')
        end=time.monotonic()+1.5
        while time.monotonic()<end:
            line=port.readline().decode('utf-8', errors='replace').strip()
            if line.startswith('RAW reporting '):
                return ' ON:' in line
    raise RuntimeError('No touchraw acknowledgement')

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--port', default='COM8')
    parser.add_argument('--duration', type=float, default=45)
    parser.add_argument('--out', type=Path, default=Path('build/panel-fix-review/physical-touch.json'))
    args=parser.parse_args()
    if not 0<=args.duration<=60: parser.error('duration must be between 0 and 60 seconds')
    samples=[]
    with open_device(args.port) as port:
        warmup(port); initial=read_status(port)
        # The toggle's acknowledgement reveals the old mode; restore it at the end.
        now_on=raw_mode(port); originally_on=not now_on
        if not now_on and not raw_mode(port): raise RuntimeError('Cannot enable raw reporting')
        try:
            end=time.monotonic()+args.duration
            while time.monotonic()<end:
                line=port.readline().decode('utf-8', errors='replace').strip()
                if line.startswith('RAW '):
                    fields=line.split()
                    if len(fields)==8 and fields[3]=='MAP' and fields[6]=='MODE':
                        samples.append(dict(rawX=int(fields[1]), rawY=int(fields[2]),
                                            x=int(fields[4]), y=int(fields[5]), mode=int(fields[7])))
                elif 'Guru Meditation' in line or 'YIYI DESK boot' in line:
                    raise RuntimeError('Device restarted during physical touch observation')
        finally:
            if not originally_on: raw_mode(port)
        final=read_status(port)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(dict(source='physical I2C only; no synthetic taps',
                                       durationSeconds=args.duration, initial=initial,
                                       final=final, samples=samples),ensure_ascii=False,indent=2),encoding='utf-8')
    print(f'Recorded {len(samples)} physical presses: {args.out.resolve()}')
    print(f'Final page={final["page"]}, app={final["app"]}, mode={final["touchMode"]}')

if __name__=='__main__': main()

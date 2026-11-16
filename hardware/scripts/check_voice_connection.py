"""Check a real device's hello exchange; end microphone capture immediately."""
import argparse
import json
import time
from pathlib import Path
from inspect_device import open_device, warmup, command, read_status

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--port',default='COM8')
    parser.add_argument('--out',type=Path,default=Path('build/voice-smooth-review/connection.json'))
    args=parser.parse_args()
    samples=[]
    logs=[]
    passed=False
    with open_device(args.port) as port:
        warmup(port)
        initial=read_status(port)
        network_deadline=time.monotonic()+35
        while not (initial.get('wifi') and initial.get('timeValid')) and time.monotonic()<network_deadline:
            time.sleep(1);initial=read_status(port)
        command(port,'app voice');time.sleep(1)
        command(port,'tap 70 216')
        deadline=time.monotonic()+100
        next_status=time.monotonic()
        try:
            while time.monotonic()<deadline:
                if time.monotonic()>=next_status:
                    command(port,'status');next_status=time.monotonic()+1
                line=port.readline().decode('utf-8','replace').strip()
                if line.startswith('{'):
                    try:state=json.loads(line)
                    except json.JSONDecodeError:
                        # Diagnostics from another FreeRTOS task can interleave
                        # a serial status line; request another complete sample.
                        next_status=time.monotonic();continue
                    keys=('version','uptimeMs','heapFree','voiceStage','voiceMessage','voiceConnected','voiceConnection','wifi','timeValid')
                    sample={k:state.get(k) for k in keys};samples.append(sample)
                    print(json.dumps(sample,ensure_ascii=False),flush=True)
                    if state.get('voiceConnected') and state.get('voiceStage')==3:
                        passed=True;break
                    if state.get('voiceStage')==6:break
                elif line.startswith('VOICE '):
                    logs.append(line);print(line,flush=True)
                elif 'Guru Meditation' in line or 'YIYI DESK boot' in line:
                    raise RuntimeError('Device restarted during voice connection')
        finally:
            # Interrupt without listen-stop: this test does not ask for a reply.
            command(port,'tap 230 216');time.sleep(0.5)
            command(port,'back');time.sleep(0.8)
            final=read_status(port)
    report={'passed':passed,'samples':samples,'logs':logs,'final':final}
    args.out.parent.mkdir(parents=True,exist_ok=True)
    args.out.write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    print(('VOICE HELLO PASS ' if passed else 'VOICE HELLO FAILED ')+str(args.out.resolve()))

if __name__=='__main__':main()

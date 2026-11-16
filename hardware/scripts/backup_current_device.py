"""Resumable full Flash backup over ROM USB, without uploading or erasing.

Small, separately committed reads avoid this Windows USB connection's corrupt
stub frames and a very long single ROM transfer. No device write occurs here.
"""
from pathlib import Path
import argparse
import hashlib
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[2]
def main():
    parser=argparse.ArgumentParser();parser.add_argument('--port',default='COM8');parser.add_argument('--output',type=Path);args=parser.parse_args()
    out=args.output.resolve() if args.output else ROOT/'build/backups/esp32s3-before-seven-pages-20261008-16mb.bin'
    parts=out.parent/(out.stem+'-parts' if args.output else 'before-seven-pages-parts');parts.mkdir(parents=True,exist_ok=True)
    size=0x100000;total=0x1000000
    for address in range(0,total,size):
        part=parts/f'{address:08x}.bin';digest=part.with_suffix('.sha256')
        if part.exists() and part.stat().st_size==size and digest.exists() and hashlib.sha256(part.read_bytes()).hexdigest()==digest.read_text().strip():
            print(f'Existing verified block 0x{address:08x}',flush=True);continue
        success=False
        for attempt in range(3):
            result=subprocess.run([sys.executable,'-m','esptool','--chip','esp32s3','--port',args.port,'--before','usb_reset','--no-stub','read_flash',hex(address),hex(size),str(part)],capture_output=True)
            if result.returncode==0 and part.exists() and part.stat().st_size==size:success=True;break
            print(f'Block 0x{address:08x} retry {attempt+1}: '+result.stderr.decode(errors='replace')[-400:],flush=True)
        if not success:raise RuntimeError('Full backup incomplete; no device Flash was written')
        digest.write_text(hashlib.sha256(part.read_bytes()).hexdigest()+'\n')
        print(f'Backed up {address+size:,}/{total:,} bytes',flush=True)
    with out.open('wb') as stream:
        for address in range(0,total,size):stream.write((parts/f'{address:08x}.bin').read_bytes())
    assert out.stat().st_size==total
    digest=hashlib.sha256(out.read_bytes()).hexdigest();out.with_suffix('.sha256').write_text(digest+'\n')
    print('Full 16MiB backup complete: '+str(out),flush=True)
if __name__=='__main__':main()

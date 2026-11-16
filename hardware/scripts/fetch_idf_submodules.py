"""Fill source-archive submodules from Espressif's matching release ZIP.

HTTP ranges avoid downloading the release's 1.3 GB of Git history. Each extracted
file is checked against the official ZIP's CRC. No project files are changed.
"""
import argparse
import concurrent.futures
import json
from pathlib import Path
import struct
import urllib.request
import zlib

URL = 'https://dl.espressif.com/github_assets/espressif/esp-idf/releases/download/v5.4.3/esp-idf-v5.4.3.zip'

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--sdk',type=Path,required=True)
    parser.add_argument('--index',type=Path,required=True)
    args=parser.parse_args()
    entries=json.loads(args.index.read_text())
    selected=[]
    for entry in entries:
        relative=entry['name'].split('/',1)[-1]
        if not relative.startswith('components/') or relative.startswith(('components/bt/','components/openthread/')):
            continue
        if relative.endswith('/') or '/.git' in relative or (args.sdk/relative).exists():
            continue
        entry['relative']=relative
        selected.append(entry)
    selected.sort(key=lambda e:e['offset'])
    groups=[]
    for entry in selected:
        end=entry['offset']+entry['compressed']+1024
        if groups and entry['offset']-groups[-1][1]<65536 and end-groups[-1][0]<8*1024*1024:
            groups[-1][1]=max(end,groups[-1][1]);groups[-1][2].append(entry)
        else:groups.append([entry['offset'],end,[entry]])
    def fetch(group):
        start,end,files=group
        request=urllib.request.Request(URL,headers={'Range':f'bytes={start}-{end}'})
        for attempt in range(3):
            try:
                with urllib.request.urlopen(request,timeout=90) as response:
                    assert response.status==206
                    data=response.read()
                for entry in files:
                    offset=entry['offset']-start
                    fields=struct.unpack_from('<IHHHHHIIIHH',data,offset)
                    assert fields[0]==0x04034b50
                    payload=offset+30+fields[-2]+fields[-1]
                    compressed=data[payload:payload+entry['compressed']]
                    content=zlib.decompress(compressed,-15) if fields[3]==8 else compressed
                    assert len(content)==entry['size'] and zlib.crc32(content)==fields[6]
                    path=args.sdk/entry['relative'];path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(content)
                return len(files)
            except Exception:
                if attempt==2:raise
    print(f'Fetching {len(selected)} SDK submodule files in {len(groups)} ranges',flush=True)
    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
        for count in pool.map(fetch,groups):print('Verified',count,'files',flush=True)

if __name__=='__main__':main()

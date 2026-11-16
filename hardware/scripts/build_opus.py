"""Build pinned, fixed-point libopus for the existing ESP32-S3 Arduino toolchain."""
from pathlib import Path
import concurrent.futures
import hashlib
import re
import shutil
import subprocess
import tarfile

ROOT=Path(__file__).resolve().parents[2]
ARCHIVE=ROOT/'tools/downloads/opus-1.3.1.tar.gz'
SHA256='4834a8944c33a7ecab5cad9454eeabe4680ca1842cb8f5a2437572dbf636de8f'
SOURCE=ROOT/'tools/downloads/opus-1.3.1'
OUT=ROOT/'build/opus-esp32s3'

def sources(file,key):
    text=(SOURCE/file).read_text()
    match=re.search(r'^'+key+r'\s*=\s*(.*?)(?=\n[A-Z_0-9]+\s*=|\Z)',text,re.M|re.S)
    return re.findall(r'[\w/]+\.c',match.group(1))

def main():
    if not ARCHIVE.exists():
        ARCHIVE.parent.mkdir(parents=True,exist_ok=True)
        subprocess.run(['curl.exe','-fsSL','--max-time','120','-o',str(ARCHIVE),'https://github.com/xiph/opus/archive/refs/tags/v1.3.1.tar.gz'],check=True)
    if hashlib.sha256(ARCHIVE.read_bytes()).hexdigest()!=SHA256:
        raise RuntimeError('Opus archive checksum mismatch')
    if not SOURCE.exists():
        with tarfile.open(ARCHIVE) as stream: stream.extractall(SOURCE.parent,filter='data')
    compiler=next((Path.home()/'AppData/Local/Arduino15/packages/esp32/tools/xtensa-esp32s3-elf-gcc').glob('*/bin/xtensa-esp32s3-elf-gcc.exe'))
    OUT.mkdir(parents=True,exist_ok=True)
    (OUT/'config.h').write_text('''#pragma once
#define OPUS_BUILD 1
#define FIXED_POINT 1
#define DISABLE_FLOAT_API 1
#define USE_ALLOCA 1
#define HAVE_LRINT 1
#define HAVE_LRINTF 1
#define OPUS_VERSION "1.3.1"
''')
    files=sources('opus_sources.mk','OPUS_SOURCES')+sources('celt_sources.mk','CELT_SOURCES')+sources('silk_sources.mk','SILK_SOURCES')+sources('silk_sources.mk','SILK_SOURCES_FIXED')
    def compile_one(filename):
        obj=OUT/(filename.replace('/','_')+'.o')
        if not obj.exists() or obj.stat().st_mtime<(OUT/'config.h').stat().st_mtime:
            command=[str(compiler),'-c','-Os','-mlongcalls','-ffunction-sections','-fdata-sections','-fno-jump-tables','-DHAVE_CONFIG_H','-Wno-unused-parameter']
            for include in [OUT,SOURCE,SOURCE/'include',SOURCE/'celt',SOURCE/'silk',SOURCE/'silk/fixed']:command.extend(['-I',str(include)])
            result=subprocess.run(command+[str(SOURCE/filename),'-o',str(obj)],capture_output=True,text=True)
            if result.returncode:raise RuntimeError(filename+'\n'+result.stderr)
        return str(obj)
    with concurrent.futures.ThreadPoolExecutor(max_workers=6) as pool:objects=list(pool.map(compile_one,files))
    subprocess.run([str(compiler.with_name('xtensa-esp32s3-elf-ar.exe')),'rcs',str(OUT/'libopus.a'),*objects],check=True)
    headers=ROOT/'hardware/arduino/yiyi_desk/src/opus_api';headers.mkdir(exist_ok=True)
    for name in ['opus.h','opus_defines.h','opus_types.h']:shutil.copyfile(SOURCE/'include'/name,headers/name)
    shutil.copyfile(SOURCE/'COPYING',headers/'LICENSE-opus.txt')
    print(f'Opus 1.3.1 fixed-point: {len(files)} sources; static library {(OUT/"libopus.a").stat().st_size:,} bytes.')

if __name__=='__main__':main()

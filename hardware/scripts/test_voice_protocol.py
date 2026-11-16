"""Host tests for real voice framing and the pinned fixed-point Opus codec."""
from pathlib import Path
import concurrent.futures
import subprocess
from build_opus import sources, SOURCE, ROOT

OUT=ROOT/'build/host-voice';ZIG=ROOT/'tools/host-python/ziglang/zig.exe'
def main():
    OUT.mkdir(parents=True,exist_ok=True)
    config=OUT/'config.h'
    text='#pragma once\n#define OPUS_BUILD 1\n#define FIXED_POINT 1\n#define DISABLE_FLOAT_API 1\n#define USE_ALLOCA 1\n#define HAVE_LRINT 1\n#define HAVE_LRINTF 1\n#define OPUS_VERSION "1.3.1"\n'
    if not config.exists() or config.read_text()!=text:config.write_text(text)
    files=sources('opus_sources.mk','OPUS_SOURCES')+sources('celt_sources.mk','CELT_SOURCES')+sources('silk_sources.mk','SILK_SOURCES')+sources('silk_sources.mk','SILK_SOURCES_FIXED')
    includes=[OUT,SOURCE,SOURCE/'include',SOURCE/'celt',SOURCE/'silk',SOURCE/'silk/fixed']
    flags=['-target','x86_64-windows-gnu','-O2','-UNDEBUG','-DHAVE_CONFIG_H']
    for include in includes:flags+=['-I',str(include)]
    def compile_one(filename):
        source=SOURCE/filename;obj=OUT/(filename.replace('/','_')+'.o')
        if not obj.exists() or obj.stat().st_mtime<max(source.stat().st_mtime,config.stat().st_mtime):
            subprocess.run([str(ZIG),'cc',*flags,'-c',str(source),'-o',str(obj)],check=True,capture_output=True)
        return str(obj)
    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:objects=list(pool.map(compile_one,files))
    executable=OUT/'voice-protocol-test.exe'
    subprocess.run([str(ZIG),'c++',*flags,'-std=c++17','-I',str(ROOT),str(ROOT/'hardware/tests/voice_protocol_test.cpp'),*objects,'-o',str(executable)],check=True)
    subprocess.run([str(executable)],check=True)
if __name__=='__main__':main()

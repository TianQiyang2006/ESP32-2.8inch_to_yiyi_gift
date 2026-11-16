"""Test the VAD/session/bounded buffer classes used by the actual firmware."""
from pathlib import Path
import subprocess

ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'build/host-continuous-voice'

def main():
    OUT.mkdir(parents=True,exist_ok=True)
    executable=OUT/'continuous-voice-test.exe'
    subprocess.run([str(ROOT/'tools/host-python/ziglang/zig.exe'),'c++',
                    '-target','x86_64-windows-gnu','-std=c++17','-O1','-UNDEBUG',
                    '-I',str(ROOT),str(ROOT/'hardware/tests/continuous_voice_test.cpp'),
                    '-o',str(executable)],check=True)
    subprocess.run([str(executable)],check=True)

if __name__=='__main__':main()

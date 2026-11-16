"""Prepare short spoken reminders as the firmware's existing P3 Opus packets."""
import asyncio
import hashlib
import os
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

from music_library import ROOT


def ogg_to_p3(data):
    offset, packet, output = 0, bytearray(), bytearray()
    while offset < len(data):
        if data[offset:offset + 4] != b'OggS' or offset + 27 > len(data):
            raise ValueError('invalid Ogg page')
        segments = data[offset + 26]
        if offset + 27 + segments > len(data):
            raise ValueError('truncated Ogg segment table')
        lacing = data[offset + 27:offset + 27 + segments]
        body = offset + 27 + segments
        for length in lacing:
            if body + length > len(data):
                raise ValueError('truncated Ogg packet')
            packet.extend(data[body:body + length])
            body += length
            if length < 255:
                if not packet.startswith((b'OpusHead', b'OpusTags')):
                    if not 0 < len(packet) <= 4096:
                        raise ValueError('invalid Opus packet')
                    output.extend(struct.pack('!BBH', 0, 0, len(packet)))
                    output.extend(packet)
                packet.clear()
        offset = body
    if packet or not output or len(output) > 160000:
        raise ValueError('invalid or oversized voice asset')
    return output


async def generate(text, target):
    import edge_tts
    target.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=target.parent) as work:
        raw, ogg = Path(work) / 'voice.mp3', Path(work) / 'voice.ogg'
        await asyncio.wait_for(edge_tts.Communicate(text, 'zh-CN-XiaoxiaoNeural').save(str(raw)), 30)
        subprocess.run([str(ROOT / 'bin/ffmpeg'), '-nostdin', '-v', 'error', '-y', '-threads', '1',
                        '-protocol_whitelist', 'file,pipe', '-i', str(raw), '-t', '60',
                        '-ac', '1', '-ar', '16000', '-c:a', 'libopus', '-b:a', '20k',
                        '-frame_duration', '60', '-application', 'voip', '-threads', '1', str(ogg)],
                       timeout=20, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        converted = ogg_to_p3(ogg.read_bytes())
        tmp = Path(work) / 'voice.p3'
        tmp.write_bytes(converted)
        os.replace(tmp, target)


def prepare_voice(text):
    ident = hashlib.sha256(('xiaoxiao-v1:' + text).encode()).hexdigest()[:32]
    target = ROOT / 'data/alerts' / (ident + '.p3')
    if target.is_file():
        return ident
    try:
        subprocess.run([sys.executable, '-B', str(Path(__file__).resolve()), ident],
                       input=text.encode('utf-8'), timeout=55, check=True,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        return ident if target.is_file() else ''
    except (OSError, subprocess.SubprocessError):
        return ''


if __name__ == '__main__':
    text = sys.stdin.buffer.read(4096).decode('utf-8')
    ident = sys.argv[1]
    expected = hashlib.sha256(('xiaoxiao-v1:' + text).encode()).hexdigest()[:32]
    if ident != expected or not 1 <= len(text) <= 200:
        raise ValueError('invalid voice request')
    asyncio.run(generate(text, ROOT / 'data/alerts' / (ident + '.p3')))

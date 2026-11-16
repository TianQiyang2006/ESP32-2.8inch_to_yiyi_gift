"""Create two original diagnostic tracks, with no external audio dependency."""
import array
import json
import math
import random
import subprocess
import sys
import tempfile
import wave
from pathlib import Path


def create(ffmpeg, root):
    media, lyrics = root / 'media', root / 'lyrics'
    media.mkdir(parents=True, exist_ok=True)
    lyrics.mkdir(parents=True, exist_ok=True)
    rate = 16000
    # Original bell melody and a quiet, filtered noise bed for a sleep test.
    melody = [72, 76, 79, 74, 77, 81, 79, 76, 74, 79, 83, 81, 77, 76, 74, 72]
    rng = random.Random(5461)
    for name, duration in [('starlight', 40), ('rain', 45)]:
        audio = array.array('h')
        low = 0.0
        for n in range(rate * duration):
            t = n / rate
            fade = min(1, t / 2, (duration - t) / 3)
            if name == 'starlight':
                local = t % .625
                freq = 440 * 2 ** ((melody[int(t / .625) % len(melody)] - 69) / 12)
                env = (1 - math.exp(-local * 60)) * math.exp(-local * 7)
                value = env * (math.sin(2 * math.pi * freq * local) + .20 * math.sin(2 * math.pi * freq * 2 * local))
                value *= .17
            else:
                noise = rng.uniform(-1, 1)
                low = .94 * low + .06 * noise
                value = (.8 * low + .14 * noise) * .40
            audio.append(int(max(-1, min(1, value * fade)) * 32767))
        with tempfile.TemporaryDirectory() as directory:
            wav = Path(directory) / 'demo.wav'
            with wave.open(str(wav), 'wb') as stream:
                stream.setnchannels(1)
                stream.setsampwidth(2)
                stream.setframerate(rate)
                if sys.byteorder != 'little':
                    audio.byteswap()
                stream.writeframes(audio.tobytes())
            subprocess.run([ffmpeg, '-y', '-loglevel', 'error', '-i', str(wav),
                            '-ac', '1', '-ar', '16000', '-codec:a', 'libmp3lame',
                            '-b:a', '48k', str(media / (name + '.mp3'))], check=True)
    (lyrics / 'starlight.lrc').write_text(
        '[ti:依依的小星光]\n[ar:专属测试曲]\n[00:00.00]给依依的小星光\n'
        '[00:05.00]愿每个平凡的日子都有音乐\n[00:15.00]这是一段原创测试旋律\n'
        '[00:25.00]也可以试试切换频谱\n[00:35.00]音乐网关连接成功\n', encoding='utf-8')
    (lyrics / 'rain.lrc').write_text('[ti:晚安雨声]\n[00:00.00]晚安雨声 · 环境音测试\n[00:20.00]愿你今晚睡个好觉\n', encoding='utf-8')
    songs = [
        {'id': 'starlight', 'title': '依依的小星光', 'artist': '专属测试曲', 'media': 'starlight.mp3',
         'lyrics': 'starlight.lrc', 'aliases': ['测试音乐', '小星光'], 'tags': ['学习', '纯音乐', '温柔', '放松'], 'source': 'original diagnostic melody'},
        {'id': 'rain', 'title': '晚安雨声', 'artist': '环境音', 'media': 'rain.mp3',
         'lyrics': 'rain.lrc', 'aliases': ['雨声'], 'tags': ['睡眠', '雨声', '放松'], 'source': 'generated noise'},
    ]
    (root / 'catalog.json').write_text(json.dumps(songs, ensure_ascii=False, indent=2), encoding='utf-8')


if __name__ == '__main__':
    create(sys.argv[1], Path(sys.argv[2]))

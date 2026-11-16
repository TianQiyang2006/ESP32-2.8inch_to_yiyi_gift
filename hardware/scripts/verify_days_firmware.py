"""Check the final ESP32 image and its source copies without opening a device."""
from pathlib import Path
import argparse
import hashlib
import json
import re
import struct
from install_grokbot import app_descriptor, partitions, LAYOUT

ROOT = Path(__file__).resolve().parents[2]


def normalized(path):
    text = path.read_text(encoding="utf-8-sig")
    return re.sub(r'\A(?:#line[^\n]*\n)+', '', text)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, default=ROOT/'build/countdown-days-20261114')
    parser.add_argument('--build', type=Path, help='Actual compiler cache when reusing a completed shared build')
    args = parser.parse_args()
    out = args.out.resolve()
    sketch = ROOT/'hardware/arduino/yiyi_desk'
    origin_path = out/'firmware-origin.json'
    origin = json.loads(origin_path.read_text(encoding='utf-8')) if origin_path.exists() else {}
    build = args.build.resolve() if args.build else Path(origin.get('build_path', out/'arduino-build')).resolve()
    compiled = build/'sketch'
    paths = [p for p in sketch.rglob('*') if p.suffix in {'.cpp', '.c', '.h', '.hpp'}]
    results = {p.relative_to(sketch).as_posix():
               (compiled/p.relative_to(sketch)).exists() and
               normalized(p) == normalized(compiled/p.relative_to(sketch)) for p in paths}
    assert all(results.values()), 'Source copies differ: '+str([p for p, ok in results.items() if not ok])
    firmware = out/'firmware/yiyi_desk.ino.bin'
    assert firmware.stat().st_mtime >= max((compiled/p.relative_to(sketch)).stat().st_mtime for p in paths), 'Image predates the final source copies'
    data = firmware.read_bytes()
    app_descriptor(firmware, 'desktop')
    layout = partitions((out/'firmware/yiyi_desk.ino.partitions.bin').read_bytes())
    assert layout == LAYOUT, 'Unexpected partition layout'
    assert len(data) <= layout['app0'][3], 'Application exceeds app0'
    checksum, offset = 0xEF, 24
    for _ in range(data[1]):
        _, size = struct.unpack_from('<II', data, offset)
        offset += 8
        for byte in data[offset:offset+size]:
            checksum ^= byte
        offset += size
    checksum_at = (offset//16)*16+15
    assert data[checksum_at] == checksum, 'ESP image XOR checksum mismatch'
    assert data[23] == 1, 'Expected appended SHA256'
    digest_at = checksum_at+1
    assert hashlib.sha256(data[:digest_at]).digest() == data[digest_at:digest_at+32], 'ESP image SHA256 mismatch'
    report = {'passed': True, 'build_path': str(build), 'source_copies_checked': len(results), 'source_matches': results,
              'image_bytes': len(data), 'app0_bytes': layout['app0'][3],
              'remaining_bytes': layout['app0'][3]-len(data),
              'sha256': hashlib.sha256(data).hexdigest(),
              'esp_image_checksum_valid': True, 'esp_image_hash_valid': True,
              'firmware_flashed': False}
    (out/'firmware-verification.json').write_text(json.dumps(report, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    print(f"PASS: {len(results)} source copies; {len(data):,} bytes; app0 remaining {report['remaining_bytes']:,} bytes; image checksum/hash valid")


if __name__ == '__main__':
    main()

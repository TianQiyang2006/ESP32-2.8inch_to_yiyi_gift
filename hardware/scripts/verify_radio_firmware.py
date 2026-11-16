"""Verify FM build copies, LVGL sources, partition fit and ESP32 image offline.

This script never opens a serial port and never writes to a device.
"""
from pathlib import Path
import argparse
import hashlib
import json
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
SKETCH = ROOT / 'hardware/arduino/yiyi_desk'


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', default='build/online-fm-20261114')
    parser.add_argument('--version', default='0.19.0-online-fm')
    args = parser.parse_args()
    output = (ROOT / args.out).resolve()
    copied_root = output / 'arduino-build/sketch'
    ui = json.loads((output / 'ui/verification.json').read_text(encoding='utf-8'))
    assert ui['passed'] and ui['production_page'] and ui['pointer_input']
    for name, expected in ui['source_sha256'].items():
        assert sha(ROOT / name) == expected, f'UI source changed: {name}'

    source_hashes = {}
    mismatches = []
    units = 0
    latest_object = 0
    for original in sorted(SKETCH.rglob('*')):
        if not original.is_file() or original.suffix.lower() not in ('.c', '.cpp', '.h', '.csv'):
            continue
        relative = original.relative_to(SKETCH)
        copied = output / 'arduino-build/partitions.csv' if relative == Path('partitions.csv') else copied_root / relative
        source_hashes[relative.as_posix()] = sha(original)
        if not copied.is_file():
            mismatches.append(f'{relative}: build copy missing')
            continue
        actual = re.sub(br'^#line 1 [^\r\n]*\r?\n', b'', copied.read_bytes(), count=1)
        if actual != original.read_bytes():
            mismatches.append(f'{relative}: build copy differs')
        if original.suffix.lower() in ('.c', '.cpp'):
            units += 1
            obj = copied.with_name(copied.name + '.o')
            if not obj.exists() or obj.stat().st_mtime < copied.stat().st_mtime:
                mismatches.append(f'{relative}: compiled object missing or older than build copy')
            else:
                latest_object = max(latest_object, obj.stat().st_mtime)
    assert not mismatches, '\n'.join(mismatches)
    source_hashes['yiyi_desk.ino'] = sha(SKETCH / 'yiyi_desk.ino')
    generated = (copied_root / 'yiyi_desk.ino.cpp').read_text(encoding='utf-8')
    assert 'void setup() { deskSetup(); }' in generated and 'void loop() { deskLoop(); }' in generated

    firmware = output / 'firmware/yiyi_desk.ino.bin'
    image = firmware.read_bytes()
    build_image = output / 'arduino-build/yiyi_desk.ino.bin'
    assert sha(build_image) == sha(firmware), 'Exported firmware differs from the verified build image'
    assert image[0] == 0xe9 and len(image) <= 0x880000
    assert firmware.stat().st_mtime >= latest_object
    result = subprocess.run([sys.executable, '-m', 'esptool', '--chip', 'esp32s3', 'image_info', str(firmware)],
                            cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            encoding='utf-8', errors='replace', check=True)
    (output / 'firmware-image-info.log').write_text(result.stdout, encoding='utf-8')
    assert re.search(r'Checksum:.*\(valid\)', result.stdout)
    assert re.search(r'Validation Hash:.*\(valid\)', result.stdout)
    version = re.search(r'Version\s*=\s*"([^"]+)"', (SKETCH / 'board_config.h').read_text(encoding='utf-8')).group(1)
    assert version == args.version, f'Unexpected firmware version: {version}'
    assert version.encode('ascii') + b'\0' in image, 'Current version string missing from firmware'
    report = {
        'passed': True, 'version': version, 'chip': 'esp32s3',
        'source_files_checked': len(source_hashes), 'source_sha256': source_hashes,
        'compiled_translation_units_checked': units, 'build_source_copy_matches': True,
        'exported_image_matches_build': True, 'version_string_in_image': True,
        'ui_source_hashes_match': True, 'ui_checks': ui['checks'],
        'app0_offset': 0x10000, 'app0_bytes': 0x880000,
        'app_image_bytes': len(image), 'app0_remaining_bytes': 0x880000 - len(image),
        'esptool_image_valid': True,
        'artifacts': {p.name: {'bytes': p.stat().st_size, 'sha256': sha(p)}
                      for p in sorted((output / 'firmware').glob('*')) if p.is_file()},
        'serial_opened': False, 'firmware_flashed': False, 'physical_device_tested': False,
        'approval': 'User confirmation of UI and build is required before flashing.',
    }
    (output / 'firmware-verification.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({k: v for k, v in report.items() if k not in ('source_sha256', 'artifacts')}, ensure_ascii=True, indent=2))


if __name__ == '__main__':
    main()

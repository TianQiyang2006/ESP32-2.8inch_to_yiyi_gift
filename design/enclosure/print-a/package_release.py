"""Package only the verified design deliverables, without local tools or caches."""
from pathlib import Path
from hashlib import sha256
import json
import zipfile

ROOT = Path(__file__).resolve().parent
PREFIX = 'Xiaohei_A_3D_Print_v1'
OUT = ROOT / (PREFIX + '.zip')

files = [ROOT / name for name in (
    '打印与装配说明.md', 'build_enclosure.py', 'validate_fit.py',
    'parameters.json', 'requirements.txt',
    'output/Xiaohei_A_print_plate.3mf', 'output/validation.json',
    'vendor/ES3C28P_3D/ES3C28P_3D.step', 'vendor/speaker-30x20x7.png',
)]
files += sorted((ROOT / 'output/STL').glob('*.stl'))
files += sorted((ROOT / 'output/STEP').glob('*.step'))
files += [ROOT / 'output/review' / (view + '.png')
          for view in ('assembled', 'inside', 'rear', 'exploded')]

report = json.loads((ROOT / 'output/validation.json').read_text(encoding='utf-8'))
assert len(report['parts']) == 8
assert all(p['cad_valid'] and p['watertight'] and p['consistent_winding']
           for p in report['parts'])
checks = report['checks']
assert checks['vendor_front_collisions'] == []
assert checks['front_rear_intersection_mm3'] == 0
assert checks['rear_clamp_intersection_mm3'] == 0
assert all(v == 0 for v in checks['speaker_max_tolerance_intersections_mm3'].values())
assert checks['3mf']['zip_crc_ok'] and checks['3mf']['build_items'] == 7

readme = '''小黑抱抱 · 3D 打印首版 v1
日期：2026-10-21

1. 先读「打印与装配说明.md」。
2. 建议先打印 output/STL/08_board_fit_gauge.stl 核对屏幕、四孔及孔径。
3. 成品可导入 output/Xiaohei_A_print_plate.3mf，或分件导入 output/STL。
   3MF 为毫米制通用模型，不含打印机配置和 G-code。保持 100% 比例。
4. 建议主件使用 0.4 mm 喷嘴、0.20 mm 层高、4 圈墙、20% 填充。
5. output/STEP 保留可修改实体；output/review 有四张实际 CAD 渲染图。

适配 ES3C28P 电容触摸板、30×20×7 mm（±0.2 mm）喇叭。
已完成实体、网格及几何干涉检查，尚未实物打印试装。
源码重新生成：在本文件夹打开终端，依次运行
python -m pip install -r requirements.txt
python build_enclosure.py
python validate_fit.py
'''
manifest = {
    'release': PREFIX,
    'date': '2026-10-21',
    'unit': 'millimeter',
    'physical_fit_validated': False,
    'files': [
        {'path': file.relative_to(ROOT).as_posix(), 'bytes': file.stat().st_size,
         'sha256': sha256(file.read_bytes()).hexdigest()}
        for file in files
    ],
}
with zipfile.ZipFile(OUT, 'w', zipfile.ZIP_DEFLATED, compresslevel=7) as archive:
    for file in files:
        archive.write(file, PREFIX + '/' + file.relative_to(ROOT).as_posix())
    archive.writestr(PREFIX + '/先读我.txt', readme.encode('utf-8-sig'))
    archive.writestr(PREFIX + '/manifest.json', json.dumps(manifest, ensure_ascii=False, indent=2))

with zipfile.ZipFile(OUT) as archive:
    assert archive.testzip() is None
    for entry in manifest['files']:
        data = archive.read(PREFIX + '/' + entry['path'])
        assert sha256(data).hexdigest() == entry['sha256']
    print(json.dumps({'zip': str(OUT), 'bytes': OUT.stat().st_size,
                      'entries': len(archive.namelist()),
                      'stl_count': sum(n.endswith('.stl') for n in archive.namelist()),
                      'step_count': sum(n.endswith('.step') and '/output/STEP/' in n
                                        for n in archive.namelist()),
                      'crc_and_hashes': 'PASS'}, ensure_ascii=False, indent=2))

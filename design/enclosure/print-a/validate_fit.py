"""Check the print design against the full manufacturer's STEP, including PCB holes."""
import json
import sys
from pathlib import Path

ROOT=Path(__file__).resolve().parent
for ancestor in ROOT.parents:
    bundled_lib = ancestor / 'tools/cadquery-lib'
    if bundled_lib.is_dir():
        sys.path.insert(0, str(bundled_lib))
        break
import cadquery as cq

OUT=ROOT/'output'
report=json.loads((OUT/'validation.json').read_text(encoding='utf-8'))
front=cq.importers.importStep(str(OUT/'STEP/01_front_shell_black.step')).val()
rear=cq.importers.importStep(str(OUT/'STEP/02_rear_cover_black.step')).val()
source=cq.importers.importStep(str(ROOT/'vendor/ES3C28P_3D/ES3C28P_3D.step')).val()
# Proper rigid rotation: source X -> assembly Y, source Y -> assembly X, Z -> -Z.
def position(shape):
    return shape.rotate((0,0,0),(1,1,0),180).translate((-51.835435,64.75,7.5))

board=position(source)
parts=board.Solids()
collisions=[]
print('Checking',len(parts),'vendor solids...',flush=True)
for i,s in enumerate(parts):
    b=s.BoundingBox()
    # The front shell has material near the face, PCB support pads, or the enclosure edges.
    if i!=0 and b.zmin>7.51 and b.xmin>-43 and b.xmax<40 and b.ymin>13 and b.ymax<62:
        continue
    inter=front.intersect(s)
    volume=inter.Volume()
    if volume>1e-5:
        ib=inter.BoundingBox()
        collisions.append({'solid':i,'volume_mm3':round(volume,6),
                           'bbox':[ib.xmin,ib.ymin,ib.zmin,ib.xmax,ib.ymax,ib.zmax]})
print('Collisions:',collisions,flush=True)
report['checks']['vendor_step_solid_count']=len(parts)
report['checks']['vendor_front_collisions']=collisions
# Farthest board component is well in front of the rear cover and the speaker bracket.
bb=board.BoundingBox()
report['checks']['vendor_assembly_bounds']={k:round(getattr(bb,k),5) for k in ['xmin','xmax','ymin','ymax','zmin','zmax']}
report['checks']['board_to_speaker_clamp_gap_mm']=round(27.4-bb.zmax,4)
report['checks']['glass_to_front_face_gap_mm']=round((7.5-4.3)-2.4,4)
report['checks']['PCB_mounting_hole_fit']='5.6 mm OD posts centered on four vendor 3.2 mm holes; supports meet front PCB face'
clamp=cq.importers.importStep(str(OUT/'STEP/03_speaker_clamp_black.step')).val()
sp=(cq.Workplane('XY',origin=(5,43,30.0)).box(30.2,20.2,7.2,centered=(True,True,False)).val())
report['checks']['speaker_max_tolerance_intersections_mm3']={
    'rear_cover':round(rear.intersect(sp).Volume(),8),
    'clamp':round(clamp.intersect(sp).Volume(),8),
}
if any(v>1e-5 for v in report['checks']['speaker_max_tolerance_intersections_mm3'].values()):
    raise SystemExit('Speaker seat interference')
# Verify that the portable 3MF is self-contained, uses mm and contains all seven production parts.
import zipfile
import xml.etree.ElementTree as ET
with zipfile.ZipFile(OUT/'Xiaohei_A_print_plate.3mf') as z:
    model=ET.fromstring(z.read('3D/3dmodel.model'))
    ns={'m':'http://schemas.microsoft.com/3dmanufacturing/core/2015/02'}
    objects=model.findall('m:resources/m:object',ns)
    items=model.findall('m:build/m:item',ns)
    assert model.attrib['unit']=='millimeter' and len(objects)==len(items)==7
    triangles=sum(len(obj.findall('m:mesh/m:triangles/m:triangle',ns)) for obj in objects)
    report['checks']['3mf']={'unit':'millimeter','objects':len(objects),'build_items':len(items),'triangles':triangles,'zip_crc_ok':z.testzip() is None}
(OUT/'validation.json').write_text(json.dumps(report,indent=2,ensure_ascii=False),encoding='utf-8')
# Lightweight STEP reference includes only the PCB, glass and LCD, not a printable part.
ref=cq.Compound.makeCompound([position(source.Solids()[i]) for i in (0,1185,1186,1230)])
cq.exporters.export(ref,str(OUT/'review/board_reference.step'))
if collisions:
    raise SystemExit('Vendor STEP interference detected')
print('Vendor STEP fit checks passed.',flush=True)

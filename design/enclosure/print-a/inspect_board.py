"""Inspect the manufacturer's STEP in its original coordinate system."""
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT.parents[2] / "tools" / "cadquery-lib"))
import cadquery as cq

shape = cq.importers.importStep(str(ROOT / "vendor/ES3C28P_3D/ES3C28P_3D.step")).val()
rows = []
for index, solid in enumerate(shape.Solids()):
    b = solid.BoundingBox()
    rows.append({"index": index, "volume": round(solid.Volume(), 5),
                 "min": [round(b.xmin, 5), round(b.ymin, 5), round(b.zmin, 5)],
                 "max": [round(b.xmax, 5), round(b.ymax, 5), round(b.zmax, 5)],
                 "size": [round(b.xlen, 5), round(b.ylen, 5), round(b.zlen, 5)]})
(ROOT / "board-inspection.json").write_text(json.dumps(rows, indent=2), encoding="utf-8")
print(json.dumps(sorted(rows, key=lambda v: -v['volume'])[:22], indent=2))
print('PCB circular edges:')
for e in shape.Solids()[0].Edges():
    if e.geomType()=='CIRCLE':
        c=e.arcCenter()
        if c.z>-.1:
            print('radius',round(e.radius(),4),'center',round(c.x,4),round(c.y,4),round(c.z,4))

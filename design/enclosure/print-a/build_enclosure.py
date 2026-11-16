"""Parametric mechanical design for the ES3C28P / 30x20x7 speaker.

Assembly coordinates: X right, Y up, Z from the front face toward the rear.
All dimensions in mm. STL files are separately oriented with their bed face at Z=0.
"""
from __future__ import annotations
import json
import math
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
for ancestor in ROOT.parents:
    bundled_lib = ancestor / 'tools/cadquery-lib'
    if bundled_lib.is_dir():
        sys.path.insert(0, str(bundled_lib))
        break
import cadquery as cq
import numpy as np
import trimesh

OUT = ROOT / 'output'
STL = OUT / 'STL'
STEP = OUT / 'STEP'
for folder in (OUT, STL, STEP, OUT / 'review'):
    folder.mkdir(parents=True, exist_ok=True)

# Values refined against the manufacturer's STEP and dimensioned drawing.
P = dict(
    width=104.0, body_height=74.0, body_center_y=43.0, radius=14.0,
    depth=40.0, rear_plate=2.4, wall=2.4,
    screen_x=0.0, screen_y=37.0, screen_w=59.4, screen_h=44.8,
    pcb_x=-2.835435, pcb_y=37.0, pcb_front_z=7.5, pcb_thickness=1.6,
    pcb_pitch_x=78.0, pcb_pitch_y=42.0, pilot_d=2.1,
    mic_x=36.164865, mic_y=51.9997, mic_d=2.6,
    usb_y=37.0, usb_z=10.17444, usb_height=17.0, usb_depth=13.0,
    speaker_w=30.0, speaker_h=20.0, speaker_t=7.0,
    speaker_x=5.0, speaker_y=43.0, speaker_clearance=.5,
    rear_fit_gap=.30, decorative_gap=.20,
)
P['rear_split_z'] = P['depth'] - P['rear_plate']
PCB_HOLES = [(P['pcb_x'] + dx, P['pcb_y'] + dy) for dx in (-39,39) for dy in (-21,21)]
REAR_SCREWS = [(-32,8.8),(32,8.8),(-34,73.8),(34,73.8)]
CLAMP_SCREWS = [(P['speaker_x'],P['speaker_y']-16.5),(P['speaker_x'],P['speaker_y']+16.5)]


def rr(w,h,r,z,hz,x=0,y=0):
    """Rounded XY rectangle, specified by the actual outside dimensions."""
    return (cq.Workplane('XY', origin=(x,y,z)).rect(w,h).extrude(hz)
            .edges('|Z').fillet(min(r,min(w,h)/2-.001)))


def block(w,h,d,x,y,z):
    return cq.Workplane('XY',origin=(x,y,z)).box(w,h,d,centered=(True,True,False))


def cylinder(d,z,h,x,y):
    return cq.Workplane('XY',origin=(x,y,z)).circle(d/2).extrude(h)


def polygon(points,z,h,r=0):
    obj=cq.Workplane('XY',origin=(0,0,z)).polyline(points).close().extrude(h)
    return obj.edges('|Z').fillet(r) if r else obj


def cutters(items):
    return cq.Compound.makeCompound([i.val() if isinstance(i,cq.Workplane) else i for i in items])


def eye_shape(z,thickness,x=0,y=0,clearance=0):
    return rr(10.4+2*clearance,5.8+2*clearance,2.7+clearance,z,thickness,x,y)


def ear_points(side):
    return [(side*x,y) for x,y in [(18,76),(38.5,100),(46,76)]]


def inner_ear_points(side):
    return [(side*x,y) for x,y in [(28.7,80.7),(38.1,93.1),(40.8,80.7)]]


def inner_ear(side,z,depth,clearance=0):
    pts=inner_ear_points(side)
    wire=cq.Workplane('XY',origin=(0,0,z)).polyline(pts).close()
    if clearance:
        wire=wire.offset2D(clearance)
    result=wire.extrude(depth)
    return result if clearance else result.edges('|Z').fillet(.5)


def make_front():
    p=P; split=p['rear_split_z']
    shell=rr(p['width'],p['body_height'],p['radius'],0,split,0,p['body_center_y']).faces('<Z').edges().fillet(.8)
    # A real hollow, rear-open shell. Face-down printing leaves the pocket open upwards.
    void=rr(p['width']-2*p['wall'],p['body_height']-2*p['wall'],p['radius']-p['wall'],p['wall'],split+1,0,p['body_center_y'])
    shell=shell.cut(void)
    for side in (-1,1):
        shell=shell.union(polygon(ear_points(side),0,12,1.7))
        # Paws are structural parts of the main shell; both have flat front bed faces.
        shell=shell.union(rr(23,10,4,0,48,side*32,5))
        # Rear heel stays below the cover; the two parts meet without sharing volume.
        shell=shell.cut(block(25,12,12,side*32,12,split))
    window=rr(p['screen_w'],p['screen_h'],.8,-1,p['wall']+2,p['screen_x'],p['screen_y'])
    shell=shell.cut(window)
    for x,y in PCB_HOLES:
        shell=shell.union(cylinder(5.6,p['wall']-.2,p['pcb_front_z']-p['wall']+.2,x,y))
    # Through-board screws enter blind polymer pilots; no screw crosses the glass.
    shell=shell.cut(cutters([cylinder(p['pilot_d'],2.8,p['pcb_front_z']-2.8+.2,x,y) for x,y in PCB_HOLES]))
    for x,y in REAR_SCREWS:
        shell=shell.union(cylinder(5.6 if y<20 else 6.4,p['wall']-.2,split-p['wall']+.2,x,y))
        if y>60:
            # Join the tall rear screw column to the upper wall with a short radial rib.
            shell=shell.union(block(2.0,3.4,32.3,x,77.8,2.2))
    shell=shell.cut(cutters([cylinder(p['pilot_d'],29,split-29+.2,x,y) for x,y in REAR_SCREWS]))
    # The side opening includes the cable overmould, beyond the recessed board socket.
    usb=block(14,p['usb_height'],p['usb_depth'],-48,p['usb_y'],p['usb_z']-p['usb_depth']/2)
    shell=shell.cut(usb)
    # Direct acoustic port aligned with the front-side MEMS microphone.
    shell=shell.union(cylinder(5.2,p['wall']-.2,6.8-p['wall']+.2,p['mic_x'],p['mic_y']))
    shell=shell.cut(cylinder(p['mic_d'],-.1,7.1,p['mic_x'],p['mic_y']))
    for side in (-1,1):
        shell=shell.cut(inner_ear(side,-.1,1.0,p['decorative_gap']))
        shell=shell.cut(eye_shape(-.1,1.0,side*20,69,p['decorative_gap']))
        for offset in (-3.3,3.3):
            shell=shell.cut(rr(.65,3.3,.30,-.1,.7,side*32+offset,4.7))
    return shell.clean()


def grille_tools():
    p=P
    return [cylinder(2.0,37.3,3,x*3.0+p['speaker_x'],y*3.0+p['speaker_y'])
            for x in range(-4,5) for y in range(-2,3)
            if (x/4.5)**2+(y/2.6)**2<=1]


def make_rear():
    p=P; split=p['rear_split_z']; cy=p['body_center_y']
    cover=rr(p['width'],p['body_height'],p['radius'],split,p['rear_plate'],0,cy)
    # A short locating rim with 0.30 mm clearance per side; it does not clamp the PCB.
    outer=rr(98.6,68.6,11.3,split-1.6,1.7,0,cy)
    inner=rr(96.2,66.2,10.1,split-1.7,2,0,cy)
    lip=outer.cut(inner)
    lip=lip.cut(cutters([cylinder(7.3,split-1.8,2.1,x,y) for x,y in REAR_SCREWS]))
    lip=lip.cut(cutters([block(25,6,2.2,x,9,split-1.8) for x in (-32,32)]))
    cover=cover.union(lip)
    hole_tools=[cylinder(2.8,split-.2,p['rear_plate']+.4,x,y) for x,y in REAR_SCREWS]
    for x,y in REAR_SCREWS:
        # 90 degree countersink for an M2.5 flat-head screw.
        cone=cq.Solid.makeCone(1.4,2.65,1.25,cq.Vector(x,y,38.75),cq.Vector(0,0,1))
        hole_tools.append(cone)
    cover=cover.cut(cutters(hole_tools))
    cover=cover.cut(cutters(grille_tools()))
    sx,sy=p['speaker_x'],p['speaker_y']
    frame=rr(35,25,4.6,29.4,split-29.4+.1,sx,sy)
    frame=frame.cut(rr(31,21,1.0,29.2,9,sx,sy))
    # Relief for the twisted pair on the short end of the speaker.
    frame=frame.cut(block(7,6,4.8,sx+17,sy,29.2))
    cover=cover.union(frame)
    for x,y in CLAMP_SCREWS:
        cover=cover.union(cylinder(6.6,29.4,split-29.4+.1,x,y))
    cover=cover.cut(cutters([cylinder(p['pilot_d'],29.2,6.4,x,y) for x,y in CLAMP_SCREWS]))
    return cover.clean()


def make_clamp():
    sx,sy=P['speaker_x'],P['speaker_y']
    obj=rr(8,40,2.6,27.4,2,sx,sy)
    obj=obj.cut(cutters([cylinder(2.8,27.2,2.4,x,y) for x,y in CLAMP_SCREWS]))
    return obj.clean()


def make_eye(x):
    obj=eye_shape(.2,.7,x,69)
    return obj.cut(rr(1.6,4.4,.75,0,1.1,x+.65,69)).clean()


def make_gauge():
    p=P
    # A thin independent fit frame, preserving the front-face/standoff relationship.
    obj=rr(92,56,5,0,2.4,p['pcb_x'],p['pcb_y'])
    obj=obj.cut(rr(p['screen_w'],p['screen_h'],.8,-.1,3,0,p['screen_y']))
    for x,y in PCB_HOLES:
        obj=obj.union(cylinder(5.6,2.2,5.3,x,y))
    obj=obj.cut(cutters([cylinder(p['pilot_d'],2.8,4.9,x,y) for x,y in PCB_HOLES]))
    obj=obj.cut(cylinder(p['mic_d'],-.1,3,p['mic_x'],p['mic_y']))
    return obj.clean()


def to_bed(obj,reverse=False):
    if reverse:
        obj=obj.rotate((0,0,0),(1,0,0),180)
    b=obj.val().BoundingBox()
    return obj.translate((-(b.xmin+b.xmax)/2,-(b.ymin+b.ymax)/2,-b.zmin))


def export_3mf(parts,path):
    """Portable 3MF core model, explicit millimetres, arranged non-overlapping on a bed."""
    import zipfile
    import xml.etree.ElementTree as ET
    ns='http://schemas.microsoft.com/3dmanufacturing/core/2015/02'
    ET.register_namespace('',ns)
    q=lambda name:f'{{{ns}}}{name}'
    root=ET.Element(q('model'),{'unit':'millimeter','xml:lang':'en-US'})
    ET.SubElement(root,q('metadata'),{'name':'Title'}).text='Xiaohei hug - printable parts v1'
    resources=ET.SubElement(root,q('resources'))
    build=ET.SubElement(root,q('build'))
    cursor_x=5;cursor_y=5;row_h=0;max_x=0
    mats=ET.SubElement(resources,q('basematerials'),{'id':'100'})
    for label,color in [('Matte black','#252B29FF'),('Leaf green','#A8BF91FF'),('Ivory','#E5EAC4FF')]:
        ET.SubElement(mats,q('base'),{'name':label,'displaycolor':color})
    for index,(name,shape) in enumerate(parts,1):
        mesh=trimesh.load_mesh(STL / (name+'.stl'))
        size=mesh.extents
        if cursor_x+size[0]>225:
            cursor_x=5;cursor_y+=row_h+5;row_h=0
        color_index=1 if 'green' in name else (2 if 'ivory' in name else 0)
        obj=ET.SubElement(resources,q('object'),{'id':str(index),'type':'model','name':name,'pid':'100','pindex':str(color_index)})
        m=ET.SubElement(obj,q('mesh'));vs=ET.SubElement(m,q('vertices'));ts=ET.SubElement(m,q('triangles'))
        for v in mesh.vertices:
            ET.SubElement(vs,q('vertex'),dict(zip(('x','y','z'),[f'{c:.6f}' for c in v])))
        for f in mesh.faces:
            ET.SubElement(ts,q('triangle'),dict(zip(('v1','v2','v3'),[str(int(c)) for c in f])))
        tx=cursor_x-mesh.bounds[0,0];ty=cursor_y-mesh.bounds[0,1]
        ET.SubElement(build,q('item'),{'objectid':str(index),'transform':f'1 0 0 0 1 0 0 0 1 {tx:.6f} {ty:.6f} 0'})
        max_x=max(max_x,cursor_x+size[0]);cursor_x+=size[0]+5;row_h=max(row_h,size[1])
    content='''<?xml version="1.0" encoding="UTF-8"?><Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types"><Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/><Default Extension="model" ContentType="application/vnd.ms-package.3dmanufacturing-3dmodel+xml"/></Types>'''
    rel='''<?xml version="1.0" encoding="UTF-8"?><Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Target="/3D/3dmodel.model" Id="rel0" Type="http://schemas.microsoft.com/3dmanufacturing/2013/01/3dmodel"/></Relationships>'''
    with zipfile.ZipFile(path,'w',zipfile.ZIP_DEFLATED) as z:
        z.writestr('[Content_Types].xml',content);z.writestr('_rels/.rels',rel)
        z.writestr('3D/3dmodel.model',ET.tostring(root,encoding='utf-8',xml_declaration=True))
    return {'occupied_x':round(max_x,2),'occupied_y':round(cursor_y+row_h,2),'suggested_bed_mm':[225,155],'unit':'millimeter'}


def main():
    print('Building front shell...',flush=True);front=make_front()
    print('Building rear / speaker seat...',flush=True);rear=make_rear();clamp=make_clamp()
    parts=[
        ('01_front_shell_black',front,'#252b29',False),
        ('02_rear_cover_black',rear,'#30382f',True),
        ('03_speaker_clamp_black',clamp,'#495744',False),
        ('04_ear_left_green',inner_ear(-1,.1,.8),'#a8bf91',False),
        ('05_ear_right_green',inner_ear(1,.1,.8),'#a8bf91',False),
        ('06_eye_left_ivory',make_eye(-20),'#e5eac4',False),
        ('07_eye_right_ivory',make_eye(20),'#e5eac4',False),
    ]
    gauge=make_gauge()
    report={'parameters':P,'pcb_holes':PCB_HOLES,'rear_screws':REAR_SCREWS,'parts':[], 'checks':{}}
    preview=[];beds=[];assembly=cq.Assembly(name='Xiaohei_hug_v1')
    for name,obj,color,reverse in parts+[('08_board_fit_gauge',gauge,'#a8ad9b',False)]:
        if not obj.val().isValid() or len(obj.val().Solids())!=1:
            raise ValueError(f'{name}: invalid or disconnected solid')
        bed=to_bed(obj,reverse)
        cq.exporters.export(bed,str(STL/(name+'.stl')),tolerance=.025,angularTolerance=.09)
        cq.exporters.export(obj,str(STEP/(name+'.step')))
        mesh=trimesh.load_mesh(STL/(name+'.stl'))
        if not mesh.is_watertight or not mesh.is_winding_consistent or mesh.volume<=0:
            raise ValueError(f'{name}: invalid STL mesh')
        row={'name':name,'solid_count':1,'cad_valid':True,'watertight':bool(mesh.is_watertight),
             'consistent_winding':bool(mesh.is_winding_consistent),'triangles':len(mesh.faces),
             'print_size_mm':np.round(mesh.extents,3).tolist(),'volume_cm3':round(mesh.volume/1000,3),
             'minimum_z':float(mesh.bounds[0,2]),'estimated_solid_PLA_g':round(mesh.volume*.00124,2)}
        report['parts'].append(row)
        print(name,row['print_size_mm'],row['volume_cm3'],'cm3',flush=True)
        if 'gauge' not in name:
            assembly.add(obj,name=name,color=cq.Color(*[int(color[i:i+2],16)/255 for i in (1,3,5)]))
            beds.append((name,bed))
            vertices,faces=obj.val().tessellate(.08,.12)
            preview.append({'name':name,'color':color,'vertices':[[v.x,v.y,v.z] for v in vertices],
                            'faces':[list(f) for f in faces]})
    # Exact CAD intersections must be zero for the interlocking and screw-held components.
    report['checks']['front_rear_intersection_mm3']=round(front.val().intersect(rear.val()).Volume(),8)
    report['checks']['rear_clamp_intersection_mm3']=round(rear.val().intersect(clamp.val()).Volume(),8)
    print('Intersections:',report['checks'],flush=True)
    if report['checks']['front_rear_intersection_mm3']>1e-5 or report['checks']['rear_clamp_intersection_mm3']>1e-5:
        raise ValueError('Assembly interference')
    assembly.save(str(STEP/'00_assembled_enclosure.step'))
    report['print_plate']=export_3mf(beds,OUT/'Xiaohei_A_print_plate.3mf')
    (OUT/'review/assembly-meshes.json').write_text(json.dumps(preview,separators=(',',':')),encoding='utf-8')
    (OUT/'validation.json').write_text(json.dumps(report,indent=2,ensure_ascii=False),encoding='utf-8')
    (ROOT/'parameters.json').write_text(json.dumps(P,indent=2,ensure_ascii=False),encoding='utf-8')
    print('CAD and mesh checks passed.',flush=True)


if __name__=='__main__':
    main()

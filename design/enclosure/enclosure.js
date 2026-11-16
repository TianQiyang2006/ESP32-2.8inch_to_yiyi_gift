import * as THREE from './vendor/three.module.js';

const params = new URLSearchParams(location.search);
const studio = params.has('studio');
if (studio) document.body.classList.add('studio');
const viewport = document.getElementById('viewport');
const renderer = new THREE.WebGLRenderer({antialias:true, alpha:false, preserveDrawingBuffer:true});
renderer.setPixelRatio(Math.min(devicePixelRatio, 2));
renderer.shadowMap.enabled = true;
renderer.shadowMap.type = THREE.PCFSoftShadowMap;
renderer.toneMapping = THREE.ACESFilmicToneMapping;
renderer.toneMappingExposure = 1.25;
renderer.outputColorSpace = THREE.SRGBColorSpace;
viewport.appendChild(renderer.domElement);
const scene = new THREE.Scene();
scene.background = new THREE.Color('#f5f2eb');
const camera = new THREE.OrthographicCamera(-95,95,95,-95,1,1000);
const target = new THREE.Vector3(0,46,0);
scene.add(new THREE.HemisphereLight(0xfaf8f0,0x8c9586,2.7));
function light(color,intensity,x,y,z,shadow=false){
  const l=new THREE.DirectionalLight(color,intensity);l.position.set(x,y,z);l.castShadow=shadow;
  if(shadow){l.shadow.mapSize.set(4096,4096);Object.assign(l.shadow.camera,{left:-130,right:130,top:160,bottom:-130,near:1,far:600});l.shadow.normalBias=.04;l.shadow.bias=-.00012;l.shadow.radius=4;}
  scene.add(l);return l;
}
light(0xfff3dc,4.1,-130,220,180,true);
light(0xdce7ec,2.5,160,140,-130);
light(0xffffff,1.0,30,80,190);
const floor=new THREE.Mesh(new THREE.PlaneGeometry(2000,2000),new THREE.ShadowMaterial({color:'#555f4b',opacity:.16}));
floor.rotation.x=-Math.PI/2;floor.position.y=-.22;floor.receiveShadow=true;scene.add(floor);
// Soft contact shadow supplements the directional light without concealing the paws.
const shadowCanvas=document.createElement('canvas');shadowCanvas.width=256;shadowCanvas.height=256;
const sh=shadowCanvas.getContext('2d');const grad=sh.createRadialGradient(128,128,0,128,128,128);
grad.addColorStop(0,'rgba(39,47,33,0.28)');grad.addColorStop(.48,'rgba(39,47,33,0.11)');grad.addColorStop(1,'rgba(39,47,33,0)');sh.fillStyle=grad;sh.fillRect(0,0,256,256);
const contact=new THREE.Mesh(new THREE.PlaneGeometry(165,105),new THREE.MeshBasicMaterial({map:new THREE.CanvasTexture(shadowCanvas),transparent:true,depthWrite:false}));contact.rotation.x=-Math.PI/2;contact.position.set(0,-.15,0);scene.add(contact);

const mat=(color,roughness=.7,metalness=0)=>new THREE.MeshStandardMaterial({color,roughness,metalness});
const materials={black:mat('#24282a',.77),front:mat('#1b2022',.68),edge:mat('#111616',.42),green:mat('#9bb58e',.5),eye:mat('#e1e9b7',.28),pupil:mat('#172021',.3),paw:mat('#2a3030',.76),back:mat('#2b3030',.78),cream:mat('#e9e5d6',.82),forest:mat('#466b52',.72),leaf:mat('#a4bb89',.6),port:mat('#9baba6',.28,.68),void:mat('#060c0b',.85),pcb:mat('#26614d',.7)};
function roundedShape(w,h,r){
  const s=new THREE.Shape();const x=-w/2,y=-h/2;r=Math.min(r,w/2,h/2);
  s.moveTo(x+r,y);s.lineTo(x+w-r,y);s.quadraticCurveTo(x+w,y,x+w,y+r);s.lineTo(x+w,y+h-r);s.quadraticCurveTo(x+w,y+h,x+w-r,y+h);s.lineTo(x+r,y+h);s.quadraticCurveTo(x,y+h,x,y+h-r);s.lineTo(x,y+r);s.quadraticCurveTo(x,y,x+r,y);return s;
}
function addMesh(g,geometry,material,x=0,y=0,z=0){const m=new THREE.Mesh(geometry,material);m.position.set(x,y,z);m.castShadow=true;m.receiveShadow=true;g.add(m);return m;}
function box(g,w,h,d,r,m,x,y,z,bevel=.5){
  const geo=new THREE.ExtrudeGeometry(roundedShape(w-2*bevel,h-2*bevel,Math.max(.1,r-bevel)),{depth:d-2*bevel,bevelEnabled:bevel>0,bevelSegments:6,steps:1,bevelSize:bevel,bevelThickness:bevel,curveSegments:36});
  geo.translate(0,0,-(d-2*bevel)/2);return addMesh(g,geo,m,x,y,z);
}
function flat(g,w,h,r,m,x,y,z){
  const geometry=new THREE.ShapeGeometry(roundedShape(w,h,r),28);
  const positions=geometry.getAttribute('position');const uv=geometry.getAttribute('uv');
  for(let i=0;i<positions.count;i++)uv.setXY(i,positions.getX(i)/w+.5,positions.getY(i)/h+.5);
  uv.needsUpdate=true;return addMesh(g,geometry,m,x,y,z);
}
function disc(g,r,m,x,y,z){return addMesh(g,new THREE.CircleGeometry(r,32),m,x,y,z);}
function ear(g,side,material=materials.black){
  const s=new THREE.Shape();s.moveTo(18,76);s.quadraticCurveTo(26,83,37,98.6);s.quadraticCurveTo(39.5,102,40.4,97.3);s.lineTo(46,75);s.quadraticCurveTo(35,73,18,76);
  const geo=new THREE.ExtrudeGeometry(s,{depth:12,bevelEnabled:true,bevelSegments:5,bevelSize:1.3,bevelThickness:1.3,curveSegments:22});geo.translate(0,0,5);
  const m=addMesh(g,geo,material);m.scale.x=side;
  const i=new THREE.Shape();i.moveTo(28.5,80);i.quadraticCurveTo(32,84,37.8,92.4);i.quadraticCurveTo(38.3,93,38.5,91.8);i.lineTo(40.3,80);i.closePath();const inner=addMesh(g,new THREE.ShapeGeometry(i,20),materials.green,0,0,18.5);inner.scale.x=side;
}
function eyes(g,y,z,pixel=false){for(const side of [-1,1]){flat(g,pixel?9.6:10.2,pixel?5.2:5.8,pixel?.1:2.8,materials.eye,side*20,y,z);flat(g,1.6,4.6,.7,materials.pupil,side*20+.7,y,z+.03);}}
function screw(g,x,y,z){disc(g,1.45,materials.edge,x,y,z);const slot=flat(g,1.3,.33,.1,materials.port,x,y,z+.03);slot.rotation.z=Math.PI/4;}
function backPanel(g,w,h,y,z,m=materials.back){
  const rear=new THREE.Group();rear.rotation.y=Math.PI;rear.position.set(0,y,z);g.add(rear);box(rear,w,h,1.3,8,m,0,0,0,.3);
  for(const x of [-w/2+6,w/2-6])for(const sy of [-h/2+6,h/2-6])screw(rear,x,sy,.8);
  // Proposed perforation field; speaker size and baffling remain for the mechanical phase.
  for(let row=-4;row<=4;row++)for(let col=-5;col<=5;col++){if((col/5.4)**2+(row/4.7)**2<1)disc(rear,.8,materials.void,col*3.1,row*3.1+2,.76);}
  const badge=labelTexture('FOR YIYI',160,40,'#a0ac9a',18);flat(rear,22,5,.1,new THREE.MeshBasicMaterial({map:badge,transparent:true}),0,-h/2+10,.8);
  return rear;
}
function labelTexture(text,w,h,color,fontSize){const c=document.createElement('canvas');c.width=w;c.height=h;const t=c.getContext('2d');t.fillStyle=color;t.font=`500 ${fontSize}px "Microsoft YaHei",sans-serif`;t.textAlign='center';t.textBaseline='middle';t.fillText(text,w/2,h/2);const texture=new THREE.CanvasTexture(c);texture.colorSpace=THREE.SRGBColorSpace;return texture;}
function usb(g,x,y,z){
  const p=new THREE.Group();p.position.set(x,y,z);p.rotation.y=-Math.PI/2;g.add(p);
  flat(p,11.2,5.8,2,materials.void,0,0,0);flat(p,9.3,3.6,1.65,materials.port,0,0,.03);flat(p,8.1,2.6,1.2,materials.void,0,0,.05);flat(p,5.7,.65,.25,materials.port,0,0,.08);
}
function mic(g,x,y,z){disc(g,1.45,materials.edge,x,y,z);disc(g,.85,materials.void,x,y,z+.02);}
const loader=new THREE.TextureLoader();
const screenTextures={home:await loader.loadAsync('./assets/screen-home.png'),clock:await loader.loadAsync('./assets/screen-clock.png')};
for(const t of Object.values(screenTextures)){t.colorSpace=THREE.SRGBColorSpace;t.minFilter=THREE.LinearFilter;t.magFilter=THREE.NearestFilter;t.anisotropy=renderer.capabilities.getMaxAnisotropy();}
const screenMaterial=new THREE.MeshBasicMaterial({map:screenTextures.home,toneMapped:false});
function screen(g,y,z,rim=materials.edge){
  box(g,65.8,51.4,1.8,3.4,rim,0,y,z,.35);
  flat(g,61,46.8,1.5,materials.void,0,y,z+1.0);
  flat(g,57.6,43.2,.25,screenMaterial,0,y,z+1.05);
}
function toeMarks(g,x,y,z){for(const dx of [-3.3,3.3])flat(g,.5,3,.22,materials.edge,x+dx,y,z);}
const concepts={};
// A: rounded black cat. All units are millimetres; exterior only, not a printable solid.
{
  const g=new THREE.Group();g.name='小黑抱抱';
  box(g,104,74,39,15.5,materials.black,0,43,0,1.5);
  box(g,100.5,71.2,2.5,14.2,materials.front,0,43,19.5,.8);
  ear(g,-1);ear(g,1);eyes(g,69,21.1);
  screen(g,35.9,21.3);mic(g,42,24.4,20.8);
  for(const x of [-32,32]){box(g,23,8,27,3.7,materials.paw,x,4,10,.8);toeMarks(g,x,4.6,23.6);}
  backPanel(g,88,59,42,-20.3);usb(g,-52.05,39.1,9);
  for(const x of [-25,25])box(g,10,4,1.4,1.7,materials.void,x,14.5,-20.15,.15);
  concepts.a=g;
}
// B: brick language and an 8 mm grid. Exact brick connections are intentionally unspecified.
{
  const g=new THREE.Group();g.name='像素小黑';
  for(let i=0;i<8;i++)box(g,112,9.45,46.8,.65,i%2?materials.front:materials.black,0,6.4+i*9.6+4.8,0,.18);
  screen(g,39.4,24.1);eyes(g,73.6,23.65,true);mic(g,43,27.9,23.65);
  for(const side of [-1,1]){
    box(g,24,9.6,16,.45,materials.black,side*36,88,12,.15);
    box(g,16,9.6,16,.45,materials.black,side*40,97.6,12,.15);
    flat(g,6.2,6.2,.2,materials.green,side*40,88,20.25);
    for(const dx of [-4,4]){const s=addMesh(g,new THREE.CylinderGeometry(2.35,2.35,1.6,32),materials.black,side*40+dx,103.2,12);s.receiveShadow=true;}
    box(g,24,6.4,24,.5,materials.black,side*36,3.2,11,.18);
    flat(g,.45,6,.1,materials.edge,side*36,3.2,23.2);
  }
  // Subtle tile and brick joints give a block-built appearance without disguising the display size.
  for(let row=0;row<8;row++){for(const x of row%2?[-40,-8,24]:[-24,8,40]){
    const y=11.2+row*9.6;if(y>13&&y<66&&Math.abs(x)<34)continue;
    flat(g,.22,8.6,.02,materials.edge,x,y,23.55);
  }}
  for(const x of [-16,-8,0,8,16])addMesh(g,new THREE.CylinderGeometry(2.35,2.35,1.6,24),materials.black,x,84,0);
  backPanel(g,96,58,43,-23.9);usb(g,-56.05,42.6,10);
  concepts.b=g;
}
// C: a quiet picture-frame expression, with a forest-green stand and a small cat signature.
{
  const g=new THREE.Group();g.name='森林信笺';
  box(g,110,80,29,12.8,materials.cream,0,48,0,1.3);
  box(g,106,76,2,11,materials.cream,0,48,14.9,.5);
  screen(g,49.4,16.0,materials.forest);
  box(g,72,6,52,2.8,materials.forest,0,3,0,.65);
  box(g,42,10,20,4,materials.forest,0,9,-1,.8);
  const caption=labelTexture('依依的小小森林',512,80,'#59714e',34);flat(g,44,6.8,.1,new THREE.MeshBasicMaterial({map:caption,transparent:true}),-4,17,16.1);
  // Small cat silhouette on the lower-right frame: a personal mark, not a second screen.
  disc(g,4.3,materials.front,39,18.8,16.2);
  for(const points of [[[35.7,20],[35,25.5],[39,22]],[[38.6,22],[43,25.5],[42.5,19.8]]]){const s=new THREE.Shape();s.moveTo(...points[0]);s.lineTo(...points[1]);s.lineTo(...points[2]);s.closePath();addMesh(g,new THREE.ShapeGeometry(s),materials.front,0,0,16.2);}
  for(const x of [37.5,40.6])disc(g,.6,materials.eye,x,19.3,16.24);
  // Restrained leaf on the top-left corner.
  const leaf=new THREE.Shape();leaf.moveTo(-43,76);leaf.quadraticCurveTo(-47,84,-36,84);leaf.quadraticCurveTo(-34,77,-43,76);addMesh(g,new THREE.ShapeGeometry(leaf,24),materials.leaf,0,0,16.1);
  mic(g,45,37.9,16.1);backPanel(g,94,63,49,-15.3,materials.forest);usb(g,-55.05,52.6,5);
  concepts.c=g;
}
const metadata={
  a:{tag:'像一只安静陪你的黑猫',description:'圆润的墨黑机身，猫耳藏一点叶绿。屏幕是小黑抱着的窗口，两只小爪稳稳落在桌上。',dimensions:'约 104 × 48 × 100 mm'},
  b:{tag:'把小黑拼成一份亲手做的礼物',description:'阶梯猫耳、方块脚和细细的拼接线，延续原来的积木想法。大部分保持黑色，只用少量浅绿提亮。',dimensions:'约 112 × 48 × 104 mm'},
  c:{tag:'让屏幕成为桌上的一封信',description:'温暖的奶油色相框搭配森林绿底座，小黑悄悄蹲在右下角。日常看时间、天气，也像看一张小相片。',dimensions:'约 110 × 55 × 88 mm'}
};
let current='a',view='hero',azimuth=-.46,elevation=.23,zoom=1,drag=null,auto=false,needsRender=true;
const viewAngles={hero:[-.46,.23],front:[0,.035],back:[Math.PI-.46,.23],side:[-Math.PI/2,.12]};
function setConcept(id){
  if(!concepts[id])return;
  for(const g of Object.values(concepts))scene.remove(g);current=id;scene.add(concepts[id]);
  document.querySelectorAll('button[data-concept]').forEach(b=>{const active=b.dataset.concept===id;b.classList.toggle('active',active);b.setAttribute('aria-pressed',String(active));});
  document.getElementById('concept-tag').textContent=metadata[id].tag;
  document.getElementById('concept-description').textContent=metadata[id].description;
  document.getElementById('dimensions').textContent=metadata[id].dimensions;
  document.body.dataset.concept=id;
  needsRender=true;
}
function setView(id){if(!viewAngles[id])return;view=id;[azimuth,elevation]=viewAngles[id];document.querySelectorAll('[data-view]').forEach(b=>b.classList.toggle('active',b.dataset.view===id));needsRender=true;}
document.querySelectorAll('button[data-concept]').forEach(b=>b.addEventListener('click',()=>setConcept(b.dataset.concept)));
document.querySelectorAll('[data-view]').forEach(b=>b.addEventListener('click',()=>{auto=false;document.getElementById('rotate').setAttribute('aria-pressed','false');setView(b.dataset.view);}));
document.getElementById('screen').addEventListener('change',e=>{screenMaterial.map=screenTextures[e.target.value];screenMaterial.needsUpdate=true;needsRender=true;});
document.getElementById('rotate').addEventListener('click',e=>{auto=!auto;e.currentTarget.setAttribute('aria-pressed',String(auto));needsRender=true;});
renderer.domElement.addEventListener('pointerdown',e=>{drag={x:e.clientX,y:e.clientY,azimuth,elevation};renderer.domElement.setPointerCapture(e.pointerId);auto=false;document.getElementById('rotate').setAttribute('aria-pressed','false');});
renderer.domElement.addEventListener('pointermove',e=>{if(!drag)return;azimuth=drag.azimuth-(e.clientX-drag.x)*.009;elevation=THREE.MathUtils.clamp(drag.elevation+(e.clientY-drag.y)*.006,-.04,1.1);document.querySelectorAll('[data-view]').forEach(b=>b.classList.remove('active'));needsRender=true;});
renderer.domElement.addEventListener('pointerup',()=>drag=null);renderer.domElement.addEventListener('pointercancel',()=>drag=null);
renderer.domElement.addEventListener('wheel',e=>{e.preventDefault();zoom=THREE.MathUtils.clamp(zoom*Math.exp(-e.deltaY*.001),.7,1.65);needsRender=true;},{passive:false});
function resize(){const w=viewport.clientWidth,h=viewport.clientHeight;renderer.setSize(w,h);const aspect=w/h;const span=studio?78:82;camera.left=-span*aspect;camera.right=span*aspect;camera.top=span;camera.bottom=-span;camera.updateProjectionMatrix();needsRender=true;}
new ResizeObserver(resize).observe(viewport);
setConcept(params.get('concept')||'a');setView(params.get('view')||'hero');
if(params.get('screen')==='clock'){screenMaterial.map=screenTextures.clock;document.getElementById('screen').value='clock';}
document.getElementById('loading').hidden=true;
let last=0;
function render(ms){const dt=Math.min((ms-last)/1000,.05);last=ms;if(auto)azimuth+=dt*.20;
  if(needsRender||auto){camera.position.set(270*Math.sin(azimuth)*Math.cos(elevation),46+270*Math.sin(elevation),270*Math.cos(azimuth)*Math.cos(elevation));camera.lookAt(target);camera.zoom=zoom;camera.updateProjectionMatrix();renderer.render(scene,camera);needsRender=false;window.enclosureReady=true;}
  requestAnimationFrame(render);
}
requestAnimationFrame(render);

// DOM event integration check. This does not replace visual browser QA.
import test from 'node:test';
import assert from 'node:assert/strict';
import { createGame, debugSecret } from './games.js';

class Classes {
  values=new Set();
  add(...names){names.forEach(n=>this.values.add(n));}
  remove(...names){names.forEach(n=>this.values.delete(n));}
  toggle(n,force){const yes=force??!this.values.has(n);yes?this.values.add(n):this.values.delete(n);return yes;}
}
class Element extends EventTarget {
  constructor(id=''){super();this.id=id;this.children=[];this.dataset={};this.style={cssText:'',setProperty(){}};this.classList=new Classes();this.attributes={};this.disabled=false;this.clientWidth=800;this.hidden=false;this.textContent='';}
  setAttribute(k,v){this.attributes[k]=String(v);}
  getAttribute(k){return this.attributes[k];}
  append(c){this.children.push(c);}
  replaceChildren(...c){this.children=c;}
  closest(){return elements.get('review-panel');}
  getBoundingClientRect(){return {left:0,top:0,width:320,height:240};}
  setPointerCapture(){}
  getContext(){return new Proxy({}, {get:(_,k)=> k==='measureText'?s=>({width:s.length*12}):()=>{},set:()=>true});}
}
const elements=new Map();
for(const id of ['game-canvas','game-nav','screen-controls','native-screen','viewport','detail-number','detail-name','detail-tag','detail-description','detail-input','detail-layout','detail-goal','screen-caption','render-caption','live-status','zoom1','zoom2','interactive-view','native-view','example','review-panel'])elements.set(id,new Element(id));
const sceneButtons=['ready','pause','restart','gameover'].map(scene=>{const b=new Element();b.dataset.scene=scene;return b;});
const document=new EventTarget();
document.hidden=false;document.documentElement=new Element();
document.getElementById=id=>elements.get(id);
document.createElement=()=>new Element();
document.querySelectorAll=selector=>selector==='#game-nav button'?elements.get('game-nav').children:selector==='[data-scene],#example'?[...sceneButtons,elements.get('example')]:selector==='[data-scene]'?sceneButtons:[];
let clock=1000,queued;
globalThis.document=document;globalThis.window=new EventTarget();globalThis.performance={now:()=>clock};
globalThis.localStorage={data:new Map(),getItem(k){return this.data.get(k)||null;},setItem(k,v){this.data.set(k,v);}};
globalThis.requestAnimationFrame=cb=>{queued=cb;};
await import('./review.js');
const snapshot=()=>window.designReview.snapshot();
const button=caption=>elements.get('screen-controls').children.find(b=>b.getAttribute('aria-label')===caption);
function press(caption){const b=button(caption);assert.ok(b,`visible control: ${caption}`);assert.ok(!b.disabled,`enabled control: ${caption}`);b.dispatchEvent(new Event('pointerdown',{cancelable:true}));}
function nav(id){elements.get('game-nav').children.find(b=>b.dataset.game===id).onclick();}
function frames(seconds){const count=Math.round(seconds*60);for(let i=0;i<count;i++){clock+=1000/60;const cb=queued;cb(clock);}}
function pointer(type,x,y){const e=new Event(type,{cancelable:true});Object.assign(e,{clientX:x,clientY:y,pointerId:1});elements.get('game-canvas').dispatchEvent(e);}
function resetCurrent(){if(snapshot().overlay==='ready')press('开始游戏');else{if(!snapshot().overlay)press('暂停');if(snapshot().overlay==='finish')press('再玩一次');else{press('重新开始');press('确认重开');}}}

test('all four lobby cards open ready scenes and pause freezes simulation',()=>{
  assert.equal(snapshot().current,'lobby');
  for(const [id,label] of [['runner','小黑快跑'],['slide','拼块逃脱'],['fruit','切水果'],['code','猜色密码']]){
    nav('lobby');press(`进入${label}`);assert.equal(snapshot().overlay,'ready');press('开始游戏');
    assert.equal(snapshot().state.status,'playing');frames(.08);press('暂停');
    const before=JSON.stringify(snapshot().state);frames(.5);assert.equal(JSON.stringify(snapshot().state),before);
    press('继续游戏');assert.equal(snapshot().overlay,null);press('返回');assert.equal(snapshot().overlay,'pause');
    press('返回列表');assert.equal(snapshot().current,'lobby');nav(id);assert.equal(snapshot().overlay,'pause');
  }
});

test('runner press jumps once; releasing and waiting do not trigger auto-jump',()=>{
  nav('runner');resetCurrent();const base=snapshot().state.jumpCount;press('跳跃');
  assert.equal(snapshot().state.jumpCount,base+1);frames(.15);assert.ok(snapshot().state.player.y<146);
  frames(.8);assert.equal(snapshot().state.player.grounded,true);assert.equal(snapshot().state.jumpCount,base+1);
});

test('slide overshoot snaps to last legal cell, undo and verified hints complete level',()=>{
  nav('slide');resetCurrent();pointer('pointerdown',104,180);pointer('pointermove',104,375);pointer('pointerup',104,375);
  assert.equal(snapshot().state.pieces.find(p=>p.id==='C').y,5);assert.equal(snapshot().state.moves,1);
  press('撤销');assert.equal(snapshot().state.moves,0);assert.equal(snapshot().state.pieces.find(p=>p.id==='C').y,4);
  let count=0;while(!snapshot().overlay&&count++<20)press('提示一步');
  assert.equal(snapshot().state.status,'won');assert.equal(snapshot().overlay,'finish');press('下一关');assert.equal(snapshot().state.level,2);
});

test('restart cancellation preserves all progress and only confirmation resets it',()=>{
  nav('slide');if(snapshot().overlay==='pause')press('继续游戏');press('提示一步');press('暂停');
  const before=JSON.stringify(snapshot().state);press('重新开始');assert.equal(snapshot().overlay,'restart');press('取消');
  assert.equal(snapshot().overlay,'pause');assert.equal(JSON.stringify(snapshot().state),before);
  press('重新开始');press('确认重开');assert.equal(snapshot().state.moves,0);assert.equal(snapshot().overlay,null);
});

test('guess input gates submit and history can be scrolled without changing guesses',()=>{
  nav('code');resetCurrent();assert.ok(button('提交').disabled);press('颜色1');assert.ok(button('提交').disabled);
  const secret=debugSecret(createGame('code',20261112)),wrong=secret[0]%6+1;
  for(let round=0;round<6;round++){
    for(let i=0;i<4;i++){press(`输入位置${i+1}`);press(`颜色${wrong}`);}
    assert.ok(!button('提交').disabled);press('提交');assert.equal(snapshot().state.history.length,round+1);
  }
  const before=JSON.stringify(snapshot().state);pointer('pointerdown',170,55);pointer('pointermove',170,135);pointer('pointerup',170,135);
  assert.ok(snapshot().historyOffset>0);assert.equal(JSON.stringify(snapshot().state),before);
});

test('fruit strokes score, HUD is excluded and pointer cancel stops the trail',()=>{
  nav('fruit');resetCurrent();frames(.45);const s=snapshot().state;
  const fruit=s.entities.find(e=>e.kind!=='bomb'&&e.y>=45&&e.y<=224);assert.ok(fruit,'a visible collectible appears');
  pointer('pointerdown',fruit.x-fruit.r-20,fruit.y);pointer('pointermove',fruit.x+fruit.r+20,fruit.y);pointer('pointerup',fruit.x+fruit.r+20,fruit.y);
  assert.ok(snapshot().state.score>s.score);const after=JSON.stringify(snapshot().state);
  pointer('pointerdown',100,10);pointer('pointermove',200,10);pointer('pointerup',200,10);assert.equal(JSON.stringify(snapshot().state),after);
  pointer('pointerdown',10,90);pointer('pointercancel',10,90);pointer('pointermove',300,90);assert.equal(JSON.stringify(snapshot().state),after);
});

test('leaving the document pauses and returning does not catch up hidden elapsed time',()=>{
  nav('runner');resetCurrent();document.hidden=true;document.dispatchEvent(new Event('visibilitychange'));
  assert.equal(snapshot().overlay,'pause');const before=JSON.stringify(snapshot().state);frames(4);
  document.hidden=false;document.dispatchEvent(new Event('visibilitychange'));assert.equal(JSON.stringify(snapshot().state),before);
  press('继续游戏');frames(.02);assert.ok(snapshot().state.time<.1);
});

test('native screenshot mode keeps controls inactive and interactive mode returns paused',()=>{
  nav('code');resetCurrent();elements.get('native-view').onclick();assert.equal(snapshot().nativeMode,true);
  assert.equal(elements.get('screen-controls').children.length,0);assert.equal(elements.get('native-screen').hidden,false);
  sceneButtons.find(b=>b.dataset.scene==='restart').onclick();assert.equal(elements.get('native-screen').src,'screenshots/restart.png');
  elements.get('interactive-view').onclick();assert.equal(snapshot().nativeMode,false);assert.equal(snapshot().overlay,'pause');
});

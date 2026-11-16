// 检查状态与数据处理；不代替浏览器/实板的视觉和硬件验证。
const fs = require('node:fs');
const vm = require('node:vm');
const path = require('node:path');
const assert = require('node:assert/strict');
const elements = new Map(), listeners = new Map(), timers = new Map();
const shortcuts = Array.from({length:5},(_,index) => ({
  dataset:{previewPage:String(index)},disabled:false,attributes:{},handlers:{},
  setAttribute(name,value) { this.attributes[name]=value; },
  addEventListener(name,fn) { this.handlers[name]=fn; }
}));
let timerId = 0, urlId = 0;
const revoked = [];
function element(id) {
  if (!elements.has(id)) elements.set(id, {
    innerHTML:'', textContent:'', value:'', style:{}, disabled:false,
    addEventListener(type,fn) { listeners.set(id + ':' + type,fn); }
  });
  return elements.get(id);
}
const context = vm.createContext({
  document:{getElementById:element,querySelectorAll() { return shortcuts; },
    addEventListener(type,fn) { listeners.set('document:' + type,fn); }},
  window:{addEventListener(type,fn) { listeners.set('window:' + type,fn); }},
  setInterval(){},
  setTimeout(fn,delay) { const id = ++timerId; timers.set(id,{fn,delay}); return id; },
  clearTimeout(id) { timers.delete(id); },
  URL:{createObjectURL(file) { return 'blob:preview/' + ++urlId + '/' + file.name; },revokeObjectURL(url) { revoked.push(url); }},
  Image:class {
    set src(url) { if (!url) return; if (url.includes('bad')) this.onerror?.(); else this.onload?.(); }
  }
});
const run = code => vm.runInContext(code,context);
async function main() {
  const html = fs.readFileSync(path.join(__dirname,'prototype.html'),'utf8');
  for (const file of ['app.js','app.css','calendar.css','studio.js','studio.css']) assert(html.includes(file),file + ' must be linked');
  for (const id of ['screen','status','left','right','wake','idle','photo-files','photo-folder','message-file',
    'design-mode','implementation-title','implementation-status','implementation-needs','implementation-check'])
    assert(html.includes('id="' + id + '"'),'missing element ' + id);
  run(fs.readFileSync(path.join(__dirname,'app.js'),'utf8'));
  run(fs.readFileSync(path.join(__dirname,'studio.js'),'utf8'));
  assert.equal(run('state.designMode'),true);
  assert(!Array.from(timers.values()).some(timer=>timer.delay===60000),
    'design mode keeps current page visible');
  assert.equal(element('implementation-title').textContent,'主页 · 天气时钟');
  shortcuts[3].handlers.click();
  assert.equal(run('state.page'),3);
  assert.equal(element('implementation-title').textContent,'倒计时 · 下一件期待的事');
  assert.equal(shortcuts[3].attributes['aria-pressed'],'true');
  run("startVoice();"); assert(shortcuts.every(button=>button.disabled));
  shortcuts[0].handlers.click(); assert.equal(run('state.page'),3);
  run('exitVoice();returnHome();');

  assert.equal(run('state.page'),0);
  run('navigate(-1)'); assert.equal(run('state.page'),4);
  run('navigate(1)'); assert.equal(run('state.page'),0);
  for (let i=0;i<5;i++) run('navigate(1)');
  assert.equal(run('state.page'),0);

  run("state.page=4;openApp('album');state.photo=2;state.slideshow=true;startVoice();navigate(1);returnHome();");
  assert.equal(run('state.voice'),true);
  assert.equal(run('state.photo'),2);
  assert(!Array.from(timers.values()).some(timer => timer.delay===5000 || timer.delay===60000),
    'voice suspends idle and slideshow timers');
  run('exitVoice()'); assert.equal(run('state.page'),4); assert.equal(run('state.app'),null); assert(!run('voice()').includes('打断')); run("openApp('album');state.photo=2;state.slideshow=true;"); assert.equal(run('state.slideshow'),true);
  run('navigate(1)'); assert.equal(run('state.photo'),0);
  run("dispatch('album-manage');dispatch('back');"); assert.equal(run('state.app'),'album');
  assert.equal(run('state.section'),null);
  run("dispatch('back');"); assert.equal(run('state.page'),4); assert.equal(run('state.app'),null);

  run("openApp('letter');dispatch('theme-night');state.draftMessageId='birthday';");
  assert.equal(run('state.theme'),'forest','draft theme is not applied immediately');
  run("dispatch('apply-message');"); assert.equal(run('state.page'),2);
  assert.equal(run('state.theme'),'night'); assert.equal(run('chosenMessage().id'),'birthday');
  run("openApp('letter');state.tf=false;refreshMessages();");
  assert.equal(run('chosenMessage().id'),'birthday','card removal keeps loaded message');
  run('state.tf=true');

  const imported = JSON.parse(fs.readFileSync(path.join(__dirname,'tf/messages/messages.json'),'utf8'));
  context.imported = imported;
  run('state.importedMessages=validateMessages(imported);refreshMessages();');
  assert.equal(run('state.messages.length'),2);
  context.duplicate = [imported[0],imported[0]];
  assert.throws(() => run('validateMessages(duplicate)'),/格式无效/);
  assert.equal(run('state.messages.length'),2,'invalid data does not change active messages');
  const handler = listeners.get('message-file:change');
  const previous = run('JSON.stringify(state.importedMessages)');
  await handler({target:{files:[{size:10,text:async () => '{broken'}]}});
  assert.equal(run('JSON.stringify(state.importedMessages)'),previous);
  await handler({target:{files:[{size:2,text:async () => '[]'}]}});
  run('refreshMessages()'); assert.equal(run('state.messages.length'),0); assert.equal(run('chosenMessage()'),null);
  run("state.importedMessages=validateMessages([{id:'a',title:'<img src=x>',text:'<script>bad</script>'}]);refreshMessages();state.page=2;state.app=null;render();");
  assert(!element('screen').innerHTML.includes('<script>bad'));
  assert(element('screen').innerHTML.includes('&lt;script&gt;bad'));

  context.files = [
    {name:'010.jpg',size:2},{name:'002.jpg',size:3},{name:'bad.jpg',size:5},{name:'ignore.txt',size:1}
  ];
  await run('importPhotos(files)');
  assert.equal(run('photos.length'),2);
  assert.equal(run('photos[0].name'),'002.jpg');
  assert.equal(run('photos[1].name'),'010.jpg');
  assert(revoked.some(url=>url.includes('bad.jpg')),'failed image URL is released');
  run('state.photo=1');
  await run('importPhotos(files)');
  assert.equal(run('photos[state.photo].name'),'010.jpg','rescan preserves selected file');
  await run('importPhotos([])'); assert.equal(run('photos.length'),0);
  run("state.app='album';state.section=null;navigate(1);render();");
  assert.equal(run('state.photo'),0);
  assert(element('screen').innerHTML.includes('还没有照片'));
  run("dispatch('restore-demo');dispatch('tf-toggle');render();");
  assert(element('screen').innerHTML.includes('请插入 TF 卡'));
  run("dispatch('tf-toggle');");

  run("openApp('settings');dispatch('setting-display');");
  listeners.get('screen:input')({target:{id:'brightness',value:'37'}});
  assert.equal(run('state.brightness'),37);
  assert.equal(element('screen').style.filter,'brightness(0.37)');
  listeners.get('screen:change')({target:{id:'background',checked:false}});
  run("dispatch('back');dispatch('back');");
  assert(element('screen').innerHTML.includes('apps-plain'));
  run("openApp('weather');dispatch('set-region');");
  element('region').value='杭州 · 西湖区';
  run("dispatch('save-region');"); assert.equal(run('state.region'),'杭州 · 西湖区');
  run("startVoice();exitVoice();"); assert.equal(run('state.page'),4); assert.equal(run('state.app'),null);
  assert.equal(run('state.section'),null);

  run("openApp('calendar');state.calendarYear=2024;state.calendarMonth=1;render();");
  assert.equal((element('screen').innerHTML.match(/class="calendar-day/g) || []).length,29);
  assert.equal(run('monthInfo(2023,1).days'),28);
  assert.equal(run('monthInfo(1900,1).days'),28);
  assert.equal(run('monthInfo(2000,1).days'),29);
  assert.equal(run('monthInfo(2024,0).offset'),0,'2024-01-01 is Monday');
  run("state.calendarYear=2026;state.calendarMonth=11;dispatch('month-next');");
  assert.equal(run('state.calendarYear'),2027); assert.equal(run('state.calendarMonth'),0);
  run("dispatch('month-prev');"); assert.equal(run('state.calendarYear'),2026);
  run("state.calendarYear=1900;state.calendarMonth=0;dispatch('month-prev');");
  assert.equal(run('state.calendarYear'),1900);
  run("state.calendarYear=2099;state.calendarMonth=11;dispatch('month-next');");
  assert.equal(run('state.calendarYear'),2099);
  assert.equal(run("chinaDateKey(new Date('2026-10-02T17:00:00Z'))"),'2026-10-03');
  assert.equal(run("parseCountdownDate('2027-01-01T00:00')"),Date.parse('2026-12-31T16:00:00Z'));
  for (const value of ['2023-02-29T00:00','2024-02-30T00:00','2024-13-01T00:00','2024-01-01T24:00','2024-01-01T00:60','2100-01-01T00:00','bad'])
    assert(Number.isNaN(run('parseCountdownDate(' + JSON.stringify(value) + ')')),value);
  assert.equal(run('countdownParts(90061000,0).days'),1);
  assert.equal(run('countdownParts(90061000,0).hours'),1);
  assert.equal(run('countdownParts(90061000,0).minutes'),1);
  assert.equal(run('countdownParts(90061000,0).seconds'),1);
  assert.equal(run('countdownParts(0,1000).done'),true);
  assert.equal(run('countdownParts(0,1000).seconds'),0);
  run("dispatch('calendar-today');dispatch('day-2027-01-01');dispatch('edit-countdown');");
  assert.equal(run('state.draftCountdownDate'),'2027-01-01T00:00');
  listeners.get('screen:input')({target:{id:'countdown-name',value:'下次见面'}});
  listeners.get('screen:input')({target:{id:'countdown-date',value:'2027-01-01T12:30'}});
  run('startVoice();exitVoice();');
  assert.equal(run('state.section'),null); assert.equal(run('state.page'),4); run("openApp('calendar');dispatch('edit-countdown');state.draftCountdownName='下次见面';state.draftCountdownDate='2027-01-01T12:30';render();");
  assert(element('screen').innerHTML.includes('下次见面'));
  assert(element('screen').innerHTML.includes('2027-01-01T12:30'));
  element('countdown-name').value='下次见面'; element('countdown-date').value='2027-01-01T12:30';
  run("dispatch('save-countdown');"); assert.equal(run('state.page'),3);
  assert.equal(run('state.app'),null); assert.equal(run('state.countdown.name'),'下次见面');
  run('startVoice();exitVoice();'); assert.equal(run('state.page'),4);
  run("dispatch('countdown-settings');");
  element('countdown-name').value=''; run("dispatch('save-countdown');");
  assert.equal(run('state.countdown.name'),'下次见面','invalid draft keeps active countdown');
  element('countdown-name').value='新目标'; element('countdown-date').value='2023-02-29T00:00';
  run("dispatch('save-countdown');"); assert.equal(run('state.countdown.name'),'下次见面');
  run("dispatch('back');"); assert.equal(run('state.app'),'calendar');
  assert(element('screen').innerHTML.includes('class="calendar-day selected target"'));
  run("dispatch('edit-countdown');dispatch('clear-countdown');");
  assert.equal(run('state.countdown'),null); assert.equal(run('state.section'),null);
  run("dispatch('back');"); assert.equal(run('state.page'),4);
  assert.equal((element('screen').innerHTML.match(/class="app"/g) || []).length,6);
  run('navigate(-1);'); assert.equal(run('state.page'),3);
  assert(element('screen').innerHTML.includes('还没有设置倒计时'));

  listeners.get('design-mode:change')({target:{checked:false}});
  assert.equal(run('state.designMode'),false);
  run('returnHome();navigate(1);');
  const idle = Array.from(timers.values()).find(timer=>timer.delay===60000);
  assert(idle); idle.fn(); assert.equal(run('state.page'),0);
  console.log('PASS: UI design shortcuts/context notes/design mode, five-page cycle, six apps, calendar/countdown timezone and validation, voice restoration, and existing album/message/settings flows.');
}
main().catch(error=>{console.error(error);process.exitCode=1;});

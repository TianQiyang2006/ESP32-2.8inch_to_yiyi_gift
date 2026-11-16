// Isolated browser QA of the exact firmware portal; production backend validation is tested separately.
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict');
const root=path.resolve(__dirname,'../..'),defaultOut=path.join(root,'build/days-style-refactor-20261115/phone');
const outputIndex=process.argv.indexOf('--out'),outputArgument=process.argv.find(argument=>argument.startsWith('--out='));
const out=path.resolve(outputArgument?outputArgument.slice(6):outputIndex>=0?process.argv[outputIndex+1]||defaultOut:defaultOut);
const {chromium}=require(path.join(root,'build/countdown-days-20261114/browser-tools/node_modules/playwright'));
const html=fs.readFileSync(path.join(root,'hardware/arduino/yiyi_desk/gift_portal.h'),'utf8').split('R"html(')[1].split(')html";')[0];
fs.mkdirSync(out,{recursive:true});
(async()=>{
 const browser=await chromium.launch({executablePath:'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe',headless:true});
 const page=await browser.newPage({viewport:{width:390,height:1040},deviceScaleFactor:1,isMobile:true,hasTouch:true});
 const errors=[],requests=[],config={today:'2026-11-14',countdownDays:{items:[]},deviceSettings:{brightness:80,volume:20},dailyRecords:{}};
 let fail=false,delay=0,nextId=2;page.on('pageerror',error=>errors.push(error.message));
 await page.route('http://127.0.0.1:8877/**',async route=>{
  const request=route.request(),url=new URL(request.url());
  if(url.pathname==='/')return route.fulfill({contentType:'text/html; charset=utf-8',body:html});
  if(url.pathname==='/api/gift')return route.fulfill({json:config});
  if(url.pathname==='/api/radio-stations')return route.fulfill({json:{limit:20,revision:1,stations:[]}});
  if(url.pathname==='/api/countdown-days'){
   const data=request.postDataJSON();requests.push(data);
   if(delay)await new Promise(resolve=>setTimeout(resolve,delay));
   if(fail)return route.fulfill({status:500,json:{error:'保存失败，原记录保留'}});
   if(data.action==='remove')config.countdownDays.items=config.countdownDays.items.filter(item=>item.id!==data.id);
   else{const item={...data.item,id:data.item.id||nextId++},index=config.countdownDays.items.findIndex(previous=>previous.id===item.id);if(index<0)config.countdownDays.items.push(item);else config.countdownDays.items[index]=item;}
   return route.fulfill({json:{ok:true}});
  }
  return route.fulfill({json:{ok:true}});
 });
 await page.goto('http://127.0.0.1:8877/#countdown-days');await page.locator('#days-count').filter({hasText:'0 / 20'}).waitFor();
 const form=page.locator('#days-form'),yearly=form.locator('[name=yearly]'),mode=value=>form.locator(`[name=mode][value="${value}"]`),category=value=>form.locator(`[name=category][value="${value}"]`),style=value=>form.locator(`[name=style][value="${value}"]`),color=value=>form.locator(`[name=color][value="${value}"]`),select=page.locator('#days-select'),preview=page.locator('#days-preview');
 const textMode=value=>form.locator(`[name=customTextColor][value="${value}"]`),textHex=page.locator('#days-text-color-hex'),textPicker=page.locator('#days-text-color-picker');
 const card=name=>page.locator('#days-list .days-card').filter({has:page.locator('.days-card-name',{hasText:name})});
 const screenshots=[],screenshotColors={};
 async function choose(locator){await locator.locator('..').click();assert(await locator.isChecked());}
 async function previewColors(){
  await page.waitForFunction(()=>{
   const card=document.querySelector('#days-preview'),hex=card.style.getPropertyValue('--day-fg').trim();
   if(!/^#[0-9a-f]{6}$/i.test(hex))return false;
   const expected='rgb('+[1,3,5].map(index=>parseInt(hex.slice(index,index+2),16)).join(', ')+')';
   return getComputedStyle(card).color===expected&&getComputedStyle(document.querySelector('#days-preview-number')).color===expected;
  });
  // Finish actual CSS transitions and let two complete frames paint before capture.
  await preview.evaluate(async element=>{await Promise.all(element.getAnimations({subtree:true}).map(animation=>animation.finished.catch(()=>{})));await new Promise(resolve=>requestAnimationFrame(()=>requestAnimationFrame(resolve)));});
  const rendered=await page.locator('#days-preview-number').evaluate(element=>getComputedStyle(element).color);
  const design=await preview.getAttribute('data-style'),tint=await form.locator('[name=color]:checked').inputValue();
  const custom=await textMode('custom').isChecked();
  if(!custom&&design==='heart'&&tint==='0')assert.equal(rendered,'rgb(255, 255, 255)','Red/pink heart must finish painting white digits');
  if(!custom&&design==='ink'&&tint==='0')assert.equal(rendered,'rgb(41, 79, 106)','Blue ink must finish painting blue digits');
  return rendered;
 }
 async function screenshot(name){screenshotColors[name]=await previewColors();screenshots.push(name);await page.screenshot({path:path.join(out,name),fullPage:false});}
 async function textRendered(expected){await previewColors();for(const selector of ['#days-preview-name','#days-preview-number','#days-preview-label','#days-preview-date','#days-preview-category'])assert.equal(await page.locator(selector).evaluate(element=>getComputedStyle(element).color),expected,selector+' must use the chosen text color');}
 async function fill(name,date,tint=0,repeat=false,type='countdown',group='general',design){
  await choose(textMode('auto'));await choose(category(group));if(design)await choose(style(design));await choose(mode(type));await form.locator('[name=name]').fill(name);await form.locator('[name=date]').fill(date);await choose(color(tint));if(type==='countdown')await yearly.setChecked(repeat);
 }
 async function settled(){await page.waitForFunction(()=>!document.querySelector('#days-submit').disabled);}
 async function saved(number){await page.locator('#days-count').filter({hasText:`${number} / 20`}).waitFor();await settled();}
 async function number(name,value,label){assert.equal(await card(name).locator('.days-card-number').evaluate(element=>element.firstChild.textContent),value);assert.equal(await card(name).locator('.days-card-number small').textContent(),label);}
 async function previewNumber(value,label){assert.equal(await page.locator('#days-preview-number').textContent(),value);if(label)assert.equal(await page.locator('#days-preview-label').textContent(),label);}
 await previewNumber('—','选择日期，开始记录');await screenshot('phone-empty.png');

 // Five-field records fall back to general/classic/countdown and remain selectable.
 config.countdownDays.items.push({id:1,name:'依依的生日',date:'2026-12-20',yearly:true,color:1});await saved(1);
 await number('依依的生日','36','天后 · 值得期待');assert.equal(await card('依依的生日').locator('.days-category-badge').textContent(),'普通日子');
 await select.selectOption('1');assert(await mode('countdown').isChecked());assert(await category('general').isChecked());assert(await style('classic').isChecked());assert(await color(1).isChecked());assert(await yearly.isChecked());assert.equal(await form.locator('[name=name]').inputValue(),'依依的生日');assert.equal(await form.locator('[name=date]').inputValue(),'2026-12-20');await previewNumber('36');
 await choose(mode('countup'));assert(await yearly.isDisabled());assert(!(await yearly.isChecked()));assert(!(await page.locator('#days-repeat').isVisible()));assert.equal(await page.locator('#days-date-label').textContent(),'开始日期');assert.match(await page.locator('#days-mode-help').textContent(),/0 天.*每天加 1/);await previewNumber('36','天后开始');
 await choose(mode('countdown'));assert(await yearly.isEnabled());assert(!(await yearly.isChecked()));await select.selectOption('0');assert(await style('classic').isChecked());assert.equal(await form.locator('[name=id]').inputValue(),'0');

 // Categories recommend style/color/mode, while keeping the custom name/date and existing ID.
 await select.selectOption('1');const writesBefore=requests.length;await form.locator('[name=name]').fill('小满的生日');await choose(category('love'));assert(await style('heart').isChecked());assert(await mode('countup').isChecked());assert(await color(0).isChecked());assert(!(await yearly.isChecked()));
 assert.equal(await form.locator('[name=id]').inputValue(),'1');assert.equal(await form.locator('[name=name]').inputValue(),'小满的生日');assert.equal(await form.locator('[name=date]').inputValue(),'2026-12-20');assert.equal(requests.length,writesBefore);
 await choose(category('birthday'));assert(await style('laurel').isChecked());assert(await mode('countdown').isChecked());assert(await yearly.isChecked());assert(await color(2).isChecked());assert.equal(await form.locator('[name=id]').inputValue(),'1');assert.equal(await page.locator('#days-preview-name').textContent(),'小满的生日');
 await page.locator('#days-submit').click();await page.getByRole('button',{name:'编辑小满的生日',exact:true}).waitFor();await settled();assert.equal(requests[0].item.style,'laurel');assert.equal(requests[0].item.category,'birthday');assert.equal(requests[0].item.color,2);assert.equal(requests[0].item.yearly,true);
 // Six/seven-field legacy category records infer their default style without changing stored color.
 delete config.countdownDays.items[0].style;await page.reload();await saved(1);await select.selectOption('1');assert(await style('laurel').isChecked());assert(await color(2).isChecked());
 // Annual previews show the next effective occurrence, while the input retains the original date.
 await form.locator('[name=date]').fill('2026-01-01');assert.match(await page.locator('#days-preview-date').textContent(),/^2027 \. 01 \. 01/);assert.equal(await form.locator('[name=date]').inputValue(),'2026-01-01');
 await form.locator('[name=date]').fill('2024-02-29');assert.match(await page.locator('#days-preview-date').textContent(),/^2027 \. 02 \. 28/);assert.equal(await form.locator('[name=date]').inputValue(),'2024-02-29');await select.selectOption('0');

 await fill('一起跨年','2027-01-01',0);await page.locator('#days-submit').click();await saved(2);
 await fill('今天也要开心','2026-11-14',1);await page.locator('#days-submit').click();await saved(3);await number('今天也要开心','今天','就是这个日子');
 await fill('柚子和小满','2026-01-01',0,false,'countup','love');await page.locator('#days-submit').click();await saved(4);await number('柚子和小满','317','天 · 已经走过');assert.equal(requests.at(-1).item.style,'heart');assert.equal(requests.at(-1).item.yearly,false);
 await fill('从今天开始','2026-11-14',0,false,'countup');await page.locator('#days-submit').click();await saved(5);await number('从今天开始','0','开始当天');
 await fill('明天出发','2026-11-15',2,false,'countup');await page.locator('#days-submit').click();await saved(6);await number('明天出发','1','天后开始');

 // Every style and palette updates the preview immediately and preserves the rest of the draft.
 await select.selectOption('4');assert(await style('heart').isChecked());await choose(color(2));const originalName=await form.locator('[name=name]').inputValue();
 for(const design of ['classic','laurel','ink','heart','dots']){await choose(style(design));assert.equal(await preview.getAttribute('data-style'),design);assert.equal(await form.locator('[name=name]').inputValue(),originalName);await previewNumber('317');}
 assert.equal(await form.locator('.days-color:visible').count(),2);assert(!(await color(2).isVisible()));assert(await color(2).isChecked());assert.equal(await preview.evaluate(element=>element.style.getPropertyValue('--day-bg')),'#ffffff');
 await choose(style('ink'));assert(await color(2).isChecked());assert.equal(await form.locator('.days-color:visible').count(),4);assert.equal(await form.locator('.days-color small').first().textContent(),'晴空蓝');
 await choose(style('heart'));assert.equal(await form.locator('.days-color small').first().textContent(),'红粉');await choose(color(1));assert.equal(await preview.evaluate(element=>element.style.getPropertyValue('--day-accent')),'#ffa518');
 await choose(style('dots'));await choose(color(3));assert.equal(await preview.evaluate(element=>element.style.getPropertyValue('--day-bg')),'#363b45');await choose(style('ink'));await choose(color(2));await choose(mode('countdown'));await page.locator('#days-submit').click();await settled();assert.equal(requests.at(-1).item.style,'ink');assert.equal(requests.at(-1).item.color,2);
 await select.selectOption('4');assert(await category('love').isChecked());assert(await mode('countdown').isChecked());assert(await style('ink').isChecked());assert(await color(2).isChecked());assert.equal(await form.locator('[name=date]').inputValue(),'2026-01-01');
 await choose(mode('countup'));await form.locator('[name=name]').fill('柚子与小满');assert.equal(await page.locator('#days-preview-name').textContent(),'柚子与小满');await form.locator('[name=date]').fill('2026-11-14');await previewNumber('0','开始当天');await form.locator('[name=date]').fill('2026-01-01');await page.locator('#days-submit').click();await settled();await select.selectOption('0');

 // Natural-day rollover updates cards and a currently open preview without replacing the draft.
 await select.selectOption('4');config.today='2026-11-15';await page.waitForFunction(()=>document.querySelectorAll('.days-card-number small')[5].textContent==='开始当天');await number('柚子与小满','318','天 · 已经走过');await number('从今天开始','1','天 · 已经走过');await number('明天出发','0','开始当天');await previewNumber('318');assert(await style('ink').isChecked());
 config.today='2026-11-14';await page.waitForFunction(()=>document.querySelectorAll('.days-card-number small')[5].textContent==='天后开始');await select.selectOption('0');

 // Optional text color overrides every card text element, survives recommendations, and never colors controls.
 await select.selectOption('4');const controlColor=await form.locator('legend').first().evaluate(element=>getComputedStyle(element).color);await choose(textMode('custom'));await textHex.fill('#1a64b2');assert.equal(await textPicker.inputValue(),'#1a64b2');await textRendered('rgb(26, 100, 178)');assert.equal(await form.locator('legend').first().evaluate(element=>getComputedStyle(element).color),controlColor);
 await choose(style('heart'));await choose(color(0));await textRendered('rgb(26, 100, 178)');await choose(category('birthday'));assert.equal(await textHex.inputValue(),'#1a64b2');await textRendered('rgb(26, 100, 178)');assert.equal(await form.locator('[name=id]').inputValue(),'4');assert.equal(await form.locator('[name=name]').inputValue(),'柚子与小满');
 await choose(category('love'));await choose(style('ink'));await choose(color(2));await page.locator('#days-submit').click();await settled();assert.equal(requests.at(-1).item.textColor,'#1A64B2');assert.equal(Object.keys(requests.at(-1).item).length,9);
 await page.reload();await saved(6);await select.selectOption('4');assert(await textMode('custom').isChecked());assert.equal(await textHex.inputValue(),'#1A64B2');await textRendered('rgb(26, 100, 178)');assert.equal(await card('柚子与小满').locator('.days-card-name').evaluate(element=>getComputedStyle(element).color),'rgb(26, 100, 178)');
 const beforeInvalid=requests.length;await textHex.fill('#GGGGGG');assert(await page.locator('#days-text-color-error').isVisible());assert.equal(await textHex.getAttribute('aria-invalid'),'true');await page.locator('#days-submit').click();assert.equal(requests.length,beforeInvalid);assert.equal(await textHex.inputValue(),'#GGGGGG');assert.equal(await form.locator('[name=name]').inputValue(),'柚子与小满');
 await choose(textMode('auto'));assert(!(await page.locator('#days-text-color-fields').isVisible()));await page.locator('#days-submit').click();await settled();assert(!Object.hasOwn(requests.at(-1).item,'textColor'));await page.reload();await saved(6);await select.selectOption('4');assert(await textMode('auto').isChecked());assert(!(await page.locator('#days-text-color-fields').isVisible()));await select.selectOption('0');

 // Polling, failed writes and edits made during a write retain raw text-color drafts, including invalid edits.
 await fill('还没保存的相伴','2026-05-20',2,false,'countup','love','laurel');await choose(textMode('custom'));await textHex.fill('#5b3758');await page.waitForTimeout(2300);assert.equal(await form.locator('[name=name]').inputValue(),'还没保存的相伴');assert(await style('laurel').isChecked());assert(await category('love').isChecked());assert(await color(2).isChecked());assert.equal(await textHex.inputValue(),'#5b3758');
 fail=true;await page.locator('#days-submit').click();await page.locator('#days-note.error').waitFor();await settled();assert.equal(await form.locator('[name=name]').inputValue(),'还没保存的相伴');assert(await style('laurel').isChecked());assert.equal(await textHex.inputValue(),'#5b3758');fail=false;
 delay=400;await page.locator('#days-submit').click();await textHex.fill('#12GGGG');await form.locator('[name=name]').fill('保存期间的新草稿');await choose(style('dots'));await choose(color(3));await saved(7);assert.equal(await form.locator('[name=name]').inputValue(),'保存期间的新草稿');assert(await style('dots').isChecked());assert(await color(3).isChecked());assert(await textMode('custom').isChecked());assert.equal(await textHex.inputValue(),'#12GGGG');assert(await page.locator('#days-text-color-error').isVisible());assert.equal(requests.at(-1).item.textColor,'#5B3758');delay=0;
 await page.getByRole('button',{name:'删除一起跨年',exact:true}).click();await page.locator('#days-delete-cancel').click();assert.equal(config.countdownDays.items.length,7);await page.getByRole('button',{name:'删除一起跨年',exact:true}).click();await page.locator('#days-delete-confirm').click();await saved(6);
 await fill('<b>相伴</b>','2026-01-01',0,false,'countup','love');assert.equal(await page.locator('#days-preview-name').textContent(),'<b>相伴</b>');assert.equal(await preview.locator('b').count(),0);await page.locator('#days-submit').click();await saved(7);assert.equal(await page.locator('#days-list b').count(),0);
 await page.setViewportSize({width:360,height:900});await preview.scrollIntoViewIfNeeded();assert(await page.evaluate(()=>document.documentElement.scrollWidth<=window.innerWidth));await screenshot('phone-360.png');

 // Clean demonstration fixtures are used only for previews; real user dates are never populated.
 config.today='2026-11-15';config.countdownDays.items=[{id:101,name:'依依的生日',date:'2026-12-20',yearly:true,color:2,mode:'countdown',category:'birthday',style:'laurel'},{id:102,name:'我们相恋了',date:'2026-01-01',yearly:false,color:0,mode:'countup',category:'love',style:'heart'}];
 await page.reload();await saved(2);await page.waitForFunction(()=>document.querySelector('#status').textContent==='已连接设备 · 无需登录');await page.setViewportSize({width:390,height:1040});
 async function top(){await page.locator('#countdown-days').evaluate(element=>window.scrollTo({top:element.getBoundingClientRect().top+window.scrollY-72,behavior:'instant'}));}
 await select.selectOption('102');
 for(const design of ['classic','laurel','ink','heart','dots']){await choose(style(design));await choose(color(design==='laurel'?2:0));await top();await screenshot('phone-style-'+design+'.png');}
 await select.selectOption('101');await top();await screenshot('phone-birthday.png');await form.locator('.days-details').scrollIntoViewIfNeeded();await screenshot('phone-categories.png');
 await select.selectOption('102');await top();await screenshot('phone-love.png');
 await page.locator('#days-list').evaluate(element=>window.scrollTo({top:element.getBoundingClientRect().top+window.scrollY-115,behavior:'instant'}));await screenshot('phone-special-list.png');
 await select.selectOption('101');await choose(style('classic'));await choose(color(2));await choose(textMode('custom'));await textHex.fill('#7a3656');await textRendered('rgb(122, 54, 86)');await top();await page.setViewportSize({width:390,height:1230});await screenshot('phone-text-color.png');
 assert.deepEqual(errors,[]);assert(requests.filter(request=>request.action==='upsert').every(request=>Object.keys(request.item).length===(Object.hasOwn(request.item,'textColor')?9:8)&&(!Object.hasOwn(request.item,'textColor')||/^#[0-9A-F]{6}$/.test(request.item.textColor))&&['classic','laurel','ink','heart','dots'].includes(request.item.style)&&['countdown','countup'].includes(request.item.mode)&&['general','birthday','love'].includes(request.item.category)&&!Object.hasOwn(request.item,'customTextColor')&&!Object.hasOwn(request.item,'textColorHex')&&!Object.hasOwn(request.item,'textColorPicker')));
 fs.writeFileSync(path.join(out,'portal-verification.json'),JSON.stringify({passed:true,assertions:'five live card styles, dynamic palettes, completed computed-color transitions, optional custom text color for all card text, strict HEX input, custom color save/reload and auto clear, controls unaffected, style/category/color retention, raw invalid in-flight draft protection, dots draft color preservation, category recommendations, custom names/dates, selection/edit synchronization, old style fallback, countdown/countup natural days, eight/nine-field persistence, mode/style overrides, polling/error/in-flight drafts, safe text, 360px layout, no page errors',writes:requests.length,api:'isolated fixtures; production backend tested separately',previewFixtures:'Demonstration dates only; real user dates are not populated',screenshots,screenshotColors},null,2));
 console.log('PASS phone portal: five live styles, palettes, selection, eight-field persistence, natural days, drafts and mobile screenshots');await browser.close();
})().catch(error=>{console.error(error);process.exit(1);});

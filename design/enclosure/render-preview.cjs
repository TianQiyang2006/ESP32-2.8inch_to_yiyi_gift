// Local visual verification. Run from the project root after starting the preview server.
const {chromium}=require('../../tools/design-review/node_modules/playwright-core');
const path=require('node:path');
const fs=require('node:fs');
const base='http://127.0.0.1:8693/';
const output=path.join(__dirname,'renders');
fs.mkdirSync(output,{recursive:true});
(async()=>{
  const browser=await chromium.launch({executablePath:'C:/Program Files/Google/Chrome/Application/chrome.exe',headless:true,args:['--use-angle=swiftshader','--enable-unsafe-swiftshader']});
  try {
  const page=await browser.newPage({viewport:{width:1440,height:980},deviceScaleFactor:1});
  const errors=[];page.on('pageerror',e=>{errors.push(e.message);console.error('Page error:',e.message);});
  await page.goto(base,{waitUntil:'networkidle'});await page.waitForFunction(()=>window.enclosureReady===true);
  await page.screenshot({path:path.join(output,'interactive-desktop.png'),fullPage:true});
  for(const id of ['b','c','a']){
    await page.locator(`button[data-concept="${id}"]`).click();
    if(await page.locator('body').getAttribute('data-concept')!==id)throw Error('Concept switch failed: '+id);
    for(const view of ['front','back','side','hero'])await page.locator(`[data-view="${view}"]`).click();
  }
  await page.locator('#screen').selectOption('clock');
  await page.locator('#rotate').click();
  if(await page.locator('#rotate').getAttribute('aria-pressed')!=='true')throw Error('Rotation toggle failed');
  await page.locator('#rotate').click();
  await page.setViewportSize({width:390,height:844});
  await page.screenshot({path:path.join(output,'interactive-mobile.png'),fullPage:true});
  const mobileOverflow=await page.evaluate(()=>document.documentElement.scrollWidth>innerWidth);
  if(mobileOverflow)throw Error('Mobile horizontal overflow');
  await page.setViewportSize({width:1500,height:1500});
  await page.goto(base+'?studio',{waitUntil:'networkidle'});
  await page.waitForFunction(()=>window.enclosureReady===true);
  for(const id of ['a','b','c'])for(const view of ['hero','front','back','side']){
    await page.locator(`button[data-concept="${id}"]`).dispatchEvent('click');
    await page.locator(`[data-view="${view}"]`).dispatchEvent('click');
    // Two animation frames ensure the resized camera has been presented.
    await page.evaluate(()=>new Promise(resolve=>requestAnimationFrame(()=>requestAnimationFrame(resolve))));
    await page.screenshot({path:path.join(output,`${id}-${view}.png`)});
  }
  const result={pageErrors:errors,mobileOverflow,conceptSwitches:3,viewButtons:12,screenSelector:true,rotationToggle:true,studioRenders:12};
  if(errors.length)throw Error('Browser errors: '+errors.join('; '));
  fs.writeFileSync(path.join(output,'verification.json'),JSON.stringify(result,null,2));
  console.log(JSON.stringify(result));
  } finally { await browser.close(); }
})().catch(e=>{console.error(e);process.exitCode=1;});

const {chromium}=require('../../../tools/design-review/node_modules/playwright-core');
const path=require('node:path');
(async()=>{
 const browser=await chromium.launch({executablePath:'C:/Program Files/Google/Chrome/Application/chrome.exe',headless:true,args:['--use-angle=swiftshader','--enable-unsafe-swiftshader']});
 try{
  const page=await browser.newPage({viewport:{width:1500,height:1200},deviceScaleFactor:1});
  const errors=[];page.on('pageerror',e=>errors.push(e.message));
  await page.goto('http://127.0.0.1:8693/print-a/review.html',{waitUntil:'networkidle'});
  await page.waitForFunction(()=>window.reviewReady===true);
  for(const view of ['assembled','inside','rear','exploded']){
   await page.locator(`button[data-view="${view}"]`).click();
   await page.evaluate(()=>new Promise(resolve=>requestAnimationFrame(()=>requestAnimationFrame(resolve))));
   await page.screenshot({path:path.join(__dirname,'output','review',view+'.png')});
  }
  if(errors.length)throw Error(errors.join('; '));
  console.log('Four actual CAD views rendered; browser errors: 0');
 }finally{await browser.close();}
})().catch(e=>{console.error(e);process.exitCode=1;});

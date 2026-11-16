(function () {
  'use strict';
  const model=window.YiyiDesign,clock=window.YiyiData,copy=model.clone;
  const endpoints={quote:'https://uapis.cn/api/v1/saying',poem:'https://poetry.palemoky.com/api/poems/random?lang=zh-Hans'};
  const providers={quote:'uapis',poem:'palemoky'};
  let options=copy(model.defaults.reading),cache=copy(model.defaults.readingCache),network=true;
  const listeners=new Set(),jobs={quote:null,poem:null},generation={quote:0,poem:0};
  const attempted={quote:'',poem:''},status={quote:{loading:false,error:'',retryAt:0},poem:{loading:false,error:'',retryAt:0}};
  const today=()=>clock.time().key;
  const current=(kind,value)=>value?.date===today()&&(kind!=='quote'||value.provider===providers.quote);
  const str=(value,max=100)=>typeof value==='string'?value.trim().slice(0,max):'';
  function parse(kind,payload) {
    const data=payload?.data;
    if(kind==='quote') {
      if(!str(payload?.text,3000))throw new Error('一言接口未返回有效内容');
      return {text:str(payload.text,3000),author:'',title:'',dynasty:''};
    }
    if(!Array.isArray(data?.content))throw new Error('诗词接口未返回有效内容');
    const text=data.content.filter(line=>typeof line==='string').map(line=>line.trim()).filter(Boolean).join('\n').slice(0,24000);
    if(!text)throw new Error('诗词接口未返回有效内容');
    return {text,author:str(data.author?.name),dynasty:str(data.dynasty?.name),title:str(data.title)};
  }
  function publish(type,kind) {listeners.forEach(fn=>fn({type,kind,cache:copy(cache)}));}
  function configure(settings,records,{enabled=true}={}) {
    for(const kind of ['quote','poem']) {
      const keys=Object.keys(options).filter(key=>key.startsWith(kind));
      const changed=keys.some(key=>options[key]!==settings[key]) || JSON.stringify(cache[kind])!==JSON.stringify(records[kind]) || network!==enabled;
      if(changed) {
        generation[kind]++;jobs[kind]?.controller.abort();jobs[kind]=null;
        attempted[kind]='';status[kind]={loading:false,error:'',retryAt:status[kind].retryAt};
      }
    }
    if(!options.autoUpdate&&settings.autoUpdate)for(const kind of ['quote','poem'])attempted[kind]='';
    options=copy(settings);cache=copy(records);network=enabled;
  }
  function read(kind) {
    const manual=options[kind+'Mode']==='manual',cached=cache[kind];
    const value=manual || !cached ? {text:options[kind+'Text'],author:options[kind+'Author'],title:options[kind+'Title']||'',dynasty:options[kind+'Dynasty']||'',updatedAt:null,date:'',source:manual?'manual':'example'} : cached;
    return {...copy(value),...status[kind],current:value.source==='api'&&current(kind,value)};
  }
  async function refresh(kind) {
    if(!Object.hasOwn(endpoints,kind)||!network||options[kind+'Mode']==='manual')return;
    if(jobs[kind])return jobs[kind].promise;
    if(Date.now()<status[kind].retryAt)return;
    const date=today(),token=++generation[kind],controller=new AbortController();
    attempted[kind]=date;
    status[kind]={loading:true,error:'',retryAt:Date.now()+10000};publish('status',kind);
    const promise=(async()=>{
      const timer=setTimeout(()=>controller.abort(),10000);
      try {
        // UApi allows HTTP origins but does not allow the null origin of file://.
        const url=kind==='quote'&&window.location?.protocol==='file:'?(window.YIYI_PHONE_BASE||'http://127.0.0.1:8766')+'/api/reading/quote':endpoints[kind];
        const response=await fetch(url,{signal:controller.signal,headers:{Accept:'application/json'},cache:'no-store'});
        if(generation[kind]!==token)return;
        if(response.status===429) {
          const header=response.headers.get('Retry-After'),seconds=Number(header),absolute=Date.parse(header||'');
          const delay=header&&Number.isFinite(seconds)?seconds*1000:Number.isFinite(absolute)?absolute-Date.now():60000;
          status[kind].retryAt=Date.now()+Math.max(60000,Math.min(delay,3600000));
          throw new Error('请求较频繁，请稍后再试');
        }
        if(!response.ok)throw new Error('接口暂不可用（'+response.status+'）');
        const value=parse(kind,await response.json());
        if(generation[kind]!==token || today()!==date)return;
        cache[kind]={...value,date,updatedAt:clock.now().getTime(),source:'api',provider:providers[kind]};
        status[kind].error='';publish('cache',kind);
      } catch(error) {
        if(generation[kind]===token)status[kind].error=error.name==='AbortError'?'请求超时，请稍后重试':error.message||'网络暂不可用';
      } finally {
        clearTimeout(timer);
        if(generation[kind]===token) {status[kind].loading=false;jobs[kind]=null;publish('status',kind);}
      }
    })();
    jobs[kind]={promise,controller};
    return promise;
  }
  function ensure() {
    if(!network||!options.autoUpdate)return;
    for(const kind of ['quote','poem'])if(options[kind+'Mode']==='api'&&!current(kind,cache[kind])&&attempted[kind]!==today())void refresh(kind);
  }
  window.YiyiReading={configure,read,refresh,ensure,parse,endpoints,snapshot:()=>copy(cache),subscribe(fn){listeners.add(fn);return()=>listeners.delete(fn);}};
})();

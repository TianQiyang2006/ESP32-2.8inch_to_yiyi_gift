(function(){
 'use strict';
 const clone=window.YiyiDesign.clone,fields=['letter','keepsake','clockMode','deviceSettings','daily','holiday','reading','colors','radius','countdown','weather'];
 const thumbnail=new URLSearchParams(location.search).has('thumbnail');
 const apiBase=window.YIYI_PHONE_BASE||'http://127.0.0.1:8766';
 let config={},last={},session=null,polling=false,syncTimer=null,networkProvider=null,netGeneration=0,sessionGeneration=0,resumed=false;
 const sessionKey='yiyi-phone-session:'+location.pathname;
 const listeners=new Set();
 let status={connected:false,pairing:false,hasLocation:false,url:'',qr:'',error:'',network:{source:'preview',status:'disconnected',ssid:'',ip:'',error:'',scanning:false,networks:[]}};
 const snapshot=()=>clone(status),emit=(type,extra={})=>listeners.forEach(fn=>fn({type,...extra,value:snapshot()}));
 const select=value=>Object.fromEntries(fields.filter(key=>Object.hasOwn(value,key)).map(key=>[key,clone(value[key])]));
 function remember(){try{if(session)sessionStorage.setItem(sessionKey,JSON.stringify({session,last,hasLocation:status.hasLocation,savedAt:Date.now()}));else sessionStorage.removeItem(sessionKey);}catch(error){/* Session restoration is optional when storage is unavailable. */}}
 async function request(path,data,activeSession=session){
  const result=await fetch(apiBase+path,{method:data?'POST':'GET',headers:{'Content-Type':'application/json',...(activeSession?{'X-Yiyi-Token':activeSession.token}:{})},...(data?{body:JSON.stringify(data)}:{}),signal:AbortSignal.timeout(20000)});
  const value=await result.json();if(!result.ok)throw Object.assign(new Error(value.error||'手机服务暂不可用'),{status:result.status});return value;
 }
 function configure(value){
  config=select(value);
  if(thumbnail)return;
  if(session&&JSON.stringify(config)!==JSON.stringify(last)){
   clearTimeout(syncTimer);syncTimer=setTimeout(async()=>{
    if(!session)return;const activeSession=session,generation=sessionGeneration,sent=clone(config),patch=Object.fromEntries(fields.filter(key=>JSON.stringify(sent[key])!==JSON.stringify(last[key])).map(key=>[key,sent[key]]));
    if(!Object.keys(patch).length)return;
    try{const result=await request('/api/device-sync',{patch,revision:activeSession.revision},activeSession);if(generation!==sessionGeneration)return;session.revision=result.revision;last=sent;remember();configure(config);}
    catch(error){if(generation!==sessionGeneration)return;if(error.status===409)void poll();else{status.error=error.message;emit('status');}}
   },600);
  }
 }
 function resume(){
  if(resumed||thumbnail)return;resumed=true;
  try{const saved=JSON.parse(sessionStorage.getItem(sessionKey)||'null');if(!saved?.session?.token||Date.now()-saved.savedAt>12*3600000)return;session=saved.session;last=select(saved.last||{});status.url=session.url;status.qr=apiBase+'/api/qr?token='+encodeURIComponent(session.token);status.hasLocation=!!saved.hasLocation;configure(config);void poll();}catch(error){/* A fresh pairing remains available. */}
 }
 async function pair(){
  if(thumbnail||status.pairing)return;
  const generation=++sessionGeneration;status.pairing=true;status.error='';emit('status');
  try{const value=await request('/api/pair',{config},null);if(generation!==sessionGeneration)return;session=value;last=clone(config);status.url=session.url;status.qr=apiBase+'/api/qr?token='+encodeURIComponent(session.token);status.connected=false;status.hasLocation=false;remember();}
  catch(error){if(generation===sessionGeneration)status.error='手机连接服务未就绪，请启动 design/phone_server.py 后重试。';}
  finally{if(generation===sessionGeneration){status.pairing=false;emit('status');}}
 }
 async function poll(){
  if(thumbnail||!session||polling)return;polling=true;const activeSession=session,generation=sessionGeneration;
  try{const result=await request('/api/device-state?since='+activeSession.revision,undefined,activeSession);if(generation!==sessionGeneration||result.revision<activeSession.revision)return;const changed=status.connected!==result.connected||status.hasLocation!==!!result.hasLocation;status.connected=result.connected;status.hasLocation=!!result.hasLocation;status.error='';
   if(result.config){const next=clone(result.config);for(const key of fields)if(JSON.stringify(config[key])!==JSON.stringify(last[key])&&JSON.stringify(result.config[key])===JSON.stringify(last[key]))next[key]=clone(config[key]);session.revision=result.revision;last=clone(result.config);config=next;emit('config',{patch:next});}
   remember();if(changed)emit('status');
  }catch(error){if(generation===sessionGeneration){status.connected=false;status.error=error.message;if(error.status===401){session=null;status.hasLocation=false;status.url='';status.qr='';remember();}emit('status');}}finally{polling=false;}
 }
 async function scan(){
  const generation=++netGeneration;status.network.scanning=true;status.network.error='';emit('network');
  try{const networks=networkProvider?await networkProvider.scan():[{ssid:'Yiyi-Home 2.4G',rssi:-42,secure:true},{ssid:'小黑的网络',rssi:-66,secure:true},{ssid:'Guest',rssi:-78,secure:false}];
   if(generation!==netGeneration)return;status.network.networks=networks.filter(item=>typeof item.ssid==='string'&&item.ssid).slice(0,30);status.network.source=networkProvider?'device':'preview';
  }catch(error){if(generation===netGeneration)status.network.error=error.message;}
  finally{if(generation===netGeneration){status.network.scanning=false;emit('network');}}
 }
 async function connect(ssid,password,secure){
  if(!ssid||new TextEncoder().encode(ssid).length>32)throw new Error('网络名需要为 1～32 字节');
  if((secure||password.length>0)&&(new TextEncoder().encode(password).length<8||new TextEncoder().encode(password).length>63))throw new Error('Wi-Fi 密码需要为 8～63 字节');
  if(status.network.status==='connecting')return;
  status.network.status='connecting';status.network.error='';emit('network');
  try{const result=networkProvider?await networkProvider.connect({ssid,password}):{ssid,ip:'192.168.1.88',source:'preview'};
   status.network={...status.network,...result,ssid,status:'connected',source:networkProvider?'device':'preview'};
  }catch(error){status.network.status='disconnected';status.network.error=error.message;}finally{emit('network');}
 }
 function disconnect(){const activeSession=session;sessionGeneration++;session=null;if(activeSession)void request('/api/disconnect',{},activeSession).catch(()=>{});clearTimeout(syncTimer);status.connected=false;status.pairing=false;status.hasLocation=false;status.url='';status.qr='';status.error='';remember();emit('status');}
 async function weatherProvider(){if(!session||!status.hasLocation)throw new Error('请先连接手机获取位置或选择地区。');const generation=sessionGeneration,result=await request('/api/weather-refresh',{});if(generation!==sessionGeneration)throw new Error('手机连接已变更，请重新刷新天气。');return result.weather;}
 async function refreshWeather(){if(!session||!status.hasLocation){status.error='请先进入设置“连接手机”，在手机上获取位置或选择地区。';emit('status');return false;}try{await window.YiyiData.refreshWeather();await poll();return true;}catch(error){status.error=error.message;emit('status');return false;}}
 window.YiyiDevice={configure,resume,pair,disconnect,poll,scan,connect,refreshWeather,weatherProvider,get:snapshot,subscribe(fn){listeners.add(fn);return()=>listeners.delete(fn);},setNetworkProvider(provider){networkProvider=provider;}};
 if(!thumbnail)setInterval(poll,2000);
})();

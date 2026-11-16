(function(){
 'use strict';
 const token=new URLSearchParams(location.search).get('token')||'',status=document.querySelector('#phone-status');
 let revision=-1,config={},active=false,writing=0,readGeneration=0;
 const show=(text,error=false)=>{status.textContent=text;status.classList.toggle('error',error);};
 function counters(){for(const key of ['letter','keepsake']){const input=document.forms[key]?.elements.text,output=document.querySelector('[data-counter="'+key+'"]');if(input&&output)output.textContent=input.value.length+' / 3000';}}
 async function api(path,data){
  const response=await fetch(path+(path.includes('?')?'&':'?')+'token='+encodeURIComponent(token),{method:data?'POST':'GET',headers:{'Content-Type':'application/json'},...(data?{body:JSON.stringify(data)}:{}),signal:AbortSignal.timeout(20000)});
  const value=await response.json();if(!response.ok)throw new Error(value.error||'连接失败');return value;
 }
 function hydrate(next){
  config=next;document.querySelector('#phone-panels').hidden=false;
  for(const [key,value]of Object.entries(next)){
   const form=document.getElementById(key);if(!form||!value)continue;
   for(const input of form.elements){if(!input.name||input===document.activeElement||form.dataset.dirty==='true')continue;const current=value[input.name];if(current===undefined)continue;if(input.type==='checkbox')input.checked=!!current;else if(input.type==='radio')input.checked=input.value===current;else input.value=current;}
  }
  for(const name of ['brightness','volume'])document.getElementById(name+'-value').textContent=document.forms.deviceSettings.elements[name].value+'%';
  counters();
  const cityInput=document.forms['city-search'].elements.city;if(document.forms['city-search'].dataset.dirty!=='true'&&cityInput!==document.activeElement)cityInput.value=next.weather?.city||'';
  document.querySelector('#weather-summary').textContent=next.weather?.city?(next.weather.city+' · '+(next.weather.condition||'暂无天气')+' · '+(next.weather.temperature??'—')+'°C'):'还未设置城市；输入中文城市名并保存后同步到桌面。';
 }
 async function poll(){if(writing)return;const generation=++readGeneration;try{const result=await api('/api/phone-state?since='+revision);if(generation!==readGeneration||writing)return;if(result.config){revision=result.revision;hydrate(result.config);}if(result.secureUrl&&!window.isSecureContext){document.querySelector('#secure-links').hidden=false;document.querySelector('#secure-link').href=result.secureUrl;document.querySelector('#certificate-link').href=result.certificateUrl;}if(!active){active=true;show('已连接 · 设置会同步到小黑桌面');}}catch(error){if(generation===readGeneration){active=false;show(error.message,true);}}}
 function draft(form){return form?JSON.stringify(Array.from(form.elements).filter(input=>input.name).map(input=>[input.name,input.type==='checkbox'||input.type==='radio'?input.checked:input.value])):'';}
 async function save(patch,form){const submitted=draft(form);writing++;readGeneration++;try{await api('/api/phone-save',{patch});if(form&&draft(form)===submitted)form.dataset.dirty='false';revision=-1;show('已保存，桌面会自动更新');}catch(error){show(error.message,true);}finally{writing--;await poll();}}
 document.querySelector('#phone-panels').addEventListener('input',event=>{const form=event.target.closest('form');if(form)form.dataset.dirty='true';counters();if(['brightness','volume'].includes(event.target.name))document.getElementById(event.target.name+'-value').textContent=event.target.value+'%';});
 for(const key of ['letter','keepsake','clockMode','deviceSettings','daily','holiday','reading','countdown','colors'])document.getElementById(key).addEventListener('submit',event=>{
  event.preventDefault();const form=event.currentTarget,values=Object.fromEntries(new FormData(form));
  for(const input of form.elements)if(input.type==='checkbox')values[input.name]=input.checked;
  if(key==='clockMode')values.seconds=true;
  for(const name of ['brightness','volume','waterGoal','weekday'])if(name in values)values[name]=Number(values[name]);
  void save({[key]:values},form);
 });
 document.querySelector('#clear-countdown').onclick=()=>save({countdown:null},document.forms.countdown);
 async function locate(latitude,longitude,city='',form=null){
  writing++;readGeneration++;show('正在获取这个位置的天气…');
  const draft=form?new FormData(form).get('city'):null;
  try{const result=await api('/api/location',{latitude,longitude,city});if(form&&new FormData(form).get('city')===draft)form.dataset.dirty='false';revision=-1;show('天气已更新：'+result.weather.city);}catch(error){show(error.message,true);}finally{writing--;await poll();}
 }
 document.querySelector('#locate').onclick=()=>{
  if(!window.isSecureContext){show('手机定位需要 HTTPS。请使用可信的 HTTPS 配置地址，或在下面搜索城市 / 填写坐标。',true);return;}
  if(!navigator.geolocation){show('此浏览器没有定位功能，请搜索地区。',true);return;}
  const button=document.querySelector('#locate');button.disabled=true;show('正在请求手机定位…');
  navigator.geolocation.getCurrentPosition(position=>{button.disabled=false;void locate(position.coords.latitude,position.coords.longitude);},error=>{button.disabled=false;show(error.code===1?'定位授权未开启，可重新授权或搜索地区。':error.code===3?'定位超时，请重试或搜索地区。':'暂时无法获取位置，请搜索地区。',true);},{enableHighAccuracy:true,timeout:15000,maximumAge:60000});
 };
 document.querySelector('#location-note').textContent=window.isSecureContext?'只有点击定位并授权后才读取手机位置。':'GPS 定位需可信 HTTPS，直接输入中文城市名即可更新天气。';
 document.forms.coordinates.onsubmit=event=>{event.preventDefault();const values=new FormData(event.currentTarget);void locate(Number(values.get('latitude')),Number(values.get('longitude')));};
 document.forms['city-search'].onsubmit=event=>{event.preventDefault();const form=event.currentTarget;void locate(undefined,undefined,String(new FormData(form).get('city')||'').trim(),form);};
 void poll();setInterval(poll,5000);
})();

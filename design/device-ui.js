(function(){
 'use strict';
 const device=window.YiyiDevice;
 let configuration=window.YiyiDesign.clone(window.YiyiDesign.defaults),artURL=value=>value,wifiDraft={ssid:'',password:'',secure:true},keyboard=false,keyboardMode='lower';
 const presets=[['landscape','时光山水','assets/clock-morning.jpg'],['matrix','蓝色像素','assets/clock-matrix-reference.jpg'],['flip','白色翻页','assets/clock-flip-reference.jpg']];
 const pixelDigits=['111101101101111','001001001001001','111001111100111','111001111001111','101101111001001','111100111001111','111100111101111','111001001001001','111101111101111','111101111001111'];
 const header=title=>'<div class="device-panel-nav"><button data-action="back">‹ 返回</button><b>'+title+'</b></div>';
 function landscapePeriod(hour){return hour<6?'predawn':hour<11?'morning':hour<16?'day':hour<18?'sunset':hour<21?'dusk':'night';}
 function matrix(value,small=false){
  const positions=small?[0,34]:[0,65,164,231],tile=esc(artURL('assets/clock-pixel-tile.png')),step=small?9:17,halo=small?3:6,size=small?13:26;
  const dot=(x,y,lit)=>'<image href="'+tile+'" x="'+(x-halo)+'" y="'+(y-halo)+'" width="'+size+'" height="'+size+'" opacity="'+(lit?'1':'0')+'"/>';
  const digits=value.replaceAll(':','').split('').map((digit,index)=>'<g data-clock-digit="'+index+'">'+Array.from({length:15},(_,cell)=>dot(positions[index]+cell%3*step,Math.floor(cell/3)*step,(pixelDigits[digit]||'000000111000000')[cell]==='1')).join('')+'</g>').join('');
  return '<svg viewBox="0 0 '+(small?'59 43':'279 82')+'" role="img" aria-label="'+value+'">'+digits+(small?'':'<g data-clock-colon>'+dot(131,17,true)+dot(131,51,true)+'</g>')+'</svg>';
 }
 function updateMatrix(element,value){
  const svg=element.querySelector('svg');if(svg.getAttribute('aria-label')===value)return;svg.setAttribute('aria-label',value);
  value.replaceAll(':','').split('').forEach((digit,index)=>svg.querySelectorAll('[data-clock-digit="'+index+'"] image').forEach((dot,cell)=>{const opacity=(pixelDigits[digit]||'000000111000000')[cell]==='1'?'1':'0';if(dot.getAttribute('opacity')!==opacity)dot.setAttribute('opacity',opacity);}));
 }
 function clockFace(){
  const time=window.YiyiData.time(),preset=configuration.clockMode.preset,hour=Number(time.clock.split(':')[0]),period=landscapePeriod(hour),clock=time.clock+':'+time.seconds;
  const body=preset==='matrix'?'<div class="matrix-time">'+matrix(time.clock)+'</div><div class="matrix-seconds" aria-label="秒数">'+matrix(time.seconds,true)+'</div>':preset==='flip'?'<div class="flip-labels"><span>HOUR</span><span>MIN</span><span>SEC</span></div><div class="flip-digits">'+clock.replaceAll(':','').split('').map((digit,index)=>'<span'+(index===2||index===4?' class="minute-start"':'')+'>'+digit+'</span>').join('')+'</div>':'<strong class="landscape-time">'+clock+'</strong>';
  return '<section class="clock-face clock-'+preset+'" data-clock-preset="'+preset+'" data-clock-period="'+period+'" data-clock-time="'+clock+'" data-clock-date="'+time.key+'" aria-label="全屏时钟，左右滑动返回预设菜单" style="'+(preset==='landscape'?'background-image:url('+esc(artURL('assets/clock-'+period+'.jpg'))+')':'')+'">'+body+(preset==='matrix'?'':'<time class="clock-date">'+time.year+' 年 '+time.month+' 月 '+time.day+' 日 周'+['日','一','二','三','四','五','六'][time.weekday]+'</time>')+'</section>';
 }
 function clockPicker(){return '<section class="device-panel">'+header('时钟模式')+'<div class="clock-picker">'+presets.map(([id,name,source])=>'<button data-action="clock-preset-'+id+'" class="'+(configuration.clockMode.preset===id?'current':'')+'"><img src="'+esc(artURL(source))+'" alt="'+name+'"><b>'+name+'</b>'+(id==='landscape'?'<small>背景随时间变化</small>':'')+'</button>').join('')+'</div><p class="device-hint">选择预设进入全屏钟面，左右滑动返回这里。</p></section>';}
 function phonePanel(){const value=device.get();return '<section class="device-panel">'+header('连接手机')+'<div class="phone-link-body">'+(value.url?'<img class="phone-qr" src="'+esc(value.qr)+'" alt="手机设置连接二维码"><div class="phone-steps"><b>'+ (value.connected?'手机已连接':'等待手机连接')+'</b><p>手机与电脑处于同一 Wi-Fi<br>扫码或打开下方地址</p><button data-action="phone-open">打开手机设置</button></div><a class="phone-url" href="'+esc(value.url)+'" target="_blank" rel="noopener">'+esc(value.url)+'</a><button class="device-secondary" data-action="phone-disconnect">结束连接</button>':'<p>在手机上修改留言、获取定位与详细天气，也可以调整全部应用设置。</p><button class="device-primary" data-action="phone-pair"'+(value.pairing?' disabled':'')+'>'+(value.pairing?'正在创建连接…':'创建手机连接')+'</button><p class="device-hint">实板使用设备热点或局域网配置地址。当前设计预览由手机连接服务同步。</p>')+'<p class="device-error" role="status">'+esc(value.error)+'</p></div></section>';}
 function keyboardMarkup(){const rows=keyboardMode==='symbols'?['1234567890','!@#$%^&*()','_+-=.,:;?','/[]{}<>']:['1234567890',keyboardMode==='upper'?'QWERTYUIOP':'qwertyuiop',keyboardMode==='upper'?'ASDFGHJKL':'asdfghjkl',keyboardMode==='upper'?'ZXCVBNM':'zxcvbnm'];return '<div class="wifi-keyboard">'+rows.map(row=>'<div>'+row.split('').map(char=>'<button data-key="'+esc(char)+'">'+esc(char)+'</button>').join('')+'</div>')+'<div><button data-key="shift">Aa</button><button data-key="symbols">123 / !?</button><button data-key="space">空格</button><button data-key="backspace">⌫</button><button data-key="done">完成</button></div></div>';}
 function wifiPanel(){const value=device.get().network;
  if(state.section==='wifi-password')return '<section class="device-panel wifi-entry">'+header('连接 Wi-Fi')+'<div class="wifi-fields"><label>网络名称<input id="device-ssid" value="'+esc(wifiDraft.ssid)+'" maxlength="32"></label><label>密码<input id="device-password" type="password" value="'+esc(wifiDraft.password)+'" autocomplete="new-password"></label><button class="device-primary" data-action="wifi-connect"'+(value.status==='connecting'?' disabled':'')+'>'+(value.status==='connecting'?'连接中…':'连接网络')+'</button><p class="device-error">'+esc(value.error)+'</p></div>'+(keyboard?keyboardMarkup():'')+'</section>';
  return '<section class="device-panel">'+header('Wi-Fi 配网')+'<div class="device-panel-body scroll-body"><p class="device-hint">'+(value.source==='preview'?'配网交互预览 · 网络列表不是电脑扫描结果':'ESP32 搜索到的 2.4GHz 网络')+'</p><p class="wifi-state">'+esc(value.status==='connected'?(value.source==='preview'?'演示连接：':'已连接：')+value.ssid:'尚未连接')+'</p><button class="device-primary" data-action="wifi-scan"'+(value.scanning?' disabled':'')+'>'+(value.scanning?'正在搜索…':'搜索 Wi-Fi')+'</button>'+value.networks.map((item,index)=>'<button class="wifi-network" data-action="wifi-select-'+index+'"><span>'+esc(item.ssid)+'</span><small>'+ (item.secure?'🔒 ':'')+esc(item.rssi)+' dBm ›</small></button>').join('')+'<button class="device-secondary" data-action="wifi-hidden">手动输入网络名称</button><p class="device-error">'+esc(value.error)+'</p></div></section>';
 }
 function settingValue(key,value){return value+'%'+(key==='volume'&&value===0?' · 静音':'');}
 function settingsPanel(){
  if(state.section==='phone')return phonePanel();if(['wifi','wifi-password'].includes(state.section))return wifiPanel();
  const controls=[['brightness','屏幕亮度',10],['volume','音量',0]].map(([key,label,min])=>{
   const value=configuration.deviceSettings[key];
   return '<label class="device-control" for="device-'+key+'"><span>'+label+'<output id="device-'+key+'-value" for="device-'+key+'">'+settingValue(key,value)+'</output></span><input id="device-'+key+'" data-device-setting="'+key+'" type="range" min="'+min+'" max="100" step="1" value="'+value+'" aria-valuetext="'+settingValue(key,value)+'"></label>';
  }).join('');
  return '<section class="device-panel">'+header('设置')+'<div class="device-panel-body device-settings-body scroll-body"><div class="device-controls">'+controls+'</div><button class="device-setting" data-action="setting-wifi"><span>Wi-Fi 配网</span><small>设备搜索并连接 ›</small></button><button class="device-setting" data-action="setting-phone"><span>连接手机</span><small>定位 · 全部设置 ›</small></button><button class="device-setting" data-action="setting-message"><span>留言设置</span><small>手机编辑留言与纪念 ›</small></button><p class="device-error" role="status">'+esc(device.get().error)+'</p></div></section>';
 }
 function message(){return window.YiyiScheduleUI?window.YiyiScheduleUI.markup():window.YiyiMessageUI.markup();}
 function configure(next,resolve){configuration=next;artURL=resolve;window.YiyiMessageUI.configure(next,resolve);if(window.YiyiScheduleUI)window.YiyiScheduleUI.configure(next,resolve);}
 function app(){return state.app==='clock'?(state.section==='clock-face'?clockFace():clockPicker()):settingsPanel();}
 function refresh(root){
  window.YiyiMessageUI.refresh(root);
  if(window.YiyiScheduleUI)window.YiyiScheduleUI.refresh(root);
  for(const input of root.querySelectorAll('[data-device-setting]')){const key=input.dataset.deviceSetting,value=configuration.deviceSettings[key];if(Number(input.value)!==value)input.value=value;input.setAttribute('value',value);root.querySelector('#device-'+key+'-value').textContent=settingValue(key,value);input.setAttribute('aria-valuetext',settingValue(key,value));}
  const settingsError=root.querySelector('.device-settings-body .device-error');if(settingsError)settingsError.textContent=device.get().error;
  const face=root.querySelector('.clock-face');if(face){
   const time=window.YiyiData.time(),clock=time.clock+':'+time.seconds,hour=Number(time.clock.split(':')[0]),period=landscapePeriod(hour),preset=face.dataset.clockPreset;
   if(face.dataset.clockTime!==clock){
    if(preset==='matrix'){updateMatrix(face.querySelector('.matrix-time'),time.clock);updateMatrix(face.querySelector('.matrix-seconds'),time.seconds);}
    else if(preset==='flip')clock.replaceAll(':','').split('').forEach((digit,index)=>{const card=face.querySelectorAll('.flip-digits>span')[index];if(card.textContent!==digit)card.textContent=digit;});
    else face.querySelector('.landscape-time').textContent=clock;
    face.dataset.clockTime=clock;
   }
   if(face.dataset.clockPeriod!==period){if(preset==='landscape')face.style.backgroundImage='url("'+artURL('assets/clock-'+period+'.jpg')+'")';face.dataset.clockPeriod=period;}
   if(face.dataset.clockDate!==time.key){const date=face.querySelector('.clock-date');if(date)date.textContent=time.year+' 年 '+time.month+' 月 '+time.day+' 日 周'+['日','一','二','三','四','五','六'][time.weekday];face.dataset.clockDate=time.key;}
  }
 }
 function handle(action,saveClock){
  if(window.YiyiScheduleUI&&window.YiyiScheduleUI.handle(action))return true;
  if(window.YiyiMessageUI.handle(action))return true;
  if(action==='connect-phone'||action==='set-region'||action==='phone-location'){state.app='settings';state.section='phone';render();resetIdle();return true;}
  if(action==='setting-wifi'){state.section='wifi';render();void device.scan();return true;}
  if(action==='setting-phone'||action==='setting-message'){state.section='phone';render();return true;}
  if(action==='phone-pair'){void device.pair();return true;}
  if(action==='phone-disconnect'){device.disconnect();return true;}
  if(action==='phone-open'){const url=device.get().url;if(url)window.open(url,'_blank','noopener');return true;}
  if(action==='wifi-scan'){void device.scan();return true;}
  if(action.startsWith('wifi-select-')||action==='wifi-hidden'){const entry=device.get().network.networks[Number(action.slice(12))];wifiDraft={ssid:entry?.ssid||'',password:'',secure:entry?.secure??false};keyboard=false;state.section='wifi-password';render();return true;}
  if(action==='wifi-connect'){wifiDraft.ssid=screen.querySelector('#device-ssid').value;wifiDraft.password=screen.querySelector('#device-password').value;void device.connect(wifiDraft.ssid,wifiDraft.password,wifiDraft.secure).catch(error=>{screen.querySelector('.device-error').textContent=error.message;});return true;}
  if(action.startsWith('clock-preset-')){saveClock({...configuration.clockMode,preset:action.slice(13)});state.section='clock-face';render();return true;}
  if(action==='clock-picker'){state.section=null;render();return true;}
  if(action==='clock-exit'){state.app=null;state.section=null;state.page=appsPage;render();return true;}
  return false;
 }
 screen.addEventListener('input',event=>{if(event.target.id==='device-password')wifiDraft.password=event.target.value;if(event.target.id==='device-ssid')wifiDraft.ssid=event.target.value;});
 screen.addEventListener('focusin',event=>{if(event.target.id==='device-password'&&!keyboard){keyboard=true;const wrapper=document.createElement('div');wrapper.innerHTML=keyboardMarkup();screen.querySelector('.wifi-entry').append(wrapper.firstElementChild);}});
 screen.addEventListener('click',event=>{const button=event.target.closest('[data-key]');if(!button)return;const key=button.dataset.key,input=screen.querySelector('#device-password');if(key==='shift'||key==='symbols'){keyboardMode=key==='symbols'?(keyboardMode==='symbols'?'lower':'symbols'):(keyboardMode==='upper'?'lower':'upper');screen.querySelector('.wifi-keyboard').outerHTML=keyboardMarkup();return;}if(key==='done'){keyboard=false;screen.querySelector('.wifi-keyboard')?.remove();return;}input.value=key==='backspace'?input.value.slice(0,-1):input.value+(key==='space'?' ':key);wifiDraft.password=input.value;});
 window.YiyiDeviceUI={configure,app,message,refresh,handle,landscapePeriod,matrix};
})();

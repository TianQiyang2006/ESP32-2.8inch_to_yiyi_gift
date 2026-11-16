(function () {
  'use strict';
  const model = window.YiyiDesign;
  const data = window.YiyiData;
  const thumbnail = new URLSearchParams(location.search).has('thumbnail');
  const standalone = !!window.YIYI_INITIAL_CONFIG;
  let config = model.clone(window.YIYI_INITIAL_CONFIG || model.defaults);
  let editing = !standalone && !thumbnail;
  let selected = null;
  let drag = null;
  let embeddedAssets = window.YIYI_INITIAL_ASSETS || {};
  const send = payload => { if (!thumbnail && !standalone) parent.postMessage({source:'yiyi-preview',...payload},'*'); };
  const artURL = source => embeddedAssets[source] || source;
  const originalRender = render;
  const originalCountdownParts = countdownParts;
  const originalDispatch = dispatch;
  const originalOpenApp = openApp;
  const originalNavigate = navigate;
  const originalCalendar = calendar;
  names[1]='天气';
  names[2]='放假倒计时';
  names[3]='名言与诗句';
  names.splice(4,0,'心情与日常','课程表');appsPage=names.length-1;
  openApp=function(name){if(name==='weather')setPage(1);else if(name==='letter')setPage(5);else if(name==='album')setPage(0);else originalOpenApp(name);};
  navigate=function(direction){if(!state.app&&!state.voice){state.section=null;state.messagePhoneReturn=null;}originalNavigate(direction);};
  let initialized = false, lastDate = null;
  const artMarkup = source => source.endsWith('.gif') && source.startsWith('../') ? '<span class="gif-viewport"><img class="gift-art" src="'+esc(artURL(source))+'" alt="原始小黑动图" draggable="false"></span>' : '<img class="gift-art" src="' + esc(artURL(source)) + '" alt="小黑陪伴素材" draggable="false">';
  cat = () => artMarkup(config.artwork);
  timeParts = () => ({clock:data.time().clock,date:data.time().short});
  chinaDateKey = () => data.time().key;
  countdownParts = (target,now=data.now().getTime()) => originalCountdownParts(target,now);
  function homeDate() { return data.time(); }
  function weekMarkup(date) {
    const items = [-1,0,1].map(offset=>{
      const value=new Date(Date.UTC(date.year,date.month-1,date.day+offset));
      return '<span class="'+(offset===0?'current':'')+'"><small>'+['日','一','二','三','四','五','六'][value.getUTCDay()]+'</small><b>'+value.getUTCDate()+'</b></span>';
    });
    return items.join('');
  }
  function monthMarkup(date) {
    const info=monthInfo(date.year,date.month-1);
    const weeks=Math.ceil((info.offset+info.days)/7);
    let cells='';
    for(let index=0;index<weeks*7;index++) {
      const day=index-info.offset+1;
      cells+='<span class="'+(day===date.day?'current':'')+'">'+(day>0 && day<=info.days?day:'')+'</span>';
    }
    return '<span class="mini-month-title">'+date.month+'月'+date.day+'日</span><span class="mini-week-header">'+['一','二','三','四','五','六','日'].map(day=>'<span>'+day+'</span>').join('')+'</span><span class="mini-month-grid" style="--mini-row-height:'+(weeks===6?7:8)+'px">'+cells+'</span>';
  }
  function weatherIcon(condition=data.getWeather().condition) {
    if (/雨|雪/.test(condition)) return '<svg viewBox="0 0 24 24" aria-hidden="true"><path d="M6 15a4 4 0 0 1-1-8 6 6 0 0 1 11-2 5 5 0 0 1 2 10Z"/><path d="m8 18-1 3m6-3-1 3m6-3-1 3" fill="none" stroke="currentColor" stroke-width="2"/></svg>';
    if (/云|阴/.test(condition)) return '<svg viewBox="0 0 24 24" aria-hidden="true"><path d="M6 19a5 5 0 0 1-1-10 7 7 0 0 1 13-2 6 6 0 0 1 0 12Z"/></svg>';
    return icon('weather');
  }
  const temperature = value => value===null ? '—' : String(value);
  function batteryMarkup() {
    const battery=data.getBattery();
    const percent=battery.present ? (battery.externalPower?'接电 · ':'')+(battery.percent===null?'--':'约'+battery.percent+'%') : battery.externalPower?'接电使用':'未接电池';
    const label=!battery.present?(battery.externalPower?'接电使用（设置），未安装电池':'未接入电池'):percent+'，'+(battery.low?'低电量，':'')+(battery.charging===true?'充电中（模拟）':battery.charging===false?'未充电（模拟）':'充电状态未知');
    return '<span class="battery-indicator'+(battery.low?' low':'')+(battery.charging===true&&battery.present?' charging':'')+'" role="img" title="'+esc(label)+'" aria-label="'+esc(label)+'"><svg viewBox="0 0 26 16" aria-hidden="true"><rect class="battery-body" x="1" y="3" width="20" height="10" rx="2"/><path class="battery-terminal" d="M23 6v4"/>'+(battery.present&&battery.percent!==null?'<rect class="battery-fill" x="3" y="5" width="'+(16*battery.percent/100)+'" height="6" rx=".5"/>':'<path class="battery-unknown" d="m7 6 7 4m0-4-7 4"/>')+(battery.charging===true&&battery.present?'<path class="battery-bolt" d="m13 0-6 8h5l-2 8 8-10h-6Z"/>':'')+'</svg><span class="battery-percent">'+esc(percent)+'</span></span>';
  }
  home = function () {
    const date=homeDate(), time={clock:date.clock}, content=data.getWeather();
    const [hours,minutes]=time.clock.split(':');
    const wifi = '<svg viewBox="0 0 20 20" aria-hidden="true"><path d="M2 7a12 12 0 0 1 16 0M5 10a7 7 0 0 1 10 0M8 13a3 3 0 0 1 4 0"/><circle cx="10" cy="16" r=".6"/>'+(state.wifi?'':'<path d="m14 12 4 5m0-5-4 5"/>')+'</svg>';
    return '<img class="postcard-backdrop" src="'+esc(artURL(config.postcard.backdrop))+'" alt="" draggable="false"><div class="ticket-sheet" aria-hidden="true"></div>'+
      '<div class="postcard-picture"><img src="'+esc(artURL(config.postcard.picture))+'" alt="春日邮票插画" draggable="false"></div>'+
      '<button class="clock" id="clock" data-action="home-calendar" aria-label="'+esc(time.clock)+'，打开日历"><span class="stamp-hours">'+esc(hours)+'</span><span class="clock-colon">:</span><span class="stamp-minutes">'+esc(minutes)+'</span></button>'+
      '<div class="date" id="date">'+date.short+'</div><div class="stamp-weekday">'+date.english+'</div>'+
      '<button class="weather-pill" data-action="app-weather" aria-label="'+esc((content.city||'未设置城市')+' '+content.condition+' '+temperature(content.temperature)+' 摄氏度，打开天气详情')+'"><img class="weather-art" src="'+esc(artURL(config.postcard.weatherPicture))+'" alt="" draggable="false"><span class="weather-primary">'+weatherIcon()+'<b>'+esc(temperature(content.temperature))+'<small>°C</small></b></span><span class="weather-description">'+esc(content.condition||'暂无天气')+'</span><span class="weather-note">'+esc(content.city||'未设置城市')+'</span></button>'+
      '<button class="week-card" data-action="home-countdown" aria-label="用今天的日期设置倒计时"><span class="week-mascot">'+artMarkup(config.postcard.mascot)+'</span><span class="week-dates">'+weekMarkup(date)+'</span></button>'+
      '<button class="home-calendar" data-action="home-calendar" aria-label="查看'+date.year+'年'+date.month+'月日历">'+monthMarkup(date)+'</button>'+
      '<span class="gift-wifi" aria-label="'+(state.wifi?'Wi-Fi 已连接':'Wi-Fi 未连接')+'">'+wifi+'Wi-Fi</span>'+
      batteryMarkup();
  };
  window.refreshHomeWidgets = function () {
    const clock=screen.querySelector('#clock');
    if (!clock?.querySelector('.stamp-hours')) return;
    const date=homeDate(), time={clock:date.clock}, [hours,minutes]=time.clock.split(':');
    clock.querySelector('.stamp-hours').textContent=hours;clock.querySelector('.stamp-minutes').textContent=minutes;
    clock.setAttribute('aria-label',time.clock+'，打开日历');
    screen.querySelector('#date').textContent=date.short;
    screen.querySelector('.stamp-weekday').textContent=date.english;
    if (screen.dataset.calendarDate!==date.key) {
      screen.querySelector('.week-dates').innerHTML=weekMarkup(date);
      screen.querySelector('.home-calendar').innerHTML=monthMarkup(date);
      screen.querySelector('.home-calendar').setAttribute('aria-label','查看'+date.year+'年'+date.month+'月日历');
      screen.dataset.calendarDate=date.key;
    }
  };
  weather = function () {
    if (state.section==='region') return pageTop('设置地区',true)+scroll('<label class="field">城市 / 区县<input id="region" maxlength="50" value="'+esc(state.draftRegion ?? data.getWeather().city)+'" placeholder="填写天气地区"></label><button class="mini-btn" data-action="save-region">保存地区</button><p class="hint" id="region-error" aria-live="polite"></p>');
    return window.YiyiWeatherUI.markup();
  };
  window.weatherPage=()=>state.section==='region'?'<div class="weather-settings">'+weather()+'</div>':window.YiyiWeatherUI.markup();
  function holidayEditor() {
    return '<div class="holiday-settings">'+pageTop('星期设置',true)+scroll('<label class="field">目标星期<select id="holiday-weekday">'+[1,2,3,4,5,6,0].map(day=>'<option value="'+day+'"'+(day===config.holiday.weekday?' selected':'')+'>周'+['日','一','二','三','四','五','六'][day]+'</option>').join('')+'</select></label><label class="field">日历文字<input id="holiday-label" maxlength="4" value="'+esc(config.holiday.label)+'" placeholder="放假 / 上班"></label><button class="mini-btn" data-action="save-holiday">保存</button><p class="hint">按每周这个星期计算；元旦与农历春节按北京时间日期计算，当天为 0 天。</p>')+'</div>';
  }
  window.holidayPage=()=>state.section==='holiday-settings'?holidayEditor():window.YiyiHolidayUI.markup(config.holiday,artURL);
  window.dailyPage=()=>window.YiyiDailyUI.markup(state.section,config.daily,artURL);
  window.readingPage=()=>window.YiyiReadingUI.markup(state.section);
  window.messagePage=()=>window.YiyiDeviceUI.message();
  function saveClock(value) {config=model.validate({...config,clockMode:value});window.YiyiDeviceUI.configure(config,artURL);window.YiyiDevice.configure(config);persistRuntime();send({type:'data',settings:{clockMode:config.clockMode}});}
  function saveDeviceSettings(patch) {
    config=model.validate({...config,deviceSettings:{...config.deviceSettings,...patch}});
    state.brightness=config.deviceSettings.brightness;state.volume=config.deviceSettings.volume;
    screen.style.filter='brightness('+state.brightness/100+')';
    window.YiyiDeviceUI.configure(config,artURL);window.YiyiDeviceUI.refresh(screen);window.YiyiDevice.configure(config);
    persistRuntime();send({type:'data',settings:{deviceSettings:config.deviceSettings}});
  }
  screen.addEventListener('input',event=>{
    const key=event.target.dataset.deviceSetting;
    if(!['brightness','volume'].includes(key))return;
    saveDeviceSettings({[key]:Number(event.target.value)});
  });
  window.YiyiDevice.subscribe(event=>{
    if(event.type==='config') {if(event.patch.weather)data.updateWeather(event.patch.weather,{origin:'phone'});applyConfig({...config,...event.patch});persistRuntime();send({type:'data',settings:event.patch});}
    if(event.type==='network') {state.wifi=event.value.network.source==='device'&&event.value.network.status==='connected';if(state.app==='settings'&&state.section==='wifi-password'&&event.value.network.status==='connected')state.section='wifi';}
    if(event.type!=='config' && state.app==='settings' && ['phone','wifi','wifi-password'].includes(state.section)) {
      // Preserve typed credentials while the asynchronous connection is progressing.
      if(state.section==='wifi-password') {const error=screen.querySelector('.device-error');if(error)error.textContent=event.value.network.error;const button=screen.querySelector('[data-action="wifi-connect"]');if(button){button.disabled=event.value.network.status==='connecting';button.textContent=button.disabled?'连接中…':'连接网络';}}
      else render();
    }
  });
  calendar=function(){
    if(state.section!=='countdown-view')return originalCalendar();
    return pageTop('自定义倒计时',true)+scroll(state.countdown?'<h2>'+esc(state.countdown.name)+'</h2><div id="countdown-readout">'+countdownReadout()+'</div><p class="hint">'+esc(state.countdown.date.replace('T',' '))+' · 北京时间</p><button class="mini-btn" data-action="edit-saved-countdown">修改倒计时</button>':'<p class="hint">还没有设置倒计时。</p><button class="mini-btn" data-action="edit-countdown">设置倒计时</button>');
  };
  window.YiyiReading.subscribe(event=>{
    window.YiyiReadingUI.refresh(screen,{content:true});
    if(event.type==='cache') {config.readingCache=event.cache;persistRuntime();send({type:'data',readingCache:event.cache});}
  });
  function saveDailyOptions(value) {
    config=model.validate({...config,daily:value});window.YiyiDaily.configure(config.daily,config.dailyRecords);
    persistRuntime();send({type:'data',daily:config.daily});
  }
  window.YiyiDaily.subscribe(event=>{
    config.dailyRecords=event.records;window.YiyiDailyUI.refresh(screen);window.YiyiMessageUI.refresh(screen);
    persistRuntime();send({type:'data',dailyRecords:event.records});
  });
  function persistRuntime() {
    if(standalone)try { localStorage.setItem('yiyi-gift-runtime-v1:'+config.name,JSON.stringify({weather:data.getWeather(),battery:data.getBattery(),holiday:config.holiday,daily:config.daily,dailyRecords:config.dailyRecords,readingCache:config.readingCache,countdown:config.countdown,letter:config.letter,keepsake:config.keepsake,clockMode:config.clockMode,deviceSettings:config.deviceSettings,reading:config.reading,colors:config.colors,radius:config.radius})); } catch(error) { notify('本地存储已满，请导出配置保存记录。'); }
  }
  dispatch = function (action) {
    if(!state.voice && window.YiyiDeviceUI.handle(action,saveClock)){resetIdle();return;}
    if(!state.voice && action==='album')return;
    if(!state.voice && window.YiyiReadingUI.handle(action))return;
    if(!state.voice && action==='view-countdown'){state.app='calendar';state.section='countdown-view';render();resetIdle();return;}
    if(!state.voice && action==='edit-saved-countdown'){
      state.draftCountdownName=state.countdown?.name||'';state.draftCountdownDate=state.countdown?.date||data.time().key+'T00:00';state.section='countdown';render();resetIdle();return;
    }
    if(!state.voice && ['save-countdown','clear-countdown'].includes(action)) {
      const previous=JSON.stringify(state.countdown);originalDispatch(action);
      if(previous!==JSON.stringify(state.countdown)) {config.countdown=state.countdown?{name:state.countdown.name,date:state.countdown.date}:null;persistRuntime();send({type:'data',countdown:config.countdown});}
      return;
    }
    if(!state.voice && window.YiyiDailyUI.handle(action,config.daily,saveDailyOptions))return;
    if(!state.voice && action.startsWith('page-')){setPage(action.slice(5));resetIdle();return;}
    if (!state.voice && action==='set-region') state.draftRegion=data.getWeather().city;
    if (!state.voice && action==='save-region') {
      try { data.setCity(document.getElementById('region').value);state.draftRegion=null;state.section=null;render();resetIdle(); }
      catch(error) { const message=screen.querySelector('#region-error');if(message)message.textContent=error.message;notify(error.message); }
      return;
    }
    if (!state.voice && action==='weather-refresh') {void window.YiyiDevice.refreshWeather().then(ok=>{if(!ok){state.app='settings';state.section='phone';render();}});resetIdle();return; }
    if (!state.voice && action==='holiday-settings') { state.section='holiday-settings';render();resetIdle();return; }
    if (!state.voice && action==='save-holiday') {
      config=model.validate({...config,holiday:{...config.holiday,weekday:Number(document.getElementById('holiday-weekday').value),label:document.getElementById('holiday-label').value}});
      state.section=null;render();resetIdle();persistRuntime();send({type:'data',holiday:config.holiday});return;
    }
    if (!state.voice && action.startsWith('holiday-date-')) {
      const value=window.YiyiHolidayCalendar.read(data.time(),config.holiday);
      const target=action==='holiday-date-newyear'?value.newYear:value.springFestival;
      if(!target.date){notify('春节日期支持 1900～2100 年。');return;}
      const [year,month]=target.date.split('-').map(Number);
      state.selectedDate=target.date;state.calendarYear=year;state.calendarMonth=month-1;openApp('calendar');return;
    }
    originalDispatch(action);
  };
  screen.addEventListener('input',event=>{if(event.target.id==='region')state.draftRegion=event.target.value;});
  function weatherExample() {
    const date=data.time(), high=[13,13,10,7,9,6,12],low=[6,8,4,2,2,3,4],conditions=['多云','阴','小雨','多云','小雨','阴','小雨'];
    data.updateWeather({city:data.getWeather().city,condition:'多云',temperature:12,feelsLike:9,humidity:55,wind:'东北风 2 级',high:12,low:5,sunrise:'07:12',sunset:'18:00',pressure:1021,uv:2.85,visibility:24.1,aqi:162,aqiLabel:'',source:'preview',updatedAt:data.now().getTime(),forecast:high.map((value,index)=>({date:new Date(Date.UTC(date.year,date.month-1,date.day+index+1)).toISOString().slice(0,10),condition:conditions[index],high:value,low:low[index]}))});
  }
  function applyStyles() {
    for (const key of ['bg','header','ink','accent','muted','surface']) document.documentElement.style.setProperty('--gift-'+key,config.colors[key]);
    document.documentElement.style.setProperty('--gift-radius',config.radius+'px');
    document.documentElement.style.setProperty('--photo-fit',config.photoFit);
    document.documentElement.style.setProperty('--postcard-clock-bg',config.home.clockColor);
    document.documentElement.style.setProperty('--daily-bg',config.daily.background);
    document.documentElement.style.setProperty('--daily-card',config.daily.cardColor);
    for(const [property,key] of [['bg','background'],['quote-card','quoteColor'],['quote-ink','quoteInk'],['poem-card','poemColor'],['poem-ink','poemInk']])document.documentElement.style.setProperty('--reading-'+property,config.reading[key]);
    window.YiyiDailyUI.refresh(screen);
    const dashboard=screen.querySelector('.holiday-dashboard');
    if(dashboard) {
      for(const [key,field] of [['light','numberColor'],['dark','unitColor'],['card','cardColor'],['ink','textColor']])dashboard.style.setProperty('--holiday-'+key,config.holiday[field]);
      screen.querySelector('.holiday-page').style.background=config.holiday.background;
    }
    screen.querySelector('.home-page')?.classList.add('postcard-home');
    screen.classList.toggle('editing',editing);
    screen.classList.toggle('with-background',!!config.background);
    screen.style.backgroundImage = config.background ? 'url("'+artURL(config.background)+'")' : '';
    screen.style.backgroundSize = 'cover';
    screen.style.backgroundPosition = 'center';
    if (state.app || state.voice || [3,4,5].includes(state.page) && state.section) return;
    for (const definition of model.layers[state.page]) {
      const element = screen.querySelector(definition.selector);
      if (!element) continue;
      const layout = config.layouts[state.page][definition.id];
      element.dataset.designLayer = definition.id;
      if (definition.id==='date' || definition.id==='weekday') element.style.pointerEvents=editing?'auto':'';
      Object.assign(element.style,{position:'absolute',left:layout.x+'px',top:layout.y+'px',right:'auto',bottom:'auto',width:layout.w+'px',height:layout.h+'px',fontSize:layout.size+'px',opacity:String(layout.opacity/100),margin:'0',color:layout.color || ''});
      element.classList.toggle('hidden-layer',!layout.visible);
      element.classList.toggle('selected-layer',selected === definition.id && editing);
      if (definition.id==='greeting') element.style.lineHeight=layout.h+'px';
      if (definition.id==='grid') element.querySelectorAll('.app').forEach(button=>{button.style.fontSize=layout.size+'px';});
      if (definition.id==='controls') element.querySelectorAll('.tiny').forEach(button=>{button.style.fontSize=layout.size+'px';});
      const gif = element.querySelector('.gif-viewport');
      if (gif && definition.id!=='week') { gif.style.width=Math.min(layout.w,layout.h)+'px';gif.style.height=Math.min(layout.w,layout.h)+'px';element.style.display=layout.visible?'grid':'none';element.style.placeItems='center'; }
    }
    window.YiyiReadingUI.fit(screen);
  }
  let previousView=null;
  render = function () {
    const nextView=[state.page,state.voice?'voice':state.app,state.section].join(':');
    const reader=screen.querySelector('.message-note-scroll,.message-detail .message-text'),previousReader=reader?{kind:reader.closest('.message-detail')?.dataset.messageKind || 'board',text:reader.textContent,top:reader.scrollTop,focused:reader===document.activeElement}:null;
    originalRender();
    applyStyles();
    const nextReader=screen.querySelector('.message-note-scroll,.message-detail .message-text');if(previousReader&&nextReader&&(nextReader.closest('.message-detail')?.dataset.messageKind || 'board')===previousReader.kind){if(previousReader.focused)nextReader.focus({preventScroll:true});if(nextReader.textContent===previousReader.text)nextReader.scrollTop=previousReader.top;}
    screen.classList.add('preview-ready');
    if(previousView!==null&&nextView!==previousView&&!editing&&!matchMedia('(prefers-reduced-motion: reduce)').matches){
      screen.firstElementChild.animate([{opacity:.75,transform:'translateY(5px)'},{opacity:1,transform:'translateY(0)'}],{duration:220,easing:'cubic-bezier(.22,.61,.36,1)'});
    }
    previousView=nextView;
    send({type:'view',page:state.page,app:state.voice?'voice':state.app,section:state.section});
    if(initialized && state.page===3 && !state.app && !state.voice)window.YiyiReading.ensure();
  };
  function applyConfig(next) {
    const previous=config;
    config = model.validate(next);
    if (!initialized && standalone) {
      try {
        const saved=JSON.parse(localStorage.getItem('yiyi-gift-runtime-v1:'+config.name)||'null');
        if(saved)config=model.validate({...config,...saved});
      } catch(error) { /* Exported design remains usable when storage is unavailable. */ }
    }
    if (!initialized) {state.designMode=!standalone;state.messageId='custom';state.draftMessageId='custom';}
    state.brightness=config.deviceSettings.brightness;state.volume=config.deviceSettings.volume;state.autoHome=config.deviceSettings.autoHome;
    if (!initialized || previous.home.wifi!==config.home.wifi) state.wifi=config.home.wifi;
    if (!initialized || JSON.stringify(previous.letter)!==JSON.stringify(config.letter)) state.messages=[{id:'custom',...config.letter},...initialMessages];
    if (!initialized || JSON.stringify(previous.countdown)!==JSON.stringify(config.countdown)) state.countdown=config.countdown?{name:config.countdown.name,date:config.countdown.date,target:parseCountdownDate(config.countdown.date)}:null;
    if (!initialized || JSON.stringify([previous.photos,previous.photoCaption])!==JSON.stringify([config.photos,config.photoCaption])) photos=config.photos.length ? config.photos.map(photo=>({...photo,url:artURL(photo.url),caption:config.photoCaption})) : demoPhotos().map(photo=>({...photo,caption:config.photoCaption}));
    window.YiyiDaily.configure(config.daily,config.dailyRecords);
    window.YiyiReading.configure(config.reading,config.readingCache,{enabled:!thumbnail});
    window.YiyiDeviceUI.configure(config,artURL);window.YiyiDevice.configure(config);
    data.configure(config);
    if (!initialized) { goToday();lastDate=homeDate(); }
    initialized=true;
    state.photo = Math.min(state.photo,photos.length-1);
    // Keep native range controls and pointer capture intact during phone/config updates.
    if(!state.voice && state.app==='settings' && !state.section && screen.querySelector('.device-controls')) {
      screen.style.filter='brightness('+state.brightness/100+')';window.YiyiDeviceUI.refresh(screen);applyStyles();
    } else render();
    resetIdle();
  }
  data.subscribe(event=>{
    if(!initialized)return;
    if (event.type==='clock') {
      const date=event.value,previous=lastDate;
      if (previous && previous.key!==date.key) {
        const followingToday=state.selectedDate===previous.key;
        if(followingToday)state.selectedDate=date.key;
        if(followingToday && state.calendarYear===previous.year && state.calendarMonth===previous.month-1) { state.calendarYear=date.year;state.calendarMonth=date.month-1; }
        if(state.app==='calendar' && !state.section && !state.voice)render();
      }
      lastDate=date;
      window.refreshHomeWidgets();
      window.YiyiWeatherUI.refresh(screen,'clock');
      window.YiyiHolidayUI.refresh(screen,config.holiday);
      if(previous?.key!==date.key)window.YiyiDailyUI.refresh(screen);
      window.YiyiReadingUI.refresh(screen);
      window.YiyiDeviceUI.refresh(screen);
      if(state.page===3 && !state.app && !state.voice)window.YiyiReading.ensure();
      const readout=screen.querySelector('#countdown-readout');
      if(readout && state.countdown)readout.innerHTML=countdownReadout();
      if(event.origin==='runtime')send({type:'data',epoch:data.now().getTime()});
      return;
    }
    if (event.type==='weather') {
      const value=event.value,widget=screen.querySelector('.weather-pill');
      if(widget) {
        widget.querySelector('.weather-primary').innerHTML=weatherIcon(value.condition)+'<b>'+esc(temperature(value.temperature))+'<small>°C</small></b>';
        widget.querySelector('.weather-description').textContent=value.condition||'暂无天气';
        widget.querySelector('.weather-note').textContent=value.city||'未设置城市';
        widget.setAttribute('aria-label',(value.city||'未设置城市')+' '+value.condition+' '+temperature(value.temperature)+' 摄氏度，打开天气详情');
      }
      window.YiyiWeatherUI.refresh(screen,'weather');
    }
    if (event.type==='battery') {
      const element=screen.querySelector('.battery-indicator');
      if(element) {
        const wrapper=document.createElement('div');wrapper.innerHTML=batteryMarkup();const current=wrapper.firstElementChild;
        element.innerHTML=current.innerHTML;
        element.classList.toggle('low',event.value.low);element.classList.toggle('charging',current.classList.contains('charging'));
        for(const key of ['title','aria-label'])element.setAttribute(key,current.getAttribute(key));
      }
    }
    if (!['config','peer','status'].includes(event.origin)) {
      send({type:'data',[event.type]:event.value});
      persistRuntime();
    }
  });
  function setPage(page) {
    state.messagePhoneReturn=null;
    state.voice = false;state.previous = null;state.app = null;state.section = null;state.page = Math.max(0,Math.min(names.length-1,Number(page)||0));state.slideshow=false;selected=null;
    render();
  }
  screen.addEventListener('click',event => {
    if (editing) { event.stopImmediatePropagation();event.preventDefault();return; }
    if (event.target.closest('[data-action="home-calendar"],[data-action="home-countdown"]')) {
      event.stopImmediatePropagation();
      const date=homeDate();state.selectedDate=date.key;state.calendarYear=date.year;state.calendarMonth=date.month-1;
      if (event.target.closest('[data-action="home-countdown"]')) { state.app='calendar';state.section=null;dispatch('edit-countdown'); } else openApp('calendar');
    }
  },true);
  screen.addEventListener('pointerdown',event => {
    if (!editing || state.app || state.voice || event.button !== 0) return;
    event.stopImmediatePropagation();event.preventDefault();
    const element = event.target.closest('[data-design-layer]');
    selected = element?.dataset.designLayer || null;
    screen.querySelectorAll('[data-design-layer]').forEach(item => item.classList.toggle('selected-layer',item === element));
    send({type:'selection',id:selected});
    if (!element) return;
    const layout = config.layouts[state.page][selected];
    drag = {element,id:selected,startX:event.clientX,startY:event.clientY,x:layout.x,y:layout.y};
    element.setPointerCapture(event.pointerId);
  },true);
  screen.addEventListener('pointermove',event => {
    if (!drag) return;
    const layout = config.layouts[state.page][drag.id];
    const scaleX = 320/screen.getBoundingClientRect().width,scaleY = 240/screen.getBoundingClientRect().height;
    layout.x = Math.round(Math.max(0,Math.min(320-layout.w,drag.x+(event.clientX-drag.startX)*scaleX)));
    layout.y = Math.round(Math.max(0,Math.min(240-layout.h,drag.y+(event.clientY-drag.startY)*scaleY)));
    drag.element.style.left = layout.x+'px';drag.element.style.top = layout.y+'px';
    send({type:'dragging',id:drag.id,x:layout.x,y:layout.y});
  });
  screen.addEventListener('pointerup',event => {
    if (!drag) return;
    event.stopImmediatePropagation();
    send({type:'layout',id:drag.id,page:state.page,layout:config.layouts[state.page][drag.id]});
    drag = null;pointerStart = null;
  },true);
  screen.addEventListener('pointercancel',() => { drag = null; });
  async function snapshot(request,sourceCSS) {
    try {
      const copy = screen.cloneNode(true);
      copy.classList.remove('editing');copy.querySelectorAll('.selected-layer').forEach(item => item.classList.remove('selected-layer'));
      const originals = [...screen.querySelectorAll('img')];
      const copies = [...copy.querySelectorAll('img')];
      for (let index=0;index<originals.length;index++) {
        const image = originals[index];
        if (!image.complete || !image.naturalWidth) await image.decode();
        const canvas = document.createElement('canvas');canvas.width=image.naturalWidth;canvas.height=image.naturalHeight;
        canvas.getContext('2d').drawImage(image,0,0);copies[index].src=canvas.toDataURL('image/png');
      }
      const svgAssets=new Map();
      for(const tile of copy.querySelectorAll('svg image')){
        const source=tile.getAttribute('href');if(!source||source.startsWith('data:'))continue;
        if(!svgAssets.has(source)){
          const image=new Image();image.src=source;await image.decode();
          const canvas=document.createElement('canvas');canvas.width=image.naturalWidth;canvas.height=image.naturalHeight;
          canvas.getContext('2d').drawImage(image,0,0);svgAssets.set(source,canvas.toDataURL('image/png'));
        }
        tile.setAttribute('href',svgAssets.get(source));
      }
      copy.style.filter='none';
      const css = sourceCSS+'\n:root{'+['bg','header','ink','accent','muted','surface'].map(key=>'--gift-'+key+':'+config.colors[key]).join(';')+';--gift-radius:'+config.radius+'px;--photo-fit:'+config.photoFit+';--postcard-clock-bg:'+config.home.clockColor+'}';
      send({type:'snapshot',request,html:new XMLSerializer().serializeToString(copy),css});
    } catch(error) { send({type:'error',request,message:'图片尚未载入，请稍后重试。'+error.message}); }
  }
  window.addEventListener('message',event => {
    if (event.source !== parent || event.data?.source !== 'yiyi-designer') return;
    const message = event.data;
    try {
      if (message.type === 'config') { embeddedAssets=message.assets || {};applyConfig(message.config);window.YiyiDevice.resume(); }
      if (message.type === 'page') setPage(message.page);
      if (message.type === 'select') { selected=message.id;applyStyles(); }
      if (message.type === 'mode') { editing=message.editing;selected=null;applyStyles(); }
      if (message.type === 'data') {
        if(message.settings)applyConfig({...config,...message.settings});
        if(message.weather)data.updateWeather(message.weather,{origin:'peer'});
        if(message.battery)data.updateBattery(message.battery,{origin:'peer'});
        if(Number.isFinite(message.epoch))data.syncTime(message.epoch);
        if(message.holiday || message.daily || message.dailyRecords || message.readingCache || Object.hasOwn(message,'countdown') && message.countdown!==undefined)applyConfig({...config,...(message.holiday?{holiday:message.holiday}:{}),...(message.daily?{daily:message.daily}:{}),...(message.dailyRecords?{dailyRecords:message.dailyRecords}:{}),...(message.readingCache?{readingCache:message.readingCache}:{}),...(message.countdown!==undefined?{countdown:message.countdown}:{})});
      }
      if (message.type === 'action') {
        if (message.action==='voice') startVoice();else if(message.action==='weather')openApp('weather');else if(message.action==='weather-region'){openApp('weather');dispatch('set-region');}else if(message.action==='weather-example')weatherExample();else if(message.action==='home')returnHome();else if(message.action==='next')navigate(1);else if(message.action==='prev')navigate(-1);
      }
      if (message.type === 'snapshot') snapshot(message.request,message.css);
    } catch(error) {send({type:'error',message:error.message});}
  });
  applyConfig(config);
  if(standalone)window.YiyiDevice.resume();
  if(!thumbnail)data.setWeatherProvider(window.YiyiDevice.weatherProvider);
  if (thumbnail) setPage(new URLSearchParams(location.search).get('page'));
  if (standalone) setPage(window.YIYI_INITIAL_PAGE || 0);
  if(standalone && window.YIYI_INITIAL_APP==='clock'){openApp('clock');state.section=window.YIYI_INITIAL_SECTION==='clock-face'?'clock-face':null;render();}
  send({type:'ready'});
})();

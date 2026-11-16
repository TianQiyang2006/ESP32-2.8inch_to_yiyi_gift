(function () {
  'use strict';
  // The weather app writes here; all widgets read the same state.
  const model = window.YiyiDesign;
  const copy = model.clone;
  const listeners = new Set();
  let weather = copy(model.defaults.weather), battery = copy(model.defaults.battery);
  let clock = copy(model.defaults.home), configured = {}, syncedTime = null;
  let provider = null, requestId = 0, pending = false, error = '', lastRefresh = 0;
  const same = (a,b) => JSON.stringify(a) === JSON.stringify(b);
  const normalized = (key,value) => model.validate({...model.defaults,[key]:value})[key];
  function now() {
    if (!clock.liveClock) return new Date(clock.calendarDate+'T'+clock.time+':00+08:00');
    return syncedTime ? new Date(syncedTime.epoch+performance.now()-syncedTime.at) : new Date();
  }
  function time(instant=now()) {
    const parts = new Intl.DateTimeFormat('en-CA',{timeZone:'Asia/Shanghai',year:'numeric',month:'2-digit',day:'2-digit',hour:'2-digit',minute:'2-digit',second:'2-digit',hourCycle:'h23'}).formatToParts(instant);
    const part = type => parts.find(value=>value.type===type).value;
    const year=Number(part('year')),month=Number(part('month')),day=Number(part('day'));
    const weekday = new Date(Date.UTC(year,month-1,day)).getUTCDay();
    return {key:part('year')+'-'+part('month')+'-'+part('day'),clock:part('hour')+':'+part('minute'),seconds:part('second'),year,month,day,weekday,
      short:part('month')+'/'+part('day')+' 星期'+['日','一','二','三','四','五','六'][weekday],
      english:['Sunday','Monday','Tuesday','Wednesday','Thursday','Friday','Saturday'][weekday]};
  }
  const getWeather = () => copy({...weather,pending,error});
  const getBattery = () => copy({...battery,low:battery.present && !battery.externalPower && battery.percent!==null && battery.percent<=20});
  function emit(type,origin='runtime') {
    const value = type==='clock' ? time() : type==='weather' ? getWeather() : getBattery();
    listeners.forEach(listener=>listener({type,value:copy(value),origin}));
  }
  function updateWeather(patch,options={}) {
    if (!patch || typeof patch!=='object') throw new Error('天气更新需要数据对象。');
    const cityChanged = typeof patch.city==='string' && patch.city.trim()!==weather.city;
    let next = {...weather};
    if (cityChanged) {
      // Never display the previous city's observations under a new city name.
      next = {...model.defaults.weather,city:patch.city,condition:'',temperature:null};
      requestId++;pending=false;error='';lastRefresh=0;
    }
    if(Object.hasOwn(patch,'aqi') && !Object.hasOwn(patch,'aqiLabel'))next.aqiLabel='';
    next = normalized('weather',{...next,...patch});
    const superseded = pending && !['config','peer'].includes(options.origin) && ['temperature','condition','updatedAt'].some(key=>Object.hasOwn(patch,key));
    if(superseded){requestId++;pending=false;error='';}
    const clearError=!!error && (options.origin==='phone' || patch.source==='preview');
    if(clearError)error='';
    if (same(next,weather) && !cityChanged && !superseded && !clearError) return;
    weather=next;emit('weather',options.origin);
  }
  function updateBattery(patch,options={}) {
    if (!patch || typeof patch!=='object') throw new Error('电池更新需要数据对象。');
    const next=normalized('battery',{...battery,...patch});
    if (same(next,battery)) return;
    battery=next;emit('battery',options.origin);
  }
  function configure(config) {
    for (const key of ['weather','battery']) {
      if (same(configured[key],config[key])) continue;
      configured[key]=copy(config[key]);
      if (key==='weather') updateWeather(config[key],{origin:'config'});
      else updateBattery(config[key],{origin:'config'});
    }
    const next={liveClock:config.home.liveClock,time:config.home.time,calendarDate:config.home.calendarDate};
    if (!same(configured.clock,next)) { configured.clock=copy(next);clock=next;emit('clock','config'); }
  }
  async function refreshWeather() {
    if (!weather.city) { error='请先设置城市';emit('weather');return false; }
    if (!provider) { error='等待天气 App 接入数据服务';emit('weather');return false; }
    if (pending) return false;
    const id=++requestId,city=weather.city;
    pending=true;error='';lastRefresh=Date.now();emit('weather','status');
    try {
      const result=await provider({city});
      if (id!==requestId || city!==weather.city) return false;
      if (!result || typeof result!=='object' || !Number.isFinite(result.temperature) || result.temperature<-99 || result.temperature>99 || typeof result.condition!=='string' || !result.condition.trim()) throw new Error('天气服务未返回有效的温度和天气状态');
      if (result.city!==undefined && result.city!==city) throw new Error('天气服务返回的城市与请求不一致');
      pending=false;error='';
      const next=normalized('weather',{...model.defaults.weather,...result,city,source:'device',updatedAt:result.updatedAt ?? Date.now()});
      weather=next;emit('weather','provider');return true;
    } catch (cause) {
      if (id!==requestId || city!==weather.city) return false;
      pending=false;error=String(cause.message||cause);emit('weather','status');return false;
    }
  }
  function setWeatherProvider(handler) {
    if (handler!==null && typeof handler!=='function') throw new Error('天气数据服务需要函数。');
    requestId++;pending=false;provider=handler;lastRefresh=0;
    if (provider && weather.city) void refreshWeather();
  }
  function setCity(city) {
    if (typeof city!=='string' || !city.trim()) throw new Error('请填写城市或区县。');
    updateWeather({city:city.trim().slice(0,50)});
    if (provider) void refreshWeather();
  }
  function syncTime(epoch) {
    if (!Number.isFinite(epoch) || epoch<0 || epoch>8640000000000000) throw new Error('时间需要有效的 Unix 毫秒时间戳。');
    syncedTime={epoch,at:performance.now()};emit('clock');
  }
  function tick() {
    emit('clock','tick');
    if (provider && weather.city && !pending && Date.now()-lastRefresh>=600000) void refreshWeather();
  }
  window.YiyiData={configure,now,time,getWeather,getBattery,updateWeather,updateBattery,setCity,setWeatherProvider,refreshWeather,syncTime,
    subscribe(listener){listeners.add(listener);return()=>listeners.delete(listener);}};
  setInterval(tick,1000);
  document.addEventListener('visibilitychange',()=>{if(!document.hidden)tick();});
})();

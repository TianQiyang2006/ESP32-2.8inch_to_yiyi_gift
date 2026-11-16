(function () {
  'use strict';
  const data=window.YiyiData;
  const text=value=>esc(value);
  const reading=value=>value===null || value===undefined ? '—' : String(value);
  const svg=body=>'<svg class="weather-symbol" viewBox="0 0 64 64" aria-hidden="true">'+body+'</svg>';
  const sun='<circle cx="43" cy="21" r="13" fill="#efc74e"/><g stroke="#d4ae37" stroke-width="2" stroke-linecap="round"><path d="M43 2v3m0 32v3M24 21h3m32 0h3M29 7l3 3m23 23 3 3M29 35l3-3M54 10l3-3"/></g>';
  const cloud='<path d="M15 49a12 12 0 0 1-1-24 17 17 0 0 1 31-3 13.5 13.5 0 1 1 3 27Z" fill="#d3dde0" stroke="#b7c7cd" stroke-width="1.2"/>';
  function symbol(condition) {
    if(!condition)return svg('<path d="M19 37a12 12 0 0 1-1-24 17 17 0 0 1 31-3 13 13 0 0 1 1 27Z" fill="#dce2df"/><path d="M31 23c0-5 9-5 9 0 0 4-5 3-5 7m0 5v1" fill="none" stroke="#84988d" stroke-width="2.5" stroke-linecap="round"/>');
    if(/雷/.test(condition))return svg(cloud+'<path d="m32 38-8 13h8l-3 11 13-17h-9l4-7Z" fill="#d6ac31"/>');
    if(/雪/.test(condition))return svg(cloud+'<g stroke="#74a6c0" stroke-width="1.5"><path d="M17 54v8m-4-6 8 4m0-4-8 4M36 52v8m-4-6 8 4m0-4-8 4M53 54v8m-4-6 8 4m0-4-8 4"/></g>');
    if(/雨/.test(condition))return svg(cloud+'<g stroke="#6596b5" stroke-width="2.5" stroke-linecap="round"><path d="m18 54-3 6m18-6-3 6m18-6-3 6"/></g>');
    if(/云/.test(condition))return svg(sun+cloud);
    if(/阴|雾|霾/.test(condition))return svg(cloud+(/雾|霾/.test(condition)?'<path d="M10 54h45M17 59h32" stroke="#adbaae" stroke-width="2" stroke-linecap="round"/>':''));
    if(/风/.test(condition))return svg('<g fill="none" stroke="#a2b8bc" stroke-width="3" stroke-linecap="round"><path d="M9 23h37c14 0 10-17 2-12M14 33h37c13 0 10 17 1 12M9 43h23"/></g>');
    return svg('<g transform="translate(-11 10)">'+sun+'</g>');
  }
  function metricIcon(name) {
    if(name==='sunrise' || name==='sunset')return svg('<path d="M8 44h48" stroke="#b7bdab" stroke-width="2"/><path d="M17 42a15 15 0 0 1 30 0" fill="#efc74e"/><path d="M32 10v17m-5-'+(name==='sunrise'?'12 5-5 5 5':'5 5 5 5-5')+'" fill="none" stroke="#c69d46" stroke-width="3" stroke-linecap="round"/>');
    if(name==='pressure')return svg('<circle cx="32" cy="34" r="21" fill="none" stroke="#96aaad" stroke-width="3"/><path d="M18 25v-6m14 3v-6m14 9v-6M32 34l12-10" stroke="#829799" stroke-width="2.5" stroke-linecap="round"/><circle cx="32" cy="34" r="3" fill="#829799"/>');
    if(name==='uv')return svg('<circle cx="32" cy="32" r="22" fill="#efd770"/><circle cx="32" cy="32" r="16" fill="#e9b851"/><text x="32" y="37" fill="#81662b" text-anchor="middle" font-size="15" font-family="Segoe UI,sans-serif" font-weight="700">UV</text>');
    if(name==='visibility')return svg('<path d="M5 32s10-18 27-18 27 18 27 18-10 18-27 18S5 32 5 32Z" fill="none" stroke="#719aa8" stroke-width="3"/><circle cx="32" cy="32" r="9" fill="#93b4c0"/><circle cx="32" cy="32" r="4" fill="#628b9c"/>');
    return svg('<path d="M10 43a22 22 0 0 1 10-26" fill="none" stroke="#80a47f" stroke-width="8"/><path d="M20 17a22 22 0 0 1 24 0" fill="none" stroke="#e2c664" stroke-width="8"/><path d="M44 17a22 22 0 0 1 10 26" fill="none" stroke="#c98363" stroke-width="8"/><path d="m32 40 14-13" stroke="#606d63" stroke-width="3" stroke-linecap="round"/>');
  }
  function quality(value) {
    if(value.aqiLabel)return value.aqiLabel;
    if(value.aqi===null)return '';
    return value.aqi<=50?'优':value.aqi<=100?'良':value.aqi<=150?'轻度污染':value.aqi<=200?'中度污染':value.aqi<=300?'重度污染':'严重污染';
  }
  function current(value) {
    return '<div class="weather-main-icon" title="'+text(value.condition||'暂无天气')+'">'+symbol(value.condition)+'</div><div class="weather-hero"><b>'+text(reading(value.temperature))+'<small>°C</small></b><span class="sub">'+text(value.condition||'暂无天气')+'</span></div><span class="weather-feels">体感温度 '+text(reading(value.feelsLike))+'°</span><span class="weather-range">'+text(reading(value.high))+'° / '+text(reading(value.low))+'°</span><span class="weather-extra" title="'+text(value.wind)+'">湿度 '+text(reading(value.humidity))+'% · '+text(value.wind||'风况暂无')+'</span>';
  }
  function metrics(value) {
    return [
      ['sunrise','日出',value.sunrise||'--:--',''],['sunset','日落',value.sunset||'--:--',''],
      ['pressure','气压',reading(value.pressure),'hPa'],['uv','紫外线',reading(value.uv),''],
      ['visibility','能见度',reading(value.visibility),'km'],['aqi','空气质量',reading(value.aqi),quality(value)]
    ].map(([id,label,number,unit])=>'<div class="weather-metric" data-metric="'+id+'">'+metricIcon(id)+'<div><span class="metric-label">'+label+'</span><span class="metric-value">'+text(number)+'<small>'+text(unit)+'</small></span></div></div>').join('');
  }
  function forecasts(value,date) {
    return Array.from({length:7},(_,index)=>{
      const item=value.forecast[index]||{};
      const hasReading=!!item.condition || item.high!==null&&item.high!==undefined || item.low!==null&&item.low!==undefined;
      const base=hasReading && value.updatedAt!==null ? data.time(new Date(value.updatedAt)) : date;
      const relative={今天:0,明天:1,后天:2};
      const offset=Object.hasOwn(relative,item.label)?relative[item.label]:index+1;
      const fallback=new Date(Date.UTC(base.year,base.month-1,base.day+offset));
      const actual=item.date?new Date(item.date+'T00:00:00Z'):fallback;
      const label=item.date || !item.label || Object.hasOwn(relative,item.label) ? '周'+['日','一','二','三','四','五','六'][actual.getUTCDay()] : item.label;
      return '<div class="forecast-card" role="listitem" title="'+text((item.date||'')+' '+(item.condition||'暂无预报'))+'"><span class="forecast-weekday">'+text(label)+'</span>'+symbol(item.condition)+'<span class="forecast-temperatures">'+text(reading(item.high))+'° / '+text(reading(item.low))+'°</span></div>';
    }).join('');
  }
  function dateText(date){return String(date.month).padStart(2,'0')+'月'+String(date.day).padStart(2,'0')+'日 星期'+['日','一','二','三','四','五','六'][date.weekday];}
  function updateText(value){
    if(value.pending)return '正在更新…';
    if(value.updatedAt===null)return '上次更新 —';
    return '上次更新 '+new Intl.DateTimeFormat('zh-CN',{timeZone:'Asia/Shanghai',hour:'2-digit',minute:'2-digit',hourCycle:'h23'}).format(new Date(value.updatedAt));
  }
  function statusText(value){
    if(value.pending)return '天气更新中';
    if(value.error)return value.error;
    const stamp=value.updatedAt===null?'等待天气数据':'更新于 '+new Intl.DateTimeFormat('zh-CN',{timeZone:'Asia/Shanghai',month:'2-digit',day:'2-digit',hour:'2-digit',minute:'2-digit',hourCycle:'h23'}).format(new Date(value.updatedAt));
    return (value.source==='preview'?'预览数据 · ':'')+stamp;
  }
  function toolbar(value){
    const status=statusText(value);
    return '<span class="weather-controls"><button data-action="set-region" aria-label="设置天气城市">地区</button><button data-action="weather-refresh"'+(value.pending?' disabled':'')+'>刷新</button></span><span class="weather-status" title="'+text(status)+'" aria-live="polite">'+text(status)+'</span>';
  }
  function markup(){
    const value=data.getWeather(),date=data.time();
    return '<section class="weather-dashboard" aria-label="天气概况"><button class="weather-city" data-action="set-region" title="'+text(value.city||'设置天气城市')+'">'+text(value.city||'未设置城市')+'</button><button class="weather-date" data-action="home-calendar">'+dateText(date)+'</button><span class="weather-updated">'+updateText(value)+'</span><div class="weather-current">'+current(value)+'</div><div class="weather-metrics weather-info">'+metrics(value)+'</div><div class="weather-seven-day" role="list" aria-label="七天预报" data-weather-date="'+date.key+'">'+forecasts(value,date)+'</div><div class="weather-toolbar">'+toolbar(value)+'</div></section>';
  }
  function refresh(root,type='weather'){
    const dashboard=root.querySelector('.weather-dashboard');if(!dashboard)return;
    const value=data.getWeather(),date=data.time();
    const dateNode=dashboard.querySelector('.weather-date');if(dateNode.textContent!==dateText(date))dateNode.textContent=dateText(date);
    const forecast=dashboard.querySelector('.weather-seven-day');
    if(type==='weather' || forecast.dataset.weatherDate!==date.key){forecast.innerHTML=forecasts(value,date);forecast.dataset.weatherDate=date.key;}
    if(type!=='weather')return;
    const city=dashboard.querySelector('.weather-city');city.textContent=value.city||'未设置城市';city.title=value.city||'设置天气城市';
    dashboard.querySelector('.weather-updated').textContent=updateText(value);
    dashboard.querySelector('.weather-current').innerHTML=current(value);
    dashboard.querySelector('.weather-metrics').innerHTML=metrics(value);
    dashboard.querySelector('.weather-toolbar').innerHTML=toolbar(value);
  }
  window.YiyiWeatherUI={markup,refresh};
})();

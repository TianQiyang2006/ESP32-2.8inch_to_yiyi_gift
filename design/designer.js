(function () {
  'use strict';
  const model = window.YiyiDesign;
  const $ = id => document.getElementById(id);
  const escape = value => String(value ?? '').replace(/[&<>"']/g,char => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[char]));
  const STORAGE = 'yiyi-gift-design-v1';
  const svgPaths = {
    upload:'<path d="M12 16V3m-5 5 5-5 5 5M4 16v4h16v-4"/>',download:'<path d="M12 3v13m-5-5 5 5 5-5M4 17v4h16v-4"/>',chevron:'<path d="m7 10 5 5 5-5"/>',
    home:'<path d="m3 10 9-7 9 7v11h-6v-7H9v7H3Z"/>',weather:'<path d="M7 19a5 5 0 0 1 0-10 6 6 0 0 1 11-2 5 5 0 0 1 0 12Z"/><path d="M18 2v2m4 3-2 1M14 3l1 1"/>',image:'<rect x="3" y="4" width="18" height="16" rx="2"/><circle cx="8" cy="9" r="1.5"/><path d="m4 18 6-6 4 4 3-3 4 5"/>',letter:'<rect x="3" y="5" width="18" height="14" rx="2"/><path d="m4 7 8 6 8-6"/>',calendar:'<rect x="3" y="5" width="18" height="16" rx="2"/><path d="M7 3v4m10-4v4M3 10h18M7 14h2m3 0h2m3 0h1M7 17h2m3 0h2"/>',
    grid:'<rect x="3" y="3" width="7" height="7" rx="1"/><rect x="14" y="3" width="7" height="7" rx="1"/><rect x="3" y="14" width="7" height="7" rx="1"/><rect x="14" y="14" width="7" height="7" rx="1"/>',clock:'<circle cx="12" cy="12" r="9"/><path d="M12 7v5l3 2"/>',text:'<path d="M5 4h14M12 4v16M8 20h8"/>',layers:'<path d="m12 3 10 6-10 6L2 9Zm-10 12 10 6 10-6M2 15l10 6 10-6"/>',cursor:'<path d="m5 3 14 9-7 1-3 7Z"/>',play:'<path d="m8 4 12 8-12 8Z"/>',undo:'<path d="M9 5 4 10l5 5M5 10h9a6 6 0 0 1 0 12"/>',redo:'<path d="m15 5 5 5-5 5M19 10h-9a6 6 0 0 0 0 12"/>',
    'arrow-left':'<path d="M20 12H4m6-6-6 6 6 6"/>','arrow-right':'<path d="M4 12h16m-6-6 6 6-6 6"/>',sparkles:'<path d="m12 3 2.5 6.5L21 12l-6.5 2.5L12 21l-2.5-6.5L3 12l6.5-2.5ZM20 2v4m-2-2h4"/>',lock:'<rect x="5" y="10" width="14" height="11" rx="2"/><path d="M8 10V7a4 4 0 0 1 8 0v3M12 14v3"/>',sliders:'<path d="M4 7h16M4 17h16"/><circle cx="9" cy="7" r="2" fill="white"/><circle cx="15" cy="17" r="2" fill="white"/>',reset:'<path d="M4 9a9 9 0 1 1 0 7M4 3v6h6"/>',close:'<path d="m6 6 12 12M6 18 18 6"/>',check:'<path d="m5 12 4 4L19 6"/>',eye:'<path d="M2 12s4-7 10-7 10 7 10 7-4 7-10 7-10-7-10-7Z"/><circle cx="12" cy="12" r="3"/>','eye-off':'<path d="m3 3 18 18M10 5h2c6 0 10 7 10 7a24 24 0 0 1-4 4M6 6a24 24 0 0 0-4 6s4 7 10 7h2"/>',info:'<circle cx="12" cy="12" r="9"/><path d="M12 11v6M12 7v1"/>',file:'<path d="M5 3h9l5 5v13H5ZM14 3v6h5M8 13h8M8 17h6"/>',code:'<path d="m8 6-6 6 6 6m8-12 6 6-6 6m-5 2 2-16"/>'
  };
  const icon = name => '<svg class="icon" viewBox="0 0 24 24" aria-hidden="true">'+(svgPaths[name]||svgPaths.layers)+'</svg>';
  function fillIcons(root=document) { root.querySelectorAll('[data-icon]').forEach(element => { element.innerHTML=icon(element.dataset.icon); }); }
  fillIcons();
  let config = model.clone(model.defaults);
  let page = 0, selected = null, editing = true, activeTab = 'design';
  let currentApp=null,currentSection=null;
  let past = [], future = [], lastChange = {path:'',at:0};
  let saveTimer, toastTimer, pendingPNG = null;
  const frame = $('preview');
  const thumbnails = [];
  try {
    const saved = localStorage.getItem(STORAGE);
    if (saved) config = model.validate(JSON.parse(saved));
  } catch(error) { setTimeout(() => toast('没有读到有效的本地配置，已载入初始设计。'),500); }
  function toast(message) {
    $('toast').textContent=message;$('toast').hidden=false;
    clearTimeout(toastTimer);toastTimer=setTimeout(() => { $('toast').hidden=true; },3800);
  }
  function send(type,extra={},target=frame) { target.contentWindow?.postMessage({source:'yiyi-designer',type,...extra},'*'); }
  function assetsForPreview() {
    return Object.fromEntries([config.artwork,config.background,...Object.values(config.postcard),config.daily.mascot,'assets/message-board.jpg','assets/message-board-yellow.png','assets/daily-happy.png','assets/daily-calm.png','assets/daily-sad.png','assets/daily-anxious.png','assets/daily-tired.png','assets/daily-neutral.png','assets/daily-wood.jpg','assets/holiday-calendar.png','assets/holiday-plane.png','assets/holiday-dragon.png',...['predawn','morning','day','sunset','dusk','night'].map(key=>'assets/clock-'+key+'.jpg'),'assets/clock-matrix-reference.jpg','assets/clock-flip-reference.jpg'].filter(src => src && window.YiyiExportSources.assets[src]).map(src => [src,window.YiyiExportSources.assets[src]]));
  }
  function updatePreview() {
    const payload={config,assets:assetsForPreview()};
    send('config',payload);thumbnails.forEach(item => send('config',payload,item));
  }
  function persist() {
    clearTimeout(saveTimer);
    try { localStorage.setItem(STORAGE,JSON.stringify(config));$('save-state').innerHTML='<i></i>已自动保存'; }
    catch(error) { $('save-state').textContent='请导出配置保存';toast('浏览器本地存储已满或不可用。请导出设计配置保存你的修改。'); }
  }
  function scheduleSave() { $('save-state').innerHTML='<i></i>正在保存';clearTimeout(saveTimer);saveTimer=setTimeout(persist,350); }
  function historyButtons() { $('undo').disabled=!past.length;$('redo').disabled=!future.length; }
  function commit(next,path='') {
    let checked;
    try { checked=model.validate(next); } catch(error) { toast(error.message);return false; }
    if (JSON.stringify(checked)===JSON.stringify(config)) return true;
    const now=Date.now();
    if (!path || path!==lastChange.path || now-lastChange.at>650) { past.push(model.clone(config));if(past.length>40)past.shift(); }
    lastChange={path,at:now};future=[];config=checked;
    updatePreview();updateSmallUI();historyButtons();scheduleSave();return true;
  }
  function undo(redo=false) {
    const source=redo?future:past,target=redo?past:future;
    if (!source.length) return;
    const dailyRecords=model.clone(config.dailyRecords),readingCache=model.clone(config.readingCache);
    target.push(model.clone(config));config=source.pop();config.dailyRecords=dailyRecords;config.readingCache=readingCache;lastChange={path:'',at:0};
    renderAll();updatePreview();scheduleSave();historyButtons();toast(redo?'已重做上一步修改。':'已撤销上一步修改。');
  }
  function setAt(target,path,value) {
    const keys=path.split('.');const last=keys.pop();let current=target;
    keys.forEach((key,index)=>{if(!current[key] || typeof current[key]!=='object')current[key]=/^\d+$/.test(keys[index+1]||last)?[]:{};current=current[key];});current[last]=value;
  }
  function getAt(path) { return path.split('.').reduce((current,key) => current?.[key],config); }
  const pageIcons=['home','weather','calendar','letter','sparkles','calendar','grid'];
  const pageSubtitles=['HOME & CLOCK','WEATHER & FORECAST','HOLIDAY & COUNTDOWN','WORDS & POETRY','MOOD & DAILY LIFE','CLASS SCHEDULE','ALL APPS'];
  function renderPages() {
    $('page-list').innerHTML=config.pageNames.map((name,index) => '<button class="page-button'+(page===index?' active':'')+'" data-page="'+index+'" aria-current="'+(page===index?'page':'false')+'"><span class="page-icon">'+icon(pageIcons[index])+'</span><span><strong>'+escape(name)+'</strong><small>'+pageSubtitles[index]+'</small></span><span class="page-number">0'+(index+1)+'</span></button>').join('');
    $('navigation-dots').innerHTML=config.pageNames.map((name,index) => '<button class="'+(page===index?'active':'')+'" data-page="'+index+'" aria-label="'+escape(name)+'"></button>').join('');
    document.querySelectorAll('.thumbnail').forEach((button,index) => { button.classList.toggle('active',index===page);button.querySelector('.thumbnail-label').innerHTML='<span>'+escape(config.pageNames[index])+'</span><span>0'+(index+1)+'</span>'; });
  }
  function renderLayers() {
    $('layer-list').innerHTML=model.layers[page].map(layer => {
      const visible=config.layouts[page][layer.id].visible;
      const type=layer.id==='avatar'||layer.id==='photo'?'image':layer.id==='clock'||layer.id==='readout'?'clock':layer.id==='grid'?'grid':layer.id==='weather'?'layers':'text';
      return '<div class="layer-row'+(selected===layer.id?' active':'')+(visible?'':' is-hidden')+'"><button data-select="'+layer.id+'">'+icon(type)+'<span>'+escape(layer.name)+'</span></button><button class="visibility-button" data-visible="'+layer.id+'" title="'+(visible?'隐藏':'显示')+'图层" aria-label="'+(visible?'隐藏':'显示')+escape(layer.name)+'">'+icon(visible?'eye':'eye-off')+'</button></div>';
    }).join('');
  }
  function renderPalettes() {
    $('palette-list').innerHTML=Object.entries(model.palettes).map(([id,palette]) => '<button class="palette-button'+(config.palette===id?' active':'')+'" data-palette="'+id+'"><span class="palette-dots">'+[palette.bg,palette.header,palette.accent].map(color => '<i style="background:'+color+'"></i>').join('')+'</span><span>'+palette.name+'</span><span class="palette-check">'+(config.palette===id?icon('check'):'')+'</span></button>').join('');
    $('color-fields').innerHTML=[['bg','屏幕背景'],['header','浅色点缀'],['ink','文字颜色'],['accent','强调色'],['surface','卡片底色'],['muted','辅助文字']].map(([key,label]) => '<label class="color-row"><span>'+label+'</span><input type="color" data-bind="colors.'+key+'" value="'+config.colors[key]+'" aria-label="'+label+'"><input class="color-hex" data-color-hex="'+key+'" value="'+config.colors[key]+'" maxlength="7" aria-label="'+label+'十六进制色值"></label>').join('');
    $('radius').value=config.radius;$('radius-output').textContent=config.radius;
  }
  const assetMarkup = asset => asset.type==='原始动图' ? '<span class="gif-card-view"><img src="'+escape(window.YiyiExportSources.assets[asset.src]||asset.src)+'" alt="'+escape(asset.name)+'" loading="lazy"></span>' : '<img src="'+escape(window.YiyiExportSources.assets[asset.src]||asset.src)+'" alt="'+escape(asset.name)+'" loading="lazy">';
  function renderAssets() {
    $('quick-assets').innerHTML=[model.assets.find(asset=>asset.src==='assets/postcard-cat.png'),model.assets[3],model.assets[9]].map(asset => '<button class="quick-asset'+(config.postcard.mascot===asset.src?' active':'')+'" data-asset="'+escape(asset.src)+'" title="'+escape(asset.name)+'">'+assetMarkup(asset)+'</button>').join('');
  }
  function openMaterials() {
    $('asset-gallery').innerHTML=model.assets.map(asset => '<button class="gallery-asset'+(config.artwork===asset.src?' active':'')+'" data-asset="'+escape(asset.src)+'"><span class="gallery-visual">'+assetMarkup(asset)+'</span><strong>'+escape(asset.name)+'</strong><small>'+asset.type+'</small></button>').join('');
    $('materials-dialog').showModal();
  }
  function field(label,path,options={}) {
    const value=getAt(path);
    if(options.type==='checkbox')return '<label class="field-label checkbox"><input type="checkbox" data-bind="'+path+'" '+(value?'checked':'')+'>'+label+'</label>';
    if(options.type==='textarea')return '<label class="field-label">'+label+'<textarea data-bind="'+path+'" maxlength="'+(options.maxlength||3000)+'">'+escape(value)+'</textarea></label>';
    if(options.type==='select')return '<label class="field-label">'+label+'<select data-bind="'+path+'">'+options.values.map(([id,name])=>'<option value="'+id+'"'+(String(value)===id?' selected':'')+'>'+name+'</option>').join('')+'</select></label>';
    return '<label class="field-label">'+label+'<input data-bind="'+path+'" type="'+(options.type||'text')+'" value="'+escape(value)+'" '+(options.maxlength?'maxlength="'+options.maxlength+'"':'')+(options.min!==undefined?' min="'+options.min+'"':'')+(options.max!==undefined?' max="'+options.max+'"':'')+(options.step?' step="'+options.step+'"':'')+'></label>';
  }
  function weatherContent() {
    let html='<section class="inspector-section"><div class="section-heading">天气 App / 共享城市</div><label class="field-label">当前城市<input data-shared-weather-city readonly value="'+escape(config.weather.city)+'" placeholder="还未选择城市"></label><button class="upload-zone" id="set-weather-city">'+icon('play')+'设置天气地区</button><p class="field-hint">第二页与主页引用同一份天气数据。点击城市或这里的按钮设置地区；真实读数由天气服务统一更新。</p><button class="upload-zone" id="weather-example">'+icon('sparkles')+'填入参考图示例读数</button><p class="field-hint">示例只用于设计，保留当前城市并标记为预览数据。</p></section>';
    html+='<section class="inspector-section"><div class="section-heading">当前天气预览</div>'+field('天气状态','weather.condition',{maxlength:20})+'<div class="field-grid">'+[['temperature','温度 °C',-99,99],['feelsLike','体感 °C',-99,99],['high','最高温 °C',-99,99],['low','最低温 °C',-99,99],['humidity','湿度 %',0,100]].map(([key,label,min,max])=>field(label,'weather.'+key,{type:'number',min,max,step:1})).join('')+'</div>'+field('风况','weather.wind',{maxlength:60})+'</section>';
    html+='<section class="inspector-section"><div class="section-heading">环境指标预览</div><div class="field-grid">'+field('日出','weather.sunrise',{type:'time'})+field('日落','weather.sunset',{type:'time'})+[['pressure','气压 hPa',0,2000],['uv','紫外线指数',0,30],['visibility','能见度 km',0,999],['aqi','空气质量 AQI',0,1000]].map(([key,label,min,max])=>field(label,'weather.'+key,{type:'number',min,max,step:.01})).join('')+'</div>'+field('空气质量说明（留空自动判断）','weather.aqiLabel',{maxlength:20})+'<p class="field-hint">空读数显示 —，不补入固定数据。单位采用 hPa、km 与 AQI。</p></section>';
    for(let index=0;index<7;index++)html+='<section class="inspector-section"><div class="section-heading">第 '+(index+1)+' 天预报</div><div class="field-grid">'+field('预报日期','weather.forecast.'+index+'.date',{type:'date'})+field('天气状态','weather.forecast.'+index+'.condition',{maxlength:20})+field('最高温 °C','weather.forecast.'+index+'.high',{type:'number',min:-99,max:99,step:1})+field('最低温 °C','weather.forecast.'+index+'.low',{type:'number',min:-99,max:99,step:1})+'</div></section>';
    return html;
  }
  function renderContent() {
    let html='<section class="inspector-section"><div class="section-heading">页面名称</div>'+field('在工作台显示的名字','pageNames.'+page,{maxlength:20})+'</section>';
    if(page===0) html+='<section class="inspector-section"><div class="section-heading">邮票时钟</div>'+field('使用北京时间实时走时','home.liveClock',{type:'checkbox'})+'<div style="height:14px"></div>'+field('预览时间','home.time',{type:'time'})+field('预览日期','home.calendarDate',{type:'date',min:'1900-01-01',max:'2099-12-31'})+field('时钟卡片底色','home.clockColor',{type:'color'})+'<p class="field-hint">时间、星期、日历和倒计时共用同一时钟。关闭实时走时后使用预览日期时间。</p></section><section class="inspector-section"><div class="section-heading">共享天气数据</div><label class="field-label">天气 App 当前城市<input data-shared-weather-city readonly value="'+escape(config.weather.city)+'" placeholder="请在天气 App 设置城市"></label><button class="upload-zone" id="open-weather">'+icon('play')+'打开天气 App 设置地区</button><div class="field-grid">'+field('预览天气状态','weather.condition',{maxlength:20})+field('预览温度 °C','weather.temperature',{type:'number',min:-99,max:99,step:1})+'</div>'+field('模拟 Wi-Fi 已连接','home.wifi',{type:'checkbox'})+'<p class="field-hint">城市、温度与天气状态只存一份；主页直接引用天气 App 的数据。这里可注入预览数据，真实数据由后续天气服务更新。</p></section><section class="inspector-section"><div class="section-heading">电池状态预览</div>'+field('常接外部电源使用','battery.externalPower',{type:'checkbox'})+field('已接入电池','battery.present',{type:'checkbox'})+'<div class="field-grid">'+field('电池电量 %','battery.percent',{type:'number',min:0,max:100,step:1})+field('充电状态','battery.charging',{type:'select',values:[['false','未充电'],['true','充电中'],['null','未知']]})+'</div><div class="field-grid"><button class="upload-zone" data-battery-preset="plugged">接电使用</button><button class="upload-zone" data-battery-preset="normal">正常 80%</button><button class="upload-zone" data-battery-preset="charging">充电 80%</button><button class="upload-zone" data-battery-preset="low">低电量 20%</button><button class="upload-zone" data-battery-preset="unknown">读数未知</button></div><p class="field-hint">电池供电时 ≤20% 显示红色低电量；充电按钮用于模拟。此板没有 USB 和充电状态检测引脚，接电使用与是否安装电池由设置指定。</p></section><section class="inspector-section"><div class="section-heading">主页插画</div><button class="upload-zone" id="postcard-picture-upload">'+icon('upload')+'替换邮票大图</button><button class="upload-zone" id="postcard-weather-upload">'+icon('upload')+'替换天气插画</button><button class="upload-zone" id="postcard-art-reset">'+icon('reset')+'恢复参考图插画</button><p class="field-hint">素材来自 Ui设计图片。小黑日历插画可在外观素材库中替换。</p></section><section class="inspector-section"><div class="section-heading">页面入口</div><p class="field-hint">邮票插画；天气 → 天气详情；小黑 → 倒计时；月历或时钟 → 日历。左右滑动或点击底部圆点进入全部应用。</p></section>';
    if(page===1)html+=weatherContent();
    if(page===2)html+='<section class="inspector-section"><div class="section-heading">星期与放假</div>'+field('目标星期','holiday.weekday',{type:'select',values:[['1','周一'],['2','周二'],['3','周三'],['4','周四'],['5','周五'],['6','周六'],['0','周日']]})+field('日历文字','holiday.label',{maxlength:4})+'<p class="field-hint">中文问题、英文星期与剩余天数共用一个目标星期。当日显示“今天”与所设日历文字，次日计算到下周。可以设为周一并把文字改为“上班”。</p></section><section class="inspector-section"><div class="section-heading">节日倒计时</div><p class="field-hint">元旦为下一次 1 月 1 日；过年为下一次农历正月初一。按北京时间日期差计算，节日当天显示 0 天，次日自动转到下一年。春节日期覆盖 1900～2100 年，离线可用。</p><p class="field-hint">预览日期与主页共用；在主页“内容”关闭实时走时后修改日期，可检查跨天和节日效果。点击节日行查看对应日历。</p></section><section class="inspector-section"><div class="section-heading">页面配色</div>'+[['background','背景'],['cardColor','卡片'],['textColor','正文'],['numberColor','数字区 / 辅助文字'],['unitColor','天字区']].map(([key,label])=>field(label,'holiday.'+key,{type:'color'})).join('')+'</section>';
    if(page===5) {
      const tt = window.YIYI_TIMETABLE;
      const summary = tt
        ? `${tt.term.name} · ${tt.campus}<br>第 ${tt.term.currentWeek} 周 / 共 ${tt.term.totalWeeks} 周<br>${new Set(tt.courses.map(c => c.name)).size} 门课 · ${tt.courses.length} 条排课`
        : '还没有导入课表';
      html += '<section class="inspector-section"><div class="section-heading">课程表数据</div><p class="field-hint">7 天 × 1–10 节，色块里直接显示课程和教室；点色块看教师、时间、周次。单双周和短周课程按所选周过滤。</p><div class="about-lines">' + summary + '</div><p class="field-hint">数据由手机连接页导入，工作台只读预览；导入后可在这里直接看到真实课表。</p></section>';
    }
    if(page===4)html+='<section class="inspector-section"><div class="section-heading">心情选择</div><p class="field-hint">切到“体验”，点击加号选择今天的心情，或点击本周日期修改当天心情。点击表情立即保存；未来日期不提前记入。每周按周一到周日排列。</p></section><section class="inspector-section"><div class="section-heading">敲木鱼</div>'+field('开启敲击声音','daily.sound',{type:'checkbox'})+'<p class="field-hint">点击木鱼计数并反馈；“更多”查看累计次数、撤销误敲或开关声音。</p></section><section class="inspector-section"><div class="section-heading">喝水记录</div>'+field('每日目标 ml','daily.waterGoal',{type:'number',min:100,max:10000,step:1})+'<p class="field-hint">200 ml / 500 ml 按钮直接记录。点击上方饮水数字进入明细，可自定义饮水量、撤销上次和设置目标。</p></section><section class="inspector-section"><div class="section-heading">本页外观</div>'+field('页面背景','daily.background',{type:'color'})+field('心情卡片底色','daily.cardColor',{type:'color'})+field('中间插画','daily.mascot',{type:'select',values:model.assets.map(item=>[item.src,item.name])})+'<p class="field-hint">心情、木鱼和饮水按共享北京时间分别保存每天记录。跨天从新一天开始，旧记录保留。JSON 与独立 HTML 导出包含记录。自定义倒计时数据保留在日历中。</p></section>';
    if(page===3) {
      html+='<section class="inspector-section"><div class="section-heading">每日名言与诗句</div>'+field('每日自动更新','reading.autoUpdate',{type:'checkbox'})+'<p class="field-hint">日期引用共享北京时间。自动更新每天一次，点击卡片右上角可分别刷新；点击正文阅读完整内容。接口失败时保留缓存，无缓存时显示参考图示例。</p></section>';
      for(const [kind,title] of [['quote','名言'],['poem','诗句']]) {
        html+='<section class="inspector-section"><div class="section-heading">'+title+'内容</div>'+field('内容来源','reading.'+kind+'Mode',{type:'select',values:[['api','在线接口'],['manual','自定义文字']]})+field('自定义'+title,'reading.'+kind+'Text',{type:'textarea',maxlength:kind==='poem'?24000:3000})+field('作者','reading.'+kind+'Author',{maxlength:100});
        if(kind==='poem')html+=field('朝代','reading.poemDynasty',{maxlength:100})+field('诗词标题','reading.poemTitle',{maxlength:100});
        html+='<p class="field-hint">选择“自定义文字”后使用这里的内容；切回“在线接口”恢复缓存并自动更新。</p></section>';
      }
      html+='<section class="inspector-section"><div class="section-heading">卡片配色</div>'+[['background','页面背景'],['quoteColor','名言卡片'],['quoteInk','名言文字'],['poemColor','诗句卡片'],['poemInk','诗句文字']].map(([key,label])=>field(label,'reading.'+key,{type:'color'})).join('')+'<p class="field-hint">两张卡片的位置、大小、字号与显隐在“外观”的图层面板修改。</p></section>';
    }
    if(page===6)html+='<section class="inspector-section"><div class="section-heading">自定义倒计时</div>'+field('倒计时名称','countdown.name',{maxlength:40})+field('目标时间（北京时间）','countdown.date',{type:'datetime-local',min:'1900-01-01T00:00',max:'2099-12-31T23:59'})+'<p class="field-hint">原第四页的倒计时数据已保留。打开日历，点击“设置倒计时”可查看、修改或清除；到期显示“已到达”。</p></section>';
    if(page===6)html+='<section class="inspector-section"><div class="section-heading">应用与手机设置</div><p class="field-hint" style="font-size:11px">时钟模式 · 留言 · 天气<br>小智 · 日历 · 设置</p><p class="field-hint">切到“体验”，点击图标查看各应用。小智、天气和设备状态为交互演示。</p><button class="upload-zone" id="try-voice">'+icon('play')+'体验小智对话</button></section>';
    $('content-panel').innerHTML=html;
  }
  function renderLayerFields() {
    const definition=model.layers[page].find(item => item.id===selected);
    if(!definition) { $('layer-fields').innerHTML='<div class="empty-selection">'+icon('cursor')+'<h3>从一个小细节开始</h3><p>点击画布或左侧图层，<br>调整位置、尺寸和字号。</p></div>';return; }
    const path='layouts.'+page+'.'+selected;
    const layout=config.layouts[page][selected];
    $('layer-fields').innerHTML='<section class="inspector-section"><div class="section-heading">已选图层<span>'+icon('layers')+'</span></div><h3 class="selected-layer-name">'+escape(definition.name)+'</h3><div class="field-grid">'+field('X · 横向位置',path+'.x',{type:'number',min:0,max:320-layout.w})+field('Y · 纵向位置',path+'.y',{type:'number',min:0,max:240-layout.h})+field('W · 宽度',path+'.w',{type:'number',min:12,max:320})+field('H · 高度',path+'.h',{type:'number',min:12,max:240})+'</div><p class="field-hint">单位为屏幕逻辑像素，也可以直接拖动画布中的图层。</p></section><section class="inspector-section"><div class="section-heading">显示样式</div>'+field('字号 px',path+'.size',{type:'number',min:page===1||page===5?6:8,max:72})+field('透明度 %',path+'.opacity',{type:'number',min:0,max:100})+field('显示这个图层',path+'.visible',{type:'checkbox'})+'</section><section class="inspector-section"><button class="reset-button" id="reset-layer">'+icon('reset')+'恢复这个图层的初始位置</button></section>';
  }
  function updateSmallUI() {
    $('project-name').textContent=model.palettes[config.palette].name;
    $('inspector-title').textContent=config.pageNames[page]+'设计';
    $('artboard-name').textContent='0'+(page+1)+' / '+config.pageNames[page];
    $('status-page').textContent=config.pageNames[page];
    $('status-selection').textContent=selected?(model.layers[page].find(item=>item.id===selected)?.name || '')+' · 已选中':'没有选中图层';
    $('radius-output').textContent=config.radius;
  }
  function renderAll() { renderPages();renderLayers();renderPalettes();renderAssets();renderContent();renderLayerFields();updateSmallUI();historyButtons(); }
  function tab(name) {
    activeTab=name;
    ['design','content','layer'].forEach(item=>{ $('tab-'+item).classList.toggle('active',item===name);$('tab-'+item).setAttribute('aria-selected',String(item===name));$(item+'-panel').hidden=item!==name; });
  }
  function selectLayer(id) { selected=id;renderLayers();renderLayerFields();updateSmallUI();if(id)tab('layer');send('select',{id}); }
  function switchPage(index) { page=(index+config.pageNames.length)%config.pageNames.length;selected=null;send('page',{page});renderPages();renderLayers();renderContent();renderLayerFields();updateSmallUI(); }
  function setMode(value) {
    editing=value;selected=null;send('mode',{editing});
    $('mode-design').classList.toggle('active',editing);$('mode-design').setAttribute('aria-pressed',String(editing));
    $('mode-preview').classList.toggle('active',!editing);$('mode-preview').setAttribute('aria-pressed',String(!editing));
    $('canvas-instruction').textContent=editing?'选择一个图层，把心意放在刚刚好的位置。':'点击应用、左右滑动，体验送给她的小小桌面。';
    $('canvas-tip').innerHTML=icon(editing?'cursor':'play')+(editing?'点击图层选中 · 拖动调整位置 · 修改后实时预览':'左右滑动切页 · 点击进入应用 · 箭头键也可以翻页');
    if(editing)send('page',{page});renderLayers();renderLayerFields();updateSmallUI();
  }
  function resizeCanvas() {
    const area=$('canvas-area');
    const automatic=Math.max(.65,Math.min(2,(area.clientWidth-85)/320,(area.clientHeight-260)/240));
    const value=$('zoom').value==='auto'?automatic:Number($('zoom').value);
    $('artboard-wrap').style.setProperty('--preview-scale',String(value));
    $('artboard-wrap').style.setProperty('--device-width',(320*value+38)+'px');
    if(innerWidth<=760) document.querySelectorAll('.thumbnail iframe').forEach(element=>{element.style.transform='scale('+Math.min(.3,(element.parentElement.clientWidth-2)/320)+')';element.parentElement.style.height=(240*Math.min(.3,(element.parentElement.clientWidth-2)/320))+'px';});
  }
  document.addEventListener('click',event => {
    const pageButton=event.target.closest('[data-page]');if(pageButton)switchPage(Number(pageButton.dataset.page));
    const layerButton=event.target.closest('[data-select]');if(layerButton)selectLayer(layerButton.dataset.select);
    const visibility=event.target.closest('[data-visible]');if(visibility){const next=model.clone(config);next.layouts[page][visibility.dataset.visible].visible=!next.layouts[page][visibility.dataset.visible].visible;commit(next);renderLayers();renderLayerFields();}
    const palette=event.target.closest('[data-palette]');if(palette){const next=model.clone(config);next.palette=palette.dataset.palette;next.colors=model.clone(model.palettes[next.palette]);commit(next);renderPalettes();}
    const asset=event.target.closest('[data-asset]');if(asset){const next=model.clone(config);next.artwork=asset.dataset.asset;next.postcard.mascot=asset.dataset.asset;commit(next);renderAssets();renderLayerFields();$('materials-dialog').close();toast('已替换为'+(next.artwork.startsWith('../')?'原始动图。':'小黑插画。'));}
    if(event.target.closest('#postcard-picture-upload'))$('postcard-picture-file').click();
    if(event.target.closest('#postcard-weather-upload'))$('postcard-weather-file').click();
    if(event.target.closest('#postcard-art-reset')){const next=model.clone(config);next.postcard=model.clone(model.defaults.postcard);commit(next);renderAssets();toast('已恢复参考图里的插画。');}
    if(event.target.closest('#set-weather-city')){setMode(false);send('action',{action:'weather-region'});}
    if(event.target.closest('#weather-example'))send('action',{action:'weather-example'});
    if(event.target.closest('#open-weather')){setMode(false);send('action',{action:'weather'});}
    const batteryPreset=event.target.closest('[data-battery-preset]');if(batteryPreset){const next=model.clone(config),preset=batteryPreset.dataset.batteryPreset;next.battery={present:preset!=='plugged',externalPower:preset==='plugged',percent:preset==='low'?20:preset==='unknown'||preset==='plugged'?null:80,charging:preset==='unknown'||preset==='plugged'?null:preset==='charging'};commit(next);renderContent();}
    if(event.target.closest('#try-voice')){setMode(false);send('action',{action:'voice'});}
    if(event.target.closest('#reset-layer')&&selected){const next=model.clone(config);next.layouts[page][selected]=model.clone(model.defaults.layouts[page][selected]);commit(next);renderLayerFields();}
    const exportItem=event.target.closest('[data-export]');if(exportItem){$('export-menu').hidden=true;$('export-button').setAttribute('aria-expanded','false');exportDesign(exportItem.dataset.export);}
    if(!event.target.closest('.export-wrap')){$('export-menu').hidden=true;$('export-button').setAttribute('aria-expanded','false');}
  });
  function handleInput(event) {
    const input=event.target;
    if(input.dataset.colorHex!==undefined){if(!/^#[0-9a-f]{6}$/i.test(input.value)){if(event.type==='change'){toast('颜色格式应为 # 加六位十六进制数字，例如 #619B78。');input.value=config.colors[input.dataset.colorHex];}return;}const next=model.clone(config);next.colors[input.dataset.colorHex]=input.value;commit(next,'color-'+input.dataset.colorHex);document.querySelector('[data-bind="colors.'+input.dataset.colorHex+'"]').value=input.value;return;}
    if(!input.dataset.bind)return;
    const path=input.dataset.bind;
    let value=input.type==='checkbox'?input.checked:['range','number'].includes(input.type)?Number(input.value):input.value;
    if(input.type==='number'&&input.value===''){if(path.startsWith('weather.'))value=null;else return;}
    if(input.type==='number'&&input.value!==''&&!Number.isFinite(Number(input.value)))return;
    if(path==='home.time'&&!/^([01]\d|2[0-3]):[0-5]\d$/.test(value))return;
    if(path==='countdown.date'&&event.type!=='change')return;
    if(path==='holiday.weekday')value=Number(input.value);
    if(path==='battery.charging')value=input.value==='null'?null:input.value==='true';
    const next=model.clone(config);setAt(next,path,value);
    if(path.startsWith('weather.')){next.weather.source='preview';next.weather.updatedAt=null;}
    if(commit(next,path)) {
      if(input.type==='color'&&path.startsWith('colors.'))document.querySelector('[data-color-hex="'+path.split('.')[1]+'"]').value=value;
      if(path.startsWith('pageNames.'))renderPages();
      if(path.startsWith('layouts.')&&event.type==='change')renderLayerFields();
    }
  }
  document.addEventListener('input',handleInput);document.addEventListener('change',handleInput);
  $('mode-design').onclick=()=>setMode(true);$('mode-preview').onclick=()=>setMode(false);
  $('undo').onclick=()=>undo();$('redo').onclick=()=>undo(true);
  $('grid-toggle').onclick=()=>{$('canvas-area').classList.toggle('grid-on');$('grid-toggle').classList.toggle('active');$('grid-toggle').setAttribute('aria-pressed',String($('canvas-area').classList.contains('grid-on')));};
  $('zoom').onchange=resizeCanvas;new ResizeObserver(resizeCanvas).observe($('canvas-area'));
  $('page-prev').onclick=()=>switchPage(page-1);$('page-next').onclick=()=>switchPage(page+1);$('home-button').onclick=()=>switchPage(0);
  $('deselect').onclick=()=>selectLayer(null);
  ['design','content','layer'].forEach(name=>{$('tab-'+name).onclick=()=>tab(name);});
  $('materials-button').onclick=openMaterials;$('all-materials').onclick=openMaterials;$('materials-close').onclick=()=>$('materials-dialog').close();
  $('materials-dialog').addEventListener('click',event=>{if(event.target===$('materials-dialog')){const rect=event.target.getBoundingClientRect();if(event.clientX<rect.left||event.clientX>rect.right||event.clientY<rect.top||event.clientY>rect.bottom)event.target.close();}});
  $('reset-button').onclick=()=>{const next=model.clone(model.defaults);next.dailyRecords=model.clone(config.dailyRecords);next.readingCache=model.clone(config.readingCache);commit(next);selected=null;renderAll();toast('已恢复初始设计，日常记录保留，可撤销找回设计修改。');};
  $('import-button').onclick=()=>$('config-file').click();
  $('export-button').onclick=()=>{const menu=$('export-menu');menu.hidden=!menu.hidden;$('export-button').setAttribute('aria-expanded',String(!menu.hidden));};
  $('artwork-upload-button').onclick=()=>$('artwork-file').click();$('background-upload-button').onclick=()=>$('background-file').click();
  $('background-clear').onclick=()=>{const next=model.clone(config);next.background='';commit(next);toast('已恢复主题纯色背景。');};
  $('config-file').onchange=async event=>{
    const file=event.target.files[0];if(!file)return;
    try{if(file.size>6500000)throw new Error('配置文件请小于 6.5 MB。');const imported=model.validate(JSON.parse((await file.text()).replace(/^\uFEFF/,'')));if(commit(imported)){selected=null;renderAll();toast('设计配置已导入，修改会继续自动保存。');}}
    catch(error){toast('导入失败：'+error.message);}event.target.value='';
  };
  async function readImage(file,maxEdge=900) {
    if(!['image/png','image/jpeg','image/gif','image/webp'].includes(file.type))throw new Error('请选择 PNG、JPEG、WebP 或 GIF 图片。');
    if(file.size>15000000)throw new Error('单张图片请小于 15 MB。');
    const data=await new Promise((resolve,reject)=>{const reader=new FileReader();reader.onload=()=>resolve(reader.result);reader.onerror=()=>reject(new Error('没有成功读取文件。'));reader.readAsDataURL(file);});
    const image=new Image();image.src=data;await image.decode();
    if(file.type==='image/gif'){if(file.size>3000000)throw new Error('自定义 GIF 请小于 3 MB，以便本地保存。');return data;}
    const scale=Math.min(1,maxEdge/Math.max(image.naturalWidth,image.naturalHeight));
    const canvas=document.createElement('canvas');canvas.width=Math.max(1,Math.round(image.naturalWidth*scale));canvas.height=Math.max(1,Math.round(image.naturalHeight*scale));
    canvas.getContext('2d').drawImage(image,0,0,canvas.width,canvas.height);
    return canvas.toDataURL(file.type==='image/png'?'image/png':'image/jpeg',.82);
  }
  for(const [id,key]of[['artwork-file','artwork'],['background-file','background']])$(id).onchange=async event=>{const file=event.target.files[0];if(!file)return;try{const image=await readImage(file);const next=model.clone(config);next[key]=image;if(key==='artwork')next.postcard.mascot=image;if(commit(next)){renderAssets();toast('自定义'+(key==='artwork'?'素材':'背景')+'已替换。');}}catch(error){toast(error.message);}event.target.value='';};
  for(const [id,key]of[['postcard-picture-file','picture'],['postcard-weather-file','weatherPicture']])$(id).onchange=async event=>{const file=event.target.files[0];if(!file)return;try{const image=await readImage(file,800);const next=model.clone(config);next.postcard[key]=image;if(commit(next))toast('主页插画已替换。');}catch(error){toast(error.message);}event.target.value='';};
  function download(blob,name) {
    const url=URL.createObjectURL(blob),link=document.createElement('a');link.href=url;link.download=name;document.body.append(link);link.click();link.remove();setTimeout(()=>URL.revokeObjectURL(url),10000);
  }
  const scriptSafe=source=>source.replace(/<\/script/gi,'<\\/script');
  const jsonSafe=value=>JSON.stringify(value).replace(/</g,'\\u003c');
  async function exportDesign(type) {
    const name=(config.name||'小黑桌面').replace(/[<>:"/\\|?*\x00-\x1f]/g,'_');
    try{
      if(type==='json'){download(new Blob([JSON.stringify(config,null,2)],{type:'application/json;charset=utf-8'}),name+'-设计配置.json');toast('设计配置已导出，可随时重新导入。');}
      if(type==='html'){
        const bundle=window.YiyiExportSources;
        const bootstrap='window.YIYI_INITIAL_CONFIG='+jsonSafe(config)+';window.YIYI_INITIAL_PAGE='+page+';window.YIYI_INITIAL_APP='+jsonSafe(currentApp==='clock'?'clock':null)+';window.YIYI_INITIAL_SECTION='+jsonSafe(currentApp==='clock'?currentSection:null)+';window.YIYI_INITIAL_ASSETS='+jsonSafe(assetsForPreview())+';';
        const html='<!doctype html><html lang="zh-CN"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>'+escape(config.name)+'</title><style>'+bundle.css+'\nhtml,body{width:100%;height:100%;min-height:280px;background:var(--gift-header);display:grid;place-items:center}#screen{box-shadow:0 20px 70px #0002;border-radius:5px;}</style></head><body>'+bundle.body+'<script>'+scriptSafe(bootstrap)+'</script><script>'+scriptSafe(bundle.model)+'</script><script>'+scriptSafe(bundle.data)+'</script><script>'+scriptSafe(bundle.holidayCalendar)+'</script><script>'+scriptSafe(bundle.dailyData)+'</script><script>'+scriptSafe(bundle.readingData)+'</script><script>'+scriptSafe(bundle.deviceLink)+'</script><script>'+scriptSafe(bundle.app)+'</script><script>'+scriptSafe(bundle.weatherUI)+'</script><script>'+scriptSafe(bundle.holidayUI)+'</script><script>'+scriptSafe(bundle.dailyUI)+'</script><script>'+scriptSafe(bundle.readingUI)+'</script><script>'+scriptSafe(bundle.deviceUI)+'</script><script>'+scriptSafe(bundle.messageUI)+'</script><script>'+scriptSafe(bundle.bridge)+'</script></body></html>';
        download(new Blob([html],{type:'text/html;charset=utf-8'}),name+'-互动预览.html');toast('独立 HTML 已导出，包含当前设计、照片与素材。');
      }
      if(type==='png'){
        if(pendingPNG){toast('正在生成效果图，请稍等。');return;}
        const request=Date.now();pendingPNG={request,name,timer:setTimeout(()=>{pendingPNG=null;toast('效果图生成超时，请等待素材加载完成后重试。');},12000)};
        send('snapshot',{request,css:window.YiyiExportSources.css});
      }
    }catch(error){toast('导出失败：'+error.message);}
  }
  async function savePNG(message) {
    if(!pendingPNG||pendingPNG.request!==message.request)return;
    const task=pendingPNG;clearTimeout(task.timer);pendingPNG=null;
    try{
      const svg='<svg xmlns="http://www.w3.org/2000/svg" width="320" height="240"><foreignObject width="320" height="240"><div xmlns="http://www.w3.org/1999/xhtml"><style>'+message.css+'</style>'+message.html+'</div></foreignObject></svg>';
      const image=new Image();image.src='data:image/svg+xml;charset=utf-8,'+encodeURIComponent(svg);await image.decode();
      const canvas=document.createElement('canvas');canvas.width=320;canvas.height=240;canvas.getContext('2d').drawImage(image,0,0);
      const blob=await new Promise(resolve=>canvas.toBlob(resolve,'image/png'));if(!blob)throw new Error('无法生成图片。');
      download(blob,task.name+'-'+config.pageNames[page]+'-320x240.png');toast('320 × 240 效果图已导出。GIF 保存当前一帧。');
    }catch(error){toast('图片导出失败：'+error.message);}
  }
  window.addEventListener('message',event=>{
    if(event.source!==frame.contentWindow||event.data?.source!=='yiyi-preview')return;
    const message=event.data;
    if(message.type==='ready'){updatePreview();send('page',{page});send('mode',{editing});}
    if(message.type==='selection')selectLayer(message.id);
    if(message.type==='view'){currentApp=message.app;currentSection=message.section;}
    if(message.type==='dragging'){
      ['x','y'].forEach(key=>{const input=document.querySelector('[data-bind="layouts.'+page+'.'+message.id+'.'+key+'"]');if(input)input.value=message[key];});
    }
    if(message.type==='layout'){const next=model.clone(config);next.layouts[message.page][message.id]=message.layout;commit(next);renderLayerFields();}
    if(message.type==='view'&&!editing){if(!message.app&&message.page!==page){page=message.page;selected=null;renderPages();renderLayers();renderContent();updateSmallUI();}$('artboard-name').textContent=message.app?'应用 / '+({clock:'时钟模式',letter:'留言',weather:'天气',voice:'小智',calendar:'日历',settings:'设置'}[message.app]||message.app):'0'+(page+1)+' / '+config.pageNames[page];}
    if(message.type==='snapshot')savePNG(message);
    if(message.type==='data') {
      const next=model.clone(config);
      if(message.settings)Object.assign(next,message.settings);
      if(message.weather)next.weather=message.weather;
      if(message.battery)next.battery=message.battery;
      if(message.holiday)next.holiday=message.holiday;
      if(message.daily)next.daily=message.daily;
      if(message.dailyRecords)next.dailyRecords=message.dailyRecords;
      if(message.readingCache)next.readingCache=message.readingCache;
      if(Object.hasOwn(message,'countdown')&&message.countdown!==undefined)next.countdown=message.countdown;
      try {
        const checked=model.validate(next);
        if(JSON.stringify(checked)!==JSON.stringify(config)) {
          config=checked;if(message.settings||message.dailyRecords||message.daily||message.readingCache||message.countdown!==undefined)persist();else scheduleSave();
          document.querySelectorAll('[data-bind^="weather."],[data-bind^="battery."],[data-bind^="holiday."],[data-bind^="daily."],[data-bind^="countdown."],[data-bind^="letter."],[data-bind^="keepsake."],[data-bind^="clockMode."],[data-bind^="deviceSettings."]').forEach(input=>{
            if(input===document.activeElement)return;
            const value=getAt(input.dataset.bind);if(input.type==='checkbox')input.checked=!!value;else input.value=value===null&&input.tagName!=='SELECT'?'':String(value);
          });
          const city=document.querySelector('[data-shared-weather-city]');if(city)city.value=config.weather.city;
        }
        thumbnails.forEach(item=>send('data',{weather:message.weather,battery:message.battery,holiday:message.holiday,daily:message.daily,dailyRecords:message.dailyRecords,readingCache:message.readingCache,countdown:message.countdown,settings:message.settings,epoch:message.epoch},item));
      } catch(error) { toast(error.message); }
    }
    if(message.type==='error'){if(pendingPNG){clearTimeout(pendingPNG.timer);pendingPNG=null;}toast(message.message);}
  });
  frame.addEventListener('load',()=>{send('config',{config,assets:assetsForPreview()});send('page',{page});send('mode',{editing});});
  $('thumbnail-list').innerHTML=config.pageNames.map((name,index)=>'<button class="thumbnail'+(index===0?' active':'')+'" data-page="'+index+'" aria-label="切换到'+escape(name)+'"><div class="thumbnail-screen"><iframe src="preview.html?thumbnail=1&amp;page='+index+'" title="'+escape(name)+'缩略图" loading="lazy" tabindex="-1"></iframe></div><span class="thumbnail-label"><span>'+escape(name)+'</span><span>0'+(index+1)+'</span></span></button>').join('');
  document.querySelectorAll('.thumbnail iframe').forEach(item=>{thumbnails.push(item);item.addEventListener('load',()=>send('config',{config,assets:assetsForPreview()},item));});
  document.addEventListener('keydown',event=>{
    const input=event.target.closest('input,textarea,select');
    if((event.ctrlKey||event.metaKey)&&event.key.toLowerCase()==='s'){event.preventDefault();persist();toast('当前设计已保存。');return;}
    if(input)return;
    if((event.ctrlKey||event.metaKey)&&event.key.toLowerCase()==='z'){event.preventDefault();undo(event.shiftKey);}
    if((event.ctrlKey||event.metaKey)&&event.key.toLowerCase()==='y'){event.preventDefault();undo(true);}
    if(event.key==='Escape'){selectLayer(null);$('export-menu').hidden=true;}
    if(event.key==='ArrowLeft'||event.key==='ArrowRight'){event.preventDefault();if(selected&&editing){const next=model.clone(config);next.layouts[page][selected].x+=event.key==='ArrowRight'?1:-1;commit(next,'keyboard-drag');renderLayerFields();}else switchPage(page+(event.key==='ArrowRight'?1:-1));}
    if((event.key==='ArrowUp'||event.key==='ArrowDown')&&selected&&editing){event.preventDefault();const next=model.clone(config);next.layouts[page][selected].y+=event.key==='ArrowDown'?1:-1;commit(next,'keyboard-drag');renderLayerFields();}
  });
  // The iframe can finish loading before this large workbench bundle does.
  // Send an initial state too; its ready/load handlers cover the opposite order.
  renderAll();resizeCanvas();persist();updatePreview();send('page',{page});send('mode',{editing});
})();

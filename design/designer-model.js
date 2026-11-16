(function () {
  'use strict';
  const clone = value => JSON.parse(JSON.stringify(value));
  const layer = (id, name, selector, x, y, w, h, size, extra = {}) => ({id, name, selector, x, y, w, h, size, visible:true, opacity:100, ...extra});
  const layers = [
    [layer('picture','邮票插画','.postcard-picture',12,13,194,116,12),layer('clock','上下排列的时钟','.clock',213,8,100,126,39),layer('date','日期与星期','.date',217,100,90,13,8),layer('weekday','英文星期','.stamp-weekday',217,114,90,15,10),layer('weather','天气卡片','.weather-pill',12,145,91,73,12),layer('week','小黑日历 / 倒计时','.week-card',112,145,88,73,10),layer('calendar','月历卡片','.home-calendar',209,145,99,73,8),layer('battery','电量 / 充电状态','.battery-indicator',51,221,48,17,8)],
    [layer('city','天气城市','.weather-city',65,8,190,20,14),layer('date','天气日期 / 星期','.weather-date',65,29,190,14,8),layer('updated','上次更新时间','.weather-updated',226,4,86,10,6),layer('current','当前天气 / 体感温度','.weather-current',8,52,138,100,34),layer('metrics','日出日落 / 环境指标','.weather-metrics',155,53,157,98,10),layer('forecast','七天预报','.weather-seven-day',8,160,304,60,8),layer('controls','地区 / 刷新 / 状态','.weather-toolbar',8,222,304,16,7)],
    [layer('card','星期卡片底色','.holiday-top-card',6,6,308,119,12),layer('question','目标星期问题','.holiday-question',22,37,180,24,16),layer('answer','放假状态 / 剩余天数','.holiday-answer',22,65,184,36,21),layer('calendar','翻页日历 / 英文星期','.holiday-calendar',201,18,110,107,14),layer('newyear','元旦倒计时','.holiday-newyear',6,134,308,43,13),layer('spring','春节倒计时','.holiday-spring',6,188,308,43,13)],
    [layer('quote','每日名言卡片','.reading-quote',8,5,304,131,16),layer('poem','诗句卡片','.reading-poem',27,140,266,91,19)],
    [layer('mood','心情选择 / 本周记录','.daily-mood-card',8,5,304,131,10),layer('wood','敲木鱼 / 今日次数','.daily-wood',8,140,100,91,10),layer('mascot','舞狮小黑插画','.daily-mascot',111,140,99,94,12),layer('water','饮水量 / 快捷记录','.daily-water',213,140,99,91,10)],
    [],
    [layer('grid','全部应用','.app-grid',14,48,292,164,11)]
  ];
  const assets = [
    {src:'assets/cat-card.png',name:'森林里的小黑',type:'静态插画'},
    ...[['195147','送你一朵花'],['201415','躺平的一天'],['201526','抱着绿枕头'],['201656','小黑的背影'],['201743','抱着黄枕头'],['201827','乖乖坐着'],['201934','小皇冠'],['202242','转过身的小黑'],['202304','生日快乐'],['202316','桃花小黑']].map(([id,name]) => ({src:'../罗小黑gif和表情/lv_0_20261006'+id+'.gif',name,type:'原始动图'}))
    ,...['predawn','morning','day','sunset','dusk','night'].map((key,index)=>({src:'assets/clock-'+key+'.jpg',name:['黎明山水','清晨山水','白昼山水','日落山水','暮色山水','深夜山水'][index],type:'时钟背景'})),
    {src:'assets/postcard-art.jpg',name:'春日邮票插画',type:'静态插画'},
    {src:'assets/postcard-weather.jpg',name:'花丛里的伙伴',type:'静态插画'},
    {src:'assets/postcard-cat.png',name:'小黑日历插画',type:'静态插画'},
    {src:'assets/postcard-background.jpg',name:'春日背景',type:'静态插画'},
    {src:'assets/daily-mascot.png',name:'舞狮小黑',type:'静态插画'},
    {src:'assets/message-board.jpg',name:'小黑留言板原图',type:'静态插画'},
    {src:'assets/message-board-yellow.png',name:'浅黄色小黑留言背景',type:'静态插画'}
  ];
  const palettes = {
    forest:{name:'森林序曲',bg:'#f4f7ef',header:'#deebd5',ink:'#244432',accent:'#619b78',muted:'#7c9275',surface:'#ffffff'},
    paper:{name:'奶油信笺',bg:'#fcf7ed',header:'#f2e7d2',ink:'#574637',accent:'#b3875f',muted:'#a38c70',surface:'#fffcf6'},
    night:{name:'月夜小黑',bg:'#1e2d35',header:'#293e47',ink:'#e1eadf',accent:'#9ac4ac',muted:'#9ab0af',surface:'#31454e'}
  };
  const moods=[{id:'happy',name:'开心'},{id:'calm',name:'平静'},{id:'neutral',name:'一般'},{id:'tired',name:'疲惫'},{id:'anxious',name:'烦恼'},{id:'sad',name:'难过'}];
  const defaults = {
    version:1,homeStyle:'postcard-v1',weatherStyle:'weather-board-v1',holidayStyle:'holiday-board-v1',name:'给依依的礼物',palette:'forest',colors:clone(palettes.forest),radius:14,
    dailyStyle:'daily-board-v1',readingStyle:'reading-board-v1',messageStyle:'yellow-paper-v3',deskStyle:'seven-pages-v1',pageNames:['主页','天气','放假倒计时','名言与诗句','心情与日常','课程表','全部应用'],
    clockMode:{preset:'landscape',seconds:true},deviceSettings:{brightness:100,volume:45,autoHome:true},
    reading:{autoUpdate:true,quoteMode:'api',poemMode:'api',background:'#a4ccac',quoteColor:'#ffffff',quoteInk:'#292929',poemColor:'#b80e10',poemInk:'#ffffff',quoteText:'虽然辛苦，\n我还是会选择那种滚烫的人生。',quoteAuthor:'北野武',poemText:'人生得意须尽欢，\n莫使金樽空对月。',poemTitle:'将进酒',poemAuthor:'李白',poemDynasty:'唐'},
    readingCache:{quote:null,poem:null},
    daily:{waterGoal:1000,sound:true,background:'#a4ccac',cardColor:'#fff8dc',mascot:'assets/daily-mascot.png'},dailyRecords:{},
    home:{liveClock:true,time:'08:26',calendarDate:'2026-10-07',clockColor:'#acc976',wifi:false},
    weather:{city:'',condition:'多云',temperature:22,feelsLike:null,humidity:null,wind:'',low:null,high:null,sunrise:null,sunset:null,pressure:null,uv:null,visibility:null,aqi:null,aqiLabel:'',forecast:[],updatedAt:null,source:'preview'},
    battery:{present:false,percent:null,charging:null,externalPower:true},
    postcard:{picture:'assets/postcard-art.jpg',weatherPicture:'assets/postcard-weather.jpg',mascot:'assets/postcard-cat.png',backdrop:'assets/postcard-background.jpg'},
    letter:{title:'写给依依的小纸条',text:'把小小的快乐，\n装进每一个普通的日子。\n\n愿你抬头时，总有小黑陪着你。',signature:'给最特别的依依 ♡'},
    keepsake:{title:'留个小纪念',text:'',signature:'',date:''},
    countdown:{name:'下一次见面',date:'2027-01-01T00:00'},
    holiday:{weekday:5,label:'放假',background:'#f2f2f2',cardColor:'#ffffff',textColor:'#403c39',numberColor:'#a19894',unitColor:'#62483e'},
    artwork:'assets/cat-card.png',background:'',photos:[],photoCaption:'把喜欢的瞬间，留在这里。',photoFit:'cover',
    layouts:layers.map(items => Object.fromEntries(items.map(item => [item.id,clone(item)])))
  };
  function validate(input) {
    if (!input || typeof input !== 'object' || Array.isArray(input) || input.version !== 1) throw new Error('请选择此工作台导出的版本 1 设计配置。');
    const next = clone(defaults);
    const string = (value,max,fallback) => typeof value === 'string' ? value.slice(0,max) : fallback;
    const number = (value,min,max,fallback) => typeof value === 'number' && Number.isFinite(value) ? Math.max(min,Math.min(max,value)) : fallback;
    const color = (value,fallback) => /^#[0-9a-f]{6}$/i.test(value) ? value : fallback;
    const asset = value => typeof value === 'string' && (assets.some(item => item.src === value) || /^data:image\/(png|jpeg|gif|webp);base64,[A-Za-z0-9+/=]+$/.test(value)) ? value : '';
    const nullable = (value,min,max) => value === null || value === undefined ? null : number(value,min,max,null);
    next.name = string(input.name,60,next.name);
    if (Object.hasOwn(palettes,input.palette)) next.palette = input.palette;
    for (const key of ['bg','header','ink','accent','muted','surface']) next.colors[key] = color(input.colors?.[key],next.colors[key]);
    next.colors.name = palettes[next.palette].name;
    next.radius = number(input.radius,0,32,next.radius);
    if (Array.isArray(input.pageNames)) next.pageNames = next.pageNames.map((name,index) => string(input.pageNames[index],20,name));
    if(next.pageNames[5]==='留言板')next.pageNames[5]='课程表';
    if(input.weatherStyle!=='weather-board-v1')next.pageNames[1]='天气';
    if(input.holidayStyle!=='holiday-board-v1')next.pageNames[2]='放假倒计时';
    if(input.readingStyle!=='reading-board-v1')next.pageNames[3]='名言与诗句';
    next.reading.autoUpdate=typeof input.reading?.autoUpdate==='boolean'?input.reading.autoUpdate:next.reading.autoUpdate;
    for(const key of ['quoteMode','poemMode'])next.reading[key]=input.reading?.[key]==='manual'?'manual':'api';
    for(const key of ['background','quoteColor','quoteInk','poemColor','poemInk'])next.reading[key]=color(input.reading?.[key],next.reading[key]);
    for(const key of ['quoteText','quoteAuthor','poemText','poemTitle','poemAuthor','poemDynasty'])next.reading[key]=string(input.reading?.[key],key==='poemText'?24000:key==='quoteText'?3000:100,next.reading[key]);
    for(const kind of ['quote','poem']) {
      const cached=input.readingCache?.[kind];
      if(!cached || typeof cached.text!=='string' || !cached.text.trim() || !/^\d{4}-\d{2}-\d{2}$/.test(cached.date) || !Number.isFinite(cached.updatedAt) || cached.updatedAt<0 || cached.updatedAt>8640000000000000)continue;
      const date=new Date(cached.date+'T00:00:00Z');if(!Number.isFinite(date.getTime())||date.toISOString().slice(0,10)!==cached.date)continue;
      next.readingCache[kind]={text:string(cached.text,kind==='poem'?24000:3000,''),author:string(cached.author,100,''),title:string(cached.title,100,''),dynasty:string(cached.dynasty,100,''),date:cached.date,updatedAt:cached.updatedAt,source:'api'};
      if(cached.provider===(kind==='quote'?'uapis':'palemoky'))next.readingCache[kind].provider=cached.provider;
    }
    const oldApps=input.dailyStyle==='daily-board-v1'?5:4;
    if(input.dailyStyle!=='daily-board-v1')next.pageNames[4]=defaults.pageNames[4];
    if(input.deskStyle!=='seven-pages-v1'){next.pageNames[5]='留言板';next.pageNames[6]=string(input.pageNames?.[oldApps],20,'全部应用');}
    if(input.messageStyle!=='yellow-paper-v3'&&next.pageNames[5]==='留言')next.pageNames[5]='留言板';
    next.clockMode.preset=['landscape','matrix','flip'].includes(input.clockMode?.preset)?input.clockMode.preset:next.clockMode.preset;
    next.clockMode.seconds=true;
    next.deviceSettings.brightness=Math.round(number(input.deviceSettings?.brightness,10,100,100));
    next.deviceSettings.volume=Math.round(number(input.deviceSettings?.volume,0,100,45));
    next.deviceSettings.autoHome=typeof input.deviceSettings?.autoHome==='boolean'?input.deviceSettings.autoHome:true;
    next.daily.waterGoal=Math.round(number(input.daily?.waterGoal,100,10000,next.daily.waterGoal));
    next.daily.sound=typeof input.daily?.sound==='boolean'?input.daily.sound:next.daily.sound;
    for(const key of ['background','cardColor'])next.daily[key]=color(input.daily?.[key],next.daily[key]);
    next.daily.mascot=asset(input.daily?.mascot)||next.daily.mascot;
    if(input.dailyRecords && typeof input.dailyRecords==='object' && !Array.isArray(input.dailyRecords)) {
      for(const [date,record] of Object.entries(input.dailyRecords)) {
        if(!/^\d{4}-\d{2}-\d{2}$/.test(date)||!record||typeof record!=='object')continue;
        const instant=new Date(date+'T00:00:00Z');if(!Number.isFinite(instant.getTime())||instant.toISOString().slice(0,10)!==date)continue;
        const water=Array.isArray(record.water)?record.water.filter(item=>Number.isInteger(item?.amount)&&item.amount>0&&item.amount<=3000&&Number.isFinite(item.at)&&item.at>=-8640000000000000&&item.at<=8640000000000000).slice(0,200).map(item=>({amount:item.amount,at:item.at})):[];
        next.dailyRecords[date]={mood:moods.some(item=>item.id===record.mood)?record.mood:'',note:string(record.note,3000,''),knocks:Math.round(number(record.knocks,0,999999,0)),water};
      }
    }
    for (const key of Object.keys(next.home)) next.home[key] = typeof next.home[key] === 'boolean' ? (typeof input.home?.[key] === 'boolean' ? input.home[key] : next.home[key]) : string(input.home?.[key],key === 'footer' ? 60 : 40,next.home[key]);
    next.home.clockColor = color(input.home?.clockColor,next.home.clockColor);
    const weather = input.weather || {condition:input.home?.weather,temperature:Number(input.home?.temperature),city:input.home?.city};
    next.weather.city = string(weather.city,50,'').trim();
    next.weather.condition = string(weather.condition,20,next.weather.condition);
    next.weather.temperature = weather.temperature === null ? null : number(weather.temperature,-99,99,next.weather.temperature);
    for (const key of ['feelsLike','low','high']) next.weather[key] = nullable(weather[key],-99,99);
    next.weather.humidity = nullable(weather.humidity,0,100);
    next.weather.wind = string(weather.wind,60,'');
    for(const key of ['sunrise','sunset'])next.weather[key]=/^([01]\d|2[0-3]):[0-5]\d$/.test(weather[key])?weather[key]:null;
    next.weather.pressure=nullable(weather.pressure,0,2000);
    next.weather.uv=nullable(weather.uv,0,30);
    next.weather.visibility=nullable(weather.visibility,0,999);
    next.weather.aqi=nullable(weather.aqi,0,1000);
    next.weather.aqiLabel=string(weather.aqiLabel,20,'');
    next.weather.updatedAt = nullable(weather.updatedAt,0,8640000000000000);
    next.weather.source = weather.source === 'device' ? 'device' : 'preview';
    if (Array.isArray(weather.forecast)) next.weather.forecast = weather.forecast.slice(0,7).map(item=>{
      const date=string(item?.date,10,'');
      const parts=/^(\d{4})-(\d{2})-(\d{2})$/.exec(date);
      const validDate=parts && Number(parts[2])>=1 && Number(parts[2])<=12 && Number(parts[3])>=1 && Number(parts[3])<=new Date(Date.UTC(Number(parts[1]),Number(parts[2]),0)).getUTCDate();
      return {date:validDate?date:null,label:string(item?.label,20,''),condition:string(item?.condition,20,''),low:nullable(item?.low,-99,99),high:nullable(item?.high,-99,99)};
    });
    next.battery.present = typeof input.battery?.present === 'boolean' ? input.battery.present : next.battery.present;
    next.battery.externalPower = typeof input.battery?.externalPower === 'boolean' ? input.battery.externalPower : next.battery.externalPower;
    next.battery.percent = input.battery?.percent === null ? null : Math.round(number(input.battery?.percent,0,100,next.battery.percent));
    next.battery.charging = input.battery?.charging === null ? null : typeof input.battery?.charging === 'boolean' ? input.battery.charging : next.battery.charging;
    const calendarMatch = /^(\d{4})-(\d{2})-(\d{2})$/.exec(next.home.calendarDate);
    if (!calendarMatch) throw new Error('预览日期需要是有效的 YYYY-MM-DD。');
    const [,cy,cm,cd] = calendarMatch.map(Number);
    if (cy<1900 || cy>2099 || cm<1 || cm>12 || cd<1 || cd>new Date(Date.UTC(cy,cm,0)).getUTCDate()) throw new Error('预览日期无效，支持 1900～2099 年。');
    for (const key of Object.keys(next.postcard)) next.postcard[key] = asset(input.postcard?.[key]) || next.postcard[key];
    if (!/^([01]\d|2[0-3]):[0-5]\d$/.test(next.home.time)) throw new Error('预览时间需要是有效的 HH:MM，例如 08:26。');
    for (const key of ['title','text','signature']) next.letter[key] = string(input.letter?.[key],key === 'text' ? 3000 : 80,next.letter[key]);
    for (const key of ['title','text','signature']) next.keepsake[key] = string(input.keepsake?.[key],key==='text'?3000:80,next.keepsake[key]);
    if(input.keepsake?.date){const date=string(input.keepsake.date,10,''),parts=/^(\d{4})-(\d{2})-(\d{2})$/.exec(date);if(!parts||Number(parts[1])<1900||Number(parts[1])>2099||!Number.isFinite(Date.parse(date+'T00:00:00Z'))||new Date(date+'T00:00:00Z').toISOString().slice(0,10)!==date)throw new Error('纪念日期需要是有效的 YYYY-MM-DD，支持 1900～2099 年。');next.keepsake.date=date;}
    next.holiday.weekday = Math.round(number(input.holiday?.weekday,0,6,next.holiday.weekday));
    next.holiday.label = string(input.holiday?.label,4,next.holiday.label).trim() || '放假';
    for(const key of ['background','cardColor','textColor','numberColor','unitColor']) next.holiday[key]=color(input.holiday?.[key],next.holiday[key]);
    if(input.countdown===null)next.countdown=null;
    else {
    next.countdown.name = string(input.countdown?.name,40,next.countdown.name);
    next.countdown.date = string(input.countdown?.date,16,next.countdown.date);
    const match = /^(\d{4})-(\d{2})-(\d{2})T(\d{2}):(\d{2})$/.exec(next.countdown.date);
    if (!match) throw new Error('倒计时需要有效的日期和时间。');
    const [,year,month,day,hour,minute] = match.map(Number);
    if (year < 1900 || year > 2099 || month < 1 || month > 12 || day < 1 || day > new Date(Date.UTC(year,month,0)).getUTCDate() || hour > 23 || minute > 59) throw new Error('倒计时日期无效，支持 1900～2099 年。');
    }
    next.artwork = asset(input.artwork) || next.artwork;
    next.background = asset(input.background);
    next.photoCaption = string(input.photoCaption,100,next.photoCaption);
    if (['cover','contain'].includes(input.photoFit)) next.photoFit = input.photoFit;
    // Album support has been retired; old imports keep the other pages and records.
    next.photos=[];
    layers.forEach((items,page) => items.forEach(definition => {
      if (page===0 && input.homeStyle!=='postcard-v1') return;
      if (page===1 && input.weatherStyle!=='weather-board-v1') return;
      if (page===2 && input.holidayStyle!=='holiday-board-v1') return;
      if (page===3 && input.readingStyle!=='reading-board-v1') return;
      if (page===4 && input.dailyStyle!=='daily-board-v1') return;
      // Retain saved note contents while replacing obsolete bitmap-bound layouts.
      if (page===5 && (input.deskStyle!=='seven-pages-v1'||input.messageStyle!=='yellow-paper-v3')) return;
      const sourcePage=page===6 && input.deskStyle!=='seven-pages-v1'?oldApps:page;
      const incoming = input.layouts?.[sourcePage]?.[definition.id] || (page===0 && definition.id==='battery' ? input.layouts?.[page]?.apps : null);
      if (!incoming || typeof incoming !== 'object') return;
      const target = next.layouts[page][definition.id];
      target.w = number(incoming.w,12,320,target.w);
      target.h = number(incoming.h,12,240,target.h);
      target.x = number(incoming.x,0,320-target.w,target.x);
      target.y = number(incoming.y,0,240-target.h,target.y);
      target.size = number(incoming.size,page===1||page===5?6:8,72,target.size);
      target.opacity = number(incoming.opacity,0,100,target.opacity);
      target.visible = typeof incoming.visible === 'boolean' ? incoming.visible : target.visible;
      if (incoming.color) target.color = color(incoming.color,next.colors.ink);
    }));
    if (input.homeStyle!=='postcard-v1') next.home.liveClock=true;
    if (JSON.stringify(next).length > 4800000) throw new Error('设计素材超过 4.8 MB，请缩小自定义素材。');
    return next;
  }
  window.YiyiDesign = {clone,layers,assets,palettes,moods,defaults,validate};
})();

'use strict';
const screen = document.getElementById('screen');
const status = document.getElementById('status');
const themes = [
  {id:'forest', name:'森林小黑'},
  {id:'letter', name:'小黑信笺'},
  {id:'night', name:'月夜小黑'}
];
const initialMessages = [
  {id:'welcome', title:'今天的小纸条', text:'希望这个小小的桌面，\n能装下一些属于我们的回忆。\n愿你每天都有一点开心。', signature:'一封示例留言'},
  {id:'birthday', title:'生日快乐', text:'祝你生日快乐！\n愿这一年有温暖、有惊喜，\n也有小黑陪伴的每一天。', signature:'生日留言示例'},
  {id:'ordinary', title:'普通的一天', text:'普通的一天，\n因为你变得特别。', signature:'日常留言示例'}
];
const state = {
  page:0, app:null, section:null, photo:0, voice:false, previous:null,
  theme:'forest', draftTheme:'forest', messageId:'welcome', draftMessageId:'welcome',
  messages:initialMessages.slice(), importedMessages:null, brightness:85, volume:45,
  autoHome:true, background:true, region:'待设置地区', wifi:false, slideshow:false,
  tf:true, calendarYear:0, calendarMonth:0, selectedDate:'',
  countdown:null, draftCountdownName:'', draftCountdownDate:'', designMode:true
};
const names = ['主页', '相册', '留言', '倒计时', '全部应用'];
let appsPage = names.length - 1;
let photos = demoPhotos();
let idleTimer, slideTimer, pointerStart = null, suppressClickUntil = 0;
let fileGeneration = 0, messageGeneration = 0;
function demoPhotos() {
  return [
    {name:'001_一起走过的风景.jpg', caption:'那天的风，和你一样温柔。', color:'#e0e9d9', demo:true},
    {name:'002_藏在日常里的快乐.jpg', caption:'普通的一天，因为你变得特别。', color:'#e8dfd4', demo:true},
    {name:'003_下一段回忆.jpg', caption:'还有很多地方，想和你一起去。', color:'#dbe5ed', demo:true}
  ];
}
function esc(value) {
  return String(value ?? '').replace(/[&<>"']/g, char => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[char]));
}
function cat() {
  return '<svg viewBox="0 0 130 140" aria-label="小黑主题占位插画" role="img"><path d="M93 103 Q127 81 122 118 Q117 140 91 126" fill="none" stroke="#192a29" stroke-width="12" stroke-linecap="round"/><ellipse cx="67" cy="106" rx="32" ry="29" fill="#192a29"/><path d="M30 50 L27 12 L51 33 Q67 27 83 33 L108 12 L105 52 Q110 86 69 87 Q28 86 30 50" fill="#192a29"/><path d="M33 25 L35 44 L46 36 M100 25 L88 36 L100 44" fill="#486056"/><ellipse class="cat-eye" cx="50" cy="57" rx="10" ry="12" fill="#bad36c"/><ellipse class="cat-eye" cx="87" cy="57" rx="10" ry="12" fill="#bad36c"/><ellipse cx="52" cy="57" rx="3" ry="9" fill="#192a29"/><ellipse cx="85" cy="57" rx="3" ry="9" fill="#192a29"/><path d="M64 72 Q69 77 74 72" fill="none" stroke="#9eb7a5" stroke-width="2" stroke-linecap="round"/><ellipse cx="48" cy="126" rx="13" ry="7" fill="#192a29"/><ellipse cx="84" cy="126" rx="13" ry="7" fill="#192a29"/></svg>';
}
function icon(name) {
  const paths = {
    clock:'<circle cx="12" cy="12" r="9"/><path d="M12 6v6l4 2"/>',
    album:'<rect x="3" y="4" width="18" height="16" rx="3"/><circle cx="8" cy="9" r="1.5"/><path d="m4 18 6-6 4 4 3-3 4 5"/>',
    letter:'<rect x="3" y="5" width="18" height="14" rx="3"/><path d="m4 7 8 6 8-6"/>',
    weather:'<circle cx="12" cy="12" r="4"/><path d="M12 2v2m0 16v2M2 12h2m16 0h2M5 5l1.5 1.5M17.5 17.5 19 19M5 19l1.5-1.5M17.5 6.5 19 5"/>',
    voice:'<rect x="9" y="3" width="6" height="11" rx="3"/><path d="M6 10v2a6 6 0 0 0 12 0v-2M12 18v3M8 21h8"/>',
    calendar:'<rect x="3" y="5" width="18" height="16" rx="3"/><path d="M7 3v4m10-4v4M3 10h18M7 14h2m3 0h2m3 0h1M7 17h2m3 0h2"/>',
    settings:'<path d="M10 2h4l.7 2.8 2.4 1.4 2.8-.8 2 3.4-2.1 2v2.8l2.1 2-2 3.4-2.8-.8-2.4 1.4L14 22h-4l-.7-2.8-2.4-1.4-2.8.8-2-3.4 2.1-2v-2.8l-2.1-2 2-3.4 2.8.8 2.4-1.4Z"/><circle cx="12" cy="12" r="3"/>'
  };
  return '<svg viewBox="0 0 24 24" aria-hidden="true">' + (paths[name] || paths.settings) + '</svg>';
}
function pageTop(label, back = false) {
  return back ? '<div class="screen-navigation"><button class="tiny" data-action="back" aria-label="返回上一页">‹ 返回</button></div>' : '';
}
function timeParts() {
  const now = new Date();
  return {
    clock:new Intl.DateTimeFormat('zh-CN',{timeZone:'Asia/Shanghai',hour:'2-digit',minute:'2-digit',hourCycle:'h23'}).format(now),
    date:new Intl.DateTimeFormat('zh-CN',{timeZone:'Asia/Shanghai',month:'long',day:'numeric',weekday:'long'}).format(now)
  };
}
function home() {
  const time = timeParts();
  return pageTop('天气时钟') + '<div class="clock" id="clock">' + time.clock + '</div><div class="date" id="date">' + time.date + '</div><div class="home-cat">' + cat() + '</div><div class="weather-pill"><span>☀ 晴</span><b>24°</b><span>示例天气</span></div><div class="home-bottom"><span>今天也要开心呀。</span><button class="album-entry" data-action="album">相册</button></div>';
}
function album(back = false) {
  const photo = photos[state.photo];
  if (!state.tf || !photo) return pageTop('回忆相册',back) + '<div class="empty">' + (!state.tf ? '请插入 TF 卡' : '相册里还没有照片') + '<br><button class="mini-btn" data-action="photo-refresh">重新扫描</button></div>';
  const visual = photo.demo ? cat() + '<span class="placeholder-label">照片占位</span>' : '<img src="' + esc(photo.url) + '" alt="' + esc(photo.name) + '">';
  return pageTop('相册 · ' + (state.photo + 1) + '/' + photos.length,back) +
    '<div class="photo-view" style="background:' + (photo.color || '#192a29') + '">' + visual + '</div>' +
    '<div class="photo-caption">' + esc(photo.caption || photo.name) + '</div>' +
    '<div class="photo-controls"><button class="tiny" data-action="photo-prev">‹ 上一张</button><button class="tiny" data-action="slideshow">' + (state.slideshow ? '暂停轮播' : '轮播') + '</button><button class="tiny" data-action="photo-next">下一张 ›</button>' +
    (back ? '<button class="tiny" data-action="album-manage">管理</button>' : '') + '</div>';
}
function chosenMessage() {
  return state.messages.find(message => message.id === state.messageId) || null;
}
function letter() {
  const message = chosenMessage();
  return pageTop('小黑的留言') + '<h2 class="letter-title">' + esc(message?.title || '还没有留言') + '</h2>' +
    '<div class="letter-content"><p>' + esc(message?.text || '从全部应用打开留言，读取 TF 卡并选择一条留言。') + '</p><div class="sign">' + esc(message?.signature || '') + '</div></div>' +
    '<div class="letter-cat">' + cat() + '</div>';
}
function apps() {
  const items = [[window.YiyiDeviceUI?'clock':'album',window.YiyiDeviceUI?'时钟模式':'相册'],['letter','留言'],['weather','天气'],['voice','小智'],['calendar','日历'],['settings','设置']];
  return pageTop('全部应用') + '<div class="app-grid">' + items.map(([name,label]) => '<button class="app" data-action="app-' + name + '"><span>' + icon(name) + '</span>' + label + '</button>').join('') + '</div>';
}
function voice() {
  return '<div class="official-voice-preview"><div class="official-voice-status">官方小智 · 网页预览</div><button data-action="exit-voice" class="official-voice-exit">退出</button><div class="official-voice-emotion">🙂</div><div class="official-voice-caption">在设备上进入官方小智语音对话</div></div>';
}
function scroll(content) { return '<div class="scroll-body">' + content + '</div>'; }
function chinaDateKey(now = new Date()) {
  const parts = new Intl.DateTimeFormat('en-CA',{
    timeZone:'Asia/Shanghai',year:'numeric',month:'2-digit',day:'2-digit'
  }).formatToParts(now);
  const value = type => parts.find(part => part.type === type).value;
  return value('year') + '-' + value('month') + '-' + value('day');
}
function dateKey(year,month,day) {
  return String(year).padStart(4,'0') + '-' + String(month + 1).padStart(2,'0') + '-' + String(day).padStart(2,'0');
}
function monthInfo(year,month) {
  return {days:new Date(Date.UTC(year,month + 1,0)).getUTCDate(),
    offset:(new Date(Date.UTC(year,month,1)).getUTCDay() + 6) % 7};
}
function goToday() {
  state.selectedDate = chinaDateKey();
  const [year,month] = state.selectedDate.split('-').map(Number);
  state.calendarYear = year; state.calendarMonth = month - 1;
}
function moveMonth(direction) {
  const date = new Date(Date.UTC(state.calendarYear,state.calendarMonth + direction,1));
  if (date.getUTCFullYear() < 1900 || date.getUTCFullYear() > 2099) return;
  state.calendarYear = date.getUTCFullYear(); state.calendarMonth = date.getUTCMonth();
}
function calendar() {
  if (state.section === 'countdown') return countdownEditor();
  const {days,offset} = monthInfo(state.calendarYear,state.calendarMonth);
  const today = chinaDateKey();
  const targetDate = state.countdown?.date.slice(0,10);
  let cells = '';
  for (let index = 0; index < 42; index++) {
    const day = index - offset + 1;
    if (day < 1 || day > days) { cells += '<span class="calendar-empty"></span>'; continue; }
    const key = dateKey(state.calendarYear,state.calendarMonth,day);
    cells += '<button class="calendar-day' + (key === today ? ' today' : '') +
      (key === state.selectedDate ? ' selected' : '') + (key === targetDate ? ' target' : '') +
      '" data-action="day-' + key + '" aria-label="' + key + (key === targetDate ? '，倒计时目标日' : '') +
      '" aria-pressed="' + (key === state.selectedDate) + '">' + day + '</button>';
  }
  return pageTop('日历',true) + '<div class="calendar-nav"><button class="tiny" data-action="month-prev" aria-label="上一月"' +
    (state.calendarYear === 1900 && state.calendarMonth === 0 ? ' disabled' : '') + '>‹</button><b>' +
    state.calendarYear + ' 年 ' + (state.calendarMonth + 1) + ' 月</b><button class="tiny" data-action="calendar-today">今天</button><button class="tiny" data-action="month-next" aria-label="下一月"' +
    (state.calendarYear === 2099 && state.calendarMonth === 11 ? ' disabled' : '') + '>›</button></div>' +
    '<div class="calendar-week">' + ['一','二','三','四','五','六','日'].map(day => '<span>' + day + '</span>').join('') +
    '</div><div class="calendar-grid">' + cells + '</div><div class="calendar-footer"><span>' +
    esc(state.selectedDate) + '</span><button class="mini-btn" data-action="edit-countdown">设置倒计时</button></div>';
}
function countdownEditor() {
  return pageTop('日历 · 自定义倒计时',true) + scroll(
    '<label class="field">名称<input id="countdown-name" maxlength="40" value="' + esc(state.draftCountdownName) +
    '" placeholder="例如：生日、见面、旅行"></label>' +
    '<label class="field">目标日期和时间（北京时间）<input id="countdown-date" type="datetime-local" min="1900-01-01T00:00" max="2099-12-31T23:59" value="' +
    esc(state.draftCountdownDate) + '"></label>' +
    (window.readingPage && state.countdown ? '<button class="calendar-countdown-summary" data-action="view-countdown">'+esc(state.countdown.name)+'<span id="countdown-readout">'+countdownReadout()+'</span></button>' : '') +
    '<div class="album-tools"><button class="mini-btn" data-action="save-countdown">'+(window.readingPage?'保存倒计时':'应用到第四页')+'</button>' +
    (state.countdown ? '<button class="mini-btn" data-action="clear-countdown">清除倒计时</button>' : '') +
    '</div><p class="hint">当前设置一个目标。到时显示“已到达”，不自动跳到下一年；点击应用前保留原设置。</p>'
  );
}
function parseCountdownDate(value) {
  const match = /^(\d{4})-(\d{2})-(\d{2})T(\d{2}):(\d{2})$/.exec(value);
  if (!match) return NaN;
  const [,year,month,day,hour,minute] = match.map(Number);
  if (year < 1900 || year > 2099 || month < 1 || month > 12 ||
      day < 1 || day > monthInfo(year,month - 1).days || hour > 23 || minute > 59) return NaN;
  // 输入明确为北京时间，避免受到浏览器本地时区影响。
  return Date.UTC(year,month - 1,day,hour - 8,minute);
}
function countdownParts(target,now = Date.now()) {
  const seconds = Math.max(0,Math.ceil((target - now) / 1000));
  return {done:target <= now,days:Math.floor(seconds / 86400),
    hours:Math.floor(seconds / 3600) % 24,minutes:Math.floor(seconds / 60) % 60,seconds:seconds % 60};
}
function countdownReadout() {
  if (!state.countdown) return '';
  const parts = countdownParts(state.countdown.target);
  const two = value => String(value).padStart(2,'0');
  return parts.done ? '<div class="countdown-done">已到达</div><p class="sub">期待的日子到了。</p>' :
    '<div class="countdown-number">' + parts.days + '<small> 天</small></div><div class="countdown-time">' +
    two(parts.hours) + ' 时 ' + two(parts.minutes) + ' 分 ' + two(parts.seconds) + ' 秒</div>';
}
function countdown() {
  return pageTop('小黑的期待') + '<div class="countdown-cat">' + cat() + '</div>' +
    (state.countdown ? '<h2 class="countdown-name">' + esc(state.countdown.name) + '</h2><div id="countdown-readout">' +
    countdownReadout() + '</div><p class="countdown-target">' + esc(state.countdown.date.replace('T',' ')) + ' · 北京时间</p>' :
    '<h2 class="countdown-name">下一件期待的事</h2><div class="countdown-empty">还没有设置倒计时</div><p class="countdown-target">生日、见面，或一场旅行。</p>') +
    '<button class="mini-btn countdown-edit" data-action="countdown-settings">设置倒计时</button>';
}
function noteManager() {
  const selected = state.messages.find(message => message.id === state.draftMessageId);
  return pageTop('留言 · 主题与内容',true) + scroll(
    '<div class="hint">选择' + (window.holidayPage?'留言阅读':'第三页') + '的小黑主题</div><div class="theme-picker">' +
    themes.map(theme => '<button data-action="theme-' + theme.id + '" class="' + (state.draftTheme === theme.id ? 'selected' : '') + '">' + theme.name + '</button>').join('') +
    '</div><div class="hint">选择要展示的留言 · 普通中文字体</div>' +
    (state.messages.length ? state.messages.map(message => '<button class="message-option ' + (message.id === state.draftMessageId ? 'selected' : '') + '" data-message-id="' + esc(message.id) + '">' + esc(message.title) + '</button>').join('') : '<p class="hint">没有可用留言，请读取 TF 卡。</p>') +
    (selected ? '<div class="message-preview">' + esc(selected.text) + '</div>' : '') +
    '<div class="album-tools"><button class="mini-btn" data-action="apply-message">' + (window.holidayPage?'阅读这条留言':'应用到第三页') + '</button><button class="mini-btn" data-action="refresh-messages">刷新 TF 留言</button></div>'
  );
}
function weather() {
  if (state.section === 'region') return pageTop('天气 · 设置地区',true) + scroll(
    '<p class="hint">用手机页面获取位置后，选择天气地区。也可以直接填写城市或区县。</p>' +
    '<label class="field">城市 / 区县<input id="region" maxlength="50" value="' + esc(state.region === '待设置地区' ? '' : state.region) + '" placeholder="例如：杭州 · 西湖区"></label>' +
    '<button class="mini-btn" data-action="save-region">保存地区</button>' +
    '<button class="mini-btn" data-action="phone-location">手机定位说明</button>'
  );
  return pageTop('详细天气',true) + scroll(
    '<div class="weather-hero">' + icon('weather') + '<b>24°</b><span class="sub">晴<br>' + esc(state.region) + '</span></div>' +
    '<div class="weather-info"><span>体感 25°</span><span>湿度 55%</span><span>东北风 2 级</span><span>19° ～ 27°</span></div>' +
    '<div class="forecast"><span>今天<br>晴 · 19/27°</span><span>明天<br>多云 · 20/26°</span><span>后天<br>小雨 · 18/23°</span></div>' +
    '<div class="album-tools"><button class="mini-btn" data-action="weather-refresh">刷新</button><button class="mini-btn" data-action="set-region">设置地区</button></div><p class="hint">示例数据 · 最终显示真实更新时间</p>'
  );
}
function settings() {
  if (!state.section) {
    const entries = [
      ['display','屏幕亮度',state.brightness + '%'],
      ['wifi','Wi-Fi',state.wifi ? '演示：已连接' : '手机配网'],
      ['phone','连接手机','配置与地区'],
      ['volume','音量',state.volume + '%'],
      ['tf','TF 卡信息',state.tf ? '已插入（示例）' : '未插入'],
      ['about','关于','板型与版本'],
      ['bluetooth','蓝牙','用途待定']
    ];
    return pageTop('设置',true) + scroll('<div class="settings-list">' + entries.map(([name,title,value]) => '<button class="row-btn" data-action="setting-' + name + '"><span>' + title + '</span><small>' + esc(value) + ' ›</small></button>').join('') + '</div>');
  }
  const labels = {display:'显示',wifi:'Wi-Fi',phone:'连接手机',volume:'音量',tf:'TF 卡信息',about:'关于',bluetooth:'蓝牙'};
  let content = '';
  if (state.section === 'display') content =
    '<label class="field">屏幕亮度 <b id="brightness-value">' + state.brightness + '%</b><input id="brightness" type="range" min="10" max="100" value="' + state.brightness + '"></label>' +
    '<label class="field">闲置 60 秒回主页<input id="auto-home" type="checkbox" ' + (state.autoHome ? 'checked' : '') + '></label>' +
    '<label class="field">应用页主题背景<input id="background" type="checkbox" ' + (state.background ? 'checked' : '') + '></label><p class="hint">关闭背景后为纯白。实板亮度通过背光控制，当前为视觉预览。</p>';
  if (state.section === 'volume') content = '<label class="field">音量 <b id="volume-value">' + state.volume + '%</b><input id="volume" type="range" min="0" max="100" value="' + state.volume + '"></label><p class="hint">当前不播放声音。最终用于小智语音回复；0% 为静音。</p>';
  if (state.section === 'wifi') content = '<p class="hint">使用手机配网，连接 2.4GHz Wi-Fi。开机自动重连。</p><div class="about-lines">状态：' + (state.wifi ? '演示已连接' : '未连接（演示）') + '</div><button class="mini-btn" data-action="wifi-toggle">' + (state.wifi ? '模拟断开' : '模拟配网成功') + '</button><p class="hint">实际配网沿用小智支持的方式。</p>';
  if (state.section === 'phone') content = '<h2>手机配置页面</h2><p class="hint">设备联网后，手机进入配置页面，修改城市、亮度等设置。首次联网使用 Wi-Fi 配网。</p><button class="mini-btn" data-action="phone-location">地区定位流程</button><p class="hint">这是流程预览，连接地址和二维码待固件实现。</p>';
  if (state.section === 'bluetooth') content = '<h2>蓝牙用途待定</h2><p class="hint">S3 支持 BLE，可按需要用于配网或控制。第一版先使用 Wi-Fi 连接手机。</p><p class="hint">S3 不支持经典蓝牙，不能直接按普通蓝牙音箱方式使用。</p>';
  if (state.section === 'tf') content = state.tf ?
    '<div class="about-lines">状态：已插入（示例）<br>总容量：16 GB（示例）<br>已用：2.7 GB（示例）<br>剩余：13.3 GB（示例）</div><div class="storage-bar"><i></i></div><p class="hint">照片索引：' + photos.length + ' 项。实板显示文件系统统计容量。</p><button class="mini-btn" data-action="tf-toggle">模拟拔卡</button>' :
    '<p class="hint">未插入 TF 卡，相册不可用。已载入的留言继续显示。</p><button class="mini-btn" data-action="tf-toggle">模拟插卡</button>';
  if (state.section === 'about') content = '<div class="about-lines">小黑的桌面 · 设计版 0.4<br>主控：ESP32-S3<br>屏幕：2.8 英寸 · 240×320<br>触摸：电容触摸<br>Flash：标称 16MB，待实测<br>PSRAM：待实测<br>框架：基于小智开源固件扩展<br>屏幕 / 触摸驱动：待资料确认</div>';
  return pageTop('设置 · ' + labels[state.section],true) + scroll(content);
}
function albumManager() {
  return pageTop('相册 · 管理',true) + scroll('<p class="hint">从 /photos/ 扫描 JPEG，按文件名自然排序。记住当前照片，支持手动刷新。</p>' +
    '<div class="about-lines">索引照片：' + photos.length + ' 张<br>模式：' + (photos.some(photo => photo.demo) ? '示例素材' : '本地照片预览') + '</div>' +
    '<div class="album-tools"><button class="mini-btn" data-action="photo-refresh">重新扫描</button><button class="mini-btn" data-action="restore-demo">恢复示例</button></div><p class="hint">浏览器可在右侧导入文件夹。不会修改或删除原照片。</p>');
}
function render() {
  let html, cls = '';
  if (state.voice) { html = voice(); cls = 'voice'; }
  else if (window.YiyiDeviceUI && ['clock','settings'].includes(state.app)) html=window.YiyiDeviceUI.app();
  else if (state.app === 'album') html = state.section === 'manage' ? albumManager() : album(true);
  else if (state.app === 'letter') html = state.section === 'read' ? '<div class="letter-reader">'+pageTop('留言',true)+letter()+'</div>' : noteManager();
  else if (state.app === 'weather') html = weather();
  else if (state.app === 'calendar') html = calendar();
  else if (state.app === 'settings') html = settings();
  else {
    html = [home,window.weatherPage || album,window.holidayPage || letter,window.readingPage || countdown,...(window.dailyPage?[window.dailyPage]:[]),...(window.messagePage?[window.messagePage]:[]),apps][state.page]();
    if (state.page === 0) cls = 'home-page';
    if (state.page === 1) cls = window.weatherPage ? 'weather-page' : 'album-widget';
    if (state.page === 2) cls = window.holidayPage ? 'holiday-page' : 'theme-letter ' + state.theme;
    if (state.page === 3) cls = window.readingPage ? 'reading-page' : 'countdown-page';
    if (state.page === 4 && window.dailyPage) cls = 'daily-page';
    if (state.page === 5 && window.messagePage) cls = 'schedule-page';
    if (state.page === appsPage) cls = state.background ? 'apps-forest' : 'apps-plain';
  }
  screen.style.filter = 'brightness(' + state.brightness / 100 + ')';
  screen.innerHTML = '<div class="page ' + cls + '">' + html +
    (!state.voice && !state.app ? '<div class="dots">' + names.map((name,index) => '<button aria-label="' + name + '" class="dot ' + (index === state.page ? 'active' : '') + '" data-action="page-' + index + '"></button>').join('') + '</div>' : '') + '</div>';
  document.getElementById('left').disabled = state.voice;
  document.getElementById('right').disabled = state.voice;
  status.textContent = state.voice ? '官方小智界面预览；退出后返回全部功能。' : state.app ? '应用内使用返回按钮；相册内横滑翻照片，其他页面上下滚动。' : '第 ' + (state.page + 1) + ' 页 · ' + names[state.page] + '。左右滑动循环切页。';
  resetSlide();
  if (typeof updateDesignPanel === 'function') updateDesignPanel();
}
function notify(message) { status.textContent = message; }
function resetIdle() {
  clearTimeout(idleTimer);
  if (!state.voice && state.app!=='clock' && state.autoHome && !state.designMode) idleTimer = setTimeout(returnHome,60000);
}
function returnHome() {
  if (state.voice) { notify('对话期间保持小智界面。'); return; }
  state.page = 0; state.app = null; state.section = null; state.slideshow = false;
  render(); resetIdle();
}
function turnPhoto(direction) {
  if (!state.tf || !photos.length) return;
  state.photo = (state.photo + direction + photos.length) % photos.length;
}
function navigate(direction) {
  if (state.voice) return;
  if (state.app === 'album' && !state.section) turnPhoto(direction);
  else if (state.app) return;
  else state.page = (state.page + direction + names.length) % names.length;
  render(); resetIdle();
}
function resetSlide() {
  clearTimeout(slideTimer);
  if (state.slideshow && state.tf && photos.length > 1 && !state.voice &&
      ((state.app === 'album' && !state.section) || (!window.weatherPage && !state.app && state.page === 1))) {
    slideTimer = setTimeout(() => { turnPhoto(1); render(); },5000);
  }
}
function startVoice() {
  if (state.voice) return;
  state.previous = {page:state.page,app:state.app,section:state.section,photo:state.photo,slideshow:state.slideshow};
  state.voice = true; render(); resetIdle();
}
function exitVoice() {
  state.voice = false;
  state.page = appsPage; state.app = null; state.section = null;
  state.previous = null;
  state.photo = Math.max(0,Math.min(state.photo,photos.length - 1));
  render(); resetIdle();
}
function openApp(name) {
  if (name === 'voice') { startVoice(); return; }
  state.app = name; state.section = null; state.slideshow = false;
  if (name === 'letter') { state.draftTheme = state.theme; state.draftMessageId = state.messageId; }
  render(); resetIdle();
}
function back() {
  if (state.section) state.section = null;
  else { state.app = null; state.page = appsPage; state.slideshow = false; }
}
function validateMessages(value) {
  if (!Array.isArray(value) || value.length > 100) throw new Error('留言文件需要是最多 100 条记录的 JSON 数组。');
  const ids = new Set();
  return value.map((item,index) => {
    if (!item || typeof item !== 'object' || typeof item.id !== 'string' ||
        !item.id.trim() || item.id.length > 80 || ids.has(item.id) ||
        typeof item.title !== 'string' || !item.title.trim() || item.title.length > 80 ||
        typeof item.text !== 'string' || item.text.length > 3000 ||
        (item.signature !== undefined && (typeof item.signature !== 'string' || item.signature.length > 80))) {
      throw new Error('第 ' + (index + 1) + ' 条格式无效：需要唯一 id、title、text，以及可选 signature。');
    }
    ids.add(item.id);
    return {id:item.id,title:item.title,text:item.text,signature:item.signature || ''};
  });
}
function refreshMessages() {
  if (!state.tf) { notify('TF 卡未插入，保留已载入的留言。'); return; }
  const next = (state.importedMessages || initialMessages).map(message => ({...message}));
  state.messages = next;
  if (!next.some(message => message.id === state.messageId)) state.messageId = next[0]?.id || null;
  if (!next.some(message => message.id === state.draftMessageId)) state.draftMessageId = state.messageId;
  render(); resetIdle();
  notify('已刷新 ' + next.length + ' 条留言。实板将读取 /messages/messages.json。');
}
function dispatch(action) {
  if (action === 'exit-voice') { exitVoice(); return; }
  if (state.voice) return;
  let feedback = '';
  if (action === 'album') { state.page = 1; state.app = null; state.section = null; }
  else if (action === 'photo-prev') turnPhoto(-1);
  else if (action === 'photo-next') turnPhoto(1);
  else if (action === 'slideshow') state.slideshow = !state.slideshow;
  else if (action === 'back') back();
  else if (action === 'month-prev') moveMonth(-1);
  else if (action === 'month-next') moveMonth(1);
  else if (action === 'calendar-today') goToday();
  else if (action.startsWith('day-')) state.selectedDate = action.slice(4);
  else if (action === 'edit-countdown' || action === 'countdown-settings') {
    const fromCalendar = state.app === 'calendar';
    state.draftCountdownName = state.countdown?.name || '';
    state.draftCountdownDate = fromCalendar ? state.selectedDate + 'T00:00' :
      (state.countdown?.date || state.selectedDate + 'T00:00');
    state.app = 'calendar'; state.section = 'countdown'; state.slideshow = false;
  }
  else if (action === 'save-countdown') {
    const name = document.getElementById('countdown-name').value.trim();
    const date = document.getElementById('countdown-date').value;
    const target = parseCountdownDate(date);
    if (!name || name.length > 40 || !Number.isFinite(target)) {
      notify('请填写名称和有效的目标日期时间（1900～2099 年，北京时间）。'); return;
    }
    state.countdown = {name,date,target}; state.selectedDate = date.slice(0,10);
    state.calendarYear = Number(date.slice(0,4)); state.calendarMonth = Number(date.slice(5,7)) - 1;
    state.app = window.readingPage ? 'calendar' : null; state.section = window.readingPage ? 'countdown-view' : null;
    if(!window.readingPage)state.page = 3;
    feedback = window.readingPage ? '已保存倒计时。' : '已应用倒计时到第四页。';
  }
  else if (action === 'clear-countdown') {
    state.countdown = null; state.draftCountdownName = ''; state.draftCountdownDate = '';
    state.section = null; feedback = '已清除倒计时。';
  }
  else if (action.startsWith('page-')) state.page = Number(action.slice(5));
  else if (action.startsWith('app-')) { openApp(action.slice(4)); return; }
  else if (action.startsWith('theme-')) state.draftTheme = action.slice(6);
  else if (action.startsWith('setting-')) state.section = action.slice(8);
  else if (action === 'apply-message') {
    if (!state.messages.some(message => message.id === state.draftMessageId)) { notify('请先载入并选择一条留言。'); return; }
    state.theme = state.draftTheme; state.messageId = state.draftMessageId;
    if (window.holidayPage) { state.app = 'letter'; state.section = 'read'; }
    else { state.app = null; state.section = null; state.page = 2; }
    feedback = window.holidayPage ? '已打开所选留言。' : '已应用主题和留言到第三页。';
  }
  else if (action === 'refresh-messages') { refreshMessages(); return; }
  else if (action === 'album-manage') state.section = 'manage';
  else if (action === 'photo-refresh') {
    feedback = state.tf ? '预览已刷新当前索引；实板会重新扫描 /photos/。' : 'TF 卡未插入。';
    state.photo = Math.max(0,Math.min(state.photo,photos.length - 1));
  }
  else if (action === 'restore-demo') {
    fileGeneration++; photos.forEach(photo => { if (photo.url) URL.revokeObjectURL(photo.url); });
    photos = demoPhotos(); state.photo = 0;
  }
  else if (action === 'weather-refresh') feedback = '草图使用示例天气；实板刷新成功后才会更新数据和时间。';
  else if (action === 'set-region') state.section = 'region';
  else if (action === 'save-region') {
    const region = document.getElementById('region').value.trim();
    if (!region) { notify('请填写城市或区县。'); return; }
    state.region = region; state.section = null; feedback = '已保存地区；天气仍为示例。';
  }
  else if (action === 'phone-location') {
    feedback = '手机打开配置页 → 主动授权定位 → 确认地区 → 保存到设备。实板需提供安全的定位页面；失败时手动选地区。';
  }
  else if (action === 'wifi-toggle') state.wifi = !state.wifi;
  else if (action === 'tf-toggle') { state.tf = !state.tf; if (!state.tf) state.slideshow = false; }
  render(); resetIdle();
  if (feedback) notify(feedback);
}
screen.addEventListener('click',event => {
  if (Date.now() < suppressClickUntil) return;
  const message = event.target.closest('[data-message-id]');
  if (message && !state.voice) { state.draftMessageId = message.dataset.messageId; render(); resetIdle(); return; }
  const target = event.target.closest('[data-action]');
  if (target) dispatch(target.dataset.action);
  else resetIdle();
});
screen.addEventListener('input',event => {
  if (event.target.id === 'countdown-name') state.draftCountdownName = event.target.value;
  if (event.target.id === 'countdown-date') state.draftCountdownDate = event.target.value;
  if (event.target.id === 'brightness') {
    state.brightness = Number(event.target.value);
    screen.style.filter = 'brightness(' + state.brightness / 100 + ')';
    document.getElementById('brightness-value').textContent = state.brightness + '%';
  }
  if (event.target.id === 'volume') {
    state.volume = Number(event.target.value);
    document.getElementById('volume-value').textContent = state.volume + '%';
  }
  resetIdle();
});
screen.addEventListener('change',event => {
  if (event.target.id === 'auto-home') state.autoHome = event.target.checked;
  if (event.target.id === 'background') state.background = event.target.checked;
  // 不替换输入框 DOM，避免选地区或拖动滑块时丢失焦点。
  resetIdle();
});
screen.addEventListener('pointerdown',event => {
  if (!event.isPrimary || (event.pointerType === 'mouse' && event.button !== 0)) return;
  if (event.target.closest('input,select,textarea,.scroll-body')) { pointerStart = null; resetIdle(); return; }
  pointerStart = {x:event.clientX,y:event.clientY,id:event.pointerId};
  resetIdle();
});
window.addEventListener('pointerup',event => {
  if (!pointerStart || event.pointerId !== pointerStart.id) return;
  const dx = event.clientX - pointerStart.x, dy = event.clientY - pointerStart.y;
  pointerStart = null;
  if (Math.abs(dx) >= 40 && Math.abs(dx) > Math.abs(dy) * 1.3) {
    suppressClickUntil = Date.now() + 300;
    if(state.app==='clock'&&state.section==='clock-face'){state.section=null;render();}
    else navigate(dx > 0 ? 1 : -1);
  }
});
window.addEventListener('pointercancel',() => { pointerStart = null; });
document.addEventListener('keydown',event => {
  if (event.target.closest?.('input,select,textarea')) return;
  if (event.key === 'ArrowRight' || event.key === 'ArrowLeft') {
    event.preventDefault(); navigate(event.key === 'ArrowRight' ? 1 : -1);
  }
});
document.getElementById('left').onclick = () => navigate(-1);
document.getElementById('right').onclick = () => navigate(1);
document.getElementById('wake').onclick = startVoice;
document.getElementById('idle').onclick = returnHome;
async function importPhotos(files) {
  const generation = ++fileGeneration;
  const candidates = Array.from(files).filter(file => /\.(jpe?g|png)$/i.test(file.name))
    .sort((a,b) => (a.webkitRelativePath || a.name).localeCompare(b.webkitRelativePath || b.name,'zh-CN',{numeric:true}));
  const next = [];
  for (const file of candidates.slice(0,200)) {
    if (generation !== fileGeneration) break;
    const url = URL.createObjectURL(file);
    const valid = await new Promise(resolve => {
      const img = new Image();
      const timeout = setTimeout(() => { img.src = ''; resolve(false); },5000);
      img.onload = () => { clearTimeout(timeout); resolve(true); };
      img.onerror = () => { clearTimeout(timeout); resolve(false); };
      img.src = url;
    });
    if (valid) next.push({name:file.webkitRelativePath || file.name,caption:file.name,url,size:file.size});
    else URL.revokeObjectURL(url);
  }
  if (generation !== fileGeneration) { next.forEach(photo => URL.revokeObjectURL(photo.url)); return; }
  const oldName = photos[state.photo]?.name;
  photos.forEach(photo => { if (photo.url) URL.revokeObjectURL(photo.url); });
  photos = next; state.photo = Math.max(0,next.findIndex(photo => photo.name === oldName));
  render(); resetIdle();
  notify('载入 ' + next.length + ' 张照片，按文件名排序。仅在此浏览器预览，不上传。' + (candidates.length > 200 ? '预览最多载入 200 张。' : ''));
}
document.getElementById('photo-files').addEventListener('change',event => importPhotos(event.target.files));
document.getElementById('photo-folder').addEventListener('change',event => importPhotos(event.target.files));
document.getElementById('message-file').addEventListener('change',async event => {
  const file = event.target.files[0]; if (!file) return;
  const generation = ++messageGeneration;
  try {
    if (file.size > 1024 * 1024) throw new Error('预览留言文件请小于 1MB。');
    const text = await file.text();
    const next = validateMessages(JSON.parse(text.replace(/^\uFEFF/,'')));
    if (generation !== messageGeneration) return;
    state.importedMessages = next;
    notify('留言文件已导入。打开留言应用，点击“刷新 TF 留言”载入。');
  } catch (error) { if (generation === messageGeneration) notify(error.message); }
});
if (!window.YiyiData) setInterval(() => {
  const clock = document.getElementById('clock'), date = document.getElementById('date');
  if (typeof refreshHomeWidgets === 'function') refreshHomeWidgets();
  else if (clock && date) { const time = timeParts(); clock.textContent = time.clock; date.textContent = time.date; }
  if (!state.voice && !state.app && state.page === 3 && state.countdown) {
    document.getElementById('countdown-readout').innerHTML = countdownReadout();
  }
},1000);
goToday();
render(); resetIdle();

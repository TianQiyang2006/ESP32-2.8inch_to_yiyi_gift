(function () {
  'use strict';
  const daily=window.YiyiDaily,moods=window.YiyiDesign.moods;
  let artURL=value=>value,audioContext=null,selectedMoodDate=null;
  const name=id=>moods.find(m=>m.id===id)?.name||'未记录';
  function face(id) {
    if(!id)return '<span class="mood-plus" aria-hidden="true"></span>';
    return '<img src="'+esc(artURL('assets/daily-'+id+'.png'))+'" alt="'+name(id)+'" draggable="false">';
  }
  function weekMarkup() {
    return daily.week().map(day=>'<button class="daily-weekday'+(day.today?' today':'')+'" data-action="daily-mood-'+day.key+'"'+(day.future?' disabled':'')+' aria-label="'+day.key+' '+day.label+' '+(day.future?'尚未到来':name(day.mood))+'"><span class="daily-face'+(!day.mood?' unrecorded':'')+'">'+(day.future?'':face(day.mood))+'</span><span>'+day.label+'</span></button>').join('');
  }
  function waterText(summary) {return summary.waterPercent+'% · '+summary.waterGoal+' ml';}
  function header(title,extra='') {return '<div class="daily-panel-nav"><button data-action="back" aria-label="返回第五页">‹ 返回</button><strong>'+esc(title)+'</strong>'+extra+'</div>'; }
  function moodEditor() {
    selectedMoodDate ||= daily.today();
    const record=daily.day(selectedMoodDate);
    return '<div class="daily-panel mood-panel">'+header('选择心情 · '+selectedMoodDate)+'<div class="daily-panel-body"><div class="mood-picker">'+moods.map(item=>'<button data-action="daily-pick-'+item.id+'" aria-pressed="'+(record.mood===item.id)+'">'+face(item.id)+'<span>'+item.name+'</span></button>').join('')+'</div><p class="daily-error" aria-live="polite"></p></div></div>';
  }
  function moodHistory() {
    const entries=Object.entries(daily.snapshot()).filter(([,r])=>r.mood).sort(([a],[b])=>b.localeCompare(a));
    return '<div class="daily-panel">'+header('心情记录')+'<div class="daily-panel-body scroll-body">'+(entries.length?entries.map(([key,r])=>'<div class="daily-history-row">'+face(r.mood)+'<span><b>'+key+' · '+name(r.mood)+'</b></span></div>').join(''):'<p class="hint">还没有心情记录。</p>')+'</div></div>';
  }
  function woodPanel(settings) {
    const summary=daily.summary();
    return '<div class="daily-panel wood-panel">'+header('敲木鱼')+'<div class="daily-panel-body scroll-body"><p class="daily-wood-stats">今日 <b data-daily-knocks>'+summary.knocks+'</b> 次 · 累计 <span data-daily-total>'+summary.totalKnocks+'</span> 次</p><button class="wood-large" data-action="daily-strike" aria-label="敲木鱼一次"><img src="'+esc(artURL('assets/daily-wood.jpg'))+'" alt="木鱼"></button><div class="daily-actions"><button data-action="daily-undo-strike">撤销一次</button><button data-action="daily-sound" aria-pressed="'+settings.sound+'">声音：'+(settings.sound?'开':'关')+'</button></div><p class="daily-error" aria-live="polite"></p></div></div>';
  }
  function waterLog() {
    const value=daily.summary();
    return '<div class="water-log">'+(value.water.length?value.water.slice().reverse().map(item=>'<div><span>'+window.YiyiData.time(new Date(item.at)).clock+'</span><b>+'+item.amount+' ml</b></div>').join(''):'<p class="hint">今天还没有饮水记录。</p>')+'</div>';
  }
  function waterPanel(settings) {
    const value=daily.summary();
    return '<div class="daily-panel">'+header('喝水记录')+'<div class="daily-panel-body scroll-body"><p class="daily-water-stats">今天 <b data-daily-water>'+value.waterTotal+'</b> ml · <span data-daily-progress>'+waterText(value)+'</span></p><div class="daily-actions"><button data-action="daily-water-200">+200 ml</button><button data-action="daily-water-500">+500 ml</button><button data-action="daily-undo-water">撤销上次</button></div><div class="daily-inline-field"><label>自定义 ml<input id="daily-water-amount" type="number" min="1" max="3000" step="1" value="250"></label><button data-action="daily-water-custom">记录</button></div><div class="daily-inline-field"><label>每日目标 ml<input id="daily-water-goal" type="number" min="100" max="10000" step="1" value="'+settings.waterGoal+'"></label><button data-action="daily-save-goal">保存目标</button></div><p class="daily-error" aria-live="polite"></p>'+waterLog()+'</div></div>';
  }
  function openMood(date) {
    if(date>daily.today())throw new Error('这一天还没到，先记录今天吧。');
    selectedMoodDate=date;state.section='daily-mood';
  }
  function markup(section,settings,resolve) {
    artURL=resolve;
    if(section==='daily-mood')return moodEditor();
    if(section==='daily-history')return moodHistory();
    if(section==='daily-wood')return woodPanel(settings);
    if(section==='daily-water')return waterPanel(settings);
    const value=daily.summary();
    return '<section class="daily-dashboard" aria-label="心情与日常" data-daily-date="'+daily.today()+'"><div class="daily-mood-card"><button class="daily-mood-title" data-action="daily-mood-today">今天的心情如何呢？</button><button class="daily-today" data-action="daily-mood-today" aria-label="'+(value.mood?'修改':'记录')+'今天的心情">'+face(value.mood)+'</button><div class="daily-week">'+weekMarkup()+'</div></div><div class="daily-wood"><button class="wood-hit" data-action="daily-strike" aria-label="敲木鱼一次"><span class="wood-label">今日敲击<br><b data-daily-knocks>'+value.knocks+'</b> 次</span><span class="wood-pattern"></span><img src="'+esc(artURL('assets/daily-wood.jpg'))+'" alt="" draggable="false"><span class="wood-mallet" aria-hidden="true"></span></button><button class="wood-more" data-action="daily-open-wood">更多 ›</button><span class="wood-feedback" aria-live="polite"></span></div><div class="daily-mascot"><img src="'+esc(artURL(settings.mascot))+'" alt="舞狮小黑为你加油" draggable="false"></div><div class="daily-water"><div class="water-wave"></div><button class="water-summary" data-action="daily-open-water" aria-label="查看饮水记录和设置目标"><strong><span data-daily-water>'+value.waterTotal+'</span><small>ml</small></strong><span data-daily-progress>'+waterText(value)+'</span></button><div class="water-quick">'+[200,500].map(amount=>'<button data-action="daily-water-'+amount+'" aria-label="记录喝水 '+amount+' 毫升"><span aria-hidden="true"><svg viewBox="0 0 24 24"><path d="M5 2h14l-2 20H7Z" fill="#d7efff"/><path d="M6 9q3-3 6 0t6 0l-1 12H7Z" fill="#159bea"/></svg></span>'+amount+'ml</button>').join('')+'</div><span class="water-feedback" aria-live="polite"></span></div></section>';
  }
  function refresh(root) {
    const value=daily.summary(),dashboard=root.querySelector('.daily-dashboard');
    if(dashboard){
      dashboard.dataset.dailyDate=daily.today();
      const week=dashboard.querySelector('.daily-week'),html=weekMarkup();if(week.innerHTML!==html)week.innerHTML=html;
      const today=dashboard.querySelector('.daily-today'),art=face(value.mood);if(today.innerHTML!==art)today.innerHTML=art;
      today.setAttribute('aria-label',(value.mood?'修改':'记录')+'今天的心情');
      dashboard.querySelector('.water-wave').style.top=(100-Math.min(100,value.waterPercent)*.6)+'%';
    }
    root.querySelectorAll('[data-daily-knocks]').forEach(e=>{e.textContent=value.knocks;});
    root.querySelectorAll('[data-daily-total]').forEach(e=>{e.textContent=value.totalKnocks;});
    root.querySelectorAll('[data-daily-water]').forEach(e=>{e.textContent=value.waterTotal;});
    root.querySelectorAll('[data-daily-progress]').forEach(e=>{e.textContent=waterText(value);});
    const log=root.querySelector('.water-log');if(log)log.outerHTML=waterLog();
    const dateInput=root.querySelector('#daily-mood-date');if(dateInput)dateInput.max=daily.today();
  }
  function sound() {
    try {
      const level=Math.max(0,Math.min(1,Number(state.volume)/100));if(!level)return;
      const Audio=window.AudioContext||window.webkitAudioContext;if(!Audio)return;
      audioContext ||= new Audio();if(audioContext.state==='suspended')void audioContext.resume();
      const t=audioContext.currentTime;
      for(const [frequency,volume] of [[650,.22],[1100,.1],[1720,.045]]) {
        const oscillator=audioContext.createOscillator(),gain=audioContext.createGain();
        oscillator.frequency.setValueAtTime(frequency,t);oscillator.frequency.exponentialRampToValueAtTime(frequency*.65,t+.12);
        gain.gain.setValueAtTime(volume*level,t);gain.gain.exponentialRampToValueAtTime(.001*level,t+.16);
        oscillator.connect(gain);gain.connect(audioContext.destination);oscillator.start(t);oscillator.stop(t+.18);
      }
    } catch(error) { /* Visual/count feedback works when audio is unavailable. */ }
  }
  function feedback(kind,text) {
    const target=screen.querySelector(kind==='wood'?'.wood-hit,.wood-large':'.daily-water');
    target?.animate([{transform:'scale(1)'},{transform:'scale(.95)'},{transform:'scale(1)'}],{duration:150});
    const label=screen.querySelector('.'+kind+'-feedback');
    if(label){label.textContent=text;label.animate([{opacity:1,transform:'translateY(0)'},{opacity:0,transform:'translateY(-12px)'}],{duration:650,fill:'forwards'});}
  }
  function handle(action,settings,saveOptions) {
    if(!action.startsWith('daily-'))return false;
    try {
      if(action.startsWith('daily-mood-')) {openMood(action.slice(11)==='today'?daily.today():action.slice(11));render();}
      else if(action==='daily-history') {state.section='daily-history';render();}
      else if(action.startsWith('daily-pick-')) {const date=selectedMoodDate||daily.today();daily.saveMood(date,action.slice(11),daily.day(date).note);selectedMoodDate=null;state.section=null;render();}
      else if(action==='daily-open-wood') {state.section='daily-wood';render();}
      else if(action==='daily-open-water') {state.section='daily-water';render();}
      else if(action==='daily-strike') {daily.strike();if(settings.sound)sound();feedback('wood','+1');}
      else if(action==='daily-undo-strike')daily.undoStrike();
      else if(action==='daily-sound') {saveOptions({...settings,sound:!settings.sound});render();}
      else if(action==='daily-undo-water')daily.undoWater();
      else if(action==='daily-save-goal') {
        const goal=Number(screen.querySelector('#daily-water-goal').value);
        if(!Number.isInteger(goal)||goal<100||goal>10000)throw new Error('每日目标请输入 100～10000 ml 的整数。');
        saveOptions({...settings,waterGoal:goal});render();
      }
      else if(action.startsWith('daily-water-')) {
        const amount=action==='daily-water-custom'?Number(screen.querySelector('#daily-water-amount').value):Number(action.slice(12));
        daily.addWater(amount);feedback('water','+'+amount+' ml');
      }
      const error=screen.querySelector('.daily-error');if(error)error.textContent='';
    } catch(error) {const label=screen.querySelector('.daily-error');if(label)label.textContent=error.message;else notify(error.message);}
    resetIdle();return true;
  }
  window.YiyiDailyUI={markup,refresh,handle};
})();

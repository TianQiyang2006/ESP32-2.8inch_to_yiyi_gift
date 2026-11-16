(function () {
  'use strict';
  const data=window.YiyiData, calendar=window.YiyiHolidayCalendar;
  const reading=value=>value===null?'—':String(value);
  function row(id,label,art,event,artURL) {
    return '<button class="holiday-row holiday-'+id+'" data-action="holiday-date-'+id+'" title="'+esc(event.date||'日期超出支持范围')+'，打开日历"><span class="holiday-row-label"><img src="'+esc(artURL(art))+'" alt="" draggable="false">'+label+'</span><span class="holiday-days" data-holiday-days="'+id+'">'+reading(event.days)+'</span><span class="holiday-unit">天</span></button>';
  }
  function markup(config,artURL=value=>value) {
    const value=calendar.read(data.time(),config);
    return '<section class="holiday-dashboard" aria-label="放假与节日倒计时" data-holiday-date="'+value.today+'"><div class="holiday-top-card" aria-hidden="true"></div><button class="holiday-question" data-action="holiday-settings" title="设置目标星期">'+esc(value.question)+'</button><button class="holiday-answer" data-action="holiday-settings">'+esc(value.answer)+'</button><button class="holiday-calendar" data-action="holiday-settings" aria-label="'+esc(value.english+' '+config.label+'，设置目标星期')+'"><img src="'+esc(artURL('assets/holiday-calendar.png'))+'" alt="" draggable="false"><span class="holiday-english">'+value.english+'</span><span class="holiday-caption'+(config.label.length>2?' long-caption':'')+'">'+esc(config.label)+'</span></button>'+row('newyear','距离元旦','assets/holiday-plane.png',value.newYear,artURL)+row('spring','距离过年','assets/holiday-dragon.png',value.springFestival,artURL)+'</section>';
  }
  function refresh(root,config) {
    const dashboard=root.querySelector('.holiday-dashboard');if(!dashboard)return;
    const value=calendar.read(data.time(),config);
    // Keep layer nodes intact while ticking, preserving drag state and styles.
    if(dashboard.dataset.holidayDate===value.today)return;
    dashboard.dataset.holidayDate=value.today;
    dashboard.querySelector('.holiday-question').textContent=value.question;
    dashboard.querySelector('.holiday-answer').textContent=value.answer;
    dashboard.querySelector('.holiday-english').textContent=value.english;
    for(const [id,event] of [['newyear',value.newYear],['spring',value.springFestival]]) {
      dashboard.querySelector('[data-holiday-days="'+id+'"]').textContent=reading(event.days);
      dashboard.querySelector('.holiday-'+id).title=(event.date||'日期超出支持范围')+'，打开日历';
    }
  }
  window.YiyiHolidayUI={markup,refresh};
})();

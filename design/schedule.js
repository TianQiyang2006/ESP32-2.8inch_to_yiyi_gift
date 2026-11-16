(function () {
  'use strict';
  const TT = window.YIYI_TIMETABLE;
  const DAY = ['一', '二', '三', '四', '五', '六', '日'];
  const ROWS = 10;
  const TOP = 44, BOTTOM = 224;
  const ROW_H = (BOTTOM - TOP) / ROWS;
  const LABEL_W = 14;
  const COL_W = (320 - LABEL_W) / 7;
  const VIVID = ['#d9534f', '#4468b8', '#2b8f8f', '#5f9a34', '#d98a2b', '#8f5fbc', '#3a4f9e', '#c4507f', '#6f8430', '#b85f3f', '#368aa3', '#7a4fa8', '#a87724', '#4f6fa3', '#964343', '#356f60'];
  let configuration = null;
  let selectedWeek = null;
  let detailId = null;

  const NAMES = TT ? [...new Set(TT.courses.map(c => c.name))].sort() : [];
  const vividOf = name => VIVID[Math.max(0, NAMES.indexOf(name)) % VIVID.length];
  const shortName = name => name.replace(/（\d+）$/, '');
  const roomHtml = room => esc(room).replace('（', '<br>（');
  const esc = value => String(value == null ? '' : value).replace(/[&<>"]/g, ch => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[ch]));

  function termStart() { return new Date(TT.term.startDate + 'T00:00:00'); }
  function addDays(date, days) { const d = new Date(date); d.setDate(d.getDate() + days); return d; }
  function monthDay(date) { return `${date.getMonth() + 1}/${date.getDate()}`; }
  function weekMonday(week) { return addDays(termStart(), (week - 1) * 7); }
  function period(node) { return TT.periods.find(p => p.node === node) || TT.periods[0]; }
  function timeRange(c) { return `${period(c.startNode).start}–${period(c.endNode).end}`; }

  function todayInfo() {
    if (window.YiyiData && window.YiyiData.time) {
      const t = window.YiyiData.time();
      if (t && t.key) return { key: t.key, day: ((Number(t.weekday) + 6) % 7) + 1 };
    }
    const fallback = TT.meta.today || TT.term.startDate;
    const d = new Date(fallback + 'T00:00:00');
    return { key: fallback, day: ((d.getDay() + 6) % 7) + 1 };
  }
  function currentWeek(info) {
    const start = termStart();
    const today = new Date(info.key + 'T00:00:00');
    const week = Math.floor((today - start) / 86400000 / 7) + 1;
    return Math.min(Math.max(week, 1), TT.term.totalWeeks);
  }
  function coursesOf(week, day) {
    return TT.courses
      .filter(c => c.weeks.includes(week) && (day === undefined || c.day === day))
      .sort((a, b) => a.startNode - b.startNode || a.name.localeCompare(b.name));
  }
  function weeksLabel(c) {
    const w = c.weeks;
    if (!w.length) return '—';
    const step2 = w.length > 1 && w.every((x, i) => i === 0 || x === w[i - 1] + 2);
    const step1 = w.length > 1 && w.every((x, i) => i === 0 || x === w[i - 1] + 1);
    if (step1) return `${w[0]}-${w[w.length - 1]}周`;
    if (step2) {
      const parity = w.every(x => x % 2 === 0) ? '双' : w.every(x => x % 2 === 1) ? '单' : '';
      return `${w[0]}-${w[w.length - 1]}周${parity ? `（${parity}周）` : ''}`;
    }
    return w.length > 6 ? `${w.slice(0, 3).join('、')}…等 ${w.length} 周` : `${w.join('、')}周`;
  }

  function markup() {
    if (!TT || !TT.courses) return '<section class="sch"><div class="sch-empty">还没有课表数据</div></section>';
    const info = todayInfo();
    const week = selectedWeek ?? currentWeek(info);
    const monday = weekMonday(week);
    const todayCol = week === currentWeek(info) ? info.day - 1 : -1;
    const list = coursesOf(week);
    let html = `<section class="sch" aria-label="课程表">
      <div class="sch-band">
        <div class="sch-week"><b>第 ${week} 周</b><span>${monthDay(monday)}–${monthDay(addDays(monday, 4))}</span></div>
        <div class="sch-nav"><i>${list.length} 节</i><button data-action="sch-prev" ${week <= 1 ? 'disabled' : ''} aria-label="上一周">‹</button><button data-action="sch-next" ${week >= TT.term.totalWeeks ? 'disabled' : ''} aria-label="下一周">›</button></div>
      </div>`;

    for (let col = 0; col < 7; col++) {
      const d = addDays(monday, col);
      html += `<div class="sch-head ${col === todayCol ? 'on' : ''}" style="left:${LABEL_W + col * COL_W}px;width:${COL_W}px">${DAY[col]}<span>${d.getDate()}</span></div>`;
    }
    for (let r = 0; r < ROWS; r++) {
      html += `<div class="sch-lbl" style="top:${TOP + r * ROW_H}px;height:${ROW_H}px">${r + 1}</div>`;
      for (let col = 0; col < 7; col++) {
        html += `<div class="sch-cell ${col === todayCol ? 'today' : ''}" style="left:${LABEL_W + col * COL_W}px;top:${TOP + r * ROW_H}px;width:${COL_W}px;height:${ROW_H}px"></div>`;
      }
    }
    for (let day = 1; day <= 7; day++) {
      coursesOf(week, day).forEach(c => {
        const x = LABEL_W + (day - 1) * COL_W;
        const y = TOP + (c.startNode - 1) * ROW_H;
        const w = COL_W;
        const h = (c.endNode - c.startNode + 1) * ROW_H;
        const name = shortName(c.name);
        html += `<button type="button" class="sch-course" data-action="sch-open-${esc(c.id)}" aria-label="${esc(c.name)}，${esc(c.room)}，查看详情"
          style="left:${x}px;top:${y}px;width:${w}px;height:${h}px;background:${vividOf(c.name)}">
          <span class="sch-name ${name.length > 8 ? 'sm' : ''}">${esc(name)}</span>
          ${h >= 28 ? `<span class="sch-room ${c.room.length > 9 ? 'sm' : ''}">${roomHtml(c.room)}</span>` : ''}
        </button>`;
      });
    }
    if (!list.length) html += '<div class="sch-empty">这一周没有课</div>';
    html += detailMarkup(week);
    return html + '</section>';
  }

  function detailMarkup(week) {
    if (!detailId) return '';
    const c = TT.courses.find(item => item.id === detailId);
    if (!c) return '';
    const p = period(c.startNode), e = period(c.endNode);
    return `<div class="sch-detail" data-action="sch-close" role="dialog" aria-modal="true" aria-label="${esc(c.name)}详情">
      <div class="sch-card" data-action="sch-noop" style="--c:${vividOf(c.name)}">
        <div class="sch-card-head"><b>${esc(c.name)}</b><small>${DAY[c.day - 1]} · 第 ${c.startNode}-${c.endNode} 节</small></div>
        <div class="sch-card-body">
          <div class="sch-row"><strong>时间</strong><span>${p.start}–${e.end}</span></div>
          <div class="sch-row"><strong>教室</strong><span>${esc(c.room || '未安排')}</span></div>
          <div class="sch-row"><strong>教师</strong><span>${esc(c.teacher || '未安排')}</span></div>
          <div class="sch-row"><strong>周次</strong><span>${esc(weeksLabel(c))}</span></div>
        </div>
        <button type="button" class="sch-close" data-action="sch-close">知道了</button>
      </div>
    </div>`;
  }

  function render() { if (typeof window.render === 'function') window.render(); }
  function handle(action) {
    if (action === 'sch-prev' || action === 'sch-next') {
      const info = todayInfo();
      const week = (selectedWeek ?? currentWeek(info)) + (action === 'sch-next' ? 1 : -1);
      if (week < 1 || week > TT.term.totalWeeks) return true;
      selectedWeek = week;
      detailId = null;
      render();
      return true;
    }
    if (action.startsWith('sch-open-')) {
      detailId = action.slice(9);
      render();
      return true;
    }
    if (action === 'sch-close') {
      detailId = null;
      render();
      return true;
    }
    if (action === 'sch-noop') return true;
    return false;
  }
  function configure(next, resolve) { configuration = next; }
  function refresh() {}

  window.YiyiScheduleUI = { configure, markup, refresh, handle };
})();

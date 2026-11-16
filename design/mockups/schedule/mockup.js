(function () {
  'use strict';
  const TT = window.YIYI_TIMETABLE;
  const NOW = new Date('2026-10-22T14:40:00+08:00');
  const DAY_CN = ['一', '二', '三', '四', '五', '六', '日'];
  const TERM_START = new Date(TT.term.startDate + 'T00:00:00');
  const TOTAL_WEEKS = TT.term.totalWeeks;
  const CURRENT_WEEK = TT.term.currentWeek;
  const TODAY_DAY = ((NOW.getDay() + 6) % 7) + 1; // 1=Mon..7=Sun

  const pad = n => String(n).padStart(2, '0');
  function weekMonday(week) {
    const d = new Date(TERM_START);
    d.setDate(d.getDate() + (week - 1) * 7);
    return d;
  }
  function monthDay(d) { return `${d.getMonth() + 1}/${d.getDate()}`; }
  function dayDate(week, day) {
    const d = weekMonday(week);
    d.setDate(d.getDate() + day - 1);
    return d;
  }
  function period(node) { return TT.periods.find(p => p.node === node) || TT.periods[0]; }
  function timeRange(c) { return `${period(c.startNode).start}–${period(c.endNode).end}`; }
  function coursesOf(week, day) {
    return TT.courses
      .filter(c => c.weeks.includes(week) && (day === undefined || c.day === day))
      .sort((a, b) => a.startNode - b.startNode || a.name.localeCompare(b.name));
  }
  function minutes(hhmm) { const [h, m] = hhmm.split(':').map(Number); return h * 60 + m; }
  function nowMinutes() { return NOW.getHours() * 60 + NOW.getMinutes(); }
  function esc(value) {
    return String(value).replace(/[&<>"]/g, ch => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[ch]));
  }
  function courseStyle(c) { return `--bg:${c.color};--ink2:${c.ink}`; }

  function statusOf(c, week) {
    if (week !== CURRENT_WEEK || c.day !== TODAY_DAY) return '';
    const start = minutes(period(c.startNode).start);
    const end = minutes(period(c.endNode).end);
    const now = nowMinutes();
    if (now >= start && now < end) return 'live';
    if (now < start) return 'next';
    return 'done';
  }

  function header(week, dayMode) {
    const monday = weekMonday(week);
    const friday = dayDate(week, 5);
    const list = dayMode ? coursesOf(week, TODAY_DAY) : coursesOf(week);
    const count = dayMode
      ? `${week === CURRENT_WEEK ? '今天' : '这一天'} ${list.length} 节`
      : `${list.length} 节`;
    const range = dayMode ? '' : `<span>${monthDay(monday)}–${monthDay(friday)}</span>`;
    return `<div class="hd">
      <div class="wk-left"><b>第 ${week} 周</b>${range}</div>
      <div class="wk-right"><i class="cnt">${count}</i><button data-act="prev" ${week <= 1 ? 'disabled' : ''} aria-label="上一周">‹</button><button data-act="next" ${week >= TOTAL_WEEKS ? 'disabled' : ''} aria-label="下一周">›</button></div>
    </div>`;
  }

  function dots() {
    return `<div class="dots">${Array.from({ length: 7 }, (_, i) => `<i class="${i === 5 ? 'on' : ''}"></i>`).join('')}</div>`;
  }

  function weekendItems(week) {
    const items = [];
    [6, 7].forEach(day => coursesOf(week, day).forEach(c => items.push({ day, c })));
    return items;
  }

  function weekendStrip(items, top) {
    if (!items.length) return '';
    const lines = items.map(({ day, c }) => `<div class="wknd-line"><i style="background:${c.color};box-shadow:inset 0 0 0 1px ${c.ink}66"></i>${DAY_CN[day - 1]} · ${c.startNode}-${c.endNode} 节 · ${esc(c.name)} · ${esc(c.room)}</div>`).join('');
    return `<div class="wknd" style="top:${top}px">${lines}</div>`;
  }

  /* ---------------- A: today list ---------------- */
  function renderA(root, week) {
    const list = coursesOf(week, TODAY_DAY);
    const firstNext = list.find(c => statusOf(c, week) === 'next');
    const strip = [1, 2, 3, 4, 5].map(day => {
      const d = dayDate(week, day);
      const on = week === CURRENT_WEEK && day === TODAY_DAY;
      return `<div class="a-day ${on ? 'on' : ''}"><span>${DAY_CN[day - 1]}</span><b>${d.getDate()}</b></div>`;
    }).join('');
    const cards = list.map(c => {
      let state = statusOf(c, week);
      if (state === 'next' && c !== firstNext) state = '';
      const tag = state === 'live' ? '<span class="a-tag live">上课中</span>'
        : state === 'next' ? '<span class="a-tag next">下一节</span>'
        : state === 'done' ? '<span class="a-tag done">已结束</span>' : '';
      return `<article class="a-card" style="${courseStyle(c)}">
        <div class="a-time"><b>${period(c.startNode).start}</b><span>${period(c.endNode).end}</span></div>
        <div class="a-name">${esc(c.name)}</div>
        <div class="a-meta">${esc(c.room)} · 第${c.startNode}-${c.endNode}节</div>
        ${tag}
      </article>`;
    }).join('');
    const empty = `<div class="a-empty">这一天没有课，好好休息 ♡</div>`;
    root.innerHTML = `${header(week, true)}
      <div class="a-strip">${strip}</div>
      <div class="a-scroll"><div class="a-list">${list.length ? cards : empty}</div></div>${dots()}`;
  }

  /* ---------------- B/C: week grid ---------------- */
  function gridParts(week, cfg) {
    const GAP = 2, COLS = 5;
    const bodyTop = cfg.top + cfg.headH + GAP;
    const bodyH = cfg.height - cfg.headH - GAP;
    const labels = ['1-2', '3-4', '5-6', '7-8', '9-10', '11-12'];
    let maxRow = 4;
    for (let d = 1; d <= 5; d++) {
      coursesOf(week, d).forEach(c => {
        maxRow = Math.max(maxRow, Math.min(labels.length - 1, Math.floor((c.endNode - 1) / 2)));
      });
    }
    const rows = maxRow + 1;
    const rowH = (bodyH - (rows - 1) * GAP) / rows;
    const colW = (308 - cfg.labelW - GAP - 4 * GAP) / 5;
    const colX = i => 6 + cfg.labelW + GAP + i * (colW + GAP);
    const rowY = r => bodyTop + r * (rowH + GAP);
    const todayCol = week === CURRENT_WEEK ? TODAY_DAY - 1 : -1;
    let html = '';
    for (let col = 0; col < COLS; col++) {
      const d = dayDate(week, col + 1);
      html += `<div class="${cfg.hClass} ${col === todayCol ? 'on' : ''}" style="left:${colX(col)}px;top:${cfg.top}px;width:${colW}px">${DAY_CN[col]}<span>${d.getDate()}</span></div>`;
    }
    for (let r = 0; r < rows; r++) {
      html += `<div class="${cfg.lblClass}" style="left:6px;top:${rowY(r)}px;width:${cfg.labelW}px;height:${rowH}px">${labels[r]}</div>`;
      for (let col = 0; col < COLS; col++) {
        html += `<div class="${cfg.cellClass} ${col === todayCol ? 'today' : ''}" style="left:${colX(col)}px;top:${rowY(r)}px;width:${colW}px;height:${rowH}px"></div>`;
      }
    }
    for (let col = 0; col < COLS; col++) {
      coursesOf(week, col + 1).forEach(c => {
        const row = Math.floor((c.startNode - 1) / 2);
        const rowEnd = Math.floor((c.endNode - 1) / 2);
        if (row > rows - 1) return;
        const span = Math.min(rowEnd, rows - 1) - row + 1;
        const top = rowY(row), height = span * rowH + (span - 1) * GAP;
        html += `<div class="${cfg.courseClass}" style="left:${colX(col) + 1}px;top:${top + 1}px;width:${colW - 2}px;height:${height - 2}px;${courseStyle(c)}" title="${esc(c.name)} · ${esc(c.room)}">${esc(c.name)}</div>`;
      });
    }
    return html;
  }

  function renderB(root, week) {
    const weekend = weekendItems(week);
    const reserved = weekend.length ? weekend.length * 8 + 4 : 0;
    const gridH = 192 - reserved;
    root.innerHTML = `${header(week)}<div class="b-grid">${gridParts(week, {
      top: 30, height: gridH, headH: 16, labelW: 20,
      hClass: 'b-h', lblClass: 'b-lbl', cellClass: 'b-cell', courseClass: 'b-course',
    })}</div>${weekendStrip(weekend, 30 + gridH + 2)}${dots()}`;
  }

  function nextInfo(week) {
    const list = coursesOf(week, TODAY_DAY);
    const now = nowMinutes();
    const live = list.find(c => week === CURRENT_WEEK && now >= minutes(period(c.startNode).start) && now < minutes(period(c.endNode).end));
    if (live) return { kind: 'live', c: live };
    const next = list.find(c => week === CURRENT_WEEK ? now < minutes(period(c.startNode).start) : true);
    if (next) return { kind: 'next', c: next };
    return { kind: 'none' };
  }

  function renderC(root, week) {
    const info = nextInfo(week);
    let bar;
    if (info.kind === 'none') {
      const word = week === CURRENT_WEEK ? '今天' : '这一天';
      bar = `<div class="c-next"><div class="lbl">${word}</div><div class="name">没有课啦</div><div class="sub">去看看书，或者发会儿呆</div><div class="c-cat"></div></div>`;
    } else {
      const c = info.c;
      const start = minutes(period(c.startNode).start);
      const diff = start - nowMinutes();
      const isNow = week === CURRENT_WEEK;
      const countdown = info.kind === 'live'
        ? `还有 ${Math.max(0, minutes(period(c.endNode).end) - nowMinutes())} 分钟下课`
        : diff >= 60 ? `${Math.floor(diff / 60)} 小时 ${diff % 60} 分后` : `${diff} 分钟后`;
      const headLabel = isNow
        ? `${info.kind === 'live' ? '现在' : '下一节'} · <b>${period(c.startNode).start}–${period(c.endNode).end}</b> · ${countdown}`
        : `这一天 · <b>${period(c.startNode).start}–${period(c.endNode).end}</b>`;
      bar = `<div class="c-next">
        <div class="lbl">${headLabel}</div>
        <div class="name">${esc(c.name)}</div>
        <div class="sub">${esc(c.room)} · 第${c.startNode}-${c.endNode}节</div>
        <div class="c-cat"></div>
      </div>`;
    }
    const weekend = weekendItems(week);
    const reserved = weekend.length ? weekend.length * 8 + 4 : 0;
    const gridH = 150 - reserved;
    root.innerHTML = `${header(week)}<div class="c-grid">${gridParts(week, {
      top: 26, height: gridH, headH: 14, labelW: 20,
      hClass: 'c-h', lblClass: 'c-lbl', cellClass: 'c-cell', courseClass: 'c-course',
    })}</div>${weekendStrip(weekend, 26 + gridH + 2)}${bar}${dots()}`;
  }

  const VIVID = ['#d9534f', '#4468b8', '#2b8f8f', '#5f9a34', '#d98a2b', '#8f5fbc', '#3a4f9e', '#c4507f', '#6f8430', '#b85f3f', '#368aa3', '#7a4fa8', '#a87724', '#4f6fa3', '#964343', '#356f60'];
  const NAMES = [...new Set(TT.courses.map(c => c.name))].sort();
  const vividOf = name => VIVID[NAMES.indexOf(name) % VIVID.length];
  const shortName = name => name.replace(/（\d+）$/, '');
  const roomHtml = room => esc(room).replace('（', '<br>（');

  /* ---------------- D: phone-style 7-day grid ---------------- */
  function renderD(root, week) {
    const ROWS = 10;
    const top = 44, bottom = 224;
    const rowH = (bottom - top) / ROWS;
    const labelW = 14;
    const colW = (320 - labelW) / 7;
    const todayCol = week === CURRENT_WEEK ? TODAY_DAY - 1 : -1;
    const monday = weekMonday(week);
    let html = `<div class="d-band">
      <div class="d-week"><b>第 ${week} 周</b><span>${monthDay(monday)}–${monthDay(dayDate(week, 5))}</span></div>
      <div class="d-nav"><i>${coursesOf(week).length} 节</i><button data-act="prev" ${week <= 1 ? 'disabled' : ''}>‹</button><button data-act="next" ${week >= TOTAL_WEEKS ? 'disabled' : ''}>›</button></div>
    </div>`;
    for (let col = 0; col < 7; col++) {
      const d = dayDate(week, col + 1);
      html += `<div class="d-head ${col === todayCol ? 'on' : ''}" style="left:${labelW + col * colW}px;width:${colW}px">${DAY_CN[col]}<span>${d.getDate()}</span></div>`;
    }
    for (let r = 0; r < ROWS; r++) {
      html += `<div class="d-lbl" style="top:${top + r * rowH}px;height:${rowH}px">${r + 1}</div>`;
      for (let col = 0; col < 7; col++) {
        html += `<div class="d-cell ${col === todayCol ? 'today' : ''}" style="left:${labelW + col * colW}px;top:${top + r * rowH}px;width:${colW}px;height:${rowH}px"></div>`;
      }
    }
    for (let day = 1; day <= 7; day++) {
      coursesOf(week, day).forEach(c => {
        const x = labelW + (day - 1) * colW;
        const y = top + (c.startNode - 1) * rowH;
        const w = colW;
        const h = (c.endNode - c.startNode + 1) * rowH;
        html += `<div class="d-course" style="left:${x}px;top:${y}px;width:${w}px;height:${h}px;background:${vividOf(c.name)}">
          <div class="d-name ${shortName(c.name).length > 8 ? 'sm' : ''}">${esc(shortName(c.name))}</div>
          ${h >= 28 ? `<div class="d-room ${c.room.length > 9 ? 'sm' : ''}">${roomHtml(c.room)}</div>` : ''}
        </div>`;
      });
    }
    root.innerHTML = html + dots();
  }

  const RENDER = { a: renderA, b: renderB, c: renderC, d: renderD };

  function render(root) {
    const kind = root.dataset.ui;
    const week = Number(root.dataset.week || CURRENT_WEEK);
    RENDER[kind](root, week);
  }

  document.querySelectorAll('.screen').forEach(render);

  const qWeek = Number(new URLSearchParams(location.search).get('week'));
  if (qWeek >= 1 && qWeek <= TOTAL_WEEKS) {
    document.querySelectorAll('.screen').forEach(root => {
      root.dataset.week = String(qWeek);
      render(root);
    });
  }

  document.addEventListener('click', event => {
    const btn = event.target.closest('[data-act]');
    if (!btn) return;
    const root = btn.closest('.screen');
    const week = Number(root.dataset.week || CURRENT_WEEK) + Number(btn.dataset.act === 'next' ? 1 : -1);
    if (week < 1 || week > TOTAL_WEEKS) return;
    root.dataset.week = String(week);
    render(root);
  });

  const hash = location.hash.slice(1);
  if (hash) {
    document.body.classList.add('isolate');
    const target = document.getElementById(hash);
    if (target) target.classList.add('on');
  }
})();

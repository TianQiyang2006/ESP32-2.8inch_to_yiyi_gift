const fs = require('node:fs');
const path = require('node:path');
const assert = require('node:assert/strict');
const crypto = require('node:crypto');

// Browser QA of the exact embedded portal, with an isolated HTTP fixture.
// This does not open a serial port or exercise the production persistence layer.
const root = path.resolve(__dirname, '../..');
const out = path.join(root, 'build/radio-manager-20261114/portal');
fs.mkdirSync(out, {recursive: true});
const {chromium} = require(path.join(root, 'build/countdown-days-20261114/browser-tools/node_modules/playwright'));
const portalPath = path.join(root, 'hardware/arduino/yiyi_desk/gift_portal.h');
const origin = 'http://127.0.0.1:8878';
const defaults = [
  {id: 'beijing_music', name: '北京音乐广播', tag: '音乐 · FM97.4', url: 'http://lhttp.qingting.fm/live/332/64k.mp3'},
  {id: 'shanghai_pop', name: '动感101', tag: '流行 · FM101.7', url: 'http://lhttp.qingting.fm/live/274/64k.mp3'},
  {id: 'shanghai_classic', name: '经典947', tag: '古典 · FM94.7', url: 'http://lhttp.qingting.fm/live/267/64k.mp3'},
  {id: 'guangdong_music', name: '广东音乐之声', tag: '音乐 · FM99.3', url: 'http://lhttp.qtfm.cn/live/1260/64k.mp3'},
  {id: 'cnr_voice', name: '中国之声', tag: '新闻 · 综合', url: 'http://lhttp.qtfm.cn/live/15318317/64k.mp3'},
  {id: 'beijing_news', name: '北京新闻广播', tag: '新闻 · FM94.5', url: 'http://lhttp.qtfm.cn/live/339/64k.mp3'},
];
const clone = value => JSON.parse(JSON.stringify(value));

async function run() {
  const header = fs.readFileSync(portalPath, 'utf8');
  const html = header.split('R"html(')[1]?.split(')html";')[0];
  assert(html && html.includes('id="radio-manager"'), 'FM management section must be present in the production portal');
  const browser = await chromium.launch({executablePath: 'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe', headless: true});
  const page = await browser.newPage({viewport: {width: 390, height: 844}, deviceScaleFactor: 1, isMobile: true, hasTouch: true});
  page.setDefaultTimeout(8000);
  const errors = [], checks = [], requests = [], screenshots = [];
  let state = {limit: 20, revision: 1, stations: clone(defaults)}, nextId = 1;
  let nextFailure = null, postDelay = 0, gets = 0, commits = 0;
  let releasePost = null, observedPost = null;
  const config = {today: '2026-11-14', countdownDays: {items: []}, deviceSettings: {brightness: 50, volume: 52}, dailyRecords: {}};
  page.on('pageerror', error => errors.push(error.message));
  await page.addInitScript(() => {
    // Expose registered production poll callbacks for deterministic refresh tests.
    // Polling itself still executes the same portal functions and HTTP requests.
    const nativeInterval = window.setInterval;
    window.__qaPolls = [];
    window.setInterval = (fn, interval, ...args) => {
      if (typeof fn === 'function') window.__qaPolls.push(() => fn(...args));
      return nativeInterval(fn, interval, ...args);
    };
  });
  await page.route(origin + '/**', async route => {
    const req = route.request(), url = new URL(req.url());
    if (url.pathname === '/') return route.fulfill({contentType: 'text/html; charset=utf-8', body: html});
    if (url.pathname === '/api/gift') return route.fulfill({json: config});
    if (url.pathname !== '/api/radio-stations') return route.fulfill({json: {ok: true}});
    if (req.method() === 'GET') { ++gets; return route.fulfill({json: clone(state)}); }
    const data = req.postDataJSON();
    requests.push(clone(data));
    if (observedPost) { observedPost(); observedPost = null; }
    if (postDelay) await new Promise(resolve => { releasePost = resolve; });
    if (nextFailure) {
      const failure = nextFailure; nextFailure = null;
      return route.fulfill({status: failure.status, json: {error: failure.message}});
    }
    if (data.revision !== state.revision) return route.fulfill({status: 409, json: {error: '电台列表已更新，请刷新后重试'}});
    if (data.action === 'remove') {
      const index = state.stations.findIndex(item => item.id === data.id);
      if (index < 0) return route.fulfill({status: 400, json: {error: '电台已不存在'}});
      state.stations.splice(index, 1);
    } else {
      const station = clone(data.station);
      const index = state.stations.findIndex(item => item.id === station.id);
      if (!station.id && state.stations.length >= 20) return route.fulfill({status: 400, json: {error: '最多保存20个电台'}});
      if (station.id && index < 0) return route.fulfill({status: 400, json: {error: '电台已不存在'}});
      station.id ||= 'custom_' + nextId++;
      if (index < 0) state.stations.push(station); else state.stations[index] = station;
    }
    ++state.revision; ++commits; delete state.error;
    return route.fulfill({json: clone(state)});
  });

  const count = page.locator('#radio-count'), form = page.locator('#radio-form');
  const field = name => form.locator('[name="' + name + '"]');
  const save = page.locator('#radio-submit'), cancel = page.locator('#radio-cancel');
  const note = page.locator('#radio-note');
  const button = (action, id) => page.locator('[data-radio-action="' + action + '"][data-station-id="' + id + '"]');
  const list = page.locator('#radio-list');
  const check = (name, valid = true) => { assert(valid, name); checks.push(name); console.log('PASS ' + name); };
  const eventually = async (predicate, message) => {
    const end = Date.now() + 8000;
    while (!await predicate()) { if (Date.now() >= end) throw Error(message); await new Promise(resolve => setTimeout(resolve, 30)); }
  };
  async function number(n) { await count.filter({hasText: new RegExp('(?:^|\\D)' + n + '\\s*/\\s*20')}).waitFor(); }
  async function poll() {
    await page.evaluate(async () => { for (const fn of window.__qaPolls || []) await fn(); });
    await page.waitForTimeout(60);
  }
  async function fill(name, url, tag = '音乐 · 自选') {
    await field('name').fill(name); await field('tag').fill(tag); await field('url').fill(url);
  }
  async function reset() {
    if (await cancel.isVisible()) await cancel.click();
    else { await field('name').fill(''); await field('tag').fill(''); await field('url').fill(''); }
  }
  async function shot(name, section = true) {
    const p = path.join(out, name + '.png');
    // Keep the portal's unrelated sticky connection bar outside section-only review captures.
    if (section) await page.locator('#radio-manager').screenshot({path: p, style: '#status{visibility:hidden!important}'});
    else await page.screenshot({path: p, fullPage: false});
    screenshots.push(name + '.png');
  }
  async function idle() { await eventually(async () => await form.getAttribute('aria-busy') !== 'true', 'Radio form did not finish saving'); }
  async function committed(n) { await eventually(() => commits === n, 'Expected write did not commit'); await number(state.stations.length); await idle(); }
  let result = null;
  try {
    await page.goto(origin + '/#radio-manager'); await number(6);
    check('six defaults count toward the twenty station limit');
    check('all six default rows render', await list.locator('[data-radio-action="edit"]').count() === 6);
    await page.locator('#radio-manager').scrollIntoViewIfNeeded(); await shot('phone-390-defaults', false);

    await fill('夜间音乐', 'https://radio.example.test/night.aac', '音乐 · 轻声');
    await save.click(); await committed(1); await number(7);
    let custom = state.stations.find(item => item.name === '夜间音乐');
    check('add sends station fields with the current revision', requests.at(-1).action === 'upsert' && requests.at(-1).revision === 1 && custom.tag === '音乐 · 轻声');
    await button('edit', custom.id).click();
    check('edit restores saved form data', await field('name').inputValue() === custom.name && await field('url').inputValue() === custom.url);
    await field('name').fill('深夜音乐'); await save.click(); await committed(2);
    check('edit keeps the stable station ID', state.stations.find(item => item.id === custom.id).name === '深夜音乐' && state.stations.length === 7);

    await fill('还没保存的电台', 'http://radio.example.test/draft.mp3'); await poll();
    check('polling preserves an unsaved draft', await field('name').inputValue() === '还没保存的电台');
    for (const status of [400, 500]) {
      nextFailure = {status, message: status === 400 ? '电台地址不正确' : '保存失败，原列表保留'};
      const previous = commits;
      await save.click();
      await eventually(async () => (await note.textContent()).includes(status === 400 ? '电台地址不正确' : '保存失败，原列表保留'), 'Expected HTTP ' + status + ' error message');
      await idle();
      check('HTTP ' + status + ' leaves the draft and stored list intact', await field('name').inputValue() === '还没保存的电台' && commits === previous);
    }

    // Keep a write in flight while the user continues typing.
    postDelay = 1;
    let postSeen; const seen = new Promise(resolve => postSeen = resolve); observedPost = postSeen;
    const beforeInflight = commits, beforeRequests = requests.length;
    await save.click(); await seen;
    check('submit is disabled while the request is pending', await save.isDisabled());
    await form.evaluate(element => element.requestSubmit());
    await page.waitForTimeout(70);
    check('duplicate pending submission is suppressed', requests.length === beforeRequests + 1);
    await field('name').fill('提交期间的新草稿');
    postDelay = 0; releasePost(); releasePost = null;
    await committed(beforeInflight + 1);
    check('typing during save preserves the newer draft', await field('name').inputValue() === '提交期间的新草稿');
    const createdDuringTyping = state.stations.find(item => item.name === '还没保存的电台');
    check('retained draft receives the ID created by the pending add', !!createdDuringTyping && await field('id').inputValue() === createdDuringTyping.id);
    const beforeFollowup = commits, countBeforeFollowup = state.stations.length;
    await save.click(); await committed(beforeFollowup + 1);
    check('saving the retained draft updates the created station without duplication', requests.at(-1).station.id === createdDuringTyping.id && state.stations.length === countBeforeFollowup && state.stations.find(item => item.id === createdDuringTyping.id).name === '提交期间的新草稿');

    await reset();
    const beforeConflict = commits;
    await fill('冲突草稿', 'https://radio.example.test/conflict.mp3');
    const staleRevision = state.revision;
    state.stations.push({id: 'external', name: '另一部手机增加', tag: '综合', url: 'https://radio.example.test/external.mp3'}); ++state.revision;
    await poll();
    check('background refresh does not advance the draft base revision', await field('name').inputValue() === '冲突草稿');
    await save.click();
    await eventually(async () => (await note.textContent()).includes('电台列表已在其他页面更新'), 'Expected conflict message');
    await idle();
    check('409 conflict sends frozen draft revision and keeps the draft', requests.at(-1).revision === staleRevision && commits === beforeConflict && await field('name').inputValue() === '冲突草稿');
    await page.locator('#radio-refresh').click();
    await eventually(async () => (await note.textContent()).includes('列表已刷新'), 'Explicit refresh did not finish');
    check('explicit refresh keeps draft text for reconciliation', await field('name').inputValue() === '冲突草稿');
    await save.click(); await committed(beforeConflict + 1);
    check('explicit refresh permits an intentional retry with latest revision', requests.at(-1).revision === state.revision - 1);

    // Delete UI requires an explicit confirmation; cancel must not write.
    const beforeDeleteRequests = requests.length;
    await button('remove', custom.id).click(); await page.locator('#radio-delete').waitFor({state: 'visible'});
    await page.locator('#radio-delete-cancel').click();
    check('cancel deletion leaves the station and sends no write', state.stations.some(item => item.id === custom.id) && requests.length === beforeDeleteRequests);
    await button('remove', custom.id).click(); const beforeConfirmedDelete = commits; await page.locator('#radio-delete-confirm').click(); await committed(beforeConfirmedDelete + 1);
    check('confirmed delete removes the chosen stable ID', !state.stations.some(item => item.id === custom.id));

    // Malicious-looking user text must remain literal DOM text.
    await reset(); await fill('<b>自选音乐</b>', 'https://radio.example.test/safe.mp3', '<i>音乐</i>');
    const beforeMarkup = commits; await save.click(); await committed(beforeMarkup + 1);
    check('station name and tag render as literal text', await list.locator('b,i,script,img').count() === 0 && (await list.textContent()).includes('<b>自选音乐</b>'));

    // Fill to exactly twenty through the isolated device reply, then use actual controls.
    await reset();
    while (state.stations.length < 20) {
      const index = state.stations.length;
      state.stations.push({id: 'full_' + index, name: '自选电台 ' + (index + 1), tag: '音乐 · 综合', url: 'https://radio.example.test/' + index + '/live.mp3'});
    }
    ++state.revision; await poll(); await number(20);
    check('adding is disabled at twenty stations', await save.isDisabled());
    await button('edit', 'beijing_music').click();
    check('editing remains enabled at the twenty station limit', await save.isEnabled());
    await field('name').fill('北京音乐精选'); const beforeFullEdit = commits; await save.click(); await committed(beforeFullEdit + 1); await number(20);
    check('editing at capacity does not add an extra station', state.stations.length === 20 && state.stations[0].name === '北京音乐精选');
    await shot('phone-capacity-20');
    await button('remove', 'full_19').click(); const beforeFullDelete = commits; await page.locator('#radio-delete-confirm').click(); await committed(beforeFullDelete + 1); await number(19);
    check('deleting below the limit enables adding again', await save.isEnabled());
    await fill('再加入的电台', 'https://radio.example.test/readded.aac'); const beforeReadd = commits; await save.click(); await committed(beforeReadd + 1); await number(20);
    check('one slot can be reused after deleting at capacity', state.stations.length === 20);

    // Empty reply is a valid catalog, not a cue to reinsert defaults.
    await reset(); state.stations = []; ++state.revision; await poll(); await number(0);
    check('zero-station empty state supports adding', await save.isEnabled() && await list.locator('[data-radio-action]').count() === 0);
    await shot('phone-empty');
    await fill('第一座电台', 'http://radio.example.test/first.mp3'); const beforeEmptyAdd = commits; await save.click(); await committed(beforeEmptyAdd + 1); await number(1);
    check('add works after the entire catalog is deleted', state.stations.length === 1);

    // Validation should stop obvious unsupported page/playlist input before committing.
    await fill('不是音频', 'https://radio.example.test/stream.m3u8');
    const beforeUnsupported = commits; await save.click(); await page.waitForTimeout(120);
    check('unsupported playlist is not committed', commits === beforeUnsupported);
    await reset();

    // Storage recovery information is visible while the user's draft survives.
    await fill('恢复后再保存', 'https://radio.example.test/recovered.mp3');
    state.error = '主电台列表损坏，已读取恢复副本';
    await poll();
    await eventually(async () => (await note.textContent()).includes(state.error), 'Expected storage recovery warning');
    check('positive-revision storage warning is shown without losing the draft', await field('name').inputValue() === '恢复后再保存' && await save.isEnabled());
    const recoveredRevision = state.revision + 1, beforeUnavailable = requests.length;
    state.revision = 0; state.error = '电台列表内存不足';
    await poll();
    await eventually(async () => (await note.textContent()).includes(state.error), 'Expected unavailable catalog message');
    check('unavailable revision zero catalog preserves draft and displayed list', await field('name').inputValue() === '恢复后再保存' && await list.locator('[data-radio-action="edit"]').count() === state.stations.length);
    check('unavailable catalog disables every station write control', await save.isDisabled() && await page.locator('#radio-add').isDisabled() && await list.locator('button:not([disabled])').count() === 0);
    await form.evaluate(element => element.requestSubmit()); await page.waitForTimeout(70);
    check('unavailable catalog also rejects programmatic form submission', requests.length === beforeUnavailable);
    state.revision = recoveredRevision; delete state.error;
    await page.locator('#radio-refresh').click();
    await eventually(async () => await save.isEnabled(), 'Recovered catalog did not enable saving');
    check('explicit refresh after recovery preserves the draft and enables saving', await field('name').inputValue() === '恢复后再保存');
    const beforeRecoverySave = commits; await save.click(); await committed(beforeRecoverySave + 1);
    check('draft can be saved against recovered catalog revision', requests.at(-1).revision === recoveredRevision && state.stations.some(item => item.name === '恢复后再保存'));

    // A reviewed presentation fixture keeps screenshots compact and meaningful.
    state.stations = clone(defaults.slice(0, 3));
    state.stations.push({id: 'custom_preview', name: '晚风里的轻音乐', tag: '音乐 · 放松', url: 'https://radio.example.test/live/piano.aac'}); ++state.revision;
    await poll(); await number(4);
    for (const [width, height] of [[360, 800], [390, 844]]) {
      await page.setViewportSize({width, height});
      await page.locator('#radio-manager').scrollIntoViewIfNeeded();
      check(width + 'px phone has no horizontal overflow', await page.evaluate(() => document.documentElement.scrollWidth <= window.innerWidth));
      await shot('phone-' + width, false);
      await shot('section-' + width);
    }
    await page.setViewportSize({width: 960, height: 1000}); await shot('desktop-section');
    await page.setViewportSize({width: 390, height: 844});
    await field('name').focus(); await page.keyboard.type('键盘电台'); await page.keyboard.press('Tab');
    check('keyboard focus can move from the station name', await field('name').evaluate(element => element !== document.activeElement));

    // Final review artifacts use a fresh page and the real six defaults, with no
    // test draft, saved fixture station, or previous operation message remaining.
    state = {limit: 20, revision: 1, stations: clone(defaults)};
    await page.reload(); await number(6);
    await shot('default-six-390');
    await form.screenshot({path: path.join(out, 'add-form-390.png'), style: '#status{visibility:hidden!important}'});
    screenshots.push('add-form-390.png');
    check('no uncaught page errors', errors.length === 0);
    result = {passed: true, checks, requests: requests.length, committedWrites: commits, catalogReads: gets, pageErrors: errors,
      portal_sha256: crypto.createHash('sha256').update(header).digest('hex'), screenshots,
      api: 'isolated fixtures; production catalog persistence and validation require their own backend test',
      serial_opened: false, firmware_flashed: false};
    console.log('PASS radio portal: ' + checks.length + ' checks');
  } catch (error) {
    result = {passed: false, checks, error: String(error.stack || error), requests, state, pageErrors: errors, screenshots};
    try { await shot('failure'); } catch {}
    throw error;
  } finally {
    fs.writeFileSync(path.join(out, 'verification.json'), JSON.stringify(result, null, 2) + '\n');
    await browser.close();
  }
}
run().catch(error => { console.error(error); process.exitCode = 1; });

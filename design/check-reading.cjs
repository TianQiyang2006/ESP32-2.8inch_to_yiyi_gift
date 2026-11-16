const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const {test} = require('node:test');

const plain = value => JSON.parse(JSON.stringify(value));
const response = (payload, status = 200, retryAfter = null) => ({
  ok: status >= 200 && status < 300, status,
  headers: {get: () => retryAfter}, json: async () => payload,
});
const poem = {data: {content: ['双金狮子四金龙，喷出香云绕殿中。', '太上垂衣今上拜，百王曾有个家风。'],
  author: {name: '杨万里'}, title: '德寿宫庆寿口号  其四', dynasty: {name: '唐'}}};

function harness() {
  let date = '2026-10-11', instant = Date.parse('2026-10-11T08:26:00+08:00');
  let reply = () => response({text: '达人远见，不与物争。'});
  const calls = [], events = [];
  const sandbox = {
    Date: class extends Date {static now() {return instant;}},
    AbortController, setTimeout, clearTimeout,
    location: {protocol: 'http:'},
    YiyiData: {now: () => new Date(instant), time: () => ({key: date, month: 10, day: 11, clock: '08:26'})},
    fetch: async (url, options) => {calls.push({url, options}); return reply(url, options);},
    esc: value => String(value).replace(/[&<>"']/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c])),
  };
  sandbox.window = sandbox;
  vm.createContext(sandbox);
  for(const file of ['designer-model.js', 'reading-data.js', 'reading-dashboard.js'])
    vm.runInContext(fs.readFileSync(path.join(__dirname, file), 'utf8'), sandbox, {filename: file});
  const model = sandbox.YiyiDesign, feed = sandbox.YiyiReading;
  const options = plain(model.defaults.reading);
  const oldQuote = {text: '旧缓存正文', author: '旧作者', title: '', dynasty: '', date,
    updatedAt: instant, source: 'api'};
  const oldPoem = {...plain(feed.parse('poem', poem)), date, updatedAt: instant, source: 'api'};
  const configure = (records = {quote: oldQuote, poem: oldPoem}, changes = {}, enabled = true) =>
    feed.configure({...options, ...changes}, records, {enabled});
  feed.subscribe(event => events.push(plain(event)));
  configure();
  return {sandbox, model, feed, options, oldQuote, oldPoem, calls, events, configure,
    reply: fn => {reply = fn;}, advance: ms => {instant += ms;}, nextDay: () => {date = '2026-10-12';}};
}

test('UApi root text is used without inventing author metadata', () => {
  const {feed} = harness();
  assert.equal(feed.endpoints.quote, 'https://uapis.cn/api/v1/saying');
  assert.deepEqual(plain(feed.parse('quote', {text: ' 一言正文 ', author: 'unexpected'})),
    {text: '一言正文', author: '', title: '', dynasty: ''});
  for(const payload of [null, {}, {text: '  '}, {text: 42}, {errno: 0, data: {text: '旧格式'}}])
    assert.throws(() => feed.parse('quote', payload), /有效内容/);
});

test('file previews use the local relay while HTTP previews use UApi directly', async () => {
  const h = harness(); h.sandbox.location.protocol = 'file:';
  await h.feed.refresh('quote');
  assert.equal(h.calls[0].url, 'http://127.0.0.1:8766/api/reading/quote');
  assert.equal(h.feed.read('quote').provider, 'uapis');
  h.advance(10001); h.sandbox.YIYI_PHONE_BASE = 'http://127.0.0.1:9876';
  await h.feed.refresh('quote');
  assert.equal(h.calls[1].url, 'http://127.0.0.1:9876/api/reading/quote');
});

test('Palemoky parsing preserves the complete poem and returned metadata', () => {
  const {feed} = harness();
  assert.equal(feed.endpoints.poem, 'https://poetry.palemoky.com/api/poems/random?lang=zh-Hans');
  assert.deepEqual(plain(feed.parse('poem', poem)), {text: poem.data.content.join('\n'),
    author: '杨万里', title: poem.data.title, dynasty: '唐'});
  assert.throws(() => feed.parse('poem', {data: {content: ['', 42]}}), /有效内容/);
});

test('today’s legacy quote migrates once, retaining today’s existing poem', async () => {
  const h = harness();
  assert.equal(h.feed.read('quote').current, false);
  assert.match(h.sandbox.YiyiReadingUI.markup('reading-detail-quote'), /历史接口缓存/);
  h.feed.ensure();
  await h.feed.refresh('quote');
  assert.equal(h.calls.length, 1);
  assert.equal(h.calls[0].url, h.feed.endpoints.quote);
  assert.equal(h.feed.read('quote').text, '达人远见，不与物争。');
  assert.equal(h.feed.read('quote').author, '');
  assert.equal(h.feed.read('quote').provider, 'uapis');
  assert.equal(h.feed.read('quote').current, true);
  assert.match(h.sandbox.YiyiReadingUI.markup('reading-detail-quote'), /UApi 一言/);
  assert.deepEqual(plain(h.feed.snapshot().poem), h.oldPoem);
  h.feed.ensure();
  assert.equal(h.calls.length, 1);
  const design = h.model.validate({...plain(h.model.defaults), readingCache: plain(h.feed.snapshot())});
  assert.equal(design.readingCache.quote.provider, 'uapis', 'save/import keeps provider');
});

test('invalid responses and HTTP errors preserve the old cache and allow manual retry', async () => {
  for(const reply of [() => response({text: ''}), () => response({}, 503), () => {throw new Error('offline');}]) {
    const h = harness(); h.reply(reply);
    h.feed.ensure(); await h.feed.refresh('quote');
    assert.deepEqual(plain(h.feed.snapshot().quote), h.oldQuote);
    assert(h.feed.read('quote').error);
    assert.equal(h.feed.read('quote').loading, false);
    assert.equal(h.events.filter(event => event.type === 'cache').length, 0);
    h.feed.ensure(); assert.equal(h.calls.length, 1, 'failed migration does not loop');
    h.advance(10001); h.reply(() => response({text: '重试成功'}));
    await h.feed.refresh('quote');
    assert.equal(h.feed.read('quote').text, '重试成功');
    assert.equal(h.feed.read('quote').error, '');
  }
});

test('poem refresh caches all lines and its provider survives design validation', async () => {
  const h = harness(); h.reply(() => response(poem));
  await h.feed.refresh('poem');
  assert.equal(h.calls[0].url, h.feed.endpoints.poem);
  assert.equal(h.feed.read('poem').text, poem.data.content.join('\n'));
  const validated = h.model.validate({...plain(h.model.defaults), readingCache: plain(h.feed.snapshot())});
  assert.equal(validated.readingCache.poem.provider, 'palemoky');
});

test('overlapping refreshes share a request; settings changes reject late responses', async () => {
  const h = harness(); let resolve;
  h.reply(() => new Promise(done => {resolve = done;}));
  const first = h.feed.refresh('quote'), second = h.feed.refresh('quote');
  assert.equal(h.calls.length, 1);
  h.configure({quote: h.oldQuote, poem: h.oldPoem}, {quoteMode: 'manual', quoteText: '自定义'});
  assert.equal(h.calls[0].options.signal.aborted, true);
  resolve(response({text: '过时响应'})); await Promise.all([first, second]);
  assert.deepEqual(plain(h.feed.snapshot().quote), h.oldQuote);
  assert.equal(h.feed.read('quote').text, '自定义');
});

test('a response crossing midnight cannot populate the new day’s cache', async () => {
  const h = harness(); let resolve;
  h.reply(() => new Promise(done => {resolve = done;}));
  const pending = h.feed.refresh('quote'); h.nextDay();
  resolve(response({text: '昨天的响应'})); await pending;
  assert.deepEqual(plain(h.feed.snapshot().quote), h.oldQuote);
});

test('rate limits retain content and obey Retry-After', async () => {
  const h = harness(); h.reply(() => response({}, 429, '120'));
  await h.feed.refresh('quote');
  h.advance(60001); await h.feed.refresh('quote');
  assert.equal(h.calls.length, 1);
  assert.deepEqual(plain(h.feed.snapshot().quote), h.oldQuote);
  h.advance(60000); h.reply(() => response({text: '恢复'}));
  await h.feed.refresh('quote'); assert.equal(h.feed.read('quote').text, '恢复');
});

test('manual, disabled network, and disabled automatic updates do not fetch', async () => {
  const h = harness();
  h.configure({quote: h.oldQuote, poem: h.oldPoem}, {quoteMode: 'manual', poemMode: 'manual'});
  h.feed.ensure(); await h.feed.refresh('quote'); await h.feed.refresh('poem');
  h.configure({quote: null, poem: null}, {}, false);
  h.feed.ensure(); await h.feed.refresh('quote');
  h.configure({quote: null, poem: null}, {autoUpdate: false}); h.feed.ensure();
  assert.equal(h.calls.length, 0);
});

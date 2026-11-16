// A small loopback-only subset of MakcRe/KuGouMusicApi. No account-write routes.
'use strict';
const http = require('node:http');
const path = require('node:path');
const base = path.join(__dirname, 'kugou-api');
const { createRequest } = require(path.join(base, 'util/request'));
const axios = require(path.join(base, 'node_modules/axios'));
axios.defaults.timeout = 18000;
const names = ['search', 'song_url', 'search_lyric', 'lyric',
  'login_qr_key', 'login_qr_create', 'login_qr_check',
  'user_playlist', 'playlist_track_all_new'];
const routes = new Map(names.map(n => ['/' + n.replaceAll('_', '/'),
  require(path.join(base, 'module', n + '.js'))]));

function reply(res, status, body) {
  res.writeHead(status, { 'Content-Type': 'application/json; charset=utf-8',
    'Cache-Control': 'no-store' });
  res.end(JSON.stringify(body));
}
http.createServer(async (req, res) => {
  if (req.url === '/health') return reply(res, 200, {ok: true});
  if (req.method !== 'POST' || !routes.has(req.url))
    return reply(res, 404, {error: 'not_found'});
  try {
    let size = 0;
    const parts = [];
    for await (const part of req) {
      size += part.length;
      if (size > 32768) throw new Error('body_too_large');
      parts.push(part);
    }
    const input = JSON.parse(Buffer.concat(parts).toString('utf8'));
    const result = await routes.get(req.url)(input, createRequest);
    reply(res, result.status || 200, {body: result.body, cookie: result.cookie || []});
  } catch (e) {
    // Never log upstream bodies, tokens, cookies, QR keys or signed URLs.
    reply(res, 502, {error: 'upstream_unavailable'});
    console.error('Kugou request failed:', req.url);
  }
}).listen(2235, '127.0.0.1', () => console.log('Kugou adapter ready on loopback'));

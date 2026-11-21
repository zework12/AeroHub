// AeroHub site: landing page, accounts, licence keys, download, admin panel and a licence check API for the hub.
// No dependencies: Node 22.13+ (node:sqlite, node:http, node:crypto).
//
//   node server.js                         start on PORT (default 8080)
//   node server.js make-admin <username>   give an existing account admin rights
//   node server.js gen-keys <count> <days> [games] print new keys (days 0 = forever, games e.g. forts)
import http from 'node:http';
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { DatabaseSync } from 'node:sqlite';
import { fileURLToPath } from 'node:url';

const ROOT = path.dirname(fileURLToPath(import.meta.url));
const PUBLIC = path.join(ROOT, 'public');
const FILES = path.join(ROOT, 'files');           // AeroHub-Setup.exe goes here
const PORT = Number(process.env.PORT || 8080);
const SECURE_COOKIE = process.env.SECURE_COOKIE === '1';  // set to 1 behind HTTPS
const SESSION_DAYS = 30;

fs.mkdirSync(path.join(ROOT, 'data'), { recursive: true });
const db = new DatabaseSync(path.join(ROOT, 'data', 'aerohub.db'));
db.exec(`
PRAGMA journal_mode = WAL;
PRAGMA foreign_keys = ON;
CREATE TABLE IF NOT EXISTS users (
  id INTEGER PRIMARY KEY, username TEXT UNIQUE NOT NULL COLLATE NOCASE, pass TEXT NOT NULL,
  admin INTEGER NOT NULL DEFAULT 0, created INTEGER NOT NULL);
CREATE TABLE IF NOT EXISTS sessions (
  token TEXT PRIMARY KEY, user_id INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE, expires INTEGER NOT NULL);
CREATE TABLE IF NOT EXISTS keys (
  key TEXT PRIMARY KEY, days INTEGER NOT NULL, note TEXT NOT NULL DEFAULT '', created INTEGER NOT NULL,
  user_id INTEGER REFERENCES users(id) ON DELETE SET NULL, redeemed INTEGER, expires INTEGER,
  hwid TEXT, revoked INTEGER NOT NULL DEFAULT 0, last_check INTEGER);
`);
// Keys carry the paid games they open (comma-separated ids). Older databases get the column with "forts".
if (!db.prepare("SELECT 1 FROM pragma_table_info('keys') WHERE name = 'games'").get())
  db.exec("ALTER TABLE keys ADD COLUMN games TEXT NOT NULL DEFAULT 'forts'");

// Paid games (id = the hub profile name in lower case) and games every account gets as a gift.
const GAMES = { forts: 'Forts' };
const FREE_GAMES = { minecraft: 'Minecraft' };
function parseGames(text) {
  return [...new Set(String(text || '').split(',').map(g => g.trim().toLowerCase()).filter(g => GAMES[g]))];
}
// Every paid game the account has through its active keys, with the latest end date (null = forever), plus the gifts.
function accountGames(userId) {
  const out = new Map();
  for (const k of db.prepare('SELECT * FROM keys WHERE user_id = ?').all(userId)) {
    if (keyStatus(k) !== 'active') continue;
    for (const g of parseGames(k.games)) {
      const exp = k.expires || null;
      if (!out.has(g)) out.set(g, exp);
      else {
        const prev = out.get(g);
        out.set(g, prev === null || exp === null ? null : Math.max(prev, exp));
      }
    }
  }
  const list = [...out].map(([id, expires]) => ({ id, name: GAMES[id], expires, free: false }));
  for (const [id, name] of Object.entries(FREE_GAMES)) list.push({ id, name, expires: null, free: true });
  return list;
}

const now = () => Date.now();
const DAY = 86400000;

// ---------------------------------------------------------------- helpers
function hashPassword(password) {
  const salt = crypto.randomBytes(16);
  const hash = crypto.scryptSync(password, salt, 64);
  return `scrypt$${salt.toString('hex')}$${hash.toString('hex')}`;
}
function checkPassword(password, stored) {
  const [kind, saltHex, hashHex] = String(stored).split('$');
  if (kind !== 'scrypt' || !saltHex || !hashHex) return false;
  const hash = crypto.scryptSync(password, Buffer.from(saltHex, 'hex'), 64);
  const want = Buffer.from(hashHex, 'hex');
  return want.length === hash.length && crypto.timingSafeEqual(hash, want);
}
const KEY_ALPHABET = 'ABCDEFGHJKLMNPQRSTUVWXYZ23456789';  // no 0/O, 1/I
function newKey() {
  const bytes = crypto.randomBytes(16);
  let s = '';
  for (let i = 0; i < 16; i++) s += KEY_ALPHABET[bytes[i] % KEY_ALPHABET.length];
  return `AERO-${s.slice(0, 4)}-${s.slice(4, 8)}-${s.slice(8, 12)}-${s.slice(12, 16)}`;
}
function makeKeys(count, days, note = '', games = 'forts') {
  const insert = db.prepare('INSERT INTO keys (key, days, note, created, games) VALUES (?, ?, ?, ?, ?)');
  const out = [];
  for (let i = 0; i < count; i++) {
    const k = newKey();
    insert.run(k, days, note, now(), games);
    out.push(k);
  }
  return out;
}
function keyStatus(k) {
  if (k.revoked) return 'revoked';
  if (!k.user_id) return 'free';
  if (k.expires && k.expires < now()) return 'expired';
  return 'active';
}
function publicKey(k) {
  return { key: k.key, days: k.days, status: keyStatus(k), redeemed: k.redeemed, expires: k.expires, hwidBound: !!k.hwid,
           games: parseGames(k.games).map(g => GAMES[g]) };
}

function parseCookies(req) {
  const out = {};
  for (const part of (req.headers.cookie || '').split(';')) {
    const i = part.indexOf('=');
    if (i > 0) out[part.slice(0, i).trim()] = decodeURIComponent(part.slice(i + 1).trim());
  }
  return out;
}
function sessionUser(req) {
  const token = parseCookies(req).aero_session;
  if (!token || token.length !== 64) return null;
  const row = db.prepare(`SELECT u.id, u.username, u.admin, s.expires FROM sessions s JOIN users u ON u.id = s.user_id
                          WHERE s.token = ?`).get(token);
  if (!row || row.expires < now()) return null;
  return row;
}
function setSession(res, userId) {
  const token = crypto.randomBytes(32).toString('hex');
  db.prepare('INSERT INTO sessions (token, user_id, expires) VALUES (?, ?, ?)').run(token, userId, now() + SESSION_DAYS * DAY);
  res.setHeader('Set-Cookie', `aero_session=${token}; HttpOnly; SameSite=Strict; Path=/; Max-Age=${SESSION_DAYS * 86400}` +
                              (SECURE_COOKIE ? '; Secure' : ''));
}

function send(res, status, body, headers = {}) {
  res.writeHead(status, { 'Content-Type': 'application/json; charset=utf-8', 'Cache-Control': 'no-store', ...headers });
  res.end(JSON.stringify(body));
}
async function readJson(req) {
  // JSON only: a cross-site form cannot send it, which (with SameSite=Strict) keeps state-changing calls same-origin.
  if (!(req.headers['content-type'] || '').startsWith('application/json')) throw new Error('bad content type');
  let size = 0;
  const chunks = [];
  for await (const c of req) {
    size += c.length;
    if (size > 16 * 1024) throw new Error('body too large');
    chunks.push(c);
  }
  const v = JSON.parse(Buffer.concat(chunks).toString('utf8') || '{}');
  if (typeof v !== 'object' || v === null) throw new Error('bad body');
  return v;
}

// Simple per-IP limiter for login, registration and the licence API.
const hits = new Map();
function limited(req, bucket, max, windowMs) {
  const ip = req.socket.remoteAddress || '?';
  const id = `${bucket}:${ip}`;
  const t = now();
  const e = hits.get(id);
  if (!e || e.reset < t) {
    hits.set(id, { n: 1, reset: t + windowMs });
    return false;
  }
  return ++e.n > max;
}
setInterval(() => {
  const t = now();
  for (const [k, v] of hits) if (v.reset < t) hits.delete(k);
  db.prepare('DELETE FROM sessions WHERE expires < ?').run(t);
}, 10 * 60000).unref();

const MIME = { '.html': 'text/html; charset=utf-8', '.css': 'text/css; charset=utf-8', '.js': 'text/javascript; charset=utf-8',
               '.jpg': 'image/jpeg', '.png': 'image/png', '.ico': 'image/x-icon', '.svg': 'image/svg+xml' };
function serveStatic(req, res, urlPath) {
  const name = urlPath === '/' ? '/index.html' : (path.extname(urlPath) ? urlPath : `${urlPath}.html`);
  const file = path.normalize(path.join(PUBLIC, name));
  if (!file.startsWith(PUBLIC + path.sep) || !fs.existsSync(file) || !fs.statSync(file).isFile()) {
    res.writeHead(404, { 'Content-Type': 'text/plain; charset=utf-8' });
    return res.end('Not found');
  }
  res.writeHead(200, { 'Content-Type': MIME[path.extname(file)] || 'application/octet-stream',
                       'X-Content-Type-Options': 'nosniff', 'X-Frame-Options': 'DENY', 'Referrer-Policy': 'same-origin' });
  fs.createReadStream(file).pipe(res);
}

// ---------------------------------------------------------------- API
const USERNAME_RE = /^[A-Za-z0-9_.-]{3,24}$/;

const routes = {
  'POST /api/register': async (req, res) => {
    if (limited(req, 'register', 10, 3600000)) return send(res, 429, { error: 'Слишком много попыток, попробуйте позже' });
    const { username, password } = await readJson(req);
    if (typeof username !== 'string' || !USERNAME_RE.test(username))
      return send(res, 400, { error: 'Логин: 3–24 символа, латиница, цифры, _ . -' });
    if (typeof password !== 'string' || password.length < 8 || password.length > 128)
      return send(res, 400, { error: 'Пароль: от 8 до 128 символов' });
    if (db.prepare('SELECT 1 FROM users WHERE username = ?').get(username)) return send(res, 409, { error: 'Такой логин уже занят' });
    const r = db.prepare('INSERT INTO users (username, pass, created) VALUES (?, ?, ?)').run(username, hashPassword(password), now());
    setSession(res, Number(r.lastInsertRowid));
    send(res, 200, { ok: true });
  },

  'POST /api/login': async (req, res) => {
    if (limited(req, 'login', 20, 900000)) return send(res, 429, { error: 'Слишком много попыток, подождите 15 минут' });
    const { username, password } = await readJson(req);
    const u = typeof username === 'string' ? db.prepare('SELECT id, pass FROM users WHERE username = ?').get(username) : null;
    if (!u || typeof password !== 'string' || !checkPassword(password, u.pass)) return send(res, 401, { error: 'Неверный логин или пароль' });
    setSession(res, u.id);
    send(res, 200, { ok: true });
  },

  'POST /api/logout': async (req, res) => {
    const token = parseCookies(req).aero_session;
    if (token) db.prepare('DELETE FROM sessions WHERE token = ?').run(token);
    send(res, 200, { ok: true }, { 'Set-Cookie': 'aero_session=; HttpOnly; SameSite=Strict; Path=/; Max-Age=0' });
  },

  'GET /api/me': async (req, res, user) => {
    if (!user) return send(res, 401, { error: 'not signed in' });
    const keys = db.prepare('SELECT * FROM keys WHERE user_id = ? ORDER BY redeemed DESC').all(user.id).map(publicKey);
    const download = fs.existsSync(path.join(FILES, 'AeroHub-Setup.exe'));
    send(res, 200, { username: user.username, admin: !!user.admin, keys, download, games: accountGames(user.id), catalog: GAMES });
  },

  'POST /api/redeem': async (req, res, user) => {
    if (!user) return send(res, 401, { error: 'Сначала войдите' });
    if (limited(req, 'redeem', 30, 3600000)) return send(res, 429, { error: 'Слишком много попыток' });
    const { key } = await readJson(req);
    const k = typeof key === 'string' ? db.prepare('SELECT * FROM keys WHERE key = ?').get(key.trim().toUpperCase()) : null;
    if (!k || k.revoked) return send(res, 404, { error: 'Ключ не найден' });
    if (k.user_id) return send(res, 409, { error: k.user_id === user.id ? 'Этот ключ уже ваш' : 'Ключ уже активирован' });
    const expires = k.days > 0 ? now() + k.days * DAY : null;
    db.prepare('UPDATE keys SET user_id = ?, redeemed = ?, expires = ? WHERE key = ? AND user_id IS NULL').run(user.id, now(), expires, k.key);
    send(res, 200, { ok: true });
  },

  'POST /api/reset-hwid': async (req, res, user) => {
    if (!user) return send(res, 401, { error: 'Сначала войдите' });
    const { key } = await readJson(req);
    const r = db.prepare('UPDATE keys SET hwid = NULL WHERE key = ? AND user_id = ?').run(String(key || ''), user.id);
    send(res, r.changes ? 200 : 404, r.changes ? { ok: true } : { error: 'Ключ не найден' });
  },

  'GET /download': async (req, res, user) => {
    const file = path.join(FILES, 'AeroHub-Setup.exe');
    if (!user) return res.writeHead(302, { Location: '/login' }).end();
    const active = db.prepare('SELECT * FROM keys WHERE user_id = ?').all(user.id).some(k => keyStatus(k) === 'active');
    if (!active) return res.writeHead(302, { Location: '/account' }).end();
    if (!fs.existsSync(file)) return send(res, 404, { error: 'Файл ещё не загружен на сервер' });
    res.writeHead(200, { 'Content-Type': 'application/octet-stream', 'Content-Disposition': 'attachment; filename="AeroHub-Setup.exe"',
                         'Content-Length': fs.statSync(file).size });
    fs.createReadStream(file).pipe(res);
  },

  // Licence check for the hub: the first check binds the key to the machine id the hub sends.
  'POST /api/license/check': async (req, res) => {
    if (limited(req, 'license', 60, 600000)) return send(res, 429, { ok: false, reason: 'rate' });
    const { key, hwid } = await readJson(req);
    if (typeof key !== 'string' || typeof hwid !== 'string' || hwid.length < 8 || hwid.length > 128)
      return send(res, 400, { ok: false, reason: 'bad request' });
    const k = db.prepare('SELECT * FROM keys WHERE key = ?').get(key.trim().toUpperCase());
    const status = k ? keyStatus(k) : 'unknown';
    if (status !== 'active') return send(res, 200, { ok: false, reason: status === 'free' ? 'not redeemed' : status });
    if (k.hwid && k.hwid !== hwid) return send(res, 200, { ok: false, reason: 'other machine' });
    db.prepare('UPDATE keys SET hwid = COALESCE(hwid, ?), last_check = ? WHERE key = ?').run(hwid, now(), k.key);
    const owner = db.prepare('SELECT username FROM users WHERE id = ?').get(k.user_id);
    send(res, 200, { ok: true, expires: k.expires, username: owner ? owner.username : '', days: k.days, games: accountGames(k.user_id) });
  },

  // The hub signs out on this machine: the key forgets this machine id, so it can be used on another computer.
  'POST /api/license/release': async (req, res) => {
    if (limited(req, 'license', 60, 600000)) return send(res, 429, { ok: false, reason: 'rate' });
    const { key, hwid } = await readJson(req);
    if (typeof key !== 'string' || typeof hwid !== 'string') return send(res, 400, { ok: false, reason: 'bad request' });
    const r = db.prepare('UPDATE keys SET hwid = NULL WHERE key = ? AND hwid = ?').run(key.trim().toUpperCase(), hwid);
    send(res, 200, { ok: r.changes > 0 });
  },

  // ---------------- admin
  'GET /api/admin/overview': async (req, res, user) => {
    if (!user?.admin) return send(res, 403, { error: 'forbidden' });
    const keys = db.prepare(`SELECT k.*, u.username FROM keys k LEFT JOIN users u ON u.id = k.user_id ORDER BY k.created DESC LIMIT 500`).all()
      .map(k => ({ ...publicKey(k), note: k.note, created: k.created, username: k.username, lastCheck: k.last_check }));
    const users = db.prepare('SELECT id, username, admin, created FROM users ORDER BY created DESC LIMIT 500').all();
    send(res, 200, { keys, users, catalog: GAMES });
  },
  'POST /api/admin/keys': async (req, res, user) => {
    if (!user?.admin) return send(res, 403, { error: 'forbidden' });
    const { count, days, note, games } = await readJson(req);
    const list = parseGames(Array.isArray(games) ? games.join(',') : games);
    if (!list.length) return send(res, 400, { error: 'Выберите хотя бы одну игру' });
    const c = Number(count), d = Number(days);
    if (!Number.isInteger(c) || c < 1 || c > 100) return send(res, 400, { error: 'Количество: 1–100' });
    if (!Number.isInteger(d) || d < 0 || d > 3650) return send(res, 400, { error: 'Дней: 0–3650 (0 = навсегда)' });
    send(res, 200, { keys: makeKeys(c, d, String(note || '').slice(0, 80), list.join(',')) });
  },
  'POST /api/admin/revoke': async (req, res, user) => {
    if (!user?.admin) return send(res, 403, { error: 'forbidden' });
    const { key, revoked } = await readJson(req);
    const r = db.prepare('UPDATE keys SET revoked = ? WHERE key = ?').run(revoked ? 1 : 0, String(key || ''));
    send(res, r.changes ? 200 : 404, { ok: !!r.changes });
  },
  'POST /api/admin/reset-hwid': async (req, res, user) => {
    if (!user?.admin) return send(res, 403, { error: 'forbidden' });
    const { key } = await readJson(req);
    const r = db.prepare('UPDATE keys SET hwid = NULL WHERE key = ?').run(String(key || ''));
    send(res, r.changes ? 200 : 404, { ok: !!r.changes });
  },
};

// ---------------------------------------------------------------- CLI
const [cmd, a1, a2] = process.argv.slice(2);
if (cmd === 'make-admin') {
  const r = db.prepare('UPDATE users SET admin = 1 WHERE username = ?').run(String(a1 || ''));
  console.log(r.changes ? `${a1} is now an admin` : `no user ${a1}`);
  process.exit(r.changes ? 0 : 1);
} else if (cmd === 'gen-keys') {
  const c = Number(a1), d = Number(a2 ?? 30);
  if (!Number.isInteger(c) || c < 1 || c > 1000 || !Number.isInteger(d) || d < 0) {
    console.error('usage: node server.js gen-keys <count 1-1000> <days, 0 = forever>');
    process.exit(1);
  }
  const games = parseGames(process.argv[5] || 'forts');
  console.log(makeKeys(c, d, 'cli', games.join(',') || 'forts').join('\n'));
  process.exit(0);
} else if (cmd) {
  console.error(`unknown command ${cmd}`);
  process.exit(1);
}

http.createServer(async (req, res) => {
  const url = new URL(req.url, 'http://local');
  const handler = routes[`${req.method} ${url.pathname}`];
  try {
    if (handler) return await handler(req, res, sessionUser(req));
    if (req.method === 'GET' && !url.pathname.startsWith('/api/')) return serveStatic(req, res, url.pathname);
    send(res, 404, { error: 'not found' });
  } catch (e) {
    if (!res.headersSent) send(res, 400, { error: 'Неверный запрос' });
    else res.destroy();
  }
}).listen(PORT, process.env.HOST || '0.0.0.0', () => console.log(`AeroHub site on port ${PORT}`));

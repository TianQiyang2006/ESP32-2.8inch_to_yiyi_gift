/**
 * Portable rules for the 320 x 240 design review. No DOM or drawing dependency.
 * The rules below are an original preview implementation, not a completed port.
 * Gameplay references: CrowPanel Pocket Arcade (Wojciech Lepczynski, MIT),
 * ESP32-CYD-Fruit-Ninja (MIT), and ESP32-S3-Mastermind (Va&Cob, MIT).
 * Full upstream notices and firmware integration belong to the approved port.
 * Units: pixels and seconds. All public operations mutate and return state.
 */

const FIXED_STEP = 1 / 120;
const MAX_FRAME = 0.25;
const secrets = new WeakMap();
const clone = value => JSON.parse(JSON.stringify(value));
const clamp = (value, low, high) => Math.max(low, Math.min(high, value));

export const FRUIT_COLORS = {apple: '#D86D58', orange: '#E5AA4B', melon: '#639B73', bomb: '#243C34'};
export const CODE_COLORS = ['#D86D58', '#E5AA4B', '#58966E', '#6297BB', '#9C7BBA', '#B87092'];
export const GEOMETRY = {
  width: 320, height: 240, header: 32,
  runner: {ground: 174, buttonY: 192, buttonH: 48},
  slide: {x: 8, y: 36, cell: 32, columns: 6, rows: 6, exitRow: 2},
  fruit: {x: 0, y: 32, w: 320, h: 208},
};

const BASE_PIECES = [
  {id: 'key', x: 0, y: 2, w: 2, h: 1, color: '#D7AA52', key: true},
  {id: 'A', x: 2, y: 1, w: 2, h: 2, color: '#739D8A'},
  {id: 'B', x: 4, y: 0, w: 1, h: 3, color: '#7F9EB6'},
  {id: 'C', x: 2, y: 4, w: 2, h: 1, color: '#B59AB5'},
  {id: 'D', x: 0, y: 4, w: 1, h: 2, color: '#BBA380'},
  {id: 'E', x: 5, y: 4, w: 1, h: 2, color: '#86A99D'},
];

export const SLIDE_LEVELS = [
  {pieces: clone(BASE_PIECES), solution: [
    {piece: 'C', dx: 0, dy: 1}, {piece: 'A', dx: 0, dy: 2},
    {piece: 'B', dx: 0, dy: 3}, {piece: 'key', dx: 4, dy: 0},
  ]},
  {pieces: BASE_PIECES.map(piece => ({...piece, ...(piece.id === 'A' ? {y: 0} : {})})), solution: [
    {piece: 'C', dx: 0, dy: 1}, {piece: 'A', dx: 0, dy: 3},
    {piece: 'B', dx: 0, dy: 3}, {piece: 'key', dx: 4, dy: 0},
  ]},
  {pieces: BASE_PIECES.map(piece => ({...piece,
    ...({key: {x: 1}, A: {x: 3}, B: {x: 5}, C: {x: 3}, E: {x: 1}}[piece.id] || {}),
  })), solution: [
    {piece: 'C', dx: 0, dy: 1}, {piece: 'A', dx: 0, dy: 2},
    {piece: 'B', dx: 0, dy: 3}, {piece: 'key', dx: 3, dy: 0},
  ]},
];

function random(state) {
  let value = state.rng >>> 0;
  value ^= value << 13; value ^= value >>> 17; value ^= value << 5;
  state.rng = value >>> 0;
  return state.rng / 4294967296;
}

function baseState(id, seed) {
  const initial = (Number.isFinite(seed) ? seed : 20261112) >>> 0;
  return {id, status: 'ready', seed: initial || 1, rng: initial || 1,
    time: 0, score: 0, accumulator: 0, revision: 0, reason: ''};
}

function secretForSeed(seed) {
  const temp = {rng: ((seed ^ 0x9E3779B9) >>> 0) || 1};
  return Array.from({length: 4}, () => 1 + Math.floor(random(temp) * 6));
}

export function createGame(id, seed = 20261112) {
  const state = baseState(id, seed);
  if (id === 'runner') {
    Object.assign(state, {
      player: {x: 54, y: 146, w: 24, h: 28, vy: 0, grounded: true},
      speed: 110, distance: 0, coinsCollected: 0, jumpCount: 0,
      obstacles: [{id: 1, x: 318, y: 148, w: 18, h: 26}],
      coins: [{id: 2, x: 242, y: 114, r: 5}],
      nextId: 3, obstacleTimer: 2.1,
    });
  } else if (id === 'slide') {
    loadLevel(state, 1);
  } else if (id === 'fruit') {
    Object.assign(state, {lives: 3, entities: [], effects: [], nextId: 1,
      waveTimer: 0.08, combo: 0, comboUntil: 0, fruitsCut: 0, mistakes: 0});
  } else if (id === 'code') {
    Object.assign(state, {guess: [null, null, null, null], selected: 0,
      history: [], attempts: 12, historyScroll: 0});
    secrets.set(state, secretForSeed(state.seed));
  } else {
    throw new Error(`Unknown game: ${id}`);
  }
  return state;
}

function loadLevel(state, number) {
  const level = ((number - 1) % SLIDE_LEVELS.length + SLIDE_LEVELS.length) % SLIDE_LEVELS.length;
  Object.assign(state, {level: level + 1, pieces: clone(SLIDE_LEVELS[level].pieces),
    moves: 0, undoStack: [], hintUsed: 0, solutionRoute: clone(SLIDE_LEVELS[level].solution)});
}

function reset(state) {
  const level = state.level;
  const fresh = createGame(state.id, state.seed);
  if (state.id === 'slide') loadLevel(fresh, level);
  const secret = secrets.get(fresh);
  for (const key of Object.keys(state)) delete state[key];
  Object.assign(state, fresh);
  if (secret) secrets.set(state, secret);
}

export function canSubmit(state) {
  return state.id === 'code' && state.status === 'playing' &&
    state.guess.length === 4 && state.guess.every(value => Number.isInteger(value) && value >= 1 && value <= 6);
}

export function scoreFeedback(secret, guess) {
  if (!Array.isArray(secret) || !Array.isArray(guess) || secret.length !== 4 || guess.length !== 4)
    throw new Error('A code and guess must both have four colors.');
  let exact = 0, misplaced = 0;
  const remainingSecret = Array(7).fill(0), remainingGuess = Array(7).fill(0);
  for (let i = 0; i < 4; i++) {
    if (secret[i] === guess[i]) exact++;
    else {remainingSecret[secret[i]]++; remainingGuess[guess[i]]++;}
  }
  for (let color = 1; color <= 6; color++) misplaced += Math.min(remainingSecret[color], remainingGuess[color]);
  return {exact, misplaced};
}

/** Only the review tests and the result overlay should call this. */
export function debugSecret(state) {
  return state.id === 'code' ? [...(secrets.get(state) || secretForSeed(state.seed))] : null;
}

function overlaps(a, b) {
  return a.x < b.x + b.w && a.x + a.w > b.x && a.y < b.y + b.h && a.y + a.h > b.y;
}

export function canMove(state, id, dx, dy) {
  if (state.id !== 'slide' || !Number.isInteger(dx) || !Number.isInteger(dy) ||
      (dx === 0 && dy === 0) || (dx !== 0 && dy !== 0)) return false;
  const piece = state.pieces.find(item => item.id === id);
  if (!piece) return false;
  const count = Math.abs(dx || dy), sx = Math.sign(dx), sy = Math.sign(dy);
  // Check every crossed cell; a large drag cannot tunnel through another block.
  for (let step = 1; step <= count; step++) {
    const next = {...piece, x: piece.x + sx * step, y: piece.y + sy * step};
    if (next.x < 0 || next.y < 0 || next.x + next.w > 6 || next.y + next.h > 6) return false;
    if (state.pieces.some(other => other !== piece && overlaps(next, other))) return false;
  }
  return true;
}

function simplifyRoute(route) {
  const result = [];
  for (const move of route) {
    const last = result.at(-1);
    if (last && last.piece === move.piece && ((!last.dx && !move.dx) || (!last.dy && !move.dy))) {
      const firstDirection = Math.sign(last.dx || last.dy);
      last.dx += move.dx; last.dy += move.dy;
      if (Math.sign(last.dx || last.dy) !== firstDirection) last.recovery = !!move.recovery;
      if (!last.dx && !last.dy) result.pop();
    } else result.push({...move});
  }
  return result;
}

function movePiece(state, payload, hinted = false) {
  const id = typeof payload.piece === 'number' ? state.pieces[payload.piece]?.id : payload.piece;
  const dx = payload.dx, dy = payload.dy;
  if (!canMove(state, id, dx, dy)) return false;
  state.undoStack.push({pieces: state.pieces.map(piece => ({id: piece.id, x: piece.x, y: piece.y})),
    route: clone(state.solutionRoute), moves: state.moves});
  const piece = state.pieces.find(item => item.id === id);
  piece.x += dx; piece.y += dy;
  state.moves++;
  if (hinted) state.hintUsed++;
  // This reversible route is a guaranteed solution from every reachable review state.
  // Following a hint cancels the corresponding reverse move instead of adding a loop.
  state.solutionRoute = simplifyRoute([{piece: id, dx: -dx, dy: -dy, recovery: true}, ...state.solutionRoute]);
  const key = state.pieces.find(item => item.key);
  if (key.y === 2 && key.x + key.w === 6) {
    state.status = 'won'; state.score = Math.max(1, 1000 - state.moves * 10);
  }
  state.revision++;
  return true;
}

export function getHint(state) {
  if (state.id !== 'slide' || state.status !== 'playing') return null;
  const key = state.pieces.find(piece => piece.key);
  const distance = 6 - key.x - key.w;
  if (key.y === 2 && distance > 0 && canMove(state, key.id, distance, 0))
    return {piece: key.id, dx: distance, dy: 0, recovery: false};
  const next = state.solutionRoute[0];
  return next && canMove(state, next.piece, next.dx, next.dy) ? {...next, recovery: !!next.recovery} : null;
}

export function action(state, type, payload = {}) {
  if (type === 'restart') {reset(state); return state;}
  if (type === 'start') {
    if (state.status === 'ready') {state.status = 'playing'; state.revision++;}
    return state;
  }
  if (state.id === 'slide' && type === 'next' && state.status === 'won') {
    loadLevel(state, state.level + 1); state.status = 'playing'; state.time = 0;
    state.score = 0; state.accumulator = 0; state.revision++; return state;
  }
  if (state.id === 'slide' && type === 'undo' && ['playing', 'won'].includes(state.status)) {
    const snapshot = state.undoStack.pop();
    if (snapshot) {
      for (const saved of snapshot.pieces) Object.assign(state.pieces.find(piece => piece.id === saved.id), saved);
      state.solutionRoute = snapshot.route; state.moves = snapshot.moves;
      state.status = 'playing'; state.score = 0; state.revision++;
    }
    return state;
  }
  if (state.status !== 'playing') return state;
  if (state.id === 'runner' && type === 'jump' && state.player.grounded) {
    state.player.vy = -335; state.player.grounded = false; state.jumpCount++; state.revision++;
  } else if (state.id === 'slide' && type === 'move') {
    movePiece(state, payload);
  } else if (state.id === 'slide' && type === 'hint') {
    const hint = getHint(state); if (hint) movePiece(state, hint, true);
  } else if (state.id === 'fruit' && type === 'stroke') {
    cutStroke(state, payload);
  } else if (state.id === 'code' && type === 'select') {
    if (Number.isInteger(payload.index) && payload.index >= 0 && payload.index < 4) {
      state.selected = payload.index; state.revision++;
    }
  } else if (state.id === 'code' && type === 'color') {
    if (Number.isInteger(payload.value) && payload.value >= 1 && payload.value <= 6) {
      state.guess[state.selected] = payload.value;
      const empty = state.guess.findIndex((value, index) => index > state.selected && value === null);
      state.selected = empty >= 0 ? empty : (state.selected + 1) % 4;
      state.revision++;
    }
  } else if (state.id === 'code' && type === 'submit' && canSubmit(state)) {
    const feedback = scoreFeedback(debugSecret(state), state.guess);
    state.history.push({guess: [...state.guess], ...feedback});
    state.historyScroll = Math.max(0, state.history.length - 3);
    if (feedback.exact === 4) {
      state.status = 'won'; state.score = (13 - state.history.length) * 100;
    } else if (state.history.length >= state.attempts) {
      state.status = 'lost'; state.reason = '机会用完了';
    } else {state.guess = [null, null, null, null]; state.selected = 0;}
    state.revision++;
  } else if (state.id === 'code' && type === 'scroll') {
    if (Number.isFinite(payload.rows)) {
      state.historyScroll = clamp(state.historyScroll + Math.round(payload.rows), 0, Math.max(0, state.history.length - 3));
      state.revision++;
    }
  }
  return state;
}

function runnerTick(state, dt) {
  const player = state.player;
  state.speed = Math.min(190, 110 + state.time * 0.45);
  state.distance += state.speed * dt;
  if (!player.grounded) {
    player.vy += 820 * dt; player.y += player.vy * dt;
    if (player.y + player.h >= 174) {player.y = 174 - player.h; player.vy = 0; player.grounded = true;}
  }
  for (const obstacle of state.obstacles) obstacle.x -= state.speed * dt;
  for (const coin of state.coins) coin.x -= state.speed * dt;
  state.obstacles = state.obstacles.filter(item => item.x + item.w >= -4);
  state.coins = state.coins.filter(item => item.x + item.r >= -4);
  state.obstacleTimer -= dt;
  if (state.obstacleTimer <= 0) {
    const w = 16 + Math.floor(random(state) * 13), h = 20 + Math.floor(random(state) * 15);
    state.obstacles.push({id: state.nextId++, x: 326, y: 174 - h, w, h});
    state.coins.push({id: state.nextId++, x: 282, y: 109 + random(state) * 12, r: 5});
    state.obstacleTimer += 1.45 + random(state) * 0.65;
  }
  const hitbox = {x: player.x + 4, y: player.y + 3, w: player.w - 8, h: player.h - 5};
  if (state.obstacles.some(obstacle => overlaps(hitbox, {x: obstacle.x + 2, y: obstacle.y + 2,
    w: obstacle.w - 4, h: obstacle.h - 2}))) {
    state.status = 'lost'; state.reason = '撞到障碍了';
  }
  state.coins = state.coins.filter(coin => {
    const x = clamp(coin.x, player.x + 2, player.x + player.w - 2);
    const y = clamp(coin.y, player.y + 2, player.y + player.h - 2);
    if ((coin.x - x) ** 2 + (coin.y - y) ** 2 <= coin.r ** 2) {state.coinsCollected++; return false;}
    return true;
  });
  state.score = Math.floor(state.distance / 12) + state.coinsCollected * 10;
}

function spawnWave(state) {
  const count = 2 + Math.floor(random(state) * 2);
  for (let i = 0; i < count && state.entities.length < 12; i++) {
    const r = 12 + random(state) * 3, x = 34 + random(state) * 252;
    const vy = -(385 + random(state) * 45), flight = -2 * vy / 600;
    const vx = clamp((random(state) - 0.5) * 70, -(x - r - 3) / flight, (317 - r - x) / flight);
    const bomb = i === count - 1 && random(state) < 0.26;
    const kind = bomb ? 'bomb' : ['apple', 'orange', 'melon'][Math.floor(random(state) * 3)];
    state.entities.push({id: state.nextId++, kind, x, y: 255 + i * 7, vx, vy, r, seen: false, age: 0});
  }
}

function effect(state, kind, entity) {
  state.effects.push({kind, x: entity.x, y: clamp(entity.y, 44, 226), r: entity.r,
    color: FRUIT_COLORS[entity.kind] || FRUIT_COLORS.apple, life: 0.45, maxLife: 0.45});
  if (state.effects.length > 16) state.effects.shift();
}

function fruitTick(state, dt) {
  for (const item of state.effects) item.life -= dt;
  state.effects = state.effects.filter(item => item.life > 0);
  if (state.time >= state.comboUntil) state.combo = 0;
  state.waveTimer -= dt;
  if (state.waveTimer <= 0) {spawnWave(state); state.waveTimer += 1.35 + random(state) * 0.4;}
  const remaining = [];
  for (const entity of state.entities) {
    entity.age += dt; entity.vy += 600 * dt; entity.x += entity.vx * dt; entity.y += entity.vy * dt;
    if (entity.y - entity.r <= 240) entity.seen = true;
    if (entity.seen && entity.vy > 0 && entity.y - entity.r > 240) {
      if (entity.kind !== 'bomb') {state.lives--; state.mistakes++; effect(state, 'miss', entity);}
    } else remaining.push(entity);
  }
  state.entities = remaining;
  state.lives = Math.max(0, state.lives);
  if (!state.lives) {state.status = 'lost'; state.reason = '三次失误，休息一下';}
}

export function segmentHitsCircle(x1, y1, x2, y2, cx, cy, r) {
  const dx = x2 - x1, dy = y2 - y1;
  const denominator = dx * dx + dy * dy;
  const t = denominator ? clamp(((cx - x1) * dx + (cy - y1) * dy) / denominator, 0, 1) : 0;
  return (x1 + t * dx - cx) ** 2 + (y1 + t * dy - cy) ** 2 <= r * r;
}

function clipStroke(p) {
  let {x1, y1, x2, y2} = p;
  if (![x1, y1, x2, y2].every(Number.isFinite)) return null;
  const dx = x2 - x1, dy = y2 - y1;
  let low = 0, high = 1;
  for (const [a, b] of [[-dx, x1], [dx, 320 - x1], [-dy, y1 - 32], [dy, 240 - y1]]) {
    if (!a) {if (b < 0) return null; continue;}
    const t = b / a;
    if (a < 0) low = Math.max(low, t); else high = Math.min(high, t);
    if (low > high) return null;
  }
  return {x1: x1 + dx * low, y1: y1 + dy * low, x2: x1 + dx * high, y2: y1 + dy * high};
}

function cutStroke(state, input) {
  const line = clipStroke(input); if (!line) return;
  let count = 0, bombs = 0;
  state.entities = state.entities.filter(entity => {
    if (entity.y < 32 + entity.r || entity.y > 240 + entity.r ||
      !segmentHitsCircle(line.x1, line.y1, line.x2, line.y2, entity.x, entity.y, entity.r)) return true;
    if (entity.kind === 'bomb') {bombs++; effect(state, 'bomb', entity);}
    else {count++; effect(state, 'slice', entity);}
    return false;
  });
  if (count) {
    state.fruitsCut += count; state.score += count * 10 + Math.max(0, count - 1) * 5;
    state.combo = count; state.comboUntil = state.time + 0.65;
  }
  state.lives = Math.max(0, state.lives - bombs); state.mistakes += bombs;
  if (!state.lives) {state.status = 'lost'; state.reason = '三次失误，休息一下';}
  if (count || bombs) state.revision++;
}

export function step(state, dt) {
  if (state.status !== 'playing' || !Number.isFinite(dt) || dt <= 0) return state;
  state.accumulator += Math.min(dt, MAX_FRAME);
  while (state.accumulator + 1e-10 >= FIXED_STEP && state.status === 'playing') {
    state.accumulator = Math.max(0, state.accumulator - FIXED_STEP);
    state.time += FIXED_STEP;
    if (state.id === 'runner') runnerTick(state, FIXED_STEP);
    else if (state.id === 'fruit') fruitTick(state, FIXED_STEP);
  }
  return state;
}

import test from 'node:test';
import assert from 'node:assert/strict';
import {createGame, step, action, getHint, canMove, canSubmit, scoreFeedback,
  debugSecret, SLIDE_LEVELS, segmentHitsCircle} from './games.js';

const start = id => action(createGame(id, 20261112), 'start');
const positions = state => state.pieces.map(({id, x, y}) => ({id, x, y}));

test('every slide review level has a legal complete solution and full undo', () => {
  const state = start('slide');
  for (let level = 0; level < SLIDE_LEVELS.length; level++) {
    assert.equal(state.level, level + 1);
    const initial = positions(state);
    for (const move of SLIDE_LEVELS[level].solution) {
      assert.ok(canMove(state, move.piece, move.dx, move.dy));
      action(state, 'move', move);
    }
    assert.equal(state.status, 'won');
    assert.equal(state.moves, SLIDE_LEVELS[level].solution.length);
    for (let i = 0; i < SLIDE_LEVELS[level].solution.length; i++) action(state, 'undo');
    assert.deepEqual(positions(state), initial);
    assert.equal(state.status, 'playing');
    assert.equal(state.moves, 0);
    for (const move of SLIDE_LEVELS[level].solution) action(state, 'move', move);
    if (level + 1 < SLIDE_LEVELS.length) action(state, 'next');
  }
});

test('slide cannot tunnel, move diagonally, or leave board', () => {
  const state = start('slide'), initial = positions(state);
  for (const move of [{piece: 'key', dx: 4, dy: 0}, {piece: 'D', dx: -1, dy: 0},
    {piece: 'A', dx: 1, dy: 1}, {piece: 'A', dx: 0, dy: 2}]) action(state, 'move', move);
  assert.deepEqual(positions(state), initial);
  assert.equal(state.moves, 0);
});

test('hint remains a valid route after legal player detours and undo', () => {
  const state = start('slide');
  action(state, 'move', {piece: 'D', dx: 1, dy: 0});
  action(state, 'move', {piece: 'E', dx: -1, dy: 0});
  action(state, 'undo');
  assert.equal(getHint(state).recovery, true, 'a detour is explicitly marked as recovery');
  let count = 0;
  while (state.status === 'playing' && count++ < 24) {
    const hint = getHint(state);
    assert.ok(hint, 'a reachable board must have a hint');
    assert.ok(canMove(state, hint.piece, hint.dx, hint.dy));
    action(state, 'hint');
  }
  assert.equal(state.status, 'won');
  assert.ok(count < 24);
  assert.ok(state.hintUsed > 0);
});

test('Mastermind counts duplicates without double counting', () => {
  assert.deepEqual(scoreFeedback([1, 1, 2, 2], [1, 2, 1, 3]), {exact: 1, misplaced: 2});
  assert.deepEqual(scoreFeedback([1, 1, 1, 2], [2, 2, 2, 1]), {exact: 0, misplaced: 2});
  assert.deepEqual(scoreFeedback([1, 1, 1, 1], [1, 2, 3, 4]), {exact: 1, misplaced: 0});
  assert.deepEqual(scoreFeedback([1, 2, 3, 4], [4, 3, 2, 1]), {exact: 0, misplaced: 4});
});

test('Mastermind incomplete submissions stay disabled, a correct code wins', () => {
  const state = start('code');
  action(state, 'color', {value: 3}); action(state, 'submit');
  assert.equal(canSubmit(state), false); assert.equal(state.history.length, 0);
  const secret = debugSecret(state);
  for (let i = 0; i < 4; i++) {
    action(state, 'select', {index: i}); action(state, 'color', {value: secret[i]});
  }
  assert.ok(canSubmit(state)); action(state, 'submit');
  assert.equal(state.status, 'won'); assert.equal(state.history[0].exact, 4);
  assert.ok(!JSON.stringify(state).includes('secret'));
});

test('Mastermind loses on attempt twelve and completed games freeze', () => {
  const state = start('code');
  const wrong = (debugSecret(state)[0] % 6) + 1;
  for (let round = 0; round < 12; round++) {
    for (let i = 0; i < 4; i++) {
      action(state, 'select', {index: i}); action(state, 'color', {value: wrong});
    }
    action(state, 'submit');
  }
  assert.equal(state.status, 'lost'); assert.equal(state.history.length, 12);
  const snapshot = JSON.stringify(state); action(state, 'color', {value: 1}); step(state, 1);
  assert.equal(JSON.stringify(state), snapshot);
});

test('runner jump has no repeat while airborne and lands on ground', () => {
  const state = start('runner'); state.obstacles = []; state.obstacleTimer = 100;
  action(state, 'jump'); const vy = state.player.vy;
  action(state, 'jump'); action(state, 'jump');
  assert.equal(state.jumpCount, 1); assert.equal(state.player.vy, vy);
  let highest = state.player.y;
  for (let i = 0; i < 120; i++) {step(state, 1 / 120); highest = Math.min(highest, state.player.y);}
  assert.ok(highest < 100); assert.equal(state.player.y, 146); assert.ok(state.player.grounded);
  step(state, 0.2); assert.equal(state.jumpCount, 1, 'landing does not auto jump');
  action(state, 'jump'); assert.equal(state.jumpCount, 2);
});

test('fixed step is frame independent, invalid dt and long frames are bounded', () => {
  const a = start('runner'), b = start('runner');
  for (const s of [a, b]) {s.obstacles = []; s.obstacleTimer = 100;}
  for (let i = 0; i < 60; i++) step(a, 1 / 60);
  for (let i = 0; i < 120; i++) step(b, 1 / 120);
  assert.ok(Math.abs(a.distance - b.distance) < 1e-9);
  const time = a.time; step(a, -1); step(a, NaN); assert.equal(a.time, time);
  step(a, 10); assert.ok(a.time - time <= 0.25000001);
});

test('runner collision ends game, and ready/restarted state does not run', () => {
  const state = createGame('runner'); const initial = state.distance; step(state, 0.2);
  assert.equal(state.distance, initial); action(state, 'start');
  state.obstacles = [{id: 9, x: 58, y: 145, w: 20, h: 29}]; step(state, 1 / 120);
  assert.equal(state.status, 'lost');
  action(state, 'restart'); assert.equal(state.status, 'ready'); assert.equal(state.score, 0);
});

test('fruit uses swept segments so fast strokes cannot tunnel through fruit', () => {
  assert.ok(segmentHitsCircle(0, 100, 320, 100, 160, 100, 13));
  assert.ok(!segmentHitsCircle(0, 120, 320, 120, 160, 100, 13));
  const state = start('fruit');
  state.entities = [
    {id: 1, kind: 'apple', x: 100, y: 100, r: 13},
    {id: 2, kind: 'orange', x: 200, y: 100, r: 13},
    {id: 3, kind: 'bomb', x: 250, y: 170, r: 13},
  ];
  action(state, 'stroke', {x1: 0, y1: 100, x2: 320, y2: 100});
  assert.equal(state.entities.length, 1); assert.equal(state.fruitsCut, 2);
  assert.equal(state.score, 25); assert.equal(state.combo, 2); assert.equal(state.lives, 3);
  action(state, 'stroke', {x1: 200, y1: 170, x2: 300, y2: 170});
  assert.equal(state.lives, 2); assert.equal(state.entities.length, 0);
});

test('fruit header does not cut, misses cost one life, missed bombs are harmless', () => {
  const state = start('fruit'); state.waveTimer = 100;
  state.entities = [{id: 1, kind: 'apple', x: 160, y: 20, r: 12}];
  action(state, 'stroke', {x1: 0, y1: 20, x2: 320, y2: 20}); assert.equal(state.entities.length, 1);
  state.entities = [
    {id: 2, kind: 'apple', x: 100, y: 254, r: 12, vx: 0, vy: 90, age: 1, seen: true},
    {id: 3, kind: 'bomb', x: 200, y: 254, r: 12, vx: 0, vy: 90, age: 1, seen: true},
  ];
  step(state, 1 / 120); assert.equal(state.lives, 2); assert.equal(state.mistakes, 1);
  step(state, 0.1); assert.equal(state.lives, 2, 'a missed fruit is removed exactly once');
});

test('generated fruit waves stay inside horizontal playfield and entity budget', () => {
  for (let seed = 1; seed <= 50; seed++) {
    const state = action(createGame('fruit', seed), 'start');
    for (let frame = 0; frame < 1200; frame++) {
      state.lives = 100;
      step(state, 1 / 120);
      assert.ok(state.entities.length <= 12);
      for (const item of state.entities) {
        assert.ok(item.x - item.r >= 1, `left edge: seed ${seed}`);
        assert.ok(item.x + item.r <= 319, `right edge: seed ${seed}`);
      }
    }
  }
});

test('all four rules states remain JSON serializable', () => {
  for (const id of ['runner', 'slide', 'fruit', 'code']) {
    const state = start(id); step(state, 0.1);
    assert.equal(JSON.parse(JSON.stringify(state)).id, id);
  }
});

// Execute the prototype's actual controls with a deterministic clock and audio mock.
// No browser, microphone, device access or duplicated production state machine.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const html = fs.readFileSync(path.join(__dirname, '../../prototype/index.html'), 'utf8');
function section(from, to) {
  const start = html.indexOf(from), end = html.indexOf(to, start);
  assert(start >= 0 && end > start, `Missing prototype section: ${from}`);
  return html.slice(start, end);
}
let time = 1000, sequence = 0;
const timers = new Map(), persisted = [], drawn = [];
const canvas = new Proxy({}, {get: (_, key) => key === 'measureText' ? s => ({width: s.length * 8}) : () => {}});
const gain = {setValueAtTime() {}, linearRampToValueAtTime(value) { gain.lastRamp = value; }};
const audio = {currentTime: 1};
let stopCalls = 0;
const context = vm.createContext({
  W: 240, H: 320, SR: 16000, mix: () => '', S: {volume: 80, a4: 440, calm: false, quiet: 60, cal: 110, sens: 0, muteUntil: 0},
  C: {cyan: '#35f0dc', green: '#5cff8a', white: '#ece8ff', bg: '#0a0716'},
  F: {mono: 'mono', sans: 'sans'}, c: canvas,
  T: (...args) => drawn.push(args[0]), rr() {}, nabo() {}, header() {}, toast() {},
  store: {set: (key, value) => persisted.push([key, value])},
  clamp: (v, lo, hi) => Math.min(hi, Math.max(lo, v)),
  cycleSens() {}, syncSliders() {}, showInfo() {},
  outputBus: {gain}, AC: audio, A: {db: -100}, src: {kind: 'mic'},
  rec: {on: false}, lastN: (_, out) => out,
  VOICES: Array.from({length: 12}, (_, i) => ({n: 'Voice ' + i, d: 'Description'})),
  renderVoice: () => ({d: new Int16Array(4000), out: new Float32Array(8000)}),
  adpcmBytes: n => n / 2,
  playPCM: (_, owner) => {owner.playGain = {gain}; owner.playSource = {stop() { stopCalls++; }};},
  nowMs: () => time,
  setTimeout: (fn, ms) => {const id = ++sequence; timers.set(id, {fn, at: time + ms}); return id;},
  clearTimeout: id => timers.delete(id),
});
vm.runInContext('let scr; const MENU={up(){},info:{}};\n' +
  section('const KC=', 'function mulberry32(') +
  section('const ECHO={', '// ================= 吉他工具箱') +
  section('const A4L=', 'const SPLASH={') +
  html.split(/\r?\n/).filter(line => line.startsWith('function go(s)') || line.startsWith('function back()')).join('\n') +
  '\n' + section('const K={', "document.querySelectorAll('.keys button')"), context);
const run = source => vm.runInContext(source, context);
function advance(ms) {
  time += ms; audio.currentTime = time / 1000;
  for (const [id, timer] of [...timers]) if (timer.at <= time) {timers.delete(id); timer.fn();}
}
const tap = key => run(`press('${key}');release('${key}')`);
function draw() {drawn.length = 0; run('scr.draw(0)'); return drawn.join('\n');}

async function main() {
  run('go(SETTINGS)'); tap('ok'); tap('up');
  assert.equal(run('S.volume'), 80); assert.equal(run("hintLit('▲')"), true);
  advance(299); assert.equal(run('S.volume'), 80);
  advance(1); assert.equal(run('S.volume'), 70);
  assert.deepEqual(persisted.at(-1), ['volume', 70]);
  assert.equal(run("hintLit('▲')"), false);
  tap('down'); assert.equal(run('S.volume'), 80);
  for (let i = 0; i < 12; i++) tap('down');
  assert.equal(run('S.volume'), 100);
  for (let i = 0; i < 12; i++) {tap('up'); advance(300);}
  assert.equal(run('S.volume'), 0);
  run('S.volume=80'); const saves = persisted.length;
  tap('up'); advance(100); tap('up');
  assert.equal(run('SETTINGS.volumeEdit'), false);
  assert.equal(run('S.volume'), 80); assert.equal(persisted.length, saves);
  tap('up'); advance(100); tap('up'); assert.equal(run('scr===MENU'), true);
  advance(400); assert.equal(run('S.volume'), 80);
  run('go(SETTINGS)'); tap('ok'); tap('up'); run('go(MENU)');
  advance(500); assert.equal(run('S.volume'), 80);
  run('go(SETTINGS);press("down");press("down");release("down")');
  assert.equal(run('SETTINGS.sel'), 1);
  console.log('PASS prototype volume, limits, local save, pending feedback, duplicate presses and return cancellation');

  run('go(ECHO)'); assert.match(draw(), /录音/);
  await run('scr.okDown()'); assert.equal(run('rec.on'), true);
  assert.match(draw(), /松开/);
  run('ECHO.up()'); assert.equal(run('ECHO.v'), 1); assert.equal(run('ECHO.state'), 'rec');
  run('rec.n=8000;ECHO.okUp()'); assert.equal(run('ECHO.state'), 'play');
  const playingText = draw();
  assert.match(playingText, /停止/); assert.match(playingText, /NABO 正在复读/);
  assert.doesNotMatch(playingText, /ADPCM|录音/);
  run('ECHO.up()'); assert.equal(run('ECHO.v'), 2); assert.equal(run('ECHO.state'), 'play');
  const stopsBefore = stopCalls;
  run("src.kind='none'"); await run('ECHO.okDown()'); run('ECHO.okUp()');
  assert.equal(run('ECHO.state'), 'idle'); assert.equal(run('rec.on'), false);
  assert.equal(stopCalls, stopsBefore + 1); assert.equal(gain.lastRamp, 0);
  run("src.kind='mic'"); await run('ECHO.okDown()');
  assert.equal(run('rec.on'), true);
  run('rec.n=8000;ECHO.okUp()'); run('back()');
  assert.equal(run('ECHO.state'), 'idle'); assert.equal(run('rec.on'), false); assert.equal(stopCalls, stopsBefore + 2);
  console.log('PASS prototype manual hints, playback stop/fade, live voice selection/replay and leave cleanup');

  advance(500); run("go(ECHO);ECHO.m=1;A.db=-20"); draw();
  assert.equal(run('ECHO.state'), 'rec'); assert.doesNotMatch(draw(), /松开/);
  run('ECHO.up()'); assert.equal(run('ECHO.v'), 3); assert.equal(run('ECHO.state'), 'rec');
  run('ECHO.down()'); assert.equal(run('ECHO.m'), 0); assert.equal(run('rec.on'), false);
  assert.equal(run('ECHO.state'), 'idle');
  run('A.db=-100;ECHO.down()'); advance(500); run('A.db=-20'); draw();
  assert.equal(run('ECHO.state'), 'rec');
  run('rec.n=8000;A.db=-100'); advance(700); draw();
  assert.equal(run('ECHO.state'), 'play'); assert.match(draw(), /停止/);
  run('ECHO.up()'); assert.equal(run('ECHO.v'), 4); assert.equal(run('ECHO.state'), 'play');
  run('ECHO.down()'); assert.equal(run('ECHO.m'), 0); assert.equal(run('ECHO.state'), 'idle');
  assert.equal(run('rec.on'), false);
  run('A.db=-100;ECHO.down()'); advance(500); run('A.db=-20'); draw();
  run('rec.n=8000;A.db=-100'); advance(700); draw();
  assert.equal(run('ECHO.state'), 'play');
  await run('ECHO.okDown()'); assert.equal(run('ECHO.state'), 'idle');
  run('A.db=-20'); advance(300); draw(); assert.equal(run('ECHO.state'), 'idle');
  advance(200); draw(); assert.equal(run('ECHO.state'), 'rec');
  run('back()'); assert.equal(run('rec.on'), false);
  console.log('PASS prototype live selectors and mode escape during auto recording/playback, stop cooldown and teardown');
}
main().catch(error => {console.error(error); process.exitCode = 1;});

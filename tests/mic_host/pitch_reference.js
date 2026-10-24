// Evaluate the actual prototype YIN function without a browser or microphone.
const fs = require('fs');
const vm = require('vm');
const path = require('path');
const html = fs.readFileSync(path.join(__dirname, '../../prototype/index.html'), 'utf8');
const start = html.indexOf('function yin(x){');
const end = html.indexOf('function estimateBpm()', start);
if (start < 0 || end <= start) throw new Error('Prototype YIN function not found');
const context = vm.createContext({
  SR: 16000,
  D: {yd: new Float32Array(512)},
  clamp: (x, lo, hi) => Math.max(lo, Math.min(hi, x)),
});
vm.runInContext(html.slice(start, end), context);
const signals = JSON.parse(fs.readFileSync(0, 'utf8'));
const results = signals.map(samples => {
  const frames = [];
  // Match the firmware's even hops and zero-padded 1024-sample history.
  for (let end = 640; end <= samples.length; end += 640) {
    let power = 0;
    for (let i = end - 320; i < end; i++) power += (samples[i] / 32767) ** 2;
    if (10 * Math.log10(power / 320 + 1e-30) <= -52) {
      frames.push([0, 0]);
      continue;
    }
    const window = new Float32Array(1024);
    for (let i = 0; i < window.length; i++) {
      const offset = end - window.length + i;
      window[i] = offset < 0 ? 0 : samples[offset] / 32767;
    }
    const result = context.yin(window);
    frames.push([result.f, result.c]);
  }
  return frames;
});
process.stdout.write(JSON.stringify(results));

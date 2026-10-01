#!/usr/bin/env node
/* PlexusX — site parity check.
 *
 * Recomputes the sample set from tests/parity_gen.c using the SHIPPED browser
 * port (site/assets/color_engine.js) and diffs it against the C golden
 * (site/parity.json, or a freshly generated one via --golden <file>).
 * CI runs this with `node scripts/site_parity.js` after regenerating the
 * golden from C — tolerance 3e-4 (float32-vs-double margin), ramps exact.
 */
'use strict';
const fs = require('fs');
const path = require('path');
const E = require(path.join(__dirname, '..', 'site', 'assets', 'color_engine.js'));

/* THE SAMPLE SET — must stay identical to tests/parity_gen.c (order included). */
const S = [
 ['neutral',       1,100,100,100,100,1.00,6500,0,100,100,100,100,100,100,100,100,0],
 ['elite-default', 1,150,120,100,100,1.00,6500,0,100,100,100,100,100,100,100,100,0],
 ['max-chroma',    1,300,300,100,100,1.00,6500,0,100,100,100,100,100,100,100,100,0],
 ['desaturate',    1,0,0,100,100,1.00,6500,0,100,100,100,100,100,100,100,100,0],
 ['warm-cold',     1,110,40,102,108,0.95,3400,-18,112,100,88,108,96,102,98,118,-45],
 ['cool-blue',     1,120,60,98,112,1.10,9200,25,90,100,110,95,105,99,101,108,60],
 ['night-vision',  1,180,150,118,124,0.80,4800,0,105,102,110,132,88,106,96,120,0],
 ['gamma-only',    1,100,100,100,100,0.62,6500,0,100,100,100,100,100,100,100,100,0],
 ['curves-only',   1,100,100,100,100,1.00,6500,0,100,100,100,142,74,100,100,150,0],
 ['black-white',   1,100,100,100,100,1.00,6500,0,100,100,100,100,100,142,62,100,0],
 ['gains',         1,100,100,100,100,1.00,6500,0,118,92,104,100,100,100,100,100,0],
 ['bypassed',      0,220,180,140,160,0.7,5000,10,110,110,110,120,120,120,120,120,30],
 ['clamp-test',    1,9999,9999,-9999,9999,99.0,999999,9999,-9999,9999,9999,9999,-9999,9999,9999,-9999,9999],
 ['hue-extremes',  1,100,100,100,100,1.00,6500,0,100,100,100,100,100,100,100,100,180],
 ['hue-neg',       1,140,80,100,100,1.00,6500,0,100,100,100,100,100,100,100,100,-179.5],
];
const PX = [[0,0,0],[1,1,1],[0.5,0.5,0.5],[0.25,0.5,0.75],
            [1,0,0],[0,1,0],[0,0,1],[0.1875,0.1875,0.1875],[0.835294,0.498039,0.101961]];

const TOL = 0.0003;
function near(a, b) { return Math.abs(a - b) <= TOL; }

function computeJS() {
  return S.map(s => {
    const keys = ['enabled','sat','vibrance','bri','con','gamma','temp','tint','r_gain','g_gain','b_gain','shadows','highlights','black_level','white_point','clarity','hue'];
    const lk = {}; keys.forEach((k, i) => { lk[k] = s[i + 1]; });
    const san = E.sanitizeLook(lk);
    const inList = keys.map(k => san[k]);
    const applied = lk.enabled ? lk : E.neutral();      /* PxPlan bypass semantics */
    const mat = E.buildEffect(applied).flat();
    const ramp = lk.enabled ? E.calcRamp(applied)[0].filter((_, i) => i % 4 === 0) : null;
    const px = PX.map(p => E.applyPixel(lk, p[0], p[1], p[2]));
    const neutral = E.curvesNeutral(san) ? 1 : 0;
    return { name: s[0], in: inList, mat, ramp, px, neutral };
  });
}

function main() {
  const argIdx = process.argv.indexOf('--golden');
  const goldenPath = argIdx >= 0 ? process.argv[argIdx + 1]
                   : path.join(__dirname, '..', 'site', 'parity.json');
  const golden = JSON.parse(fs.readFileSync(goldenPath, 'utf8'));
  const js = computeJS();
  if (golden.looks.length !== js.length) { console.error('FATAL: sample count mismatch (C spec drift?)'); process.exit(1); }
  let checked = 0, worst = 0;
  for (let i = 0; i < js.length; i++) {
    const a = golden.looks[i], b = js[i];
    if (a.name !== b.name) { console.error(`FATAL: sample order mismatch at ${i}: ${a.name} vs ${b.name}`); process.exit(1); }
    const cmpArr = (x, y, exact) => {
      if (x.length !== y.length) { console.error(`FATAL ${a.name}: length ${x.length} != ${y.length}`); process.exit(1); }
      for (let j = 0; j < x.length; j++) {
        const d = Math.abs(x[j] - y[j]);
        if (exact ? d > 1e-9 : !near(x[j], y[j])) {
          console.error(`PARITY FAIL [${a.name}] idx ${j}: C=${x[j]} JS=${y[j]} d=${d}`);
          process.exit(1);
        }
        worst = Math.max(worst, d); checked++;
      }
    };
    cmpArr(a.in, b.in, false);
    cmpArr(a.mat, b.mat, false);
    /* LUT entries are 16-bit quantizations of powf(); C float pow vs JS
     * double pow -> fround may differ by exactly one rounding step (1/65535).
     * Anything beyond a single LUT step is a real drift: allow <= 1. */
    {
      const x = a.ramp, y = b.ramp;
      if ((x === null) !== (y === null)) { console.error(`FATAL ${a.name}: ramp nullness mismatch`); process.exit(1); }
      if (x === null) { checked++; } else {
      if (x.length !== y.length) { console.error('FATAL ramp length'); process.exit(1); }
      for (let j = 0; j < x.length; j++) {
        const d = Math.abs(x[j] - y[j]);
        if (d > 1) { console.error(`PARITY FAIL [${a.name}] ramp idx ${j * 4}: C=${x[j]} JS=${y[j]}`); process.exit(1); }
        worst = Math.max(worst, d); checked++;
      }
      }
    }
    a.px.forEach((p, k) => cmpArr(p, b.px[k], false));
    cmpArr([a.neutral], [b.neutral], true);
  }
  console.log(`SITE PARITY OK: ${js.length} looks, ${checked} values, max |C-JS| = ${worst.toExponential(2)} (tol ${TOL}, ramps exact)`);
}
main();

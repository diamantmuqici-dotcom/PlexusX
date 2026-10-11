#!/usr/bin/env node
/* PlexusX — website DOM smoke test (jsdom).
 *
 * Loads site/index.html and site/preview.html, executes the shipped scripts in
 * order and fails on ANY uncaught page error.  This is the regression guard for
 * the failure mode that once shipped: app.js referenced elements that no
 * longer existed (#stCode/#stCopy/#sha), threw a TypeError during load and
 * silently killed everything after it — reveal animations (most of the page
 * stayed at opacity:0), the app-mock navigation, scene chips and the profile
 * recommender.
 *
 * Requires jsdom:  npm install --no-save jsdom
 * Run:             node scripts/site_smoke.js
 */
'use strict';
const fs = require('fs');
const path = require('path');
const { JSDOM, VirtualConsole } = require('jsdom');

const ROOT = path.join(__dirname, '..');
const SITE = path.join(ROOT, 'site');

let failures = 0;
function ok(cond, msg) {
  if (cond) { console.log(`  [ok] ${msg}`); }
  else { console.error(`  [FAIL] ${msg}`); failures++; }
}

function asset(name) { return fs.readFileSync(path.join(SITE, 'assets', name), 'utf8'); }

function buildDom(htmlFile, scripts) {
  const errors = [];
  const vc = new VirtualConsole();
  vc.on('jsdomError', e => errors.push(e));
  vc.on('error', (...a) => errors.push(new Error(a.join(' '))));
  const dom = new JSDOM(fs.readFileSync(path.join(SITE, htmlFile), 'utf8'), {
    runScripts: 'dangerously',
    pretendToBeVisual: true,          /* provides requestAnimationFrame */
    url: 'https://plexusx.local/' + htmlFile,
    virtualConsole: vc,
  });
  const w = dom.window;

  /* IntersectionObserver is not implemented by jsdom: provide a stub that
   * reports every observed element as intersecting, exactly like a browser
   * would for a page scrolled to the bottom. */
  const observed = [];
  w.IntersectionObserver = class {
    constructor(cb) { this.cb = cb; }
    observe(el) {
      observed.push(el);
      this.cb([{ isIntersecting: true, target: el }], this);
    }
    unobserve() {}
    disconnect() {}
  };

  /* clipboard: record copies instead of touching the (absent) system clipboard */
  const copied = [];
  Object.defineProperty(w.navigator, 'clipboard', {
    value: { writeText: t => { copied.push(t); return Promise.resolve(); } },
    configurable: true,
  });
  w.document.execCommand = () => true;

  for (const s of scripts) {
    try { w.eval(asset(s)); }
    catch (e) { errors.push(new Error(`uncaught while executing ${s}: ${e.message}`)); }
  }
  return { dom, w, errors, observed, copied };
}

function click(w, el) {
  el.dispatchEvent(new w.MouseEvent('click', { bubbles: true, cancelable: true }));
}

/* ---------------- index.html ---------------- */
console.log('== site/index.html ==');
{
  const { w, errors, copied } = buildDom('index.html',
    ['color_engine.js', 'presets.js', 'app.js']);
  const d = w.document;
  const realErrors = errors.filter(e => !/getContext|not implemented/i.test(String(e.message || e)));
  ok(realErrors.length === 0,
     `no uncaught page errors${realErrors.length ? ': ' + realErrors.map(e => e.message).join(' | ') : ''}`);

  /* the fatal-load regression: script must have run all the way through */
  const reveals = [...d.querySelectorAll('.reveal')];
  ok(reveals.length > 20, `reveal sections present (${reveals.length})`);
  ok(reveals.every(el => el.classList.contains('in')),
     'every .reveal section was animated in (IntersectionObserver registered — script survived to the end)');

  /* game gallery */
  const cards = [...d.querySelectorAll('#gameGrid .game-card')];
  ok(cards.length === 6, `game gallery rendered 6 cards (got ${cards.length})`);
  ok(d.querySelector('#stSatVal').textContent === '180%',
     `stage saturation mirrors the native Rust Competitive look (got ${d.querySelector('#stSatVal').textContent})`);
  click(w, cards[4]);   /* Tarkov */
  ok(d.querySelector('#stageName').textContent === 'TARKOV', 'clicking a game card selects it');
  ok(d.querySelector('#stGamVal').textContent === '0.76',
     `Tarkov Dark Room gamma shown as 0.76 (got ${d.querySelector('#stGamVal').textContent})`);

  /* comparator branding */
  ok(/^PLEXUSX/.test(d.querySelector('#cmpTag').textContent),
     `comparator tag says PLEXUSX (got "${d.querySelector('#cmpTag').textContent}")`);

  /* app mock navigation */
  const sideItems = [...d.querySelectorAll('.mock-side-item')];
  const gamesTabBtn = sideItems.find(b => b.dataset.tab === 'games');
  click(w, gamesTabBtn);
  ok(d.querySelector('.mock-tab[data-tab="games"]').classList.contains('active'),
     'mock sidebar switches tabs');
  ok(!d.querySelector('.mock-tab[data-tab="display"]').classList.contains('active'),
     'previous mock tab is deactivated');

  /* scene chips drive the mock saturation slider */
  const chip = [...d.querySelectorAll('#mockScenes .chip')].find(c => c.textContent === 'Vivid 300%');
  ok(!!chip, 'mock scene library lists "Vivid 300%" (name matches the native scene)');
  if (chip) {
    click(w, chip);
    ok(d.querySelector('#mockSat').value === '300', 'Vivid 300% chip drives the mock slider to 300');
  }

  /* recommender outputs real built-in values */
  d.querySelector('#recPriority').value = 'competitive';
  click(w, d.querySelector('#recBtn'));
  ok(d.querySelector('#recResult').style.display === 'block', 'recommender result becomes visible');
  ok(d.querySelector('#recSat').textContent === '180%',
     `recommender competitive saturation is the shipped 180% (got ${d.querySelector('#recSat').textContent})`);

  /* checksum card: real ids, copy buttons, hashes match SHA256SUMS.txt */
  const sums = fs.readFileSync(path.join(SITE, 'download', 'SHA256SUMS.txt'), 'utf8');
  const sumOf = f => (sums.match(new RegExp(`([0-9a-f]{64})\\s+${f}`)) || [])[1];
  const shaP = d.querySelector('#shaPortable'), shaS = d.querySelector('#shaSetup');
  ok(!!shaP && /^[0-9a-f]{64}$/.test(shaP.textContent.trim()), 'portable SHA-256 element present and well-formed');
  ok(shaP && shaP.textContent.trim() === sumOf('PlexusX\\.exe'),
     'page shows the portable hash from download/SHA256SUMS.txt');
  ok(shaS && shaS.textContent.trim() === sumOf('PlexusX-Setup\\.exe'),
     'page shows the setup hash from download/SHA256SUMS.txt');
  click(w, d.querySelector('#copyShaPortable'));
  ok(copied.length === 1 && copied[0] === shaP.textContent.trim(), 'copy button copies the portable hash');

  /* toast is announced to assistive tech */
  const toast = d.querySelector('.toast');
  ok(!!toast && toast.getAttribute('aria-live') === 'polite', 'toast has aria-live="polite"');
}

/* ---------------- preview.html ---------------- */
console.log('== site/preview.html ==');
{
  const { w, errors } = buildDom('preview.html',
    ['color_engine.js', 'presets.js', 'preview.js']);
  const d = w.document;
  /* jsdom has no 2D canvas: the page must degrade gracefully via the guard,
   * not throw.  Any other error is a failure. */
  const realErrors = errors.filter(e => !/getContext|not implemented/i.test(String(e.message || e)));
  ok(realErrors.length === 0,
     `preview degrades without uncaught errors${realErrors.length ? ': ' + realErrors.map(e => e.message).join(' | ') : ''}`);
  ok(/CANVAS UNAVAILABLE/.test(d.querySelector('#chipMode').textContent),
     'preview reports CANVAS UNAVAILABLE when 2D context is missing (graceful, honest)');
}

if (failures) { console.error(`\nSITE SMOKE FAILED: ${failures} failure(s)`); process.exit(1); }
console.log('\nSITE SMOKE OK: index.html + preview.html execute cleanly; wiring, presets, hash card and recommender verified');

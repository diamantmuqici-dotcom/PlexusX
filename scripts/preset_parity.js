#!/usr/bin/env node
/* PlexusX — site preset parity check.
 *
 * Proves that the built-in presets the WEBSITE shows (site/assets/presets.js,
 * used by preview.html and referenced by the marketing pages) are exactly the
 * presets the NATIVE app seeds (app/src/presets/preset_store.h).  The golden
 * JSON is produced by tests/preset_gen.c from the real C seed — no hand-copied
 * values on either side.
 *
 * CI:
 *   gcc -std=gnu11 -Wall -Wextra -Werror -O2 -Iapp/src \
 *       tests/preset_gen.c app/src/games/game_profile.c -o /tmp/preset_gen -lm
 *   /tmp/preset_gen > /tmp/preset_seed.json
 *   node scripts/preset_parity.js --golden /tmp/preset_seed.json
 *
 * Every Global-category preset of the native seed must appear on the site with
 * identical Look values (tolerance 1e-3 — both sides are decimal literals) and
 * the site must not advertise presets the app does not have.
 */
'use strict';
const fs = require('fs');
const path = require('path');
const site = require(path.join(__dirname, '..', 'site', 'assets', 'presets.js'));

const TOL = 1e-3;
const LOOK_KEYS = ['enabled', 'sat', 'vibrance', 'bri', 'con', 'gamma', 'temp', 'tint',
                   'r_gain', 'g_gain', 'b_gain', 'shadows', 'highlights',
                   'black_level', 'white_point', 'clarity', 'hue'];
/* JSON export field names (PxPre_ToJson) -> Look field names */
const JSON_KEYS = { enabled: 'enabled', sat: 'sat', vibrance: 'vibrance', brightness: 'bri',
                    contrast: 'con', gamma: 'gamma', temperature: 'temp', tint: 'tint',
                    r: 'r_gain', g: 'g_gain', b: 'b_gain', shadows: 'shadows',
                    highlights: 'highlights', blackLevel: 'black_level',
                    whitePoint: 'white_point', clarity: 'clarity', hue: 'hue' };

function fail(msg) { console.error('PRESET PARITY FAIL: ' + msg); process.exit(1); }

function main() {
  const argIdx = process.argv.indexOf('--golden');
  const goldenPath = argIdx >= 0 ? process.argv[argIdx + 1]
                   : path.join(__dirname, '..', 'site', 'parity_presets.json');
  if (!fs.existsSync(goldenPath))
    fail(`golden not found (${goldenPath}) — generate it with tests/preset_gen.c`);
  const golden = JSON.parse(fs.readFileSync(goldenPath, 'utf8'));
  if (!golden.presets || !Array.isArray(golden.presets)) fail('golden has no presets array');

  /* native Global presets are the ones the website mirrors */
  const native = golden.presets.filter(p => p.category === 0);
  if (native.length < 8) fail(`expected >= 8 native Global presets, got ${native.length}`);

  const siteByName = new Map(site.presets.map(p => [p.name.toLowerCase(), p]));
  let checked = 0;

  for (const np of native) {
    const sp = siteByName.get(np.name.toLowerCase());
    if (!sp) fail(`native Global preset "${np.name}" is missing from site/assets/presets.js`);
    for (const [jk, lk] of Object.entries(JSON_KEYS)) {
      const a = np.look[jk], b = sp.look[lk];
      if (typeof a !== 'number' || typeof b !== 'number')
        fail(`"${np.name}".${jk}: non-numeric (native=${a}, site=${b})`);
      if (Math.abs(a - b) > TOL)
        fail(`"${np.name}".${jk}: native=${a} site=${b} (tol ${TOL})`);
      checked++;
    }
    siteByName.delete(np.name.toLowerCase());
  }
  /* the site must not advertise anything the app does not ship */
  for (const extra of siteByName.values())
    fail(`site advertises preset "${extra.name}" which the native seed does not contain`);

  console.log(`PRESET PARITY OK: ${native.length} built-in presets, ${checked} values identical between site and native seed (tol ${TOL})`);
}
main();

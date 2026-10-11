/* PlexusX — built-in preset definitions for the website.
 *
 * ONE SOURCE OF TRUTH RULE: every entry below mirrors, field for field, the
 * native built-in seed in app/src/presets/preset_store.h (PxPre_SeedBuiltins),
 * which in turn matches the same-named entries of the scene library in
 * app/src/games/game_preset_manager.c.  CI enforces this: tests/preset_gen.c
 * prints the native seed as JSON and scripts/preset_parity.js fails the build
 * when this file drifts from it.
 *
 * Look field order (identical to app/src/color/look.h):
 *   enabled, sat, vibrance, bri, con, gamma, temp, tint,
 *   r_gain, g_gain, b_gain, shadows, highlights, black_level, white_point,
 *   clarity, hue
 *
 * Units: sat/vibrance/bri/con/gains/shadows/highlights/black/white/clarity are
 * percent (100 = neutral), gamma is a 0.40–2.50 power curve exponent
 * (1.00 = neutral), temp is Kelvin (6500 = D65 neutral), tint and hue are
 * signed (-100..100 green→magenta, -180..180 degrees).
 */
(function (root) {
  'use strict';

  function look(enabled, sat, vibrance, bri, con, gamma, temp, tint,
                r, g, b, shadows, highlights, black, white, clarity, hue) {
    return { enabled: enabled, sat: sat, vibrance: vibrance, bri: bri, con: con,
             gamma: gamma, temp: temp, tint: tint,
             r_gain: r, g_gain: g, b_gain: b,
             shadows: shadows, highlights: highlights,
             black_level: black, white_point: white, clarity: clarity, hue: hue };
  }

  /* Short, honest descriptions — what the preset actually does, not marketing. */
  var PRESETS = [
    { name: "Neutral",        category: "Global", builtin: 1,
      desc: "Every control at its no-op value. The baseline all other presets are measured against.",
      look: look(1, 100, 100, 100, 100, 1.00, 6500, 0, 100, 100, 100, 100, 100, 100, 100, 100, 0) },
    { name: "Competitive",    category: "Global", builtin: 1,
      desc: "Moderate chroma boost with lifted shadows and a touch of clarity for player visibility.",
      look: look(1, 180, 150, 106, 114, 0.95, 6500, 0, 100, 100, 100, 120, 95, 100, 100, 115, 0) },
    { name: "Vivid 300%",     category: "Global", builtin: 1,
      desc: "The explicit maximum-chroma showcase: 300% saturation with a bounded 240% vibrance. Strong by design — not a natural look.",
      look: look(1, 300, 240, 105, 112, 1.00, 6500, 0, 100, 100, 100, 110, 100, 100, 100, 115, 0) },
    { name: "Night Ops",      category: "Global", builtin: 1,
      desc: "Brightened shadows, steep 0.78 gamma and warm 6200K for dark scenes; highlights pulled back to protect UI glare.",
      look: look(1, 180, 150, 122, 110, 0.78, 6200, 0, 102, 100, 98, 155, 85, 108, 100, 120, 0) },
    { name: "Cinematic Warm", category: "Global", builtin: 1,
      desc: "Lower saturation, warm 5800K, soft contrast curve — a film-like grade, not a visibility boost.",
      look: look(1, 135, 120, 102, 110, 1.04, 5800, 4, 105, 100, 94, 95, 105, 100, 100, 105, 0) },
    { name: "Cool Clarity",   category: "Global", builtin: 1,
      desc: "Slightly cool 7500K with lifted blue gain and clarity for hazy or washed-out scenes.",
      look: look(1, 140, 130, 104, 108, 0.98, 7500, -2, 96, 100, 108, 110, 95, 100, 100, 112, 0) },
    { name: "High Contrast",  category: "Global", builtin: 1,
      desc: "125% contrast plus a clarity S-curve; shadows compress, highlights separate. Can clip very dark scenes.",
      look: look(1, 165, 140, 105, 125, 0.95, 6500, 0, 100, 100, 100, 115, 90, 100, 100, 125, 0) },
    { name: "OLED Punch",     category: "Global", builtin: 1,
      desc: "Mild chroma and clarity boost with the tone curve left untouched — suited to OLED's already-deep blacks.",
      look: look(1, 175, 150, 100, 112, 1.00, 6500, 0, 100, 100, 100, 100, 100, 100, 100, 110, 0) }
  ];

  var api = {
    presets: PRESETS,
    byName: function (name) {
      for (var i = 0; i < PRESETS.length; i++)
        if (PRESETS[i].name.toLowerCase() === String(name).toLowerCase()) return PRESETS[i];
      return null;
    }
  };
  root.PxSitePresets = api;
  if (typeof module !== 'undefined' && module.exports) module.exports = api;
})(typeof window !== 'undefined' ? window : globalThis);

/* PlexusX — ColorTransform (web port).
 *
 * EXACT port of the native kernel app/src/color/color_math.h — same order of
 * operations, same constants, float32 rounding via Math.fround so the browser
 * preview and the shipped Windows engine produce the same numbers (CI checks
 * this parity against the C reference; see tests/parity + scripts/site_parity.js).
 *
 * This is a PREVIEW ONLY: it simulates what the engine's math would do to
 * pixels. It cannot and does not touch your GPU LUTs, DWM color effects, or any
 * display. The native app applies these same values through
 * MagSetFullscreenColorEffect (linear half) and SetDeviceGammaRamp (tone half).
 */
(function (root) {
  'use strict';
  var F = Math.fround;
  var CM_PI = F(3.14159265358979323846);
  var LUM_R = F(0.2126729), LUM_G = F(0.7151522), LUM_B = F(0.0721750);
  var WEIGHT_LIMIT = F(4.0);

  function clampf(v, lo, hi) { v = F(v); if (!(v >= lo)) return lo; if (v > hi) return hi; return v; }
  function finiteClamp(v, lo, hi, fallback) {
    v = F(v);
    if (!isFinite(v)) return F(fallback);
    return clampf(v, lo, hi);
  }

  /* Look order (identical to color/look.h):
   * enabled, sat, vibrance, bri, con, gamma, temp, tint,
   * r_gain, g_gain, b_gain, shadows, highlights, black_level, white_point, clarity, hue */
  function neutral() {
    return { enabled: 1, sat: F(100), vibrance: F(100), bri: F(100), con: F(100),
             gamma: F(1.00), temp: F(6500), tint: F(0),
             r_gain: F(100), g_gain: F(100), b_gain: F(100),
             shadows: F(100), highlights: F(100), black_level: F(100), white_point: F(100),
             clarity: F(100), hue: F(0) };
  }

  function sanitizeLook(lk) {
    var o = neutral();
    o.enabled = lk.enabled ? 1 : 0;
    o.sat         = finiteClamp(lk.sat,         0,     300,   100);
    o.vibrance    = finiteClamp(lk.vibrance,    0,     300,   100);
    o.bri         = finiteClamp(lk.bri,         0,     200,   100);
    o.con         = finiteClamp(lk.con,         0,     200,   100);
    o.gamma       = finiteClamp(lk.gamma,       0.40,  2.50,  1.0);
    o.temp        = finiteClamp(lk.temp,        1000,  40000, 6500);
    o.tint        = finiteClamp(lk.tint,        -100,  100,   0);
    o.r_gain      = finiteClamp(lk.r_gain,      0,     200,   100);
    o.g_gain      = finiteClamp(lk.g_gain,      0,     200,   100);
    o.b_gain      = finiteClamp(lk.b_gain,      0,     200,   100);
    o.shadows     = finiteClamp(lk.shadows,     0,     200,   100);
    o.highlights  = finiteClamp(lk.highlights,  0,     200,   100);
    o.black_level = finiteClamp(lk.black_level, 0,     200,   100);
    o.white_point = finiteClamp(lk.white_point, 0,     200,   100);
    o.clarity     = finiteClamp(lk.clarity,     0,     200,   100);
    o.hue         = finiteClamp(lk.hue,         -180,  180,   0);
    return o;
  }

  /* kelvin -> normalized rgb (same constants as cm_kelvin_to_rgb) */
  function kelvinToRgb(k) {
    k = F(k);
    if (!isFinite(k)) k = F(6500);
    k = clampf(k, 1000, 40000);
    var temp = F(k / 100.0);
    var red, green, blue;
    if (temp <= 66.0) {
      red = 255.0;
      green = F(F(99.4708025861) * F(Math.log(temp)) - 161.1195681661);
      if (temp <= 19.0) {
        blue = 0.0;
      } else {
        blue = F(F(138.5177312231) * F(Math.log(temp - 10.0)) - 305.0447927307);
      }
    } else {
      red   = F(F(329.698727446)  * F(Math.pow(temp - 60.0, -0.1332047592)));
      green = F(F(288.1221695283) * F(Math.pow(temp - 60.0, -0.0755148492)));
      blue  = 255.0;
    }
    return { r: clampf(F(red / 255.0), 0, 2), g: clampf(F(green / 255.0), 0, 2), b: clampf(F(blue / 255.0), 0, 2) };
  }

  function identity() {
    var m = []; for (var i = 0; i < 5; i++) { var row = []; for (var j = 0; j < 5; j++) row.push(i === j ? 1 : 0); m.push(row); }
    return m;
  }

  function mul(a, b) {
    var t = [];
    for (var r = 0; r < 5; r++) {
      var row = [];
      for (var c = 0; c < 5; c++) {
        var s = 0;
        for (var k = 0; k < 5; k++) s = F(s + F(a[r][k] * b[k][c]));
        row.push(s);
      }
      t.push(row);
    }
    return t;
  }

  function chromaEff(satPct, vibPct) {
    var s = F(satPct / 100.0);
    var extra = F(F(0.5) * F(F(vibPct / 100.0) - 1.0));
    var eff = F(s * F(1.0 + F(F(0.40) * extra)));
    var maxEff = F(F(WEIGHT_LIMIT - LUM_B) / F(1.0 - LUM_B));
    var minEff = F(1.0 - F(WEIGHT_LIMIT / LUM_G));
    if (eff > maxEff) eff = maxEff;
    if (eff < minEff) eff = minEff;
    if (Math.abs(eff - 1.0) < 1e-6) eff = 1.0;
    return eff;
  }

  function saturation(satPct, vibPct) {
    var w = [LUM_R, LUM_G, LUM_B];
    var eff = chromaEff(satPct, vibPct);
    var o = identity();
    for (var i = 0; i < 3; i++)
      for (var j = 0; j < 3; j++)
        o[i][j] = F(F(i === j ? eff : 0.0) + F(w[i] * F(1.0 - eff)));
    return o;
  }

  function hue(degrees) {
    var lr = LUM_R, lg = LUM_G, lb = LUM_B;
    var rad = F(F(degrees) * F(F(CM_PI) / 180.0));
    var c = F(Math.cos(rad)), s = F(Math.sin(rad));
    var k10 = F(F(F(lr * lr) + F(lb * F(1.0 - lr))) / lg);
    var k11 = F(lr - lb);
    var k12 = F(F(-F(F(lr * F(1.0 - lb)) + F(lb * lb))) / lg);
    var h = [
      [ F(lr + F(c * F(1.0 - lr)) - F(s * lr)),          F(lg - F(c * lg) - F(s * lg)),   F(lb - F(c * lb) + F(s * F(1.0 - lb))) ],
      [ F(lr - F(c * lr) + F(s * k10)),                  F(lg + F(c * F(1.0 - lg)) + F(s * k11)), F(lb - F(c * lb) + F(s * k12)) ],
      [ F(lr - F(c * lr) - F(s * F(1.0 - lr))),          F(lg - F(c * lg) + F(s * lg)),   F(lb + F(c * F(1.0 - lb)) + F(s * lb)) ]
    ];
    var o = identity();
    for (var i = 0; i < 3; i++)
      for (var j = 0; j < 3; j++)
        o[i][j] = h[j][i];             /* transpose for row vectors */
    return o;
  }

  function tempTintGain(tempK, tintPct, rg, gg, bg) {
    var k = kelvinToRgb(tempK);
    var n = kelvinToRgb(6500.0);
    var kr = F(k.r / n.r), kg = F(k.g / n.g), kb = F(k.b / n.b);
    var tn = F(tintPct / 100.0);
    var tg = F(1.0 - F(tn * 0.20));
    var tr = F(1.0 + F(tn * 0.15));
    var tb = F(1.0 + F(tn * 0.15));
    var o = identity();
    o[0][0] = F(F(kr * tr) * rg);
    o[1][1] = F(F(kg * tg) * gg);
    o[2][2] = F(F(kb * tb) * bg);
    return o;
  }

  function briCon(conPct, briPct, blPct, wpPct) {
    var c = F(conPct / 100.0);
    var b = F(briPct / 100.0);
    var wp = F(wpPct / 100.0);
    var bl = F(F(F(blPct) - 100.0) / 200.0);
    var slope = F(F(c * b) * wp);
    var offset = F(F(F(0.5) * F(1.0 - c)) + F(F(b - 1.0) * 0.5) + bl);
    var o = identity();
    for (var i = 0; i < 3; i++) { o[i][i] = slope; o[4][i] = offset; }
    return o;
  }

  /* last gate before "the API": identical fit-toward-identity + contract rows */
  function sanitize(m) {
    var r, c;
    for (r = 0; r < 5; r++)
      for (c = 0; c < 5; c++)
        if (!isFinite(m[r][c])) return { m: identity(), flags: 1 };
    var flags = 0;
    var lim = F(WEIGHT_LIMIT * 0.9975);
    var k = 1.0;
    for (r = 0; r < 3; r++)
      for (c = 0; c < 3; c++) {
        var id = r === c ? 1.0 : 0.0;
        var d = F(m[r][c] - id);
        if (d === 0.0) continue;
        var hi = d > 0.0 ? F((lim - id) / d) : F((-lim - id) / d);
        if (hi < k) k = hi;
      }
    if (k < 1.0) {
      if (k < 0.0) k = 0.0;
      for (r = 0; r < 3; r++)
        for (c = 0; c < 3; c++) {
          var id2 = r === c ? 1.0 : 0.0;
          m[r][c] = F(id2 + F(k * F(m[r][c] - id2)));
        }
      flags |= 2;
    }
    for (r = 0; r < 3; r++)
      for (c = 0; c < 3; c++) {
        var v = m[r][c];
        if (v < -WEIGHT_LIMIT || v > WEIGHT_LIMIT) { m[r][c] = clampf(v, -WEIGHT_LIMIT, WEIGHT_LIMIT); flags |= 2; }
      }
    for (r = 0; r < 5; r++)
      for (c = 0; c < 5; c++) {
        if (r < 3 && c < 3) continue;
        var v2 = m[r][c];
        if (v2 < -WEIGHT_LIMIT || v2 > WEIGHT_LIMIT) { m[r][c] = clampf(v2, -WEIGHT_LIMIT, WEIGHT_LIMIT); flags |= 2; }
      }
    for (r = 0; r < 4; r++) m[r][4] = 0.0;
    m[4][4] = 1.0;
    for (r = 0; r < 3; r++) m[r][3] = 0.0;
    for (c = 0; c < 3; c++) m[3][c] = 0.0;
    m[3][3] = 1.0;
    m[4][3] = 0.0;
    return { m: m, flags: flags };
  }

  /* mirrors cm_build_effect(): sanitize + hue -> sat -> temp -> bri/con.
   * Engine-level bypass (enabled=0 -> neutral look) is applied by the caller
   * exactly like PxPlan_Compute does — never silently inside this function. */
  function buildEffect(lk) {
    var l = sanitizeLook(lk);
    var m = identity();
    var applied = l;
    if (Math.abs(applied.hue) > 0.01) m = mul(m, hue(applied.hue));
    m = mul(m, saturation(applied.sat, applied.vibrance));
    m = mul(m, tempTintGain(applied.temp, applied.tint,
                            F(applied.r_gain / 100.0), F(applied.g_gain / 100.0), F(applied.b_gain / 100.0)));
    m = mul(m, briCon(applied.con, applied.bri, applied.black_level, applied.white_point));
    var res = sanitize(m);
    return res.m;
  }

  /* non-linear half: the 256-entry GPU ramp LUT (identical order incl.
   * monotonic enforcement); output values are 0..65535 like WORD ramps */
  function curvesNeutral(lk) {
    return Math.abs(lk.gamma - 1.0) <= 0.005 &&
           Math.abs(lk.shadows - 100.0) <= 0.5 &&
           Math.abs(lk.highlights - 100.0) <= 0.5 &&
           Math.abs(lk.clarity - 100.0) <= 0.5;
  }

  function calcRamp(lk) {
    var l = sanitizeLook(lk);
    var g = clampf(l.gamma, 0.40, 2.50);
    var shLift = F(F(l.shadows - 100.0) / 100.0);
    var hlLift = F(F(l.highlights - 100.0) / 100.0);
    var clar = F(F(l.clarity - 100.0) / 100.0);
    var ramp = [[], [], []];
    for (var i = 0; i < 256; i++) {
      var x = F(i / 255.0);
      var y = F(Math.pow(x, g));
      if (Math.abs(shLift) > 0.001) {
        var toe = F(F(F(1.0 - x) * F(1.0 - x)) * F(F(shLift) * 0.35));
        y = clampf(F(y + toe), 0, 1);
      }
      if (Math.abs(hlLift) > 0.001) {
        var shoulder = F(F(x * x) * F(F(hlLift) * 0.30));
        y = clampf(F(y + shoulder), 0, 1);
      }
      if (Math.abs(clar) > 0.001) {
        var sc = F(F(0.5) * F(1.0 - Math.cos(F(x * CM_PI))) - x);
        y = clampf(F(y + F(F(sc) * F(F(clar) * 0.25))), 0, 1);
      }
      var val = (y * 65535.0 + 0.5) | 0;
      if (val < 0) val = 0; if (val > 65535) val = 65535;
      ramp[0].push(val); ramp[1].push(val); ramp[2].push(val);
    }
    for (var ch = 0; ch < 3; ch++)
      for (var j = 1; j < 256; j++)
        if (ramp[ch][j] < ramp[ch][j - 1]) ramp[ch][j] = ramp[ch][j - 1];
    return ramp;
  }

  function rampSample(ch, x) {
    x = clampf(x, 0.0, 1.0);
    var f = F(x * 255.0);
    var i0 = f | 0; if (i0 < 0) i0 = 0; if (i0 > 255) i0 = 255;
    var i1 = i0 < 255 ? i0 + 1 : 255;
    var t = F(f - i0);
    var a = F(ch[i0] / 65535.0), b = F(ch[i1] / 65535.0);
    return F(a + F(t * F(b - a)));
  }

  function xformRgb(m, r, g, b) {
    var inp = [r, g, b, 1.0, 1.0];
    var o = [0, 0, 0];
    for (var c = 0; c < 3; c++)
      for (var i = 0; i < 5; i++)
        o[c] = F(o[c] + F(inp[i] * m[i][c]));
    return o;
  }

  /* pixel in: 0..1 float rgb; pixel out: 0..1 — matrix then ramp, like the OS */
  function applyPixel(lk, r, g, b) {
    var l = sanitizeLook(lk);
    if (!l.enabled) return [r, g, b];
    var m = buildEffect(l);
    var o = xformRgb(m, r, g, b);
    var ramp = calcRamp(l);
    return [rampSample(ramp[0], o[0]), rampSample(ramp[1], o[1]), rampSample(ramp[2], o[2])];
  }

  var api = {
    neutral: neutral,
    sanitizeLook: sanitizeLook,
    kelvinToRgb: kelvinToRgb,
    buildEffect: buildEffect,
    calcRamp: calcRamp,
    curvesNeutral: curvesNeutral,
    applyPixel: applyPixel,
    rampSample: rampSample,
    sampleRamp: function (ch, x) { return rampSample(ch, x); },
    identity: identity,
    LUM: { r: LUM_R, g: LUM_G, b: LUM_B }
  };
  root.PxColorEngine = api;
  if (typeof module !== 'undefined' && module.exports) module.exports = api;
})(typeof window !== 'undefined' ? window : globalThis);

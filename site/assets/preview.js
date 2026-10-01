/* PlexusX web preview — runs the SAME kernel (color_engine.js) as the native
 * Windows app: 5x5 linear matrix (what the app sends to DWM's
 * MagSetFullscreenColorEffect) followed by the 256-entry gamma-ramp LUT (what
 * it writes through SetDeviceGammaRamp).  Nothing here touches your actual
 * display; it is a faithful numeric preview, CI-parity-checked against C. */
"use strict";
(() => {
  const E = window.PxColorEngine;
  if (!E) { document.getElementById("chipMode").textContent = "ENGINE SCRIPT MISSING"; return; }

  const cv = document.getElementById("cv");
  const ctx = cv.getContext("2d", { willReadFrequently: true });
  const W = cv.width, H = cv.height;

  const SPECS = [
    ["sat",         "Saturation",   0, 300, 0.5,  "%"],
    ["vibrance",    "Vibrance",     0, 300, 0.5,  "%"],
    ["bri",         "Brightness",   0, 200, 0.5,  "%"],
    ["con",         "Contrast",     0, 200, 0.5,  "%"],
    ["gamma",       "Gamma",     0.4, 2.5, 0.01,  ""],
    ["temp",        "Temp",      1000, 40000, 50, "K"],
    ["tint",        "Tint",      -100, 100, 1,    ""],
    ["r_gain",      "R Gain",       0, 200, 0.5,  "%"],
    ["g_gain",      "G Gain",       0, 200, 0.5,  "%"],
    ["b_gain",      "B Gain",       0, 200, 0.5,  "%"],
    ["shadows",     "Shadows",      0, 200, 0.5,  "%"],
    ["highlights",  "Highlights",   0, 200, 0.5,  "%"],
    ["black_level", "Black Flr",    0, 200, 0.5,  "%"],
    ["white_point", "White Pt",     0, 200, 0.5,  "%"],
    ["clarity",     "Clarity",      0, 200, 0.5,  "%"],
    ["hue",         "Hue",       -180, 180, 1,    "°"],
  ];
  const PRESETS = {
    "Competitive": { sat: 235, vibrance: 120, con: 118, bri: 102, gamma: 1.0 },
    "Night Ops":   { sat: 265, vibrance: 150, bri: 128, gamma: 0.85, shadows: 132 },
    "Cinema":      { sat: 118, vibrance: 108, con: 106, gamma: 1.12, temp: 5200 },
    "Natural":     { sat: 100, vibrance: 100, bri: 100, con: 100, gamma: 1.0, temp: 6500 },
    "Max Chroma":  { sat: 300, vibrance: 300, con: 108 },
  };

  let look = E.neutral();
  Object.assign(look, { sat: 150, vibrance: 120 });   /* the app's default boost */
  let base = null;                                     /* ImageData of the source scene */
  let splitPct = 50;

  /* ---------- procedural game-ish scene (no network needed) ---------- */
  function drawScene() {
    const g = ctx.createLinearGradient(0, 0, 0, H);
    g.addColorStop(0, "#1b2a4a"); g.addColorStop(0.55, "#3a4d6b"); g.addColorStop(1, "#7a8fa6");
    ctx.fillStyle = g; ctx.fillRect(0, 0, W, H);
    const sun = ctx.createRadialGradient(W * 0.72, H * 0.3, 8, W * 0.72, H * 0.3, 190);
    sun.addColorStop(0, "rgba(255,236,190,0.95)"); sun.addColorStop(1, "rgba(255,236,190,0)");
    ctx.fillStyle = sun; ctx.fillRect(0, 0, W, H);
    ctx.fillStyle = "#243247";                                        /* far ridge */
    ctx.beginPath(); ctx.moveTo(0, H * 0.62);
    for (let x = 0; x <= W; x += 24) ctx.lineTo(x, H * 0.62 - 46 * Math.sin(x / 140) - 20 * Math.sin(x / 41));
    ctx.lineTo(W, H); ctx.lineTo(0, H); ctx.fill();
    ctx.fillStyle = "#141d2b";                                        /* near ridge */
    ctx.beginPath(); ctx.moveTo(0, H * 0.78);
    for (let x = 0; x <= W; x += 18) ctx.lineTo(x, H * 0.78 - 30 * Math.sin(x / 66 + 2));
    ctx.lineTo(W, H); ctx.lineTo(0, H); ctx.fill();
    for (let i = 0; i < 14; i++) {                                    /* tree silhouettes */
      const x = 30 + (i * 67) % (W - 60), h = 50 + (i * 37) % 90;
      ctx.fillStyle = "#0b111a";
      ctx.beginPath(); ctx.moveTo(x, H * 0.86); ctx.lineTo(x + 14, H * 0.86 - h); ctx.lineTo(x + 28, H * 0.86); ctx.fill();
    }
    ctx.fillStyle = "#0a0f17"; ctx.fillRect(0, H * 0.86, W, H * 0.14);
    ctx.fillStyle = "rgba(198,255,61,.9)"; ctx.fillRect(W / 2 - 1, H / 2 - 9, 2, 18);   /* crosshair */
    ctx.fillRect(W / 2 - 9, H / 2 - 1, 18, 2);
    base = ctx.getImageData(0, 0, W, H);
  }

  function setBaseFromImage(img) {
    const r = Math.min(W / img.naturalWidth, H / img.naturalHeight);
    const w = Math.round(img.naturalWidth * r), h = Math.round(img.naturalHeight * r);
    ctx.fillStyle = "#000"; ctx.fillRect(0, 0, W, H);
    ctx.drawImage(img, (W - w) / 2, (H - h) / 2, w, h);
    base = ctx.getImageData(0, 0, W, H);
  }

  /* ---------- render: identical pipeline to the native preview ---------- */
  let raf = 0;
  function schedule() { if (!raf) raf = requestAnimationFrame(() => { raf = 0; render(); }); }

  function render() {
    if (!base) return;
    const splitX = Math.round(W * splitPct / 100);
    const out = ctx.createImageData(W, H);
    const d = out.data, s = base.data;
    if (!look.enabled) {
      d.set(s); paint(out, splitX); return;
    }
    const m = E.buildEffect(look);           /* linear half  == DWM matrix   */
    const ramp = E.calcRamp(look);           /* non-linear half == GPU LUT  */
    for (let y = 0; y < H; y++) {
      const rowOff = y * W * 4;
      for (let x = 0; x < W; x++) {
        const i = rowOff + x * 4;
        if (x <= splitX) { d[i] = s[i]; d[i + 1] = s[i + 1]; d[i + 2] = s[i + 2]; d[i + 3] = 255; continue; }
        const r = s[i] / 255, g = s[i + 1] / 255, b = s[i + 2] / 255;
        const rr = m[0][0] * r + m[1][0] * g + m[2][0] * b + m[3][0] + m[4][0];
        const gg = m[0][1] * r + m[1][1] * g + m[2][1] * b + m[3][1] + m[4][1];
        const bb = m[0][2] * r + m[1][2] * g + m[2][2] * b + m[3][2] + m[4][2];
        d[i]     = E.rampSample(ramp[0], rr) * 255;
        d[i + 1] = E.rampSample(ramp[1], gg) * 255;
        d[i + 2] = E.rampSample(ramp[2], bb) * 255;
        d[i + 3] = 255;
      }
    }
    paint(out, splitX);
    facts(m, ramp);
  }
  function paint(imgData, splitX) {
    ctx.putImageData(imgData, 0, 0);
    ctx.fillStyle = "rgba(198,255,61,.85)";
    ctx.fillRect(splitX - 1, 0, 2, H);
  }

  function facts(m, ramp) {
    const san = E.sanitizeLook(look);
    const row0 = m[0][0] + m[0][1] + m[0][2];
    const el = document.getElementById("facts");
    el.textContent =
      "matrix row0 col-sum  " + row0.toFixed(5) + "   (gray stays gray when = 1)\n" +
      "LUT @128             " + ramp[0][128] + " / 65535\n" +
      "curves neutral       " + (E.curvesNeutral(san) ? "yes — no LUT write needed" : "no — LUT active") + "\n" +
      "sanitized look       " + SPECS.map(([k]) => k + "=" + (+san[k]).toFixed(2)).join(" ") +
      (JSON.stringify(san.sat) !== JSON.stringify(look.sat) ? "\n(values clamped by sanitize — same rule as the app)" : "");
  }

  /* ---------- controls ---------- */
  const slidersEl = document.getElementById("sliders");
  const inputs = {};
  slidersEl.insertAdjacentHTML("beforeend",
    `<div class="pv-slider"><label>enabled</label>
       <input type="range" min="0" max="1" step="1" value="1" id="in-enabled">
       <output id="out-enabled">on</output></div>`);
  document.getElementById("in-enabled").addEventListener("input", e => {
    look.enabled = +e.target.value;
    document.getElementById("out-enabled").textContent = look.enabled ? "on" : "off";
    schedule();
  });
  for (const [key, label, lo, hi, step, unit] of SPECS) {
    const row = document.createElement("div");
    row.className = "pv-slider";
    row.innerHTML = `<label>${label}</label>
      <input type="range" min="${lo}" max="${hi}" step="${step}" value="${look[key]}" id="in-${key}">
      <output id="out-${key}"></output>`;
    slidersEl.appendChild(row);
    const inp = row.querySelector("input"), out = row.querySelector("output");
    inputs[key] = { inp, out, unit };
    const upd = () => {
      look[key] = +inp.value;
      out.textContent = (key === "gamma" ? (+inp.value).toFixed(2) : Math.round(inp.value)) + unit;
      schedule();
    };
    inp.addEventListener("input", upd);
    upd();
  }
  function syncInputs() {
    document.getElementById("in-enabled").value = look.enabled;
    document.getElementById("out-enabled").textContent = look.enabled ? "on" : "off";
    for (const key in inputs) {
      inputs[key].inp.value = look[key];
      inputs[key].out.textContent = (key === "gamma" ? (+look[key]).toFixed(2) : Math.round(look[key])) + inputs[key].unit;
    }
  }

  const presetsEl = document.getElementById("presets");
  for (const name in PRESETS) {
    const b = document.createElement("button");
    b.type = "button"; b.className = "btn btn-sm btn-ghost"; b.textContent = name;
    b.addEventListener("click", () => {
      look = Object.assign(E.neutral(), PRESETS[name], { enabled: 1 });
      syncInputs(); schedule();
    });
    presetsEl.appendChild(b);
  }
  document.getElementById("resetBtn").addEventListener("click", () => {
    look = E.neutral(); Object.assign(look, { sat: 150, vibrance: 120 });
    syncInputs(); schedule();
  });
  document.getElementById("sceneBtn").addEventListener("click", () => { drawScene(); schedule(); });
  document.getElementById("file").addEventListener("change", e => {
    const f = e.target.files && e.target.files[0];
    if (!f) return;
    const img = new Image();
    img.onload = () => { setBaseFromImage(img); URL.revokeObjectURL(img.src); schedule(); };
    img.src = URL.createObjectURL(f);
  });

  /* split drag */
  const stage = document.getElementById("stage"), div = document.getElementById("div");
  let drag = false;
  const setSplit = cx => {
    const r = stage.getBoundingClientRect();
    splitPct = Math.max(0, Math.min(100, ((cx - r.left) / r.width) * 100));
    div.style.left = splitPct + "%";
    div.setAttribute("aria-valuenow", Math.round(splitPct));
    schedule();
  };
  stage.addEventListener("pointerdown", e => { drag = true; setSplit(e.clientX); });
  addEventListener("pointermove", e => { if (drag) setSplit(e.clientX); });
  addEventListener("pointerup", () => drag = false);

  drawScene();
  schedule();
})();

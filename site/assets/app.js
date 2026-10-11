/* ═══════════ PlexusX — app.js ═══════════ */
"use strict";

const $  = (s, r = document) => r.querySelector(s);
const $$ = (s, r = document) => [...r.querySelectorAll(s)];

/* ---------- range fill (webkit track gradient) ---------- */
function paintRange(el) {
  const min = +el.min || 0, max = +el.max || 100, v = +el.value;
  const pct = ((v - min) / (max - min)) * 100;
  el.style.setProperty("--fill", pct + "%");
}
function bindRange(el, fn) {
  const run = () => { paintRange(el); fn && fn(+el.value); };
  el.addEventListener("input", run);
  paintRange(el);
  return run;
}

/* ---------- shared filter maths ----------
 * PRIMARY path: the REAL engine kernel (assets/color_engine.js — an exact port
 * of app/src/color/color_math.h, CI-checked for parity against the C source)
 * rendering into a canvas overlay, i.e. the same matrix + gamma-ramp LUT the
 * Windows app pushes to DWM and the GPU.  CSS filters remain only as a
 * fallback when canvas or image pixel access is unavailable; the caption on
 * every preview states plainly that this is a browser preview, not a live
 * capture of your display.
 */
function cssFilter({ sat = 100, bri = 0, con = 0, hue = 0, gamma = 1 }) {
  // fallback ONLY (no PxColorEngine loaded): approximate preview
  const gBri = bri + (1 - gamma) * 60;
  const gCon = con + (gamma - 1) * 22;
  return `saturate(${sat}%) brightness(${1 + gBri / 130}) contrast(${1 + gCon / 130}) hue-rotate(${hue}deg)`;
}

const ENGINE = typeof PxColorEngine !== "undefined" ? PxColorEngine : null;

function lookFrom(partial) {
  const l = ENGINE ? ENGINE.neutral() : null;
  if (!l) return partial;
  Object.assign(l, partial || {});
  return l;
}

/* Attach an engine renderer to an <img>: draws the processed pixels into a
 * canvas overlaid on the image.  Returns render(lookFields) or null when the
 * image cannot be read (cross-origin), so callers can fall back. */
function attachEnginePreview(img) {
  if (!ENGINE || !img || !window.HTMLCanvasElement) return null;
  const wrap = img.parentElement;
  const cv = document.createElement("canvas");
  cv.className = "engine-canvas";
  cv.setAttribute("aria-hidden", "true");
  wrap.appendChild(cv);
  let srcData = null, pending = null, lastLook = null, raf = 0;

  function rebuild() {
    const w = Math.min(img.naturalWidth || 0, 720);
    if (!w) return false;
    const h = Math.round(w * (img.naturalHeight / img.naturalWidth));
    const sc = document.createElement("canvas");
    sc.width = w; sc.height = h;
    const sctx = sc.getContext("2d", { willReadFrequently: true });
    try {
      sctx.drawImage(img, 0, 0, w, h);
      srcData = sctx.getImageData(0, 0, w, h);
    } catch { srcData = null; return false; }     /* tainted canvas: no pixel access */
    cv.width = w; cv.height = h;
    return true;
  }

  function render(partial) {
    if (!srcData) return;
    const look = lookFrom(partial);
    if (partial && partial.enabled === 0) {          /* engine bypass = raw pixels */
      cv.getContext("2d").putImageData(srcData, 0, 0);
      return;
    }
    const out = new ImageData(new Uint8ClampedArray(srcData.data), srcData.width, srcData.height);
    const ramp = ENGINE.calcRamp(look);
    const m = ENGINE.buildEffect(look);
    const px = out.data;
    for (let i = 0; i < px.length; i += 4) {      /* per pixel: matrix, then LUT — exactly like the OS pipeline */
      const r = px[i] / 255, g = px[i + 1] / 255, b = px[i + 2] / 255;
      let rr = m[0][0] * r + m[1][0] * g + m[2][0] * b + m[3][0] * 1 + m[4][0];
      let gg = m[0][1] * r + m[1][1] * g + m[2][1] * b + m[3][1] * 1 + m[4][1];
      let bb = m[0][2] * r + m[1][2] * g + m[2][2] * b + m[3][2] * 1 + m[4][2];
      px[i]     = ENGINE.sampleRamp(ramp[0], rr) * 255;
      px[i + 1] = ENGINE.sampleRamp(ramp[1], gg) * 255;
      px[i + 2] = ENGINE.sampleRamp(ramp[2], bb) * 255;
    }
    cv.getContext("2d").putImageData(out, 0, 0);
  }

  function schedule(partial) {
    if (partial) lastLook = partial;
    pending = partial || lastLook;
    if (raf) return;
    raf = requestAnimationFrame(() => { raf = 0; const p = pending; pending = null; render(p); });
  }

  const boot = () => { if (!rebuild()) return; img.style.visibility = "hidden"; schedule(lastLook); };
  img.addEventListener("load", boot);          /* also fires on game switches (stage <img> src changes) */
  if (img.complete && img.naturalWidth) boot();
  return { render: schedule, ok: () => !!srcData };
}

function tint(warmEl, coolEl, temp) {
  if (!warmEl || !coolEl) return;
  const t = Math.max(-1, Math.min(1, temp / 100));
  warmEl.style.opacity = t > 0 ? t * 0.5 : 0;
  coolEl.style.opacity = t < 0 ? -t * 0.5 : 0;
}
const sgn = n => (n > 0 ? "+" : "") + n;

/* ---------- nav ---------- */
const nav = $("#nav");
addEventListener("scroll", () => nav.classList.toggle("stuck", scrollY > 30), { passive: true });

/* ---------- hero ---------- */
const heroSat = $("#heroSat"), heroImg = $("#heroImg");
const heroEng = attachEnginePreview(heroImg);
bindRange(heroSat, v => {
  $("#heroSatVal").textContent = v + "%";
  if (heroEng && heroEng.ok()) heroEng.render({ sat: v, vibrance: 100, con: 108 });
  else heroImg.style.filter = cssFilter({ sat: v, con: 8 });
  paintRange(heroSat);
});
heroSat.dispatchEvent(new Event("input"));

/* ---------- comparator ---------- */
(() => {
  const box = $("#compare"), after = $("#compareAfter"), handle = $("#compareHandle");
  const afterImg = $("#compareAfterImg");
  const sat = $("#cmpSat"), con = $("#cmpCon"), temp = $("#cmpTemp");
  let pos = 50;

  function syncSize() {
    const w = box.clientWidth;
    afterImg.style.width = w + "px";
    after.style.width = pos + "%";
    handle.style.left = pos + "%";
  }
  const cmpEng = attachEnginePreview(afterImg);
  function applyPreview() {
    const s = +sat.value, c = +con.value, t = +temp.value;
    $("#cmpSatVal").textContent = s + "%";
    $("#cmpConVal").textContent = sgn(c);
    $("#cmpTempVal").textContent = sgn(t);
    $("#cmpTag").textContent = "PLEXUSX · " + s + "%";
    if (cmpEng && cmpEng.ok()) cmpEng.render({ sat: s, con: 100 + c, temp: 6500 + t * 22 });
    else afterImg.style.filter = cssFilter({ sat: s, con: c });
    tint($("#cmpWarm"), $("#cmpCool"), t);
    [sat, con, temp].forEach(paintRange);
  }
  function setPos(clientX) {
    const r = box.getBoundingClientRect();
    pos = Math.max(2, Math.min(98, ((clientX - r.left) / r.width) * 100));
    syncSize();
    handle.setAttribute("aria-valuenow", Math.round(pos));
  }
  let drag = false;
  const down = e => { drag = true; setPos((e.touches ? e.touches[0] : e).clientX); e.preventDefault(); };
  const move = e => { if (drag) setPos((e.touches ? e.touches[0] : e).clientX); };
  const up   = () => drag = false;
  box.addEventListener("mousedown", down);
  box.addEventListener("touchstart", down, { passive: false });
  addEventListener("mousemove", move);
  addEventListener("touchmove", move, { passive: true });
  addEventListener("mouseup", up);
  addEventListener("touchend", up);
  handle.addEventListener("keydown", e => {
    if (e.key === "ArrowLeft")  { pos = Math.max(2, pos - 4); syncSize(); }
    if (e.key === "ArrowRight") { pos = Math.min(98, pos + 4); syncSize(); }
  });
  [sat, con, temp].forEach(el => bindRange(el, applyPreview));
  $("#cmpReset").addEventListener("click", () => {
    sat.value = 240; con.value = 16; temp.value = 0;
    applyPreview();
  });
  addEventListener("resize", syncSize);
  syncSize(); applyPreview();
})();

/* ---------- game gallery ----------
 * Values below mirror the app's real built-in sub-modes in
 * app/src/games/game_preset_manager.c (each game's "Competitive" look, except
 * Tarkov which ships "Dark Room" as its signature mode).  bri/con/temp are the
 * site's relative display of the native percent values (bri 106 → +6,
 * temp 6400K → −5 on the ±50 slider, which maps t → 6500 + t·22 K).
 * If the native table changes, update these too. */
const GAMES = [
  { id: "rust",      name: "Rust",      mode: "Competitive", sub: "survival · treelines",   img: "media/scene-rust.jpg",
    sat: 180, vibrance: 150, bri: 6,  con: 16, temp: -5, gamma: 94, shadows: 135, highlights: 95, clarity: 120 },
  { id: "cs2",       name: "CS2",       mode: "Competitive", sub: "tactical · dust",        img: "media/scene-cs2.jpg",
    sat: 180, vibrance: 150, bri: 5,  con: 18, temp: 0,  gamma: 96, shadows: 125, highlights: 95, clarity: 118 },
  { id: "valorant",  name: "Valorant",  mode: "Competitive", sub: "hero shooter · clean",   img: "media/scene-valorant.jpg",
    sat: 180, vibrance: 150, bri: 4,  con: 16, temp: 5,  gamma: 95, shadows: 120, highlights: 95, clarity: 116 },
  { id: "fortnite",  name: "Fortnite",  mode: "Competitive", sub: "battle royale · pop",    img: "media/scene-fortnite.jpg",
    sat: 180, vibrance: 150, bri: 4,  con: 10, temp: 0,  gamma: 96, shadows: 118, highlights: 95, clarity: 112 },
  { id: "tarkov",    name: "Tarkov",    mode: "Dark Room",   sub: "night raids · dark",     img: "media/scene-tarkov.jpg",
    sat: 180, vibrance: 150, bri: 26, con: 14, temp: -9, gamma: 76, shadows: 165, highlights: 85, clarity: 125 },
  { id: "warzone",   name: "Warzone",   mode: "Competitive", sub: "snow maps · glare",      img: "media/scene-warzone.jpg",
    sat: 180, vibrance: 150, bri: 8,  con: 18, temp: 0,  gamma: 92, shadows: 135, highlights: 92, clarity: 122 },
];
const STATS = [
  ["MODE",       g => g.mode],
  ["SATURATION", g => g.sat + "%"],
  ["BRIGHTNESS", g => sgn(g.bri)],
  ["CONTRAST",   g => sgn(g.con)],
  ["TEMP",       g => sgn(g.temp)],
  ["GAMMA",      g => (g.gamma / 100).toFixed(2)],
];

const grid = $("#gameGrid");
const stageImg = $("#stageImg");
let current = GAMES[0];
const stRange = { sat: $("#stSat"), bri: $("#stBri"), con: $("#stCon"), temp: $("#stTemp"), gam: $("#stGam") };

const stageEng = attachEnginePreview(stageImg);
function renderStage() {
  const g = current;
  const s = { sat: +stRange.sat.value, bri: +stRange.bri.value, con: +stRange.con.value,
              temp: +stRange.temp.value, gamma: +stRange.gam.value / 100 };
  $("#stageName").textContent = g.name.toUpperCase();
  if (stageEng && stageEng.ok()) {
    stageEng.render({ sat: s.sat, vibrance: g.vibrance || 100, bri: 100 + s.bri, con: 100 + s.con,
                      gamma: s.gamma, temp: 6500 + s.temp * 22,
                      shadows: g.shadows || 100, highlights: g.highlights || 100,
                      clarity: g.clarity || 100, hue: g.hue || 0 });
  } else {
    stageImg.style.filter = cssFilter(s);
  }
  tint($("#stWarm"), $("#stCool"), s.temp);
  $("#stSatVal").textContent = s.sat + "%";
  $("#stBriVal").textContent = sgn(s.bri);
  $("#stConVal").textContent = sgn(s.con);
  $("#stTempVal").textContent = sgn(s.temp);
  $("#stGamVal").textContent = s.gamma.toFixed(2);
  Object.values(stRange).forEach(paintRange);
}
function selectGame(g, push = true) {
  current = g;
  stageImg.src = g.img;
  stRange.sat.value = g.sat; stRange.bri.value = g.bri; stRange.con.value = g.con;
  stRange.temp.value = g.temp; stRange.gam.value = g.gamma;
  $("#stageStats").innerHTML = STATS
    .map(([label, f]) => `<span class="stat">${label} <b>${f(g)}</b></span>`).join("");
  $$(".game-card").forEach(c => c.classList.toggle("active", c.dataset.id === g.id));
  if (push) history.replaceState(null, "", "#games");
  renderStage();
}
grid.innerHTML = GAMES.map(g => `
  <button class="game-card" data-id="${g.id}" role="tab" aria-selected="false">
    <img class="game-thumb" src="${g.img}" alt="" loading="lazy">
    <span><b>${g.name}</b><span>${g.sub}</span></span>
    <span class="go">→</span>
  </button>`).join("");
grid.addEventListener("click", e => {
  const card = e.target.closest(".game-card");
  if (!card) return;
  selectGame(GAMES.find(g => g.id === card.dataset.id));
});
Object.values(stRange).forEach(el => bindRange(el, renderStage));
selectGame(GAMES[0], false);

/* ---------- toast ---------- */
const toastEl = document.createElement("div");
toastEl.className = "toast";
toastEl.setAttribute("role", "status");
toastEl.setAttribute("aria-live", "polite");
document.body.appendChild(toastEl);
let toastTimer;
function toast(msg) {
  toastEl.textContent = msg;
  toastEl.classList.add("show");
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => toastEl.classList.remove("show"), 2200);
}
async function copy(text, label) {
  try { await navigator.clipboard.writeText(text); }
  catch {
    const ta = document.createElement("textarea");
    ta.value = text; document.body.appendChild(ta); ta.select();
    document.execCommand("copy"); ta.remove();
  }
  toast(label + " copied ✓");
}
/* Optional wiring: every lookup is null-checked so one missing element can
 * never abort the whole script again (this exact failure mode once killed the
 * reveal observer, mock tabs and recommender because #stCopy/#stCode/#sha had
 * been removed from the markup while the listeners stayed). */
function on(sel, ev, fn) { const el = $(sel); if (el) el.addEventListener(ev, fn); return !!el; }

on("#copyShaPortable", "click", () => copy($("#shaPortable")?.textContent.trim() || "", "Portable SHA-256"));
on("#copyShaSetup",    "click", () => copy($("#shaSetup")?.textContent.trim() || "", "Setup SHA-256"));
on("#copyUrl", "click", () => copy("http://192.168.1.24:8777", "Phone link"));

/* ---------- app mock ---------- */
$("#mockSide").addEventListener("click", e => {
  const btn = e.target.closest(".mock-side-item");
  if (!btn) return;
  $$(".mock-side-item").forEach(b => b.classList.toggle("active", b === btn));
  $$(".mock-tab").forEach(t => t.classList.toggle("active", t.dataset.tab === btn.dataset.tab));
});
/* switches */
document.addEventListener("click", e => {
  const sw = e.target.closest(".sw");
  if (!sw) return;
  sw.classList.toggle("on");
  sw.setAttribute("aria-pressed", sw.classList.contains("on"));
  if (sw.id === "mockMaster") toast(sw.classList.contains("on") ? "Look on" : "Look off");
  if (sw.id === "xhToggle") $("#crossGlyph").style.opacity = sw.classList.contains("on") ? 1 : 0;
});
/* mock display sliders live value text + readout */
$$(".mock-slider input").forEach(inp => {
  bindRange(inp, v => {
    const out = inp.parentElement.querySelector("output");
    const min = +inp.min;
    if (min < 0) out.textContent = sgn(v) + (inp.max <= 60 ? "°" : "%");
    else if (inp.max <= 300 && inp.min === 100) { out.textContent = v + "%"; }
    else out.textContent = (v / 100).toFixed(2);
    if (inp.id === "mockSat") $("#mockSatOut").textContent = v + "%";
  });
});
/* scenes drive the mock saturation + toast */
$("#mockScenes .chip")?.classList.add("active");
$("#mockScenes").addEventListener("click", e => {
  const chip = e.target.closest(".chip");
  if (!chip) return;
  $$(".chip", $("#mockScenes")).forEach(c => c.classList.toggle("active", c === chip));
  const sat = $("#mockSat");
  sat.value = Math.min(300, +chip.dataset.sat);
  sat.dispatchEvent(new Event("input"));
  toast(`${chip.textContent} scene applied`);
});
/* crosshair shapes */
$("#crossShapes").addEventListener("click", e => {
  const b = e.target.closest(".shape");
  if (!b) return;
  $$(".shape").forEach(x => x.classList.toggle("active", x === b));
  $("#crossGlyph").dataset.shape = b.dataset.shape;
  toast(b.textContent + " crosshair");
});

/* ---------- reveal on scroll ---------- */
const io = new IntersectionObserver(es => {
  es.forEach(en => { if (en.isIntersecting) { en.target.classList.add("in"); io.unobserve(en.target); } });
}, { threshold: 0.12 });
$$(".reveal").forEach(el => io.observe(el));

/* ---------- download ping ---------- */
$$('a[href*=".exe"]').forEach(a =>
  a.addEventListener("click", () => toast("Download started — verify the SHA-256 after ✓")));

/* ---------- Profile Recommender ---------- */
const recBtn = $("#recBtn");
if (recBtn) {
  recBtn.addEventListener("click", () => {
    const game = $("#recGame").value;
    const prio = $("#recPriority").value;
    const env = $("#recEnv").value;
    const hdr = $("#recHdr").value;
    
    let sub = "Balanced";
    let desc = "";
    let caveat = "";
    let lk = { bri: 100, con: 100, sat: 100, gam: 1.0 };

    /* Deterministic rules.  Every recommendation below is the actual value set
     * of a shipped built-in preset (app/src/games/game_preset_manager.c scene
     * library / preset store), so the numbers you see here are the numbers the
     * app applies — not invented marketing values. */
    if (prio === "competitive") {
      sub = "Competitive — matches the built-in “Competitive” preset";
      desc = "180% saturation with a bounded vibrance boost, lifted shadows and slight clarity. Separates player models from the environment without oversaturating skin tones.";
      lk = { bri: 106, con: 114, sat: 180, gam: 0.95 };
    } else if (prio === "natural") {
      sub = "Natural — matches the built-in “Natural Neutral” preset";
      desc = "Every control at its no-op value: the game renders exactly as the artist intended. Also the baseline every other preset resets to.";
      lk = { bri: 100, con: 100, sat: 100, gam: 1.0 };
    } else {
      // Balanced
      if (env === "dark") {
        sub = "Low-Light / Night — matches the built-in “Night Ops” preset";
        desc = "Elevates shadows (155%) and steepens gamma to 0.78 without blowing out UI highlights; slightly warm 6200K.";
        lk = { bri: 122, con: 110, sat: 180, gam: 0.78 };
      } else if (env === "snow") {
        sub = "Snow / Bright — matches the built-in “Snow Glare” preset";
        desc = "Pulls brightness down to 92%, raises contrast and cools to 7200K with compressed highlights to reduce glare strain.";
        lk = { bri: 92, con: 118, sat: 180, gam: 1.05 };
      } else if (env === "forest") {
        sub = "Forest — matches the built-in “Treeline Forest” preset";
        desc = "Green-biased gains and lifted shadow-mids to see into foliage, with a mild green/magenta tint correction.";
        lk = { bri: 105, con: 114, sat: 180, gam: 0.96 };
      } else {
        sub = "Daylight / Mixed — matches the app’s “Daylight” game sub-mode";
        desc = "A solid all-around profile: gentle contrast and clarity lift, neutral 6500K white point. Safe for indoor and outdoor environments.";
        lk = { bri: 104, con: 112, sat: 180, gam: 0.98 };
      }
    }
    
    if (hdr === "hdr") {
      caveat = "Note: HDR displays require Windows Advanced Color. Some legacy gamma features (like Shadows/Highlights sliders) will not apply in HDR mode.";
      $("#recCaveat").style.display = "block";
      $("#recCaveat").textContent = caveat;
    } else {
      $("#recCaveat").style.display = "none";
    }
    
    $("#recTitle").textContent = sub;
    $("#recDesc").textContent = desc;
    $("#recBri").textContent = lk.bri + "%";
    $("#recCon").textContent = lk.con + "%";
    $("#recSat").textContent = lk.sat + "%";
    $("#recGam").textContent = lk.gam.toFixed(2);
    
    $("#recResult").style.display = "block";
    toast("Recommendation generated");
  });
}

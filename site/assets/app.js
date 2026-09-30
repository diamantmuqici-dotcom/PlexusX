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

/* ---------- shared filter maths (mirrors the app) ---------- */
function cssFilter({ sat = 100, bri = 0, con = 0, hue = 0, gamma = 1 }) {
  // gamma approximated as a brightness/contrast pair (preview only)
  const gBri = bri + (1 - gamma) * 60;
  const gCon = con + (gamma - 1) * 22;
  return `saturate(${sat}%) brightness(${1 + gBri / 130}) contrast(${1 + gCon / 130}) hue-rotate(${hue}deg)`;
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
bindRange(heroSat, v => {
  $("#heroSatVal").textContent = v + "%";
  heroImg.style.filter = cssFilter({ sat: v, con: 8 });
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
  function applyPreview() {
    const s = +sat.value, c = +con.value, t = +temp.value;
    $("#cmpSatVal").textContent = s + "%";
    $("#cmpConVal").textContent = sgn(c);
    $("#cmpTempVal").textContent = sgn(t);
    $("#cmpTag").textContent = "CHROMAX · " + s + "%";
    afterImg.style.filter = cssFilter({ sat: s, con: c });
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

/* ---------- game gallery ---------- */
const GAMES = [
  { id: "rust",      name: "Rust",      sub: "survival · treelines",   img: "media/scene-rust.jpg",
    sat: 250, bri: 6,  con: 14, temp: 4,   gamma: 100 },
  { id: "cs2",       name: "CS2",       sub: "tactical · dust",        img: "media/scene-cs2.jpg",
    sat: 235, bri: 0,  con: 18, temp: -4,  gamma: 100 },
  { id: "valorant",  name: "Valorant",  sub: "hero shooter · clean",   img: "media/scene-valorant.jpg",
    sat: 240, bri: 2,  con: 14, temp: 0,   gamma: 100 },
  { id: "fortnite",  name: "Fortnite",  sub: "battle royale · pop",    img: "media/scene-fortnite.jpg",
    sat: 200, bri: 0,  con: 8,  temp: 0,   gamma: 100 },
  { id: "tarkov",    name: "Tarkov",    sub: "night raids · dark",     img: "media/scene-tarkov.jpg",
    sat: 265, bri: 28, con: 12, temp: -6,  gamma: 85 },
  { id: "warzone",   name: "Warzone",   sub: "snow maps · glare",      img: "media/scene-warzone.jpg",
    sat: 245, bri: -4, con: 16, temp: -10, gamma: 95 },
];
const STATS = [
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

function code(g) {
  const p = n => String(Math.abs(n)).padStart(2, "0");
  return `CHX-${g.id.toUpperCase()}-${g.sat}-${g.bri < 0 ? "m" : ""}${p(g.bri)}-${p(g.con)}-${p(g.temp)}`;
}
function renderStage() {
  const g = current;
  const s = { sat: +stRange.sat.value, bri: +stRange.bri.value, con: +stRange.con.value,
              temp: +stRange.temp.value, gamma: +stRange.gam.value / 100 };
  $("#stageName").textContent = g.name.toUpperCase();
  stageImg.style.filter = cssFilter(s);
  tint($("#stWarm"), $("#stCool"), s.temp);
  $("#stSatVal").textContent = s.sat + "%";
  $("#stBriVal").textContent = sgn(s.bri);
  $("#stConVal").textContent = sgn(s.con);
  $("#stTempVal").textContent = sgn(s.temp);
  $("#stGamVal").textContent = s.gamma.toFixed(2);
  $("#stCode").textContent = code({ ...g, sat: s.sat, bri: s.bri, con: s.con, temp: s.temp });
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
$("#stCopy").addEventListener("click", () => copy($("#stCode").textContent, "Preset code"));
$("#copySha").addEventListener("click", () => copy($("#sha").textContent, "SHA-256"));
$("#copyUrl")?.addEventListener("click", () => copy("http://192.168.1.24:8777", "Phone link"));

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

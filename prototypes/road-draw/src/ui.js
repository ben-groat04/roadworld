// ui.js: profile panel, HUD, tuning panel, help, main loop and scripting hook.
// ===== Profile panel (cut / fill against the ORIGINAL ground) =====
function renderProfile() {
  if (!state.profile) return;
  const PW = VW, PH = PROF_H;
  fc.setTransform(profDpr, 0, 0, profDpr, 0, 0);
  fc.fillStyle = getComputedStyle(document.body).getPropertyValue('--panel'); fc.fillRect(0, 0, PW, PH);
  const r = (state.mode === 'draw' && state.preview && state.preview.road) || state.sel || state.hover;
  fc.fillStyle = '#8b949e'; fc.font = '12px system-ui';
  if (!r) { fc.fillText('Profile: draw a road, or select one in edit mode (Tab). P hides this panel.', 16, 22); return; }
  const S = r.samples, L = r.chain.L, ml = 52, mr = 16, mt = 22, mb = 24, w = PW - ml - mr, h = PH - mt - mb;
  let lo = Infinity, hi = -Infinity; for (const s of S) { lo = Math.min(lo, s.z, s.t); hi = Math.max(hi, s.z, s.t); }
  const pad = Math.max(5, (hi - lo) * 0.15); lo -= pad; hi += pad;
  const X = s => ml + s / L * w, Y = z => mt + (hi - z) / (hi - lo) * h;
  fc.strokeStyle = 'rgba(255,255,255,0.07)'; fc.lineWidth = 1; fc.fillStyle = '#8b949e';
  const zs = [1, 2, 5, 10, 20, 50].find(v => (hi - lo) / v < 8) || 50; for (let z = Math.ceil(lo / zs) * zs; z < hi; z += zs) { fc.beginPath(); fc.moveTo(ml, Y(z)); fc.lineTo(ml + w, Y(z)); fc.stroke(); fc.fillText(z + ' m', 8, Y(z) + 4); }
  const ss = [10, 20, 50, 100, 200, 500, 1000].find(v => L / v < 10) || 1000; for (let s = 0; s <= L; s += ss) fc.fillText(s + '', X(s) - 6, PH - 6);
  fc.beginPath(); fc.moveTo(X(0), mt + h); S.forEach(s => fc.lineTo(X(s.s), Y(s.t))); fc.lineTo(X(L), mt + h); fc.fillStyle = '#3a3f2e'; fc.fill();
  for (let i = 0; i + 1 < S.length; i++) {
    const a = S[i], b = S[i + 1], dz = (a.z + b.z) / 2 - (a.t + b.t) / 2;
    fc.beginPath(); fc.moveTo(X(a.s), Y(a.z)); fc.lineTo(X(b.s), Y(b.z)); fc.lineTo(X(b.s), Y(b.t)); fc.lineTo(X(a.s), Y(a.t));
    fc.fillStyle = Math.abs(dz) > T.wallCap ? 'rgba(229,72,77,0.55)' : dz > 0 ? 'rgba(126,224,255,0.35)' : 'rgba(245,166,35,0.4)'; fc.fill();
  }
  fc.beginPath(); S.forEach((s, i) => i ? fc.lineTo(X(s.s), Y(s.t)) : fc.moveTo(X(s.s), Y(s.t))); fc.strokeStyle = '#9aa57a'; fc.lineWidth = 1.5; fc.stroke();
  fc.beginPath(); S.forEach((s, i) => i ? fc.lineTo(X(s.s), Y(s.z)) : fc.moveTo(X(s.s), Y(s.z))); fc.strokeStyle = '#fff'; fc.lineWidth = 2.5; fc.stroke();
  r.vp.forEach((b, i) => {
    if (b.L > 0) { fc.fillStyle = 'rgba(255,216,102,0.18)'; fc.fillRect(X(b.s - b.L / 2), mt, X(b.s + b.L / 2) - X(b.s - b.L / 2), h); }
    fc.fillStyle = '#ffd866'; fc.beginPath(); fc.arc(X(b.s), Y(b.z), 3.5, 0, 7); fc.fill();
    if (i + 1 < r.vp.length) {
      const n = r.vp[i + 1], g = (n.z - b.z) / (n.s - b.s) * 100, ty = ROAD_TYPES[r.type];
      fc.fillStyle = Math.abs(g) > ty.maxGrade ? '#f5a623' : '#e6edf3'; fc.font = '12px system-ui';
      fc.fillText((g >= 0 ? '+' : '') + g.toFixed(1) + '%', X((b.s + n.s) / 2) - 16, Y((b.z + n.z) / 2) - 8);
    }
  });
  fc.fillStyle = '#8b949e'; fc.fillText(`Profile · ${L.toFixed(0)} m · vs original ground · blue = fill, orange = cut, red = over ${T.wallCap} m wall cap (bridge/tunnel) · yellow = vertical curves`, ml, 15);
}
function renderHud() {
  const ty = ROAD_TYPES[state.type], pv = state.preview;
  let html = `<b>${state.mode === 'draw' ? 'DRAW' : 'EDIT'}</b> · ${ty.name}${state.oneWay ? (ty.dual ? ' · <b>single carriageway</b>' : ' · one-way') : ''} · colour: ${COLOUR_MODES[state.colour]}${state.shift ? ' · <span class="warn">snap off</span>' : ''}`;
  if (state.mode === 'draw' && state.snap) {
    const z = pv ? pv.cursorZ : snapZ(state.snap);
    html += `<br>cursor z ${z != null ? z.toFixed(1) : '-'} m${state.dz ? ` (${state.dz > 0 ? '+' : ''}${state.dz} m)` : ''} · snap: ${state.snap.kind}`;
  }
  if (pv && pv.road) {
    const f = R => R === Infinity ? '∞' : R.toFixed(0);
    html += `<br>span ${pv.spanLen.toFixed(1)} m · bearing ${pv.heading.toFixed(0)}° · R at cursor ${f(pv.endR)} m · min R ${f(pv.minR)} m`;
    html += `<br>grade ${pv.grade.toFixed(1)}% · max ${pv.maxG.toFixed(1)}% · ${pv.road.chain.g2 ? 'G2 ✓' : 'G1'} · total ${pv.road.chain.L.toFixed(0)} m`;
  }
  if (pv) { for (const e of pv.errors) html += `<br><span class="err">✕ ${e}</span>`; for (const w of pv.warns) html += `<br><span class="warn">⚠ ${w}</span>`; }
  if (state.mode === 'edit' && state.sel) html += `<br>selected road #${state.sel.id} · ${state.sel.chain.L.toFixed(0)} m · drag handles, PgUp/PgDn height, Del delete`;
  document.getElementById('hud').innerHTML = html;
}

// ===== Tuning panel =====
const TUNE = [['snapAngle', 'Snap angle °', 5, 45, 5], ['snapLen', 'Snap length m', 1, 50, 1], ['wallCap', 'Wall cap m (bridge/tunnel)', 3, 30, 1], ['wallStart', 'Wall start m', 2, 12, 1],
  ['batter', 'Batter (H per V)', 1, 4, 0.1], ['kMul', 'Vert. curve K ×', 0.25, 2, 0.05], ['minSpan', 'Min segment m', 1, 20, 1], ['water', 'Water level m', 0, 60, 0.5]];
document.getElementById('tune-body').innerHTML = TUNE.map(([k, l, a, b, s]) => `<label>${l} <span id="v-${k}">${T[k]}</span><input type="range" min="${a}" max="${b}" step="${s}" value="${T[k]}" data-k="${k}"></label>`).join('');
let refitTimer = 0;
function refitAll() { for (const r of roads) rebuild(r); regradeAll(); updateTerrainMesh(null); buildPreview(); dirty(); }
function setTune(k, v) {
  T[k] = +v; const el = document.getElementById('v-' + k); if (el) el.textContent = T[k];
  const inp = document.querySelector(`#tune-body input[data-k="${k}"]`); if (inp && +inp.value !== T[k]) inp.value = T[k];
  if (k === 'water') { water.position.y = T.water; clearTimeout(refitTimer); refitTimer = setTimeout(() => { updateTerrainMesh(null); dirty(); }, 150); }
  else if (['kMul', 'wallCap', 'wallStart', 'batter'].includes(k)) { clearTimeout(refitTimer); refitTimer = setTimeout(refitAll, 120); }
  buildPreview(); dirty();
}
document.querySelectorAll('#tune-body input').forEach(inp => inp.addEventListener('input', () => setTune(inp.dataset.k, inp.value)));
document.getElementById('tune-toggle').addEventListener('click', () => document.getElementById('tune').classList.toggle('open'));
document.getElementById('help').addEventListener('click', () => { state.help = false; document.getElementById('help').style.display = 'none'; });

// ===== Main loop =====
let lastT = performance.now();
function frame(now) {
  requestAnimationFrame(frame);
  const dt = Math.min(0.1, (now - lastT) / 1000); lastT = now;
  updateCamera(dt);
  if (mouseDirty) processMouse();
  syncRoads();
  if (overlayDirty) { overlayDirty = false; buildOverlay(); }
  if (needsUI) { needsUI = false; renderProfile(); renderHud(); }
  if (needsRender || performance.now() - state.flash < 300) {
    needsRender = false;
    scaleOverlay(); updateShadow();
    renderer.render(scene, camera);
  }
}
resize();
setView(1000, 900, 1300, 0.35, 0.95);
requestAnimationFrame(frame);

// Scripting hook for automated screenshots.
window.__proto = {
  THREE, state, T, scene, camera, controls, roads: () => roads, hAt, terrainH, setView, project, pick, setTune, undo, redo, flushGrade, regradeAll,
  undoCount: () => undoStack.length, TG, TH, ROAD_TYPES, crossSections, maskCanvas,
  drawRoad(type, pts, oneWay = false) {
    state.type = type; state.oneWay = oneWay; state.mode = 'draw';
    state.draw = { pts: pts.map(p => ({ x: p[0], y: p[1], z: p[2] != null ? p[2] : hAt(p[0], p[1]) })), hs: null, attach: pts.map(() => 'free') };
    finishRoad(); dirty();
  },
  renderNow() { syncRoads(); buildOverlay(); scaleOverlay(); updateShadow(); renderer.render(scene, camera); },
};

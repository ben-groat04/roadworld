// input.js: commands/undo, picking, snapping, drawing preview and overlay, edit mode, keyboard and mouse.
// ===== Commands =====
function exec(cmd) { if (cmd.apply() === false) return false; undoStack.push(cmd); redoStack.length = 0; flushGrade(); dirty(); return true; }
function undo() { const c = undoStack.pop(); if (c) { c.revert(); redoStack.push(c); flushGrade(); fixSelection(); dirty(); } }
function redo() { const c = redoStack.pop(); if (c) { c.apply(); undoStack.push(c); flushGrade(); fixSelection(); dirty(); } }
const AddRoads = list => ({ apply() { roads.push(...list); list.forEach(touch); }, revert() { list.forEach(touch); roads = roads.filter(r => !list.includes(r)); } });
const DeleteRoad = r => { let idx; return { apply() { touch(r); idx = roads.indexOf(r); roads.splice(idx, 1); }, revert() { roads.splice(idx, 0, r); touch(r); } }; };
const EditPoint = (r, i, before, after) => ({
  apply() { touch(r); r.pts[i] = { ...after }; if (!rebuild(r)) { r.pts[i] = { ...before }; rebuild(r); pend = []; return false; } touch(r); },
  revert() { touch(r); r.pts[i] = { ...before }; rebuild(r); touch(r); },
});
function fixSelection() { if (state.sel && !roads.includes(state.sel)) state.sel = null; if (state.hover && !roads.includes(state.hover)) state.hover = null; }

// ===== Picking =====
const ray = new THREE.Raycaster(), ndc = new THREE.Vector2();
function rayTerrain(o, d) {
  const max = 14000; let prev = 0, inside = false;
  for (let t = 2; t < max; t += 2) {
    const x = o.x + d.x * t, y = -(o.z + d.z * t), py = o.y + d.y * t;
    if (x < 0 || x > WORLD || y < 0 || y > WORLD) { if (inside) return null; prev = t; continue; }
    inside = true;
    if (py < hAt(x, y)) {
      let a = prev, b = t;
      for (let i = 0; i < 24; i++) { const m = (a + b) / 2; if (o.y + d.y * m < hAt(o.x + d.x * m, -(o.z + d.z * m))) b = m; else a = m; }
      return (a + b) / 2;
    }
    prev = t;
  }
  return null;
}
function pick(sx, sy) {
  ndc.set(sx / VW * 2 - 1, -(sy / VH) * 2 + 1); ray.setFromCamera(ndc, camera);
  const o = ray.ray.origin, d = ray.ray.direction, tt = rayTerrain(o, d);
  const hits = pickMeshes.length ? ray.intersectObjects(pickMeshes, false) : [];
  let dist = tt, road = null;
  if (hits.length && (tt == null || hits[0].distance < tt + 0.3)) { dist = hits[0].distance; road = hits[0].object.userData.road; }
  if (dist == null) return null;
  const x = o.x + d.x * dist, y = -(o.z + d.z * dist), z = o.y + d.y * dist;
  return { x, y, z, road, dist, wpp: dist * 2 * Math.tan(camera.fov * Math.PI / 360) / VH };
}
const wppAt = (x, y, z) => camera.position.distanceTo(new THREE.Vector3(x, z, -y)) * 2 * Math.tan(camera.fov * Math.PI / 360) / VH;
function project(x, y, z) { const v = new THREE.Vector3(x, z, -y).project(camera); return [(v.x + 1) / 2 * VW, (1 - v.y) / 2 * VH, v.z]; }

// ===== Snapping =====
function roadEnds() {
  const out = [];
  for (const r of roads) {
    const L = r.chain.L, a = G.chainPose(r.chain, 0), b = G.chainPose(r.chain, L);
    out.push({ r, s: 0, x: a.x, y: a.y, z: r.vp[0].z, out: a.h + Math.PI });
    out.push({ r, s: L, x: b.x, y: b.y, z: r.vp[r.vp.length - 1].z, out: b.h });
  }
  return out;
}
function nearestOnRoad(x, y, maxD, skip, hitZ) {
  let best = null;
  for (const r of roads) {
    if (r === skip) continue;
    const S = r.samples;
    for (let i = 0; i + 1 < S.length; i++) {
      const a = S[i], b = S[i + 1];
      if (hitZ != null && Math.abs(a.z - hitZ) > 15) continue;
      const dx = b.x - a.x, dy = b.y - a.y, l2 = dx * dx + dy * dy || 1;
      const t = clamp(((x - a.x) * dx + (y - a.y) * dy) / l2, 0, 1);
      const px = a.x + dx * t, py = a.y + dy * t, d = Math.hypot(x - px, y - py);
      if (d < maxD && (!best || d < best.d)) {
        const sgn = Math.sign(dx * (y - a.y) - dy * (x - a.x)) || 1;
        best = { r, d, x: px, y: py, s: a.s + (b.s - a.s) * t, z: a.z + (b.z - a.z) * t, h: a.h, sd: sgn * d, i };
      }
    }
  }
  return best;
}
function computeSnap(pk) {
  const mx = pk.x, my = pk.y, px = pk.wpp, hz = pk.z;
  const d = state.draw, last = d && d.pts.length ? d.pts[d.pts.length - 1] : null;
  const res = { x: mx, y: my, z: null, kind: 'free', guide: null };
  if (!state.shift) {
    let be = null;
    for (const e of roadEnds()) { const dd = Math.hypot(e.x - mx, e.y - my); if (dd < 14 * px && Math.abs(e.z - hz) < 15 && (!be || dd < be.dd)) be = { ...e, dd }; }
    if (be && !(last && Math.hypot(be.x - last.x, be.y - last.y) < 0.01)) return { x: be.x, y: be.y, z: be.z, kind: 'end', out: be.out };
    const on = nearestOnRoad(mx, my, 10 * px, null, hz);
    if (on) return { x: on.x, y: on.y, z: on.z, kind: 'road', road: on.r };
    let bg = null;
    for (const e of roadEnds()) {
      const ux = Math.cos(e.out), uy = Math.sin(e.out), t = (mx - e.x) * ux + (my - e.y) * uy;
      if (t <= 1) continue;
      const qx = e.x + ux * t, qy = e.y + uy * t, dd = Math.hypot(mx - qx, my - qy);
      if (dd < 9 * px && (!bg || dd < bg.dd)) bg = { dd, x: qx, y: qy, guide: { type: 'line', x0: e.x, y0: e.y, x1: qx, y1: qy } };
    }
    if (bg) { res.x = bg.x; res.y = bg.y; res.kind = 'guide'; res.guide = bg.guide; }
    if (!bg) {
      const near = nearestOnRoad(mx, my, 80, null, hz);
      if (near) {
        const tyA = ROAD_TYPES[near.r.type], tyB = ROAD_TYPES[state.type];
        const single = tyB.dual && state.oneWay;   // one carriageway at a time: centre-to-centre = median gap + carriageway width
        const base = single ? tyA.total / 2 + (tyA.dual ? tyA.gap / 2 : 0) + tyB.total / 2 + tyB.gap / 2 : tyA.total / 2 + (tyB.dual ? tyB.total + tyB.gap / 2 : tyB.total / 2);
        for (const k of [1, 2, 3]) {
          const off = base * k;
          if (Math.abs(Math.abs(near.sd) - off) < 7 * px) {
            const nx = -Math.sin(near.h), ny = Math.cos(near.h), sg = Math.sign(near.sd);
            res.x = near.x + nx * off * sg; res.y = near.y + ny * off * sg; res.kind = 'guide';
            res.guide = { type: 'offset', road: near.r, off: off * sg };
            break;
          }
        }
      }
    }
    if (res.kind === 'free' && last) {
      let base = 0;
      if (d.pts.length >= 2) { const p = d.pts[d.pts.length - 2]; base = Math.atan2(last.y - p.y, last.x - p.x); }
      else if (d.hs != null) base = d.hs;
      const step = T.snapAngle * Math.PI / 180;
      const ang = base + Math.round(G.wrap(Math.atan2(my - last.y, mx - last.x) - base) / step) * step;
      const len = Math.max(T.snapLen, Math.round(Math.hypot(mx - last.x, my - last.y) / T.snapLen) * T.snapLen);
      res.x = last.x + Math.cos(ang) * len; res.y = last.y + Math.sin(ang) * len; res.kind = 'angle';
      res.guide = { type: 'line', x0: last.x, y0: last.y, x1: res.x, y1: res.y, faint: true };
    }
  }
  if (res.kind === 'free' && pk.road) res.z = pk.z;   // clicked onto a bridge deck / road surface
  return res;
}
const snapZ = sn => sn.z != null ? sn.z : hAt(sn.x, sn.y) + state.dz;

// ===== Preview + validation =====
function buildPreview() {
  const d = state.draw, sn = state.snap;
  if (!d || !sn) { state.preview = null; return; }
  const ty = ROAD_TYPES[state.type], z = sn.z != null ? sn.z + state.dz : hAt(sn.x, sn.y) + state.dz;
  const pts = [...d.pts, { x: sn.x, y: sn.y, z }];
  const he = sn.kind === 'end' ? sn.out + Math.PI : null;
  const r = { id: 0, type: state.type, pts, hs: d.hs, he };
  const pv = { road: null, errors: [], warns: [], cursorZ: z };
  if (pts.length >= 2) {
    for (let i = 0; i + 1 < pts.length; i++) if (Math.hypot(pts[i + 1].x - pts[i].x, pts[i + 1].y - pts[i].y) < T.minSpan) { pv.errors.push(`segment shorter than ${T.minSpan} m`); break; }
    if (!pv.errors.length && rebuild(r)) {
      pv.road = r;
      let kmax = 0, gmax = 0;
      for (const s of r.samples) { kmax = Math.max(kmax, Math.abs(s.k)); gmax = Math.max(gmax, Math.abs(s.g)); }
      pv.minR = kmax > 1e-9 ? 1 / kmax : Infinity; pv.maxG = gmax * 100;
      const lastS = r.samples[r.samples.length - 1];
      pv.endR = Math.abs(lastS.k) > 1e-9 ? 1 / Math.abs(lastS.k) : Infinity;
      pv.spanLen = r.chain.spans[r.chain.spans.length - 1].L;
      pv.heading = ((90 - lastS.h * 180 / Math.PI) % 360 + 360) % 360;
      const n = pts.length; pv.grade = (pts[n - 1].z - pts[n - 2].z) / pv.spanLen * 100;
      const halfW = (ty.dual && !state.oneWay ? ty.gap / 2 + ty.total : ty.total / 2);
      if (pv.minR < halfW) pv.errors.push(`too tight: radius ${pv.minR.toFixed(1)} m < half-width ${halfW.toFixed(1)} m (edge folds)`);
      if (pv.minR < ty.minR) pv.warns.push(`radius ${pv.minR.toFixed(0)} m below comfortable ${ty.minR} m for ${ty.name}`);
      if (pv.maxG > ty.maxGrade) pv.warns.push(`grade ${pv.maxG.toFixed(1)}% above advisory ${ty.maxGrade}%`);
      let br = false, tu = false; for (const s of r.samples) { if (s.br) br = true; if (s.tu) tu = true; }
      if (br) pv.warns.push(`fill over ${T.wallCap} m wall cap: becomes a bridge`);
      if (tu) pv.warns.push(`cut over ${T.wallCap} m wall cap: becomes a tunnel`);
      if (!r.chain.g2) pv.warns.push('G2 solve fell back to G1 (curvature kink)');
    } else if (!pv.errors.length) pv.errors.push('cannot fit curve');
  }
  state.preview = pv;
}
function startOrAddPoint() {
  const sn = state.snap; if (!sn) return;
  const pv = state.preview;
  if (pv && pv.errors.length) { state.flash = performance.now(); dirty(); return; }
  const z = snapZ(sn);
  if (!state.draw) state.draw = { pts: [{ x: sn.x, y: sn.y, z }], hs: sn.kind === 'end' ? sn.out : null, attach: [sn.kind] };
  else {
    state.draw.pts.push({ x: sn.x, y: sn.y, z });
    state.draw.attach.push(sn.kind);
    if (sn.kind === 'end' || sn.kind === 'road') { state.draw.he = sn.kind === 'end' ? sn.out + Math.PI : null; finishRoad(); return; }
  }
  state.dz = 0; dirty();
}
function finishRoad() {
  const d = state.draw; state.draw = null; state.preview = null; state.dz = 0;
  if (!d || d.pts.length < 2) { dirty(); return; }
  const ty = ROAD_TYPES[state.type];
  const centre = { id: nextId++, type: state.type, oneWay: state.oneWay, pts: d.pts, hs: d.hs, he: d.he ?? null, attach: d.attach };
  if (!rebuild(centre)) { dirty(); return; }
  if (!ty.dual || state.oneWay) { exec(AddRoads([centre])); return; }
  const off = ty.gap / 2 + ty.total / 2, sideSign = DRIVING_SIDE === 'left' ? 1 : -1;
  const offPts = sg => centre.pts.map((p, i) => { const h = G.chainPose(centre.chain, centre.S[i]).h; return { x: p.x - Math.sin(h) * off * sg, y: p.y + Math.cos(h) * off * sg, z: p.z }; });
  const fwd = { id: nextId++, type: state.type, oneWay: true, pts: offPts(sideSign), hs: d.hs, he: d.he ?? null, attach: d.attach };
  const back = { id: nextId++, type: state.type, oneWay: true, pts: offPts(-sideSign).reverse(),
    hs: d.he != null ? d.he + Math.PI : null, he: d.hs != null ? d.hs + Math.PI : null, attach: [...d.attach].reverse() };
  const made = [fwd, back].filter(rebuild);
  if (made.length) exec(AddRoads(made));
}

// ===== Overlay (ghost road, guides, cursor ring, points, edit handles) =====
const overlay = new THREE.Group(); scene.add(overlay);
const scaled = [];   // {obj, px, at: Vector3}
const KIND_COL = { end: 0x3fb950, road: 0x3fb950, guide: 0x7ee0ff, angle: 0xffffff, free: 0xffffff };
function clearOverlay() { for (const o of [...overlay.children]) { overlay.remove(o); if (o.geometry) o.geometry.dispose(); } scaled.length = 0; }
function lineObj(pts, color, dashed, opacity = 1) {
  const g = new THREE.BufferGeometry().setFromPoints(pts.map(p => new THREE.Vector3(...w3(p[0], p[1], p[2]))));
  const m = dashed ? new THREE.LineDashedMaterial({ color, dashSize: 4, gapSize: 3, depthTest: false, transparent: true, opacity }) : new THREE.LineBasicMaterial({ color, depthTest: false, transparent: true, opacity });
  const l = new THREE.Line(g, m); if (dashed) l.computeLineDistances(); l.renderOrder = 20; l.frustumCulled = false; return l;
}
function draped(P) {   // sample a 2D polyline every ~3 m onto the ground
  const out = [];
  for (let i = 0; i + 1 < P.length; i++) {
    const l = Math.hypot(P[i + 1][0] - P[i][0], P[i + 1][1] - P[i][1]), n = Math.max(1, Math.ceil(l / 3));
    for (let k = 0; k < n; k++) { const t = k / n, x = P[i][0] + (P[i + 1][0] - P[i][0]) * t, y = P[i][1] + (P[i + 1][1] - P[i][1]) * t; out.push([x, y, hAt(x, y) + 0.5]); }
  }
  const e = P[P.length - 1]; out.push([e[0], e[1], hAt(e[0], e[1]) + 0.5]); return out;
}
const offsetLine = (S, d) => S.map(s => [s.x - Math.sin(s.h) * d, s.y + Math.cos(s.h) * d]);
function ghostMesh(S, off, hw, color) {
  const mb = new MB([1, 1, 1]);
  for (let k = 0; k + 1 < S.length; k++) {
    const P = (q, l) => [S[q].x - Math.sin(S[q].h) * (off + l), S[q].y + Math.cos(S[q].h) * (off + l), S[q].z + 0.35];
    mb.quad(P(k, -hw), P(k + 1, -hw), P(k + 1, hw), P(k, hw), UP);
  }
  const m = mb.mesh(new THREE.MeshBasicMaterial({ color, transparent: true, opacity: 0.55, depthTest: false, depthWrite: false, side: THREE.DoubleSide }), false);
  if (m) m.renderOrder = 15; return m;
}
function ball(x, y, z, px, color) {
  const m = new THREE.Mesh(new THREE.SphereGeometry(1, 14, 10), new THREE.MeshBasicMaterial({ color, depthTest: false, transparent: true }));
  m.renderOrder = 30; m.position.set(...w3(x, y, z)); scaled.push({ obj: m, px }); overlay.add(m); return m;
}
function buildOverlay() {
  clearOverlay();
  if (state.mode === 'draw') {
    const pv = state.preview, ty = ROAD_TYPES[state.type];
    if (pv && pv.road) {
      const col = pv.errors.length ? 0xe5484d : 0xdde6f0;
      if (ty.dual && !state.oneWay) {
        const off = ty.gap / 2 + ty.total / 2;
        for (const sg of [1, -1]) { const m = ghostMesh(pv.road.samples, off * sg, ty.total / 2, col); if (m) overlay.add(m); }
      } else { const m = ghostMesh(pv.road.samples, 0, ty.total / 2, col); if (m) overlay.add(m); }
    }
    if (state.draw) for (const p of state.draw.pts) ball(p.x, p.y, p.z + 0.4, 4, 0xffd866);
    const sn = state.snap;
    if (sn) {
      const gd = sn.guide;
      if (gd && gd.type === 'line') overlay.add(lineObj(draped([[gd.x0, gd.y0], [gd.x1, gd.y1]]), gd.faint ? 0xffffff : 0x7ee0ff, true, gd.faint ? 0.5 : 1));
      else if (gd && gd.type === 'offset') overlay.add(lineObj(draped(offsetLine(gd.road.samples, gd.off)), 0x7ee0ff, true));
      const ring = new THREE.Mesh(new THREE.RingGeometry(0.72, 1, 40), new THREE.MeshBasicMaterial({ color: KIND_COL[sn.kind], side: THREE.DoubleSide, depthTest: false, transparent: true }));
      ring.rotation.x = -Math.PI / 2; ring.renderOrder = 30;
      const z = (sn.z != null ? sn.z : hAt(sn.x, sn.y)) + 0.5;
      ring.position.set(...w3(sn.x, sn.y, z)); scaled.push({ obj: ring, px: sn.kind === 'free' ? 7 : 11 }); overlay.add(ring);
    }
  } else {
    for (const r of roads) if (r === state.sel || r === state.hover) overlay.add(lineObj(r.samples.map(s => [s.x, s.y, s.z + 0.8]), r === state.sel ? 0xffd866 : 0xffe9a8, false));
    if (state.sel) state.sel.pts.forEach((p, i) => ball(p.x, p.y, p.z + 0.5, i === state.hoverHandle ? 10 : 7, i === state.hoverHandle ? 0xffd866 : 0xffffff));
  }
  needsRender = true;
}
function scaleOverlay() {
  for (const s of scaled) { const w = camera.position.distanceTo(s.obj.position) * 2 * Math.tan(camera.fov * Math.PI / 360) / VH * s.px; s.obj.scale.set(w, w, w); }
}

// ===== Input =====
let down = null;
function mouseXY(e) { const rc = canvas.getBoundingClientRect(); return [e.clientX - rc.left, e.clientY - rc.top]; }
function processMouse() {
  mouseDirty = false;
  if (!mouseIn || state.drag) return;
  const [sx, sy] = state.mouse, pk = state.pick = pick(sx, sy);
  if (state.mode === 'draw') { state.snap = pk ? computeSnap(pk) : null; buildPreview(); }
  else {
    state.hoverHandle = -1;
    if (state.sel) state.sel.pts.forEach((p, i) => { const [hx, hy] = project(p.x, p.y, p.z + 0.5); if (Math.hypot(hx - sx, hy - sy) < 11) state.hoverHandle = i; });
    let hov = null;
    if (pk) { hov = pk.road; if (!hov) { const on = nearestOnRoad(pk.x, pk.y, 12 * pk.wpp, null, pk.z); hov = on ? on.r : null; } }
    state.hover = hov;
  }
  dirty();
}
canvas.addEventListener('contextmenu', e => e.preventDefault());
canvas.addEventListener('pointerenter', () => { mouseIn = true; });
canvas.addEventListener('pointerleave', () => { mouseIn = false; });
canvas.addEventListener('pointerdown', e => {
  const [sx, sy] = mouseXY(e); state.mouse = [sx, sy]; mouseIn = true;
  if (e.button === 1) e.preventDefault();
  down = { x: sx, y: sy, b: e.button, moved: false };
  processMouse();
  if (e.button === 0 && state.mode === 'edit' && state.sel && state.hoverHandle >= 0) {
    const i = state.hoverHandle, p = state.sel.pts[i];
    state.drag = { r: state.sel, i, before: { ...p }, dzT: p.z - terrainH(p.x, p.y), planeY: p.z };
  }
});
addEventListener('pointermove', e => {
  const [sx, sy] = mouseXY(e); state.mouse = [sx, sy]; state.shift = e.shiftKey;
  if (down && Math.hypot(sx - down.x, sy - down.y) > 4) down.moved = true;
  if (state.drag && down) {
    ndc.set(sx / VW * 2 - 1, -(sy / VH) * 2 + 1); ray.setFromCamera(ndc, camera);
    const o = ray.ray.origin, d = ray.ray.direction;
    if (Math.abs(d.y) > 1e-5) {
      const t = (state.drag.planeY - o.y) / d.y;
      if (t > 0) {
        const x = clamp(o.x + d.x * t, 1, WORLD - 1), y = clamp(-(o.z + d.z * t), 1, WORLD - 1), dr = state.drag, prev = { ...dr.r.pts[dr.i] };
        dr.r.pts[dr.i] = { x, y, z: terrainH(x, y) + dr.dzT };
        if (!rebuild(dr.r)) { dr.r.pts[dr.i] = prev; rebuild(dr.r); }
        dirty();
      }
    }
    return;
  }
  mouseDirty = true;
});
addEventListener('pointerup', e => {
  if (!down) return;
  const wasClick = !down.moved, b = down.b; down = null;
  const [sx, sy] = mouseXY(e); state.mouse = [sx, sy];
  if (state.drag) {
    const d = state.drag; state.drag = null;
    const after = { ...d.r.pts[d.i] };
    d.r.pts[d.i] = d.before; rebuild(d.r);
    if (!wasClick) exec(EditPoint(d.r, d.i, d.before, after));
    dirty(); return;
  }
  if (!wasClick || e.target !== canvas) return;
  processMouse();
  if (state.mode === 'draw') {
    if (b === 0) startOrAddPoint();
    else if (b === 2) { if (state.draw && state.draw.pts.length >= 2) finishRoad(); else { state.draw = null; state.preview = null; } dirty(); }
  } else if (b === 0) { state.sel = state.hover; dirty(); }
});
canvas.addEventListener('dblclick', () => { if (state.mode === 'draw' && state.draw && state.draw.pts.length >= 2) finishRoad(); });
function adjustHeight(delta) {
  if (state.mode === 'draw') { state.dz += delta; buildPreview(); }
  else if (state.sel && state.hoverHandle >= 0) { const i = state.hoverHandle, p = state.sel.pts[i]; exec(EditPoint(state.sel, i, { ...p }, { ...p, z: p.z + delta })); }
  dirty();
}
function toggleProfile() { state.profile = !state.profile; resize(); }
addEventListener('keydown', e => {
  if (e.target.tagName === 'INPUT') return;
  state.shift = e.shiftKey;
  const k = e.key, kl = k.toLowerCase();
  if ((e.ctrlKey || e.metaKey) && kl === 'z') { e.preventDefault(); e.shiftKey ? redo() : undo(); return; }
  if ((e.ctrlKey || e.metaKey) && kl === 'y') { e.preventDefault(); redo(); return; }
  if (e.ctrlKey || e.metaKey || e.altKey) return;
  if (['w', 'a', 's', 'd', 'q', 'e', 'arrowup', 'arrowdown', 'arrowleft', 'arrowright'].includes(kl)) { keys.add(kl); if (kl.startsWith('arrow')) e.preventDefault(); needsRender = true; return; }
  if (k >= '1' && k <= '4') { state.type = +k; if (state.mode === 'draw') buildPreview(); }
  else if (kl === 'o') state.oneWay = !state.oneWay;
  else if (kl === 'c') state.colour = (state.colour + 1) % 3;
  else if (kl === 'p') toggleProfile();
  else if (kl === 'f') togglePerf();
  else if (kl === 'h' || k === '?') { state.help = !state.help; document.getElementById('help').style.display = state.help ? 'block' : 'none'; }
  else if (k === 'Tab') { e.preventDefault(); state.mode = state.mode === 'draw' ? 'edit' : 'draw'; state.draw = null; state.preview = null; state.snap = null; state.hoverHandle = -1; mouseDirty = true; }
  else if (k === 'Escape') { if (state.draw) { state.draw = null; state.preview = null; } else state.sel = null; }
  else if (k === 'Enter') { if (state.draw) finishRoad(); }
  else if (k === 'Backspace') { e.preventDefault(); if (state.draw) { state.draw.pts.pop(); state.draw.attach.pop(); if (!state.draw.pts.length) state.draw = null; buildPreview(); } }
  else if (k === 'Delete') { if (state.sel) { exec(DeleteRoad(state.sel)); state.sel = null; } }
  else if (k === 'PageUp' || k === ']') { e.preventDefault(); adjustHeight(e.shiftKey ? 5 : 1); }
  else if (k === 'PageDown' || k === '[') { e.preventDefault(); adjustHeight(e.shiftKey ? -5 : -1); }
  else if (k === 'Shift') mouseDirty = true;
  dirty();
});
addEventListener('keyup', e => { keys.delete(e.key.toLowerCase()); if (e.key === 'Shift') { state.shift = false; mouseDirty = true; dirty(); } });
addEventListener('blur', () => keys.clear());
addEventListener('resize', resize);

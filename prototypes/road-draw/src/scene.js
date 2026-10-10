// scene.js: renderer, camera, lights, sky, fog, shadow, shared state, view/camera control.
// ===== Scene =====
const canvas = document.getElementById('view'), profCv = document.getElementById('prof'), fc = profCv.getContext('2d');
const renderer = new THREE.WebGLRenderer({ canvas, antialias: true, powerPreference: 'high-performance' });
renderer.toneMapping = THREE.ACESFilmicToneMapping; renderer.toneMappingExposure = 0.92;
renderer.shadowMap.enabled = true; renderer.shadowMap.type = THREE.PCFShadowMap; renderer.shadowMap.autoUpdate = false;   // shadows re-rendered only when shadowDirty (see updateShadow)
const SKY = 0xb9d3e6;   // horizon / fog colour (hazy morning)
const scene = new THREE.Scene(); scene.background = new THREE.Color(SKY); scene.fog = new THREE.Fog(SKY, 1800, 7500);
// Sky dome: vertical gradient, follows the camera, ignores fog and depth.
const skyDome = new THREE.Mesh(new THREE.SphereGeometry(1, 32, 16), new THREE.ShaderMaterial({
  side: THREE.BackSide, depthWrite: false, fog: false,
  uniforms: { top: { value: new THREE.Color(0x3f7fc4) }, mid: { value: new THREE.Color(0x8fbbe0) }, hor: { value: new THREE.Color(SKY) }, glow: { value: new THREE.Color(0xffd9a8) }, sunDir: { value: new THREE.Vector3(-0.7, 0.5, 0.45).normalize() } },
  vertexShader: 'varying vec3 vD; void main(){ vD = normalize(position); gl_Position = projectionMatrix * modelViewMatrix * vec4(position,1.0); }',
  fragmentShader: 'uniform vec3 top,mid,hor,glow,sunDir; varying vec3 vD; void main(){ vec3 d=normalize(vD); float h=max(d.y,0.0); vec3 c=mix(hor,mid,smoothstep(0.0,0.18,h)); c=mix(c,top,smoothstep(0.15,0.8,h)); float g=pow(max(dot(d,sunDir),0.0),8.0); c+=glow*g*0.35*(1.0-h); if(d.y<0.0) c=hor; gl_FragColor=vec4(c,1.0);\n#include <tonemapping_fragment>\n#include <colorspace_fragment>\n}',
}));
skyDome.renderOrder = -10; skyDome.frustumCulled = false; scene.add(skyDome);
const camera = new THREE.PerspectiveCamera(50, 1, 1, 20000);
const hemi = new THREE.HemisphereLight(0xbcd8f5, 0x6b6a52, 0.95); scene.add(hemi);
const sun = new THREE.DirectionalLight(0xffe0b0, 3.4); scene.add(sun, sun.target);
sun.castShadow = true; sun.shadow.mapSize.set(2048, 2048); sun.shadow.bias = -0.0004;
const SUN_DIR = new THREE.Vector3(-0.7, 0.5, 0.45).normalize();
const w3 = (x, y, z) => [x, z, -y];

// ===== State =====
const state = { mode: 'draw', type: 1, oneWay: false, colour: 0, draw: null, dz: 0, snap: null, mouse: [0, 0], shift: false,
  hover: null, sel: null, hoverHandle: -1, drag: null, help: true, profile: true, flash: 0, preview: null, pick: null };
const COLOUR_MODES = ['plain', 'curvature', 'gradient'];
let needsUI = true, needsRender = true, overlayDirty = true, mouseDirty = false, mouseIn = false, shadowDirty = true;
function dirty() { needsUI = true; needsRender = true; overlayDirty = true; shadowDirty = true; }

// ===== View =====
let VW = 1, VH = 1, PROF_H = 190;
function resize() {
  PROF_H = state.profile ? Math.min(220, Math.round(innerHeight * 0.28)) : 0;
  VW = innerWidth; VH = Math.max(100, innerHeight - PROF_H);
  const dpr = Math.max(0.5, Math.min(window.devicePixelRatio || 1, 1.5, Math.sqrt(3.7e6 / (VW * VH))));   // cap 1.5 and ~1440p of pixels
  renderer.setPixelRatio(dpr); renderer.setSize(VW, VH); camera.aspect = VW / VH; camera.updateProjectionMatrix();
  profCv.style.display = state.profile ? 'block' : 'none';
  profCv.width = VW * dpr; profCv.height = PROF_H * dpr; profCv.style.width = VW + 'px'; profCv.style.height = PROF_H + 'px';
  fc.setTransform(dpr, 0, 0, dpr, 0, 0); profDpr = dpr;
  dirty();
}
let profDpr = 1;

const controls = new OrbitControls(camera, canvas);
controls.mouseButtons = { LEFT: null, MIDDLE: THREE.MOUSE.PAN, RIGHT: THREE.MOUSE.ROTATE };
controls.screenSpacePanning = false; controls.enableDamping = false;
controls.minDistance = 15; controls.maxDistance = 3800; controls.maxPolarAngle = 1.42; controls.minPolarAngle = 0.05; controls.zoomSpeed = 1.1;
controls.addEventListener('change', () => { needsRender = true; mouseDirty = true; overlayDirty = true; });
function setView(tx, ty, dist, az, polar) {
  const gx = clamp(tx, 0, WORLD), gy = clamp(ty, 0, WORLD), gz = hAt(gx, gy);
  controls.target.set(gx, gz, -gy);
  camera.position.set(gx + dist * Math.sin(polar) * Math.sin(az), gz + dist * Math.cos(polar), -gy + dist * Math.sin(polar) * Math.cos(az));
  controls.update(); dirty();
}
const keys = new Set();
function updateCamera(dt) {
  const tg = controls.target, ox = tg.x, oz = tg.z;
  let mx = 0, mz = 0, rot = 0;
  if (keys.has('w') || keys.has('arrowup')) mz += 1; if (keys.has('s') || keys.has('arrowdown')) mz -= 1;
  if (keys.has('d') || keys.has('arrowright')) mx += 1; if (keys.has('a') || keys.has('arrowleft')) mx -= 1;
  if (keys.has('q')) rot -= 1; if (keys.has('e')) rot += 1;
  const dist = camera.position.distanceTo(tg);
  if (mx || mz) {
    const fx = tg.x - camera.position.x, fz = tg.z - camera.position.z, fl = Math.hypot(fx, fz) || 1;
    const sp = dist * 0.9 * dt * (state.shift ? 3 : 1), dx = (fx / fl * mz + (-fz / fl) * mx) * sp, dz = (fz / fl * mz + (fx / fl) * mx) * sp;
    tg.x += dx; tg.z += dz; camera.position.x += dx; camera.position.z += dz;
  }
  if (rot) {
    const a = rot * 1.3 * dt, c = Math.cos(a), s = Math.sin(a), rx = camera.position.x - tg.x, rz = camera.position.z - tg.z;
    camera.position.x = tg.x + rx * c - rz * s; camera.position.z = tg.z + rx * s + rz * c;
  }
  tg.x = clamp(tg.x, 0, WORLD); tg.z = clamp(tg.z, -WORLD, 0);
  const gy = hAt(tg.x, -tg.z), dy = gy - tg.y;   // keep the target on the terrain surface, camera moves with it
  if (Math.abs(dy) > 1e-4) { tg.y = gy; camera.position.y += dy; }
  const moved = controls.update() || mx || mz || rot || ox !== tg.x || oz !== tg.z;
  const cg = hAt(camera.position.x, -camera.position.z) + 3;
  if (camera.position.y < cg) { camera.position.y = cg; controls.update(); }
  if (moved || Math.abs(dy) > 1e-4) { needsRender = true; mouseDirty = true; overlayDirty = true; }
  camera.near = Math.max(0.5, dist / 250); camera.far = Math.max(8000, dist * 6); camera.updateProjectionMatrix();
}
// Shadow frustum: one 2048 map fitted around the target. Refit (and re-render the map) only when the target leaves the fitted
// area, the zoom changes a lot, or geometry changed (dirty()); pure orbiting reuses the map.
let shCx = 0, shCy = 0, shCz = 0, shHalf = 0;
function updateShadow(force) {
  const tg = controls.target, dist = camera.position.distanceTo(tg), want = clamp(dist * 0.95, 80, 1500);
  if (force || shadowDirty || !shHalf || want > shHalf * 0.95 || want < shHalf * 0.5 || Math.hypot(tg.x - shCx, tg.z - shCz) > shHalf * 0.2) {
    shadowDirty = false; shHalf = Math.min(1600, want * 1.25); shCx = tg.x; shCy = tg.y; shCz = tg.z;
    const half = shHalf, ld = half * 2 + 500;
    sun.target.position.set(shCx, shCy, shCz); sun.position.set(shCx, shCy, shCz).addScaledVector(SUN_DIR, ld);
    sun.target.updateMatrixWorld();
    const c = sun.shadow.camera; c.left = -half; c.right = half; c.top = half; c.bottom = -half; c.near = 10; c.far = ld * 2 + 400; c.updateProjectionMatrix();
    sun.shadow.normalBias = half * 2 / 2048 * 1.5;
    renderer.shadowMap.needsUpdate = true; PF.shadowPasses++;
  }
  // Atmosphere: fog scales with camera height / distance; dome follows the camera.
  const hgt = Math.max(camera.position.y, 20), fn = Math.max(300, dist * 1.5 + hgt * 2);
  scene.fog.near = fn; scene.fog.far = fn * 4.5 + 3000;
  skyDome.position.copy(camera.position); skyDome.scale.setScalar(Math.max(camera.far * 0.9, 1000));
}
// ===== Frame stats / HUD readout (F) =====
const perfEl = document.createElement('div');
perfEl.style.cssText = 'position:absolute;right:12px;top:12px;display:none;background:rgba(13,17,23,0.82);color:#9fd;font:12px/1.4 ui-monospace,Consolas,monospace;padding:6px 9px;border-radius:6px;pointer-events:none;white-space:pre;z-index:5';
document.body.appendChild(perfEl);
const perfS = { on: false, n: 0, ms: 0, t0: 0 };
// Per-frame JS cost buckets (ms). cur = this frame, avg = rolling (EMA ~30 active frames), tot/frames = exact totals for tools/scene.mjs --check.
const PF = { names: ['pick', 'snap', 'preview', 'shadow', 'clip', 'render'], cur: {}, avg: {}, tot: {}, frames: 0, ticks: 0, shadowPasses: 0, gpu: '' };
for (const k of PF.names) { PF.cur[k] = 0; PF.avg[k] = 0; PF.tot[k] = 0; }
function pfFrameEnd(active) {
  PF.ticks++; for (const k of PF.names) PF.tot[k] += PF.cur[k];
  if (active) { PF.frames++; for (const k of PF.names) { PF.avg[k] += (PF.cur[k] - PF.avg[k]) * (PF.frames < 30 ? 1 / PF.frames : 1 / 30); } }
  for (const k of PF.names) PF.cur[k] = 0;
}
try {   // which GPU is the browser really using?
  const gl = renderer.getContext(), ext = gl.getExtension('WEBGL_debug_renderer_info');
  PF.gpu = (ext && gl.getParameter(ext.UNMASKED_RENDERER_WEBGL)) || gl.getParameter(gl.RENDERER) || 'unknown';
} catch (e) { PF.gpu = 'unknown'; }
const SOFT_GPU = /swiftshader|llvmpipe|microsoft basic|intel/i;
function togglePerf() { perfS.on = !perfS.on; perfEl.style.display = perfS.on ? 'block' : 'none'; needsRender = true; }
function drawScene(force) {
  const t = performance.now();
  scaleOverlay(); updateShadow(force); PF.cur.shadow += performance.now() - t;
  const tc = performance.now(); updateClipmap(); PF.cur.clip += performance.now() - tc;
  const tr = performance.now(); renderer.render(scene, camera); PF.cur.render += performance.now() - tr;
  if (perfS.on) {
    perfS.n++; perfS.ms += performance.now() - t;
    if (t - perfS.t0 > 500) {
      const inf = renderer.info.render, ft = perfS.t0 ? (t - perfS.t0) / perfS.n : 0;
      const a = PF.avg, f = k => a[k].toFixed(2), js = PF.names.reduce((q, k) => q + a[k], 0);
      perfEl.textContent = `${ft.toFixed(1)} ms/frame (${ft ? (1000 / ft).toFixed(0) : '-'} fps while active)\nGPU: ${PF.gpu}\n` + (SOFT_GPU.test(PF.gpu) ? 'WARNING Browser is not using your discrete GPU: see chrome://gpu, or set Windows Graphics settings → High performance for the browser\n' : '') +
        `JS ${js.toFixed(2)} ms/frame: pick ${f('pick')} snap ${f('snap')} preview ${f('preview')}\n   shadow ${f('shadow')} clip ${f('clip')} render ${f('render')}  (shadow maps drawn ${PF.shadowPasses})\ncalls ${inf.calls}  tris ${(inf.triangles / 1000).toFixed(0)}k\npx ${renderer.domElement.width}x${renderer.domElement.height}`;
      perfS.n = 0; perfS.ms = 0; perfS.t0 = t;
    }
  }
}
window.__perf = { step: now => tick(now), info: renderer.info, pf: PF };

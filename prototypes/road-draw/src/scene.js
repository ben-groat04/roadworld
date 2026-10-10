// scene.js: renderer, camera, lights, sky, fog, shadow, shared state, view/camera control.
// ===== Scene =====
const canvas = document.getElementById('view'), profCv = document.getElementById('prof'), fc = profCv.getContext('2d');
const renderer = new THREE.WebGLRenderer({ canvas, antialias: true, powerPreference: 'high-performance' });
renderer.toneMapping = THREE.ACESFilmicToneMapping; renderer.toneMappingExposure = 0.92;
renderer.shadowMap.enabled = true; renderer.shadowMap.type = THREE.PCFSoftShadowMap;
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
let needsUI = true, needsRender = true, overlayDirty = true, mouseDirty = false, mouseIn = false;
function dirty() { needsUI = true; needsRender = true; overlayDirty = true; }

// ===== View =====
let VW = 1, VH = 1, PROF_H = 190;
function resize() {
  PROF_H = state.profile ? Math.min(220, Math.round(innerHeight * 0.28)) : 0;
  VW = innerWidth; VH = Math.max(100, innerHeight - PROF_H);
  const dpr = Math.min(window.devicePixelRatio || 1, 2);
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
function updateShadow() {
  const tg = controls.target, dist = camera.position.distanceTo(tg), half = clamp(dist * 0.95, 80, 1500), ld = half * 2 + 500;
  sun.target.position.copy(tg); sun.position.copy(tg).addScaledVector(SUN_DIR, ld);
  const c = sun.shadow.camera; c.left = -half; c.right = half; c.top = half; c.bottom = -half; c.near = 10; c.far = ld * 2 + 400; c.updateProjectionMatrix();
  sun.shadow.normalBias = half * 2 / 2048 * 1.5;
  // Atmosphere: fog scales with camera height / distance; dome follows the camera.
  const hgt = Math.max(camera.position.y, 20), fn = Math.max(300, dist * 1.5 + hgt * 2);
  scene.fog.near = fn; scene.fog.far = fn * 4.5 + 3000;
  skyDome.position.copy(camera.position); skyDome.scale.setScalar(Math.max(camera.far * 0.9, 1000));
}

import * as THREE from 'three';
import { OrbitControls } from 'three/addons/controls/OrbitControls.js';

// ---- constants (legacy-compatible) ------------------------------------
const FLY_MS = 3000;                 // legacy flight time
const FIELD = 200;                   // legacy: (v - 0.5) * 100 * 2
const BOARD_Z = 110;
const REWIND_MS = 5 * 60 * 1000;     // rolling buffer: 5 minutes (spec 2026-06-10)
const MAX_EVENTS = 2_000_000;        // hard cap (legacy MaxPacketNum spirit)
const MAX_VISIBLE = 20000;           // instanced draw capacity

// flag colors lifted from legacy packter0-9.png (1x1 swatches)
const FLAG_COLORS = [
  0xffa3b1, 0x0000ff, 0xff0000, 0x8000ff, 0x008000,
  0x00ffff, 0xffff00, 0xffffff, 0x00ff00, 0xff8040,
].map(c => new THREE.Color(c));

// ---- scene -------------------------------------------------------------
const canvas = document.getElementById('view');
const renderer = new THREE.WebGLRenderer({ canvas, antialias: true });
renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
const scene = new THREE.Scene();
const camera = new THREE.PerspectiveCamera(55, 1, 0.1, 4000);
camera.position.set(260, 120, 0);
const controls = new OrbitControls(camera, canvas);
controls.enableDamping = true;
controls.maxDistance = 800;

scene.add(new THREE.AmbientLight(0xffffff, 0.9));
const dir = new THREE.DirectionalLight(0xffffff, 1.2);
dir.position.set(200, 300, 100);
scene.add(dir);

const texLoader = new THREE.TextureLoader();

// skydome: legacy skydometexture (2048x1024) on an inward-facing sphere
texLoader.load('assets/skydome.png', tex => {
  tex.colorSpace = THREE.SRGBColorSpace;
  const dome = new THREE.Mesh(
    new THREE.SphereGeometry(1500, 48, 32),
    new THREE.MeshBasicMaterial({ map: tex, side: THREE.BackSide }),
  );
  scene.add(dome);
});

// sender / receiver boards facing each other across the field
function addBoard(file, z, rotY) {
  texLoader.load(file, tex => {
    tex.colorSpace = THREE.SRGBColorSpace;
    const board = new THREE.Mesh(
      new THREE.PlaneGeometry(FIELD * 1.15, FIELD * 1.15),
      new THREE.MeshBasicMaterial({ map: tex, transparent: true, opacity: 0.9, side: THREE.DoubleSide }),
    );
    board.position.z = z;
    board.rotation.y = rotY;
    scene.add(board);
  });
}
addBoard('assets/packter_sender.png', -BOARD_Z, 0);
addBoard('assets/packter_receiver.png', BOARD_Z, Math.PI);

// flying objects: one instanced mesh of small spheres, per-instance color
const packetGeo = new THREE.SphereGeometry(2.2, 10, 8);
const packetMat = new THREE.MeshBasicMaterial();
const packets = new THREE.InstancedMesh(packetGeo, packetMat, MAX_VISIBLE);
packets.instanceMatrix.setUsage(THREE.DynamicDrawUsage);
packets.count = 0;
scene.add(packets);

// ---- event ring buffer (rewind spec: keep last 5 minutes) ---------------
// columns-of-arrays for memory compactness
const ring = {
  t: new Float64Array(0), sx: 0, // initialized below
};
const ev = {
  t: [], sx: [], sy: [], dx: [], dy: [], flag: [],
};
let liveStart = 0;          // index of first event still inside fly window (live mode)
let received = 0;           // total events received (stats)
let ppsWindow = [];         // [time, count] samples for pps display

function pushEvent(t, sx, sy, dx, dy, flag) {
  ev.t.push(t); ev.sx.push(sx); ev.sy.push(sy); ev.dx.push(dx); ev.dy.push(dy); ev.flag.push(flag);
  received++;
}

function pruneRing(now) {
  const cutoff = now - REWIND_MS;
  let drop = 0;
  while (drop < ev.t.length && ev.t[drop] < cutoff) drop++;
  const over = ev.t.length - drop > MAX_EVENTS ? ev.t.length - MAX_EVENTS - drop : 0;
  drop += over;
  if (drop > 0) {
    for (const k of Object.keys(ev)) ev[k].splice(0, drop);
    liveStart = Math.max(0, liveStart - drop);
  }
}

function lowerBound(arr, value) {
  let lo = 0, hi = arr.length;
  while (lo < hi) {
    const mid = (lo + hi) >> 1;
    if (arr[mid] < value) lo = mid + 1; else hi = mid;
  }
  return lo;
}

// ---- time control (pause / scrub / live) --------------------------------
let mode = 'live';          // 'live' | 'paused'
let pausedAt = 0;           // viewTime while paused/scrubbing
const seek = document.getElementById('seek');
const timeLabel = document.getElementById('timeLabel');
const btnPause = document.getElementById('btnPause');
const btnLive = document.getElementById('btnLive');

function viewTime(now) { return mode === 'live' ? now : pausedAt; }
function pause(now) { mode = 'paused'; pausedAt = now; btnPause.classList.add('active'); }
function goLive() { mode = 'live'; btnPause.classList.remove('active'); seek.value = 1000; }

btnPause.onclick = () => mode === 'live' ? pause(performance.now()) : goLive();
btnLive.onclick = goLive;
seek.oninput = () => {
  const now = performance.now();
  const start = ev.t.length ? Math.max(ev.t[0], now - REWIND_MS) : now - REWIND_MS;
  if (mode === 'live') pause(now);
  pausedAt = start + (now - start) * (Number(seek.value) / 1000);
};
window.addEventListener('keydown', e => {
  const now = performance.now();
  if (e.code === 'KeyS') { mode === 'live' ? pause(now) : goLive(); }
  if (e.code === 'KeyC') goLive();
  if (e.code === 'KeyB') { if (mode === 'live') pause(now); pausedAt -= e.shiftKey ? FLY_MS / 3 : FLY_MS / 30; }
  if (e.code === 'KeyF') { if (mode === 'live') pause(now); pausedAt += e.shiftKey ? FLY_MS / 3 : FLY_MS / 30; }
  if (e.code === 'Backspace') { if (mode === 'live') pause(now); pausedAt -= 5 * 60 * 1000; e.preventDefault(); }
});

// ---- websocket ----------------------------------------------------------
const hud = document.getElementById('hud');
let wsState = 'connecting';
function connect() {
  const ws = new WebSocket(`ws://${location.host}/ws`);
  ws.binaryType = 'arraybuffer';
  ws.onopen = () => { wsState = 'connected'; };
  ws.onclose = () => { wsState = 'reconnecting'; setTimeout(connect, 1500); };
  ws.onmessage = e => {
    const dv = new DataView(e.data);
    if (dv.getUint8(0) !== 1) return;
    const count = dv.getUint32(4, true);
    const now = performance.now();
    let off = 8;
    for (let i = 0; i < count; i++, off += 20) {
      pushEvent(now,
        dv.getFloat32(off, true), dv.getFloat32(off + 4, true),
        dv.getFloat32(off + 8, true), dv.getFloat32(off + 12, true),
        dv.getUint32(off + 16, true));
    }
    ppsWindow.push([now, count]);
  };
}
connect();

// ---- render loop ---------------------------------------------------------
const m4 = new THREE.Matrix4();
const fieldCoord = v => (v - 0.5) * FIELD;

function updatePackets(vt) {
  // candidate window: events with t in (vt - FLY_MS, vt]
  let i;
  if (mode === 'live') {
    while (liveStart < ev.t.length && ev.t[liveStart] + FLY_MS < vt) liveStart++;
    i = liveStart;
  } else {
    i = lowerBound(ev.t, vt - FLY_MS);
  }
  let n = 0;
  for (; i < ev.t.length && n < MAX_VISIBLE; i++) {
    const t0 = ev.t[i];
    if (t0 > vt) break;
    const f = (vt - t0) / FLY_MS;
    const x0 = fieldCoord(ev.sx[i]), y0 = fieldCoord(ev.sy[i]);
    const x1 = fieldCoord(ev.dx[i]), y1 = fieldCoord(ev.dy[i]);
    m4.makeTranslation(
      x0 + (x1 - x0) * f,
      y0 + (y1 - y0) * f,
      -BOARD_Z + (2 * BOARD_Z) * f,
    );
    packets.setMatrixAt(n, m4);
    packets.setColorAt(n, FLAG_COLORS[(ev.flag[i] >>> 0) % 10]);
    n++;
  }
  packets.count = n;
  packets.instanceMatrix.needsUpdate = true;
  if (packets.instanceColor) packets.instanceColor.needsUpdate = true;
  return n;
}

let lastPrune = 0, lastHud = 0;
function frame(nowDom) {
  requestAnimationFrame(frame);
  const now = performance.now();
  const vt = viewTime(now);
  const visible = updatePackets(vt);

  if (now - lastPrune > 1000) { lastPrune = now; pruneRing(now); }
  if (now - lastHud > 250) {
    lastHud = now;
    while (ppsWindow.length && ppsWindow[0][0] < now - 1000) ppsWindow.shift();
    const pps = ppsWindow.reduce((a, s) => a + s[1], 0);
    const span = ev.t.length ? ((ev.t[ev.t.length - 1] - ev.t[0]) / 1000).toFixed(0) : 0;
    hud.innerHTML =
      `PACKTER 3.0 alpha — ${wsState}<br>` +
      `events/s: ${pps} | visible: ${visible} | buffered: ${ev.t.length.toLocaleString()} (${span}s / 300s)<br>` +
      `mode: ${mode === 'live' ? 'LIVE' : 'REWIND'} | keys: S=stop C=live B/F=step Backspace=-5min`;
    if (mode === 'paused') {
      const start = ev.t.length ? Math.max(ev.t[0], now - REWIND_MS) : now - REWIND_MS;
      seek.value = Math.round(1000 * (pausedAt - start) / Math.max(1, now - start));
      timeLabel.textContent = `-${((now - pausedAt) / 1000).toFixed(1)}s`;
    } else {
      timeLabel.textContent = 'live';
    }
  }

  const w = canvas.clientWidth, h = canvas.clientHeight;
  if (canvas.width !== w || canvas.height !== h) {
    renderer.setSize(w, h, false);
    camera.aspect = w / h;
    camera.updateProjectionMatrix();
  }
  controls.update();
  renderer.render(scene, camera);
}
requestAnimationFrame(frame);

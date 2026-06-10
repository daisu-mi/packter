import * as THREE from 'three';
import { OrbitControls } from 'three/addons/controls/OrbitControls.js';
import { VRButton } from 'three/addons/webxr/VRButton.js';
import { GLTFLoader } from 'three/addons/loaders/GLTFLoader.js';

// ---- configuration (web/config.json, all keys optional) -----------------
const DEFAULTS = {
  size: 1.0,
  flyMs: 3000,
  rewindMs: 5 * 60 * 1000,
  toastMs: 10000,
  maxSe: 10,
  skydome: 'assets/skydome.png',
  flagColors: null,
  axis: { xStart: 0.0, xEnd: 1.0, yStart: 0.0, yEnd: 1.0 },
  boards: {
    sender: { texture: 'assets/packter_sender.png', scale: 1.0 },
    receiver: { texture: 'assets/packter_receiver.png', scale: 1.0 },
    gateway: { texture: 'assets/packter_gateway.png', scale: 1.0, enabled: true },
  },
  ball: 'assets/ball.json',
  terrain: null,
};

let cfg = DEFAULTS;
try {
  const r = await fetch('config.json');
  if (r.ok) {
    const user = await r.json();
    cfg = {
      ...DEFAULTS, ...user,
      axis: { ...DEFAULTS.axis, ...(user.axis || {}) },
      boards: {
        sender: { ...DEFAULTS.boards.sender, ...(user.boards?.sender || {}) },
        receiver: { ...DEFAULTS.boards.receiver, ...(user.boards?.receiver || {}) },
        gateway: { ...DEFAULTS.boards.gateway, ...(user.boards?.gateway || {}) },
      },
    };
  }
} catch { /* defaults */ }

const FLY_MS = cfg.flyMs;
const FIELD = 200;                   // legacy: (v - 0.5) * 100 * 2
const BOARD_Z = 110;
const ARC_HEIGHT = 60;
const REWIND_MS = cfg.rewindMs;
const MAX_EVENTS = 2_000_000;
const MAX_VISIBLE = 20000;

const KIND_LAY = 0, KIND_BALLISTIC = 1, KIND_GATEWAY = 2;
const GATEWAY_POS = new THREE.Vector3(0, FIELD * 0.55, 0);

// flag colors from legacy packter0-9.png swatches (config-overridable)
const FLAG_HEX = cfg.flagColors || [
  '#ffa3b1', '#0000ff', '#ff0000', '#8000ff', '#008000',
  '#00ffff', '#ffff00', '#ffffff', '#00ff00', '#ff8040',
];
const FLAG_COLORS = FLAG_HEX.map(c => new THREE.Color(c));

// legacy xaxisstart/end etc: remap the normalized coordinate to a sub-range
// (values accept 0-1 floats or dotted IPv4 strings)
function axisVal(v) {
  if (typeof v === 'string' && v.includes('.') && !/^[\d.]+$/.test(v) === false) {
    const parts = v.split('.').map(Number);
    if (parts.length === 4 && parts.every(p => p >= 0 && p <= 255)) {
      return ((parts[0] * 16777216 + parts[1] * 65536 + parts[2] * 256 + parts[3]) >>> 0) / 4294967295;
    }
  }
  return Number(v) || 0;
}
const AX = {
  x0: axisVal(cfg.axis.xStart), x1: axisVal(cfg.axis.xEnd),
  y0: axisVal(cfg.axis.yStart), y1: axisVal(cfg.axis.yEnd),
};
const revX = v => AX.x1 !== AX.x0 ? Math.min(1, Math.max(0, (v - AX.x0) / (AX.x1 - AX.x0))) : v;
const revY = v => AX.y1 !== AX.y0 ? Math.min(1, Math.max(0, (v - AX.y0) / (AX.y1 - AX.y0))) : v;

// ---- scene -------------------------------------------------------------
const canvas = document.getElementById('view');
const renderer = new THREE.WebGLRenderer({ canvas, antialias: true });
renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
renderer.xr.enabled = true;
const vrButton = VRButton.createButton(renderer);
vrButton.style.bottom = '64px';
document.body.appendChild(vrButton);

const scene = new THREE.Scene();
const camera = new THREE.PerspectiveCamera(55, 1, 0.1, 4000);
camera.position.set(260, 120, 0);
const controls = new OrbitControls(camera, canvas);
controls.enableDamping = true;
controls.maxDistance = 800;

scene.add(new THREE.AmbientLight(0xffffff, 0.45));
scene.add(new THREE.HemisphereLight(0x9db8d8, 0x3a3a3a, 0.5));
const dir = new THREE.DirectionalLight(0xffffff, 1.4);
dir.position.set(200, 300, 100);
scene.add(dir);

const texLoader = new THREE.TextureLoader();

let dome = null;
function setSkydome(url) {
  texLoader.load(url, tex => {
    tex.colorSpace = THREE.SRGBColorSpace;
    if (dome === null) {
      dome = new THREE.Mesh(
        new THREE.SphereGeometry(1500, 48, 32),
        new THREE.MeshBasicMaterial({ map: tex, side: THREE.BackSide }),
      );
      scene.add(dome);
    } else {
      dome.material.map = tex;
      dome.material.needsUpdate = true;
    }
  });
}
setSkydome(cfg.skydome);

function addBoard(conf, pos, rotY, size) {
  texLoader.load(conf.texture, tex => {
    tex.colorSpace = THREE.SRGBColorSpace;
    const board = new THREE.Mesh(
      new THREE.PlaneGeometry(size * conf.scale, size * conf.scale),
      new THREE.MeshBasicMaterial({ map: tex, transparent: true, opacity: 0.9, side: THREE.DoubleSide }),
    );
    board.position.copy(pos);
    board.rotation.y = rotY;
    scene.add(board);
  });
}
addBoard(cfg.boards.sender, new THREE.Vector3(0, 0, -BOARD_Z), 0, FIELD * 1.15);
addBoard(cfg.boards.receiver, new THREE.Vector3(0, 0, BOARD_Z), Math.PI, FIELD * 1.15);
if (cfg.boards.gateway.enabled) {
  addBoard(cfg.boards.gateway, GATEWAY_POS.clone().add(new THREE.Vector3(0, 20, 0)), Math.PI / 2, 40);
}

// optional terrain (ballistic map successor): glTF via config
if (cfg.terrain && cfg.terrain.url) {
  new GLTFLoader().load(cfg.terrain.url, g => {
    g.scene.scale.setScalar(cfg.terrain.scale || 1);
    scene.add(g.scene);
  });
}

// flying objects: the actual legacy ball.x mesh (converted to JSON),
// lit material so the balls read as spheres; fallback = shaded sphere
const BALL_SCALE = 4.5 * cfg.size;   // ball.x radius 0.49 -> legacy size 2.2
const packetMat = new THREE.MeshStandardMaterial({ roughness: 0.35, metalness: 0.05 });
let packets = new THREE.InstancedMesh(
  new THREE.SphereGeometry(2.2 * cfg.size, 16, 12), packetMat, MAX_VISIBLE);
packets.instanceMatrix.setUsage(THREE.DynamicDrawUsage);
packets.count = 0;
scene.add(packets);

fetch(cfg.ball).then(r => r.ok ? r.json() : null).then(d => {
  if (!d) return;
  const geo = new THREE.BufferGeometry();
  geo.setAttribute('position', new THREE.Float32BufferAttribute(d.positions, 3));
  geo.setIndex(d.indices);
  if (d.normals) {
    geo.setAttribute('normal', new THREE.Float32BufferAttribute(d.normals, 3));
  } else {
    geo.computeVertexNormals();
  }
  geo.scale(BALL_SCALE, BALL_SCALE, BALL_SCALE);
  packets.geometry.dispose();
  packets.geometry = geo;
}).catch(() => {});

// ---- event ring buffer (rewind spec: keep last 5 minutes) ---------------
const ev = { t: [], sx: [], sy: [], dx: [], dy: [], flag: [], kind: [], desc: [] };
let liveStart = 0;
let ppsWindow = [];

function pushEvent(t, sx, sy, dx, dy, flag, kind, desc) {
  ev.t.push(t); ev.sx.push(sx); ev.sy.push(sy); ev.dx.push(dx); ev.dy.push(dy);
  ev.flag.push(flag); ev.kind.push(kind); ev.desc.push(desc);
}

function clearEvents() {
  for (const k of Object.keys(ev)) ev[k].length = 0;
  liveStart = 0;
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

// ---- time control --------------------------------------------------------
let mode = 'live';
let pausedAt = 0;
const seek = document.getElementById('seek');
const timeLabel = document.getElementById('timeLabel');
const btnPause = document.getElementById('btnPause');
const btnLive = document.getElementById('btnLive');
const hud = document.getElementById('hud');
const timebar = document.getElementById('timebar');
let hudVisible = true;

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
  if (e.code === 'Space') {           // legacy: stats display toggle
    hudVisible = !hudVisible;
    hud.style.display = hudVisible ? 'block' : 'none';
    timebar.style.display = hudVisible ? 'flex' : 'none';
    e.preventDefault();
  }
  if (e.code === 'Enter' && e.altKey) { // legacy: Alt+Enter fullscreen
    if (document.fullscreenElement) document.exitFullscreen();
    else document.body.requestFullscreen();
  }
  keysDown.add(e.code);
  if (keysDown.has('KeyV') && (keysDown.has('Digit4') || keysDown.has('Numpad4'))) {
    v4logo.style.display = v4logo.style.display === 'none' ? 'block' : 'none';
  }
});
window.addEventListener('keyup', e => keysDown.delete(e.code));

// legacy easter egg: V+4 shows the V4 logo (compiled asset from 2.4)
const keysDown = new Set();
const v4logo = document.createElement('img');
v4logo.src = 'assets/compiled/v4.png';
v4logo.style.cssText =
  'position:fixed;bottom:64px;left:14px;width:128px;display:none;pointer-events:none;';
document.body.appendChild(v4logo);

// ---- audio (PACKTERSE / PACKTERSOUND) ------------------------------------
let audioCtx = null;
const audioCache = new Map();
let activeSE = 0;
let bgmSource = null;

function ensureAudio() {
  if (audioCtx === null) {
    audioCtx = new (window.AudioContext || window.webkitAudioContext)();
  }
  if (audioCtx.state === 'suspended') audioCtx.resume();
}
window.addEventListener('pointerdown', ensureAudio, { once: true });

async function loadAudio(file) {
  if (audioCache.has(file)) return audioCache.get(file);
  const p = fetch(`assets/legacy/${file}`)
    .then(r => { if (!r.ok) throw new Error(file); return r.arrayBuffer(); })
    .then(b => audioCtx.decodeAudioData(b))
    .catch(() => null);
  audioCache.set(file, p);
  return p;
}

async function playSE(file) {
  if (audioCtx === null || activeSE >= cfg.maxSe) return;
  const buf = await loadAudio(file);
  if (!buf) return;
  const src = audioCtx.createBufferSource();
  src.buffer = buf;
  src.connect(audioCtx.destination);
  activeSE++;
  src.onended = () => activeSE--;
  src.start();
}

async function playBGM(time, file) {
  if (bgmSource) { try { bgmSource.stop(); } catch {} bgmSource = null; }
  const t = parseFloat(time) || 0;
  if (t === 0 || audioCtx === null) return;
  const buf = await loadAudio(file);
  if (!buf) return;
  const src = audioCtx.createBufferSource();
  src.buffer = buf;
  src.loop = true;
  src.connect(audioCtx.destination);
  src.start();
  bgmSource = src;
  if (t > 0) setTimeout(() => { if (bgmSource === src) { try { src.stop(); } catch {} bgmSource = null; } }, t * 1000);
}

// ---- toast (PACKTERMSG / PACKTERHTML) -------------------------------------
// htmlconvert successor: explicit template variables (裁定2026-06-10)
function expandTemplate(html) {
  return html
    .replaceAll('{{ASSET_URL}}', new URL('assets/legacy/', location.href).href)
    .replaceAll('{{VIEW_WIDTH}}', String(canvas.clientWidth))
    .replaceAll('{{VIEW_HEIGHT}}', String(canvas.clientHeight));
}

function showToast(pic, html) {
  const card = document.createElement('div');
  card.className = 'toastcard';
  if (pic) {
    const img = document.createElement('img');
    img.src = `assets/legacy/${pic}`;
    img.onerror = () => img.remove();
    card.appendChild(img);
  }
  const frame = document.createElement('iframe');
  frame.setAttribute('sandbox', '');   // scripts disabled by default (裁定2026-06-10)
  frame.srcdoc = expandTemplate(html);
  card.appendChild(frame);
  document.getElementById('toast').appendChild(card);
  setTimeout(() => card.remove(), cfg.toastMs);
}

function speak(text) {
  const cleaned = text.replace(/\/[A-Za-z]+:\S*/g, ' ').trim();
  if (!cleaned || !window.speechSynthesis) return;
  const u = new SpeechSynthesisUtterance(cleaned);
  u.lang = 'ja-JP';
  speechSynthesis.speak(u);
}

function handleControl(c) {
  switch (c.t) {
    case 'msg': showToast(c.pic, c.html); break;
    case 'html': showToast(null, c.html); break;
    case 'se': ensureAudio(); playSE(c.file); break;
    case 'sound': ensureAudio(); playBGM(c.time, c.file); break;
    case 'voice': speak(c.text); break;
    case 'skydome': setSkydome(`assets/legacy/${c.file}`); break;
  }
}

// ---- websocket ----------------------------------------------------------
const textDecoder = new TextDecoder();
let wsState = 'connecting';

function connect() {
  const ws = new WebSocket(`ws://${location.host}/ws`);
  ws.binaryType = 'arraybuffer';
  ws.onopen = () => { wsState = 'connected'; clearEvents(); };
  ws.onclose = () => { wsState = 'reconnecting'; setTimeout(connect, 1500); };
  ws.onmessage = e => {
    if (typeof e.data === 'string') {
      try { handleControl(JSON.parse(e.data)); } catch {}
      return;
    }
    const dv = new DataView(e.data);
    if (dv.getUint8(0) !== 2 || dv.getUint8(1) !== 1) return;
    const count = dv.getUint32(4, true);
    const now = performance.now();
    let off = 8, live = 0;
    for (let i = 0; i < count; i++) {
      const age = dv.getInt32(off, true);
      const sx = dv.getFloat32(off + 4, true);
      const sy = dv.getFloat32(off + 8, true);
      const dx = dv.getFloat32(off + 12, true);
      const dy = dv.getFloat32(off + 16, true);
      const flag = dv.getUint16(off + 20, true);
      const kind = dv.getUint8(off + 22);
      const dlen = dv.getUint8(off + 23);
      const desc = dlen > 0
        ? textDecoder.decode(new Uint8Array(e.data, off + 24, dlen)) : '';
      off += 24 + dlen;
      pushEvent(now - age, revX(sx), revY(sy), revX(dx), revY(dy), flag, kind, desc);
      if (age < 1000) live++;
    }
    if (live > 0) ppsWindow.push([now, live]);
  };
}
connect();

// ---- packet position by trajectory kind -----------------------------------
const fieldCoord = v => (v - 0.5) * FIELD;
const tmpVec = new THREE.Vector3();

function packetPosition(i, f, out) {
  const x0 = fieldCoord(ev.sx[i]), y0 = fieldCoord(ev.sy[i]);
  const x1 = fieldCoord(ev.dx[i]), y1 = fieldCoord(ev.dy[i]);
  switch (ev.kind[i]) {
    case KIND_BALLISTIC:
      out.set(
        x0 + (x1 - x0) * f,
        y0 + (y1 - y0) * f + Math.sin(Math.PI * f) * ARC_HEIGHT,
        -BOARD_Z + 2 * BOARD_Z * f,
      );
      break;
    case KIND_GATEWAY:
      if (f < 0.5) {
        const g = f * 2;
        out.set(
          x0 + (GATEWAY_POS.x - x0) * g,
          y0 + (GATEWAY_POS.y - y0) * g,
          -BOARD_Z + (GATEWAY_POS.z + BOARD_Z) * g,
        );
      } else {
        const g = (f - 0.5) * 2;
        out.set(
          GATEWAY_POS.x + (x1 - GATEWAY_POS.x) * g,
          GATEWAY_POS.y + (y1 - GATEWAY_POS.y) * g,
          GATEWAY_POS.z + (BOARD_Z - GATEWAY_POS.z) * g,
        );
      }
      break;
    default:
      out.set(
        x0 + (x1 - x0) * f,
        y0 + (y1 - y0) * f,
        -BOARD_Z + 2 * BOARD_Z * f,
      );
  }
}

// ---- selection -------------------------------------------------------------
const raycaster = new THREE.Raycaster();
const pointer = new THREE.Vector2();
const selinfo = document.getElementById('selinfo');
let visMap = [];

canvas.addEventListener('click', e => {
  pointer.x = (e.clientX / canvas.clientWidth) * 2 - 1;
  pointer.y = -(e.clientY / canvas.clientHeight) * 2 + 1;
  raycaster.setFromCamera(pointer, camera);
  const hits = raycaster.intersectObject(packets);
  if (hits.length > 0 && hits[0].instanceId !== undefined && visMap[hits[0].instanceId] !== undefined) {
    const sel = visMap[hits[0].instanceId];
    const kindName = ['lay', 'ballistic', 'gateway'][ev.kind[sel]] || 'lay';
    selinfo.style.display = 'block';
    selinfo.textContent =
      `flag:${ev.flag[sel]} (${kindName})\n${ev.desc[sel] || '(no description)'}`;
  } else {
    selinfo.style.display = 'none';
  }
});

// ---- render loop ---------------------------------------------------------
const m4 = new THREE.Matrix4();
const flagVisible = new Array(10).fill(0);

function updatePackets(vt) {
  let i;
  if (mode === 'live') {
    while (liveStart < ev.t.length && ev.t[liveStart] + FLY_MS < vt) liveStart++;
    i = liveStart;
  } else {
    i = lowerBound(ev.t, vt - FLY_MS);
  }
  let n = 0;
  flagVisible.fill(0);
  for (; i < ev.t.length && n < MAX_VISIBLE; i++) {
    const t0 = ev.t[i];
    if (t0 > vt) break;
    const f = (vt - t0) / FLY_MS;
    packetPosition(i, f, tmpVec);
    m4.makeTranslation(tmpVec.x, tmpVec.y, tmpVec.z);
    packets.setMatrixAt(n, m4);
    packets.setColorAt(n, FLAG_COLORS[ev.flag[i] % 10]);
    flagVisible[ev.flag[i] % 10]++;
    visMap[n] = i;
    n++;
  }
  packets.count = n;
  visMap.length = n;
  packets.instanceMatrix.needsUpdate = true;
  if (packets.instanceColor) packets.instanceColor.needsUpdate = true;
  return n;
}

function flagStatsHtml() {
  let out = '';
  for (let f = 0; f < 10; f++) {
    if (flagVisible[f] === 0) continue;
    out += ` <span style="color:${FLAG_HEX[f]}">●</span>${flagVisible[f]}`;
  }
  return out || ' -';
}

let lastPrune = 0, lastHud = 0;
function frame() {
  const now = performance.now();
  const vt = viewTime(now);
  const visible = updatePackets(vt);

  if (now - lastPrune > 1000) { lastPrune = now; pruneRing(now); }
  if (hudVisible && now - lastHud > 250) {
    lastHud = now;
    while (ppsWindow.length && ppsWindow[0][0] < now - 1000) ppsWindow.shift();
    const pps = ppsWindow.reduce((a, s) => a + s[1], 0);
    const span = ev.t.length ? ((ev.t[ev.t.length - 1] - ev.t[0]) / 1000).toFixed(0) : 0;
    hud.innerHTML =
      `PACKTER 3.0 alpha — ${wsState}<br>` +
      `events/s: ${pps} | visible: ${visible} | buffered: ${ev.t.length.toLocaleString()} (${span}s / ${REWIND_MS / 1000}s)<br>` +
      `flags:${flagStatsHtml()}<br>` +
      `mode: ${mode === 'live' ? 'LIVE' : 'REWIND'} | S=stop C=live B/F=step Bksp=-5min Space=HUD Alt+Enter=fullscreen | click=select`;
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
renderer.setAnimationLoop(frame);   // rAF + WebXR both

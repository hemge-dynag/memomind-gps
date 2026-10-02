import { createGMPlugin } from './vendor/gm-plugin-web-sdk.esm.js';
import {
  DOWNLINK_CHANNEL,
  UPLINK_CHANNEL,
  EVENT_BUTTON,
  FLAG_RUNNING,
  FLAG_PAUSED,
  FLAG_GPS_OK,
  encodeMetrics,
  encodeReset,
  decodeEvent,
} from './protocol.js';

const STORAGE_KEY = 'gps';

const gm = createGMPlugin();

const statusEl = document.querySelector('#status');
const distanceEl = document.querySelector('#distance');
const durationEl = document.querySelector('#duration');
const paceEl = document.querySelector('#pace');
const speedEl = document.querySelector('#speed');
const startButton = document.querySelector('#start-button');
const statsEls = {
  todayDistance: document.querySelector('#today-distance'),
  weekDistance: document.querySelector('#week-distance'),
  weekSessions: document.querySelector('#week-sessions'),
};
const lastSessionEl = document.querySelector('#last-session');

// Metres per degree of latitude; longitude is scaled by cos(latitude).
const METRES_PER_DEGREE = 111320;
const SEND_INTERVAL_MS = 1000;
// Ignore impossible jumps (teleports / noisy fixes) beyond this speed.
const MAX_PLAUSIBLE_SPEED_MS = 15;

let connected = false;
let running = false;
let paused = false;

let watchId = null;
let offPosition = null;
let offError = null;

let lastPoint = null; // { latitude, longitude, timestamp }
let coursePoint = null; // older point used for bearing/speed
let distanceM = 0;
let startedAt = null;
let accumulatedMs = 0; // time accrued before the current run segment
let sessionStart = null;
let gpsOk = false;
let sendTimer = null;
let history = {}; // { 'YYYY-MM-DD': { sessions, distanceM, durationSec } }

function setStatus(text, state = '') {
  statusEl.textContent = text;
  statusEl.className = `status ${state}`.trim();
}

function todayKey() {
  const now = new Date();
  const month = String(now.getMonth() + 1).padStart(2, '0');
  const day = String(now.getDate()).padStart(2, '0');
  return `${now.getFullYear()}-${month}-${day}`;
}

function formatDistance(metres) {
  if (metres < 1000) return `${Math.round(metres)} m`;
  return `${(metres / 1000).toFixed(2)} km`;
}

function formatDuration(seconds) {
  const total = Math.max(0, Math.floor(seconds));
  const hours = Math.floor(total / 3600);
  const minutes = Math.floor((total % 3600) / 60);
  const secs = total % 60;
  const mm = String(minutes).padStart(2, '0');
  const ss = String(secs).padStart(2, '0');
  return hours > 0 ? `${hours}:${mm}:${ss}` : `${minutes}:${ss}`;
}

function elapsedSeconds() {
  let ms = accumulatedMs;
  if (running && !paused && startedAt !== null) ms += Date.now() - startedAt;
  return ms / 1000;
}

function haversineMeters(a, b) {
  const toRad = (deg) => (deg * Math.PI) / 180;
  const dLat = toRad(b.latitude - a.latitude);
  const dLon = toRad(b.longitude - a.longitude);
  const lat1 = toRad(a.latitude);
  const lat2 = toRad(b.latitude);
  const h =
    Math.sin(dLat / 2) ** 2 +
    Math.cos(lat1) * Math.cos(lat2) * Math.sin(dLon / 2) ** 2;
  return 2 * 6371000 * Math.asin(Math.min(1, Math.sqrt(h)));
}

function bearingDegrees(a, b) {
  const toRad = (deg) => (deg * Math.PI) / 180;
  const toDeg = (rad) => (rad * 180) / Math.PI;
  const dLon = toRad(b.longitude - a.longitude);
  const lat1 = toRad(a.latitude);
  const lat2 = toRad(b.latitude);
  const y = Math.sin(dLon) * Math.cos(lat2);
  const x =
    Math.cos(lat1) * Math.sin(lat2) -
    Math.sin(lat1) * Math.cos(lat2) * Math.cos(dLon);
  return (toDeg(Math.atan2(y, x)) + 360) % 360;
}

function currentPace() {
  const seconds = elapsedSeconds();
  if (distanceM < 25 || seconds < 3) return 0;
  return Math.round((seconds / distanceM) * 1000);
}

function currentSpeedDmS() {
  if (!coursePoint || !lastPoint) return 0;
  const dt = (lastPoint.timestamp - coursePoint.timestamp) / 1000;
  if (dt <= 0) return 0;
  const metres = haversineMeters(coursePoint, lastPoint);
  return Math.round((metres / dt) * 10);
}

function buildMetrics() {
  let flags = 0;
  if (running) flags |= FLAG_RUNNING;
  if (paused) flags |= FLAG_PAUSED;
  if (gpsOk) flags |= FLAG_GPS_OK;
  return {
    durationSec: elapsedSeconds(),
    distanceM,
    paceSecKm: currentPace(),
    bearing: coursePoint && lastPoint ? bearingDegrees(coursePoint, lastPoint) : 0,
    speedDmS: currentSpeedDmS(),
    flags,
  };
}

async function sendMetrics() {
  if (!connected) return;
  try {
    await gm.plugin.sendMessage(DOWNLINK_CHANNEL, encodeMetrics(buildMetrics()));
  } catch (error) {
    setStatus(error.message || String(error), 'error');
  }
}

async function sendReset() {
  if (!connected) return;
  try {
    await gm.plugin.sendMessage(DOWNLINK_CHANNEL, encodeReset());
  } catch (error) {
    setStatus(error.message || String(error), 'error');
  }
}

function render() {
  distanceEl.textContent = formatDistance(distanceM);
  durationEl.textContent = formatDuration(elapsedSeconds());
  const pace = currentPace();
  paceEl.textContent = pace > 0 ? `${formatDuration(pace)} /km` : '--';
  const speedKmh = (currentSpeedDmS() * 36) / 100;
  speedEl.textContent = `${speedKmh.toFixed(1)} km/h`;
}

function renderControls() {
  startButton.disabled = !running && !gpsOk;
  startButton.textContent = paused ? 'Reprendre' : running ? 'Pause' : 'Démarrer';
}

function renderStats() {
  const today = history[todayKey()] || { sessions: 0, distanceM: 0, durationSec: 0 };
  let weekDistance = 0;
  let weekSessions = 0;
  const now = new Date();
  for (let i = 0; i < 7; i += 1) {
    const d = new Date(now);
    d.setDate(now.getDate() - i);
    const month = String(d.getMonth() + 1).padStart(2, '0');
    const day = String(d.getDate()).padStart(2, '0');
    const entry = history[`${d.getFullYear()}-${month}-${day}`];
    if (entry) {
      weekDistance += entry.distanceM;
      weekSessions += entry.sessions;
    }
  }
  statsEls.todayDistance.textContent = formatDistance(today.distanceM);
  statsEls.weekDistance.textContent = formatDistance(weekDistance);
  statsEls.weekSessions.textContent = String(weekSessions);
}

async function loadState() {
  const result = await gm.storage.get(STORAGE_KEY);
  if (result.value && result.value.history) history = result.value.history;
}

async function persistState() {
  await gm.storage.set(STORAGE_KEY, { history });
}

function resetRun() {
  distanceM = 0;
  startedAt = null;
  accumulatedMs = 0;
  sessionStart = null;
  lastPoint = null;
  coursePoint = null;
  running = false;
  paused = false;
  render();
  renderControls();
}

function toggleRun() {
  if (!running) {
    running = true;
    paused = false;
    startedAt = Date.now();
    sessionStart = Date.now();
  } else {
    paused = !paused;
    if (paused) {
      accumulatedMs += Date.now() - startedAt;
      startedAt = null;
    } else {
      startedAt = Date.now();
    }
  }
  renderControls();
  void sendMetrics();
}

async function finishRun() {
  if (sessionStart === null) return;
  const seconds = elapsedSeconds();
  const key = todayKey();
  const entry = history[key] || { sessions: 0, distanceM: 0, durationSec: 0 };
  entry.sessions += 1;
  entry.distanceM += distanceM;
  entry.durationSec += seconds;
  history[key] = entry;
  await persistState();
  renderStats();
  lastSessionEl.textContent = `Dernière sortie : ${formatDistance(distanceM)} en ${formatDuration(seconds)}.`;
  resetRun();
  await sendReset();
}

function handlePosition(position) {
  gpsOk = true;
  const point = {
    latitude: position.latitude,
    longitude: position.longitude,
    timestamp: position.timestamp || Date.now(),
  };
  if (lastPoint && running && !paused) {
    const metres = haversineMeters(lastPoint, point);
    const dt = (point.timestamp - lastPoint.timestamp) / 1000;
    if (dt > 0 && metres / dt <= MAX_PLAUSIBLE_SPEED_MS) {
      distanceM += metres;
      coursePoint = lastPoint;
    }
  }
  lastPoint = point;
  render();
  renderControls();
}

async function startWatching() {
  if (watchId !== null) return;
  offPosition = gm.location.onPosition(handlePosition);
  offError = gm.location.onError(({ error }) => {
    setStatus(error && error.message ? error.message : String(error), 'error');
  });
  try {
    const result = await gm.location.watchPosition({ timeoutMs: 15000 });
    watchId = result.watchId;
  } catch (error) {
    setStatus(error.message || String(error), 'error');
  }
}

async function stopWatching() {
  if (watchId !== null) {
    try {
      await gm.location.clearWatch(watchId);
    } catch (error) {
      void error;
    }
    watchId = null;
  }
  if (offPosition) offPosition();
  if (offError) offError();
  offPosition = null;
  offError = null;
}

startButton.addEventListener('click', () => {
  if (running && !paused) {
    finishRun();
  } else {
    toggleRun();
  }
});

const offMessages = gm.plugin.onMessage((message) => {
  if (message.channel !== UPLINK_CHANNEL) return;
  const decoded = decodeEvent(message.data);
  if (!decoded || decoded.event !== EVENT_BUTTON) return;
  if (decoded.command === 'long') {
    void finishRun();
  } else {
    toggleRun();
  }
});

async function start() {
  try {
    await gm.ready();
    await loadState();
    render();
    renderStats();
    renderControls();
    const info = await gm.device.getInfo();
    connected = Boolean(info.connected);
    setStatus(connected ? 'Lunettes connectées' : 'Lunettes déconnectées', connected ? 'ready' : 'error');
    await gm.device.subscribeEvents(['connection']);
    gm.device.onConnection((event) => {
      connected = Boolean(event.connected);
      setStatus(connected ? 'Lunettes connectées' : 'Lunettes déconnectées', connected ? 'ready' : 'error');
      if (connected) void sendMetrics();
    });
    await startWatching();
    sendTimer = setInterval(() => {
      render();
      void sendMetrics();
    }, SEND_INTERVAL_MS);
  } catch (error) {
    setStatus(error.message || String(error), 'error');
  }
}

window.addEventListener('pagehide', () => {
  if (sendTimer !== null) clearInterval(sendTimer);
  offMessages();
  void stopWatching();
  gm.close();
});

void start();

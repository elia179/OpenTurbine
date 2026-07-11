'use strict';

// ── Channel label cache (updated from telemetry labels object) ─
const _labels = {
  tot:'TOT', tit:'TIT', n1:'N1', n2:'N2',
  oil_press:'Oil Press', oil_temp:'Oil Temp',
  p1:'P1', p2:'P2', fuel_press:'Fuel Press', fuel_flow:'Fuel Flow',
  stop:'Stop', start:'Start', ab_arm:'AB Arm'
};
function lbl(key) { return _labels[key] || key; }

// ── Unit preferences (persisted in localStorage) ─────────────
const _unitPrefs = (() => { try { return JSON.parse(localStorage.getItem('ot_units') || '{}'); } catch { return {}; } })();
function _saveUP() { try { localStorage.setItem('ot_units', JSON.stringify(_unitPrefs)); } catch {} }
function tempUnit()   { return _unitPrefs.temp  || 'C'; }
function pressUnit()  { return _unitPrefs.press || 'bar'; }
function setTempUnit(v)  { _unitPrefs.temp  = v; _saveUP(); applyUnitLabels(); if (_lastData) applyData(_lastData); }
function setPressUnit(v) { _unitPrefs.press = v; _saveUP(); applyUnitLabels(); if (_lastData) applyData(_lastData); }
function toDispTemp(c)   { return tempUnit()  === 'F'   ? c * 9/5 + 32  : c; }
function fromDispTemp(v) { return tempUnit()  === 'F'   ? (v - 32) * 5/9 : v; }
function toDispTempDelta(c)   { return tempUnit() === 'F' ? c * 9/5 : c; }
function fromDispTempDelta(v) { return tempUnit() === 'F' ? v * 5/9 : v; }
function toDispPress(b)  { return pressUnit() === 'psi' ? b * 14.5038   : b; }
function fromDispPress(v){ return pressUnit() === 'psi' ? v / 14.5038   : v; }
function dispTempUnit()  { return tempUnit()  === 'F'   ? '°F' : '°C'; }
function dispPressUnit() { return pressUnit() === 'psi' ? 'PSI' : 'bar'; }
function fmtInt(v) {
  const n = Number(v);
  return Number.isFinite(n) ? Math.round(n).toLocaleString('en-US') : '-';
}
function applyUnitLabels() {
  document.querySelectorAll('[data-unit="temp"]').forEach( el => el.textContent = dispTempUnit());
  document.querySelectorAll('[data-unit="press"]').forEach(el => el.textContent = dispPressUnit());
  const bt = document.getElementById('unit-temp-btn');
  if (bt) bt.textContent = tempUnit()  === 'C'   ? '°F'  : '°C';
  const bp = document.getElementById('unit-press-btn');
  if (bp) bp.textContent = pressUnit() === 'bar' ? 'PSI' : 'bar';
}

function applyContextTooltips(root = document) {
  root.querySelectorAll('.tool-card, .cfg-field, .hw-field, .hw-item-card').forEach(el => {
    if (el.title) return;
    const label = el.querySelector('.tool-name, .cfg-label, .hw-label, b')?.textContent?.trim() || '';
    const desc = el.querySelector('.tool-desc, .cfg-desc, .hw-desc')?.textContent?.trim() || '';
    if (desc) el.title = label ? label + ': ' + desc : desc;
  });
}
window.applyContextTooltips = applyContextTooltips;

function organizeDashboardCards() {
  const groups = {
    'temperature-cards': ['tot-card', 'tit-card', 'n1-card', 'n2-card'],
    'speed-cards': ['oil-card', 'oil-temp-card', 'oilpump-current-card'],
    'combustion-cards': ['flame-card', 'fuel-press-card', 'fuel-flow-card'],
    'pressure-cards': ['p1-card', 'p2-card'],
    'electrical-cards': ['batt-card', 'torque-card', 'glow-current-card',
      'igniter-current-card', 'igniter2-current-card']
  };
  Object.entries(groups).forEach(([targetId, cardIds]) => {
    const target = document.getElementById(targetId);
    if (!target) return;
    cardIds.forEach(cardId => {
      const card = document.getElementById(cardId);
      if (card) target.appendChild(card);
    });
  });
  const modeRow = document.querySelector('.mode-row');
  const advActSection = document.getElementById('adv-act-section');
  if (modeRow && advActSection) modeRow.insertAdjacentElement('afterend', advActSection);
  if (modeRow) {
    let outputCards = document.getElementById('actuator-output-cards');
    if (!outputCards) {
      outputCards = document.createElement('section');
      outputCards.id = 'actuator-output-cards';
      outputCards.className = 'grid-2 telemetry';
    }
    ['throttle-output-card', 'oil-output-card'].forEach(cardId => {
      const card = document.getElementById(cardId);
      if (card) outputCards.appendChild(card);
    });
    const anchor = advActSection || modeRow;
    anchor.insertAdjacentElement('afterend', outputCards);
  }
}

// ── Sparkline circular buffers ────────────────────────────────
function resolveCssColor(color) {
  const m = String(color || '').trim().match(/^var\((--[-_a-zA-Z0-9]+)\)$/);
  if (!m) return color;
  const v = getComputedStyle(document.documentElement).getPropertyValue(m[1]).trim();
  return v || color;
}

const SPARK_LEN = 60;
const _sparkN1       = new Array(SPARK_LEN).fill(0);
const _sparkTot      = new Array(SPARK_LEN).fill(0);
const _sparkOilTemp  = new Array(SPARK_LEN).fill(0);
const _sparkBattVolt = new Array(SPARK_LEN).fill(0);
const _sparkTorque   = new Array(SPARK_LEN).fill(0);

function pushSparkline(arr, val) {
  arr.shift();
  arr.push(val);
}

function drawSparkline(canvasId, data, color) {
  const c = document.getElementById(canvasId);
  if (!c) return;
  const ctx = c.getContext('2d');
  const w = c.width = c.offsetWidth || 200;
  const h = c.height = 36;
  ctx.clearRect(0, 0, w, h);
  const max = Math.max(...data, 1);
  const min = 0;
  const range = max - min || 1;
  ctx.strokeStyle = resolveCssColor(color);
  ctx.lineWidth = 2.2;
  ctx.beginPath();
  data.forEach((v, i) => {
    const x = (i / (data.length - 1)) * w;
    const y = h - ((v - min) / range) * h;
    if (i === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
  });
  ctx.stroke();
}

// ── WebSocket live telemetry ──────────────────────────────────
// Architecture: client-pull model.
//
// The server pushes telemetry in response to a tiny "p" message sent by the
// client at the page-specific live interval.  This ensures the push runs inside the async_tcp task
// context (WS_EVT_DATA callback) rather than from webTask — eliminating the
// cross-task notification lag that caused 5-20 s burst-then-silence behaviour.
//
// setInterval at 333 ms is reliable for a visible foreground tab; if the tab
// is backgrounded the rate drops to ~1 s which is acceptable for monitoring.
let ws = null;
let _lastMsgMs = 0;
let _lastConnectMs = 0;
let _pullTimer = null;
let _pullPeriodMs = 0;
let _restFallbackTimer = null;
let _restFallbackInFlight = false;
let _wsRequestInFlight = false;
let _wsRequestSentMs = 0;
let _lastUptimeS = null;
let _lastBootCount = null;
let _statusHeartbeatTimer = null;

function isLiveTelemetryPage() {
  if (document.body?.dataset?.page === 'dashboard') return true;
  return location.pathname === '/' ||
    location.pathname === '/index.html' ||
    location.pathname === '/calibration.html' ||
    location.pathname === '/config.html';
}
function isDashboardPage() {
  if (document.body?.dataset?.page === 'dashboard') return true;
  return location.pathname === '/' || location.pathname === '/index.html';
}
function isConfigPage() {
  return location.pathname === '/config.html';
}
function isCalibrationPage() {
  return location.pathname === '/calibration.html';
}
function usesGlobalTelemetry() {
  return isLiveTelemetryPage();
}
function pageStartsTelemetryAfterSetup() {
  return isConfigPage() || isCalibrationPage();
}
function hasPageLocalTelemetry() {
  return location.pathname === '/hardware.html' ||
    location.pathname === '/sequence.html' ||
    location.pathname === '/tools.html';
}

function desiredPullPeriodMs() {
  if (!isLiveTelemetryPage()) return 2000;
  if (isConfigPage()) return 1000;
  // Dashboard and Calibration are live-control pages.  Keep them at 3 Hz from
  // the first load; slower background pages still use the 2 s period above.
  return 333;
}

function startPullTimer() {
  const period = desiredPullPeriodMs();
  if (_pullTimer && _pullPeriodMs === period) return;
  if (_pullTimer) clearInterval(_pullTimer);
  _pullPeriodMs = period;
  requestTelemetryNow();
  _pullTimer = setInterval(requestTelemetryNow, period);
}

function requestTelemetryNow() {
  if (_wsRequestInFlight && Date.now() - _wsRequestSentMs > 1200) {
    _wsRequestInFlight = false;
  }
  if (ws && ws.readyState === WebSocket.OPEN && !_wsRequestInFlight) {
    _wsRequestInFlight = true;
    _wsRequestSentMs = Date.now();
    ws.send('p');
  }
}

async function restTelemetryFallbackNow() {
  if (!isLiveTelemetryPage() || document.hidden || _restFallbackInFlight) return;
  if (_lastMsgMs && Date.now() - _lastMsgMs < 2500) return;
  _restFallbackInFlight = true;
  try {
    const r = await fetch('/api/data', { cache: 'no-store' });
    if (r.ok) {
      const d = await r.json();
      _lastMsgMs = Date.now();
      applyData(d);
    }
  } catch (_) {
  } finally {
    _restFallbackInFlight = false;
  }
}

function startRestFallbackTimer() {
  if (_restFallbackTimer || !isLiveTelemetryPage()) return;
  _restFallbackTimer = setInterval(restTelemetryFallbackNow, 1500);
}

function stopGlobalTelemetry() {
  if (_pullTimer) { clearInterval(_pullTimer); _pullTimer = null; _pullPeriodMs = 0; }
  if (_restFallbackTimer) { clearInterval(_restFallbackTimer); _restFallbackTimer = null; }
  if (_statusHeartbeatTimer) { clearInterval(_statusHeartbeatTimer); _statusHeartbeatTimer = null; }
  _wsRequestInFlight = false;
  _restFallbackInFlight = false;
  if (ws) {
    try {
      ws.onclose = null;
      ws.onerror = null;
      ws.onmessage = null;
      if (ws.readyState === WebSocket.OPEN || ws.readyState === WebSocket.CONNECTING) ws.close(1000, 'page navigation');
    } catch (_) {}
    ws = null;
  }
}

function setConnectionState(ok, text) {
  const dot = document.getElementById('conn');
  const lbl = document.getElementById('conn-label');
  if (dot) dot.className = 'conn-dot ' + (ok ? 'connected' : 'disconnected');
  if (lbl) {
    lbl.textContent = text || (ok ? 'Connected' : 'Disconnected');
    lbl.style.color = ok ? 'var(--green)' : 'var(--yellow)';
  }
}

function startStatusHeartbeat() {
  if (usesGlobalTelemetry() || hasPageLocalTelemetry() || _statusHeartbeatTimer) return;
  const poll = async () => {
    try {
      const r = await fetch('/api/status', { cache: 'no-store' });
      setConnectionState(r.ok, r.ok ? 'Connected' : 'Disconnected');
    } catch (_) {
      setConnectionState(false, 'Disconnected');
    }
  };
  poll();
  _statusHeartbeatTimer = setInterval(poll, 3000);
}

function connect() {
  if (!usesGlobalTelemetry()) return;
  if (ws && ws.readyState <= WebSocket.OPEN) return;
  _lastConnectMs = Date.now();
  ws = new WebSocket('ws://' + location.host + '/ws');

  ws.onopen = () => {
    setConnectionState(true, 'Connected');
    _lastMsgMs = Date.now();
    // Start sending pull requests — server responds with compact live telemetry.
    // /api/data supplies full boot snapshots for slow labels and limits.
    // Dashboard and calibration require responsive live values. Keep them at 1-3 Hz;
    // other pages only need the connection indicator and can poll more slowly.
    startPullTimer();
  };

  ws.onclose = () => {
    _wsRequestInFlight = false;
    _wsRequestSentMs = 0;
    if (_pullTimer) { clearInterval(_pullTimer); _pullTimer = null; _pullPeriodMs = 0; }
    setConnectionState(false, 'Reconnecting - values retained');
    const wait = 1000 - (Date.now() - _lastConnectMs);
    if (wait <= 0) {
      connect();
    } else {
      setTimeout(connect, wait);
    }
  };

  ws.onerror = () => { ws.close(); };

  ws.onmessage = (ev) => {
    _wsRequestInFlight = false;
    _wsRequestSentMs = 0;
    _lastMsgMs = Date.now();
    try { applyData(JSON.parse(ev.data)); } catch(e) {}
  };
}

// ── Apply telemetry frame to DOM ──────────────────────────────
let _lastData = null;
function applyData(d) {
  let bootChanged = false;
  if (d && d.boot_count !== undefined) {
    const nextBootCount = Number(d.boot_count);
    if (Number.isFinite(nextBootCount) && _lastBootCount !== null && nextBootCount !== _lastBootCount) {
      _lastData = null;
      _lastUptimeS = null;
      bootChanged = true;
    }
    if (Number.isFinite(nextBootCount)) _lastBootCount = nextBootCount;
  }
  if (d && d.uptime_s !== undefined && _lastUptimeS !== null) {
    const nextUptime = Number(d.uptime_s);
    if (Number.isFinite(nextUptime) && nextUptime <= 5 && _lastUptimeS > 5) {
      _lastData = null;
      _lastUptimeS = null;
      bootChanged = true;
    }
    if (Number.isFinite(nextUptime) && nextUptime < _lastUptimeS && (_lastUptimeS - nextUptime) < 30) {
      return null;
    }
  }
  // Merge into _lastData rather than replace — fast frames only carry live
  // fields; slow fields (has_*, limits, max_oil_temp, etc.) must persist so
  // that applyData(_lastData) called by the unit-toggle buttons still has them.
  if (!_lastData) _lastData = {};
  // di_channels: fast frames only carry {state,pin} — merge per-entry so the
  // label/role fields from the /api/data snapshot survive fast WS frames.
  if (d && Array.isArray(d.di_channels) && Array.isArray(_lastData.di_channels)) {
    d.di_channels = d.di_channels.map((ch, i) => Object.assign({}, _lastData.di_channels[i], ch));
  }
  Object.assign(_lastData, d);
  d = _lastData;
  if (d.uptime_s !== undefined && Number.isFinite(Number(d.uptime_s))) {
    _lastUptimeS = Number(d.uptime_s);
  }
  if (ws && ws.readyState === WebSocket.OPEN) startPullTimer();
  if (bootChanged && usesGlobalTelemetry()) {
    fetch('/api/data', { cache: 'no-store' })
      .then(r => r.json())
      .then(full => { try { applyData(full); } catch(e) {} })
      .catch(() => {});
  }
  // ── Channel labels ─────────────────────────────────────────
  if (d.labels) {
    Object.assign(_labels, d.labels);
    const sl = (id, txt) => { const e = document.getElementById(id); if (e) e.textContent = txt; };
    sl('lbl-n1',         lbl('n1') + ' RPM');
    sl('lbl-n2',         lbl('n2') + ' RPM');
    sl('lbl-tot',        lbl('tot'));
    sl('lbl-tit',        lbl('tit'));
    sl('lbl-oil',        lbl('oil_press'));
    sl('lbl-oil-temp',   lbl('oil_temp'));
    sl('lbl-p1',         lbl('p1'));         // unit shown by adjacent data-unit="press" span
    sl('lbl-p2',         lbl('p2'));
    sl('lbl-fuel-press', lbl('fuel_press'));
    sl('lbl-fuel-flow',  lbl('fuel_flow'));
  }
  // Coerce to Number so formatting works even if JSON sent as int
  setText('n1',  d.n1  !== undefined ? fmtInt(d.n1)  : '—');
  const n1Card = document.getElementById('n1-card');
  if (n1Card && d.has_n1 !== undefined) n1Card.style.display = d.has_n1 ? '' : 'none';
  setText('n2',  d.n2  !== undefined ? fmtInt(d.n2)  : '—');
  const n2Card = document.getElementById('n2-card');
  if (n2Card && d.has_n2 !== undefined) n2Card.style.display = d.has_n2 ? '' : 'none';
  setText('tot', d.tot !== undefined ? toDispTemp(Number(d.tot)).toFixed(1)       : '—');
  const totCard = document.getElementById('tot-card');
  if (totCard && d.has_tot !== undefined) totCard.style.display = d.has_tot ? '' : 'none';
  setText('max-n1',  d.max_n1  !== undefined ? fmtInt(d.max_n1) : '—');
  setText('max-n2',  d.max_n2  !== undefined ? fmtInt(d.max_n2) : '—');
  setText('max-tot', d.max_tot !== undefined ? toDispTemp(Number(d.max_tot)).toFixed(1) : '—');
  setText('oil', d.oil !== undefined ? toDispPress(Number(d.oil)).toFixed(2)       : '—');
  const oilCard = document.getElementById('oil-card');
  if (oilCard && d.has_oil_press !== undefined) oilCard.style.display = d.has_oil_press ? '' : 'none';
  setText('oil-demand-val', d.oil_demand !== undefined ? toDispPress(Number(d.oil_demand)).toFixed(2) : '—');
  const baseThrottle = d.throttle_demand !== undefined ? Number(d.throttle_demand) : undefined;
  const effectiveThrottle = d.throttle_effective !== undefined ? Number(d.throttle_effective) : baseThrottle;
  setText('throttle-demand', effectiveThrottle !== undefined
    ? (effectiveThrottle * 100).toFixed(1) + '%' : '—');
  const throttleOutputCard = document.getElementById('throttle-output-card');
  if (throttleOutputCard && d.has_throttle !== undefined) {
    throttleOutputCard.style.display = d.has_throttle ? '' : 'none';
  }
  // Throttle gauge bar
  if (effectiveThrottle !== undefined) {
    const gb = document.getElementById('throttle-gauge-bar');
    if (gb) gb.style.width = (effectiveThrottle * 100).toFixed(1) + '%';
  }
  const feedbackInhibit = document.getElementById('throttle-feedback-inhibit-note');
  if (feedbackInhibit) {
    const primaryEgtUnhealthy =
      d.egt_source === 1 ? (d.has_tot && d.tot_healthy === false) :
      d.egt_source === 2 ? (d.has_tit && d.tit_healthy === false) :
      ((d.has_tot && d.tot_healthy === false) || (d.has_tit && d.tit_healthy === false));
    const sensorBlocksIncrease = !d.bench_mode &&
      (d.mode === 'RUNNING' || d.mode === 'STARTUP') &&
      (primaryEgtUnhealthy ||
       (d.has_n1 && d.n1_healthy === false));
    feedbackInhibit.style.display = sensorBlocksIncrease ? '' : 'none';
  }
  // Physical throttle input display (when throttle input is configured)
  {
    const iRow = document.getElementById('throttle-input-row');
    const iVal = document.getElementById('throttle-input-pct');
    const hasInput = d.throttle_input_type && d.throttle_input_type !== 'none';
    if (iRow) iRow.style.display = hasInput && d.mode === 'RUNNING' ? '' : 'none';
    if (iVal && hasInput) {
      const rawNorm = d.throttle_input_raw !== undefined ? Number(d.throttle_input_raw) / 4095 : 0;
      const norm = d.throttle_input_norm !== undefined
        ? Number(d.throttle_input_norm)
        : (d.throttle_input_type === 'servo' && d.rc_throttle_norm !== undefined)
          ? Number(d.rc_throttle_norm) : rawNorm;
      const pct = (norm * 100).toFixed(1);
      iVal.textContent = pct;
    }
  }
  setText('oil-pct',     d.oil_pct  !== undefined ? d.oil_pct + '%'          : '—');
  const oilOutputCard = document.getElementById('oil-output-card');
  if (oilOutputCard && d.has_oil_pump !== undefined) {
    oilOutputCard.style.display = d.has_oil_pump ? '' : 'none';
  }
  const speedGroup = document.getElementById('speed-group');
  if (speedGroup) {
    const hasOilSystem = d.has_oil_press || d.has_oil_temp || d.has_oil_pump_current;
    speedGroup.style.display = hasOilSystem ? '' : 'none';
  }
  const temperatureGroup = document.getElementById('temperature-group');
  if (temperatureGroup) temperatureGroup.style.display =
    (d.has_tot || d.has_tit || d.has_n1 || d.has_n2) ? '' : 'none';
  setText('uptime',      d.uptime_s !== undefined ? formatUptime(d.uptime_s)  : '—');
  setText('last-event',  d.last_event || '—');

  // Throttle sub-labels: calibrated fuel-pump min-spin + dynamic idle target.
  // Non-standby commands below min-spin are displayed as zero after firmware
  // applies the same deadband used at the actuator output.
  if (d.fuel_pump_min_pct !== undefined) {
    setText('throttle-idle-floor', Number(d.fuel_pump_min_pct).toFixed(1));
  }
  const floorRow = document.getElementById('throttle-floor-row');
  if (floorRow) floorRow.style.display = d.mode === 'STARTUP' ? 'none' : '';
  const startupRangeRow = document.getElementById('throttle-startup-range-row');
  if (startupRangeRow) {
    const showStartupRange = d.mode === 'STARTUP' &&
      d.fuel_pump_min_pct !== undefined && d.fuel_idle_max_pct !== undefined;
    startupRangeRow.style.display = showStartupRange ? '' : 'none';
    if (showStartupRange) {
      setText('throttle-startup-range',
        Number(d.fuel_pump_min_pct).toFixed(1) + ' to ' + Number(d.fuel_idle_max_pct).toFixed(1));
    }
  }
  const effectiveNote = document.getElementById('throttle-effective-note');
  if (effectiveNote) {
    const showEffective = baseThrottle !== undefined && effectiveThrottle !== undefined &&
      Math.abs(effectiveThrottle - baseThrottle) > 0.0005;
    effectiveNote.style.display = showEffective ? '' : 'none';
    if (showEffective) setText('throttle-base-demand', (baseThrottle * 100).toFixed(1));
  }
  const oilStartupNote = document.getElementById('oil-startup-setting-note');
  if (oilStartupNote) {
    const showOilStartup = d.mode === 'STARTUP' && d.oil_pump_on_pct !== undefined;
    oilStartupNote.style.display = showOilStartup ? '' : 'none';
    if (showOilStartup) setText('oil-startup-setting', Number(d.oil_pump_on_pct).toFixed(1));
  }
  const diWrap = document.getElementById('throttle-di-wrap');
  if (diWrap) {
    const showDi = d.dynamic_idle_enabled && d.mode === 'RUNNING';
    diWrap.style.display = showDi ? '' : 'none';
    if (showDi && d.idle_target_rpm !== undefined) {
      setText('throttle-di-rpm', fmtInt(d.idle_target_rpm));
    }
  }

  // Relight status — hide card entirely when relight is disabled in config
  const relightCard = document.getElementById('relight-card');
  if (relightCard && d.relight_enabled !== undefined) relightCard.style.display = d.relight_enabled ? '' : 'none';

  if (d.relight_armed !== undefined || d.relight_attempts !== undefined) {
    const armed    = !!d.relight_armed;
    const attempts = d.relight_attempts !== undefined ? d.relight_attempts : 0;
    setText('relight-status', armed
      ? (attempts > 0 ? 'Armed — ' + attempts + ' attempt' + (attempts !== 1 ? 's' : '') : 'Armed')
      : 'Disarmed');
  }

  // Oil min bar (session minimum — only shown once engine has run)
  const oilMinRow = document.getElementById('oil-min-bar-row');
  if (oilMinRow) {
    const minVal = d.oil_min_bar !== undefined ? Number(d.oil_min_bar) : 0;
    oilMinRow.style.display = minVal > 0 ? '' : 'none';
    if (minVal > 0) setText('oil-min-bar-val', toDispPress(minVal).toFixed(2));
  }

  // Oil failsafe indicator
  const failsafeNote = document.getElementById('oil-failsafe-note');
  if (failsafeNote) failsafeNote.style.display = d.oil_failsafe_active ? '' : 'none';

  // Manual relight indicator
  const manualRelightNote = document.getElementById('manual-relight-note');
  if (manualRelightNote) manualRelightNote.style.display = d.manual_relight_active ? '' : 'none';

  // Extra cooldown indicator + countdown
  const ecCard = document.getElementById('extra-cooldown-card');
  if (ecCard) ecCard.style.display = d.extra_cooldown_active ? '' : 'none';
  if (d.extra_cooldown_remaining_s !== undefined)
    setText('extra-cooldown-remaining', d.extra_cooldown_remaining_s);

  // Standby oil feed indicator
  const standbyOilNote = document.getElementById('standby-oil-feed-note');
  if (standbyOilNote) standbyOilNote.style.display = d.standby_oil_feed_active ? '' : 'none';

  // System stats — flash + log records
  if (d.flash_free_kb  !== undefined) setText('sys-flash-free',  d.flash_free_kb);
  if (d.flash_used_kb  !== undefined) setText('sys-flash-used',  d.flash_used_kb);
  if (d.flash_total_kb !== undefined) setText('sys-flash-total', d.flash_total_kb);
  if (d.log_records    !== undefined) setText('sys-log-records', d.log_records);
  if (d.log_max_records !== undefined) setText('sys-log-max',   d.log_max_records);
  if (d.boot_count     !== undefined) setText('sys-boot-count',  d.boot_count);
  if (d.reset_reason   !== undefined) {
    const reasons = ['UNKNOWN','POWER_ON','EXT','SW','PANIC','INT_WDT','TASK_WDT','WDT','DEEPSLEEP','BROWNOUT','SDIO'];
    setText('sys-reset-reason', reasons[d.reset_reason] || d.reset_reason);
  }

  // Flame progress bar + threshold marker
  if (d.has_flame === false) {
    setText('flame-raw-val', 'No data');
    const fill = document.getElementById('flame-bar-fill');
    if (fill) fill.style.width = '0%';
  } else if (d.flame_raw !== undefined) {
    const pct = Math.max(0, Math.min(100, (d.flame_raw / 4095) * 100));
    const fill = document.getElementById('flame-bar-fill');
    if (fill) {
      fill.style.width = pct + '%';
      fill.style.background = d.flame ? 'var(--green)' : 'var(--dim)';
    }
    setText('flame-raw-val', d.flame_raw + ' ADC');
  }
  if (d.flame_threshold !== undefined) {
    const thrPct = Math.max(0, Math.min(100, (d.flame_threshold / 4095) * 100));
    const mark = document.getElementById('flame-thr-mark');
    if (mark) mark.style.left = thrPct + '%';
    setText('flame-thr-label', 'thr: ' + d.flame_threshold);
  }

  // Pressure sensors — show/hide based on whether sensors are fitted
  const psSection = document.getElementById('pressure-section');
  if (psSection) psSection.style.display = (d.has_p1 || d.has_p2) ? '' : 'none';
  const p1Card = document.getElementById('p1-card');
  if (p1Card && d.has_p1 !== undefined) p1Card.style.display = d.has_p1 ? '' : 'none';
  const p2Card = document.getElementById('p2-card');
  if (p2Card && d.has_p2 !== undefined) p2Card.style.display = d.has_p2 ? '' : 'none';
  // A railed/disconnected optional pressure sensor now reports unhealthy —
  // show an explicit dash instead of a believable extrapolated number.
  const p1Ok = d.p1_healthy !== false, p2Ok = d.p2_healthy !== false;
  setText('p1', d.p1 !== undefined && p1Ok ? toDispPress(Number(d.p1)).toFixed(2) : '—');
  setText('p2', d.p2 !== undefined && p2Ok ? toDispPress(Number(d.p2)).toFixed(2) : '—');
  setText('max-p1', d.max_p1 !== undefined ? toDispPress(Number(d.max_p1)).toFixed(2) : '—');
  setText('max-p2', d.max_p2 !== undefined ? toDispPress(Number(d.max_p2)).toFixed(2) : '—');
  const p1El = document.getElementById('p1'), p2El = document.getElementById('p2');
  if (p1El) p1El.title = p1Ok ? '' : 'P1 sensor fault (railed/disconnected) — check wiring';
  if (p2El) p2El.title = p2Ok ? '' : 'P2 sensor fault (railed/disconnected) — check wiring';

  // Health dots
  // RPM health is only meaningful when the engine is running — zero RPM at standby is valid.
  // Pass null when not in an operational mode so the dot shows neutral (dim), not fault (red).
  const engineOp = (d.mode === 'RUNNING' || d.mode === 'STARTUP');
  // RPM health: green whenever the sensor has trustworthy data (any mode, so
  // live bench RPM shows green like every other sensor dot). A stopped shaft
  // reads 0 with no pulses (unhealthy) — show that as a red fault only while
  // operating; in STANDBY a still shaft is expected, so stay neutral (grey).
  setDot('n1-health',  d.n1_healthy ? true : (engineOp ? false : null), lbl('n1') + ' RPM');
  setDot('n2-health',  d.n2_healthy ? true : (engineOp ? false : null), lbl('n2') + ' RPM');
  setDot('tot-health', d.tot_healthy, lbl('tot'));
  setDot('oil-health', d.oil_healthy, lbl('oil_press'));
  // Flame dot: green = flame confirmed; red = no flame while the engine is
  // operating (running flameout cue); neutral = no flame otherwise (normal
  // at standby). Title set manually below to bypass setDot's generic
  // "sensor fault (check wiring)" text — flame-off is not a wiring fault.
  // Exception: a railed ADC at standby (flame_healthy=false) IS a wiring
  // hint — while running a strong flame can saturate legitimately, so the
  // rail check is only surfaced outside operational modes.
  const flameWiring = !engineOp && d.flame_healthy === false;
  setDot('flame-dot',  d.has_flame === false ? null
    : d.flame ? true : ((engineOp || flameWiring) ? false : null));
  {
    const flameDot = document.getElementById('flame-dot');
    if (flameDot) flameDot.title = d.has_flame === false ? 'Flame sensor'
      : flameWiring ? 'Flame sensor — ADC railed (check wiring)'
      : (d.flame ? 'Flame sensor — flame confirmed' : 'Flame sensor — no flame');
  }
  const flameCard = document.getElementById('flame-card');
  if (flameCard && d.has_flame !== undefined) flameCard.style.display = d.has_flame ? '' : 'none';
  const combustionGroup = document.getElementById('combustion-group');
  if (combustionGroup) combustionGroup.style.display =
    (d.has_flame || d.has_fuel_press || d.has_fuel_flow) ? '' : 'none';

  // Mode badge
  const badge = document.getElementById('mode-badge');
  if (badge) {
    badge.textContent = d.mode || '—';
    badge.className   = 'mode-badge ' + (d.mode || '');
  }

  // Banners
  const devBanner   = document.getElementById('dev-banner');
  if (devBanner) devBanner.style.display = d.dev_mode ? '' : 'none';
  const benchBanner = document.getElementById('bench-banner');
  if (benchBanner) benchBanner.style.display = d.bench_mode ? '' : 'none';
  const logDropBanner = document.getElementById('session-log-drop-banner');
  if (logDropBanner) {
    const dropped = Number(d.session_dropped_rows || 0);
    logDropBanner.style.display = dropped > 0 ? '' : 'none';
    if (dropped > 0) {
      logDropBanner.textContent = 'Session log dropped ' + dropped + ' row' + (dropped === 1 ? '' : 's') + '. CSV for this run is incomplete.';
    }
  }
  const flightDropBanner = document.getElementById('flight-log-drop-banner');
  if (flightDropBanner) {
    const dropped = Number(d.flight_dropped_events || 0);
    flightDropBanner.style.display = dropped > 0 ? '' : 'none';
    if (dropped > 0) {
      flightDropBanner.textContent = 'Event recorder dropped ' + dropped + ' event' + (dropped === 1 ? '' : 's') + '. Event log may be incomplete.';
    }
  }
  const storageBanner = document.getElementById('config-storage-banner');
  if (storageBanner) storageBanner.style.display = d.config_storage_fault ? '' : 'none';
  // Boot-config load warning (full frames: config_load_warning = string|null).
  // Dismiss hides it for this page load only; reappears on reload while it persists.
  const cfgLoadWarnBanner = document.getElementById('config-load-warn-banner');
  if (cfgLoadWarnBanner && d.config_load_warning !== undefined) {
    const warn = d.config_load_warning;
    if (warn) setText('config-load-warn-text', warn);
    cfgLoadWarnBanner.style.display = (warn && !window._cfgLoadWarnDismissed) ? 'flex' : 'none';
  }

  // Stop switch warning below start button
  const stopWarn = document.getElementById('stop-switch-warn');
  if (stopWarn) stopWarn.style.display = d.stop_switch_active ? '' : 'none';

  // Start/Stop buttons — disable + hardware glow when physical button is pressed
  const running = d.mode === 'RUNNING' || d.mode === 'STARTUP' || d.mode === 'SHUTDOWN';
  const startBtn = document.getElementById('btn-start');
  if (startBtn) {
    startBtn.textContent = d.mode === 'STARTUP' ? 'Starting...' : 'START';
    if (d.mode !== 'STANDBY' && startBtn._startTimeout) {
      clearTimeout(startBtn._startTimeout);
      startBtn._startTimeout = null;
    }
  }
  // Mirror the backend start-preflight reasons visible in telemetry, so
  // START is disabled with an explanation instead of accepting the click
  // and rejecting it server-side (backend remains the authority).
  let startBlock = '';
  if (!running) {
    if (d.mode === 'FAULT')                startBlock = 'ECU is in FAULT — fix config/profile first';
    else if (d.stop_switch_active)         startBlock = 'STOP switch is active';
    else if (d.profile_match === false)    startBlock = 'Profile mismatch — upload a matching config';
    // (boot config-load failure enters FAULT mode, caught above; telemetry
    // config_locked means "running, edits locked" and never applies here)
    else if (d.extra_cooldown_active)      startBlock = 'Extra cooldown is running — stop it on Tools';
    else if (d.seq_has_structural_errors)  startBlock = 'Startup sequence has structural errors — see Sequence page';
    else if (d.seq_has_errors && !d.bench_mode) startBlock = 'Sequence hardware errors — see Sequence page (Bench Mode bypasses)';
    else if (Array.isArray(d.di_channels) &&
             d.di_channels.some(ch => ch && ch.pin >= 0 && ch.state && ch.role === 'inhibit_start'))
                                           startBlock = 'Start-inhibit input is active';
  }
  setDisabled('btn-start', running || !!startBlock);
  if (startBtn) startBtn.title = startBlock;
  const sbr = document.getElementById('start-block-reason');
  if (sbr) {
    // stop-switch has its own warning line — avoid doubling it
    const show = startBlock && !d.stop_switch_active;
    sbr.style.display = show ? '' : 'none';
    if (show) sbr.textContent = '⚠ ' + startBlock;
  }
  setDisabled('btn-stop',  !running);
  setHwActive('btn-start', !!d.start_switch_active);
  setHwActive('btn-stop',  !!d.stop_switch_active);

  // ── Sequence progress ─────────────────────────────────────
  const seqSection = document.getElementById('seq-progress-section');
  if (seqSection) {
    const abSeqActive = d.mode === 'RUNNING' && (d.ab_mode === 'Igniting' || d.ab_mode === 'ShuttingDown');
    const inSeq = (d.mode === 'STARTUP' || d.mode === 'SHUTDOWN' || abSeqActive) && d.seq_block_total > 0;
    seqSection.style.display = inSeq ? '' : 'none';
    if (inSeq) {
      setText('seq-block-name', d.current_block || '—');
      setText('seq-wait-reason', d.seq_wait_reason || '');
      const step = (d.seq_block_idx || 0) + 1;
      const total = d.seq_block_total || 1;
      setText('seq-step-text', step + ' / ' + total);
      const pct = Math.round((step / total) * 100);
      const bar = document.getElementById('seq-progress-bar');
      if (bar) bar.style.width = pct + '%';
    }
  }

  // ── Fault description banner ──────────────────────────────
  const faultCard = document.getElementById('fault-card');
  if (faultCard) {
    const hasFaultDesc = d.fault_description && d.fault_description.length > 0;
    const showFault = hasFaultDesc && (d.mode === 'FAULT' || d.mode === 'STANDBY' || d.mode === 'SHUTDOWN');
    faultCard.style.display = showFault ? '' : 'none';
    if (showFault) setText('fault-desc-text', d.fault_description);
  }

  // ── Profile mismatch banner ───────────────────────────────
  const mismatchBanner = document.getElementById('profile-mismatch-banner');
  if (mismatchBanner) {
    const mismatch = d.profile_match === false;
    mismatchBanner.style.display = mismatch ? '' : 'none';
  }
  // Older pages may still include this compact profile-error element.
  const profErr = document.getElementById('profile-error');
  if (profErr) profErr.style.display = (d.profile_match === false) ? '' : 'none';

  // ── Config version mismatch banner ────────────────────────
  const verBanner = document.getElementById('config-version-banner');
  if (verBanner) verBanner.style.display = d.config_version_mismatch ? '' : 'none';

  // ── Hour meter ────────────────────────────────────────────
  if (d.start_attempt_count !== undefined) setText('hour-start-count', fmtInt(d.start_attempt_count));
  else if (d.run_count !== undefined) setText('hour-start-count', fmtInt(d.run_count));
  if (d.run_count !== undefined) setText('hour-run-count', fmtInt(d.run_count));
  if (d.total_run_seconds !== undefined) {
    const hrs = Math.floor(d.total_run_seconds / 3600);
    const mins = Math.floor((d.total_run_seconds % 3600) / 60);
    setText('hour-run-time', hrs + 'h ' + String(mins).padStart(2, '0') + 'm');
  }

  // ── EGT rate of rise ──────────────────────────────────────
  if (d.tot_rise_rate !== undefined) {
    const rate = Number(d.tot_rise_rate);
    const rateEl = document.getElementById('tot-rise-rate-val');
    if (rateEl) rateEl.textContent = toDispTempDelta(rate).toFixed(1) + ' ' + dispTempUnit() + '/s';
  }

  // ── Color gauges + approach-to-limit warnings ────────────
  if (d.rpm_limit && d.n1 !== undefined) {
    const pct = Math.min(100, (d.n1 / d.rpm_limit) * 100);
    setGaugeBar('n1-gauge-bar', pct);
    const warn = document.getElementById('n1-approach-warn');
    if (warn) {
      const show = pct >= 85;
      warn.style.display = show ? '' : 'none';
      if (show) warn.textContent = '⚠ N1 at ' + pct.toFixed(0) + '% — '
        + fmtInt(d.n1) + ' / ' + fmtInt(d.rpm_limit) + ' RPM';
    }
    const absLbl = document.getElementById('n1-abs-label');
    if (absLbl) absLbl.textContent = fmtInt(d.n1) + ' / ' + fmtInt(d.rpm_limit) + ' RPM';
  }
  const selectedEgtSource = d.egt_source === 2 ? 'tit'
    : (d.egt_source === 1 ? 'tot' : (d.has_tot ? 'tot' : (d.has_tit ? 'tit' : null)));
  const isPrimaryTot = selectedEgtSource === 'tot';
  const isPrimaryTit = selectedEgtSource === 'tit';

  if (d.tot !== undefined) {
    const totLimit = Number(d.tot_limit || 0);
    const pct = totLimit > 0 ? Math.min(100, (d.tot / totLimit) * 100) : 0;
    setGaugeBar('tot-gauge-bar', pct);
    const warn = document.getElementById('tot-approach-warn');
    if (warn) {
      const limit = Number(d.egt_limit || totLimit);
      const primaryPct = limit > 0 ? Math.min(100, (Number(d.tot) / limit) * 100) : 0;
      const show = isPrimaryTot && primaryPct >= 85;
      warn.style.display = show ? '' : 'none';
      if (show) warn.textContent = 'Warning: ' + lbl('tot') + ' at ' + primaryPct.toFixed(0) + '% - '
        + toDispTemp(Number(d.tot)).toFixed(0) + ' / ' + toDispTemp(limit).toFixed(0) + ' ' + dispTempUnit();
    }
    const absLbl = document.getElementById('tot-abs-label');
    if (absLbl) absLbl.textContent = totLimit > 0
      ? toDispTemp(Number(d.tot)).toFixed(0) + ' / ' + toDispTemp(totLimit).toFixed(0) + ' ' + dispTempUnit()
      : toDispTemp(Number(d.tot)).toFixed(0) + ' ' + dispTempUnit() + ' / OFF';
  }
  if (d.oil !== undefined) {
    const oilMin = Number(d.oil_running_min || 0);
    if (d.mode === 'RUNNING' || d.mode === 'SHUTDOWN') {
      // SHUTDOWN included: pump is still active and the engine is spinning,
      // and a low-oil red bar is the cue that explains a low-oil shutdown.
      // Oil is inverted vs the other gauges: LOW pressure is the fault state.
      // Width tracks pressure (minimum = 50% width, floor 8% so a red sliver
      // is always visible); color is forced — red below the running minimum,
      // amber within 15% above it, green otherwise. Previously a below-min
      // reading rendered as an EMPTY neutral bar, which read as "fine".
      const ratio = oilMin > 0 ? d.oil / oilMin : 0;
      const width = oilMin > 0 ? Math.min(100, Math.max(8, ratio * 50)) : 0;
      const cls = oilMin > 0
                ? (d.oil < oilMin ? 'danger' : d.oil < oilMin * 1.15 ? 'warn' : 'ok')
                : '';
      setGaugeBar('oil-gauge-bar', width, cls);
      const warn = document.getElementById('oil-approach-warn');
      if (warn) {
        const low = oilMin > 0 && d.oil < oilMin * 1.15;
        warn.style.display = low ? '' : 'none';
        if (low) warn.textContent = '⚠ Oil ' + toDispPress(Number(d.oil)).toFixed(2)
          + ' ' + dispPressUnit() + ' — near min ' + toDispPress(oilMin).toFixed(2) + ' ' + dispPressUnit();
      }
    } else {
      // STANDBY/FAULT and other non-op modes: keep the bar live but neutral —
      // clears a stale red 'danger' bar/warning left over from the last
      // RUNNING/SHUTDOWN frame.
      const ratio = oilMin > 0 ? d.oil / oilMin : 0;
      const width = oilMin > 0 ? Math.min(100, Math.max(0, ratio * 50)) : 0;
      setGaugeBar('oil-gauge-bar', width, 'ok');
      const warn = document.getElementById('oil-approach-warn');
      if (warn) warn.style.display = 'none';
    }
    const absLbl = document.getElementById('oil-abs-label');
    if (absLbl) absLbl.textContent = oilMin > 0
      ? toDispPress(Number(d.oil)).toFixed(2) + ' / ≥' + toDispPress(oilMin).toFixed(2) + ' ' + dispPressUnit()
      : toDispPress(Number(d.oil)).toFixed(2) + ' ' + dispPressUnit() + ' / OFF';
  }

  // ── Firmware version (shown once on first telemetry frame) ──
  if (d.fw_version) {
    const el = document.getElementById('fw-version');
    if (el && !el._set) { el.textContent = 'v' + d.fw_version; el._set = true; }
  }

  // ── Sparklines ────────────────────────────────────────────
  if (d.n1 !== undefined) {
    pushSparkline(_sparkN1, Number(d.n1));
    drawSparkline('n1-sparkline', _sparkN1, 'var(--accent)');
  }
  if (d.tot !== undefined) {
    pushSparkline(_sparkTot, Number(d.tot));
    drawSparkline('tot-sparkline', _sparkTot, 'var(--accent)');
  }
  if (d.has_oil_temp && d.oil_temp !== undefined) {
    pushSparkline(_sparkOilTemp, Number(d.oil_temp));
    drawSparkline('oil-temp-sparkline', _sparkOilTemp, 'var(--accent)');
  }
  if (d.has_batt_voltage && d.batt_voltage !== undefined) {
    pushSparkline(_sparkBattVolt, Number(d.batt_voltage));
    drawSparkline('batt-sparkline', _sparkBattVolt, 'var(--accent)');
  }
  if (d.has_torque && d.torque !== undefined) {
    pushSparkline(_sparkTorque, Number(d.torque));
    drawSparkline('torque-sparkline', _sparkTorque, 'var(--accent)');
  }

  // ── Extended sensors (oil temp, battery, torque, current sensors) ──
  const extSection = document.getElementById('ext-sensors-section');
  if (extSection) {
    const anyExt = d.has_batt_voltage || d.has_torque ||
                   d.has_glow_current || d.has_igniter_current ||
                   d.has_igniter2_current;
    extSection.style.display = anyExt ? '' : 'none';
  }

  // Oil temperature card
  const oilTempCard = document.getElementById('oil-temp-card');
  if (oilTempCard) {
    oilTempCard.style.display = d.has_oil_temp ? '' : 'none';
    if (d.has_oil_temp) {
      setText('oil-temp', d.oil_temp !== undefined ? toDispTemp(Number(d.oil_temp)).toFixed(1) : '—');
      setText('max-oil-temp', d.max_oil_temp !== undefined ? toDispTemp(Number(d.max_oil_temp)).toFixed(1) : '—');
      setDot('oil-temp-health', d.oil_temp_healthy, lbl('oil_temp'));
      if (d.oil_temp !== undefined) {
        const oilTempLimit = Number(d.oil_temp_limit || 0);
        setGaugeBar('oil-temp-gauge-bar', oilTempLimit > 0 ? Math.min(100, (d.oil_temp / oilTempLimit) * 100) : 0);
      }
    }
  }

  // TIT card
  const titCard = document.getElementById('tit-card');
  if (titCard) {
    titCard.style.display = d.has_tit ? '' : 'none';
    if (d.has_tit) {
      setText('tit', d.tit !== undefined ? toDispTemp(Number(d.tit)).toFixed(0) : '—');
      setText('max-tit', d.max_tit !== undefined ? toDispTemp(Number(d.max_tit)).toFixed(0) : '—');
      setDot('tit-health', d.tit_healthy, lbl('tit'));
      if (d.tit !== undefined) {
        const titLimit = Number(d.tit_limit || 0);
        setGaugeBar('tit-gauge-bar', titLimit > 0 ? Math.min(100, (d.tit / titLimit) * 100) : 0);
        const warn = document.getElementById('tit-approach-warn');
        if (warn) {
          const limit = Number(d.egt_limit || titLimit);
          const primaryPct = limit > 0 ? Math.min(100, (Number(d.tit) / limit) * 100) : 0;
          const show = isPrimaryTit && primaryPct >= 85;
          warn.style.display = show ? '' : 'none';
          if (show) warn.textContent = 'Warning: ' + lbl('tit') + ' at ' + primaryPct.toFixed(0) + '% - '
            + toDispTemp(Number(d.tit)).toFixed(0) + ' / ' + toDispTemp(limit).toFixed(0) + ' ' + dispTempUnit();
        }
        const absLbl = document.getElementById('tit-abs-label');
        if (absLbl) absLbl.textContent = titLimit > 0
          ? toDispTemp(Number(d.tit)).toFixed(0) + ' / ' + toDispTemp(titLimit).toFixed(0) + ' ' + dispTempUnit()
          : toDispTemp(Number(d.tit)).toFixed(0) + ' ' + dispTempUnit() + ' / OFF';
      } else {
        const warn = document.getElementById('tit-approach-warn');
        if (warn) warn.style.display = 'none';
      }
    }
  }

  // Fuel pressure card
  const fuelPressCard = document.getElementById('fuel-press-card');
  if (fuelPressCard) {
    fuelPressCard.style.display = d.has_fuel_press ? '' : 'none';
    if (d.has_fuel_press) {
      setText('fuel-press', d.fuel_press !== undefined ? toDispPress(Number(d.fuel_press)).toFixed(2) : '—');
      setText('max-fuel-press', d.max_fuel_press !== undefined ? toDispPress(Number(d.max_fuel_press)).toFixed(2) : '—');
      setDot('fuel-press-health', d.fuel_press_healthy, lbl('fuel_press'));
      if (d.fuel_press !== undefined) {
        const fuelPressMin = Number(d.fuel_press_min || 0);
        // Gauge: 0% = at min threshold, 100% = 3× min (typical healthy range)
        const pct = fuelPressMin > 0 ? Math.min(100, Math.max(0,
          ((d.fuel_press - fuelPressMin) / (fuelPressMin * 2)) * 100)) : 0;
        setGaugeBar('fuel-press-gauge-bar', pct);
        const absLbl = document.getElementById('fuel-press-abs-label');
        if (absLbl) absLbl.textContent = fuelPressMin > 0
          ? toDispPress(Number(d.fuel_press)).toFixed(2) + ' / ≥' + toDispPress(fuelPressMin).toFixed(2) + ' ' + dispPressUnit()
          : toDispPress(Number(d.fuel_press)).toFixed(2) + ' ' + dispPressUnit() + ' / OFF';
      }
    }
  }

  // Battery voltage card
  const battCard = document.getElementById('batt-card');
  if (battCard) {
    battCard.style.display = d.has_batt_voltage ? '' : 'none';
    if (d.has_batt_voltage) {
      setText('batt-voltage', d.batt_voltage !== undefined ? Number(d.batt_voltage).toFixed(2) : '—');
      setText('max-batt-voltage', d.max_batt_voltage !== undefined ? Number(d.max_batt_voltage).toFixed(2) : '—');
      setDot('batt-health', d.batt_healthy, 'Battery voltage');
      {
        const battMin = Number(d.batt_volt_min || 0);
        setText('batt-volt-min', battMin > 0 ? battMin.toFixed(1) : 'OFF');
        if (d.batt_voltage !== undefined) {
          const v = Number(d.batt_voltage);
          // 0% width = at alarm threshold, 100% = 30% above threshold (typical full-charge headroom).
          const fullV = battMin * 1.3;
          const pct = battMin > 0 ? Math.min(100, Math.max(0,
            ((v - battMin) / (fullV - battMin)) * 100)) : 0;
          // Battery is inverted vs the temp/RPM gauges: a FULL pack is the good
          // state (green), a near-empty pack is the fault (red). Colour is forced
          // by voltage thresholds so a full battery never renders red. Optional
          // over-voltage (charger fault / wrong cell count) also flags once the
          // pack climbs meaningfully above the full reference.
          let cls = 'ok';
          if (battMin > 0) {
            if (v <= battMin)            cls = 'danger';   // at/below undervoltage alarm
            else if (v < battMin * 1.1)  cls = 'warn';     // within 10% of the alarm
            else if (v > fullV * 1.08)   cls = 'danger';   // over-voltage (well above full)
            else if (v > fullV)          cls = 'warn';     // slightly over full headroom
          }
          setGaugeBar('batt-gauge-bar', pct, battMin > 0 ? cls : 'ok');
        }
      }
    }
  }

  // Torque / shaft power card
  const torqueCard = document.getElementById('torque-card');
  if (torqueCard) {
    torqueCard.style.display = d.has_torque ? '' : 'none';
    if (d.has_torque) {
      setText('torque', d.torque !== undefined ? Number(d.torque).toFixed(1) : '—');
      setDot('torque-health', d.torque_healthy, 'Torque sensor');
      if (d.has_n2 && d.n2_healthy !== false && d.turbo_power_w !== undefined && d.turbo_power_w !== null) {
        const kw = Number(d.turbo_power_w) / 1000;
        setText('turbo-power', kw.toFixed(2));
      } else {
        setText('turbo-power', 'N2 required');
      }
    }
  }

  // Glow plug current card
  const glowCurCard = document.getElementById('glow-current-card');
  if (glowCurCard) {
    glowCurCard.style.display = d.has_glow_current ? '' : 'none';
    if (d.has_glow_current) {
      setText('glow-current-val', d.glow_current_amps !== undefined ? Number(d.glow_current_amps).toFixed(1) : '—');
      const hot = !!d.glow_plug_hot;
      // Not-hot is a normal state (plug off or still heating), not a sensor
      // fault — render neutral instead of the red fault dot.
      setDot('glow-hot-dot', hot ? true : null);   // set class only; title managed below
      const ghDot = document.getElementById('glow-hot-dot');
      if (ghDot) ghDot.title = hot ? 'Glow plug — HOT (ready)' : 'Glow plug — cold / off';
      setText('glow-hot-label', hot ? 'HOT — ready' : 'not hot');
    }
  }

  // Igniter 1 / coil current card
  const ignCurCard = document.getElementById('igniter-current-card');
  if (ignCurCard) {
    ignCurCard.style.display = d.has_igniter_current ? '' : 'none';
    if (d.has_igniter_current) {
      setText('igniter-current-val', d.igniter_current_amps !== undefined ? Number(d.igniter_current_amps).toFixed(1) : '—');
    }
  }

  // Igniter 2 / AB coil current card
  const ign2CurCard = document.getElementById('igniter2-current-card');
  if (ign2CurCard) {
    ign2CurCard.style.display = d.has_igniter2_current ? '' : 'none';
    if (d.has_igniter2_current) {
      setText('igniter2-current-val', d.igniter2_current_amps !== undefined ? Number(d.igniter2_current_amps).toFixed(1) : '—');
    }
  }

  // Oil pump current card
  const oilpCurCard = document.getElementById('oilpump-current-card');
  if (oilpCurCard) {
    oilpCurCard.style.display = d.has_oil_pump_current ? '' : 'none';
    if (d.has_oil_pump_current) {
      setText('oilpump-current-val', d.oil_pump_current_amps !== undefined ? Number(d.oil_pump_current_amps).toFixed(1) : '—');
      const oc = !!d.oil_pump_overcurrent;
      setDot('oilpump-oc-dot', !oc);         // set class only; title managed below
      const ocDot = document.getElementById('oilpump-oc-dot');
      if (ocDot) ocDot.title = oc ? 'Oil pump current — ⚠ OVERCURRENT' : 'Oil pump current — OK';
      setText('oilpump-oc-label', oc ? '⚠ OVERCURRENT' : 'OK');
    }
  }

  // Fuel flow card
  const fuelFlowCard = document.getElementById('fuel-flow-card');
  if (fuelFlowCard) {
    fuelFlowCard.style.display = d.has_fuel_flow ? '' : 'none';
    if (d.has_fuel_flow) {
      const ffOk = d.fuel_flow_healthy !== false;
      setText('fuel-flow-val', d.fuel_flow !== undefined && ffOk ? Number(d.fuel_flow).toFixed(2) : '—');
      const ffEl = document.getElementById('fuel-flow-val');
      if (ffEl) ffEl.title = ffOk ? '' : 'Fuel flow sensor fault (railed/disconnected) — check wiring';
    }
  }

  // ── General-purpose DI channel states ─────────────────────
  if (d.di_channels) {
    const wrap = document.getElementById('di-states-wrap');
    if (wrap) {
      const anyConfigured = d.di_channels.some(ch => ch.pin >= 0);
      wrap.style.display = anyConfigured ? 'flex' : 'none';
      d.di_channels.forEach((ch, i) => {
        if (ch.pin < 0) {
          const old = document.getElementById('di-badge-' + i);
          if (old) old.remove();
          return;
        }
        let el = document.getElementById('di-badge-' + i);
        if (!el) {
          el = document.createElement('span');
          el.id = 'di-badge-' + i;
          el.className = 'di-badge';
          wrap.appendChild(el);
        }
        const name = (ch.label && ch.label.length) ? ch.label : ('DI-' + (i + 1));
        el.textContent = name + ': ' + (ch.state ? 'ACTIVE' : 'off');
        // Color by role semantics: only fault/estop are alarming when active.
        // An active arm switch / interlock / start-inhibit is a normal state
        // and must not look like a fault.
        const alarmRole = ch.role === 'fault' || ch.role === 'estop';
        const activeColor = alarmRole ? 'var(--red)' : 'var(--yellow)';
        el.style.color = ch.state ? activeColor : 'var(--dim)';
        el.style.borderColor = ch.state ? activeColor : 'var(--border)';
      });
    }
  }

  // Surge warning banner
  const surgeBanner = document.getElementById('surge-warn-banner');
  if (surgeBanner) surgeBanner.style.display = d.surge_detected ? '' : 'none';

  // Governor status section
  const govSection = document.getElementById('governor-section');
  if (govSection) {
    govSection.style.display = d.has_governor ? '' : 'none';
    if (d.has_governor) {
      setText('gov-target-rpm', d.governor_target_rpm !== undefined ? fmtInt(d.governor_target_rpm) : '—');
      setText('gov-n2-actual',  d.n2 !== undefined ? fmtInt(d.n2) : '—');
      const govMode = document.getElementById('gov-mode');
      if (govMode) {
        if (d.governor_mode) {
          govMode.textContent = d.governor_mode === 'pitch' ? 'PROP-PITCH' : 'THROTTLE-DRIVEN';
          govMode.title = d.governor_mode === 'pitch'
            ? 'Holds N2 by adjusting propeller pitch/load — you set power with the throttle.'
            : 'Holds N2 by winding fuel/throttle directly — the governor owns the throttle.';
          govMode.style.display = '';
        } else {
          govMode.style.display = 'none';
        }
      }
    }
  }

  // ── Advanced actuators section (glow, bleed, prop pitch, fuel pump 2, fan, airstarter, scavenge)
  const advActSection = document.getElementById('adv-act-section');
  if (advActSection) {
    const anyAdv = d.has_starter   || d.has_starter_en || d.has_fuel_sol  || d.has_igniter
                || d.has_igniter2  || d.has_glow_plug  || d.has_bleed_valve || d.has_prop_pitch || d.has_fuel_pump2
                || d.has_cool_fan  || d.has_airstarter  || d.has_oil_scavenge;
    advActSection.style.display = anyAdv ? '' : 'none';
    const actuatorIsRelay = type => Number(type) === 2;

    const advStarter = document.getElementById('adv-starter');
    if (advStarter) {
      advStarter.style.display = d.has_starter ? '' : 'none';
      if (d.has_starter && d.starter_demand !== undefined) {
        const pct = Math.round(Number(d.starter_demand) * 100);
        const relay = actuatorIsRelay(d.starter_type);
        setText('starter-pct', relay ? (pct > 0 ? 'ON' : 'OFF') : pct);
        setText('starter-unit', relay ? '' : '%');
        setGaugeBar('starter-gauge-bar', relay ? (pct > 0 ? 100 : 0) : pct);
      }
    }

    const advStarterEn = document.getElementById('adv-starter-en');
    if (advStarterEn) {
      advStarterEn.style.display = d.has_starter_en ? '' : 'none';
      if (d.has_starter_en) setText('starter-en-state', d.starter_enabled ? 'ON' : 'OFF');
    }

    const advFuelSol = document.getElementById('adv-fuel-sol');
    if (advFuelSol) {
      advFuelSol.style.display = d.has_fuel_sol ? '' : 'none';
      if (d.has_fuel_sol) setText('fuel-sol-state', d.fuel_sol_open ? 'OPEN' : 'CLOSED');
    }

    const advIgniter = document.getElementById('adv-igniter');
    if (advIgniter) {
      advIgniter.style.display = d.has_igniter ? '' : 'none';
      if (d.has_igniter) setText('igniter-state', d.igniter_on ? 'ON' : 'OFF');
    }

    const advIgniter2 = document.getElementById('adv-igniter2');
    if (advIgniter2) {
      advIgniter2.style.display = d.has_igniter2 ? '' : 'none';
      if (d.has_igniter2) setText('igniter2-state', d.igniter2_on ? 'ON' : 'OFF');
    }

    const advGlow = document.getElementById('adv-glow');
    if (advGlow) {
      advGlow.style.display = d.has_glow_plug ? '' : 'none';
      if (d.has_glow_plug && d.glow_plug_pct !== undefined) {
        const relay = Number(d.glow_plug_output_type || 0) === 1;
        setText('glow-pct', relay ? (Number(d.glow_plug_pct) > 0 ? 'ON' : 'OFF') : Math.round(d.glow_plug_pct));
        setText('glow-unit', relay ? '' : '%');
        setGaugeBar('glow-gauge-bar', relay ? (Number(d.glow_plug_pct) > 0 ? 100 : 0) : d.glow_plug_pct);
        const wetGlowFuel = document.getElementById('wet-glow-fuel-wrap');
        if (wetGlowFuel) wetGlowFuel.style.display = d.has_wet_glow ? '' : 'none';
        if (d.has_wet_glow && d.wet_glow_fuel_pct !== undefined) {
          const wetRelay = Number(d.wet_glow_fuel_type ?? 0) === 0;
          const wetPct = Number(d.wet_glow_fuel_pct);
          setText('wet-glow-fuel-pct', wetRelay ? (wetPct > 0 ? 'ON' : 'OFF') : Math.round(wetPct));
          setText('wet-glow-fuel-unit', wetRelay ? '' : '%');
        }
      }
    }

    const advBleed = document.getElementById('adv-bleed');
    if (advBleed) {
      advBleed.style.display = d.has_bleed_valve ? '' : 'none';
      if (d.has_bleed_valve) setText('bleed-state', d.bleed_valve_open ? 'OPEN' : 'CLOSED');
    }

    const advPitch = document.getElementById('adv-pitch');
    if (advPitch) {
      advPitch.style.display = d.has_prop_pitch ? '' : 'none';
      if (d.has_prop_pitch && d.prop_pitch_demand !== undefined) {
        const pct = Math.round(d.prop_pitch_demand * 100);
        const relay = actuatorIsRelay(d.prop_pitch_type);
        setText('pitch-pct', relay ? (pct > 0 ? 'ON' : 'OFF') : pct);
        setText('pitch-unit', relay ? '' : '%');
        setGaugeBar('pitch-gauge-bar', relay ? (pct > 0 ? 100 : 0) : pct);
      }
    }

    const advFp2 = document.getElementById('adv-fp2');
    if (advFp2) {
      advFp2.style.display = d.has_fuel_pump2 ? '' : 'none';
      if (d.has_fuel_pump2 && d.fuel_pump2_demand !== undefined) {
        const pct = Math.round(d.fuel_pump2_demand * 100);
        const relay = actuatorIsRelay(d.fuel_pump2_type);
        setText('fp2-pct', relay ? (pct > 0 ? 'ON' : 'OFF') : pct);
        setText('fp2-unit', relay ? '' : '%');
        setGaugeBar('fp2-gauge-bar', relay ? (pct > 0 ? 100 : 0) : pct);
      }
    }

    const advFan = document.getElementById('adv-coolfan');
    if (advFan) {
      advFan.style.display = d.has_cool_fan ? '' : 'none';
      if (d.has_cool_fan) setText('coolfan-state', d.cool_fan_on ? 'ON' : 'OFF');
    }

    const advAir = document.getElementById('adv-airstarter');
    if (advAir) {
      advAir.style.display = d.has_airstarter ? '' : 'none';
      if (d.has_airstarter) setText('airstarter-state', d.airstarter_open ? 'OPEN' : 'CLOSED');
    }

    const advScav = document.getElementById('adv-scavenge');
    if (advScav) {
      advScav.style.display = d.has_oil_scavenge ? '' : 'none';
      if (d.has_oil_scavenge) setText('scavenge-state', d.oil_scavenge_on ? 'ON' : 'OFF');
    }
  }

  // ── Afterburner card
  const abSection = document.getElementById('ab-section');
  if (abSection) {
    const hasAB = !!d.has_afterburner;
    abSection.style.display = hasAB ? '' : 'none';
    if (hasAB) {
      const abMode = d.ab_mode || 'Off';
      const modeEl = document.getElementById('ab-mode-val');
      if (modeEl) {
        modeEl.textContent = abMode.toUpperCase();
        modeEl.className   = 'ab-mode-val ab-mode-' + abMode;
      }
      // These are normal boolean STATES, not sensor health: closed solenoid /
      // arm off / trigger idle must render neutral, not red-fault. Titles are
      // set manually so the generic "sensor fault (check wiring)" text never
      // appears for a normal inactive state.
      setDot('ab-sol-dot',  d.ab_sol_open      ? true : null);
      setDot('ab-arm-dot',  d.ab_arm_switch_on ? true : null);
      setDot('ab-trig-dot', d.ab_trigger_active ? true : null);
      const _abT = (id, txt) => { const e = document.getElementById(id); if (e) e.title = txt; };
      _abT('ab-sol-dot',  'AB solenoid — ' + (d.ab_sol_open ? 'OPEN' : 'closed'));
      _abT('ab-arm-dot',  'AB arm switch — ' + (d.ab_arm_switch_on ? 'ARMED' : 'off'));
      _abT('ab-trig-dot', 'AB trigger — ' + (d.ab_trigger_active ? 'ACTIVE' : 'idle'));
      // AB flame: red only when the AB should be lit but no flame is seen —
      // that is a real cue; otherwise neutral (green when burning).
      const abExpectFlame = abMode === 'Igniting' || abMode === 'Running';
      setDot('ab-flame-dot', d.has_ab_flame === false ? null
                            : (d.ab_flame_on ? true : (abExpectFlame ? false : null)));
      _abT('ab-flame-dot', d.has_ab_flame === false ? 'AB flame sensor not fitted'
                          : (d.ab_flame_on ? 'AB flame — confirmed'
                             : (abExpectFlame ? 'AB flame — NOT DETECTED while lit' : 'AB flame — no flame')));
      if (d.ab_pump_demand !== undefined) setText('ab-pump-demand', Math.round(Number(d.ab_pump_demand) * 100));
      const abOffset = Number(d.ab_fuel_offset || 0);
      const abOffsetRow = document.getElementById('ab-fuel-offset-row');
      if (abOffsetRow) abOffsetRow.style.display = Math.abs(abOffset) > 0.001 ? '' : 'none';
      setText('ab-fuel-offset', Math.round(abOffset * 100));

      // Manual FIRE is only meaningful when Hardware trigger source is Manual command only.
      const manualAb = Number(d.ab_trigger_source ?? 0) === 0;
      const canFire = manualAb && d.mode === 'RUNNING' && (abMode === 'Off' || abMode === 'Fault');
      setDisabled('btn-ab-fire', !canFire);
      // STOP: only enabled when AB is active
      const abActive = abMode === 'Arming' || abMode === 'Igniting' || abMode === 'Running' || abMode === 'ShuttingDown';
      setDisabled('btn-ab-stop', !abActive);
    }
  }

  // ── Post-run summary + sequence timeline tracking ─────────
  _trackRunState(d);
  return d;
}

// ── Run state tracking ────────────────────────────────────────
let _prevMode        = null;
let _runStartMs      = null;
let _seqTimeline     = [];    // [{name, durationMs}] completed startup blocks
let _seqLastBlockIdx = -1;
let _seqBlockStart   = null;
let _seqCurBlockName = null;

function _trackRunState(d) {
  const mode = d.mode;
  const now  = Date.now();

  // ── Sequence timeline — track block transitions during STARTUP ──
  if (mode === 'STARTUP') {
    if (_prevMode !== 'STARTUP') {
      // New sequence starting — reset timeline
      _seqTimeline     = [];
      _seqLastBlockIdx = -1;
      _seqBlockStart   = now;
      _seqCurBlockName = d.current_block || '—';
    }
    const idx = d.seq_block_idx !== undefined ? d.seq_block_idx : -1;
    if (idx > _seqLastBlockIdx) {
      if (_seqLastBlockIdx >= 0) {
        // Previous block just completed — record it
        _seqTimeline.push({ name: _seqCurBlockName, durationMs: now - (_seqBlockStart || now) });
        _renderLiveTimeline();
      }
      _seqLastBlockIdx = idx;
      _seqCurBlockName = d.current_block || '—';
      _seqBlockStart   = now;
    }
  }

  // Live timeline strip inside seq-progress section
  const liveStrip = document.getElementById('seq-timeline-strip');
  if (liveStrip) {
    if (mode === 'STARTUP' && _seqTimeline.length > 0) {
      liveStrip.style.display = 'flex';
      _renderLiveTimeline();
    } else {
      liveStrip.style.display = 'none';
    }
  }

  // ── Post-run summary on transition back to idle ────────────
  const wasActive = _prevMode === 'RUNNING' || _prevMode === 'STARTUP' || _prevMode === 'SHUTDOWN';
  const nowIdle   = mode === 'STANDBY' || mode === 'FAULT';
  if (wasActive && nowIdle && _runStartMs !== null) {
    // Finalise timeline — push last block if we were in STARTUP
    if (_prevMode === 'STARTUP' && _seqCurBlockName) {
      _seqTimeline.push({ name: _seqCurBlockName, durationMs: now - (_seqBlockStart || now) });
    }
    _showRunSummary(d, now - _runStartMs);
    _runStartMs = null;
  }

  // Track engine start (STANDBY→STARTUP/RUNNING)
  if ((_prevMode === 'STANDBY' || _prevMode === 'FAULT' || _prevMode === null)
      && (mode === 'STARTUP' || mode === 'RUNNING')) {
    _runStartMs = now;
  }

  _prevMode = mode;
}

function _renderLiveTimeline() {
  const strip = document.getElementById('seq-timeline-strip');
  if (!strip) return;
  strip.innerHTML = _seqTimeline.map(b => _timelinePill(b)).join('');
}

// Escape user-influenced strings (e.g. custom sequence block names) before
// interpolating into innerHTML. Shared site-wide: app.js loads before every
// page's inline script, so pages use this instead of defining their own copy.
function escapeHtmlText(v) {
  return String(v)
    .replace(/&/g, '&amp;').replace(/</g, '&lt;')
    .replace(/>/g, '&gt;').replace(/"/g, '&quot;')
    .replace(/'/g, '&#39;');
}
const _escapeHtml = escapeHtmlText;  // internal alias (pre-unification name)

function _timelinePill(b) {
  const s = (b.durationMs / 1000).toFixed(1);
  return `<span style="font-size:.7rem;padding:.15rem .5rem;border-radius:3px;` +
    `background:rgba(255,255,255,.06);border:1px solid var(--border);` +
    `color:var(--dim);white-space:nowrap">${_escapeHtml(b.name)} ` +
    `<span style="color:var(--text)">${s}s</span></span>`;
}

function _showRunSummary(d, durationMs) {
  const card = document.getElementById('run-summary-card');
  if (!card) return;

  const faultText = String(d.fault_description || '').trim();
  const lastEvent = String(d.last_event || '');
  const isAbort = lastEvent.startsWith('Aborted') || faultText.startsWith('Startup aborted');
  const isFault  = d.mode === 'FAULT' || isAbort || faultText.length > 0 || lastEvent.startsWith('FAULT:');
  const titleEl  = document.getElementById('run-summary-title');
  if (titleEl) {
    titleEl.textContent = isFault ? (isAbort ? 'Run ended - Abort' : 'Run ended - Fault') : 'Run complete';
    titleEl.style.color = isFault ? 'var(--red)' : 'var(--green)';
  }

  const mins = Math.floor(durationMs / 60000);
  const secs = Math.round((durationMs % 60000) / 1000);
  const durStr = mins > 0 ? mins + 'm ' + secs + 's' : secs + 's';

  const stats = [
    { label: 'Duration', value: durStr },
    (d.has_n1 && d.max_n1 !== undefined) ? { label: 'Peak N1',  value: fmtInt(d.max_n1) + ' RPM' } : null,
    (d.has_tot && d.max_tot !== undefined) ? { label: 'Peak TOT', value: toDispTemp(Number(d.max_tot)).toFixed(0) + ' ' + dispTempUnit() } : null,
    (d.has_tit && d.max_tit !== undefined) ? { label: 'Peak TIT', value: toDispTemp(Number(d.max_tit)).toFixed(0) + ' ' + dispTempUnit() } : null,
    (d.has_oil_press && d.oil_min_bar !== undefined && Number(d.oil_min_bar) > 0)
      ? { label: 'Min oil', value: toDispPress(Number(d.oil_min_bar)).toFixed(2) + ' ' + dispPressUnit() } : null,
  ].filter(Boolean);

  const statsEl = document.getElementById('run-summary-stats');
  if (statsEl) {
    statsEl.innerHTML = stats.map(s =>
      `<span><span style="color:var(--dim)">${_escapeHtml(s.label)}:</span> <strong>${_escapeHtml(s.value)}</strong></span>`
    ).join('');
    if (isFault && faultText.length > 0) {
      const line = faultText.split('\n')[0].slice(0, 120);
      const faultEl = document.createElement('div');
      faultEl.style.cssText = 'width:100%;color:var(--red);font-size:.78rem;margin-top:.3rem';
      faultEl.textContent = (isAbort ? 'Abort: ' : 'Fault: ') + line;
      statsEl.appendChild(faultEl);
    }
  }

  // Sequence timeline
  const tlSection = document.getElementById('run-summary-timeline');
  const tlStrip   = document.getElementById('run-summary-timeline-strip');
  if (tlSection && tlStrip) {
    if (_seqTimeline.length > 0) {
      tlStrip.innerHTML = _seqTimeline.map(b => _timelinePill(b)).join('');
      tlSection.style.display = '';
    } else {
      tlSection.style.display = 'none';
    }
  }

  card.style.display = '';
}

function dismissRunSummary() {
  const card = document.getElementById('run-summary-card');
  if (card) card.style.display = 'none';
}

function setText(id, val) {
  const el = document.getElementById(id);
  if (el) el.textContent = val;
}

function setDot(id, ok, tooltip) {
  const el = document.getElementById(id);
  if (!el) return;
  // null/undefined → neutral gray dot (sensor not relevant in this mode)
  if (ok === null || ok === undefined) {
    el.className = 'dot';
    if (tooltip !== undefined) el.title = tooltip ? tooltip + ' — standby' : '';
  } else {
    el.className = 'dot ' + (ok ? 'ok' : 'fault');
    if (tooltip !== undefined) {
      el.title = ok
        ? tooltip + ' — OK'
        : tooltip + ' — sensor fault (check wiring)';
    }
  }
}

function setDisabled(id, dis) {
  const el = document.getElementById(id);
  if (el) el.disabled = dis;
}

function setHwActive(id, active) {
  const el = document.getElementById(id);
  if (!el) return;
  if (active) el.classList.add('hw-active');
  else        el.classList.remove('hw-active');
}

function setGaugeBar(id, pct, forceClass) {
  const bar = document.getElementById(id);
  if (!bar) return;
  const clamped = Math.min(100, Math.max(0, pct));
  bar.style.width = clamped + '%';
  // forceClass decouples color from width for gauges whose danger direction
  // is inverted (oil: LOW pressure is the dangerous state, not high).
  if (forceClass !== undefined) {
    bar.className = forceClass === 'ok' ? 'gauge-bar' : 'gauge-bar ' + forceClass;
    return;
  }
  if (pct >= 95) { bar.className = 'gauge-bar danger'; }
  else if (pct >= 80) { bar.className = 'gauge-bar warn'; }
  else { bar.className = 'gauge-bar'; }
}

function formatUptime(s) {
  const h = Math.floor(s / 3600);
  const m = Math.floor((s % 3600) / 60);
  const sec = s % 60;
  if (h > 0) return h + 'h ' + m + 'm';
  if (m > 0) return m + 'm ' + sec + 's';
  return sec + 's';
}

// ── REST helpers ──────────────────────────────────────────────
function sendCmd(url) {
  return fetch(url, { method: 'POST' })
    .then(async r => {
      let d = {};
      try { d = await r.json(); } catch {}
      if (!r.ok || !d.ok) {
        const msg = d.error || ('HTTP ' + r.status);
        console.warn('Command failed', d);
        alert(msg);
      }
      return d;
    })
    .catch(e => {
      console.error('Network error', e);
      alert('Network error: ' + e.message);
      return { ok:false, error:e.message };
    });
}

function sendAbCmd(cmd) {
  fetch('/api/command', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ cmd })
  })
    .then(async r => {
      const d = await r.json().catch(() => ({}));
      if (!r.ok || d.ok === false) throw new Error(d.error || d.reason || ('HTTP ' + r.status));
    })
    .catch(e => alert('Afterburner command failed: ' + e.message));
}

// Clear all session peak values (max N1/N2/TOT/TIT/pressures/battery) — the
// firmware command existed but had no web control, so a bench spike could
// only be cleared by reboot or cluster command.
function resetPeaks() {
  if (!confirm('Reset all session peak values (max RPM, temperatures, pressures)?')) return;
  fetch('/api/command', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ cmd: 'RESET_PEAKS' })
  })
    .then(async r => {
      const d = await r.json().catch(() => ({}));
      if (!r.ok || d.ok === false) throw new Error(d.error || d.reason || ('HTTP ' + r.status));
      requestTelemetryNow();
    })
    .catch(e => alert('Peak reset failed: ' + e.message));
}

// ── Boot: prime dashboard via REST for instant first paint, then WS takes over ─
applyUnitLabels();
organizeDashboardCards();
document.addEventListener('DOMContentLoaded', () => {
  applyContextTooltips();
  new MutationObserver(records => {
    records.forEach(record => record.addedNodes.forEach(node => {
      if (node.nodeType === 1) applyContextTooltips(node);
    }));
  }).observe(document.body, { childList: true, subtree: true });
});
window.addEventListener('focus', requestTelemetryNow);
window.addEventListener('pageshow', (e) => {
  // bfcache restore (iOS Safari back/forward): pagehide tore down the WS and
  // all timers, but the DOM comes back showing the old green Connected dot.
  // Restart whichever telemetry path this page uses. Only on real restores
  // (e.persisted) — a normal first show must not defeat the staggered
  // startTelemetryBoot() below.
  if (!e.persisted) return;
  if (usesGlobalTelemetry()) {
    if (!ws) {
      setConnectionState(false, 'Reconnecting - values retained');
      connect();
      startRestFallbackTimer();
    }
    requestTelemetryNow();
  } else if (!hasPageLocalTelemetry()) {
    startStatusHeartbeat();
  }
});
document.addEventListener('visibilitychange', () => {
  if (!document.hidden) requestTelemetryNow();
});
window.addEventListener('pagehide', stopGlobalTelemetry);
window.addEventListener('beforeunload', stopGlobalTelemetry);
function startTelemetryBoot() {
  if (!usesGlobalTelemetry()) {
    if (!hasPageLocalTelemetry()) startStatusHeartbeat();
    return;
  }
  connect();
  startRestFallbackTimer();
  if (isDashboardPage()) {
    fetch('/api/data', { cache: 'no-store' })
      .then(r => r.json())
      .then(d => { try { applyData(d); } catch(e) {} })
      .catch(() => {});
  }
}
window.startTelemetryBoot = startTelemetryBoot;
function scheduleTelemetryBoot() {
  if (pageStartsTelemetryAfterSetup()) return;
  const delay = isDashboardPage() ? 0 : 400;
  const boot = () => setTimeout(startTelemetryBoot, delay);
  if (document.readyState === 'complete') boot();
  else window.addEventListener('load', boot, { once: true });
}
scheduleTelemetryBoot();

// Show banner if startup sequence is empty (checked once at page load)
const _emptySeqBanner = document.getElementById('empty-seq-banner');
if (_emptySeqBanner) {
  fetch('/api/hardware')
    .then(r => r.json())
    .then(hw => {
      _emptySeqBanner.style.display = (hw.startup_seq && hw.startup_seq.length > 0) ? 'none' : '';
    })
    .catch(() => {});
}

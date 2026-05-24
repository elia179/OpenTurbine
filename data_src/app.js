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
function toDispPress(b)  { return pressUnit() === 'psi' ? b * 14.5038   : b; }
function fromDispPress(v){ return pressUnit() === 'psi' ? v / 14.5038   : v; }
function dispTempUnit()  { return tempUnit()  === 'F'   ? '°F' : '°C'; }
function dispPressUnit() { return pressUnit() === 'psi' ? 'PSI' : 'bar'; }
function applyUnitLabels() {
  document.querySelectorAll('[data-unit="temp"]').forEach( el => el.textContent = dispTempUnit());
  document.querySelectorAll('[data-unit="press"]').forEach(el => el.textContent = dispPressUnit());
  const bt = document.getElementById('unit-temp-btn');
  if (bt) bt.textContent = tempUnit()  === 'C'   ? '°F'  : '°C';
  const bp = document.getElementById('unit-press-btn');
  if (bp) bp.textContent = pressUnit() === 'bar' ? 'PSI' : 'bar';
}

// ── Sparkline circular buffers ────────────────────────────────
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
  ctx.strokeStyle = color;
  ctx.lineWidth = 1.5;
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
// client every 500 ms.  This ensures the push runs inside the async_tcp task
// context (WS_EVT_DATA callback) rather than from webTask — eliminating the
// cross-task notification lag that caused 5-20 s burst-then-silence behaviour.
//
// setInterval at 500 ms is reliable for a visible foreground tab; if the tab
// is backgrounded the rate drops to ~1 s which is acceptable for monitoring.
let ws = null;
let _lastMsgMs = 0;
let _lastConnectMs = 0;
let _pullTimer = null;

function connect() {
  if (ws && ws.readyState <= WebSocket.OPEN) return;
  _lastConnectMs = Date.now();
  ws = new WebSocket('ws://' + location.host + '/ws');

  ws.onopen = () => {
    document.getElementById('conn').className = 'conn-dot connected';
    const lbl = document.getElementById('conn-label');
    if (lbl) { lbl.textContent = 'Connected'; lbl.style.color = 'var(--green)'; }
    _lastMsgMs = Date.now();
    // Start sending pull requests — server responds with full telemetry each time.
    // Server also sends one frame on WS_EVT_CONNECT so the UI populates immediately.
    // Dashboard needs 10 Hz for smooth live gauges; other pages (log, config, etc.)
    // only need the connection indicator — 2 s is enough and keeps the ESP32 free
    // to serve page fetches without queuing behind a flood of WS events.
    const _pullMs = (location.pathname === '/' || location.pathname === '/index.html') ? 100 : 2000;
    _pullTimer = setInterval(() => {
      if (ws && ws.readyState === WebSocket.OPEN) ws.send('p');
    }, _pullMs);
  };

  ws.onclose = () => {
    if (_pullTimer) { clearInterval(_pullTimer); _pullTimer = null; }
    document.getElementById('conn').className = 'conn-dot disconnected';
    const lbl = document.getElementById('conn-label');
    if (lbl) { lbl.textContent = 'Disconnected'; lbl.style.color = 'var(--dim)'; }
    ['n1','n2','tot','oil','p1','p2','oil-temp','batt-voltage','torque'].forEach(id => {
      const el = document.getElementById(id); if (el) el.textContent = '—';
    });
    ['n1-approach-warn','tot-approach-warn','oil-approach-warn'].forEach(id => {
      const el = document.getElementById(id); if (el) el.style.display = 'none';
    });
    const wait = 1000 - (Date.now() - _lastConnectMs);
    if (wait <= 0) {
      connect();
    } else {
      setTimeout(connect, wait);
    }
  };

  ws.onerror = () => { ws.close(); };

  ws.onmessage = (ev) => {
    _lastMsgMs = Date.now();
    try { applyData(JSON.parse(ev.data)); } catch(e) {}
  };
}

// ── Apply telemetry frame to DOM ──────────────────────────────
let _lastData = null;
function applyData(d) {
  // Merge into _lastData rather than replace — fast frames only carry live
  // fields; slow fields (has_*, limits, max_oil_temp, etc.) must persist so
  // that applyData(_lastData) called by the unit-toggle buttons still has them.
  if (!_lastData) _lastData = {};
  Object.assign(_lastData, d);
  d = _lastData;
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
  // Coerce to Number so .toFixed/.toLocaleString always work even if JSON sent as int
  setText('n1',  d.n1  !== undefined ? Number(d.n1).toLocaleString()  : '—');
  setText('n2',  d.n2  !== undefined ? Number(d.n2).toLocaleString()  : '—');
  const n2Card = document.getElementById('n2-card');
  if (n2Card && d.has_n2 !== undefined) n2Card.style.display = d.has_n2 ? '' : 'none';
  setText('tot', d.tot !== undefined ? toDispTemp(Number(d.tot)).toFixed(1)       : '—');
  setText('max-n1',  d.max_n1  !== undefined ? Number(d.max_n1).toLocaleString() : '—');
  setText('max-n2',  d.max_n2  !== undefined ? Number(d.max_n2).toLocaleString() : '—');
  setText('max-tot', d.max_tot !== undefined ? toDispTemp(Number(d.max_tot)).toFixed(1) : '—');
  setText('oil', d.oil !== undefined ? toDispPress(Number(d.oil)).toFixed(2)       : '—');
  setText('oil-demand-val', d.oil_demand !== undefined ? toDispPress(Number(d.oil_demand)).toFixed(2) : '—');
  setText('throttle-demand', d.throttle_demand !== undefined
    ? (Number(d.throttle_demand) * 100).toFixed(1) + '%' : '—');
  // Throttle gauge bar
  if (d.throttle_demand !== undefined) {
    const gb = document.getElementById('throttle-gauge-bar');
    if (gb) gb.style.width = (Number(d.throttle_demand) * 100).toFixed(1) + '%';
  }
  // Physical throttle input display (when throttle input is configured)
  {
    const iRow = document.getElementById('throttle-input-row');
    const iVal = document.getElementById('throttle-input-pct');
    const hasInput = d.throttle_input_type && d.throttle_input_type !== 'none';
    if (iRow) iRow.style.display = hasInput ? '' : 'none';
    if (iVal && hasInput) {
      const rawNorm = d.throttle_input_raw !== undefined ? Number(d.throttle_input_raw) / 4095 : 0;
      const pct = (d.throttle_input_type === 'servo' && d.rc_throttle_norm !== undefined)
        ? (Number(d.rc_throttle_norm) * 100).toFixed(1)
        : (rawNorm * 100).toFixed(1);
      iVal.textContent = pct;
    }
  }
  setText('oil-pct',     d.oil_pct  !== undefined ? d.oil_pct + '%'          : '—');
  setText('uptime',      d.uptime_s !== undefined ? formatUptime(d.uptime_s)  : '—');
  setText('last-event',  d.last_event || '—');

  // Throttle sub-labels: idle floor + dynamic idle target
  if (d.idle_min_pct !== undefined) {
    setText('throttle-idle-floor', Number(d.idle_min_pct).toFixed(1));
  }
  const diWrap = document.getElementById('throttle-di-wrap');
  if (diWrap) {
    const showDi = d.dynamic_idle_enabled && d.mode === 'RUNNING';
    diWrap.style.display = showDi ? '' : 'none';
    if (showDi && d.idle_target_rpm !== undefined) {
      setText('throttle-di-rpm', Number(d.idle_target_rpm).toLocaleString());
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
  if (d.flame_raw !== undefined) {
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
  setText('p1', d.p1 !== undefined ? toDispPress(Number(d.p1)).toFixed(2) : '—');
  setText('p2', d.p2 !== undefined ? toDispPress(Number(d.p2)).toFixed(2) : '—');
  setText('max-p1', d.max_p1 !== undefined ? toDispPress(Number(d.max_p1)).toFixed(2) : '—');
  setText('max-p2', d.max_p2 !== undefined ? toDispPress(Number(d.max_p2)).toFixed(2) : '—');

  // Health dots
  // RPM health is only meaningful when the engine is running — zero RPM at standby is valid.
  // Pass null when not in an operational mode so the dot shows neutral (dim), not fault (red).
  const engineOp = (d.mode === 'RUNNING' || d.mode === 'STARTUP');
  setDot('n1-health',  engineOp ? d.n1_healthy : null, lbl('n1') + ' RPM');
  setDot('n2-health',  engineOp ? d.n2_healthy : null, lbl('n2') + ' RPM');
  setDot('tot-health', d.tot_healthy, lbl('tot'));
  setDot('oil-health', d.oil_healthy, lbl('oil_press'));
  setDot('flame-dot',  d.flame, 'Flame sensor');

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

  // Stop switch warning below start button
  const stopWarn = document.getElementById('stop-switch-warn');
  if (stopWarn) stopWarn.style.display = d.stop_switch_active ? '' : 'none';

  // Start/Stop buttons — disable + hardware glow when physical button is pressed
  const running = d.mode === 'RUNNING' || d.mode === 'STARTUP' || d.mode === 'SHUTDOWN';
  setDisabled('btn-start', running || d.mode === 'FAULT' || d.stop_switch_active);
  setDisabled('btn-stop',  !running);
  setHwActive('btn-start', !!d.start_switch_active);
  setHwActive('btn-stop',  !!d.stop_switch_active);

  // ── Sequence progress ─────────────────────────────────────
  const seqSection = document.getElementById('seq-progress-section');
  if (seqSection) {
    const inSeq = (d.mode === 'STARTUP' || d.mode === 'SHUTDOWN') && d.seq_block_total > 0;
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
  // legacy small error banner (kept for compat)
  const profErr = document.getElementById('profile-error');
  if (profErr) profErr.style.display = (d.profile_match === false) ? '' : 'none';

  // ── Config version mismatch banner ────────────────────────
  const verBanner = document.getElementById('config-version-banner');
  if (verBanner) verBanner.style.display = d.config_version_mismatch ? '' : 'none';

  // ── Hour meter ────────────────────────────────────────────
  if (d.run_count !== undefined) setText('hour-run-count', d.run_count);
  if (d.total_run_seconds !== undefined) {
    const hrs = Math.floor(d.total_run_seconds / 3600);
    const mins = Math.floor((d.total_run_seconds % 3600) / 60);
    setText('hour-run-time', hrs + 'h ' + String(mins).padStart(2, '0') + 'm');
  }

  // ── EGT rate of rise ──────────────────────────────────────
  if (d.tot_rise_rate !== undefined) {
    const rate = Number(d.tot_rise_rate);
    const rateEl = document.getElementById('tot-rise-rate-val');
    // Rise rate is a delta — only apply the scale factor (×9/5), not the +32 offset
    if (rateEl) rateEl.textContent = (tempUnit() === 'F' ? (rate * 9/5).toFixed(1) + ' °F/s'
                                                         : rate.toFixed(1) + ' °C/s');
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
        + Number(d.n1).toLocaleString() + ' / ' + Number(d.rpm_limit).toLocaleString() + ' RPM';
    }
    const absLbl = document.getElementById('n1-abs-label');
    if (absLbl) absLbl.textContent = Number(d.n1).toLocaleString() + ' / ' + Number(d.rpm_limit).toLocaleString() + ' RPM';
  }
  if (d.tot_limit && d.tot !== undefined) {
    const pct = Math.min(100, (d.tot / d.tot_limit) * 100);
    setGaugeBar('tot-gauge-bar', pct);
    const warn = document.getElementById('tot-approach-warn');
    if (warn) {
      const show = pct >= 85;
      warn.style.display = show ? '' : 'none';
      if (show) warn.textContent = '⚠ TOT at ' + pct.toFixed(0) + '% — '
        + toDispTemp(Number(d.tot)).toFixed(0) + ' / ' + toDispTemp(Number(d.tot_limit)).toFixed(0) + ' ' + dispTempUnit();
    }
    const absLbl = document.getElementById('tot-abs-label');
    if (absLbl) absLbl.textContent = toDispTemp(Number(d.tot)).toFixed(0) + ' / ' + toDispTemp(Number(d.tot_limit)).toFixed(0) + ' ' + dispTempUnit();
  }
  if (d.oil_running_min && d.oil !== undefined) {
    if (d.mode === 'RUNNING') {
      const pctForColor = d.oil < d.oil_running_min ? 0 : 80;
      setGaugeBar('oil-gauge-bar', pctForColor);
      const warn = document.getElementById('oil-approach-warn');
      if (warn) {
        const low = d.oil < d.oil_running_min * 1.15;
        warn.style.display = low ? '' : 'none';
        if (low) warn.textContent = '⚠ Oil ' + toDispPress(Number(d.oil)).toFixed(2)
          + ' ' + dispPressUnit() + ' — near min ' + toDispPress(Number(d.oil_running_min)).toFixed(2) + ' ' + dispPressUnit();
      }
    }
    const absLbl = document.getElementById('oil-abs-label');
    if (absLbl) absLbl.textContent = toDispPress(Number(d.oil)).toFixed(2) + ' / ≥' + toDispPress(Number(d.oil_running_min)).toFixed(2) + ' ' + dispPressUnit();
  }

  // ── Firmware version (shown once on first telemetry frame) ──
  if (d.fw_version) {
    const el = document.getElementById('fw-version');
    if (el && !el._set) { el.textContent = 'v' + d.fw_version; el._set = true; }
  }

  // ── Sparklines ────────────────────────────────────────────
  if (d.n1 !== undefined) {
    pushSparkline(_sparkN1, Number(d.n1));
    drawSparkline('n1-sparkline', _sparkN1, 'var(--green)');
  }
  if (d.tot !== undefined) {
    pushSparkline(_sparkTot, Number(d.tot));
    drawSparkline('tot-sparkline', _sparkTot, 'var(--yellow)');
  }
  if (d.has_oil_temp && d.oil_temp !== undefined) {
    pushSparkline(_sparkOilTemp, Number(d.oil_temp));
    drawSparkline('oil-temp-sparkline', _sparkOilTemp, 'var(--accent)');
  }
  if (d.has_batt_voltage && d.batt_voltage !== undefined) {
    pushSparkline(_sparkBattVolt, Number(d.batt_voltage));
    drawSparkline('batt-sparkline', _sparkBattVolt, 'var(--green)');
  }
  if (d.has_torque && d.torque !== undefined) {
    pushSparkline(_sparkTorque, Number(d.torque));
    drawSparkline('torque-sparkline', _sparkTorque, 'var(--accent)');
  }

  // ── Extended sensors (oil temp, battery, torque, current sensors) ──
  const extSection = document.getElementById('ext-sensors-section');
  if (extSection) {
    const anyExt = d.has_oil_temp || d.has_batt_voltage || d.has_torque
                || d.has_glow_current || d.has_igniter_current || d.has_igniter2_current
                || d.has_oil_pump_current
                || d.has_fuel_flow || d.has_tit || d.has_fuel_press;
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
      if (d.oil_temp_limit && d.oil_temp_limit > 0 && d.oil_temp !== undefined) {
        setGaugeBar('oil-temp-gauge-bar', Math.min(100, (d.oil_temp / d.oil_temp_limit) * 100));
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
      if (d.tit_limit && d.tit_limit > 0 && d.tit !== undefined) {
        setGaugeBar('tit-gauge-bar', Math.min(100, (d.tit / d.tit_limit) * 100));
        const absLbl = document.getElementById('tit-abs-label');
        if (absLbl) absLbl.textContent = toDispTemp(Number(d.tit)).toFixed(0) + ' / ' + toDispTemp(Number(d.tit_limit)).toFixed(0) + ' ' + dispTempUnit();
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
      if (d.fuel_press_min && d.fuel_press_min > 0 && d.fuel_press !== undefined) {
        // Gauge: 0% = at min threshold, 100% = 3× min (typical healthy range)
        const pct = Math.min(100, Math.max(0,
          ((d.fuel_press - d.fuel_press_min) / (d.fuel_press_min * 2)) * 100));
        setGaugeBar('fuel-press-gauge-bar', pct);
        const absLbl = document.getElementById('fuel-press-abs-label');
        if (absLbl) absLbl.textContent = toDispPress(Number(d.fuel_press)).toFixed(2) + ' / ≥' + toDispPress(Number(d.fuel_press_min)).toFixed(2) + ' ' + dispPressUnit();
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
      if (d.batt_volt_min && d.batt_volt_min > 0) {
        setText('batt-volt-min', Number(d.batt_volt_min).toFixed(1));
        if (d.batt_voltage !== undefined) {
          // 0% = at alarm threshold, 100% = 30% above threshold (typical full charge headroom)
          const fullV = d.batt_volt_min * 1.3;
          const pct = Math.min(100, Math.max(0,
            ((d.batt_voltage - d.batt_volt_min) / (fullV - d.batt_volt_min)) * 100));
          setGaugeBar('batt-gauge-bar', pct);
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
      if (d.turbo_power_w !== undefined) {
        const kw = Number(d.turbo_power_w) / 1000;
        setText('turbo-power', kw.toFixed(2));
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
      setDot('glow-hot-dot', hot);          // set class only; title managed below
      const ghDot = document.getElementById('glow-hot-dot');
      if (ghDot) ghDot.title = hot ? 'Glow plug — HOT (ready)' : 'Glow plug — warming up';
      setText('glow-hot-label', hot ? 'HOT — ready' : 'warming up…');
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
      setText('fuel-flow-val', d.fuel_flow !== undefined ? Number(d.fuel_flow).toFixed(2) : '—');
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
        el.style.color = ch.state ? 'var(--red)' : 'var(--dim)';
        el.style.borderColor = ch.state ? 'var(--red)' : 'var(--border)';
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
      setText('gov-target-rpm', d.governor_target_rpm !== undefined ? Number(d.governor_target_rpm).toLocaleString() : '—');
      setText('gov-n2-actual',  d.n2 !== undefined ? Number(d.n2).toLocaleString() : '—');
    }
  }

  // ── Advanced actuators section (glow, bleed, prop pitch, fuel pump 2, fan, airstarter, scavenge)
  const advActSection = document.getElementById('adv-act-section');
  if (advActSection) {
    const anyAdv = d.has_glow_plug || d.has_bleed_valve || d.has_prop_pitch || d.has_fuel_pump2
                || d.has_cool_fan  || d.has_airstarter  || d.has_oil_scavenge;
    advActSection.style.display = anyAdv ? '' : 'none';

    const advGlow = document.getElementById('adv-glow');
    if (advGlow) {
      advGlow.style.display = d.has_glow_plug ? '' : 'none';
      if (d.has_glow_plug && d.glow_plug_pct !== undefined) {
        setText('glow-pct', Math.round(d.glow_plug_pct));
        setGaugeBar('glow-gauge-bar', d.glow_plug_pct);
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
        setText('pitch-pct', Math.round(d.prop_pitch_demand * 100));
        setGaugeBar('pitch-gauge-bar', d.prop_pitch_demand * 100);
      }
    }

    const advFp2 = document.getElementById('adv-fp2');
    if (advFp2) {
      advFp2.style.display = d.has_fuel_pump2 ? '' : 'none';
      if (d.has_fuel_pump2 && d.fuel_pump2_demand !== undefined) {
        setText('fp2-pct', Math.round(d.fuel_pump2_demand * 100));
        setGaugeBar('fp2-gauge-bar', d.fuel_pump2_demand * 100);
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
      setDot('ab-sol-dot',   !!d.ab_sol_open,        'AB Solenoid');
      setDot('ab-arm-dot',   !!d.ab_arm_switch_on,   'AB Arm switch');
      setDot('ab-flame-dot', !!d.ab_flame_on,        'AB flame sensor');
      setDot('ab-trig-dot',  !!d.ab_trigger_active,  'AB trigger');

      // FIRE: only enabled when engine is RUNNING and AB is Off or Fault
      const canFire = d.mode === 'RUNNING' && (abMode === 'Off' || abMode === 'Fault');
      setDisabled('btn-ab-fire', !canFire);
      // STOP: only enabled when AB is active
      const abActive = abMode === 'Arming' || abMode === 'Igniting' || abMode === 'Running' || abMode === 'ShuttingDown';
      setDisabled('btn-ab-stop', !abActive);
    }
  }

  // ── Post-run summary + sequence timeline tracking ─────────
  _trackRunState(d);
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

function _timelinePill(b) {
  const s = (b.durationMs / 1000).toFixed(1);
  return `<span style="font-size:.7rem;padding:.15rem .5rem;border-radius:3px;` +
    `background:rgba(255,255,255,.06);border:1px solid var(--border);` +
    `color:var(--dim);white-space:nowrap">${b.name} ` +
    `<span style="color:var(--text)">${s}s</span></span>`;
}

function _showRunSummary(d, durationMs) {
  const card = document.getElementById('run-summary-card');
  if (!card) return;

  const isFault  = d.mode === 'FAULT';
  const titleEl  = document.getElementById('run-summary-title');
  if (titleEl) {
    titleEl.textContent = isFault ? '⚠ Run ended — Fault' : '✓ Run complete';
    titleEl.style.color = isFault ? 'var(--red)' : 'var(--green)';
  }

  const mins = Math.floor(durationMs / 60000);
  const secs = Math.round((durationMs % 60000) / 1000);
  const durStr = mins > 0 ? mins + 'm ' + secs + 's' : secs + 's';

  const stats = [
    { label: 'Duration', value: durStr },
    d.max_n1  !== undefined ? { label: 'Peak N1',  value: Number(d.max_n1).toLocaleString() + ' RPM' } : null,
    d.max_tot !== undefined ? { label: 'Peak TOT', value: toDispTemp(Number(d.max_tot)).toFixed(0) + ' ' + dispTempUnit() } : null,
    (d.oil_min_bar !== undefined && Number(d.oil_min_bar) > 0)
      ? { label: 'Min oil', value: toDispPress(Number(d.oil_min_bar)).toFixed(2) + ' ' + dispPressUnit() } : null,
  ].filter(Boolean);

  const statsEl = document.getElementById('run-summary-stats');
  if (statsEl) {
    statsEl.innerHTML = stats.map(s =>
      `<span><span style="color:var(--dim)">${s.label}:</span> <strong>${s.value}</strong></span>`
    ).join('');
    if (isFault && d.fault_description && d.fault_description.length > 0) {
      const line = d.fault_description.split('\n')[0].slice(0, 120);
      statsEl.innerHTML += `<div style="width:100%;color:var(--red);font-size:.78rem;margin-top:.3rem">Fault: ${line}</div>`;
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

function setGaugeBar(id, pct) {
  const bar = document.getElementById(id);
  if (!bar) return;
  const clamped = Math.min(100, Math.max(0, pct));
  bar.style.width = clamped + '%';
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
  fetch(url, { method: 'POST' })
    .then(r => r.json())
    .then(d => { if (!d.ok) console.warn('Command failed', d); })
    .catch(e => console.error('Network error', e));
}

function sendAbCmd(cmd) {
  fetch('/api/command', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ cmd })
  })
    .then(r => r.json())
    .then(d => { if (!d.ok) console.warn('AB command failed', d); })
    .catch(e => console.error('Network error', e));
}

// ── Boot: prime dashboard via REST for instant first paint, then WS takes over ─
applyUnitLabels();
connect();
fetch('/api/data')
  .then(r => r.json())
  .then(d => { try { applyData(d); } catch(e) {} })
  .catch(() => {});

// Show banner if startup sequence is empty (checked once at page load)
fetch('/api/hardware')
  .then(r => r.json())
  .then(hw => {
    const banner = document.getElementById('empty-seq-banner');
    if (banner) banner.style.display = (hw.startup_seq && hw.startup_seq.length > 0) ? 'none' : '';
  })
  .catch(() => {});

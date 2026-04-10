#pragma once

// ─────────────────────────────────────────────────────────────────────────────
//  config_html.h  —  Cluster web config page (served from flash, no uploadfs)
// ─────────────────────────────────────────────────────────────────────────────
// On ESP32, global const char[] lives in .rodata (flash) automatically.
// AsyncWebServer can serve it directly — no PROGMEM workarounds needed.

static const char CONFIG_HTML[] = R"rawhtml(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>GPX750 Cluster</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:system-ui,sans-serif;background:#111;color:#ddd;padding:1rem;max-width:640px;margin:0 auto}
h1{color:#f90;margin-bottom:1.2rem}
h2{color:#aaa;font-size:.85rem;text-transform:uppercase;letter-spacing:.05em;border-bottom:1px solid #2a2a2a;padding-bottom:.3rem;margin:1.2rem 0 .6rem}
.row{display:flex;align-items:center;justify-content:space-between;padding:.25rem 0}
.row label{color:#bbb;font-size:.9rem}
input[type=number],input[type=text],select{background:#1e1e1e;color:#eee;border:1px solid #3a3a3a;border-radius:4px;padding:.3rem .5rem;width:110px;font-size:.9rem}
input[type=checkbox]{width:18px;height:18px;accent-color:#f90;cursor:pointer}
.pwm-grid{display:grid;grid-template-columns:repeat(5,1fr);gap:.4rem;margin:.4rem 0}
.pwm-grid div{text-align:center}
.pwm-grid span{display:block;font-size:.75rem;color:#777;margin-bottom:.2rem}
.pwm-grid input{width:100%}
.actions{display:flex;gap:.5rem;margin-top:1.2rem;flex-wrap:wrap}
button{border:none;border-radius:4px;padding:.5rem 1.2rem;cursor:pointer;font-size:.9rem;font-weight:600}
.btn-save{background:#f90;color:#000}
.btn-reboot{background:#333;color:#ddd}
.btn-flash{background:#1a6;color:#fff}
button:active{opacity:.8}
.toast{position:fixed;top:1rem;right:1rem;padding:.5rem 1rem;border-radius:4px;font-size:.9rem;font-weight:600;display:none;z-index:99}
.toast.ok{background:#1a6;color:#fff}
.toast.err{background:#c33;color:#fff}
.ota-row{display:flex;gap:.5rem;align-items:center;margin-top:.4rem}
input[type=file]{background:#1e1e1e;border:1px solid #3a3a3a;border-radius:4px;padding:.3rem .5rem;color:#aaa;font-size:.85rem;flex:1}
hr{border:none;border-top:1px solid #2a2a2a;margin:1.5rem 0}
small{color:#666;font-size:.78rem}
</style>
</head>
<body>
<h1>GPX750 Cluster</h1>
<div id="toast" class="toast"></div>

<h2>Display</h2>
<div class="row"><label>RPM header</label>
  <select id="rpm_mode">
    <option value="0">&#215; 10 000 r/min</option>
    <option value="1">% max N1</option>
  </select>
</div>
<div class="row"><label>Demo mode <small>(reboots to apply)</small></label><input type="checkbox" id="demo_mode"></div>
<div class="row"><label>Boot gauge sweep</label><input type="checkbox" id="boot_sweep"></div>

<h2>Engine limits <small>— cluster defaults, ECU overrides on connect</small></h2>
<div class="row"><label>N1 max RPM</label><input type="number" id="n1_max" step="100" min="0"></div>
<div class="row"><label>N1 warn RPM</label><input type="number" id="n1_warn" step="100" min="0"></div>
<div class="row"><label>N2 warn RPM</label><input type="number" id="n2_warn" step="100" min="0"></div>
<div class="row"><label>TOT max &#176;C</label><input type="number" id="tot_max" step="1" min="0"></div>
<div class="row"><label>TOT warn &#176;C</label><input type="number" id="tot_warn" step="1" min="0"></div>
<div class="row"><label>Oil warn bar</label><input type="number" id="oil_warn" step="0.1" min="0"></div>
<div class="row"><label>Fuel warn %</label><input type="number" id="fuel_warn" step="1" min="0" max="100"></div>

<h2>Signal loss</h2>
<div class="row"><label>Enter NO SIGNAL (ms)</label><input type="number" id="enter_loss_ms" step="100" min="100"></div>
<div class="row"><label>Exit NO SIGNAL (ms)</label><input type="number" id="exit_good_ms" step="50" min="50"></div>

<h2>Tachometer</h2>
<div class="row"><label>Gauge full-scale Hz</label><input type="number" id="rpm_max_hz" step="1" min="1"></div>
<div class="row"><small>Tip: enable demo mode and adjust until needle reads full scale at 100% demo RPM</small></div>

<h2>Temp gauge PWM <small>— at 0 / 25 / 50 / 75 / 100 %</small></h2>
<div class="pwm-grid">
  <div><span>0%</span><input type="number" id="t0" min="0" max="255"></div>
  <div><span>25%</span><input type="number" id="t1" min="0" max="255"></div>
  <div><span>50%</span><input type="number" id="t2" min="0" max="255"></div>
  <div><span>75%</span><input type="number" id="t3" min="0" max="255"></div>
  <div><span>100%</span><input type="number" id="t4" min="0" max="255"></div>
</div>

<h2>Fuel gauge PWM <small>— at 0 / 25 / 50 / 75 / 100 %</small></h2>
<div class="pwm-grid">
  <div><span>0%</span><input type="number" id="g0" min="0" max="255"></div>
  <div><span>25%</span><input type="number" id="g1" min="0" max="255"></div>
  <div><span>50%</span><input type="number" id="g2" min="0" max="255"></div>
  <div><span>75%</span><input type="number" id="g3" min="0" max="255"></div>
  <div><span>100%</span><input type="number" id="g4" min="0" max="255"></div>
</div>

<h2>Fuel sender ADC</h2>
<div class="row"><label>Empty (ADC 0–4095)</label><input type="number" id="fuel_raw_empty" min="0" max="4095"></div>
<div class="row"><label>Full (ADC 0–4095)</label><input type="number" id="fuel_raw_full" min="0" max="4095"></div>

<div class="actions">
  <button class="btn-save" onclick="save()">Save</button>
  <button class="btn-reboot" onclick="reboot()">Reboot</button>
</div>

<hr>
<h2>Firmware update (OTA)</h2>
<small>Build with PlatformIO, select the .bin from .pio/build/esp32dev/</small>
<div class="ota-row">
  <input type="file" id="fwfile" accept=".bin">
  <button class="btn-flash" onclick="flash()">Flash</button>
</div>
<div id="ota-progress" style="margin-top:.5rem;color:#aaa;font-size:.85rem"></div>

<script>
const $ = id => document.getElementById(id);
function toast(msg, ok) {
  const t = $('toast');
  t.textContent = msg;
  t.className = 'toast ' + (ok ? 'ok' : 'err');
  t.style.display = 'block';
  clearTimeout(t._tid);
  t._tid = setTimeout(() => t.style.display = 'none', 3000);
}
async function load() {
  try {
    const r = await fetch('/api/config');
    if (!r.ok) throw new Error();
    const d = await r.json();
    $('rpm_mode').value    = d.rpm_mode   ?? 0;
    $('demo_mode').checked = !!d.demo_mode;
    $('boot_sweep').checked= !!d.boot_sweep;
    $('n1_max').value      = d.n1_max     ?? 100000;
    $('n1_warn').value     = d.n1_warn    ?? 90000;
    $('n2_warn').value     = d.n2_warn    ?? 22000;
    $('tot_max').value     = d.tot_max    ?? 750;
    $('tot_warn').value    = d.tot_warn   ?? 680;
    $('oil_warn').value    = d.oil_warn   ?? 2.2;
    $('fuel_warn').value   = d.fuel_warn  ?? 25;
    $('enter_loss_ms').value = d.enter_loss_ms ?? 2000;
    $('exit_good_ms').value  = d.exit_good_ms  ?? 300;
    $('rpm_max_hz').value  = d.rpm_max_hz ?? 200;
    (d.temp_pwm || []).forEach((v,i) => { const e = $('t'+i); if(e) e.value = v; });
    (d.fuel_pwm || []).forEach((v,i) => { const e = $('g'+i); if(e) e.value = v; });
    $('fuel_raw_empty').value = d.fuel_raw_empty ?? 4050;
    $('fuel_raw_full').value  = d.fuel_raw_full  ?? 79;
  } catch(e) { toast('Failed to load config', false); }
}
async function save() {
  const body = {
    rpm_mode:       parseInt($('rpm_mode').value),
    demo_mode:      $('demo_mode').checked,
    boot_sweep:     $('boot_sweep').checked,
    n1_max:         parseFloat($('n1_max').value),
    n1_warn:        parseFloat($('n1_warn').value),
    n2_warn:        parseFloat($('n2_warn').value),
    tot_max:        parseFloat($('tot_max').value),
    tot_warn:       parseFloat($('tot_warn').value),
    oil_warn:       parseFloat($('oil_warn').value),
    fuel_warn:      parseFloat($('fuel_warn').value),
    enter_loss_ms:  parseInt($('enter_loss_ms').value),
    exit_good_ms:   parseInt($('exit_good_ms').value),
    rpm_max_hz:     parseFloat($('rpm_max_hz').value),
    temp_pwm:       [0,1,2,3,4].map(i => parseInt($('t'+i).value)),
    fuel_pwm:       [0,1,2,3,4].map(i => parseInt($('g'+i).value)),
    fuel_raw_empty: parseInt($('fuel_raw_empty').value),
    fuel_raw_full:  parseInt($('fuel_raw_full').value)
  };
  try {
    const r = await fetch('/api/config', {
      method: 'POST',
      headers: {'Content-Type': 'application/json'},
      body: JSON.stringify(body)
    });
    toast(r.ok ? 'Saved' : 'Save failed', r.ok);
  } catch(e) { toast('Save failed', false); }
}
async function reboot() {
  toast('Rebooting\u2026', true);
  await fetch('/reboot', {method:'POST'}).catch(()=>{});
}
async function flash() {
  const file = $('fwfile').files[0];
  if (!file) { toast('Select a .bin file first', false); return; }
  const prog = $('ota-progress');
  prog.textContent = 'Uploading\u2026';
  const fd = new FormData();
  fd.append('firmware', file);
  try {
    const xhr = new XMLHttpRequest();
    xhr.open('POST', '/update');
    xhr.upload.onprogress = e => {
      if (e.lengthComputable)
        prog.textContent = 'Uploading: ' + Math.round(e.loaded/e.total*100) + '%';
    };
    xhr.onload = () => {
      if (xhr.status === 200) {
        prog.textContent = 'Done — rebooting';
        toast('Flash OK — rebooting', true);
      } else {
        prog.textContent = 'Flash failed (HTTP ' + xhr.status + ')';
        toast('Flash failed', false);
      }
    };
    xhr.onerror = () => { prog.textContent = 'Upload error'; toast('Upload error', false); };
    xhr.send(fd);
  } catch(e) { toast('Flash error', false); }
}
window.addEventListener('load', load);
</script>
</body>
</html>)rawhtml";

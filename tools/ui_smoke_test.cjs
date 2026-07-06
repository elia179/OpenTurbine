const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const { chromium } = require('playwright');

const base = 'http://127.0.0.1:8766';

async function waitShown(page, selector, shown) {
  await page.waitForFunction(
    ({ selector, shown }) => {
      const element = document.querySelector(selector);
      return element && (getComputedStyle(element).display !== 'none') === shown;
    },
    { selector, shown }
  );
}

async function text(page, selector) {
  return (await page.locator(selector).textContent()).trim();
}

async function state(page) {
  return (await page.request.get(`${base}/__sim/state`)).json();
}

async function scenario(page, name) {
  const response = await page.request.post(`${base}/__sim/scenario/${name}`);
  assert.equal(response.ok(), true, `scenario ${name} request failed`);
}

function installedBrowser() {
  const candidates = [
    process.env.PROGRAMFILES && path.join(process.env.PROGRAMFILES, 'Google', 'Chrome', 'Application', 'chrome.exe'),
    process.env['PROGRAMFILES(X86)'] && path.join(process.env['PROGRAMFILES(X86)'], 'Microsoft', 'Edge', 'Application', 'msedge.exe'),
    process.env.PROGRAMFILES && path.join(process.env.PROGRAMFILES, 'Microsoft', 'Edge', 'Application', 'msedge.exe'),
    process.env.LOCALAPPDATA && path.join(process.env.LOCALAPPDATA, 'Google', 'Chrome', 'Application', 'chrome.exe')
  ].filter(Boolean);
  return candidates.find(candidate => fs.existsSync(candidate));
}

(async () => {
  globalThis.OT_UI_SIM_PORT = 8766;
  await import('./ui_mock_server.mjs');
  const executablePath = installedBrowser();
  const browser = await chromium.launch({ headless: true, ...(executablePath ? { executablePath } : {}) });
  const page = await browser.newPage();
  page.on('pageerror', error => console.error(`Browser page error: ${error.message}`));
  const results = [];

  try {
    await page.goto(base);
    await page.evaluate(() => localStorage.clear());
    await page.reload();
    await waitShown(page, '#n1-card', true);

    // Fresh profile → the beta safety notice must appear and gate clicks;
    // acknowledge it the way a tester would, then continue the smoke run.
    await waitShown(page, '#beta-ack-overlay', true);
    await page.locator('#beta-ack-check').check();
    await page.locator('#beta-ack-btn').click();
    await waitShown(page, '#beta-ack-overlay', false);
    assert.equal(await page.evaluate(() => localStorage.getItem('ot_beta_notice_ack_v1')), '1');
    results.push('beta safety notice gates a fresh profile and is dismissible via checkbox + continue');
    // First-run theme chooser appears right after the beta notice; dismiss it like a tester would.
    await waitShown(page, '#theme-firstrun', true);
    await page.locator('#theme-firstrun button.primary').click();
    await waitShown(page, '#theme-firstrun', false);
    assert.equal(await page.evaluate(() => localStorage.getItem('ot_theme_onboarded_v1')), '1');
    results.push('first-run theme chooser appears after the beta notice and is dismissible');
    assert.equal(await text(page, '#fw-version'), 'vsim-1.0.0');
    assert.equal(await text(page, '#throttle-input-pct'), '50.0');
    assert.equal(await page.evaluate(() =>
      ['tot-card', 'tit-card', 'n1-card', 'n2-card'].every(id =>
        document.querySelector('#temperature-cards').contains(document.getElementById(id)))), true);
    assert.equal(await page.evaluate(() =>
      ['oil-card', 'oil-temp-card', 'oilpump-current-card'].every(id =>
        document.querySelector('#speed-cards').contains(document.getElementById(id)))), true);
    assert.equal(await page.evaluate(() => {
      const mode = document.querySelector('.mode-row');
      const outputs = document.getElementById('actuator-output-cards');
      const adv = document.getElementById('adv-act-section');
      return !!mode && !!outputs && !!adv &&
        !!(mode.compareDocumentPosition(adv) & Node.DOCUMENT_POSITION_FOLLOWING) &&
        !!(adv.compareDocumentPosition(outputs) & Node.DOCUMENT_POSITION_FOLLOWING);
    }), true);
    assert.equal(await text(page, '#hour-start-count'), '12');
    results.push('dashboard prioritizes primary data, oil cards, and actuator outputs below start/stop');
    assert.equal((await text(page, '#getting-started-banner')).match(/[⚙🔧📋🔨▶]/u), null);
    assert.equal(await page.locator('.gs-steps a').first().evaluate(el => getComputedStyle(el).color), 'rgb(245, 245, 247)');
    results.push('getting-started checklist uses the high-contrast text colour for plain-text actions');

    await page.request.post(`${base}/__sim/data`, { data: {
      mode: 'RUNNING', bench_mode: false, egt_source: 2,
      has_tit: true, tit_healthy: false,
      has_tot: true, tot_healthy: true,
      has_n1: true, n1_healthy: true
    } });
    await page.waitForFunction(() => getComputedStyle(document.getElementById('throttle-feedback-inhibit-note')).display !== 'none');
    await page.request.post(`${base}/__sim/data`, { data: { tit_healthy: true, egt_source: 1 } });
    await page.waitForFunction(() => getComputedStyle(document.getElementById('throttle-feedback-inhibit-note')).display === 'none');
    results.push('dashboard throttle inhibit warning follows selected EGT source, including TIT-primary setups');
    await scenario(page, 'full');

    await page.locator('#unit-temp-btn').click();
    await page.locator('#unit-press-btn').click();
    assert.equal(await text(page, '#tot'), '1184.0');
    assert.equal(await text(page, '#oil'), '31.18');
    assert.match(await text(page, '#tot-abs-label'), /F$/);
    assert.match(await text(page, '#oil-abs-label'), /PSI$/);
    results.push('dashboard temperature and pressure unit toggles convert live values and limit labels');

    await page.request.post(`${base}/__sim/data`, { data: {
      mode: 'RUNNING',
      oil_running_min: 0,
      fuel_press_min: 0,
      batt_volt_min: 0,
      oil_temp_limit: 0,
      egt_source: 2,
      has_tit: true,
      tit: 720,
      tit_limit: 0
    } });
    await page.waitForFunction(() => document.getElementById('oil-abs-label')?.textContent?.includes('/ OFF'));
    assert.match(await text(page, '#fuel-press-abs-label'), /\/ OFF$/);
    assert.equal(await text(page, '#batt-volt-min'), 'OFF');
    assert.match(await text(page, '#tit-abs-label'), /\/ OFF$/);
    results.push('dashboard zero-disabled thresholds clear stale gauge limits instead of retaining old values');
    await scenario(page, 'full');
    await page.locator('#unit-temp-btn').click();

    const retainedTot = await text(page, '#tot');
    await page.evaluate(() => ws.close());
    await page.waitForTimeout(50);
    assert.equal(await text(page, '#tot'), retainedTot);
    await page.request.post(`${base}/__sim/data`, { data: { tot: 651 } });
    await page.waitForFunction(() => document.getElementById('tot')?.textContent?.includes('651'), null, { timeout: 5000 });
    results.push('brief telemetry reconnect retains values and REST fallback keeps live pages updating without navigation');
    await page.locator('#unit-temp-btn').click();

    await page.request.post(`${base}/__sim/data`, { data: { config_storage_fault: true } });
    await page.waitForFunction(() => getComputedStyle(document.getElementById('config-storage-banner')).display !== 'none');
    await page.request.post(`${base}/__sim/data`, { data: { config_storage_fault: false } });
    await page.waitForFunction(() => getComputedStyle(document.getElementById('config-storage-banner')).display === 'none');
    results.push('dashboard shows storage-fault lock banner from telemetry');

    await page.reload();
    await waitShown(page, '#n1-card', true);
    await scenario(page, 'minimal');
    await waitShown(page, '#n2-card', false);
    await waitShown(page, '#p1-card', false);
    await waitShown(page, '#speed-group', true);
    await waitShown(page, '#pressure-section', false);
    await waitShown(page, '#ext-sensors-section', false);
    await waitShown(page, '#ab-section', false);
    results.push('minimal hardware telemetry keeps oil visible and hides optional sections');

    await scenario(page, 'startup');
    await waitShown(page, '#seq-progress-section', true);
    assert.equal(await text(page, '#seq-block-name'), 'FlameConfirm');
    assert.equal(await text(page, '#seq-step-text'), '4 / 6');
    await waitShown(page, '#throttle-startup-range-row', true);
    assert.match(await text(page, '#throttle-startup-range-row'), /10\.0 to 25\.0/);
    assert.match(await text(page, '#oil-startup-setting-note'), /100\.0/);
    results.push('startup scenario shows live sequence progress and direct startup output settings');

    await scenario(page, 'fault');
    await waitShown(page, '#fault-card', true);
    assert.equal(await text(page, '#fault-desc-text'), 'Oil pressure below running minimum');
    results.push('fault scenario exposes the fault card and current diagnosis');

    await scenario(page, 'full');
    await page.goto(`${base}/config.html`);
    await page.waitForSelector('#cf-tot_limit');
    assert.equal(await page.locator('#cf-tot_limit').inputValue(), '1328');
    assert.equal(await page.locator('#cf-tot_safe_margin').inputValue(), '72');
    // tot_limit is zeroOff ("0 = disabled"): the raw 0 sentinel must remain
    // enterable in every display unit, so min stays 0 even in °F mode.
    assert.equal(await page.locator('#cf-tot_limit').getAttribute('min'), '0');
    assert.equal(await page.locator('text=Automation Rules').count(), 0);
    results.push('config loads converted values without duplicating control-rule editing');

    await page.locator('#unit-temp-btn').click();
    assert.equal(await page.locator('#cf-tot_limit').inputValue(), '720');
    assert.equal(await page.locator('#cf-tot_safe_margin').inputValue(), '40');
    await page.locator('#unit-temp-btn').click();
    assert.equal(await page.locator('#cf-tot_limit').inputValue(), '1328');
    assert.equal(await page.locator('#cf-tot_safe_margin').inputValue(), '72');
    results.push('switching config units converts configuration inputs without changing meaning');

    await page.locator('#btn-view-expert').click();
    await page.locator('#cf-tot_limit').fill('1220');
    await page.locator('#cf-tot_safe_margin').fill('90');
    await page.locator('#cf-oil_rm').fill('29.008');
    page.once('dialog', dialog => dialog.accept());
    await page.locator('#btn-save').click();
    await page.locator('#save-recap-confirm-btn').click();
    await page.waitForFunction(() => document.querySelector('#save-msg').textContent.includes('Saved'));
    let saved = await state(page);
    assert.ok(Math.abs(saved.settings.engine.tot_limit - 660) < 0.001);
    assert.ok(Math.abs(saved.settings.engine.tot_safe_margin - 50) < 0.001);
    assert.ok(Math.abs(saved.settings.oil.running_min - 2) < 0.001);
    results.push('config saves converted F and PSI edits back as canonical C and bar values');
    await page.request.post(`${base}/__sim/data`, { data: {
      has_tit: true, tit_limit: 0, has_oil_temp: false,
      has_batt_voltage: false, fuel_press_min: 0, has_governor: false
    } });
    await waitShown(page, '#safety-ext-section', true);
    await waitShown(page, '#field-sf-tit', true);
    results.push('installed TIT sensor exposes its configurable limit before a limit is set');
    assert.equal(await page.locator('text=Download settings').count(), 0);
    assert.equal(await page.locator('text=Full engine file backup / restore').count(), 1);
    results.push('config page directs import and export to the unified engine file flow');

    await page.goto(`${base}/calibration.html`);
    await page.waitForFunction(() => document.querySelector('#th-wiz-steps').textContent.includes('servo signal'));
    assert.match(await text(page, '#cal-th-raw'), /us|µs/);
    await page.request.post(`${base}/__sim/data`, { data: { throttle_input_us: 1120, throttle_input_norm: 0.12 } });
    await page.waitForFunction(() => document.querySelector('#cal-th-raw').textContent.includes('1120'));
    await page.locator('#throttle-cal-row button', { hasText: 'Capture Min' }).click();
    await page.request.post(`${base}/__sim/data`, { data: { throttle_input_us: 1880, throttle_input_norm: 0.88 } });
    await page.waitForFunction(() => document.querySelector('#cal-th-raw').textContent.includes('1880'));
    await page.locator('#throttle-cal-row button', { hasText: 'Capture Max' }).click();
    await page.locator('#btn-th-save').click();
    await page.waitForTimeout(100);
    saved = await state(page);
    assert.equal(saved.settings.calibration.throttle_min_raw, 1120);
    assert.equal(saved.settings.calibration.throttle_max_raw, 1880);
    results.push('servo throttle calibration captures pulse widths and persists them');

    await page.goto(`${base}/hardware.html`);
    await page.waitForSelector('#f-thinput-type');
    assert.equal(await page.locator('#f-thinput-type').inputValue(), 'servo');
    assert.equal(await page.locator('#f-wifi-tx-power').inputValue(), '8');
    results.push('hardware page restores servo-input source from saved hardware state');
    await page.evaluate(() => {
      cfg.sensors.p1.enabled = false;
      cfg.sensors.p1.pin = cfg.sensors.oil_press.pin;
      _releaseInactivePinConflicts();
    });
    assert.equal(await page.evaluate(() => cfg.sensors.p1.pin), -1);
    results.push('inactive hardware releases a pin when an enabled device uses it');
    await page.evaluate(() => {
      cfg.sensors.tit.clk = 16;
      cfg.sensors.tit.miso = 17;
      refreshAllPins();
      setSensorEnabled('tit', 'tit', false);
      setSensorEnabled('tit', 'tit', true);
    });
    assert.equal(await page.locator('#f-tit-clk').inputValue(), await page.locator('#f-tot-clk').inputValue());
    assert.equal(await page.locator('#f-tit-miso').inputValue(), await page.locator('#f-tot-miso').inputValue());
    assert.equal(await page.evaluate(() => _checkGpioConflicts().some(c =>
      c.names.length === 2 && c.names.every(n => /^(TOT|TIT)/.test(n)))), false);
    await page.selectOption('#f-oiltemp-chip', 'max31855');
    assert.equal(await page.locator('#f-oiltemp-clk').inputValue(), await page.locator('#f-tot-clk').inputValue());
    assert.equal(await page.locator('#f-oiltemp-miso').inputValue(), await page.locator('#f-tot-miso').inputValue());
    results.push('hardware page automatically shares SPI bus lines across configured SPI sensors');

    await page.request.patch(`${base}/api/hardware`, { data: { platform: 'esp32s3' } });
    await page.reload();
    await page.waitForFunction(() => document.querySelector('#f-thr-pin option[value="16"]'));
    assert.equal(await page.locator('#f-thr-pin option[value="22"]').count(), 0);
    assert.equal(await page.locator('#f-oilpress-pin option[value="1"]').count(), 1);
    results.push('hardware page selects ESP32-S3 output and ADC pin choices from firmware platform');
    saved = await state(page);
    const renamedHardware = structuredClone(saved.hardware);
    renamedHardware.profile_id = 'renamed-bench-engine';
    let response = await page.request.post(`${base}/api/hardware`, { data: renamedHardware });
    assert.equal(response.ok(), true);
    saved = await state(page);
    assert.equal(saved.settings.profile_id, 'renamed-bench-engine');
    results.push('hardware engine identity save synchronizes the unified settings section');

    await scenario(page, 'minimal');
    await page.request.patch(`${base}/api/hardware`, { data: {
      controllers: { dynamic_idle: false },
      sensors: { n1_rpm: { enabled: false }, n2_rpm: { enabled: false } }
    } });
    await page.goto(`${base}/tools.html`);
    await page.waitForFunction(() => {
      const devButton = document.querySelector('#btn-dev-mode');
      const oilButton = document.querySelector('#btn-OIL_PRIME');
      return devButton && !devButton.disabled && oilButton && !oilButton.disabled;
    });
    assert.equal(await text(page, '#btn-dev-mode'), 'Enable Dev Mode');
    assert.equal(await page.locator('#card-TOGGLE_BENCH_MODE').isVisible(), false);
    assert.equal(await page.locator('#card-TOGGLE_SAFETY_CHECKS').isVisible(), false);
    assert.equal(await page.locator('#card-TOGGLE_DYNAMIC_IDLE').count(), 0);
    await page.request.post(`${base}/__sim/data`, { data: { dev_mode: true } });
    await waitShown(page, '#card-TOGGLE_BENCH_MODE', true);
    assert.match(await text(page, '#card-WEB-ASSETS'), /without erasing config or logs/);
    await page.locator('#btn-OIL_PRIME').click();
    await page.waitForTimeout(100);
    saved = await state(page);
    assert.equal(saved.commands.at(-1).cmd, 'OIL_PRIME');
    results.push('tools gates bench and dynamic idle by prerequisites while keeping standby actuator tests');

    await page.request.patch(`${base}/api/hardware`, { data: {
      controllers: { dynamic_idle: true },
      sensors: { n1_rpm: { enabled: true }, n2_rpm: { enabled: true } }
    } });

    await page.request.patch(`${base}/api/hardware`, { data: {
      sensors: { p1: { enabled: false } },
      actuators: { bleed_valve: { enabled: false } },
      has_afterburner: true,
      ab_trigger: { source: 3, input_pin: 32, input_rc_pwm: true, input_threshold: 2500, requires_arm: true, arm_pin: 33 }
    } });
    await page.goto(`${base}/sequence.html`);
    await page.waitForFunction(() => document.body.textContent.includes('Oil Pump On'));
    const delayInputs = page.locator('#list-startup .block-card[data-block="TimedDelay"] input[type="number"]');
    assert.equal(await delayInputs.count(), 3);
    assert.deepEqual(await delayInputs.evaluateAll(els => els.map(el => el.value)), ['15000', '10000', '5000']);
    await page.locator('#list-startup .block-card[data-block="TimedDelay"] .block-header').nth(2).click();
    await delayInputs.nth(2).fill('6000');
    await delayInputs.nth(2).dispatchEvent('input');
    page.once('dialog', dialog => dialog.accept());
    await page.locator('#save-btn').click();
    await page.waitForFunction(() => document.querySelector('#reboot-overlay')?.classList.contains('show'));
    assert.match(await page.locator('body').textContent(), /Igniter On/);
    assert.match(await page.locator('body').textContent(), /Fuel Pump/);
    assert.ok((await page.locator('.block-header').first().getAttribute('title')).length > 10);
    await page.locator('.seq-tab', { hasText: 'Afterburner' }).click();
    assert.match(await text(page, '#ab-lightup-criteria'), /Analog \/ RC input/);
    assert.match(await text(page, '#ab-lightup-criteria'), /Arm switch must be active/);
    assert.match(await text(page, '#ab-shutoff-criteria'), /Analog \/ RC input drops below/);
    assert.equal(await page.locator('#ab-edit-input-wrap').isVisible(), true);
    assert.equal(await page.locator('#ab-edit-input-pct').inputValue(), '61');
    await page.locator('#ab-edit-input-pct').fill('72');
    await page.locator('#ab-edit-input-pct').dispatchEvent('input');
    await page.locator('#ab-edit-min-n1').fill('50000');
    await page.locator('#ab-edit-min-n1').dispatchEvent('input');
    assert.equal(await page.evaluate(() => hwCfg.ab_trigger.input_threshold), Math.round(72 * 4095 / 100));
    assert.equal(await page.evaluate(() => cfg.afterburner.min_n1), 50000);
    results.push('sequence editor keeps independent timed delays and edits AB input/gate thresholds in friendly units');
    await page.goto(`${base}/hardware.html`);
    await page.waitForFunction(() => document.querySelector('#f-ab-inp-type')?.value === 'pwm');
    assert.equal(await page.locator('#f-ab-inp-type').inputValue(), 'pwm');
    results.push('hardware editor preserves dedicated AB servo-PWM command input type');
    await page.evaluate(() => {
      cfg.actuators.oil_pump.enabled = false;
      cfg.actuators.oil_pump.has_current = true;
      cfg.controllers.oil_loop = true;
      cfg.actuators.throttle.enabled = false;
      cfg.controllers.throttle_slew = true;
      updateHardwarePrerequisites(true);
    });
    assert.equal(await page.locator('#en-oilpumpcurrent').isDisabled(), true);
    assert.equal(await page.locator('#en-oilpumpcurrent').isChecked(), false);
    assert.equal(await page.locator('#f-ctrl-oil').isDisabled(), true);
    assert.equal(await page.locator('#f-ctrl-oil').isChecked(), false);
    assert.equal(await page.locator('#f-ctrl-slew').isDisabled(), true);
    assert.equal(await page.locator('#f-ctrl-slew').isChecked(), false);
    results.push('hardware editor ghosts current sensing and controllers when required hardware is absent');
    await page.evaluate(() => {
      cfg.has_two_shaft = false;
      cfg.sensors.n2_rpm.enabled = true;
      cfg.controllers.governor = true;
      updateFeaturesUI();
      updateHardwarePrerequisites(true);
    });
    assert.equal(await page.locator('#section-n2rpm').isVisible(), false);
    assert.equal(await page.locator('#f-ctrl-gov').isDisabled(), true);
    assert.equal(await page.locator('#f-ctrl-gov').isChecked(), false);
    results.push('single-shaft topology cannot leave hidden N2 governor dependencies enabled');
    await page.goto(`${base}/config.html`);
    assert.equal(await page.locator('#cf-ab_pcm').inputValue(), '1');
    assert.equal(await page.locator('#cf-ab_fm option[value="0"]').isDisabled(), false);
    assert.equal(await page.locator('#cf-ab_pcm option[value="2"]').isDisabled(), false);
    await page.request.patch(`${base}/api/hardware`, { data: {
      sensors: { tot: { enabled: false } },
      actuators: { igniter2: { enabled: false }, ab_pump: { enabled: false } },
      ab_flame: { enabled: false },
      ab_trigger: { input_pin: -1 }
    } });
    await page.reload();
    assert.equal(await page.locator('#cf-ab_fm option[value="0"]').isDisabled(), true);
    assert.equal(await page.locator('#cf-ab_fm option[value="1"]').isDisabled(), false);
    assert.equal(await page.locator('#cf-ab_pcm option[value="2"]').isDisabled(), true);
    assert.equal(await page.locator('#cf-ab_ui').isDisabled(), true);
    results.push('config editor ghosts afterburner choices whose hardware is unavailable');
    await page.goto(`${base}/sequence.html`);
    await page.locator('.seq-tab', { hasText: 'Control Rules' }).click();
    assert.equal(await page.locator('#tab-rules iframe').count(), 0);
    assert.equal(await page.locator('#rule-sensor-0 option[value="13"]').isDisabled(), true);
    assert.equal(await page.locator('#rule-act-0 option[value="1"]').isDisabled(), true);
    await page.request.patch(`${base}/api/hardware`, { data: {
      has_afterburner: false,
      has_two_shaft: false,
      sensors: { n2_rpm: { enabled: true } },
      actuators: { ab_sol: { enabled: true }, ab_pump: { enabled: true } },
      ab_trigger: { input_pin: 32 }
    } });
    await page.reload();
    assert.equal(await page.locator('#add-afterburner-sel option[value="ABPumpOn"]').count(), 0);
    assert.equal(await page.locator('#add-afterburner-sel option[value="ABSolOpen"]').count(), 0);
    await page.locator('.seq-tab', { hasText: 'Control Rules' }).click();
    assert.equal(await page.locator('#rule-sensor-0 option[value="6"]').isDisabled(), true);
    assert.equal(await page.locator('#rule-sensor-0 option[value="24"]').isDisabled(), true);
    assert.equal(await page.locator('#rule-act-0 option[value="11"]').isDisabled(), true);
    assert.deepEqual(await page.evaluate(() => getEnabledActuators()
      .filter(a => a.key === 'ab_sol' || a.key === 'ab_pump').map(a => a.key)), []);
    results.push('sequence dependencies ignore hidden N2 and afterburner children when master features are off');
    await page.locator('#btn-add-rule').click();
    assert.equal(await page.locator('#rules-list .rules-row').count(), 2);
    page.once('dialog', dialog => dialog.accept());
    await page.locator('#save-btn').click();
    await page.waitForFunction(() => document.querySelector('#reboot-overlay')?.classList.contains('show'));
    saved = await state(page);
    assert.equal(saved.hardware.profile_id, saved.settings.profile_id);
    assert.equal(saved.hardware.startup_delay_ms[6], 6000);
    assert.equal(saved.settings.rules.length, 2);
    results.push('sequence editor owns control rules, ghosts unavailable hardware, and saves one engine file');

    const unified = await (await page.request.get(`${base}/api/ecu_config`)).json();
    unified.hardware.profile_id = 'second-bench-engine';
    unified.settings.profile_id = 'second-bench-engine';
    response = await page.request.post(`${base}/api/ecu_config`, { data: unified });
    assert.equal(response.ok(), true);
    const crossed = structuredClone(unified);
    crossed.settings.profile_id = 'wrong-engine';
    response = await page.request.post(`${base}/api/ecu_config`, { data: crossed });
    assert.equal(response.status(), 400);
    saved = await state(page);
    assert.equal(saved.hardware.profile_id, saved.settings.profile_id);
    results.push('full engine-file restore accepts matching identities and rejects crossed sections');

    // The log page now follows the site-wide unit preference; earlier steps
    // in this run left °F/PSI active, so pin to canonical units first.
    await page.evaluate(() => localStorage.setItem('ot_units', JSON.stringify({ temp: 'C', press: 'bar' })));
    await page.goto(`${base}/log.html`);
    await page.waitForFunction(() => document.body.textContent.includes('Run #'));
    const logText = await page.locator('body').textContent();
    assert.match(logText, /Low_Oil|LOW_OIL|Fault/);
    assert.match(logText, /Peak TIT/);
    assert.match(logText, /TIT 840/);
    assert.match(logText, /Oil 0\.55 bar/);
    // And the same summaries convert when the preference is imperial.
    await page.evaluate(() => localStorage.setItem('ot_units', JSON.stringify({ temp: 'F', press: 'psi' })));
    await page.reload();
    await page.waitForFunction(() => document.body.textContent.includes('Run #'));
    assert.match(await page.locator('body').textContent(), /TIT 1544/);
    results.push('flight log renders firmware event keys, TIT peaks, and follows the unit preference');

    console.log(`UI smoke test passed (${results.length} checks):`);
    results.forEach(result => console.log(`- ${result}`));
  } finally {
    await browser.close();
  }
  process.exit(0);
})().catch(error => {
  console.error(error.stack || error);
  process.exit(1);
});

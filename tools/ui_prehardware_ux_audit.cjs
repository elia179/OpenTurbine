const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const { chromium } = require('playwright');

const port = 11100 + Math.floor(Math.random() * 500);
const base = `http://127.0.0.1:${port}`;

function installedBrowser() {
  const candidates = [
    process.env.PROGRAMFILES && path.join(process.env.PROGRAMFILES, 'Google', 'Chrome', 'Application', 'chrome.exe'),
    process.env['PROGRAMFILES(X86)'] && path.join(process.env['PROGRAMFILES(X86)'], 'Microsoft', 'Edge', 'Application', 'msedge.exe'),
    process.env.PROGRAMFILES && path.join(process.env.PROGRAMFILES, 'Microsoft', 'Edge', 'Application', 'msedge.exe'),
    process.env.LOCALAPPDATA && path.join(process.env.LOCALAPPDATA, 'Google', 'Chrome', 'Application', 'chrome.exe')
  ].filter(Boolean);
  return candidates.find(candidate => fs.existsSync(candidate));
}

async function reset(page) {
  const response = await page.request.post(`${base}/__sim/reset`);
  assert.equal(response.ok(), true);
}

async function goto(page, route, selector = 'body') {
  await page.goto(`${base}/${route}`);
  await page.waitForSelector(selector, { state: 'attached' });
  await page.waitForTimeout(150);
}

async function text(page, selector) {
  return (await page.locator(selector).textContent()).trim();
}

async function visible(page, selector) {
  return page.evaluate(sel => {
    const el = document.querySelector(sel);
    return !!el && getComputedStyle(el).display !== 'none' && el.getClientRects().length > 0;
  }, selector);
}

async function state(page) {
  return (await page.request.get(`${base}/__sim/state`)).json();
}

async function patchHardware(page, patch) {
  const response = await page.request.patch(`${base}/api/hardware`, { data: patch });
  assert.equal(response.ok(), true);
}

async function patchConfig(page, patch) {
  const response = await page.request.patch(`${base}/api/config`, { data: patch });
  assert.equal(response.ok(), true);
}

async function patchData(page, patch) {
  const response = await page.request.post(`${base}/__sim/data`, { data: patch });
  assert.equal(response.ok(), true);
}

async function scenario(page, name) {
  const response = await page.request.post(`${base}/__sim/scenario/${name}`);
  assert.equal(response.ok(), true);
}

async function assertNoSevereLayoutIssues(page, route, viewport) {
  await page.setViewportSize(viewport);
  await goto(page, route);
  const metrics = await page.evaluate(() => {
    const doc = document.documentElement;
    const overflow = Math.max(0, doc.scrollWidth - doc.clientWidth);
    const badControls = [];
    for (const el of document.querySelectorAll('button,select,input,.tool-card,.card,.hw-item-card,.block-card')) {
      const style = getComputedStyle(el);
      if (style.display === 'none' || style.visibility === 'hidden') continue;
      const rect = el.getBoundingClientRect();
      if (!rect.width || !rect.height) continue;
      if (el.scrollWidth > el.clientWidth + 6 && rect.width < doc.clientWidth + 1) {
        badControls.push((el.id || el.textContent || el.tagName).trim().replace(/\s+/g, ' ').slice(0, 80));
      }
    }
    return { overflow, badControls: badControls.slice(0, 12) };
  });
  assert.ok(metrics.overflow <= 24, `${route} overflows viewport ${viewport.width}px by ${metrics.overflow}px`);
  assert.deepEqual(metrics.badControls, [], `${route} has clipped controls/cards at ${viewport.width}px: ${metrics.badControls.join(' | ')}`);
}

(async () => {
  globalThis.OT_UI_SIM_PORT = port;
  await import('./ui_mock_server.mjs');
  const executablePath = installedBrowser();
  const browser = await chromium.launch({ headless: true, ...(executablePath ? { executablePath } : {}) });
  const page = await browser.newPage();
  const consoleErrors = [];
  const badResponses = [];
  page.on('pageerror', error => { throw error; });
  page.on('console', message => {
    if (message.type() === 'error' && !/Failed to load resource/i.test(message.text())) consoleErrors.push(message.text());
  });
  page.on('response', response => {
    if (response.status() >= 400 && !/\/favicon\.ico($|\?)/.test(response.url())) badResponses.push(`${response.status()} ${response.url()}`);
  });

  const results = [];
  try {
    await reset(page);

    await goto(page, 'index.html', '#getting-started-banner');
    await page.evaluate(() => localStorage.clear());
    await page.reload();
    await page.waitForSelector('#getting-started-banner');
    const gs = await text(page, '#getting-started-banner');
    assert.match(gs, /Hardware/i);
    assert.match(gs, /Config/i);
    assert.match(gs, /Calibration/i);
    assert.equal(await page.locator('#getting-started-banner a[href="/hardware.html"]').count(), 1);
    assert.equal(await page.locator('#getting-started-banner a[href="/config.html"]').count(), 1);
    assert.equal(await page.locator('#getting-started-banner a[href="/calibration.html"]').count(), 1);
    results.push('first-boot checklist gives a clear hardware -> config -> calibration path');

    await goto(page, 'hardware.html', '#f-profile-id');
    assert.match(await text(page, '#save-msg'), /Loaded/i);
    assert.equal(await page.locator('#btn-save').isDisabled(), true);
    const hwFailPage = await browser.newPage();
    await hwFailPage.addInitScript(() => {
      const originalFetch = window.fetch.bind(window);
      window.fetch = (url, init) => String(url).includes('/api/hardware')
        ? Promise.reject(new Error('simulated hardware load failure'))
        : originalFetch(url, init);
    });
    await hwFailPage.goto(`${base}/hardware.html`);
    await hwFailPage.waitForSelector('#save-msg');
    await hwFailPage.waitForFunction(() => document.querySelector('#save-msg').textContent.includes('Load failed'));
    assert.match((await hwFailPage.locator('#save-msg').textContent()).trim(), /Load failed/i);
    await hwFailPage.close();
    results.push('hardware page has an understandable load-failure state');

    await goto(page, 'config.html', '#save-msg');
    const cfgFailPage = await browser.newPage();
    await cfgFailPage.addInitScript(() => {
      const originalFetch = window.fetch.bind(window);
      window.fetch = (url, init) => String(url).includes('/api/config')
        ? Promise.reject(new Error('simulated config load failure'))
        : originalFetch(url, init);
    });
    await cfgFailPage.goto(`${base}/config.html`);
    await cfgFailPage.waitForSelector('#cfg-form', { state: 'attached' });
    await cfgFailPage.waitForFunction(() => document.querySelector('#cfg-form').textContent.includes('Error loading config'));
    assert.match((await cfgFailPage.locator('#cfg-form').textContent()).trim(), /Error loading config/i);
    await cfgFailPage.close();
    results.push('config page has an understandable load-failure state');

    let response = await page.request.post(`${base}/api/ecu_config`, { data: { hardware: { profile_id: 'a' } } });
    assert.equal(response.status(), 400);
    response = await page.request.post(`${base}/api/ecu_config`, {
      data: { hardware: { profile_id: 'a' }, settings: { profile_id: 'b' } }
    });
    assert.equal(response.status(), 400);
    results.push('bad or crossed engine-file restores are rejected before changing state');

    await reset(page);
    await goto(page, 'tools.html', '#cfg-restore-btn');
    await scenario(page, 'startup');
    await page.reload();
    await page.waitForSelector('#cfg-restore-btn');
    assert.equal(await page.locator('#cfg-restore-btn').isDisabled(), true);
    await scenario(page, 'minimal');
    await page.reload();
    await page.waitForSelector('#cfg-restore-btn');
    assert.equal(await page.locator('#cfg-restore-btn').isDisabled(), false);
    assert.match(await text(page, '#card-CONFIG_BACKUP'), /STANDBY/i);
    results.push('backup/restore UX blocks restore outside STANDBY and explains the gate');

    await reset(page);
    await patchHardware(page, {
      has_afterburner: false,
      actuators: {
        starter_en: { enabled: false },
        ab_sol: { enabled: false },
        ab_pump: { enabled: false },
        airstarter_sol: { enabled: false },
        cool_fan: { enabled: false },
        oil_scavenge_pump: { enabled: false },
        fuel_pump2: { enabled: false },
        bleed_valve: { enabled: false },
        prop_pitch: { enabled: false },
        glow_plug: { enabled: false, has_current: false },
        igniter: { has_current: false },
        igniter2: { has_current: false },
        oil_pump: { has_current: false },
        status_led: { enabled: false }
      },
      sensors: {
        p1: { enabled: false },
        p2: { enabled: false },
        fuel_press: { enabled: false },
        fuel_flow: { enabled: false },
        batt_voltage: { enabled: false },
        torque: { enabled: false },
        throttle_input: { enabled: false },
        idle_input: { enabled: false }
      },
      cluster_serial: { enabled: false, tx_pin: -1, rx_pin: -1 },
      mavlink: { enabled: false, tx_pin: -1 },
      buzzer: { enabled: false, pin: -1 },
      di_channels: [
        { pin: -1, role: 'none' },
        { pin: -1, role: 'none' },
        { pin: -1, role: 'none' },
        { pin: -1, role: 'none' }
      ]
    });
    await scenario(page, 'minimal');
    await goto(page, 'hardware.html', '#f-profile-desc');
    await page.locator('#f-profile-desc').fill('UX pre-hardware audit');
    await page.locator('#f-profile-desc').dispatchEvent('input');
    await page.locator('#btn-save').click();
    await page.waitForSelector('#save-recap-modal', { state: 'visible' });
    assert.match(await text(page, '#save-recap-subtitle'), /reboot/i);
    assert.match(await text(page, '#save-recap-confirm-btn'), /Save.*Reboot/i);
    await page.locator('#save-recap-confirm-btn').click();
    await page.waitForFunction(() => document.getElementById('reboot-overlay')?.style.display === 'flex');
    assert.match(await text(page, '#reboot-overlay'), /Reconnecting/i);
    results.push('hardware save recap and reboot overlay are explicit');

    await goto(page, 'config.html', '#cf-rpm_limit');
    await page.locator('#btn-view-expert').click();
    await page.locator('#cf-rpm_limit').fill('96000');
    await page.locator('#btn-save').click();
    await page.waitForSelector('#save-recap-modal', { state: 'visible' });
    assert.match(await text(page, '#save-recap-subtitle'), /updated on the device/i);
    assert.doesNotMatch(await text(page, '#save-recap-subtitle'), /reboot/i);
    await page.locator('#save-recap-confirm-btn').click();
    await page.waitForFunction(() => document.querySelector('#save-msg').textContent.includes('Saved'));
    assert.match(await text(page, '#save-msg'), /Saved/i);
    results.push('config save recap distinguishes live settings from hardware reboot saves');

    await reset(page);
    await goto(page, 'hardware.html', '#f-stop-pin');
    const stopPin = await page.locator('#f-stop-pin').inputValue();
    await page.locator('#f-start-pin').selectOption(stopPin);
    await page.locator('#f-start-pin').dispatchEvent('change');
    await page.waitForFunction(() => getComputedStyle(document.getElementById('pin-conflict-banner')).display !== 'none');
    const conflictText = await text(page, '#pin-conflict-banner');
    assert.match(conflictText, /GPIO/i);
    assert.match(conflictText, /Stop/i);
    assert.match(conflictText, /Start/i);
    assert.equal(await page.locator('#btn-save').isDisabled(), true);
    results.push('pin conflicts name the exact GPIO and devices, and block save');

    await reset(page);
    await patchHardware(page, {
      sensors: { n1_rpm: { enabled: false }, n2_rpm: { enabled: false }, tot: { enabled: false }, oil_press: { enabled: false } },
      safety: { overspeed: true, overtemp: true, low_oil: true },
      controllers: { oil_loop: true, dynamic_idle: true }
    });
    await goto(page, 'hardware.html', '#f-saf-overspeed');
    assert.equal(await page.locator('#f-saf-overspeed').isDisabled(), true);
    assert.equal(await page.locator('#f-saf-overtemp').isDisabled(), true);
    assert.equal(await page.locator('#f-saf-lowoil').isDisabled(), true);
    assert.equal(await page.locator('#f-ctrl-oil').isDisabled(), true);
    assert.equal(await page.locator('#f-ctrl-idle').isDisabled(), true);
    results.push('safety/controller dependencies visibly ghost when required hardware is absent');

    await reset(page);
    await patchHardware(page, { cluster_serial: { enabled: false, tx_pin: -1, rx_pin: -1 } });
    await goto(page, 'config.html', '#cf-cl_en');
    assert.equal(await page.locator('#cf-cl_en').isDisabled(), true);
    assert.match(await page.locator('#cf-cl_en').evaluate(el => el.closest('.cfg-field')?.title || ''), /not fitted|Hardware/i);
    await goto(page, 'hardware.html', '#en-cluster');
    assert.match(await text(page, '#grp-cluster').catch(() => ''), /OpenTurbine Cluster|TX pin|RX pin/i);
    assert.match(await text(page, 'body'), /Config > Cluster > Enable/i);
    assert.equal(await page.locator('#f-cl-rx option[value="-1"]').count(), 1);
    results.push('cluster TX-only/two-way setup exposes the right gates and telemetry-only RX option');

    await reset(page);
    await patchHardware(page, {
      has_two_shaft: false,
      has_afterburner: false,
      actuators: { prop_pitch: { enabled: false }, ab_sol: { enabled: true }, ab_pump: { enabled: true } },
      sensors: { n2_rpm: { enabled: true } }
    });
    await goto(page, 'sequence.html', '#save-btn');
    assert.equal(await visible(page, '#tab-btn-afterburner'), false);
    assert.equal(await page.locator('#add-startup-sel option[value*="AB"]').count(), 0);
    await page.locator('.seq-tab', { hasText: 'Control Rules' }).click();
    assert.equal(await page.locator('#rule-sensor-0 option[value="6"]').isDisabled(), true);
    assert.equal(await page.locator('#rule-act-0 option[value="11"]').isDisabled(), true);
    results.push('sequencer and rules do not offer hidden N2/afterburner dependencies');

    await reset(page);
    await patchHardware(page, {
      sensors: {
        oil_press: { enabled: false },
        flame: { enabled: false },
        p1: { enabled: false },
        p2: { enabled: false },
        throttle_input: { enabled: true, rc_pwm: true },
        idle_input: { enabled: true, rc_pwm: true }
      }
    });
    await patchData(page, {
      has_oil_press: false,
      has_flame: false,
      has_p1: false,
      has_p2: false,
      throttle_input_type: 'servo',
      throttle_input_us: 1510,
      idle_input_type: 'servo',
      idle_input_us: 1320
    });
    await goto(page, 'calibration.html', '#throttle-cal-row');
    assert.equal(await visible(page, '#oil-press-cal-row'), false);
    assert.equal(await visible(page, '#flame-cal-row'), false);
    assert.equal(await visible(page, '#p1-cal-row'), false);
    assert.match(await text(page, '#cal-th-raw'), /1510.*(us|s)/i);
    assert.match(await text(page, '#cal-idle-raw'), /1320.*(us|s)/i);
    results.push('calibration hides absent sensors and labels servo pulse units');

    await reset(page);
    await goto(page, 'log.html', '#tab-session');
    await page.locator('#tab-session').click();
    await page.request.delete(`${base}/api/session/all`);
    await page.reload();
    await page.waitForSelector('#tab-session');
    await page.locator('#tab-session').click();
    await page.waitForTimeout(300);
    assert.match(await text(page, 'body'), /No session|No data|empty|CSV/i);
    results.push('session log page handles empty log state without breaking controls');

    await reset(page);
    await patchData(page, {
      config_version_mismatch: true,
      flash_free_kb: 24,
      mode: 'STANDBY',
      dev_mode: false,
      bench_mode: false
    });
    await goto(page, 'tools.html', '#tool-area');
    assert.match(await text(page, '#cfg-version-mismatch-banner'), /schema mismatch|review/i);
    assert.match(await text(page, '#card-WEB-ASSETS'), /without erasing config or logs/i);
    assert.equal(await visible(page, '#card-TOGGLE_BENCH_MODE'), false);
    await patchData(page, { dev_mode: true });
    await page.waitForTimeout(250);
    assert.equal(await visible(page, '#card-TOGGLE_BENCH_MODE'), true);
    results.push('tools page surfaces schema/version warnings and gates bench mode behind dev mode');

    for (const route of ['index.html', 'hardware.html', 'config.html', 'sequence.html', 'calibration.html', 'tools.html', 'log.html']) {
      await assertNoSevereLayoutIssues(page, route, { width: 390, height: 844 });
      await assertNoSevereLayoutIssues(page, route, { width: 1366, height: 768 });
    }
    results.push('main pages avoid major overflow or clipped controls on phone and desktop viewports');

    assert.deepEqual(consoleErrors, [], 'browser console should stay free of errors');
    assert.deepEqual(badResponses.filter(r => !/simulated|api\/ecu_config/.test(r)), [], 'browser should not request missing app resources');

    console.log(`Pre-hardware UX audit passed (${results.length} groups):`);
    for (const result of results) console.log(`- ${result}`);
  } finally {
    await browser.close();
  }
  process.exit(0);
})().catch(error => {
  console.error(error.stack || error);
  process.exit(1);
});

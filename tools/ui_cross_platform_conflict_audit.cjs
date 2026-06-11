const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const { chromium } = require('playwright');

const port = 9800 + Math.floor(Math.random() * 500);
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

async function patchHardware(page, patch) {
  const response = await page.request.patch(`${base}/api/hardware`, { data: patch });
  assert.equal(response.ok(), true);
}

async function gotoHardware(page) {
  await page.goto(`${base}/hardware.html`);
  await page.waitForSelector('#f-profile-id', { state: 'attached' });
}

async function hasOption(page, selector, value) {
  return page.locator(`${selector} option[value="${value}"]`).count();
}

(async () => {
  globalThis.OT_UI_SIM_PORT = port;
  await import('./ui_mock_server.mjs');
  const executablePath = installedBrowser();
  const browser = await chromium.launch({ headless: true, ...(executablePath ? { executablePath } : {}) });
  const page = await browser.newPage();
  page.on('pageerror', error => { throw error; });

  const results = [];
  try {
    await reset(page);
    await patchHardware(page, { platform: 'esp32' });
    await gotoHardware(page);
    assert.equal(await hasOption(page, '#f-led-pin', 34), 0, 'ESP32 output selector must reject input-only GPIO34');
    assert.equal(await hasOption(page, '#f-led-pin', 6), 0, 'ESP32 output selector must reject flash GPIO6');
    assert.equal(await hasOption(page, '#f-buzzer-pin', 34), 0, 'ESP32 buzzer selector must reject input-only GPIO34');
    assert.equal(await hasOption(page, '#f-oilpress-pin', 34), 1, 'ESP32 ADC selector must allow ADC1 GPIO34');
    assert.equal(await hasOption(page, '#f-oilpress-pin', 25), 0, 'ESP32 ADC selector must reject non-ADC1 GPIO25');
    results.push('ESP32 pin selectors match firmware GPIO, output and ADC limits');

    await reset(page);
    await patchHardware(page, { platform: 'esp32s3' });
    await gotoHardware(page);
    assert.equal(await hasOption(page, '#f-led-pin', 46), 0, 'ESP32-S3 output selector must reject input-only GPIO46');
    assert.equal(await hasOption(page, '#f-led-pin', 22), 0, 'ESP32-S3 output selector must reject absent/flash GPIO22');
    assert.equal(await hasOption(page, '#f-buzzer-pin', 46), 0, 'ESP32-S3 buzzer selector must reject input-only GPIO46');
    assert.equal(await hasOption(page, '#f-cl-rx', 46), 1, 'ESP32-S3 input selector may allow input-only GPIO46');
    assert.equal(await hasOption(page, '#f-oilpress-pin', 10), 1, 'ESP32-S3 ADC selector must allow ADC1 GPIO10');
    assert.equal(await hasOption(page, '#f-oilpress-pin', 11), 0, 'ESP32-S3 ADC selector must reject non-ADC1 GPIO11');
    results.push('ESP32-S3 pin selectors match firmware GPIO, output and ADC limits');

    const conflictMatrix = await page.evaluate(() => {
      cfg.platform = 'esp32s3';
      cfg.controls = { stop_pin: 33, start_pin: 34 };
      for (const item of Object.values(cfg.sensors || {})) item.enabled = false;
      for (const item of Object.values(cfg.actuators || {})) {
        item.enabled = false;
        item.has_current = false;
      }
      cfg.has_afterburner = false;
      cfg.di_channels = [];
      cfg.actuators.status_led = { enabled: true, pin: 4 };
      cfg.mavlink = { enabled: true, tx_pin: 4, baud: 115200, interval_ms: 200 };
      cfg.cluster_serial = { enabled: true, protocol: 1, tx_pin: 5, rx_pin: -1, baud: 115200, interval_ms: 100 };
      const statusLedMav = _checkGpioConflicts().some(c =>
        c.pin === 4 && c.names.includes('Status LED') && c.names.includes('MAVLink TX'));

      cfg.mavlink.enabled = false;
      cfg.sensors.tot = { enabled: true, chip: 'max31856', clk: 40, cs: 37, miso: 41, mosi: 42 };
      cfg.sensors.tit = { enabled: true, chip: 'max31856', clk: 40, cs: 38, miso: 41, mosi: 42 };
      cfg.sensors.oil_temp = { enabled: true, chip: 'max31856', clk: 40, cs: 39, miso: 41, mosi: 42 };
      const sharedSpiBusOk = !_checkGpioConflicts().some(c => [40, 41, 42].includes(c.pin));

      cfg.sensors.tit.cs = 37;
      const sharedCsBlocked = _checkGpioConflicts().some(c =>
        c.pin === 37 && c.names.includes('TOT CS') && c.names.includes('TIT CS'));

      cfg.sensors.tit.cs = 16;
      cfg.cluster_serial.tx_pin = 4;
      cfg.actuators.status_led.enabled = false;
      const released = _releaseInactivePinConflicts();
      const statusLedReleased = cfg.actuators.status_led.pin === -1 && released;

      return { statusLedMav, sharedSpiBusOk, sharedCsBlocked, statusLedReleased };
    });
    assert.deepEqual(conflictMatrix, {
      statusLedMav: true,
      sharedSpiBusOk: true,
      sharedCsBlocked: true,
      statusLedReleased: true
    });
    results.push('hardware conflict logic covers status LED, serial pins, SPI bus sharing and hidden inactive pins');

    const txOnlyCluster = await page.evaluate(() => {
      cfg.cluster_serial = { enabled: true, protocol: 1, tx_pin: 5, rx_pin: -1, baud: 115200, interval_ms: 100 };
      return _checkGpioConflicts().some(c => c.names.some(n => n.startsWith('Cluster Serial')));
    });
    assert.equal(txOnlyCluster, false, 'TX-only cluster mode must not create a false RX conflict');
    results.push('TX-only cluster mode remains conflict-free with RX disabled');

    console.log(`Cross-platform conflict audit passed (${results.length} groups):`);
    for (const result of results) console.log(`- ${result}`);
  } finally {
    await browser.close();
  }
  process.exit(0);
})().catch(error => {
  console.error(error.stack || error);
  process.exit(1);
});

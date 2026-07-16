#!/usr/bin/env node
// Deterministic public screenshots from the existing UI simulator. These never
// connect to a real ECU or include a user profile, password, path, or live run.
const fs = require('node:fs');
const path = require('node:path');
const { chromium } = require('playwright');

const root = path.resolve(__dirname, '..');
const output = path.join(root, 'site', 'assets', 'images');
const port = 11700 + Math.floor(Math.random() * 400);
const base = `http://127.0.0.1:${port}`;

function installedBrowser() {
  const candidates = [
    process.env.PROGRAMFILES && path.join(process.env.PROGRAMFILES, 'Google', 'Chrome', 'Application', 'chrome.exe'),
    process.env['PROGRAMFILES(X86)'] && path.join(process.env['PROGRAMFILES(X86)'], 'Microsoft', 'Edge', 'Application', 'msedge.exe'),
    process.env.PROGRAMFILES && path.join(process.env.PROGRAMFILES, 'Microsoft', 'Edge', 'Application', 'msedge.exe'),
    process.env.LOCALAPPDATA && path.join(process.env.LOCALAPPDATA, 'Google', 'Chrome', 'Application', 'chrome.exe'),
  ].filter(Boolean);
  return candidates.find((candidate) => fs.existsSync(candidate));
}

async function prepare(page) {
  await page.goto(`${base}/index.html`);
  await page.evaluate(() => {
    localStorage.setItem('ot_beta_notice_ack_v1', '1');
    localStorage.setItem('ot_theme_onboarded_v1', '1');
    localStorage.setItem('ot_gs_dismissed', '1');
    localStorage.setItem('ot_theme', 'carbon');
    localStorage.setItem('ot_units', JSON.stringify({ temp: 'C', press: 'bar' }));
  });
  await page.reload();
  await page.request.post(`${base}/__sim/scenario/full`);
  await page.request.post(`${base}/__sim/data`, { data: { mode: 'STANDBY', last_event: 'STANDBY', uptime_s: 0 } });
}

async function capture(page, route, file, selector, scrollToSelector = false) {
  await page.goto(`${base}/${route}`);
  await page.waitForSelector(selector);
  if (scrollToSelector) {
    await page.locator(selector).scrollIntoViewIfNeeded();
    await page.evaluate(() => window.scrollBy(0, -72));
  }
  await page.screenshot({ path: path.join(output, file), type: 'png' });
}

(async () => {
  fs.mkdirSync(output, { recursive: true });
  globalThis.OT_UI_SIM_PORT = port;
  await import('./ui_mock_server.mjs');
  const executablePath = installedBrowser();
  const browser = await chromium.launch({ headless: true, ...(executablePath ? { executablePath } : {}) });
  try {
    const page = await browser.newPage({ viewport: { width: 1800, height: 1050 }, deviceScaleFactor: 1 });
    await prepare(page);
    await capture(page, 'index.html', 'hero-dashboard.png', '#n1-card');
    await capture(page, 'hardware.html', 'hardware-page.png', '#hardware-inputs-panel', true);
    await capture(page, 'config.html', 'config-page.png', '#cfg-form');
    await capture(page, 'calibration.html', 'calibration-page.png', '#throttle-cal-row');
    await capture(page, 'sequence.html', 'sequence-page.png', '#list-startup');
    await page.getByRole('button', { name: 'Control Rules' }).click();
    await page.waitForSelector('#rules-list');
    await page.screenshot({ path: path.join(output, 'control-rules-page.png'), type: 'png' });
    await capture(page, 'tools.html', 'tools-page.png', '#tool-area');
    const heroData = fs.readFileSync(path.join(output, 'hero-dashboard.png')).toString('base64');
    const social = await browser.newPage({ viewport: { width: 1280, height: 640 }, deviceScaleFactor: 1 });
    await social.setContent(`<!doctype html><style>body{margin:0;background:#0c1218;color:#eef4f8;font:700 48px Arial,sans-serif;overflow:hidden}.copy{position:absolute;z-index:1;left:72px;top:104px;width:580px}.copy p{font-size:26px;line-height:1.35;color:#c8d3dd;font-weight:400}.rule{width:88px;height:7px;background:#ff6a00;margin-bottom:30px}.shot{position:absolute;right:-85px;bottom:-95px;width:760px;border:2px solid #354553;border-radius:18px;transform:rotate(-3deg);box-shadow:0 30px 80px #000}</style><div class="copy"><div class="rule"></div>OpenTurbine<p>Open-source turbine ECU for ESP32</p></div><img class="shot" src="data:image/png;base64,${heroData}"></html>`);
    await social.screenshot({ path: path.join(output, 'social-preview.png'), type: 'png' });
  } finally {
    await browser.close();
  }
  console.log(`Captured public UI screenshots in ${output}`);
  process.exit(0);
})().catch((error) => { console.error(error.stack || error); process.exit(1); });

const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const { chromium } = require('playwright');

// The firmware/UI source assertions below read repo-relative paths (src/, data_src/),
// so anchor the working directory to the repo root regardless of where node was launched.
process.chdir(path.resolve(__dirname, '..'));

const port = 10300 + Math.floor(Math.random() * 500);
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

async function shown(page, selector) {
  return page.evaluate(sel => {
    const el = document.querySelector(sel);
    return !!el && getComputedStyle(el).display !== 'none' && el.getClientRects().length > 0;
  }, selector);
}

async function pageSweep(page, viewport) {
  await page.setViewportSize(viewport);
  const pages = [
    ['index.html', '#n1-card'],
    ['hardware.html', '#f-profile-id'],
    ['config.html', 'body'],
    ['sequence.html', '#save-btn'],
    ['calibration.html', 'body'],
    ['tools.html', 'body'],
    ['log.html', 'body']
  ];
  for (const [route, selector] of pages) {
    await page.goto(`${base}/${route}`);
    await page.waitForSelector(selector, { state: 'attached' });
    await page.waitForTimeout(250);
    const metrics = await page.evaluate(() => ({
      title: document.title,
      textLength: document.body.innerText.trim().length,
      overflow: Math.max(0, document.documentElement.scrollWidth - document.documentElement.clientWidth),
      visibleInputs: document.querySelectorAll('input:not([type="hidden"]),select,button').length
    }));
    assert.ok(metrics.textLength > 50, `${route} should render meaningful content`);
    assert.ok(metrics.overflow <= 24, `${route} has horizontal overflow ${metrics.overflow}px at ${viewport.width}px`);
    assert.ok(metrics.visibleInputs > 0 || route === 'index.html' || route === 'log.html', `${route} should expose controls`);
  }
}

function enumNames(source, marker) {
  const start = source.indexOf(marker);
  assert.notEqual(start, -1, `missing ${marker}`);
  const body = source.slice(start, source.indexOf('};', start));
  return [...body.matchAll(/\b(F_[A-Z0-9_]+)\b/g)].map(match => match[1]);
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
    if (message.type() === 'error' && !/Failed to load resource/i.test(message.text())) {
      consoleErrors.push(message.text());
    }
  });
  page.on('response', response => {
    if (response.status() >= 400 && !/\/favicon\.ico($|\?)/.test(response.url())) {
      badResponses.push(`${response.status()} ${response.url()}`);
    }
  });

  const results = [];
  try {
    await reset(page);

    for (const viewport of [{ width: 1366, height: 768 }, { width: 390, height: 844 }]) {
      await pageSweep(page, viewport);
    }
    results.push('all main pages render without console errors or major horizontal overflow on desktop and mobile widths');

    const exported = await (await page.request.get(`${base}/api/ecu_config`)).json();
    assert.equal(exported.hardware.profile_id, exported.settings.profile_id);
    exported.hardware.profile_id = 'beta-release-engine';
    exported.settings.profile_id = 'beta-release-engine';
    let response = await page.request.post(`${base}/api/ecu_config`, { data: exported });
    assert.equal(response.ok(), true, 'matching full ECU file should restore');
    const crossed = structuredClone(exported);
    crossed.settings.profile_id = 'wrong-engine';
    response = await page.request.post(`${base}/api/ecu_config`, { data: crossed });
    assert.equal(response.status(), 400, 'crossed ECU file should be rejected');
    response = await page.request.post(`${base}/api/factory_reset`);
    assert.equal(response.ok(), true, 'factory reset endpoint should complete');
    const afterReset = await (await page.request.get(`${base}/api/ecu_config`)).json();
    assert.equal(afterReset.hardware.profile_id, afterReset.settings.profile_id);
    results.push('full ECU export/restore, crossed-file rejection, and factory reset path behave consistently');

    await reset(page);
    let hardware = await (await page.request.get(`${base}/api/hardware`)).json();
    const oilTempBefore = structuredClone(hardware.sensors.oil_temp);
    response = await page.request.patch(`${base}/api/hardware`, {
      data: { sensors: { oil_temp: { ntc_beta: oilTempBefore.ntc_beta + 25 } } }
    });
    assert.equal(response.ok(), true, 'nested calibration hardware patch should save');
    hardware = await (await page.request.get(`${base}/api/hardware`)).json();
    assert.equal(hardware.sensors.oil_temp.ntc_beta, oilTempBefore.ntc_beta + 25);
    assert.equal(hardware.sensors.oil_temp.enabled, oilTempBefore.enabled);
    assert.equal(hardware.sensors.oil_temp.pin, oilTempBefore.pin);
    assert.equal(hardware.sensors.oil_temp.chip, oilTempBefore.chip);
    response = await page.request.patch(`${base}/api/hardware`, {
      data: { sensors: { oil_temp: { use_raw_poly: true, poly_a: 0.000001, poly_b: -0.001, poly_c: 0.5, poly_d: -20, poly_x_min: 420, poly_x_max: 3400 } } }
    });
    assert.equal(response.ok(), true, 'known-point oil-temperature range should save');
    hardware = await (await page.request.get(`${base}/api/hardware`)).json();
    assert.equal(hardware.sensors.oil_temp.use_raw_poly, true);
    assert.equal(hardware.sensors.oil_temp.poly_x_min, 420);
    assert.equal(hardware.sensors.oil_temp.poly_x_max, 3400);
    assert.equal(hardware.sensors.oil_temp.pin, oilTempBefore.pin);
    results.push('nested calibration PATCH preserves sibling hardware topology fields');

    await reset(page);
    const profiles = [
      {
        name: 'single-shaft minimal cluster TX-only',
        patch: {
          has_two_shaft: false,
          has_afterburner: false,
          cluster_serial: { enabled: true, tx_pin: 5, rx_pin: -1 },
          sensors: { n2_rpm: { enabled: true }, tit: { enabled: false }, fuel_press: { enabled: false } },
          actuators: { prop_pitch: { enabled: false }, ab_sol: { enabled: true }, ab_pump: { enabled: true } }
        },
        checks: async () => {
          assert.equal(await shown(page, '#section-n2rpm'), false);
          assert.equal(await shown(page, '#section-ab-actuators'), false);
          assert.equal(await page.locator('#f-cl-rx option[value="-1"]').count(), 1);
        }
      },
      {
        name: 'two-shaft turboprop governor',
        patch: {
          has_two_shaft: true,
          has_afterburner: false,
          sensors: { n2_rpm: { enabled: true } },
          actuators: { prop_pitch: { enabled: true } },
          controllers: { governor: true }
        },
        checks: async () => {
          assert.equal(await shown(page, '#section-n2rpm'), true);
          assert.equal(await page.locator('#en-proppitch').isChecked(), true);
          assert.equal(await page.locator('#f-ctrl-gov').isChecked(), true);
        }
      },
      {
        name: 'afterburner hardware',
        patch: {
          has_afterburner: true,
          actuators: { ab_sol: { enabled: true }, ab_pump: { enabled: true }, igniter2: { enabled: true } },
          ab_flame: { enabled: true }
        },
        checks: async () => {
          assert.equal(await shown(page, '#section-ab-actuators'), true);
          assert.equal(await shown(page, '#grp-ab-flame'), true);
        }
      },
      {
        name: 'ESP32-S3 external telemetry',
        patch: {
          platform: 'esp32s3',
          cluster_serial: { enabled: true, tx_pin: 1, rx_pin: 46 },
          mavlink: { enabled: true, tx_pin: 4 },
          buzzer: { enabled: true, pin: 6 },
          actuators: { status_led: { enabled: true, pin: 5 } }
        },
        checks: async () => {
          assert.equal(await page.locator('#f-cl-rx option[value="46"]').count(), 1);
          assert.equal(await page.locator('#f-mav-tx option[value="46"]').count(), 0);
          assert.equal(await page.locator('#f-led-pin option[value="46"]').count(), 0);
          assert.equal(await page.locator('#f-buzzer-pin option[value="46"]').count(), 0);
        }
      }
    ];
    for (const profile of profiles) {
      await reset(page);
      await patchHardware(page, profile.patch);
      await page.goto(`${base}/hardware.html`);
      await page.waitForSelector('#f-profile-id', { state: 'attached' });
      const hardwareViewToggle = page.locator('#btn-hide-unsel-act');
      if (((await hardwareViewToggle.textContent()) || '').includes('Show full editor')) await hardwareViewToggle.click();
      await profile.checks();
    }
    results.push(`representative hardware profiles render expected feature gates (${profiles.map(p => p.name).join(', ')})`);

    await reset(page);
    await patchHardware(page, {
      has_two_shaft: false,
      has_afterburner: false,
      sensors: { n2_rpm: { enabled: true } },
      actuators: {
        prop_pitch: { enabled: false },
        glow_plug: { enabled: false },
        fuel_pump2: { enabled: false },
        bleed_valve: { enabled: false },
        oil_scavenge_pump: { enabled: false }
      },
      controllers: { governor: true }
    });
    await page.goto(`${base}/sequence.html`);
    await page.waitForSelector('#save-btn', { state: 'attached' });
    const sequencer = await page.evaluate(() => {
      const optionText = [...document.querySelectorAll('#add-startup-sel option,#add-shutdown-sel option')].map(o => o.textContent);
      const ruleText = [...document.querySelectorAll('.rule-sensor option,.rule-actuator option')].map(o => o.textContent);
      return {
        hasAbBlock: optionText.some(t => /\bAB\b|Afterburner/i.test(t)),
        abTabVisible: getComputedStyle(document.getElementById('tab-btn-afterburner')).display !== 'none',
        hasGovernorBlock: optionText.some(t => /Governor/i.test(t)),
        hasHiddenN2Rule: ruleText.some(t => /\bN2\b/i.test(t)),
        hasPropRule: ruleText.some(t => /Prop/i.test(t))
      };
    });
    assert.deepEqual(sequencer, { hasAbBlock: false, abTabVisible: false, hasGovernorBlock: false, hasHiddenN2Rule: false, hasPropRule: false });
    results.push('sequencer options and rule dependencies stay filtered when master hardware features are disabled');

    const ecuCluster = fs.readFileSync(path.join('src', 'system', 'ClusterSerial.cpp'), 'utf8');
    const client = fs.readFileSync(path.join('examples', 'OTCClusterClient.h'), 'utf8');
    assert.deepEqual(enumNames(client, 'enum FieldId'), enumNames(ecuCluster, 'enum FieldId'));
    assert.match(ecuCluster, /bool hasPrimaryEgt\(\) \{ return Config::effectiveEgtSource\(\) != 0; \}/);
    assert.match(ecuCluster, /bool hasShaftPower\(\) \{ return HardwareConfig::hasTorque && HardwareConfig::hasN2Rpm; \}/);
    assert.match(ecuCluster, /\{ F_TOT_RATE,[^}]*"TOT_RATE",\s*"EGT rise C\/s",\s*hasPrimaryEgt/s);
    assert.match(ecuCluster, /\{ F_POWER_W,[^}]*"POWER_W",\s*"Power W",\s*hasShaftPower/s);
    assert.match(ecuCluster, /\{ F_THROTTLE_PCT,[^}]*"THROTTLE_PCT",\s*"Throttle pct",\s*hasThrottle/s);
    assert.match(client, /OTC:SUB,ALL/);
    assert.match(client, /OTC:CMD,STOP/);
    assert.match(client, /signalLost\(\)/);
    results.push('cluster example field ids and documented commands match the ECU protocol enum');

    const throttleSlew = fs.readFileSync(path.join('src', 'engine', 'controllers', 'ThrottleSlew.h'), 'utf8');
    // Zero-disabled limits must not produce pullback: the guard now lives
    // centrally inside applyPullback (hard<=soft covers hard==0 disabled).
    assert.match(throttleSlew, /if \(hard <= soft \|\| value <= soft\) return;/);
    const safetyMonitor = fs.readFileSync(path.join('src', 'engine', 'SafetyMonitor.h'), 'utf8');
    // Relight needs a viable N1 AND the CONFIGURED ignition target fitted
    // (igniter / igniter2 / glow — no longer hardcoded to igniter 1).
    assert.match(safetyMonitor, /relightIgnitionOk && n1Ok/);
    assert.match(safetyMonitor, /case 1: relightIgnitionOk = HardwareConfig::hasIgniter2/);
    const configSource = fs.readFileSync(path.join('src', 'system', 'Config.cpp'), 'utf8');
    // Relight sanitization is target-aware: disabled when N1 is missing or
    // the SELECTED ignition output (igniter/igniter2/glow) is not fitted.
    assert.match(configSource, /case 1: relightTargetAvailable = HardwareConfig::hasIgniter2/);
    assert.match(configSource, /\(!HardwareConfig::hasN1Rpm \|\| !relightTargetAvailable\) && relightEnabled[\s\S]{0,80}relightEnabled = false/);
    assert.match(configSource, /case 6:\s+return HardwareConfig::hasTwoShaft && HardwareConfig::hasN2Rpm/);
    assert.match(configSource, /case 19:\s+return HardwareConfig::hasAfterburner && HardwareConfig::hasAbFlame/);
    assert.match(configSource, /case 20:\s+return HardwareConfig::hasGlowPlug && HardwareConfig::hasGlowCurrentSensor/);
    assert.match(configSource, /case 21:\s+return HardwareConfig::hasIgniter && HardwareConfig::hasIgniterCurrentSensor/);
    assert.match(configSource, /case 22:\s+return HardwareConfig::hasIgniter2 && HardwareConfig::hasIgniter2CurrentSensor/);
    assert.match(configSource, /case 23:\s+return HardwareConfig::hasOilPump && HardwareConfig::hasOilPumpCurrentSensor/);
    const rulesEngine = fs.readFileSync(path.join('src', 'system', 'RulesEngine.h'), 'utf8');
    assert.match(rulesEngine, /case N2_RPM:\s+return HardwareConfig::hasTwoShaft && HardwareConfig::hasN2Rpm && ed\.n2Healthy/);
    assert.match(rulesEngine, /case AB_FLAME:\s+return HardwareConfig::hasAfterburner && HardwareConfig::hasAbFlame/);
    assert.match(rulesEngine, /case GLOW_CURRENT:\s+return HardwareConfig::hasGlowPlug && HardwareConfig::hasGlowCurrentSensor/);
    const mainSource = fs.readFileSync(path.join('src', 'main.cpp'), 'utf8');
    assert.match(mainSource, /Selected EGT hard limit is 0 - overtemperature shutdown is disabled", false/);
    assert.match(mainSource, /Running oil minimum is 0 - low-oil shutdown is disabled", false/);
    assert.match(mainSource, /EGT flameout drop is 0 - EGT-source flameout detection is disabled", false/);
    assert.match(mainSource, /EGT relight recovery rise is 0 - EGT-source relight cannot confirm success", false/);
    assert.match(mainSource, /Minimum N1 for relight is 0 - windmill viability check is effectively disabled", false/);
    assert.match(mainSource, /Relight timeout is 0 - igniter can stay active indefinitely during a failed relight attempt", false/);
    assert.match(mainSource, /START blocked: actuator tool active/);
    assert.match(mainSource, /Cannot start: an actuator test or prime tool is still active/);
    assert.match(mainSource, /auto checkAbActuatorBlockHardware = \[\&\]\(const char\* nm\)/);
    assert.match(mainSource, /for \(int i = 0; i < _abShutCount; i\+\+\)[\s\S]*checkAbActuatorBlockHardware\(_abShutBlocks\[i\]->name\(\)\)/);
    assert.match(mainSource, /class CustomSequenceBlock : public IBlock/);
    assert.match(mainSource, /RulesEngine::sensorConditionMet\(_def->sensor, _def->op, _def->threshold\)/);
    assert.match(mainSource, /RulesEngine::applyActuatorDemand\(step\.actuator, step\.value\)/);
    assert.match(mainSource, /custom\.bind\(def\);[\s\S]*blocks\[count\+\+\] = &custom/);
    assert.match(mainSource, /auto customDefFor = \[\&\]\(const char\* key\)/);
    assert.doesNotMatch(mainSource, /if \(EngineData::instance\(\)\.benchMode\) return BlockResult::Complete;/);
    assert.match(mainSource, /const bool benchMode = EngineData::instance\(\)\.benchMode/);
    assert.doesNotMatch(mainSource, /Overtemp safety requires a selected TOT\/TIT source and a limit above zero", true/);
    const hardwareHeader = fs.readFileSync(path.join('src', 'system', 'HardwareConfig.h'), 'utf8');
    assert.match(hardwareHeader, /MAX_CUSTOM_BLOCKS = 8/);
    assert.match(hardwareHeader, /struct CustomBlockDef/);
    const hardwareConfig = fs.readFileSync(path.join('src', 'system', 'HardwareConfig.cpp'), 'utf8');
    assert.match(hardwareConfig, /void writeCustomBlocks\(JsonDocument& doc\)/);
    assert.match(hardwareConfig, /void readCustomBlocks\(const JsonDocument& doc\)/);
    assert.match(hardwareConfig, /doc\["custom_blocks"\]/);
    assert.match(hardwareConfig, /strncmp\(name, "custom_", 7\) == 0\) return customBlockAvailable\(name\)/);
    const webSource = fs.readFileSync(path.join('src', 'system', 'web', 'WebServer.cpp'), 'utf8');
    assert.match(webSource, /doc\["has_ab_flame"\]\s+=\s+HardwareConfig::hasAfterburner && HardwareConfig::hasAbFlame/);
    assert.match(webSource, /doc\["has_n2"\]\s+=\s+HardwareConfig::hasTwoShaft && HardwareConfig::hasN2Rpm/);
    assert.match(webSource, /doc\["has_glow_current"\]\s+=\s+HardwareConfig::hasGlowPlug && HardwareConfig::hasGlowCurrentSensor/);
    assert.match(webSource, /doc\["has_igniter_current"\]\s+=\s+HardwareConfig::hasIgniter && HardwareConfig::hasIgniterCurrentSensor/);
    assert.match(webSource, /doc\["has_igniter2_current"\]\s+=\s+HardwareConfig::hasIgniter2 && HardwareConfig::hasIgniter2CurrentSensor/);
    assert.match(webSource, /doc\["has_oil_pump_current"\]\s+=\s+HardwareConfig::hasOilPump && HardwareConfig::hasOilPumpCurrentSensor/);
    assert.match(webSource, /doc\["uptime_s"\]\s+=\s+ed\.uptimeMs \/ 1000;\s*doc\["boot_count"\]\s+=\s+ed\.bootCount/);
    assert.match(webSource, /doc\["config_storage_fault"\]\s+=\s+ed\.configStorageFault/);
    assert.match(webSource, /char lineBuf\[640\];[\s\S]*readBytesUntil\('\\n', lineBuf, sizeof\(lineBuf\) - 1\)/);
    assert.match(webSource, /JsonDocument doc;[\s\S]*char lineBuf\[640\];[\s\S]*deserializeJson\(doc, lineBuf\)/);
    const calibrationHtml = fs.readFileSync(path.join('data_src', 'calibration.html'), 'utf8');
    assert.match(calibrationHtml, /const hasAfterburner = hw\?\.has_afterburner === true \|\| hw\?\.afterburner\?\.enabled === true/);
    assert.match(calibrationHtml, /return !!\(hasAfterburner && \(hw\?\.ab_flame\?\.enabled \|\| hw\?\.has_ab_flame\)\)/);
    const sequenceHtml = fs.readFileSync(path.join('data_src', 'sequence.html'), 'utf8');
    for (const tab of ['startup', 'shutdown', 'afterburner', 'ab-shut']) {
      assert.match(sequenceHtml, new RegExp(`openCustomBlockDialog\\('${tab}'\\)`));
    }
    assert.match(sequenceHtml, /const MAX_CUSTOM_BLOCKS = 8/);
    assert.match(sequenceHtml, /const MAX_CUSTOM_STEPS = 8/);
    assert.match(sequenceHtml, /const MAX_CUSTOM_KEY_LEN = 23/);
    assert.match(sequenceHtml, /validateCustomBlockLimits\(\)/);
    assert.match(sequenceHtml, /def\.type === 'wait' \? \[\]/);
    assert.match(sequenceHtml, /if \(type !== 'wait'\) rawDef\.steps/);
    assert.match(sequenceHtml, /validateRulesForSave\(\)/);
    assert.match(sequenceHtml, /Control rule hardware mismatch/);
    results.push('runtime safety guards cover zero limits, inactive EGT flameout, and stale auto-relight prerequisites');

    const indexHtml = fs.readFileSync(path.join('data_src', 'index.html'), 'utf8');
    assert.doesNotMatch(indexHtml, /20260612b|20260617b|20260619a|20260625a|20260705a|Primary thermal limit/);
    assert.match(indexHtml, /20260711f/);
    assert.match(indexHtml, /<body data-page="dashboard">/);
    assert.match(indexHtml, /id="profile-mismatch-banner" style="display:none"/);
    const appSource = fs.readFileSync(path.join('data_src', 'app.js'), 'utf8');
    assert.match(appSource, /let _lastBootCount = null/);
    assert.match(appSource, /nextUptime <= 5 && _lastUptimeS > 5/);
    assert.match(appSource, /bootChanged && usesGlobalTelemetry\(\)/);
    results.push('dashboard asset cache key and selected-EGT tooltip are beta-current');

    await page.goto(`${base}/generate_204`);
    await page.waitForSelector('#n1-card', { state: 'attached' });
    const portalBoot = await page.evaluate(() => {
      startTelemetryBoot();
      return {
        path: location.pathname,
        isLive: isLiveTelemetryPage(),
        isDashboard: isDashboardPage(),
        pullPeriod: _pullPeriodMs
      };
    });
    assert.deepEqual(portalBoot, {
      path: '/generate_204',
      isLive: true,
      isDashboard: true,
      pullPeriod: 333
    });
    results.push('captive portal dashboard entry starts the same 3 Hz telemetry pull as /index.html');

    const webServer = fs.readFileSync(path.join('src', 'system', 'web', 'WebServer.cpp'), 'utf8');
    // Current strategy: shared assets served with no-cache (query-string
    // ?v= keys bust stale copies; the server does not use immutable caching).
    assert.match(webServer, /SHARED_ASSET_CACHE = "public, max-age=31536000, immutable"/);
    assert.match(webServer, /app\.js\.gz", "application\/javascript", SHARED_ASSET_CACHE/);
    assert.match(webServer, /style\.css\.gz", "text\/css", SHARED_ASSET_CACHE/);
    assert.match(webServer, /static void _mergeJsonObject\(JsonObject dst, JsonObjectConst patch\)/);
    assert.equal((webServer.match(/_mergeJsonObject\(current\.as<JsonObject>\(\), patch\.as<JsonObjectConst>\(\)\)/g) || []).length, 2);
    assert.doesNotMatch(webServer, /2-level deep merge/);
    results.push('firmware PATCH handlers use recursive JSON merge for nested objects');

    await reset(page);
    await page.goto(`${base}/index.html`);
    await page.waitForSelector('#n1-card', { state: 'attached' });
    const scenarios = ['full', 'startup', 'fault', 'minimal'];
    for (let i = 0; i < 240; i++) {
      const name = scenarios[i % scenarios.length];
      response = await page.request.post(`${base}/__sim/scenario/${name}`);
      assert.equal(response.ok(), true);
      if (i % 20 === 0) await page.waitForTimeout(120);
    }
    await page.waitForTimeout(750);
    const dashText = await page.locator('body').innerText();
    assert.match(dashText, /N1|TOT|STANDBY|RUNNING|FAULT/);
    results.push('dashboard survives repeated telemetry scenario changes during a short mock soak');

    assert.deepEqual(consoleErrors, [], 'browser console should stay free of errors');
    assert.deepEqual(badResponses, [], 'browser should not request missing app resources');
    console.log(`Beta release audit passed (${results.length} groups):`);
    for (const result of results) console.log(`- ${result}`);
  } finally {
    await browser.close();
  }
  process.exit(0);
})().catch(error => {
  console.error(error.stack || error);
  process.exit(1);
});

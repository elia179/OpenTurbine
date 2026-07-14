const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const { chromium } = require('playwright');

const port = 8800 + Math.floor(Math.random() * 500);
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

async function shown(page, selector) {
  return page.evaluate(sel => {
    const el = document.querySelector(sel);
    return !!el && getComputedStyle(el).display !== 'none';
  }, selector);
}

async function disabled(page, selector) {
  return page.evaluate(sel => document.querySelector(sel)?.disabled ?? null, selector);
}

async function checked(page, selector) {
  return page.evaluate(sel => document.querySelector(sel)?.checked ?? null, selector);
}

async function reset(page) {
  const response = await page.request.post(`${base}/__sim/reset`);
  assert.equal(response.ok(), true);
}

async function patchHardware(page, patch) {
  const response = await page.request.patch(`${base}/api/hardware`, { data: patch });
  assert.equal(response.ok(), true);
}

async function patchData(page, patch) {
  const response = await page.request.post(`${base}/__sim/data`, { data: patch });
  assert.equal(response.ok(), true);
}

async function goto(page, route, waitSelector) {
  await page.goto(`${base}/${route}`);
  await page.waitForSelector(waitSelector, { state: 'attached' });
}

(async () => {
  globalThis.OT_UI_SIM_PORT = port;
  await import('./ui_mock_server.mjs');
  const executablePath = installedBrowser();
  const browser = await chromium.launch({ headless: true, ...(executablePath ? { executablePath } : {}) });
  const page = await browser.newPage();
  page.on('pageerror', error => {
    throw error;
  });

  const results = [];
  try {
    await reset(page);

    await goto(page, 'hardware.html', '#f-profile-id');
    assert.equal(await page.locator('#f-saf-lowfuel').count(), 0);
    assert.equal(await page.evaluate(() => 'low_fuel' in (cfg.safety || {})), false);
    results.push('hardware page does not expose unsupported low-fuel flow safety');

    const safetyMatrix = await page.evaluate(() => {
      function st(id) {
        const el = document.getElementById(id);
        return { disabled: !!el?.disabled, checked: !!el?.checked };
      }
      function setSensor(key, enabled) {
        cfg.sensors[key].enabled = enabled;
      }
      cfg.safety.overspeed = true;
      cfg.safety.surge = true;
      setSensor('n1_rpm', false);
      updateSafetyPrerequisites(true);
      const n1Off = { overspeed: st('f-saf-overspeed'), surge: st('f-saf-surge') };

      cfg.safety.overtemp = true;
      cfg.safety.hot_start = true;
      setSensor('tot', false);
      setSensor('tit', false);
      updateSafetyPrerequisites(true);
      const totOff = { overtemp: st('f-saf-overtemp'), hotStart: st('f-saf-hotstart') };

      cfg.safety.flameout = true;
      setSensor('flame', false);
      setSensor('n1_rpm', false);
      setSensor('tot', false);
      setSensor('tit', false);
      updateSafetyPrerequisites(true);
      const combustionOff = { flameout: st('f-saf-flameout') };

      cfg.safety.low_oil = true;
      cfg.safety.oil_zero = true;
      cfg.controllers.oil_loop = true;
      setSensor('oil_press', false);
      updateSafetyPrerequisites(true);
      updateHardwarePrerequisites(true);
      const oilOff = {
        lowOil: st('f-saf-lowoil'),
        oilZero: st('f-saf-oilzero'),
        oilLoop: st('f-ctrl-oil')
      };

      cfg.safety.fuel_press_low = true;
      setSensor('fuel_press', false);
      cfg.safety.batt_low = true;
      setSensor('batt_voltage', false);
      setSensor('tit', false);
      cfg.safety.oil_temp_high = true;
      setSensor('oil_temp', false);
      updateSafetyPrerequisites(true);
      const optionalOff = {
        fuelPress: st('f-saf-fuelpresslo'),
        batt: st('f-saf-battlo'),
        oilTemp: st('f-saf-oiltemphi')
      };
      return { n1Off, totOff, combustionOff, oilOff, optionalOff };
    });
    assert.equal(await page.locator('#f-saf-titovertemp').count(), 0);
    for (const group of Object.values(safetyMatrix)) {
      for (const state of Object.values(group)) {
        assert.equal(state.disabled, true);
        assert.equal(state.checked, false);
      }
    }
    results.push('hardware safety cards disable and uncheck when required sensors disappear');

    const hardwareDeps = await page.evaluate(() => {
      cfg.has_two_shaft = false;
      cfg.sensors.n2_rpm.enabled = true;
      cfg.controllers.governor = true;
      cfg.controllers.dynamic_idle = true;
      cfg.actuators.throttle.enabled = true;
      cfg.sensors.n1_rpm.enabled = false;
      updateFeaturesUI();
      updateHardwarePrerequisites(true);
      const singleShaft = {
        n2Visible: getComputedStyle(document.getElementById('section-n2rpm')).display !== 'none',
        governorDisabled: document.getElementById('f-ctrl-gov').disabled,
        governorChecked: document.getElementById('f-ctrl-gov').checked,
        idleDisabled: document.getElementById('f-ctrl-idle').disabled,
        idleChecked: document.getElementById('f-ctrl-idle').checked
      };

      cfg.has_two_shaft = true;
      cfg.sensors.n2_rpm.enabled = true;
      cfg.actuators.prop_pitch.enabled = true;
      cfg.controllers.governor = true;
      cfg.controllers.dynamic_idle = true;
      updateFeaturesUI();
      updateHardwarePrerequisites(true);
      const twinShaft = {
        n2Visible: getComputedStyle(document.getElementById('section-n2rpm')).display !== 'none',
        governorDisabled: document.getElementById('f-ctrl-gov').disabled,
        idleDisabled: document.getElementById('f-ctrl-idle').disabled
      };

      cfg.has_afterburner = false;
      updateFeaturesUI();
      const noAb = {
        abVisible: getComputedStyle(document.getElementById('section-ab-actuators')).display !== 'none',
        ign2Label: document.getElementById('lbl-igniter2').textContent.trim()
      };

      cfg.has_afterburner = true;
      updateFeaturesUI();
      const withAb = {
        abVisible: getComputedStyle(document.getElementById('section-ab-actuators')).display !== 'none',
        ign2Label: document.getElementById('lbl-igniter2').textContent.trim()
      };
      return { singleShaft, twinShaft, noAb, withAb };
    });
    assert.deepEqual(hardwareDeps.singleShaft, {
      n2Visible: false, governorDisabled: true, governorChecked: false, idleDisabled: true, idleChecked: false
    });
    assert.equal(hardwareDeps.twinShaft.n2Visible, true);
    assert.equal(hardwareDeps.twinShaft.governorDisabled, false);
    assert.equal(hardwareDeps.twinShaft.idleDisabled, false);
    assert.equal(hardwareDeps.noAb.abVisible, false);
    assert.equal(hardwareDeps.noAb.ign2Label, 'Igniter 2');
    assert.equal(hardwareDeps.withAb.abVisible, true);
    assert.match(hardwareDeps.withAb.ign2Label, /AB Igniter/);
    results.push('hardware master feature flags gate N2, governor, dynamic idle, and afterburner children');

    const typeGroups = await page.evaluate(() => {
      function visible(id) {
        const el = document.getElementById(id);
        return !!el && el.style.display !== 'none';
      }
      const out = {};
      setThrType(0); out.thrServo = [visible('grp-thr-servo'), visible('grp-thr-ledc'), visible('grp-thr-onoff')];
      setThrType(1); out.thrPwm = [visible('grp-thr-servo'), visible('grp-thr-ledc'), visible('grp-thr-onoff')];
      setThrType(2); out.thrRelay = [visible('grp-thr-servo'), visible('grp-thr-ledc'), visible('grp-thr-onoff')];
      setTotChip('max31855'); out.tot31855 = [visible('grp-tot-mosi'), visible('grp-tot-tctype')];
      setTotChip('max31856'); out.tot31856 = [visible('grp-tot-mosi'), visible('grp-tot-tctype')];
      setOilTempChip('ntc'); out.oilNtc = [visible('grp-oiltemp-ntc'), visible('grp-oiltemp-onewire'), visible('grp-oiltemp-spi')];
      setOilTempChip('ds18b20'); out.oilOneWire = [visible('grp-oiltemp-ntc'), visible('grp-oiltemp-onewire'), visible('grp-oiltemp-spi')];
      setOilTempChip('max31856'); out.oilSpi = [visible('grp-oiltemp-ntc'), visible('grp-oiltemp-onewire'), visible('grp-oiltemp-spi'), visible('grp-oiltemp-mosi')];
      setTorqueType(0); out.torqueAdc = [visible('torque-adc-pin'), visible('torque-hx-dt')];
      setTorqueType(1); out.torqueHx = [visible('torque-adc-pin'), visible('torque-hx-dt')];
      document.getElementById('f-fuelflow-type').value = '0'; updateFuelFlowTypeUI();
      out.fuelFlowAnalog = [visible('grp-fuelflow-ppl'), document.getElementById('f-fuelflow-pin-hint').textContent];
      document.getElementById('f-fuelflow-type').value = '1'; updateFuelFlowTypeUI();
      out.fuelFlowPulse = [visible('grp-fuelflow-ppl'), document.getElementById('f-fuelflow-pin-hint').textContent];
      cfg.ab_trigger.source = 0; updateAbTrigUI(0); out.abSrcThrottle = [visible('grp-ab-sw'), visible('grp-ab-inp'), visible('grp-ab-arm')];
      cfg.ab_trigger.source = 2; updateAbTrigUI(2); out.abSrcSwitch = [visible('grp-ab-sw'), visible('grp-ab-inp'), visible('grp-ab-arm')];
      return out;
    });
    assert.deepEqual(typeGroups.thrServo, [true, false, false]);
    assert.deepEqual(typeGroups.thrPwm, [false, true, false]);
    assert.deepEqual(typeGroups.thrRelay, [false, false, true]);
    assert.deepEqual(typeGroups.tot31855, [false, false]);
    assert.deepEqual(typeGroups.tot31856, [true, true]);
    assert.deepEqual(typeGroups.oilNtc, [true, false, false]);
    assert.deepEqual(typeGroups.oilOneWire, [false, true, false]);
    assert.deepEqual(typeGroups.oilSpi, [false, false, true, true]);
    assert.deepEqual(typeGroups.torqueAdc, [true, false]);
    assert.deepEqual(typeGroups.torqueHx, [false, true]);
    assert.equal(typeGroups.fuelFlowAnalog[0], false);
    assert.match(typeGroups.fuelFlowAnalog[1], /ADC1-capable/);
    assert.equal(typeGroups.fuelFlowPulse[0], true);
    assert.match(typeGroups.fuelFlowPulse[1], /digital-capable/);
    assert.deepEqual(typeGroups.abSrcThrottle, [false, true, false]);
    assert.deepEqual(typeGroups.abSrcSwitch, [true, true, true]);
    results.push('hardware type selectors show the correct servo/PWM/relay, thermocouple, torque, fuel-flow, and AB input fields');

    await patchHardware(page, { platform: 'esp32s3' });
    await goto(page, 'hardware.html', '#f-thr-pin');
    assert.equal(await page.locator('#f-thr-pin option[value="22"]').count(), 0);
    assert.equal(await page.locator('#f-oilpress-pin option[value="1"]').count(), 1);
    assert.equal(await page.locator('#f-cl-tx option[value="22"]').count(), 0);
    assert.equal(await page.locator('#f-cl-rx option[value="22"]').count(), 0);
    assert.equal(await page.locator('#f-cl-rx option[value="-1"]').count(), 1);
    results.push('hardware GPIO lists switch to ESP32-S3-safe output, input, ADC, and cluster RX choices');

    await reset(page);
    await goto(page, 'sequence.html', '#add-startup-sel');
    const seqDeps = await page.evaluate(() => {
      function optionDisabled(selector, value) {
        const opt = document.querySelector(`${selector} option[value="${value}"]`);
        return opt ? opt.disabled : null;
      }
      const fullSensors = getEnabledSensors().map(s => s.key);
      const fullActuators = getEnabledActuators().map(a => a.key);
      hwCfg.has_two_shaft = false;
      hwCfg.has_afterburner = false;
      hwCfg.sensors.n2_rpm.enabled = true;
      hwCfg.actuators.ab_sol.enabled = true;
      hwCfg.actuators.ab_pump.enabled = true;
      populateAddSelects();
      renderRules();
      const hiddenMaster = {
        sensors: getEnabledSensors().map(s => s.key),
        actuators: getEnabledActuators().map(a => a.key),
        abPumpOptionCount: document.querySelectorAll('#add-afterburner-sel option[value="ABPumpOn"]').length,
        ruleN2Disabled: optionDisabled('#rule-sensor-0', '6'),
        ruleAbFlameDisabled: optionDisabled('#rule-sensor-0', '19'),
        ruleAbInputDisabled: optionDisabled('#rule-sensor-0', '24'),
        ruleAbSolDisabled: optionDisabled('#rule-act-0', '11'),
        ruleAbPumpDisabled: optionDisabled('#rule-act-0', '12')
      };
      return { fullSensors, fullActuators, hiddenMaster };
    });
    assert.ok(seqDeps.fullSensors.includes('n1_rpm'));
    assert.ok(seqDeps.fullSensors.includes('n2_rpm'));
    assert.ok(seqDeps.fullActuators.includes('ab_sol'));
    assert.equal(seqDeps.hiddenMaster.sensors.includes('n2_rpm'), false);
    assert.equal(seqDeps.hiddenMaster.actuators.includes('ab_sol'), false);
    assert.equal(seqDeps.hiddenMaster.abPumpOptionCount, 0);
    assert.equal(seqDeps.hiddenMaster.ruleN2Disabled, true);
    assert.equal(seqDeps.hiddenMaster.ruleAbFlameDisabled, true);
    assert.equal(seqDeps.hiddenMaster.ruleAbInputDisabled, true);
    assert.equal(seqDeps.hiddenMaster.ruleAbSolDisabled, true);
    assert.equal(seqDeps.hiddenMaster.ruleAbPumpDisabled, true);
    results.push('sequence and control rules ignore hidden N2/afterburner hardware dependencies');

    await reset(page);
    await goto(page, 'config.html', '#cf-tot_limit');
    const firstTot = await page.locator('#cf-tot_limit').inputValue();
    assert.ok(firstTot === '720' || firstTot === '1328');
    await page.locator('#unit-temp-btn').click();
    const secondTot = await page.locator('#cf-tot_limit').inputValue();
    assert.notEqual(secondTot, firstTot);
    assert.ok(secondTot === '720' || secondTot === '1328');
    await page.locator('#unit-press-btn').click();
    assert.equal(await page.locator('#cf-oil_rm').inputValue(), '17.405');
    await patchHardware(page, {
      has_afterburner: false,
      controllers: { governor: false },
      sensors: { n1_rpm: { enabled: false }, tit: { enabled: false }, oil_temp: { enabled: false }, fuel_press: { enabled: false }, batt_voltage: { enabled: false }, tot: { enabled: false } },
      actuators: { igniter2: { enabled: false }, ab_pump: { enabled: false } },
      ab_flame: { enabled: false },
      ab_trigger: { input_pin: -1 }
    });
    await patchData(page, {
      has_afterburner: false, has_governor: false, has_n1: false, has_tit: false, has_oil_temp: false,
      has_fuel_press: false, has_batt_voltage: false, has_tot: false,
      fuel_press_min: 0, tit_limit: 0
    });
    await page.reload();
    await page.waitForSelector('#cf-tot_limit');
    assert.equal(await shown(page, '#safety-ext-section'), false);
    assert.equal(await shown(page, '#governor-cfg-section'), false);
    for (const selector of ['#ab-cfg-section', '#ab-ign-section', '#ab-flame-section', '#ab-run-section']) {
      assert.equal(await shown(page, selector), false, `${selector} should hide`);
    }
    results.push('config unit conversions preserve meaning and optional sections hide when hardware is absent');

    await reset(page);
    await patchHardware(page, {
      sensors: {
        oil_press: { enabled: false }, flame: { enabled: false }, p1: { enabled: false }, p2: { enabled: false },
        oil_temp: { enabled: false }, batt_voltage: { enabled: false }, torque: { enabled: false },
        fuel_press: { enabled: false }, fuel_flow: { enabled: true, type: 1 },
        throttle_input: { enabled: false }, idle_input: { enabled: false }
      },
      actuators: {
        glow_plug: { enabled: true, has_current: false },
        igniter: { enabled: true, has_current: false },
        igniter2: { enabled: true, has_current: false },
        oil_pump: { enabled: true, has_current: false }
      }
    });
    await goto(page, 'calibration.html', '#oil-press-cal-row');
    await patchData(page, {
      has_oil_press: false, has_flame: false, has_p1: false, has_p2: false,
      has_oil_temp: false, has_batt_voltage: false, has_torque: false,
      has_fuel_press: false, has_fuel_flow: true, fuel_flow_type: 1,
      has_glow_current: false, has_igniter_current: false, has_igniter2_current: false,
      has_oil_pump_current: false, throttle_input_type: 'none', idle_input_type: 'none'
    });
    await page.waitForTimeout(100);
    for (const selector of ['#oil-press-cal-row', '#flame-cal-row', '#p1-cal-row', '#p2-cal-row', '#oiltemp-cal-row', '#battvolt-cal-row', '#torque-cal-row', '#fuelpress-cal-row', '#fuelflow-cal-row', '#throttle-cal-row', '#idle-cal-row']) {
      assert.equal(await shown(page, selector), false, `${selector} should be hidden`);
    }
    await patchHardware(page, {
      sensors: {
        fuel_flow: { enabled: true, type: 0 },
        throttle_input: { enabled: true, rc_pwm: true },
        idle_input: { enabled: true, rc_pwm: true }
      }
    });
    await patchData(page, { has_fuel_flow: true, fuel_flow_type: 0, throttle_input_type: 'servo', throttle_input_us: 1500, idle_input_type: 'servo', idle_input_us: 1300 });
    await page.reload();
    await page.waitForSelector('#oil-press-cal-row', { state: 'attached' });
    await page.waitForFunction(() => /us|µs|Âµs/.test(document.querySelector('#cal-th-raw')?.textContent || ''), null, { timeout: 5000 });
    assert.equal(await shown(page, '#fuelflow-cal-row'), true);
    assert.equal(await shown(page, '#throttle-cal-row'), true);
    assert.equal(await shown(page, '#idle-cal-row'), true);
    assert.match(await page.locator('#cal-th-raw').textContent(), /us|µs/);
    assert.match(await page.locator('#cal-idle-raw').textContent(), /us|µs/);
    results.push('calibration rows and servo/ADC units follow fitted hardware and telemetry type');

    await reset(page);
    await goto(page, 'log.html', '#tab-session');
    await page.locator('#tab-session').click();
    assert.equal(await disabled(page, 'input[data-bit="n2"]'), false);
    assert.equal(await disabled(page, 'input[data-bit="ab"]'), false);
    assert.equal(await disabled(page, 'input[data-bit="prop"]'), false);
    await patchHardware(page, {
      has_afterburner: false,
      has_two_shaft: false,
      sensors: { n2_rpm: { enabled: false } },
      actuators: { prop_pitch: { enabled: false } }
    });
    await page.reload();
    await page.waitForSelector('#tab-session');
    await page.locator('#tab-session').click();
    await page.waitForFunction(() => document.querySelector('input[data-bit="ab"]')?.disabled === true);
    assert.equal(await disabled(page, 'input[data-bit="n2"]'), true);
    assert.equal(await disabled(page, 'input[data-bit="ab"]'), true);
    assert.equal(await disabled(page, 'input[data-bit="prop"]'), true);
    results.push('log session channels ghost when their hardware is not fitted');

    await reset(page);
    await patchData(page, { config_locked: true });
    await goto(page, 'log.html', '#tab-session');
    await page.locator('#tab-session').click();
    await page.waitForFunction(() => document.querySelector('#session-save-btn')?.disabled === true);
    assert.equal(await disabled(page, 'input[data-bit="n1"]'), true);
    assert.equal(await disabled(page, '#log-standby'), true);
    assert.equal(await disabled(page, '#session-save-btn'), true);
    assert.equal(await shown(page, '#session-lock-msg'), true);
    results.push('log session settings respect the saved-config lock state');

    await reset(page);
    await goto(page, 'tools.html', '#tool-area');
    assert.equal(await page.locator('#card-AB_SOL_TEST').isVisible(), true);
    await patchHardware(page, {
      has_afterburner: false,
      controllers: { dynamic_idle: false },
      sensors: { n1_rpm: { enabled: false }, n2_rpm: { enabled: false } },
      actuators: { throttle: { enabled: false }, ab_sol: { enabled: true }, ab_pump: { enabled: true } }
    });
    await page.reload();
    await page.waitForSelector('#tool-area');
    assert.equal(await page.locator('#card-AB_SOL_TEST').count(), 0);
    assert.equal(await page.locator('#card-AB_PUMP_TEST').count(), 0);
    assert.equal(await page.locator('#card-TOGGLE_DYNAMIC_IDLE').count(), 0);
    assert.equal(await page.locator('#card-TOGGLE_LIMP_MODE').count(), 0);
    results.push('tools page hides tests and toggles whose hardware prerequisites are absent');

    console.log(`UI configuration audit passed (${results.length} groups):`);
    results.forEach(result => console.log(`- ${result}`));
  } finally {
    await browser.close();
  }
  process.exit(0);
})().catch(error => {
  console.error(error.stack || error);
  process.exit(1);
});

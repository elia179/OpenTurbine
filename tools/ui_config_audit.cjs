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
      const sst = key => ({ disabled: !safetyAvailability(key).ok, checked: !!cfg.safety[key] });
      const cst = key => ({ disabled: !controllerAvailability(key).ok, checked: !!cfg.controllers[key] });
      function setSensor(key, enabled) {
        cfg.sensors[key].enabled = enabled;
        const purpose = ({
          n1_rpm:'n1_speed', n2_rpm:'n2_speed', tot:'tot', tit:'tit', flame:'flame', oil_press:'oil_pressure',
          fuel_press:'fuel_pressure', batt_voltage:'battery_voltage', oil_temp:'oil_temperature'
        })[key];
        cfg.channel_registry.inputs.forEach(channel => {
          if (registryDerivedPurpose('input', channel) === purpose) channel.installed = enabled;
        });
      }
      cfg.safety.overspeed = true;
      cfg.safety.surge = true;
      setSensor('n1_rpm', false);
      updateSafetyPrerequisites(true);
      const n1Off = { overspeed: sst('overspeed'), surge: sst('surge') };

      cfg.safety.n2_overspeed = true;
      setSensor('n2_rpm', false);
      updateSafetyPrerequisites(true);
      const n2Off = { n2Overspeed: sst('n2_overspeed') };
      setSensor('n2_rpm', true);

      cfg.safety.overtemp = true;
      cfg.safety.hot_start = true;
      setSensor('tot', false);
      setSensor('tit', false);
      updateSafetyPrerequisites(true);
      const totOff = { overtemp: sst('overtemp'), hotStart: sst('hot_start') };

      cfg.safety.flameout = true;
      setSensor('flame', false);
      setSensor('n1_rpm', false);
      setSensor('tot', false);
      setSensor('tit', false);
      updateSafetyPrerequisites(true);
      const combustionOff = { flameout: sst('flameout') };

      cfg.safety.low_oil = true;
      cfg.safety.oil_zero = true;
      cfg.controllers.oil_loop = true;
      setSensor('oil_press', false);
      updateSafetyPrerequisites(true);
      updateHardwarePrerequisites(true);
      const oilOff = {
        lowOil: sst('low_oil'),
        oilZero: sst('oil_zero'),
        oilLoop: cst('oil_loop')
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
        fuelPress: sst('fuel_press_low'),
        batt: sst('batt_low'),
        oilTemp: sst('oil_temp_high')
      };
      return { n1Off, n2Off, totOff, combustionOff, oilOff, optionalOff };
    });
    assert.equal(await page.locator('#f-saf-titovertemp').count(), 0);
    for (const [groupName, group] of Object.entries(safetyMatrix)) {
      for (const [stateName, state] of Object.entries(group)) {
        assert.equal(state.disabled, true, `${groupName}.${stateName} should be disabled`);
        assert.equal(state.checked, false, `${groupName}.${stateName} should be unchecked`);
      }
    }
    results.push('hardware safety cards disable and uncheck when required sensors disappear');

    const hardwareDeps = await page.evaluate(() => {
      cfg.has_two_shaft = false;
      cfg.has_afterburner = false;
      const staleOff = {
        n2: registryHasPurpose('input', 'n2_speed'),
        governor: controllerAvailability('governor').ok,
        afterburner: hardwareHasAfterburner()
      };
      cfg.has_two_shaft = true;
      cfg.has_afterburner = true;
      const staleOn = {
        n2: registryHasPurpose('input', 'n2_speed'),
        governor: controllerAvailability('governor').ok,
        afterburner: hardwareHasAfterburner()
      };
      return { staleOff, staleOn };
    });
    assert.deepEqual(hardwareDeps.staleOff, hardwareDeps.staleOn);
    assert.deepEqual(hardwareDeps.staleOn, { n2:true, governor:true, afterburner:true });
    results.push('hardware derives N2, governor, dynamic idle, and afterburner surfaces from fitted devices');

    const typeGroups = await page.evaluate(() => {
      const tempHtml = registryTemperatureInterfaceEditor({purpose:'tot', role:'temperature', driver:1, temp_interface:2}, 0);
      const oilTempHtml = registryTemperatureInterfaceEditor({purpose:'oil_temperature', role:'temperature', driver:1, temp_interface:4}, 0);
      const torqueHtml = registryTorqueInterfaceEditor('input', {purpose:'torque', role:'torque', driver:1, torque_interface:1}, 0);
      return {
        mainFuel: registryAllowedDrivers('output', 'fuel', 'main_fuel'),
        starter: registryAllowedDrivers('output', 'starter', 'starter'),
        oilPump: registryAllowedDrivers('output', 'oil_pump', 'oil_pump'),
        fuelFlow: registryAllowedDrivers('input', 'flow', 'fuel_flow'),
        abFlame: registryAllowedDrivers('input', 'flame', 'ab_flame'),
        tempInterfaces: ['MAX6675','MAX31855','MAX31856'].every(label => tempHtml.includes(label)) &&
          ['NTC','DS18B20'].every(label => oilTempHtml.includes(label)),
        torqueHx711: torqueHtml.includes('HX711') && torqueHtml.includes('SCK GPIO')
      };
    });
    assert.deepEqual(typeGroups, {
      mainFuel:[5,6], starter:[4,5,6], oilPump:[4,5,6], fuelFlow:[2,1],
      abFlame:[0,1], tempInterfaces:true, torqueHx711:true
    });
    results.push('hardware type selectors show the correct servo/PWM/relay, thermocouple, torque, fuel-flow, and AB input fields');

    await patchHardware(page, { platform: 'esp32s3' });
    await goto(page, 'hardware.html', '#f-profile-id');
    const s3Pins = await page.evaluate(() => ({
      output22: buildPinOptions(22, 'out').includes('value="22"'),
      adc1: buildPinOptions(1, 'adc').includes('value="1"')
    }));
    assert.deepEqual(s3Pins, { output22:false, adc1:true });
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
    assert.equal(seqDeps.hiddenMaster.sensors.includes('n2_rpm'), true);
    assert.equal(seqDeps.hiddenMaster.actuators.includes('ab_sol'), true);
    assert.equal(seqDeps.hiddenMaster.abPumpOptionCount, 1);
    assert.equal(seqDeps.hiddenMaster.ruleN2Disabled, false);
    assert.equal(seqDeps.hiddenMaster.ruleAbFlameDisabled, false);
    assert.equal(seqDeps.hiddenMaster.ruleAbInputDisabled, true);
    assert.equal(seqDeps.hiddenMaster.ruleAbSolDisabled, false);
    assert.equal(seqDeps.hiddenMaster.ruleAbPumpDisabled, false);
    results.push('sequence and control rules follow fitted N2/afterburner devices regardless of obsolete masters');

    await reset(page);
    await patchHardware(page, {
      has_afterburner: false,
      actuators: { igniter2: { enabled: false }, ab_sol: { enabled: false }, ab_pump: { enabled: false } },
      ab_flame: { enabled: false },
      ab_trigger: { source: 0, switch_pin: -1, input_pin: 4 }
    });
    await goto(page, 'config.html', '#cf-tot_limit');
    assert.equal(await page.locator('.config-group').count(), 7,
      'Config workspace should render the canonical grouped navigation');
    assert.equal(await page.locator('#btn-view-basic').getAttribute('class').then(v => v.includes('active')), true);
    assert.equal(await page.locator('#cfg-state-badge').textContent(), 'Saved');
    const essentialCount = await page.locator('.cfg-field:visible').count();
    assert.ok(essentialCount <= 32, `Essentials should stay focused; rendered ${essentialCount} fields`);
    assert.match(await page.locator('#cf-lm_mt').evaluate(el => el.closest('.cfg-field')?.textContent || ''),
      /automatically because feedback used by an enabled protection\/controller becomes unhealthy/i);
    await page.locator('#cfg-search').fill('governor');
    assert.match(await page.locator('#cfg-result-count').textContent(), /^\d+ settings?$/);
    assert.equal(await shown(page, '[data-group="power"]'), true);
    assert.equal(await shown(page, '[data-group="engine"]'), false);
    await page.locator('#cfg-search').fill('');
    assert.ok(await page.locator('.cfg-help').count() > 100,
      'Long engineering help should remain available through progressive disclosure');
    results.push('config workspace groups settings and searches field metadata without losing detailed help');
    const n2RelationshipWarnings = await page.evaluate(() => {
      const setNumber = (key, value) => { const el = document.getElementById('cf-' + key); if (el) el.value = String(value); };
      const setCheck = (key, value) => { const el = document.getElementById('cf-' + key); if (el) el.checked = value; };
      hwCfg.safety.n2_overspeed = true;
      hwCfg.controllers.governor = true;
      hwCfg.controllers.dynamic_idle = true;
      setNumber('n2_rpm_limit', 30000);
      setCheck('pb_n2e', true); setNumber('pb_n2s', 30000); setNumber('pb_n2h', 32000);
      setNumber('gv_tr', 29000); setNumber('gv_bd', 1500);
      setCheck('di_n2', true); setNumber('di_tr', 30000);
      setNumber('cl_n2', 30000);
      runValidation();
      return Array.from(document.querySelectorAll('.cfg-inline-warn')).map(el => el.textContent);
    });
    const n2WarningDetail = JSON.stringify(n2RelationshipWarnings);
    assert.ok(n2RelationshipWarnings.some(text => /pullback/i.test(text)), n2WarningDetail);
    assert.ok(n2RelationshipWarnings.some(text => /Governor target/i.test(text)), n2WarningDetail);
    assert.ok(n2RelationshipWarnings.some(text => /Idle target/i.test(text)), n2WarningDetail);
    assert.ok(n2RelationshipWarnings.some(text => /Cluster N2 warning/i.test(text)), n2WarningDetail);
    results.push('config warns when N2 pullback, governor, idle, or display values do not leave margin below the hard trip');
    await goto(page, 'config.html', '#cf-tot_limit');
    assert.equal(await page.locator('#dev-mode-tools-link').getAttribute('href'), '/tools.html#card-dev-mode');
    assert.equal(await page.locator('#btn-dev-mode').count(), 0,
      'Config must not bypass the guarded Developer Mode control on Tools');
    for (const selector of ['#ab-cfg-section', '#ab-ign-section', '#ab-flame-section']) {
      assert.equal(await shown(page, selector), true, `${selector} should be available in Essentials for fitted AB hardware`);
    }
    assert.equal(await shown(page, '#ab-run-section'), false,
      'specialist afterburner running-fuel tuning belongs under All settings');
    assert.equal(await page.locator('#cf-ab_pcm option[value="2"]').isDisabled(), true,
      'a stale hidden input pin must not enable Dedicated AB Input unless that trigger source is active');
    assert.match(await page.locator('#cf-ab_tt').evaluate(el => el.closest('.cfg-field')?.title || ''), /Hardware.*Afterburner trigger and arm/i);
    results.push('Essentials stays focused while exposing the fitted afterburner commissioning choices');
    const firstTot = await page.locator('#cf-tot_limit').inputValue();
    assert.ok(firstTot === '720' || firstTot === '1328');
    await page.locator('#unit-temp-btn').click();
    const secondTot = await page.locator('#cf-tot_limit').inputValue();
    assert.notEqual(secondTot, firstTot);
    assert.ok(secondTot === '720' || secondTot === '1328');
    await page.locator('#unit-press-btn').click();
    assert.equal(await page.locator('#cf-oil_rm').inputValue(), '17.405');
    const configFullHardware = await (await page.request.get(`${base}/api/hardware`)).json();
    await patchHardware(page, {
      has_afterburner: false,
      controllers: { governor: false },
      sensors: { n1_rpm: { enabled: false }, tit: { enabled: false }, oil_temp: { enabled: false }, fuel_press: { enabled: false }, batt_voltage: { enabled: false }, tot: { enabled: false } },
      actuators: { igniter2: { enabled: false }, ab_pump: { enabled: false } },
      ab_flame: { enabled: false },
      ab_trigger: { switch_pin: -1, input_pin: -1 },
      channel_registry: {
        ...configFullHardware.channel_registry,
        inputs: configFullHardware.channel_registry.inputs.filter(channel =>
          !['n1_speed', 'tot', 'tit', 'oil_temperature', 'fuel_pressure', 'battery_voltage', 'ab_flame'].includes(channel.purpose)),
        outputs: configFullHardware.channel_registry.outputs.filter(channel =>
          !['ab_igniter', 'ab_valve', 'ab_pump'].includes(channel.purpose))
      }
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
    const noThrottleInputHardware = await (await page.request.get(`${base}/api/hardware`)).json();
    await patchHardware(page, {
      sensors: { throttle_input: { enabled: false, pin: -1 } },
      channel_registry: {
        ...noThrottleInputHardware.channel_registry,
        inputs: noThrottleInputHardware.channel_registry.inputs.filter(channel => channel.purpose !== 'throttle')
      }
    });
    await goto(page, 'config.html', '#cf-th_ex');
    assert.equal(await disabled(page, '#cf-th_ex'), true,
      'Throttle Expo must lock when the main fuel output exists but no physical throttle input is fitted');
    assert.match(await page.locator('#cf-th_ex').evaluate(el => el.closest('.cfg-field')?.title || ''),
      /only applies to a physical throttle input/i);
    assert.equal(await disabled(page, '#cf-th_mx'), false,
      'Idle Max remains useful to startup and idle sequence blocks without an operator throttle input');
    results.push('Throttle Expo locks when no physical throttle input can consume it');

    await reset(page);
    const beforePresetConfig = await (await page.request.get(`${base}/api/config`)).json();
    const beforePresetSequence = JSON.parse(JSON.stringify(beforePresetConfig.sequence));
    await goto(page, 'config.html', '#preset-sel');
    await page.locator('#preset-bar > summary').click();
    await page.locator('#preset-sel').selectOption('turboshaft');
    await page.waitForSelector('#ot-app-dialog.show');
    assert.match(await page.locator('#ot-dialog-message').textContent(), /Hardware assignments and sequences are not changed/i);
    await page.locator('#ot-dialog-confirm').click();
    await page.locator('#btn-save').click();
    await page.waitForSelector('#save-recap-modal', {state:'visible'});
    const presetRecap = await page.locator('#save-recap-body').textContent();
    assert.match(presetRecap, /Engine Protection Limits\s*\/\s*Maximum N1 Speed/i);
    assert.match(presetRecap, /Automatic Idle Speed Control\s*\/\s*Idle Speed Target/i);
    assert.doesNotMatch(presetRecap, /live:/i);
    assert.match(await page.locator('#save-recap-subtitle').textContent(), /6 fields/i);
    await page.locator('#save-recap-confirm-btn').click();
    await page.waitForFunction(() => /Saved/i.test(document.querySelector('#save-msg')?.textContent || ''));
    const afterPresetConfig = await (await page.request.get(`${base}/api/config`)).json();
    assert.deepEqual(afterPresetConfig.sequence, beforePresetSequence);
    results.push('engine examples change only reviewable fitted-hardware settings and recap every value with an unambiguous section label');

    await reset(page);
    const calibrationHardware = await (await page.request.get(`${base}/api/hardware`)).json();
    const hiddenCalibrationPurposes = new Set([
      'oil_pressure', 'flame', 'p1_pressure', 'p2_pressure', 'oil_temperature',
      'battery_voltage', 'torque', 'fuel_pressure', 'throttle', 'idle'
    ]);
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
      },
      channel_registry: {
        ...calibrationHardware.channel_registry,
        inputs: calibrationHardware.channel_registry.inputs.map(channel =>
          hiddenCalibrationPurposes.has(channel.purpose) ? { ...channel, installed: false } : channel),
        outputs: calibrationHardware.channel_registry.outputs.map(channel =>
          ['glow_plug', 'igniter', 'ab_igniter', 'oil_pump'].includes(channel.purpose)
            ? { ...channel, has_current: false } : channel)
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
      },
      channel_registry: {
        ...calibrationHardware.channel_registry,
        inputs: calibrationHardware.channel_registry.inputs.map(channel =>
          ['fuel_flow', 'throttle', 'idle'].includes(channel.purpose)
            ? { ...channel, installed: true }
            : hiddenCalibrationPurposes.has(channel.purpose) ? { ...channel, installed: false } : channel)
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
    const logHardware = await (await page.request.get(`${base}/api/hardware`)).json();
    await patchHardware(page, {
      has_afterburner: false,
      has_two_shaft: false,
      sensors: { n2_rpm: { enabled: false } },
      actuators: { prop_pitch: { enabled: false }, ab_sol: { enabled: false }, ab_pump: { enabled: false } },
      ab_flame: { enabled: false },
      ab_trigger: { switch_pin: -1, input_pin: -1 },
      channel_registry: {
        ...logHardware.channel_registry,
        inputs: logHardware.channel_registry.inputs.filter(channel => !['n2_speed', 'ab_flame'].includes(channel.purpose)),
        outputs: []
      }
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
    assert.equal(await page.locator('#card-AB_SOL_TEST').count(), 1);
    assert.equal(await page.locator('#card-AB_PUMP_TEST').count(), 1);
    assert.equal(await page.locator('#card-TOGGLE_DYNAMIC_IDLE').count(), 0);
    assert.equal(await page.locator('#card-TOGGLE_LIMP_MODE').count(), 0);
    results.push('tools page follows fitted actuator prerequisites and ignores obsolete master fields');

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

const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const { chromium } = require('playwright');

const port = 9300 + Math.floor(Math.random() * 500);
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

async function patchData(page, patch) {
  const response = await page.request.post(`${base}/__sim/data`, { data: patch });
  assert.equal(response.ok(), true);
}

async function goto(page, route, waitSelector) {
  await page.goto(`${base}/${route}`);
  await page.waitForSelector(waitSelector, { state: 'attached' });
}

async function shown(page, selector) {
  return page.evaluate(sel => {
    const el = document.querySelector(sel);
    return !!el && getComputedStyle(el).display !== 'none' && el.getClientRects().length > 0;
  }, selector);
}

async function disabled(page, selector) {
  return page.evaluate(sel => document.querySelector(sel)?.disabled ?? null, selector);
}

async function checked(page, selector) {
  return page.evaluate(sel => document.querySelector(sel)?.checked ?? null, selector);
}

async function value(page, selector) {
  return page.locator(selector).inputValue();
}

async function visibleIds(page, ids) {
  return page.evaluate(ids => Object.fromEntries(ids.map(id => {
    const el = document.getElementById(id);
    return [id, !!el && getComputedStyle(el).display !== 'none'];
  })), ids);
}

async function optionDisabled(page, selector, value) {
  return page.evaluate(({ selector, value }) => {
    const opt = document.querySelector(`${selector} option[value="${value}"]`);
    return opt ? opt.disabled : null;
  }, { selector, value });
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

    await goto(page, 'index.html', '#n1-card');
    let cards = await visibleIds(page, [
      'n1-card', 'n2-card', 'tot-card', 'tit-card', 'oil-card', 'flame-card', 'p1-card', 'p2-card',
      'oil-temp-card', 'batt-card', 'torque-card', 'glow-current-card', 'igniter-current-card',
      'igniter2-current-card', 'oilpump-current-card', 'fuel-flow-card', 'fuel-press-card',
      'governor-section', 'adv-act-section', 'ab-section', 'relight-card'
    ]);
    for (const [id, isShown] of Object.entries(cards)) assert.equal(isShown, true, `${id} should show with full hardware`);
    results.push('dashboard shows every fitted full-hardware card and advanced section');

    await patchData(page, { has_torque: true, has_n2: false, turbo_power_w: 12345 });
    await page.waitForTimeout(150);
    assert.equal(await shown(page, '#torque-card'), true, 'torque card should still show when torque is fitted without N2');
    assert.equal((await page.locator('#turbo-power').textContent()).trim(), 'N2 required');
    results.push('dashboard shaft power display requires a fitted N2 source');

    await patchData(page, {
      has_n2: false, has_tit: false, has_oil_press: false, has_flame: false, has_p1: false, has_p2: false,
      has_oil_temp: false, has_batt_voltage: false, has_torque: false, has_glow_current: false,
      has_igniter_current: false, has_igniter2_current: false, has_oil_pump_current: false,
      has_fuel_flow: false, has_fuel_press: false, has_governor: false,
      has_glow_plug: false, has_bleed_valve: false, has_prop_pitch: false, has_fuel_pump2: false,
      has_cool_fan: false, has_airstarter: false, has_oil_scavenge: false, has_afterburner: false,
      relight_enabled: false
    });
    await page.waitForTimeout(150);
    cards = await visibleIds(page, [
      'n2-card', 'tit-card', 'oil-card', 'flame-card', 'p1-card', 'p2-card', 'oil-temp-card',
      'batt-card', 'torque-card', 'glow-current-card', 'igniter-current-card', 'igniter2-current-card',
      'oilpump-current-card', 'fuel-flow-card', 'fuel-press-card', 'governor-section',
      'adv-act-section', 'ab-section', 'relight-card'
    ]);
    for (const [id, isShown] of Object.entries(cards)) assert.equal(isShown, false, `${id} should hide when hardware is absent`);
    assert.equal(await shown(page, '#temperature-group'), true);
    assert.equal(await shown(page, '#combustion-group'), false);
    assert.equal(await shown(page, '#ext-sensors-section'), false);
    results.push('dashboard hides every absent optional card and empty section without losing core N1/TOT layout');

    await reset(page);
    await goto(page, 'hardware.html', '#f-profile-id');
    const hwPrereq = await page.evaluate(() => {
      function st(id) {
        const el = document.getElementById(id);
        return { disabled: !!el?.disabled, checked: !!el?.checked };
      }
      cfg.safety.overspeed = cfg.safety.surge = cfg.safety.overtemp = cfg.safety.hot_start = true;
      cfg.safety.low_oil = cfg.safety.oil_zero = cfg.safety.flameout = true;
      cfg.safety.tit_overtemp = cfg.safety.oil_temp_high = cfg.safety.fuel_press_low = cfg.safety.batt_low = true;
      for (const key of ['n1_rpm', 'tot', 'oil_press', 'flame', 'tit', 'oil_temp', 'fuel_press', 'batt_voltage']) {
        cfg.sensors[key].enabled = false;
      }
      updateSafetyPrerequisites(true);
      cfg.controllers.oil_loop = cfg.controllers.throttle_slew = cfg.controllers.dynamic_idle = cfg.controllers.governor = true;
      cfg.actuators.oil_pump.enabled = false;
      cfg.actuators.throttle.enabled = false;
      cfg.has_two_shaft = false;
      cfg.sensors.n2_rpm.enabled = true;
      cfg.actuators.prop_pitch.enabled = false;
      updateFeaturesUI();
      updateHardwarePrerequisites(true);
      return {
        safety: {
          overspeed: st('f-saf-overspeed'), surge: st('f-saf-surge'), overtemp: st('f-saf-overtemp'),
          hotStart: st('f-saf-hotstart'), lowOil: st('f-saf-lowoil'), oilZero: st('f-saf-oilzero'),
          flameout: st('f-saf-flameout'), tit: st('f-saf-titovertemp'), oilTemp: st('f-saf-oiltemphi'),
          fuelPress: st('f-saf-fuelpresslo'), batt: st('f-saf-battlo')
        },
        controllers: { oil: st('f-ctrl-oil'), slew: st('f-ctrl-slew'), idle: st('f-ctrl-idle'), gov: st('f-ctrl-gov') },
        currentGroups: {
          oil: st('en-oilpumpcurrent'),
          glow: st('en-glowcurrent')
        },
        n2Visible: getComputedStyle(document.getElementById('section-n2rpm')).display !== 'none'
      };
    });
    for (const state of Object.values(hwPrereq.safety)) assert.deepEqual(state, { disabled: true, checked: false });
    for (const state of Object.values(hwPrereq.controllers)) assert.deepEqual(state, { disabled: true, checked: false });
    assert.deepEqual(hwPrereq.currentGroups.oil, { disabled: true, checked: false });
    assert.deepEqual(hwPrereq.currentGroups.glow, { disabled: false, checked: true });
    assert.equal(hwPrereq.n2Visible, false);
    results.push('hardware editor removes unsafe safety/controller dependencies when prerequisite sensors or actuators disappear');

    const typeMatrix = await page.evaluate(() => {
      function visible(id) {
        const el = document.getElementById(id);
        return !!el && getComputedStyle(el).display !== 'none';
      }
      const out = {};
      for (const [name, fn, groups] of [
        ['thr', setThrType, ['grp-thr-servo', 'grp-thr-ledc', 'grp-thr-onoff']],
        ['str', setStrType, ['grp-str-servo', 'grp-str-ledc', 'grp-str-onoff']],
        ['op', setOpType, ['grp-op-servo', 'grp-op-ledc', 'grp-op-onoff']],
        ['oscav', setOscavType, ['grp-oscav-servo', 'grp-oscav-ledc', 'grp-oscav-onoff']],
        ['abp', setAbpType, ['grp-abp-servo', 'grp-abp-ledc', 'grp-abp-onoff']],
        ['fan', setFanType, ['grp-fan-servo', 'grp-fan-ledc', 'grp-fan-onoff']],
        ['fp2', setFp2Type, ['grp-fp2-servo', 'grp-fp2-ledc', 'grp-fp2-onoff']],
        ['bleed', setBleedType, ['grp-bleed-onoff', 'grp-bleed-servo', 'grp-bleed-pwm']],
        ['prop', setPropPitchType, ['grp-pp-servo', 'grp-pp-pwm', 'grp-pp-onoff']]
      ]) {
        out[name] = [];
        for (const type of [0, 1, 2]) {
          fn(type);
          out[name].push(groups.map(visible));
        }
      }
      setTotChip('max31855'); out.tot31855 = [visible('grp-tot-mosi'), visible('grp-tot-tctype')];
      setTotChip('max31856'); out.tot31856 = [visible('grp-tot-mosi'), visible('grp-tot-tctype')];
      setTitChip('max31855'); out.tit31855 = [visible('grp-tit-mosi'), visible('grp-tit-tctype')];
      setTitChip('max31856'); out.tit31856 = [visible('grp-tit-mosi'), visible('grp-tit-tctype')];
      for (const chip of ['ntc', 'ds18b20', 'max31855', 'max31856']) {
        setOilTempChip(chip);
        out[`oiltemp_${chip}`] = ['grp-oiltemp-ntc', 'grp-oiltemp-onewire', 'grp-oiltemp-spi', 'grp-oiltemp-mosi']
          .map(id => !!document.getElementById(id) && visible(id));
      }
      setTorqueType(0); out.torqueAdc = [visible('torque-adc-pin'), visible('torque-hx-dt')];
      setTorqueType(1); out.torqueHx = [visible('torque-adc-pin'), visible('torque-hx-dt')];
      cfg.ab_trigger.source = 0; updateAbTrigUI(0); out.abThrottle = [visible('grp-ab-sw'), visible('grp-ab-inp'), visible('grp-ab-arm')];
      cfg.ab_trigger.source = 2; updateAbTrigUI(2); out.abSwitch = [visible('grp-ab-sw'), visible('grp-ab-inp'), visible('grp-ab-arm')];
      cfg.ab_trigger.source = 3; updateAbTrigUI(3); out.abAnalog = [visible('grp-ab-sw'), visible('grp-ab-inp'), visible('grp-ab-arm')];
      return out;
    });
      for (const [key, states] of Object.entries(typeMatrix)) {
      if (['tot31855', 'tit31855'].includes(key)) assert.deepEqual(states, [false, false], key);
      else if (['tot31856', 'tit31856'].includes(key)) assert.deepEqual(states, [true, true], key);
      else if (key === 'oiltemp_ntc') assert.deepEqual(states, [true, false, false, false], key);
      else if (key === 'oiltemp_ds18b20') assert.deepEqual(states, [false, true, false, false], key);
      else if (key === 'oiltemp_max31855') assert.deepEqual(states, [false, false, true, false], key);
      else if (key === 'oiltemp_max31856') assert.deepEqual(states, [false, false, true, true], key);
      else if (key === 'torqueAdc') assert.deepEqual(states, [true, false], key);
      else if (key === 'torqueHx') assert.deepEqual(states, [false, true], key);
      else if (key === 'abThrottle') assert.deepEqual(states, [false, true, false], key);
      else if (key === 'abSwitch') assert.deepEqual(states, [true, true, true], key);
      else if (key === 'abAnalog') assert.deepEqual(states, [false, true, true], key);
      else if (key === 'bleed') {
        assert.deepEqual(states[0], [true, false, false], `${key} onoff`);
        assert.deepEqual(states[1], [false, true, false], `${key} servo`);
        assert.deepEqual(states[2], [false, false, true], `${key} pwm`);
      }
      else {
        assert.equal(states[0][0], true, `${key} servo group`);
        assert.equal(states[0][2], false, `${key} onoff hidden in servo mode`);
        assert.equal(states[1][0], false, `${key} servo hidden in pwm mode`);
        if (states[1].length > 2 && states[1][1] !== false) assert.equal(states[1][1], true, `${key} pwm group`);
        assert.equal(states[1][2], false, `${key} onoff hidden in pwm mode`);
        assert.equal(states[2][0], false, `${key} servo hidden in onoff mode`);
        assert.equal(states[2][2], true, `${key} onoff group`);
      }
    }
    results.push('hardware editor type selectors show exactly the servo/PWM/relay/SPI/analog subfields that apply');

    await patchHardware(page, { platform: 'esp32s3', cluster_serial: { enabled: true, tx_pin: 1, rx_pin: -1 } });
    await goto(page, 'hardware.html', '#f-cl-rx');
    assert.equal(await page.locator('#f-thr-pin option[value="22"]').count(), 0);
    assert.equal(await page.locator('#f-oilpress-pin option[value="1"]').count(), 1);
    assert.equal(await page.locator('#f-cl-rx option[value="-1"]').count(), 1);
    assert.equal(await page.locator('#f-cl-rx option[value="22"]').count(), 0);
    assert.equal(await page.locator('#f-cl-protocol').inputValue(), '1');
    results.push('hardware GPIO choices switch by ESP32 target and cluster RX keeps the telemetry-only -1 option');

    const n1UnlockTip = await page.locator('#en-n1rpm').getAttribute('title');
    assert.match(n1UnlockTip || '', /dynamic idle/i);
    assert.match(n1UnlockTip || '', /standby oil/i);
    assert.match(n1UnlockTip || '', /overspeed/i);
    const missingEnableTips = await page.evaluate(() =>
      Array.from(document.querySelectorAll('input[type="checkbox"][id^="en-"]'))
        .map(el => ({ id: el.id, title: el.getAttribute('title') || '' }))
        .filter(item => !/unlock|provides|enabled/i.test(item.title))
    );
    assert.deepEqual(missingEnableTips, []);
    assert.match((await page.locator('#en-cluster').getAttribute('title')) || '', /external display/i);
    assert.match((await page.locator('#en-glowcurrent').getAttribute('title')) || '', /wait-until-hot/i);
    results.push('hardware enable checkboxes explain what currently disabled hardware unlocks');

    await reset(page);
    await patchHardware(page, {
      sensors: { oil_press: { enabled: false }, tot: { enabled: false }, flame: { enabled: false }, n1_rpm: { enabled: false }, tit: { enabled: false }, oil_temp: { enabled: false }, fuel_press: { enabled: false }, batt_voltage: { enabled: false } },
      actuators: { oil_pump: { enabled: false }, throttle: { enabled: false }, starter: { enabled: false }, glow_plug: { enabled: false }, ab_pump: { enabled: false }, igniter2: { enabled: false }, oil_scavenge_pump: { enabled: false } },
      controllers: { governor: false },
      has_afterburner: true,
      has_two_shaft: false,
      cluster_serial: { enabled: false },
      ab_flame: { enabled: false },
      ab_trigger: { input_pin: -1 }
    });
    await goto(page, 'config.html', '#cf-oil_sp');
    const configGhosts = {
      oilSp: await shown(page, '#cf-oil_sp'),
      oilMm: await shown(page, '#cf-oil_mm'),
      oilSpDisabled: await disabled(page, '#cf-oil_sp'),
      oilMmDisabled: await disabled(page, '#cf-oil_mm'),
      standby: await page.evaluate(() => Array.from(document.querySelectorAll('.cfg-section')).find(sec => sec.querySelector('.cfg-title')?.textContent.trim() === 'Standby Oil Feed')?.style.display !== 'none'),
      flameSection: await shown(page, '#flame-detect-section'),
      dynamicIdleDisabled: await disabled(page, '#cf-di_tr'),
      clusterEnabledDisabled: await disabled(page, '#cf-cl_en'),
      relightDisabled: await disabled(page, '#cf-rl_en'),
      abIgnDisabled: await disabled(page, '#cf-ab_ui'),
      abTorchDisabled: await disabled(page, '#cf-ab_ut'),
      abTotDisabled: await disabled(page, '#cf-ab_mt'),
      abPumpModeDisabled: await disabled(page, '#cf-ab_pcm'),
      abFlameOpt: await optionDisabled(page, '#cf-ab_fm', '0'),
      abTotOpt: await optionDisabled(page, '#cf-ab_fm', '1'),
      abInputOpt: await optionDisabled(page, '#cf-ab_pcm', '2')
    };
    assert.deepEqual(configGhosts, {
      oilSp: true, oilMm: true, oilSpDisabled: true, oilMmDisabled: true,
      standby: true, flameSection: false, dynamicIdleDisabled: true,
      clusterEnabledDisabled: true, relightDisabled: true, abIgnDisabled: true,
      abTorchDisabled: true, abTotDisabled: true, abPumpModeDisabled: true,
      abFlameOpt: true, abTotOpt: true, abInputOpt: true
    });
    results.push('config editor hides or ghosts oil, flame, relight, cluster, and afterburner settings when hardware is absent');

    await reset(page);
    await patchHardware(page, {
      sensors: { oil_press: { enabled: false }, tot: { enabled: false }, tit: { enabled: false }, n1_rpm: { enabled: true } },
      cluster_serial: { enabled: true }
    });
    await goto(page, 'config.html', '#cf-cl_en');
    await page.locator('#btn-view-expert').click();
    await page.locator('#cf-cl_en').check();
    assert.equal(await shown(page, '#cf-cl_tw'), true, 'cluster EGT warning should stay visible when cluster is fitted');
    assert.equal(await disabled(page, '#cf-cl_tw'), true, 'cluster EGT warning must ghost without TOT/TIT');
    assert.equal(await shown(page, '#cf-cl_ow'), true, 'cluster oil warning should stay visible when cluster is fitted');
    assert.equal(await disabled(page, '#cf-cl_ow'), true, 'cluster oil warning must ghost without oil pressure sensor');
    results.push('config cluster warning thresholds are visible but locked when their sensor sources are not fitted');

    await reset(page);
    await patchHardware(page, {
      has_afterburner: true,
      has_two_shaft: false,
      sensors: {
        throttle_input: { enabled: false },
        idle_input: { enabled: false },
        n1_rpm: { enabled: true },
        n2_rpm: { enabled: false },
        tot: { enabled: true },
        tit: { enabled: false },
        flame: { enabled: false }
      },
      actuators: {
        igniter2: { enabled: true },
        ab_pump: { enabled: true },
        throttle: { enabled: true }
      },
      cluster_serial: { enabled: true },
      ab_flame: { enabled: false }
    });
    await goto(page, 'config.html', '#cf-sf_fs');
    await page.locator('#btn-view-expert').click();
    assert.equal(await shown(page, '#rc-pwm-section'), false, 'RC PWM section must stay hidden in Advanced view without servo inputs');
    assert.equal(await shown(page, '#cf-di_n2'), false, 'N2 dynamic-idle field must stay hidden in Advanced view on single-shaft hardware');
    assert.equal(await shown(page, '#cf-cl_n2'), false, 'N2 cluster warning field must stay hidden in Advanced view on single-shaft hardware');
    await page.locator('#cf-sf_fs').selectOption('2');
    assert.equal(await shown(page, '#cf-sf_fn'), true, 'N1 flameout threshold should show for N1 flameout source');
    assert.equal(await shown(page, '#cf-sf_ft'), false, 'EGT drop field should hide for N1 flameout source');
    await page.locator('#cf-sf_fs').selectOption('3');
    assert.equal(await shown(page, '#cf-sf_fn'), false, 'N1 flameout threshold should hide for EGT flameout source');
    assert.equal(await shown(page, '#cf-sf_ft'), true, 'EGT drop field should show for EGT flameout source');
    await page.locator('#cf-ab_ut').uncheck();
    await page.locator('#cf-ab_fm').selectOption('2');
    for (const selector of ['#cf-ab_tpct', '#cf-ab_tms', '#cf-ab_ttl', '#cf-ab_tr', '#cf-ab_tw']) {
      assert.equal(await shown(page, selector), false, `${selector} should stay hidden when torch is off and AB confirmation is timed`);
    }
    assert.equal(await shown(page, '#cf-ab_ams'), true, 'timed AB confirmation delay should show in timed mode');
    await page.locator('#cf-ab_fm').selectOption('1');
    assert.equal(await shown(page, '#cf-ab_tr'), true, 'EGT rise threshold should show in EGT AB confirmation mode');
    assert.equal(await shown(page, '#cf-ab_tw'), true, 'EGT rise window should show in EGT AB confirmation mode');
    assert.equal(await shown(page, '#cf-ab_ams'), false, 'timed AB confirmation delay should hide in EGT AB confirmation mode');
    results.push('config field-level conditional visibility survives Advanced view for RC, N2, flameout, and AB method controls');

    await reset(page);
    await patchHardware(page, {
      has_two_shaft: true,
      sensors: { n1_rpm: { enabled: false }, n2_rpm: { enabled: true } },
      actuators: { oil_pump: { enabled: true } }
    });
    await goto(page, 'config.html', '#cf-so_src');
    assert.equal(await disabled(page, '#cf-so_src'), false);
    assert.equal(await optionDisabled(page, '#cf-so_src', '0'), true);
    assert.equal(await optionDisabled(page, '#cf-so_src', '1'), false);
    assert.equal(await optionDisabled(page, '#cf-so_src', '2'), false);
    await patchHardware(page, {
      has_two_shaft: false,
      sensors: { n1_rpm: { enabled: false }, n2_rpm: { enabled: false } },
      actuators: { oil_pump: { enabled: true } }
    });
    await goto(page, 'config.html', '#cf-so_src');
    assert.equal(await disabled(page, '#cf-so_src'), true);
    assert.equal(await disabled(page, '#cf-so_rl'), true);
    assert.equal(await disabled(page, '#cf-so_fp'), true);
    results.push('standby oil feed requires oil pump plus a fitted N1 or N2 source and ghosts invalid shaft options');

    await reset(page);
    await patchHardware(page, {
      has_afterburner: true,
      has_two_shaft: false,
      sensors: {
        tot: { enabled: false },
        tit: { enabled: true },
        flame: { enabled: false },
        n1_rpm: { enabled: true }
      },
      actuators: {
        throttle: { enabled: false },
        ab_pump: { enabled: false },
        igniter2: { enabled: false }
      },
      ab_flame: { enabled: false },
      ab_trigger: { input_pin: -1 }
    });
    await goto(page, 'config.html', '#cf-eg_src');
    assert.equal(await optionDisabled(page, '#cf-eg_src', '1'), true);
    assert.equal(await optionDisabled(page, '#cf-eg_src', '2'), false);
    assert.equal(await disabled(page, '#cf-tot_limit'), true);
    assert.equal(await disabled(page, '#cf-sf_tit'), false);
    assert.equal(await disabled(page, '#cf-th_mn'), true);
    assert.equal(await disabled(page, '#cf-ab_ui'), true);
    assert.equal(await disabled(page, '#cf-ab_ut'), true);
    assert.equal(await disabled(page, '#cf-ab_pcm'), true);
    assert.equal(await optionDisabled(page, '#cf-ab_fm', '0'), true);
    assert.equal(await optionDisabled(page, '#cf-ab_fm', '1'), false);

    await patchHardware(page, {
      sensors: {
        tot: { enabled: false },
        tit: { enabled: false },
        flame: { enabled: true },
        n1_rpm: { enabled: true }
      }
    });
    await goto(page, 'config.html', '#cf-eg_src');
    assert.equal(await disabled(page, '#cf-eg_src'), true);
    assert.equal(await disabled(page, '#cf-sf_hs'), true);
    assert.equal(await disabled(page, '#cf-sf_fo'), false);
    assert.equal(await optionDisabled(page, '#cf-sf_fs', '1'), false);
    assert.equal(await optionDisabled(page, '#cf-sf_fs', '2'), false);
    assert.equal(await optionDisabled(page, '#cf-sf_fs', '3'), true);

    await patchHardware(page, { has_afterburner: false });
    await goto(page, 'config.html', '#cf-eg_src');
    for (const selector of ['#ab-cfg-section', '#ab-ign-section', '#ab-flame-section', '#ab-run-section']) {
      assert.equal(await shown(page, selector), false, `${selector} should hide when afterburner is not fitted`);
    }
    results.push('config page ghosts or hides dependent fields across TIT-only, no-EGT, no-throttle, and afterburner-off combinations');

    await reset(page);
    await goto(page, 'config.html', '#cf-tot_limit');
    const firstTotLimit = await value(page, '#cf-tot_limit');
    assert.ok(['720', '1328'].includes(firstTotLimit), `unexpected displayed TOT limit ${firstTotLimit}`);
    await page.locator('#unit-temp-btn').click();
    const secondTotLimit = await value(page, '#cf-tot_limit');
    assert.ok(['720', '1328'].includes(secondTotLimit), `unexpected toggled TOT limit ${secondTotLimit}`);
    assert.notEqual(secondTotLimit, firstTotLimit);
    await page.locator('#unit-temp-btn').click();
    assert.equal(await value(page, '#cf-tot_limit'), firstTotLimit);
    await page.locator('#unit-press-btn').click();
    const firstOilMin = await value(page, '#cf-oil_rm');
    assert.ok(['1.2', '17.405'].includes(firstOilMin), `unexpected displayed oil min ${firstOilMin}`);
    await page.locator('#unit-press-btn').click();
    const secondOilMin = await value(page, '#cf-oil_rm');
    assert.ok(['1.2', '17.405'].includes(secondOilMin), `unexpected toggled oil min ${secondOilMin}`);
    assert.notEqual(secondOilMin, firstOilMin);
    results.push('config temperature and pressure toggles preserve canonical meaning and displayed precision');

    await reset(page);
    await patchHardware(page, {
      sensors: {
        n1_rpm: { enabled: true },
        tot: { enabled: true },
        tit: { enabled: true },
        oil_press: { enabled: true }
      }
    });
    await patchData(page, {
      has_n1: true,
      has_tot: true,
      has_tit: true,
      has_oil_press: true,
      n1: 12000,
      tot: 500,
      tit: 505,
      oil: 1.2
    });
    await goto(page, 'config.html', '#cf-rpm_limit');
    await page.waitForSelector('#live-rpm_limit', { state: 'attached' });
    await page.evaluate(() => {
      window.applyData({
        n1: 45678,
        tot: 612.3,
        tit: 620.1,
        oil: 2.34
      });
    });
    assert.match(await page.locator('#live-rpm_limit').textContent(), /45,678|45678/);
    assert.match(await page.locator('#live-tot_limit').textContent(), /612\.3/);
    assert.match(await page.locator('#live-sf_tit').textContent(), /620\.1/);
    assert.match(await page.locator('#live-oil_rm').textContent(), /2\.34/);
    results.push('config live badges keep updating from fast frames without repeated has_* flags');

    await reset(page);
    await patchHardware(page, {
      sensors: {
        oil_press: { enabled: false },
        flame: { enabled: false },
        p1: { enabled: false },
        p2: { enabled: false },
        oil_temp: { enabled: false },
        batt_voltage: { enabled: false },
        torque: { enabled: false },
        fuel_press: { enabled: false },
        fuel_flow: { enabled: false },
        throttle_input: { enabled: false },
        idle_input: { enabled: false }
      },
      has_afterburner: false,
      actuators: {
        glow_plug: { enabled: false, has_current: false },
        igniter: { enabled: false, has_current: false },
        igniter2: { enabled: false, has_current: false },
        oil_pump: { enabled: false, has_current: false }
      },
      ab_flame: { enabled: false }
    });
    await patchData(page, {
      has_oil_press: false, has_flame: false, has_p1: false, has_p2: false, has_oil_temp: false,
      has_batt_voltage: false, has_torque: false, has_fuel_press: false, has_fuel_flow: false,
      has_glow_current: false, has_igniter_current: false, has_igniter2_current: false,
      has_oil_pump_current: false, has_ab_flame: false, throttle_input_type: 'none', idle_input_type: 'none'
    });
    await goto(page, 'calibration.html', '#oil-press-cal-row');
    await page.waitForFunction(() => document.querySelector('#throttle-cal-row')?.style.display === 'none');
    for (const selector of [
      '#oil-press-cal-row', '#flame-cal-row', '#p1-cal-row', '#p2-cal-row', '#oiltemp-cal-row',
      '#battvolt-cal-row', '#torque-cal-row', '#fuelpress-cal-row', '#fuelflow-cal-row',
      '#glow-current-cal-row', '#igniter-current-cal-row', '#igniter2-current-cal-row',
      '#oilpump-current-cal-row', '#throttle-cal-row', '#idle-cal-row', '#ab-flame-cal-row'
    ]) {
      assert.equal(await shown(page, selector), false, `${selector} should hide`);
    }
    await patchData(page, {
      has_fuel_flow: true, fuel_flow_type: 0, has_glow_current: true, has_igniter_current: true,
      has_igniter2_current: true, has_oil_pump_current: true, throttle_input_type: 'servo',
      throttle_input_us: 1500, idle_input_type: 'adc', idle_input_raw: 1234,
      has_ab_flame: true, ab_flame_raw: 760, ab_flame_threshold: 1234, ab_flame_on: false
    });
    await patchHardware(page, {
      has_afterburner: true,
      sensors: {
        oil_press: { enabled: true },
        flame: { enabled: true },
        fuel_flow: { enabled: true, type: 0 },
        p1: { enabled: true },
        p2: { enabled: true },
        fuel_press: { enabled: true },
        oil_temp: { enabled: true },
        batt_voltage: { enabled: true },
        torque: { enabled: true },
        throttle_input: { enabled: true, rc_pwm: true },
        idle_input: { enabled: true, rc_pwm: false }
      },
      actuators: {
        glow_plug: { enabled: true, has_current: true },
        igniter: { enabled: true, has_current: true },
        igniter2: { enabled: true, has_current: true },
        oil_pump: { enabled: true, has_current: true }
      },
      ab_flame: { enabled: true, threshold: 1234 }
    });
    await page.reload();
    await page.waitForSelector('#oil-press-cal-row', { state: 'attached' });
    await page.waitForFunction(() => /us|µs|Âµs/.test(document.querySelector('#cal-th-raw')?.textContent || ''), null, { timeout: 5000 });
    assert.equal(await shown(page, '#fuelflow-cal-row'), true);
    assert.equal(await shown(page, '#glow-current-cal-row'), true);
    assert.equal(await shown(page, '#igniter-current-cal-row'), true);
    assert.equal(await shown(page, '#igniter2-current-cal-row'), true);
    assert.equal(await shown(page, '#oilpump-current-cal-row'), true);
    assert.equal(await shown(page, '#ab-flame-cal-row'), true);
    assert.match(await page.locator('#cal-th-raw').textContent(), /us|µs/);
    assert.match(await page.locator('#idle-raw-label').textContent(), /raw/i);
    await page.evaluate(() => {
      live = {};
      window.applyData({
        fuel_flow_type: 0,
        fuel_flow_raw: 1200,
        fuel_flow: 4.2,
        glow_current_raw: 1300,
        glow_current_amps: 6.1,
        igniter_current_raw: 1400,
        igniter_current_amps: 2.2,
        igniter2_current_raw: 1500,
        igniter2_current_amps: 2.4,
        oil_pump_current_raw: 1600,
        oil_pump_current_amps: 3.3,
        oil_pump_overcurrent: false,
        ab_flame_raw: 760,
        ab_flame_threshold: 1234,
        ab_flame_on: false,
        p1_raw: 810,
        p1: 1.23,
        p2_raw: 910,
        p2: 2.34,
        oil_raw: 1110,
        oil: 1.11,
        flame_raw: 1210,
        flame: true,
        fuel_press_raw: 1010,
        fuel_press: 3.45,
        oil_temp: 77.7,
        batt_voltage: 12.3,
        batt_voltage_raw: 2010,
        torque: 8.9,
        torque_raw: 2110
      });
    });
    for (const selector of [
      '#oil-press-cal-row', '#flame-cal-row', '#p1-cal-row', '#p2-cal-row',
      '#oiltemp-cal-row', '#battvolt-cal-row', '#torque-cal-row', '#fuelpress-cal-row',
      '#fuelflow-cal-row', '#glow-current-cal-row', '#igniter-current-cal-row',
      '#igniter2-current-cal-row', '#oilpump-current-cal-row', '#ab-flame-cal-row'
    ]) {
      assert.equal(await shown(page, selector), true, `${selector} should stay visible on fast frames without has_* flags`);
    }
    assert.match(await page.locator('#cal-ff').textContent(), /4\.200/);
    assert.match(await page.locator('#cal-p1').textContent(), /1\.230/);
    assert.match(await page.locator('#cal-fuelpress').textContent(), /3\.45/);
    results.push('calibration rows and raw-unit labels track fitted sensors and input source types');

    await reset(page);
    await patchHardware(page, {
      has_afterburner: true,
      sensors: {
        oil_press: { enabled: true },
        flame: { enabled: true },
        p1: { enabled: true },
        p2: { enabled: true },
        fuel_press: { enabled: true },
        fuel_flow: { enabled: true, type: 0 },
        oil_temp: { enabled: true, chip: 'ntc' },
        batt_voltage: { enabled: true },
        torque: { enabled: true, hx711: false },
        throttle_input: { enabled: true, rc_pwm: false },
        idle_input: { enabled: true, rc_pwm: true }
      },
      actuators: {
        glow_plug: { enabled: true, has_current: true },
        igniter: { enabled: true, has_current: true },
        igniter2: { enabled: true, has_current: true },
        oil_pump: { enabled: true, has_current: true }
      },
      ab_flame: { enabled: true, threshold: 1800 }
    });
    await goto(page, 'calibration.html', '#oil-press-cal-row');
    for (const selector of [
      '#oil-press-cal-row', '#flame-cal-row', '#p1-cal-row', '#p2-cal-row', '#fuelpress-cal-row',
      '#fuelflow-cal-row', '#oiltemp-cal-row', '#battvolt-cal-row', '#torque-cal-row',
      '#throttle-cal-row', '#idle-cal-row', '#glow-current-cal-row', '#igniter-current-cal-row',
      '#igniter2-current-cal-row', '#oilpump-current-cal-row', '#ab-flame-cal-row'
    ]) {
      assert.equal(await shown(page, selector), true, `${selector} should show when its hardware is fitted`);
    }
    assert.equal(await shown(page, '#oiltemp-ntc-wizard'), true);
    assert.equal(await shown(page, '#oiltemp-fixed-note'), false);
    assert.equal(await shown(page, '#torque-adc-wizard'), true);
    assert.equal(await shown(page, '#torque-hx711-wizard'), false);

    await patchHardware(page, {
      has_afterburner: false,
      sensors: {
        fuel_flow: { enabled: true, type: 1 },
        oil_temp: { enabled: true, chip: 'max31855' },
        torque: { enabled: true, hx711: true }
      },
      actuators: {
        glow_plug: { enabled: false, has_current: true },
        igniter: { enabled: false, has_current: true },
        igniter2: { enabled: false, has_current: true },
        oil_pump: { enabled: false, has_current: true }
      },
      ab_flame: { enabled: true }
    });
    await goto(page, 'calibration.html', '#oil-press-cal-row');
    assert.equal(await shown(page, '#fuelflow-cal-row'), false, 'pulse fuel flow is configured by pulses-per-litre, not analog calibration');
    assert.equal(await shown(page, '#oiltemp-cal-row'), true, 'non-NTC oil temp still shows the calibration/config note');
    assert.equal(await shown(page, '#oiltemp-ntc-wizard'), false);
    assert.equal(await shown(page, '#oiltemp-fixed-note'), true);
    assert.equal(await shown(page, '#torque-adc-wizard'), false);
    assert.equal(await shown(page, '#torque-hx711-wizard'), true);
    for (const selector of ['#glow-current-cal-row', '#igniter-current-cal-row', '#igniter2-current-cal-row', '#oilpump-current-cal-row', '#ab-flame-cal-row']) {
      assert.equal(await shown(page, selector), false, `${selector} should require its parent hardware feature`);
    }
    results.push('calibration page has correct show/hide semantics for every fitted analog, temperature, flow, AB flame, and current sensor');

    await reset(page);
    await goto(page, 'sequence.html', '#add-startup-sel');
    const sequenceFull = await page.evaluate(() => ({
      startup: Array.from(document.querySelectorAll('#add-startup-sel option:not([disabled])')).map(o => o.value).filter(Boolean),
      shutdown: Array.from(document.querySelectorAll('#add-shutdown-sel option:not([disabled])')).map(o => o.value).filter(Boolean),
      afterburner: Array.from(document.querySelectorAll('#add-afterburner-sel option:not([disabled])')).map(o => o.value).filter(Boolean),
      sensors: getEnabledSensors().map(s => s.key),
      actuators: getEnabledActuators().map(a => a.key)
    }));
    for (const key of ['FuelPulse', 'PreHeat', 'ThrottleSet', 'WaitForInput', 'OilScavengeOn', 'AirstarterOn', 'CoolFanOn', 'BleedOpen', 'GlowPreheat', 'FuelPumpRamp', 'FuelPump2Set', 'GovernorHold']) {
      assert.ok(sequenceFull.startup.includes(key), `full startup should include ${key}`);
    }
    for (const key of ['ABSolOpen', 'ABPumpOn', 'ABIgnOn', 'ABFlameConfirm', 'ABStabilize']) {
      assert.ok(sequenceFull.afterburner.includes(key), `full AB should include ${key}`);
    }
    await patchHardware(page, {
      has_afterburner: false,
      has_two_shaft: false,
      sensors: { n2_rpm: { enabled: true }, tit: { enabled: false } },
      actuators: {
        ab_sol: { enabled: true }, ab_pump: { enabled: true }, igniter2: { enabled: true },
        igniter: { enabled: false }, prop_pitch: { enabled: false }, oil_scavenge_pump: { enabled: false }, fuel_sol: { enabled: false },
        glow_plug: { enabled: false }, fuel_pump2: { enabled: false }, bleed_valve: { enabled: false },
        cool_fan: { enabled: false }, airstarter_sol: { enabled: false }
      },
      di_channels: [{ pin: -1 }, { pin: -1 }, { pin: -1 }, { pin: -1 }]
    });
    await page.reload();
    await page.waitForSelector('#add-startup-sel');
    const sequenceHidden = await page.evaluate(() => ({
      startup: Array.from(document.querySelectorAll('#add-startup-sel option')).map(o => ({ value: o.value, disabled: o.disabled })).filter(o => o.value),
      afterburnerCount: document.querySelectorAll('#add-afterburner-sel option[value="ABPumpOn"],#add-afterburner-sel option[value="ABSolOpen"],#add-afterburner-sel option[value="ABIgnOn"]').length,
      sensors: getEnabledSensors().map(s => s.key),
      actuators: getEnabledActuators().map(a => a.key),
      ruleN2: document.querySelector('#rule-sensor-0 option[value="6"]')?.disabled,
      ruleAbInput: document.querySelector('#rule-sensor-0 option[value="24"]')?.disabled,
      ruleAbSol: document.querySelector('#rule-act-0 option[value="11"]')?.disabled
    }));
    for (const absent of ['FuelPulse', 'PreHeat', 'WaitForInput', 'OilScavengeOn', 'AirstarterOn', 'CoolFanOn', 'BleedOpen', 'GlowPreheat', 'FuelPumpRamp', 'FuelPump2Set', 'GovernorHold']) {
      const opt = sequenceHidden.startup.find(o => o.value === absent);
      assert.ok(!opt || opt.disabled, `${absent} should be absent/disabled`);
    }
    assert.equal(sequenceHidden.afterburnerCount, 0);
    assert.equal(sequenceHidden.sensors.includes('n2_rpm'), false);
    assert.equal(sequenceHidden.actuators.some(a => a.startsWith('ab_')), false);
    assert.equal(sequenceHidden.ruleN2, true);
    assert.equal(sequenceHidden.ruleAbInput, true);
    assert.equal(sequenceHidden.ruleAbSol, true);
    results.push('sequence editor filters blocks, sensors, actuators, and rules by fitted hardware and master features');

    await reset(page);
    await patchHardware(page, {
      has_afterburner: false,
      has_two_shaft: false,
      sensors: { n2_rpm: { enabled: false }, tit: { enabled: false }, batt_voltage: { enabled: false }, fuel_flow: { enabled: false }, fuel_press: { enabled: false } },
      actuators: { prop_pitch: { enabled: false }, fuel_pump2: { enabled: false }, glow_plug: { enabled: false } }
    });
    await goto(page, 'log.html', '#tab-session');
    await page.locator('#tab-session').click();
    await page.waitForFunction(() => document.querySelector('input[data-bit="n2"]')?.disabled === true);
    for (const bit of ['n2', 'tit', 'batt', 'fuel_press', 'fuel_flow', 'glow', 'fp2', 'ab', 'prop']) {
      assert.equal(await disabled(page, `input[data-bit="${bit}"]`), true, `${bit} session log should be disabled`);
      assert.equal(await checked(page, `input[data-bit="${bit}"]`), false, `${bit} session log should be unchecked`);
    }
    results.push('log session channels are ghosted and unchecked for absent fitted hardware');

    await reset(page);
    await patchHardware(page, {
      has_afterburner: false,
      controllers: { dynamic_idle: false },
      sensors: { n1_rpm: { enabled: false }, n2_rpm: { enabled: true } },
      actuators: {
        fuel_sol: { enabled: false }, oil_pump: { enabled: false }, igniter: { enabled: false },
        igniter2: { enabled: true }, starter: { enabled: false }, starter_en: { enabled: false },
        oil_scavenge_pump: { enabled: false }, cool_fan: { enabled: false }, airstarter_sol: { enabled: false },
        bleed_valve: { enabled: false }, glow_plug: { enabled: false }, fuel_pump2: { enabled: false },
        ab_sol: { enabled: true }, ab_pump: { enabled: true }, prop_pitch: { enabled: false }, throttle: { enabled: false }
      }
    });
    await goto(page, 'tools.html', '#tool-area');
    for (const id of ['FUEL_PRIME', 'OIL_PRIME', 'IGN_TEST', 'START_TEST', 'FUEL_SOL_TEST', 'IDLE_TEST', 'STARTER_EN_TEST', 'OIL_SCAV_TEST', 'COOL_FAN_TEST', 'AIRSTARTER_TEST', 'BLEED_VALVE_TEST', 'GLOW_TEST', 'FUEL_PUMP2_TEST', 'AB_SOL_TEST', 'AB_PUMP_TEST', 'PROP_PITCH_TEST', 'TOGGLE_DYNAMIC_IDLE', 'TOGGLE_LIMP_MODE']) {
      assert.equal(await page.locator(`#card-${id}`).count(), 0, `${id} tool should hide`);
    }
    assert.equal(await page.locator('#card-IGN2_TEST').count(), 1);
    results.push('tools page hides every actuator test/toggle whose prerequisites are not fitted');

    console.log(`UI beta dependency audit passed (${results.length} groups):`);
    results.forEach(result => console.log(`- ${result}`));
  } finally {
    await browser.close();
  }
  process.exit(0);
})().catch(error => {
  console.error(error.stack || error);
  process.exit(1);
});

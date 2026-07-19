'use strict';

const fs = require('fs');
const path = require('path');

const root = path.resolve(__dirname, '..');
const read = rel => fs.readFileSync(path.join(root, rel), 'utf8');
const checks = [];

function expect(label, condition) {
  if (!condition) throw new Error(`Safety regression audit failed: ${label}`);
  checks.push(label);
}

const configCpp = read('src/system/Config.cpp');
const configHtml = read('data_src/config.html');
const hardware = read('src/Hardware.h');
const hwConfig = read('src/system/HardwareConfig.cpp');
const main = read('src/main.cpp');
const web = read('src/system/web/WebServer.cpp');
const configGate = read('src/system/ConfigApplyGate.h');
const pcnt = read('src/hal/sensors/PCNTRpmSensor.h');
const analog = read('src/hal/sensors/AnalogSensor.h');
const safety = read('src/engine/SafetyMonitor.h');
const sessionLogger = read('src/system/SessionLogger.cpp');
const governor = read('src/engine/controllers/PowerTurbineGovernor.h');
const feedback = read('src/system/FeedbackRequirements.h');
const ntc = read('src/hal/sensors/NTCSensor.h');
const sequenceHtml = read('data_src/sequence.html');
const hardwareHtml = read('data_src/hardware.html');
const calibrationHtml = read('data_src/calibration.html');
const toolsHtml = read('data_src/tools.html');
const capabilities = read('src/system/HardwareCapabilities.h');
const cooldown = read('src/engine/sequencer/blocks/CooldownSpin.h');
const finalStop = read('src/engine/sequencer/blocks/FinalStop.h');
const version = read('src/system/version.h');
const changelog = read('CHANGELOG.md');
const phase2Hil = read('dev/bench/campaign/phase2_safety_hil.py');

expect('repository PlatformIO launcher avoids the broken global shim',
  fs.existsSync(path.join(root, 'tools/pio.cmd')));
expect('PCNT failures are recoverable',
  !pcnt.includes('ESP_ERROR_CHECK') && pcnt.includes('feedback disabled without reboot'));
expect('analog filters use four samples', analog.includes('RollingAvg<4> _avg'));
expect('oil-pressure mapping is explicit',
  !hwConfig.includes('"oil_pressure_main", "pressure"'));
expect('legacy oil loop binds explicit oil-pressure purpose',
  hwConfig.includes('channelRegistry.inputs[i].purpose, "oil_pressure"') &&
  !hwConfig.includes('channelRegistry.inputs[i].role, "pressure"'));
expect('battery mapping is explicit',
  !hwConfig.includes('"battery_voltage", "voltage"'));
expect('OTA and START share the canonical output-demand scan',
  (web.match(/OutputActivity::anyPhysicalDemand/g) || []).length === 2);
expect('configuration writes and START share an atomic gate',
  web.includes('ConfigApplyGate::tryBeginWebWrite') &&
  main.includes('ConfigApplyGate::tryBeginStartTransition') &&
  main.includes('ConfigApplyGate::tryBeginCoreApply'));
expect('START readiness is consumer-aware on both command paths',
  feedback.includes('allRequiredStartFeedbackHealthy') &&
  main.includes('FeedbackRequirements::allRequiredStartFeedbackHealthy') &&
  web.includes('FeedbackRequirements::allRequiredStartFeedbackHealthy'));
expect('limp and governor feedback failure command coarse pitch',
  hardware.includes('ed.limpMode && hw.hasPropPitch') &&
  governor.includes('_pitchCurrent + maxStep'));
expect('fault shutdown commands coarse pitch',
  main.includes('ed.propPitchDemand = 1.0f') &&
  hardware.includes('if (HardwareConfig::hasPropPitch) ed.propPitchDemand = 1.0f'));
expect('surge detection consumes only fresh shaft samples',
  safety.includes('ed.n1SampleSeq != _lastSurgeN1SampleSeq'));
expect('generic overcurrent protection is timed',
  safety.includes('_registryOvercurrentSinceMs') && safety.includes('OUTPUT_OVERCURRENT'));
expect('all safety dwell confirmations reset across inactive and bypassed monitoring',
  (safety.match(/_resetDwellConfirmations\(\)/g) || []).length >= 4 &&
  safety.includes('memset(_registryOvercurrentSinceMs'));
expect('general safety scan is capped at 250 ms in firmware and UI',
  configCpp.includes('validInt(sf["check_interval_ms"], 10, 250)') &&
  configHtml.includes("path:['safety','check_interval_ms']") && configHtml.includes('max:250'));

for (const key of [
  'low_oil_confirm_ms', 'oil_zero_confirm_ms', 'oil_temp_confirm_ms',
  'fuel_press_confirm_ms', 'batt_low_confirm_ms'
]) {
  expect(`${key} round-trips through firmware and is editable`,
    (configCpp.match(new RegExp(key, 'g')) || []).length >= 3 && configHtml.includes(key));
}

expect('physical STOP remains mandatory while START is optional',
  hwConfig.includes('if (stopPin < 0') && hwConfig.includes('(startPin >= 0 && !gpioAllowed(startPin))'));
expect('NTC divider orientation reaches the resistance calculation',
  ntc.includes('_cal.fixedPullup') && hardware.includes('hw.ntcFixedPullup'));
expect('sequencer uses turbine startup terminology',
  sequenceHtml.includes("label:'Starter Spin to Light-Off Speed'") && !sequenceHtml.includes("label:'Crank Engine'"));
expect('hardware dependency warnings use the same turbine block names',
  hardwareHtml.includes("StarterSpin:'Starter Spin to Light-Off Speed'") && !hardwareHtml.includes("StarterSpin:'Crank Engine'"));
expect('zero minimum N1 remains a valid underspeed-disable setting',
  configCpp.includes('if (!isfinite(minRpm) || minRpm < 0.0f)') &&
  configHtml.includes('Set 0 to disable this independent underspeed check'));
expect('N1 feedback-loss limp is independent of the optional underspeed limit',
  safety.includes('if (HardwareConfig::hasN1Rpm && m == SysMode::RUNNING)') &&
  safety.includes('if (minRpm > 0.0f && ed.n1Healthy && ed.n1Rpm < minRpm)') &&
  safety.includes('LIMP: N1 feedback lost'));
expect('empty session-log selection does not write timestamp-only run files',
  sessionLogger.includes('if (Config::sessionLogMask == 0)') &&
  sessionLogger.includes('_startPending = false;'));
expect('session-log listing cannot perform an unbounded directory walk',
  web.includes('checked < 4096 && millis() - started < 500'));
expect('valid slow igniter PWM cycles have sufficient LEDC timer resolution',
  hardware.includes('g_actIgniterLedc.begin(hw.igniterPin, freq, 14)') &&
  hardware.includes('g_actIgniter2Ledc.begin(hw.igniter2Pin, freq, 14)') &&
  hwConfig.includes('intRange(actuators["igniter"], "dwell_ms", 1, 200)') &&
  hwConfig.includes('intRange(actuators["igniter2"], "rest_ms", 1, 200)'));
expect('reduced-power caps total main fuel after the afterburner offset',
  hardware.includes('ed.throttleDemand + ed.abFuelOffset') &&
  hardware.indexOf('if (ed.limpMode && ed.mode == SysMode::RUNNING)') >
    hardware.indexOf('ed.throttleDemand + ed.abFuelOffset'));
expect('startup feedback follows actual block consumers',
  feedback.includes('startupHas("FlameConfirm")') &&
  !feedback.includes('startupHas("StarterSpin") || startupHas("Spool") ||\n               startupHas("SafetyHold")'));
expect('every enabled oil loop makes its pressure feedback operationally required',
  feedback.includes('allOilLoopFeedbackHealthy') && safety.includes('allOilLoopFeedbackHealthy'));
expect('pilot fuel and registry starter channels join the immediate shutdown cut',
  hardware.includes('!strcmp(purpose, "pilot_fuel")') &&
  hardware.includes('registryStarterPurpose') && main.includes('cutRegistryHazardousDemands'));
expect('critical safety capability checks reject generic temperature and voltage roles',
  !capabilities.includes('hasInputRole("temperature")') && !capabilities.includes('hasInputRole("voltage")'));
expect('cooldown defaults agree at sixty seconds',
  cooldown.includes('timeoutMs          = 60000') &&
  sequenceHtml.includes("def:60000, configKey:'cooldown_timeout_ms'"));
expect('FinalStop waits for its timeout when N1 is missing or unhealthy',
  finalStop.includes('bool stopped = HardwareConfig::hasN1Rpm') &&
  finalStop.includes('&& ed.n1Healthy') &&
  finalStop.includes('No N1 sensor (waiting %lu ms spool-down delay)') &&
  !finalStop.includes(': true;'));
expect('Developer Mode live config writes use the same runtime lock as Config',
  (web.match(/enable Developer Mode before starting to allow live settings updates/g) || []).length === 2 &&
  (web.match(/if \(Config::isLocked\(\)\)/g) || []).length >= 4);
expect('active Developer Mode writes defer disruptive copies until STANDBY',
  configGate.includes('tryBeginDeferredCoreApply') &&
  main.includes('_configApplyDeferred = true') &&
  main.includes('configMode == SysMode::STANDBY || configMode == SysMode::FAULT') &&
  web.includes('\\"block_hardware_apply\\":\\"deferred_until_standby\\"'));
expect('bench-test timing is edited only from Tools',
  !configHtml.includes("id:'bench', title:'Bench Test Timing'") &&
  toolsHtml.includes('openTestSettings()'));
expect('thermistor calibration explains the configured divider orientation',
  calibrationHtml.includes('ntc-divider-note') && calibrationHtml.includes('ntc_pullup: registryOil.ntc_pullup'));
expect('reduced-power cap discloses automatic safety-feedback activation',
  configHtml.includes('automatically because feedback used by an enabled protection/controller becomes unhealthy') &&
  toolsHtml.includes('feedback required by an enabled safety protection or shaft controller is lost'));
expect('release changelog covers the source firmware version',
  changelog.includes(`## [${version.match(/OT_VERSION\s+"([^"]+)"/)[1]}]`));
expect('phase-two HIL records the live DUT firmware version',
  phase2Hil.includes('self.firmware_before = self.dut.data().get("fw_version"') &&
  !phase2Hil.includes('"firmware": "1.9.2"'));

console.log(`Safety regression audit passed (${checks.length} checks).`);

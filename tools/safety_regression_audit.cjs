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
const pcnt = read('src/hal/sensors/PCNTRpmSensor.h');
const analog = read('src/hal/sensors/AnalogSensor.h');
const safety = read('src/engine/SafetyMonitor.h');
const governor = read('src/engine/controllers/PowerTurbineGovernor.h');
const feedback = read('src/system/FeedbackRequirements.h');
const ntc = read('src/hal/sensors/NTCSensor.h');
const sequenceHtml = read('data_src/sequence.html');

expect('repository PlatformIO launcher avoids the broken global shim',
  fs.existsSync(path.join(root, 'tools/pio.cmd')));
expect('PCNT failures are recoverable',
  !pcnt.includes('ESP_ERROR_CHECK') && pcnt.includes('feedback disabled without reboot'));
expect('analog filters use four samples', analog.includes('RollingAvg<4> _avg'));
expect('oil-pressure mapping is explicit',
  !hwConfig.includes('"oil_pressure_main", "pressure"'));
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

console.log(`Safety regression audit passed (${checks.length} checks).`);

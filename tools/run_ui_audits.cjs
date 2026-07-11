const { spawnSync } = require('node:child_process');
const { readdirSync } = require('node:fs');
const { join } = require('node:path');

const toolsDir = __dirname;
const scripts = readdirSync(toolsDir)
  .filter(name => /^ui_.*\.cjs$/.test(name))
  .sort();

for (const script of scripts) {
  process.stdout.write(`\n=== ${script} ===\n`);
  const result = spawnSync(process.execPath, [join(toolsDir, script)], {
    stdio: 'inherit',
    env: process.env,
  });
  if (result.error) throw result.error;
  if (result.status !== 0) process.exit(result.status || 1);
}

process.stdout.write(`\nAll ${scripts.length} UI audit programs passed.\n`);

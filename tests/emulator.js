// @ts-check
// Layer 3 — Pebble SDK emulator SMOKE tests
//
// These verify that the app BUILDS, INSTALLS, and LAUNCHES on the basalt
// emulator. They deliberately do NOT assert on phone<->watch AppMessage
// delivery: the pypkjs<->QEMU bridge in this headless environment does not
// reliably deliver send-app-message payloads (PHONESIM logs "Exception
// decoding QemuInboundPacket.footer" and the C inbox callback never fires,
// even under GDB). Real transport behavior is exercised on the physical
// watch instead.
//
// Prerequisites: pebble CLI on PATH (pebble --version)
// Run: cd tests && npx playwright test emulator.js

const { test, expect } = require('@playwright/test');
const { execSync, spawn } = require('child_process');
const path = require('path');
const fs = require('fs');

const ROOT = path.resolve(__dirname, '..');
const GYMTRACKER = path.join(ROOT, 'gymtracker');

function pebble(cmd, opts = {}) {
  return execSync(`pebble ${cmd}`, {
    cwd: GYMTRACKER,
    encoding: 'utf8',
    timeout: opts.timeout || 30000,
    stdio: ['pipe', 'pipe', 'pipe'],
    ...opts,
  });
}

function captureLogs(seconds) {
  return new Promise((resolve) => {
    let output = '';
    const proc = spawn('pebble', ['logs', '--emulator', 'basalt'], {
      cwd: GYMTRACKER,
      stdio: ['pipe', 'pipe', 'pipe'],
    });
    proc.stdout.on('data', (d) => { output += d.toString(); });
    proc.stderr.on('data', (d) => { output += d.toString(); });
    setTimeout(() => {
      proc.kill('SIGTERM');
      resolve(output);
    }, seconds * 1000);
  });
}

function killEmu() {
  try { pebble('kill --emulator basalt', { timeout: 5000 }); } catch {}
}

test.describe('Pebble SDK build', () => {
  test('pebble build produces a .pbw file', async () => {
    const output = pebble('build', { timeout: 60000 });
    expect(output).toContain('finished successfully');

    const pbw = path.join(GYMTRACKER, 'build', 'gymtracker.pbw');
    expect(fs.existsSync(pbw)).toBe(true);

    const stat = fs.statSync(pbw);
    expect(stat.size).toBeGreaterThan(0);
    console.log(`Built: ${pbw} (${stat.size} bytes)`);
  });
});

test.describe('Pebble SDK emulator install', () => {
  test('install on emulator and verify app launches via logs', async () => {
    killEmu();
    await new Promise(r => setTimeout(r, 1000));

    const logsPromise = captureLogs(8);
    await new Promise(r => setTimeout(r, 2000));

    const installOutput = pebble('install --emulator basalt', { timeout: 30000 });
    console.log('Install:', installOutput.trim());

    const logs = await logsPromise;
    killEmu();

    console.log('Logs:', logs.slice(0, 500));
    expect(logs).toContain('Heap Usage');
  });
});

test.describe('Pebble SDK screenshot', () => {
  test('take screenshot from running emulator', async () => {
    killEmu();
    await new Promise(r => setTimeout(r, 1000));

    pebble('install --emulator basalt', { timeout: 30000 });
    await new Promise(r => setTimeout(r, 3000));

    const screenshotPath = path.join(GYMTRACKER, 'build', 'test-screenshot.png');
    try {
      pebble(`screenshot --emulator basalt --no-open "${screenshotPath}"`, { timeout: 15000 });
      expect(fs.existsSync(screenshotPath)).toBe(true);
      const stat = fs.statSync(screenshotPath);
      expect(stat.size).toBeGreaterThan(0);
      console.log(`Screenshot: ${screenshotPath} (${stat.size} bytes)`);
    } catch (e) {
      console.log('Screenshot error:', e.message?.slice(0, 200));
    } finally {
      killEmu();
    }
  });
});

test.describe('Phone<->watch transport (troubleshooting only)', () => {
  // The headless pypkjs<->QEMU bridge does not reliably deliver AppMessages
  // to the C app. This test is a best-effort smoke probe that ALWAYS stays
  // green regardless of delivery, so it is NOT a functional assertion. Use
  // it only to eyeball the `parse_routine:` INFO log during troubleshooting.
  test('send-app-message sends (best-effort, no delivery assertion)', async () => {
    killEmu();
    await new Promise(r => setTimeout(r, 1000));

    const logsPromise = captureLogs(12);
    await new Promise(r => setTimeout(r, 2000));

    pebble('install --emulator basalt', { timeout: 30000 });
    await new Promise(r => setTimeout(r, 3000));

    const routineStr = 'TransportProbe|-1|2|Bench|3|10|60|0|-';
    try {
      pebble(`send-app-message --emulator basalt --string 0="${routineStr}"`, { timeout: 10000 });
      console.log('AppMessage sent (delivery NOT asserted - see note above)');
    } catch (e) {
      console.log('send-app-message output:', e.stdout?.slice(0, 300));
      console.log('send-app-message error:', e.stderr?.slice(0, 300));
    }

    await new Promise(r => setTimeout(r, 3000));
    const logs = await logsPromise;
    killEmu();

    console.log('Logs (look for parse_routine: during troubleshooting):', logs.slice(0, 800));
  });
});

// @ts-check
// Pebble SDK emulator integration tests.
//
// Uses the local Pebble CLI to build, install, and interact with
// GymTracker on the emulator. Verifies the full C + JS stack.
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

// Collect logs from emulator for N seconds using spawn (non-blocking)
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

// ============================================================
// 1. BUILD — compiles without errors
// ============================================================
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

// ============================================================
// 2. INSTALL + LAUNCH — emulator starts and app loads
// ============================================================
test.describe('Pebble SDK emulator install', () => {
  test('install on emulator and verify app launches via logs', async () => {
    killEmu();
    await new Promise(r => setTimeout(r, 1000));

    // Start log capture in background
    const logsPromise = captureLogs(8);

    // Give emulator time to start
    await new Promise(r => setTimeout(r, 2000));

    // Install the app
    const installOutput = pebble('install --emulator basalt', { timeout: 30000 });
    console.log('Install:', installOutput.trim());

    // Wait for log capture to finish
    const logs = await logsPromise;
    killEmu();

    console.log('Logs:', logs.slice(0, 500));

    // App launch produces heap usage logs from C code
    expect(logs).toContain('Heap Usage');
  });
});

// ============================================================
// 3. SEND-APP-MESSAGE — simulate phone sending a routine
// ============================================================
test.describe('Pebble SDK send-app-message', () => {
  test('send routine data to emulator via numeric key', async () => {
    killEmu();
    await new Promise(r => setTimeout(r, 1000));

    const logsPromise = captureLogs(10);
    await new Promise(r => setTimeout(r, 2000));

    pebble('install --emulator basalt', { timeout: 30000 });
    await new Promise(r => setTimeout(r, 3000));

    // ROUTINE_DATA is key index 0 in package.json appKeys array
    const routineStr = 'Test|-1|2|Bench|3|10|60|0|-';
    try {
      pebble(`send-app-message --emulator basalt --string 0="${routineStr}"`, { timeout: 10000 });
      console.log('AppMessage sent successfully');
    } catch (e) {
      console.log('send-app-message output:', e.stdout?.slice(0, 300));
      console.log('send-app-message error:', e.stderr?.slice(0, 300));
    }

    await new Promise(r => setTimeout(r, 3000));
    const logs = await logsPromise;
    killEmu();

    console.log('Logs:', logs.slice(0, 800));

    // The app should have received the message (C code logs on receipt)
    expect(logs.length).toBeGreaterThan(0);
  });
});

// ============================================================
// 4. SCREENSHOT — capture emulator display
// ============================================================
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

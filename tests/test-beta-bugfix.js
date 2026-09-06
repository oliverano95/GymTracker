// @ts-check
// Beta-troubleshooting regression tests.
//
// Reproduces and guards against the two bugs found while beta-testing the
// chunked-transfer feature on a real watch:
//
//   Bug 1 (data lost): saveAndSendSingle issued a SECOND pebblejs://close#
//   navigation (JSON with google url/pwd) after the raw-routine navigation.
//   The last response wins, so the routine never reached the watch.
//
//   Bug 2 (garbling): the watch's C-side parse_routine_string did not skip
//   the optional "Name|prog|inc|..." header, so prog was parsed as the first
//   exercise's name (sets/reps/weight shifted by one field).
//
// Run: cd tests && npx playwright test test-beta-bugfix.js

const { test, expect } = require('@playwright/test');
const path = require('path');
const http = require('http');
const fs = require('fs');

const ROOT = path.resolve(__dirname, '..');

function createServer() {
  return new Promise((resolve) => {
    const server = http.createServer((req, res) => {
      const url = new URL(req.url, 'http://localhost');
      const filePath = path.join(ROOT, url.pathname === '/' ? '/index_dev.html' : url.pathname);
      if (!fs.existsSync(filePath)) { res.writeHead(404); res.end('Not found'); return; }
      const ext = path.extname(filePath);
      const mime = { '.html': 'text/html', '.js': 'application/javascript', '.css': 'text/css' }[ext] || 'text/plain';
      res.writeHead(200, { 'Content-Type': mime });
      res.end(fs.readFileSync(filePath));
    });
    server.listen(0, () => resolve({ server, port: server.address().port }));
  });
}

let serverInfo;
let BASE_URL;

test.beforeAll(async () => {
  serverInfo = await createServer();
  BASE_URL = `http://127.0.0.1:${serverInfo.port}`;
});
test.afterAll(async () => { serverInfo?.server.close(); });

async function autoConfirm(page) {
  page.on('dialog', (dialog) => dialog.accept());
}

async function waitForInit(page) {
  await page.waitForFunction(() => {
    const sel = document.getElementById('savedRoutineSelect');
    return sel && sel.options.length > 1;
  }, { timeout: 5000 });
}

async function openWithSeed(page, { syncDict = {}, savedRoutines = [] } = {}) {
  const syncParam = encodeURIComponent(JSON.stringify(syncDict));
  const url = `${BASE_URL}/index_dev.html?sync=${syncParam}`;
  await page.goto(url);
  await waitForInit(page);

  await page.evaluate(({ savedRoutines }) => {
    localStorage.setItem('savedRoutines', JSON.stringify(savedRoutines));
    localStorage.setItem('deletedSyncKeys', JSON.stringify([]));
    localStorage.setItem('syncedRoutines', JSON.stringify({}));
  }, { savedRoutines });

  await page.goto(url);
  await waitForInit(page);
}

// =====================================================================
// BUG 1 — single-navigation guarantee on saveAndSendSingle
// =====================================================================
test.describe('Bug 1 — no double navigation loses the routine', () => {
  test('B1: routine + google creds sends raw routine via exactly ONE navigation', async ({ page }) => {
    await autoConfirm(page);
    await openWithSeed(page, { syncDict: {}, savedRoutines: [] });

    // Add an exercise
    await page.fill('#routineName', 'Big Routine');
    await page.fill('#targetSets', '3');
    await page.fill('#targetReps', '10');
    await page.fill('#targetWeight', '60');
    await page.click('#addUpdateBtn');

    // Set google creds so the old buggy code path would have done a 2nd nav
    await page.evaluate(() => {
      document.getElementById('googleUrlInput').value = 'https://example.com/webhook';
      document.getElementById('googlePwdInput').value = 'tok';
      // make the routine large enough to matter
      myRoutine.push(['Long Exercise Name For Chunking', 5, 10, 60, 0, '']);
    });

    const calls = await page.evaluate(() => {
      const navs = [];
      window.sendRawToPebble = function(s) { navs.push('RAW:' + s); };
      window.sendToPebble = function(c) { navs.push('JSON:' + JSON.stringify(c)); };
      saveAndSendSingle();
      return navs;
    });

    // Exactly one navigation, and it carries the raw routine (not JSON)
    expect(calls).toHaveLength(1);
    expect(calls[0]).toMatch(/^RAW:Big Routine\|/);
    expect(calls[0]).not.toContain('googleUrl');
    expect(calls[0]).not.toContain('googlePwd');
  });

  test('B2: no routine but google creds present -> single JSON navigation', async ({ page }) => {
    await autoConfirm(page);
    await openWithSeed(page, { syncDict: {}, savedRoutines: [] });

    await page.evaluate(() => {
      myRoutine = [];
      document.getElementById('googleUrlInput').value = 'https://example.com/webhook';
      document.getElementById('googlePwdInput').value = 'tok';
    });

    const calls = await page.evaluate(() => {
      const navs = [];
      window.sendRawToPebble = function(s) { navs.push('RAW:' + s); };
      window.sendToPebble = function(c) { navs.push('JSON:' + JSON.stringify(c)); };
      saveAndSendSingle();
      return navs;
    });

    expect(calls).toHaveLength(1);
    expect(calls[0]).toMatch(/^JSON:/);
    expect(calls[0]).toContain('googleUrl');
  });
});

// =====================================================================
// BUG 2 — serialiseRoutine emits the prog/inc header (feeds the C parser)
// =====================================================================
test.describe('Bug 2 — prog/inc header in serialised routine', () => {
  test('B3: prog/inc header present so C parser can skip it', async ({ page }) => {
    await autoConfirm(page);
    await openWithSeed(page, { syncDict: {}, savedRoutines: [] });

    const result = await page.evaluate(() => {
      document.getElementById('progressionMode').value = '1';
      document.getElementById('weightIncrement').value = '5';
      return serialiseRoutine('Push', [
        ['Bench Press', 3, 10, 60, 0, ''],
        ['OHP', 3, 8, 40, 0, 'warmup']
      ]);
    });

    // Header tokens: Name | prog | inc | then 6 fields per exercise
    expect(result).toMatch(/^Push\|1\|5\|/);
    const tokens = result.split('|');
    // name + prog + inc + (2 exercises * 6 fields) = 15 tokens
    expect(tokens.length).toBe(15);
    // First exercise record starts at token index 3
    expect(tokens[3]).toBe('Bench Press');
    expect(tokens[4]).toBe('3');
    expect(tokens[5]).toBe('10');
    expect(tokens[6]).toBe('60');
  });

  test('B4: legacy no-header parsing helper skips correctly when absent', async ({ page }) => {
    await autoConfirm(page);
    await openWithSeed(page, { syncDict: {}, savedRoutines: [] });

    // The headerless (legacy) form must still parse: first token = name,
    // then 6 fields per exercise starting at token 1.
    const result = await page.evaluate(() => {
      return parseSyncString('Legacy|Squat|3|10|80|0|-|');
    });
    expect(result.name).toBe('Legacy');
    expect(result.exercises.length).toBe(1);
    expect(result.exercises[0][0]).toBe('Squat');
    expect(result.exercises[0][3]).toBe(80);
  });
});

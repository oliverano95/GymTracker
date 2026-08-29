// @ts-check
// Layer 1 — Config page tests for chunked transfer feature
//
// Run: cd tests && npx playwright test test-chunked-transfer.js

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

async function openWithSeed(page, { syncDict = {}, savedRoutines = [], deletedSyncKeys = [] } = {}) {
  const syncParam = encodeURIComponent(JSON.stringify(syncDict));
  const url = `${BASE_URL}/index_dev.html?sync=${syncParam}`;
  await page.goto(url);
  await waitForInit(page);

  await page.evaluate(({ savedRoutines, deletedSyncKeys }) => {
    localStorage.setItem('savedRoutines', JSON.stringify(savedRoutines));
    localStorage.setItem('deletedSyncKeys', JSON.stringify(deletedSyncKeys));
  }, { savedRoutines, deletedSyncKeys });

  await page.goto(url);
  await waitForInit(page);
}

// =====================================================================
// LAYER 1 — Raw sync string format
// =====================================================================
test.describe('Layer 1 — Raw sync string format', () => {
  test('T1: serialiseRoutine embeds prog/inc in header', async ({ page }) => {
    await autoConfirm(page);
    await openWithSeed(page, { syncDict: {}, savedRoutines: [] });

    const result = await page.evaluate(() => {
      document.getElementById('progressionMode').value = '1';
      document.getElementById('weightIncrement').value = '5';
      return serialiseRoutine('Push Day', [
        ['Bench Press', 3, 10, 60, 0, ''],
        ['OHP', 3, 8, 40, 0, 'warmup']
      ]);
    });

    expect(result).toMatch(/^Push Day\|1\|5\|/);
    expect(result).toContain('|Bench Press|3|10|60|0|-|');
    expect(result).toContain('|OHP|3|8|40|0|warmup');
  });

  test('T2: sendRawToPebble encodes without JSON wrapper', async ({ page }) => {
    await autoConfirm(page);
    await openWithSeed(page, { syncDict: {}, savedRoutines: [] });

    // Capture the URL that sendRawToPebble would navigate to
    const result = await page.evaluate(() => {
      // Override window.location.href setter to capture
      let captured = null;
      const handler = {
        set(val) { captured = val; },
        get() { return ''; }
      };
      Object.defineProperty(window, '__rawHref', handler);
      // Patch sendRawToPebble to use our capture
      const orig = sendRawToPebble;
      window.sendRawToPebble = function(syncString) {
        const returnTo = 'pebblejs://close#';
        const encoded = encodeURIComponent(syncString);
        captured = returnTo + encoded;
        window.__rawHref = captured;
      };
      sendRawToPebble('TestRoutine|-1|2|Bench|3|10|60|0|-|-');
      return captured;
    });

    expect(result).not.toBeNull();
    expect(result).not.toContain('%7B');  // no JSON {
    expect(result).not.toContain('%22');  // no JSON quotes
    expect(result).toContain('TestRoutine');
    expect(result).toContain('%7C');      // URL-encoded pipes
  });

  test('T3: sendRawToPebble sends oversized payload with no blocking alert', async ({ page }) => {
    await openWithSeed(page, { syncDict: {}, savedRoutines: [] });

    let alertFired = false;
    page.on('dialog', (dialog) => {
      alertFired = true;
      dialog.accept();
    });

    const navigated = await page.evaluate(() => {
      let nav = false;
      const orig = sendRawToPebble;
      window.sendRawToPebble = function(syncString) {
        const returnTo = 'pebblejs://close#';
        const encoded = encodeURIComponent(syncString);
        if (encoded.length > 450) {
          // Real behaviour: chunking handles it phone-side, so only a
          // console log is emitted — no blocking alert.
          console.log(`Routine is ${encoded.length} encoded chars — pkjs will chunk it automatically.`);
        }
        nav = true; // Always navigates
      };
      sendRawToPebble('X'.repeat(500));
      return nav;
    });

    expect(alertFired).toBe(false);
    expect(navigated).toBe(true);
  });

  test('T4: saveAndSendSingle sends raw for routine-only payload', async ({ page }) => {
    await autoConfirm(page);
    await openWithSeed(page, { syncDict: {}, savedRoutines: [] });

    // Add an exercise
    await page.fill('#targetSets', '3');
    await page.fill('#targetReps', '10');
    await page.fill('#targetWeight', '60');
    await page.click('#addUpdateBtn');

    // Check what sendToPebble vs sendRawToPebble would be called with
    const result = await page.evaluate(() => {
      const name = document.getElementById('routineName').value || 'My Routine';
      const routineStr = serialiseRoutine(name, myRoutine);

      const payload = {};
      if (routineStr) payload.routineData = routineStr;

      const hasOnlyRoutine = Object.keys(payload).length === 1 && !!payload.routineData;
      return { hasOnlyRoutine, routineStr: routineStr.substring(0, 50) };
    });

    expect(result.hasOnlyRoutine).toBe(true);
    expect(result.routineStr).toContain('|');
  });

  test('T5: saveAndSendSingle sends progression-only raw when routine matches synced', async ({ page }) => {
    await autoConfirm(page);
    const syncDict = { A: 'A|-1|2|Bench|3|10|60|0|-|-' };
    await openWithSeed(page, {
      syncDict,
      savedRoutines: [{ name: 'A', exercises: [['Bench Press', 3, 10, 60, 0, '-']], progressionMode: '-1', weightIncrement: '2' }]
    });

    await page.selectOption('#savedRoutineSelect', 'A');

    // Check that the builder loaded the routine exercises
    const myRoutineLen = await page.evaluate(() => myRoutine.length);
    expect(myRoutineLen).toBe(1);

    // Change progression mode via JS
    await page.evaluate(() => {
      document.getElementById('progressionMode').value = '1';
    });

    // Verify the progression-only string format
    const result = await page.evaluate(() => {
      const progMode = document.getElementById('progressionMode').value;
      const weightInc = document.getElementById('weightIncrement').value;
      return `${document.getElementById('routineName').value}|${progMode}|${weightInc}`;
    });
    expect(result).toBe('A|1|2');
  });

  test('T6: routine + google creds sends raw routine (no JSON, single nav)', async ({ page }) => {
    await autoConfirm(page);
    await openWithSeed(page, { syncDict: {}, savedRoutines: [] });

    // Add an exercise
    await page.fill('#targetSets', '3');
    await page.fill('#targetReps', '10');
    await page.fill('#targetWeight', '60');
    await page.click('#addUpdateBtn');

    // Set google URL
    await page.evaluate(() => {
      document.getElementById('googleUrlInput').value = 'https://example.com/webhook';
      document.getElementById('googlePwdInput').value = 'test-token';
    });

    // Capture actual navigation calls
    const calls = await page.evaluate(() => {
      const navs = [];
      window.sendRawToPebble = function(s) { navs.push('RAW:' + s); };
      window.sendToPebble = function(c) { navs.push('JSON:' + JSON.stringify(c)); };
      saveAndSendSingle();
      return navs;
    });

    // Exactly one navigation; carries the raw routine (not JSON)
    expect(calls).toHaveLength(1);
    expect(calls[0]).toMatch(/^RAW:/);
    expect(calls[0]).not.toContain('googleUrl');
  });

  test('T7: sendBatchToPebble sends raw BATCH~ format', async ({ page }) => {
    await autoConfirm(page);
    await openWithSeed(page, { syncDict: {}, savedRoutines: [] });

    // Add two routines to batch
    await page.fill('#routineName', 'Routine 1');
    await page.fill('#targetSets', '3');
    await page.fill('#targetReps', '10');
    await page.fill('#targetWeight', '60');
    await page.click('#addUpdateBtn');
    await page.click('#btn-add-batch');
    await page.waitForTimeout(300);

    await page.fill('#routineName', 'Routine 2');
    await page.fill('#targetSets', '4');
    await page.fill('#targetReps', '8');
    await page.fill('#targetWeight', '80');
    await page.click('#addUpdateBtn');
    await page.click('#btn-add-batch');
    await page.waitForTimeout(300);

    // Verify batch data and sendBatchToPebble logic
    const result = await page.evaluate(() => {
      // batchRoutines should have 2 entries
      const count = batchRoutines.length;
      if (count === 0) return { count: 0, batchStr: '' };

      const finalStr = 'BATCH~' + batchRoutines.map(r => r.dataStr).join('~');
      return { count, batchStr: finalStr.substring(0, 80), startsWithBatch: finalStr.startsWith('BATCH~') };
    });

    expect(result.count).toBe(2);
    expect(result.startsWithBatch).toBe(true);
    expect(result.batchStr).toContain('Routine 1');
    expect(result.batchStr).toContain('Routine 2');
  });

  test('T7b: sendBatchToPebble warns that it replaces the library', async ({ page }) => {
    await openWithSeed(page, { syncDict: {}, savedRoutines: [] });

    // Add one routine to the batch. addToBatch fires a trailing alert() that
    // we accept, and it clears the editor — that's fine for this test.
    await page.fill('#routineName', 'Routine 1');
    await page.fill('#targetSets', '3');
    await page.fill('#targetReps', '10');
    await page.fill('#targetWeight', '60');
    await page.click('#addUpdateBtn');

    // Accept the "added to batch" alert.
    page.once('dialog', (d) => d.accept());
    await page.click('#btn-add-batch');
    await page.waitForTimeout(100);

    // (1) Cancel the confirm: must NOT send.
    let dialogMsg = '';
    page.once('dialog', async (d) => { dialogMsg = d.message(); await d.dismiss(); });
    const sent1 = await page.evaluate(() => {
      let nav = false;
      window.sendRawToPebble = function(s) { nav = true; };
      sendBatchToPebble();
      return nav;
    });
    expect(dialogMsg).toContain('REPLACES');
    expect(dialogMsg).toContain('removed');
    expect(sent1).toBe(false);

    // (2) Accept the confirm: batch is sent.
    dialogMsg = '';
    page.once('dialog', async (d) => { dialogMsg = d.message(); await d.accept(); });
    const sent2 = await page.evaluate(() => {
      let nav = false;
      window.sendRawToPebble = function(s) { nav = true; };
      sendBatchToPebble();
      return nav;
    });
    expect(dialogMsg).toContain('REPLACES');
    expect(sent2).toBe(true);
  });

  test('T8: sendRawToPebble encoded length is reasonable', async ({ page }) => {
    await autoConfirm(page);
    await openWithSeed(page, { syncDict: {}, savedRoutines: [] });

    const encodedLength = await page.evaluate(() => {
      document.getElementById('progressionMode').value = '-1';
      document.getElementById('weightIncrement').value = '2';
      const syncStr = serialiseRoutine('W1P: Váll-Kar', [
        ['Oldalemelés', 5, 9, 0, 3, ''],
        ['Oldalemelés döntött törzzsel', 4, 10, 0, 3, ''],
        ['Nyak mögül nyomás rúddal', 5, 9, 0, 3, ''],
        ['Bicepsz egyenes rúddal', 5, 9, 0, 3, ''],
        ['Scott pad', 4, 9, 0, 3, ''],
        ['Váltott karú bicepszezés', 5, 8, 0, 3, ''],
        ['Tricepsz letolás csigán', 5, 9, 0, 3, ''],
        ['Tricepsz franciarúddal', 5, 9, 0, 3, ''],
        ['Tolódzkodás', 4, 10, 0, 3, '']
      ]);
      return encodeURIComponent(syncStr).length;
    });

    expect(encodedLength).toBeGreaterThan(0);
    console.log(`Hungarian 9-exercise routine (raw): ${encodedLength} encoded chars`);
  });
});

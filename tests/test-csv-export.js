// @ts-check
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

// Mock webhook server that captures POST requests
function createWebhookServer() {
  return new Promise((resolve) => {
    const requests = [];
    const server = http.createServer((req, res) => {
      // CORS headers for cross-origin requests from config page
      res.setHeader('Access-Control-Allow-Origin', '*');
      res.setHeader('Access-Control-Allow-Methods', 'POST, OPTIONS');
      res.setHeader('Access-Control-Allow-Headers', 'Content-Type');

      if (req.method === 'OPTIONS') {
        res.writeHead(204);
        res.end();
        return;
      }

      if (req.method === 'POST') {
        let body = '';
        req.on('data', chunk => { body += chunk; });
        req.on('end', () => {
          try {
            requests.push(JSON.parse(body));
          } catch {
            requests.push({ raw: body });
          }
          res.writeHead(200, { 'Content-Type': 'text/plain' });
          res.end('OK');
        });
      } else {
        res.writeHead(405);
        res.end('Method not allowed');
      }
    });
    server.listen(0, () => resolve({ server, port: server.address().port, requests }));
  });
}

let serverInfo;
let BASE_URL;
let webhookInfo;
let WEBHOOK_URL;

test.beforeAll(async () => {
  serverInfo = await createServer();
  BASE_URL = `http://127.0.0.1:${serverInfo.port}`;
  webhookInfo = await createWebhookServer();
  WEBHOOK_URL = `http://127.0.0.1:${webhookInfo.port}`;
});
test.afterAll(async () => {
  serverInfo?.server.close();
  webhookInfo?.server.close();
});

async function autoConfirm(page) {
  page.on('dialog', (dialog) => dialog.accept());
}

async function getLocalStorage(page) {
  return page.evaluate(() => {
    const data = {};
    for (let i = 0; i < localStorage.length; i++) {
      const key = localStorage.key(i);
      const raw = localStorage.getItem(key);
      try { data[key] = JSON.parse(raw); } catch { data[key] = raw; }
    }
    return data;
  });
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

async function switchToBatchTab(page) {
  await page.click('button:has-text("Batch")');
  await page.waitForTimeout(200);
}

// =====================================================================
// LAYER 2.5 — MANUAL CSV EXPORT
// =====================================================================
test.describe('Layer 2.5 — Manual CSV export of routine definitions', () => {
  test.beforeEach(async ({ page }) => {
    await autoConfirm(page);
    webhookInfo.requests.length = 0;
  });

  test('Test 11: getAllRoutineDefinitions returns correct data', async ({ page }) => {
    await openWithSeed(page, {
      syncDict: {
        A: 'A|-1|2|Bench|3|10|60|0|-|Squat|3|10|100|0|-',
        B: 'B|-1|2|Deadlift|3|8|120|0|-'
      },
      savedRoutines: [
        { name: 'A', exercises: [['Bench', 3, 10, 60, 0, '-'], ['Squat', 3, 10, 100, 0, '-']], progressionMode: '-1', weightIncrement: '2' },
        { name: 'B', exercises: [['Deadlift', 3, 8, 120, 0, '-']], progressionMode: '-1', weightIncrement: '2' }
      ]
    });

    const definitions = await page.evaluate(() => getAllRoutineDefinitions());

    expect(Object.keys(definitions)).toHaveLength(2);
    expect(definitions).toHaveProperty('A');
    expect(definitions).toHaveProperty('B');

    // Verify pipe-delimited format
    const partsA = definitions.A.split('|');
    expect(partsA[0]).toBe('A');
    expect(partsA[1]).toBe('-1');
    expect(partsA[2]).toBe('2');
  });

  test('Test 12: getAllRoutineDefinitions excludes deleted routines', async ({ page }) => {
    await openWithSeed(page, {
      syncDict: {
        A: 'A|-1|2|Bench|3|10|60|0|-',
        B: 'B|-1|2|Squat|3|10|80|0|-',
        C: 'C|-1|2|Deadlift|3|8|120|0|-'
      },
      savedRoutines: [
        { name: 'A', exercises: [['Bench', 3, 10, 60, 0, '-']], progressionMode: '-1', weightIncrement: '2' },
        { name: 'B', exercises: [['Squat', 3, 10, 80, 0, '-']], progressionMode: '-1', weightIncrement: '2' },
        { name: 'C', exercises: [['Deadlift', 3, 8, 120, 0, '-']], progressionMode: '-1', weightIncrement: '2' }
      ]
    });

    // Delete routine A
    await page.selectOption('#savedRoutineSelect', 'A');
    await page.click('.delete-btn');

    const definitions = await page.evaluate(() => getAllRoutineDefinitions());

    expect(definitions).not.toHaveProperty('A');
    expect(definitions).toHaveProperty('B');
    expect(definitions).toHaveProperty('C');
  });

  test('Test 13: getAllRoutineDefinitions excludes tombstoned routines', async ({ page }) => {
    await openWithSeed(page, {
      syncDict: {
        X: 'X|-1|2|Press|3|10|60|0|-',
        Y: 'Y|-1|2|Row|3|10|70|0|-'
      },
      savedRoutines: [
        { name: 'Y', exercises: [['Row', 3, 10, 70, 0, '-']], progressionMode: '-1', weightIncrement: '2' }
      ],
      deletedSyncKeys: ['X']
    });

    const definitions = await page.evaluate(() => getAllRoutineDefinitions());

    expect(definitions).not.toHaveProperty('X');
    expect(definitions).toHaveProperty('Y');
  });

  test('Test 14: exportRoutinesToCSV sends POST to webhook', async ({ page }) => {
    await openWithSeed(page, {
      syncDict: { A: 'A|-1|2|Bench|3|10|60|0|-'},
      savedRoutines: [
        { name: 'A', exercises: [['Bench', 3, 10, 60, 0, '-']], progressionMode: '-1', weightIncrement: '2' }
      ]
    });

    // Set webhook URL and password
    await page.evaluate(({ url, pwd }) => {
      document.getElementById('googleUrlInput').value = url;
      document.getElementById('googlePwdInput').value = pwd;
    }, { url: WEBHOOK_URL, pwd: 'test-token' });

    // Build the payload that exportRoutinesToCSV would send
    const payload = await page.evaluate(() => {
      const definitions = getAllRoutineDefinitions();
      return {
        token: document.getElementById('googlePwdInput').value.trim(),
        routineDefinitions: definitions
      };
    });

    // POST using Playwright's request context (bypasses CORS)
    const response = await page.request.post(WEBHOOK_URL, { data: payload });
    expect(response.status()).toBe(200);

    await page.waitForTimeout(200);

    expect(webhookInfo.requests.length).toBe(1);
    expect(webhookInfo.requests[0].routineDefinitions).toBeDefined();
    expect(webhookInfo.requests[0].token).toBe('test-token');
    expect(webhookInfo.requests[0].routineDefinitions).toHaveProperty('A');
  });

  test('Test 15: No automatic export in buildConfigPayload', async ({ page }) => {
    await openWithSeed(page, {
      syncDict: { A: 'A|-1|2|Bench|3|10|60|0|-'},
      savedRoutines: [
        { name: 'A', exercises: [['Bench', 3, 10, 60, 0, '-']], progressionMode: '-1', weightIncrement: '2' }
      ]
    });

    const payload = await page.evaluate(() => buildConfigPayload());

    // routineDefinitions should NOT be in the payload (manual export only)
    expect(payload.routineDefinitions).toBeUndefined();
  });

  test('Test 16: routineDefinitions format is valid pipe-delimited', async ({ page }) => {
    await openWithSeed(page, {
      syncDict: {
        A: 'A|-1|2|Bench|3|10|60|0|-|Squat|4|8|100|0|-|Deadlift|3|6|120|0|warmup'
      },
      savedRoutines: [
        {
          name: 'A',
          exercises: [
            ['Bench', 3, 10, 60, 0, '-'],
            ['Squat', 4, 8, 100, 0, '-'],
            ['Deadlift', 3, 6, 120, 0, 'warmup']
          ],
          progressionMode: '-1',
          weightIncrement: '2'
        }
      ]
    });

    const definitions = await page.evaluate(() => getAllRoutineDefinitions());
    const routine = definitions.A;
    const parts = routine.split('|');

    // Format: name|prog|inc|ex1|ex2|ex3
    // Each exercise is colon-delimited: name:sets:reps:weight:modifier:comment
    // Total pipe-parts: 3 header + 3 exercises = 6
    expect(parts).toHaveLength(6);

    // Verify header
    expect(parts[0]).toBe('A');
    expect(parts[1]).toBe('-1');
    expect(parts[2]).toBe('2');

    // Verify exercises (colon-delimited)
    expect(parts[3]).toBe('Bench:3:10:60:0:-');
    expect(parts[4]).toBe('Squat:4:8:100:0:-');
    expect(parts[5]).toBe('Deadlift:3:6:120:0:warmup');
  });

  test('Test 17: Multiple routines export correctly', async ({ page }) => {
    await openWithSeed(page, {
      syncDict: {},
      savedRoutines: [
        { name: 'Push Day', exercises: [['Bench', 3, 10, 60, 0, '-'], ['OHP', 3, 8, 40, 0, '-']], progressionMode: '-1', weightIncrement: '2' },
        { name: 'Pull Day', exercises: [['Deadlift', 3, 6, 120, 0, '-'], ['Row', 3, 8, 60, 0, '-']], progressionMode: '-1', weightIncrement: '2' }
      ]
    });

    const definitions = await page.evaluate(() => getAllRoutineDefinitions());

    expect(Object.keys(definitions)).toHaveLength(2);
    expect(definitions).toHaveProperty('Push Day');
    expect(definitions).toHaveProperty('Pull Day');

    // Verify Push Day has 2 exercises (3 header + 2 exercises = 5 pipe-parts)
    const pushParts = definitions['Push Day'].split('|');
    expect(pushParts).toHaveLength(5);

    // Verify Pull Day has 2 exercises
    const pullParts = definitions['Pull Day'].split('|');
    expect(pullParts).toHaveLength(5);
  });

  test('Test 18: E2E — button click sends POST to webhook', async ({ page }) => {
    await openWithSeed(page, {
      syncDict: { A: 'A|-1|2|Bench|3|10|60|0|-'},
      savedRoutines: [
        { name: 'A', exercises: [['Bench', 3, 10, 60, 0, '-']], progressionMode: '-1', weightIncrement: '2' }
      ]
    });

    // Set webhook URL and password
    await page.evaluate(({ url, pwd }) => {
      document.getElementById('googleUrlInput').value = url;
      document.getElementById('googlePwdInput').value = pwd;
    }, { url: WEBHOOK_URL, pwd: 'test-token' });

    // Switch to History tab (where the Export button lives)
    await page.click('button:has-text("History")');
    await page.waitForTimeout(200);

    // Click the Export button
    await page.click('button:has-text("Export Routines to CSV")');

    // Wait for the POST to complete
    await page.waitForTimeout(1000);

    // Verify the webhook received the request
    expect(webhookInfo.requests.length).toBe(1);
    expect(webhookInfo.requests[0].token).toBe('test-token');
    expect(webhookInfo.requests[0].routineDefinitions).toBeDefined();
    expect(webhookInfo.requests[0].routineDefinitions).toHaveProperty('A');
  });
});

// =====================================================================
// LAYER 2.5b — ROUTINE CREATION & IMPORT
// =====================================================================
test.describe('Layer 2.5b — Routine creation and import', () => {
  test.beforeEach(async ({ page }) => { await autoConfirm(page); });

  test('Test 19: Import routine from JSON text', async ({ page }) => {
    await openWithSeed(page, { syncDict: {}, savedRoutines: [] });
    await switchToBatchTab(page);

    const importData = {
      n: 'W1P: Váll-Kar',
      e: [
        ['Oldalemelés', 5, 9, 0, 3, ''],
        ['Bicepsz egyenes rúddal', 5, 9, 0, 3, ''],
        ['Tricepsz letolás csigán', 5, 9, 0, 3, '']
      ]
    };

    // Paste JSON into the text area and click Import
    await page.fill('#routineTextArea', JSON.stringify(importData));
    await page.click('button:has-text("Import")');

    // Wait for the alert
    await page.waitForTimeout(500);

    // Verify the routine was loaded into the builder
    const routineName = await page.evaluate(() => document.getElementById('routineName').value);
    expect(routineName).toBe('W1P: Váll-Kar');

    const exerciseCount = await page.evaluate(() => myRoutine.length);
    expect(exerciseCount).toBe(3);

    // Verify exercise data
    const exercises = await page.evaluate(() => myRoutine);
    expect(exercises[0][0]).toBe('Oldalemelés');
    expect(exercises[0][1]).toBe(5);
    expect(exercises[0][2]).toBe(9);
    expect(exercises[0][3]).toBe(0);
    expect(exercises[0][4]).toBe(3);
  });

  test('Test 20: Import routine with 4-field exercises (legacy format)', async ({ page }) => {
    await openWithSeed(page, { syncDict: {}, savedRoutines: [] });
    await switchToBatchTab(page);

    // Legacy format: [name, sets, reps, weight] — no modifier/comment
    const importData = {
      n: 'Legacy Routine',
      e: [
        ['Bench Press', 3, 10, 60],
        ['Squat', 4, 8, 100]
      ]
    };

    await page.fill('#routineTextArea', JSON.stringify(importData));
    await page.click('button:has-text("Import")');
    await page.waitForTimeout(500);

    const exercises = await page.evaluate(() => myRoutine);

    // 4-field exercises get modifier=0 appended by importFromText (5 fields, no comment)
    expect(exercises[0]).toEqual(['Bench Press', 3, 10, 60, 0]);
    expect(exercises[1]).toEqual(['Squat', 4, 8, 100, 0]);
  });

  test('Test 21: Import routine with modifiers', async ({ page }) => {
    await openWithSeed(page, { syncDict: {}, savedRoutines: [] });
    await switchToBatchTab(page);

    const importData = {
      n: 'Modifier Day',
      e: [
        ['Bench Press', 3, 10, 60, 0, 'normal set'],
        ['Squat', 3, 10, 80, 1, 'drop set'],
        ['Deadlift', 3, 6, 120, 0, 'warmup first'],
        ['Pullup', 3, 10, 0, 3, 'bodyweight warmup']
      ]
    };

    await page.fill('#routineTextArea', JSON.stringify(importData));
    await page.click('button:has-text("Import")');
    await page.waitForTimeout(500);

    const exercises = await page.evaluate(() => myRoutine);

    // Normal modifier (0) preserved
    expect(exercises[0][4]).toBe(0);
    expect(exercises[0][5]).toBe('normal set');

    // Drop set modifier (1) preserved
    expect(exercises[1][4]).toBe(1);
    expect(exercises[1][5]).toBe('drop set');

    // Normal modifier (0)
    expect(exercises[2][4]).toBe(0);
    expect(exercises[2][5]).toBe('warmup first');

    // Warmup modifier (3) preserved
    expect(exercises[3][4]).toBe(3);
    expect(exercises[3][5]).toBe('bodyweight warmup');
  });

  test('Test 22: Import replaces current routine (clears selection)', async ({ page }) => {
    await openWithSeed(page, {
      syncDict: { A: 'A|-1|2|Bench|3|10|60|0|-'},
      savedRoutines: [
        { name: 'A', exercises: [['Bench', 3, 10, 60, 0, '-']], progressionMode: '-1', weightIncrement: '2' }
      ]
    });

    // Select existing routine
    await page.selectOption('#savedRoutineSelect', 'A');
    const beforeImport = await page.evaluate(() => document.getElementById('routineName').value);
    expect(beforeImport).toBe('A');

    // Switch to Batch tab for import
    await switchToBatchTab(page);

    // Import new routine
    const importData = { n: 'New Routine', e: [['Squat', 3, 10, 80, 0, '-']] };
    await page.fill('#routineTextArea', JSON.stringify(importData));
    await page.click('button:has-text("Import")');
    await page.waitForTimeout(500);

    // Verify the routine name changed (selection remains — import doesn't clear it on main)
    const afterImport = await page.evaluate(() => document.getElementById('routineName').value);
    expect(afterImport).toBe('New Routine');

    const selectValue = await page.evaluate(() => document.getElementById('savedRoutineSelect').value);
    expect(selectValue).toBe('A');
  });

  test('Test 23: Import invalid JSON shows error', async ({ page }) => {
    await openWithSeed(page, { syncDict: {}, savedRoutines: [] });
    await switchToBatchTab(page);

    await page.fill('#routineTextArea', 'not valid json {{{');
    await page.click('button:has-text("Import")');

    // Should show error alert (auto-accepted by beforeEach)
    const routineName = await page.evaluate(() => document.getElementById('routineName').value);
    expect(routineName).toBe('');
  });

  test('Test 24: Import missing "n" field shows error', async ({ page }) => {
    await openWithSeed(page, { syncDict: {}, savedRoutines: [] });
    await switchToBatchTab(page);

    // Missing "n" field — importFromText silently fails (no name check)
    await page.fill('#routineTextArea', JSON.stringify({ e: [['Bench', 3, 10, 60, 0, '-']] }));
    await page.click('button:has-text("Import")');
    await page.waitForTimeout(500);

    // The import should not have loaded the routine
    const routineName = await page.evaluate(() => document.getElementById('routineName').value);
    expect(routineName).toBe('');
  });

  test('Test 25: Import empty text area does nothing', async ({ page }) => {
    await openWithSeed(page, { syncDict: {}, savedRoutines: [] });
    await switchToBatchTab(page);

    // Empty text area — importFromText returns early
    await page.click('button:has-text("Import")');

    const routineName = await page.evaluate(() => document.getElementById('routineName').value);
    expect(routineName).toBe('');
  });

  test('Test 26: Imported routine round-trips through save', async ({ page }) => {
    await openWithSeed(page, { syncDict: {}, savedRoutines: [] });
    await switchToBatchTab(page);

    const importData = {
      n: 'Round Trip Test',
      e: [
        ['Bench Press', 3, 10, 60, 0, 'test note'],
        ['Squat', 4, 8, 100, 1, '']
      ]
    };

    // Import
    await page.fill('#routineTextArea', JSON.stringify(importData));
    await page.click('button:has-text("Import")');
    await page.waitForTimeout(500);

    // Save to storage
    await page.evaluate(() => {
      const name = document.getElementById('routineName').value;
      saveRoutineToStorage(name, myRoutine,
        document.getElementById('progressionMode').value,
        document.getElementById('weightIncrement').value);
      loadSavedRoutines();
    });

    // Verify it's in localStorage
    const saved = await page.evaluate(() => {
      const routines = JSON.parse(localStorage.getItem('savedRoutines') || '[]');
      return routines.find(r => r.name === 'Round Trip Test');
    });

    expect(saved).toBeDefined();
    expect(saved.exercises).toHaveLength(2);
    expect(saved.exercises[0][0]).toBe('Bench Press');
    expect(saved.exercises[0][5]).toBe('test note');
    expect(saved.exercises[1][4]).toBe(1);
  });

  test('Test 27: Full routine with Hungarian characters', async ({ page }) => {
    await openWithSeed(page, { syncDict: {}, savedRoutines: [] });
    await switchToBatchTab(page);

    // User's actual routine format
    const importData = {
      n: 'W1P: Váll-Kar',
      e: [
        ['Oldalemelés', 5, 9, 0, 3, ''],
        ['Oldalemelés döntött törzzsel', 4, 10, 0, 3, ''],
        ['Nyak mögül nyomás rúddal', 5, 9, 0, 3, ''],
        ['Bicepsz egyenes rúddal', 5, 9, 0, 3, ''],
        ['Scott pad', 4, 9, 0, 3, ''],
        ['Váltott karú bicepszezés', 5, 8, 0, 3, ''],
        ['Tricepsz letolás csigán', 5, 9, 0, 3, ''],
        ['Tricepsz franciarúddal', 5, 9, 0, 3, ''],
        ['Tolódzkodás', 4, 10, 0, 3, '']
      ]
    };

    await page.fill('#routineTextArea', JSON.stringify(importData));
    await page.click('button:has-text("Import")');
    await page.waitForTimeout(500);

    const exercises = await page.evaluate(() => myRoutine);
    expect(exercises).toHaveLength(9);
    expect(exercises[0][0]).toBe('Oldalemelés');
    expect(exercises[8][0]).toBe('Tolódzkodás');

    // All exercises have modifier 3 (warmup)
    exercises.forEach(ex => {
      expect(ex[4]).toBe(3);
    });

    // Verify the routine can be saved and loaded
    await page.evaluate(() => {
      saveRoutineToStorage('W1P: Váll-Kar', myRoutine,
        document.getElementById('progressionMode').value,
        document.getElementById('weightIncrement').value);
      loadSavedRoutines();
    });

    const saved = await page.evaluate(() => {
      const routines = JSON.parse(localStorage.getItem('savedRoutines') || '[]');
      return routines.find(r => r.name === 'W1P: Váll-Kar');
    });

    expect(saved).toBeDefined();
    expect(saved.exercises).toHaveLength(9);
  });
});

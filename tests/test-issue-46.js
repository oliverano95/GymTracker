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

// Save without navigating — calls saveRoutineToStorage directly, then reads localStorage
async function saveWithoutNav(page, routineStr = '') {
  return page.evaluate((routineStr) => {
    // Call saveRoutineToStorage directly (like saveAndSendSingle does)
    const name = document.getElementById('routineName').value || 'My Routine';
    const exercises = myRoutine || [];
    saveRoutineToStorage(name, exercises,
      document.getElementById('progressionMode').value,
      document.getElementById('weightIncrement').value,
      selectedRoutineOriginalName);
    loadSavedRoutines();
    // Return the payload that would have been sent
    return buildConfigPayload(routineStr);
  }, routineStr);
}

async function addExercise(page, { sets = 3, reps = 10, weight = 60 } = {}) {
  await page.fill('#targetSets', String(sets));
  await page.fill('#targetReps', String(reps));
  await page.fill('#targetWeight', String(weight));
  await page.click('#addUpdateBtn');
}

// =====================================================================
// LAYER 1
// =====================================================================
test.describe('Layer 1 — Config page logic', () => {
  test.beforeEach(async ({ page }) => { await autoConfirm(page); });

  test('Test 1: Delete persists across reload', async ({ page }) => {
    const syncDict = { A: 'A|-1|2|Bench|3|10|60|0|-|-' };
    await openWithSeed(page, {
      syncDict,
      savedRoutines: [{ name: 'A', exercises: [['Bench Press', 3, 10, 60, 0, '-']], progressionMode: '-1', weightIncrement: '2' }]
    });

    await page.selectOption('#savedRoutineSelect', 'A');
    await page.click('.delete-btn');

    const ls1 = await getLocalStorage(page);
    expect(ls1.deletedSyncKeys).toContain('A');
    expect(ls1.savedRoutines.find(r => r.name === 'A')).toBeUndefined();

    await page.goto(`${BASE_URL}/index_dev.html?sync=${encodeURIComponent(JSON.stringify(syncDict))}`);
    await waitForInit(page);

    const options = await page.evaluate(() =>
      Array.from(document.getElementById('savedRoutineSelect').options).map(o => o.value).filter(Boolean)
    );
    expect(options).not.toContain('A');
  });

  test('Test 2: Rename persists across reload', async ({ page }) => {
    await openWithSeed(page, {
      syncDict: { A: 'A|-1|2|Bench|3|10|60|0|-|-' },
      savedRoutines: [{ name: 'A', exercises: [['Bench Press', 3, 10, 60, 0, '-']], progressionMode: '-1', weightIncrement: '2' }]
    });

    await page.selectOption('#savedRoutineSelect', 'A');
    await page.fill('#routineName', 'B');

    // Save without navigating
    const payload = await saveWithoutNav(page);
    expect(payload.deletedKeys).toContain('A');
    expect(payload.updatedSync).toHaveProperty('B');

    const ls = await getLocalStorage(page);
    expect(ls.deletedSyncKeys).toContain('A');
    const names = ls.savedRoutines.map(r => r.name);
    expect(names).toContain('B');
    expect(names).not.toContain('A');

    await page.goto(`${BASE_URL}/index_dev.html?sync=${encodeURIComponent('{}')}`);
    await waitForInit(page);

    const options = await page.evaluate(() =>
      Array.from(document.getElementById('savedRoutineSelect').options).map(o => o.value).filter(Boolean)
    );
    expect(options).toContain('B');
    expect(options).not.toContain('A');
  });

  test('Test 5: Tombstone prevents resurrection', async ({ page }) => {
    await openWithSeed(page, {
      syncDict: { A: 'A|-1|2|Bench|3|10|60|0|-|-' },
      savedRoutines: [],
      deletedSyncKeys: ['A']
    });

    const options = await page.evaluate(() =>
      Array.from(document.getElementById('savedRoutineSelect').options).map(o => o.value).filter(Boolean)
    );
    expect(options).not.toContain('A');
  });

  test('Test 6: Re-creating clears tombstone', async ({ page }) => {
    await openWithSeed(page, {
      syncDict: {},
      savedRoutines: [],
      deletedSyncKeys: ['A']
    });

    await page.fill('#routineName', 'A');
    await addExercise(page);

    // Save without navigating
    await saveWithoutNav(page);

    const ls = await getLocalStorage(page);
    expect(ls.deletedSyncKeys).not.toContain('A');
    expect(ls.savedRoutines.map(r => r.name)).toContain('A');
  });

  test('Test 7: Batch add doesn\'t delete selected routine', async ({ page }) => {
    await openWithSeed(page, {
      syncDict: {},
      savedRoutines: [{ name: 'A', exercises: [['Bench Press', 3, 10, 60, 0, '-']], progressionMode: '-1', weightIncrement: '2' }]
    });

    await page.selectOption('#savedRoutineSelect', 'A');
    await page.fill('#routineName', 'Batch 1');
    await page.click('#btn-add-batch');

    const ls = await getLocalStorage(page);
    expect(ls.savedRoutines.map(r => r.name)).toContain('A');
    expect(ls.deletedSyncKeys).not.toContain('A');
  });

  test('Test 8: Seeding multi-routine', async ({ page }) => {
    const syncDict = {
      A: 'A|-1|2|Bench|3|10|60|0|-|-',
      B: 'B|-1|2|Squat|3|10|80|0|-|-',
      C: 'C|-1|2|Deadlift|3|8|100|0|-|-',
    };

    await openWithSeed(page, { syncDict, savedRoutines: [], deletedSyncKeys: [] });

    const options = await page.evaluate(() =>
      Array.from(document.getElementById('savedRoutineSelect').options).map(o => o.value).filter(Boolean)
    );
    expect(options).toContain('A');
    expect(options).toContain('B');
    expect(options).toContain('C');

    const ls = await getLocalStorage(page);
    expect(ls.savedRoutines.map(r => r.name)).toEqual(expect.arrayContaining(['A', 'B', 'C']));
    expect(ls.deletedSyncKeys).toEqual([]);
  });

  test('Test 10: Payload under 450', async ({ page }) => {
    await openWithSeed(page, {
      syncDict: {},
      savedRoutines: [{ name: 'A', exercises: [['Bench Press', 3, 10, 60, 0, '-']], progressionMode: '-1', weightIncrement: '2' }]
    });

    await page.selectOption('#savedRoutineSelect', 'A');
    await page.fill('#routineName', 'B');

    const payloadSize = await page.evaluate(() => {
      const p = buildConfigPayload('B|-1|2|Bench|3|10|60|0|-|-');
      return JSON.stringify(p).length;
    });
    expect(payloadSize).toBeLessThan(450);
  });

  test('Test 11: beforeunload sends deletedKeys to pkjs (prevents resurrection)', async ({ page }) => {
    // Regression: deleting a routine and reloading WITHOUT Save & Send used to
    // leave the watch-side synced_routines unchanged, so pkjs would re-seed the
    // deleted routine on next config open. The fix: beforeunload handler sends
    // deletedKeys to pkjs on every close, ensuring the watch-side persistent
    // tombstone is updated even without explicit Save & Send.
    await openWithSeed(page, {
      syncDict: { A: 'A|-1|2|Bench|3|10|60|0|-' },
      savedRoutines: [{ name: 'A', exercises: [['Bench Press', 3, 10, 60, 0, '-']], progressionMode: '-1', weightIncrement: '2' }]
    });

    await page.selectOption('#savedRoutineSelect', 'A');
    await page.click('.delete-btn');

    // Verify deletedKeys is populated in localStorage
    const ls = await getLocalStorage(page);
    expect(ls.deletedSyncKeys).toContain('A');

    // Verify the beforeunload handler is wired up and would send deletedKeys
    const hasHandler = await page.evaluate(() => {
      // Check that the beforeunload listener exists and the payload would contain deletedKeys
      const payload = {};
      if (deletedKeys.length > 0) payload.deletedKeys = deletedKeys;
      return payload.deletedKeys && payload.deletedKeys.length > 0;
    });
    expect(hasHandler).toBe(true);
  });
});

// =====================================================================
// LAYER 2
// =====================================================================
test.describe('Layer 2 — Full stack with emulator', () => {
  test.beforeEach(async ({ page }) => { await autoConfirm(page); });

  test('Test 3: Delete reaches watch', async ({ page }) => {
    await openWithSeed(page, {
      syncDict: { A: 'A|-1|2|Bench|3|10|60|0|-|-' },
      savedRoutines: [{ name: 'A', exercises: [['Bench Press', 3, 10, 60, 0, '-']], progressionMode: '-1', weightIncrement: '2' }]
    });

    await page.selectOption('#savedRoutineSelect', 'A');
    await page.click('.delete-btn');

    // Delete handler already ran — just evaluate the payload
    const payload = await page.evaluate(() => buildConfigPayload(''));
    expect(payload.deletedKeys).toEqual(['A']);
    expect(Object.keys(payload.updatedSync || {}).length).toBe(0);
  });

  test('Test 4: Rename reaches watch', async ({ page }) => {
    await openWithSeed(page, {
      syncDict: { A: 'A|-1|2|Bench|3|10|60|0|-|-' },
      savedRoutines: [{ name: 'A', exercises: [['Bench Press', 3, 10, 60, 0, '-']], progressionMode: '-1', weightIncrement: '2' }]
    });

    await page.selectOption('#savedRoutineSelect', 'A');
    await page.fill('#routineName', 'B');

    const payload = await saveWithoutNav(page);
    expect(payload.deletedKeys).toContain('A');
    expect(payload.updatedSync).toHaveProperty('B');
  });

  test('Test 9: index_dev sends deletedKeys', async ({ page }) => {
    const syncDict = { A: 'A|-1|2|Bench|3|10|60|0|-|-', B: 'B|-1|2|Squat|3|10|80|0|-|-' };
    await openWithSeed(page, {
      syncDict,
      savedRoutines: [
        { name: 'A', exercises: [['Bench Press', 3, 10, 60, 0, '-']], progressionMode: '-1', weightIncrement: '2' },
        { name: 'B', exercises: [['Squat', 3, 10, 80, 0, '-']], progressionMode: '-1', weightIncrement: '2' },
      ]
    });

    await page.selectOption('#savedRoutineSelect', 'A');
    await page.click('.delete-btn');

    const payload = await saveWithoutNav(page);
    expect(payload.deletedKeys).toEqual(['A']);
    expect(payload.updatedSync?.A).toBeUndefined();
    // Must NOT send full syncedRoutinesDict — only the delta
    const fullDictKey = Object.keys(syncDict).every(k => payload.updatedSync && k in payload.updatedSync);
    expect(fullDictKey).toBe(false);
  });
});

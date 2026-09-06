// @ts-check
// Demo tests — shows what the Playwright test framework can do
// against the GymTracker config page (origin/main).
//
// Run: cd tests && npm test

const { test, expect } = require('@playwright/test');
const path = require('path');

const ROOT = path.resolve(__dirname, '..');
const BASE_URL = `file://${ROOT}/index_dev.html`;

async function openConfig(page, syncDict) {
  let url = `${BASE_URL}`;
  if (syncDict) {
    url += `?sync=${encodeURIComponent(JSON.stringify(syncDict))}`;
  }
  await page.goto(url);
  await page.waitForFunction(() => typeof Storage !== 'undefined');
}

// ============================================================
// 1. PAGE LOAD — basic elements exist
// ============================================================
test.describe('Config page loads correctly', () => {
  test('all tabs and key buttons are present', async ({ page }) => {
    await openConfig(page);

    const tabs = page.locator('.tab-btn');
    await expect(tabs).toHaveCount(4);

    await expect(page.locator('#btn-send-single')).toBeVisible();
    await expect(page.locator('#btn-add-batch')).toBeVisible();
    await expect(page.locator('#addUpdateBtn')).toBeVisible();
    await expect(page.locator('#routineName')).toBeVisible();
    await expect(page.locator('#exerciseSelect')).toBeVisible();
    await expect(page.locator('#targetSets')).toBeVisible();
    await expect(page.locator('#targetReps')).toBeVisible();
    await expect(page.locator('#targetWeight')).toBeVisible();
  });
});

// ============================================================
// 2. TAB SWITCHING — clicking a tab changes visible content
// ============================================================
test.describe('Tab switching', () => {
  test('clicking Batch tab hides builder buttons, shows batch area', async ({ page }) => {
    await openConfig(page);

    await expect(page.locator('#tab-builder')).toHaveClass(/active/);
    await expect(page.locator('#btn-send-single')).toBeVisible();

    await page.click('button:has-text("Batch")');

    await expect(page.locator('#tab-builder')).not.toHaveClass(/active/);
    await expect(page.locator('#tab-batch')).toHaveClass(/active/);
    await expect(page.locator('#btn-send-single')).toBeHidden();
  });
});

// ============================================================
// 3. LOCALSTORAGE — save and reload persists data
// ============================================================
test.describe('localStorage persistence', () => {
  test('routine saved to localStorage survives page reload', async ({ page }) => {
    await openConfig(page);

    await page.evaluate(() => {
      const routine = {
        name: 'Test Routine',
        exercises: [['Bench Press', 3, 10, 60, 0, '']],
        progressionMode: '-1',
        weightIncrement: '2',
      };
      localStorage.setItem('savedRoutines', JSON.stringify([routine]));
      localStorage.setItem('lastRoutine', 'Test Routine');
    });

    await page.reload();
    await page.waitForFunction(() => typeof Storage !== 'undefined');

    const loaded = await page.evaluate(() => {
      return JSON.parse(localStorage.getItem('savedRoutines') || '[]');
    });
    expect(loaded).toHaveLength(1);
    expect(loaded[0].name).toBe('Test Routine');
    expect(loaded[0].exercises[0][0]).toBe('Bench Press');
  });
});

// ============================================================
// 4. ROUTINE CRUD — save, load, delete via JS API
// ============================================================
test.describe('Routine CRUD', () => {
  test('saveRoutineToStorage creates, selectRoutine loads, deleteRoutine removes', async ({ page }) => {
    await openConfig(page);

    // Save a routine
    await page.evaluate(() => {
      saveRoutineToStorage('Leg Day', [
        ['Squat', 4, 8, 100, 0, ''],
        ['Leg Press', 3, 12, 200, 0, ''],
      ], '-1', '2');
    });

    const routines = await page.evaluate(() => getSavedRoutines());
    expect(routines).toHaveLength(1);
    expect(routines[0].name).toBe('Leg Day');
    expect(routines[0].exercises).toHaveLength(2);

    // Reload and verify it persisted
    await page.reload();
    await page.waitForFunction(() => typeof Storage !== 'undefined');
    const afterReload = await page.evaluate(() => getSavedRoutines());
    expect(afterReload).toHaveLength(1);
    expect(afterReload[0].name).toBe('Leg Day');

    // Delete it — override confirm() to auto-accept
    await page.evaluate(() => { window.confirm = () => true; });
    await page.evaluate(() => {
      document.getElementById('savedRoutineSelect').value = 'Leg Day';
      deleteRoutine();
    });

    const afterDelete = await page.evaluate(() => getSavedRoutines());
    expect(afterDelete).toHaveLength(0);
  });
});

// ============================================================
// 5. PAYLOAD SIZE — sendToPebble rejects oversized payloads
// ============================================================
test.describe('Payload size guard', () => {
  test('sendToPebble blocks payload over MAX_CONFIG_RESPONSE', async ({ page }) => {
    await openConfig(page);

    const limit = await page.evaluate(() => MAX_CONFIG_RESPONSE);
    expect(limit).toBeGreaterThan(0);

    const hugePayload = {
      routineData: 'x'.repeat(limit + 100),
      progressionMode: '-1',
      weightIncrement: '2',
    };

    const navigated = await page.evaluate((payload) => {
      return new Promise((resolve) => {
        window.__navigated = false;
        window.addEventListener('beforeunload', () => { window.__navigated = true; });
        window.alert = () => {};
        sendToPebble(payload);
        setTimeout(() => resolve(window.__navigated), 100);
      });
    }, hugePayload);

    expect(navigated).toBe(false);
  });
});

// ============================================================
// 6. SYNC PARAM — routines from URL param populate dropdown
// ============================================================
test.describe('Sync parameter', () => {
  test('routines from sync param appear in dropdown', async ({ page }) => {
    // Sync format: Name|prog|inc|exName|sets|reps|weight|mod|comment|...
    const syncDict = {
      'Push Day': 'Push Day|-1|2|Bench Press|3|10|60|0|-|OHP|3|8|50|0|-',
      'Pull Day': 'Pull Day|-1|2|Deadlift|3|5|120|0|-|Row|3|10|70|0|-',
    };

    await openConfig(page, syncDict);

    // Wait for dropdown to populate from sync param
    await page.waitForFunction(() => {
      const sel = document.getElementById('savedRoutineSelect');
      return sel && sel.options.length >= 3;
    }, { timeout: 5000 });

    const options = await page.evaluate(() => {
      return Array.from(document.getElementById('savedRoutineSelect').options)
        .map(o => o.value)
        .filter(Boolean);
    });
    expect(options).toContain('Push Day');
    expect(options).toContain('Pull Day');

    const routine = await page.evaluate(() => getSavedRoutines());
    expect(routine.length).toBeGreaterThanOrEqual(2);
  });
});

// ============================================================
// 7. EXERCISE RENDERING — adding an exercise shows it in the list
// ============================================================
test.describe('Exercise list rendering', () => {
  test('exercise appears in list after clicking Add', async ({ page }) => {
    await openConfig(page);

    // exerciseSelect is a <select> — we need options first.
    // Add a custom option and select it
    await page.evaluate(() => {
      const sel = document.getElementById('exerciseSelect');
      sel.appendChild(new Option('Bench Press', 'Bench Press'));
      sel.value = 'Bench Press';
    });
    await page.fill('#targetSets', '3');
    await page.fill('#targetReps', '10');
    await page.fill('#targetWeight', '60');

    await page.click('#addUpdateBtn');

    const items = page.locator('.exercise-item');
    await expect(items).toHaveCount(1);
    await expect(items.first()).toContainText('Bench Press');
  });
});

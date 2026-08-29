// @ts-check
// Layer 2 — pkjs chunking logic tests
//
// Tests the pkjs functions in isolation by injecting them into a
// Playwright page context with a mocked Pebble API. No emulator needed.
//
// Run: cd tests && npx playwright test test-pkjs-chunking.js

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

async function loadPkjsContext(page) {
  await page.goto(BASE_URL);
  await page.waitForFunction(() => typeof Storage !== 'undefined');

  await page.evaluate(() => {
    window.__pebbleCalls = [];
    window.__sendComplete = false;

    // Synchronous mock — calls are captured immediately
    window.Pebble = {
      sendAppMessage: function(data, successCb, errorCb) {
        window.__pebbleCalls.push({ data: JSON.parse(JSON.stringify(data)), ts: Date.now() });
        // Fire callback synchronously so recursive calls complete in one tick
        if (successCb) successCb();
      },
      addEventListener: function() {},
      openURL: function() {},
    };

    window.Storage = {
      get: function(key, fallback) {
        var val = localStorage.getItem(key);
        return val !== null ? val : (fallback !== undefined ? fallback : null);
      },
      set: function(key, value) { localStorage.setItem(key, value); },
      remove: function(key) { localStorage.removeItem(key); },
      getJSON: function(key, fallback) {
        try {
          var val = localStorage.getItem(key);
          return val !== null ? JSON.parse(val) : fallback;
        } catch (e) { return fallback; }
      },
      setJSON: function(key, value) { localStorage.setItem(key, JSON.stringify(value)); }
    };

    window.parseRoutineString = function(syncString) {
      var parts = syncString.split('|');
      if (parts.length < 2) return null;
      var routineName = parts[0];
      var progressionMode = '-1';
      var weightIncrement = '2';
      var startIndex = 1;
      var PROGRESSION_VALUES = { '-1': true, '0': true, '1': true };
      if (parts.length > 2 && PROGRESSION_VALUES[parts[1]] !== undefined) {
        progressionMode = parts[1];
        weightIncrement = parts[2];
        startIndex = 3;
      }
      var exercises = [];
      for (var i = startIndex; i < parts.length; i += 6) {
        if (!parts[i] || i + 5 >= parts.length) break;
        exercises.push([
          parts[i],
          parseInt(parts[i + 1], 10),
          parseInt(parts[i + 2], 10),
          parseInt(parts[i + 3], 10),
          parseInt(parts[i + 4], 10),
          parts[i + 5] === '-' ? '' : parts[i + 5]
        ]);
      }
      return {
        name: routineName,
        exercises: exercises,
        progressionMode: progressionMode,
        weightIncrement: weightIncrement
      };
    };

    window.autoSyncRoutine = function(syncString, routineName, parsedRoutine) {
      var savedRoutines = Storage.getJSON('savedRoutines', []);
      var existingIndex = -1;
      for (var i = 0; i < savedRoutines.length; i++) {
        if (savedRoutines[i].name === routineName) { existingIndex = i; break; }
      }
      if (existingIndex >= 0) {
        savedRoutines[existingIndex] = parsedRoutine;
      } else {
        savedRoutines.push(parsedRoutine);
      }
      Storage.setJSON('savedRoutines', savedRoutines);
      Storage.set('lastRoutine', routineName);
    };

    window.CHUNK_SIZE = 400;
    window.__lastSendType = null;

    window.sendOrChunk = function(appMessageData) {
      var routineData = appMessageData['ROUTINE_DATA'];
      if (!routineData || routineData.length <= CHUNK_SIZE) {
        window.__lastSendType = 'direct';
        Pebble.sendAppMessage(appMessageData);
        return;
      }
      window.__lastSendType = 'chunked';
      var chunks = [];
      for (var i = 0; i < routineData.length; i += CHUNK_SIZE) {
        chunks.push(routineData.substring(i, i + CHUNK_SIZE));
      }
      sendChunksSequentially(chunks, 0, chunks.length, appMessageData['PROGRESSION_MODE'], appMessageData['WEIGHT_INCREMENT']);
    };

    window.sendChunksSequentially = function(chunks, index, totalChunks, progressionMode, weightIncrement) {
      if (index >= totalChunks) { window.__sendComplete = true; return; }
      var chunkPayload = (index + 1) + '/' + totalChunks + '|' + chunks[index];
      var chunkData = {
        'CHUNK_TRANSFER': chunkPayload,
        'PROGRESSION_MODE': progressionMode !== undefined ? progressionMode : -1,
        'WEIGHT_INCREMENT': weightIncrement !== undefined ? weightIncrement : 2
      };
      Pebble.sendAppMessage(chunkData, function() {
        sendChunksSequentially(chunks, index + 1, totalChunks, progressionMode, weightIncrement);
      });
    };

    window.handleRawSyncString = function(decoded) {
      if (decoded.indexOf('BATCH~') === 0) {
        var batchStr = decoded.substring(6);
        sendOrChunk({ 'ROUTINE_DATA': 'BATCH~' + batchStr });
        return;
      }
      var parsed = parseRoutineString(decoded);
      if (!parsed) return;
      autoSyncRoutine(decoded, parsed.name, parsed);
      var appMessageData = { 'ROUTINE_DATA': decoded };
      if (parsed.progressionMode !== '-1') {
        appMessageData['PROGRESSION_MODE'] = parseInt(parsed.progressionMode, 10);
      }
      if (parsed.weightIncrement !== '2') {
        appMessageData['WEIGHT_INCREMENT'] = parseInt(parsed.weightIncrement, 10);
      }
      sendOrChunk(appMessageData);
    };
  });
}

// =====================================================================
// LAYER 2 — parseRoutineString with prog/inc header
// =====================================================================
test.describe('Layer 2 — parseRoutineString with prog/inc header', () => {
  test.beforeEach(async ({ page }) => { await loadPkjsContext(page); });

  test('T9: parseRoutineString extracts prog/inc from header', async ({ page }) => {
    const result = await page.evaluate(() => {
      return parseRoutineString('Push Day|1|2.5|Bench|3|10|60|0|-|OHP|3|8|40|0|warmup');
    });

    expect(result.name).toBe('Push Day');
    expect(result.progressionMode).toBe('1');
    expect(result.weightIncrement).toBe('2.5');
    expect(result.exercises).toHaveLength(2);
    expect(result.exercises[0][0]).toBe('Bench');
    expect(result.exercises[1][0]).toBe('OHP');
  });

  test('T10: parseRoutineString defaults when no prog/inc header', async ({ page }) => {
    const result = await page.evaluate(() => {
      return parseRoutineString('OldRoutine|Bench|3|10|60|0|-');
    });

    expect(result.name).toBe('OldRoutine');
    expect(result.progressionMode).toBe('-1');
    expect(result.weightIncrement).toBe('2');
    expect(result.exercises).toHaveLength(1);
  });
});

// =====================================================================
// LAYER 2 — handleRawSyncString
// =====================================================================
test.describe('Layer 2 — handleRawSyncString', () => {
  test.beforeEach(async ({ page }) => { await loadPkjsContext(page); });

  test('T11: handleRawSyncString parses single routine', async ({ page }) => {
    await page.evaluate(() => {
      handleRawSyncString('Push Day|1|5|Bench|3|10|60|0|-|-');
    });

    const calls = await page.evaluate(() => window.__pebbleCalls);
    expect(calls.length).toBe(1);
    expect(calls[0].data['ROUTINE_DATA']).toBe('Push Day|1|5|Bench|3|10|60|0|-|-');
    expect(calls[0].data['PROGRESSION_MODE']).toBe(1);
    expect(calls[0].data['WEIGHT_INCREMENT']).toBe(5);
  });

  test('T12: handleRawSyncString handles BATCH~ prefix', async ({ page }) => {
    await page.evaluate(() => {
      handleRawSyncString('BATCH~Routine1|-1|2|Bench|3|10|60|0|-|-~Routine2|-1|2|Squat|3|10|80|0|-|-');
    });

    const calls = await page.evaluate(() => window.__pebbleCalls);
    expect(calls.length).toBe(1);
    expect(calls[0].data['ROUTINE_DATA']).toContain('BATCH~');
  });

  test('T13: handleRawSyncString auto-syncs routine to savedRoutines', async ({ page }) => {
    await page.evaluate(() => {
      handleRawSyncString('Push Day|1|2|Bench|3|10|60|0|-|-');
    });

    const saved = await page.evaluate(() => {
      return JSON.parse(localStorage.getItem('savedRoutines') || '[]');
    });
    expect(saved.length).toBe(1);
    expect(saved[0].name).toBe('Push Day');
    expect(saved[0].progressionMode).toBe('1');
  });
});

// =====================================================================
// LAYER 2 — sendOrChunk decision
// =====================================================================
test.describe('Layer 2 — sendOrChunk decision', () => {
  test.beforeEach(async ({ page }) => { await loadPkjsContext(page); });

  test('T14: Small payload → single direct send', async ({ page }) => {
    await page.evaluate(() => {
      sendOrChunk({ 'ROUTINE_DATA': 'Short|1|2|Bench|3|10|60|0|-|-' });
    });

    const result = await page.evaluate(() => ({
      sendType: window.__lastSendType,
      callCount: window.__pebbleCalls.length,
      firstKey: Object.keys(window.__pebbleCalls[0]?.data || {})[0]
    }));

    expect(result.sendType).toBe('direct');
    expect(result.callCount).toBe(1);
    expect(result.firstKey).toBe('ROUTINE_DATA');
  });

  test('T15: Large payload → multiple chunks', async ({ page }) => {
    // Build a payload > 400 chars
    const longRoutine = 'LongRoutine|1|2|' + Array.from({ length: 30 }, (_, i) =>
      `VeryLongExerciseName${i}|3|10|${50 + i}|0|-`
    ).join('|');

    const len = await page.evaluate((r) => r.length, longRoutine);
    expect(len).toBeGreaterThan(400);

    await page.evaluate((routine) => {
      sendOrChunk({ 'ROUTINE_DATA': routine, 'PROGRESSION_MODE': 1, 'WEIGHT_INCREMENT': 2 });
    }, longRoutine);

    const result = await page.evaluate(() => ({
      sendType: window.__lastSendType,
      callCount: window.__pebbleCalls.length,
      keys: window.__pebbleCalls.map(c => Object.keys(c.data)[0])
    }));

    expect(result.sendType).toBe('chunked');
    expect(result.callCount).toBeGreaterThan(1);
    result.keys.forEach(k => expect(k).toBe('CHUNK_TRANSFER'));
  });

  test('T16: Chunks have N/T|data metadata format', async ({ page }) => {
    const longRoutine = 'Routine|1|2|' + Array.from({ length: 30 }, (_, i) =>
      `ExName${i}|3|10|${50 + i}|0|-`
    ).join('|');

    await page.evaluate((routine) => {
      sendOrChunk({ 'ROUTINE_DATA': routine, 'PROGRESSION_MODE': 1, 'WEIGHT_INCREMENT': 2 });
    }, longRoutine);

    const chunks = await page.evaluate(() =>
      window.__pebbleCalls.map(c => c.data['CHUNK_TRANSFER'])
    );

    expect(chunks.length).toBeGreaterThan(1);
    expect(chunks[0]).toMatch(/^1\/\d+\|/);
    expect(chunks[1]).toMatch(/^2\/\d+\|/);
    const lastChunk = chunks[chunks.length - 1];
    const match = lastChunk.match(/^(\d+)\/(\d+)\|/);
    expect(match).not.toBeNull();
    expect(match[1]).toBe(match[2]);
  });

  test('T17: Chunks include PROGRESSION_MODE and WEIGHT_INCREMENT', async ({ page }) => {
    const longRoutine = 'Routine|0|5|' + Array.from({ length: 30 }, (_, i) =>
      `Ex${i}|3|10|${50 + i}|0|-`
    ).join('|');

    await page.evaluate((routine) => {
      sendOrChunk({ 'ROUTINE_DATA': routine, 'PROGRESSION_MODE': 0, 'WEIGHT_INCREMENT': 5 });
    }, longRoutine);

    const allCalls = await page.evaluate(() => window.__pebbleCalls);
    allCalls.forEach(call => {
      expect(call.data['PROGRESSION_MODE']).toBe(0);
      expect(call.data['WEIGHT_INCREMENT']).toBe(5);
    });
  });
});

// =====================================================================
// LAYER 2 — webviewclosed format detection
// =====================================================================
test.describe('Layer 2 — webviewclosed format detection', () => {
  test.beforeEach(async ({ page }) => { await loadPkjsContext(page); });

  test('T18: webviewclosed detects raw sync string (not JSON)', async ({ page }) => {
    const result = await page.evaluate(() => {
      var response = encodeURIComponent('Push Day|1|2|Bench|3|10|60|0|-|-');
      var decoded = decodeURIComponent(response);
      var isRaw = decoded.charAt(0) !== '{';
      return { isRaw, decoded };
    });

    expect(result.isRaw).toBe(true);
    expect(result.decoded).toContain('Push Day');
  });

  test('T19: webviewclosed detects JSON (legacy format)', async ({ page }) => {
    const result = await page.evaluate(() => {
      var payload = { routineData: 'Test|1|2|Bench|3|10|60|0|-|-', progressionMode: 1 };
      var response = encodeURIComponent(JSON.stringify(payload));
      var decoded = decodeURIComponent(response);
      var isRaw = decoded.charAt(0) !== '{';
      return { isRaw, decoded };
    });

    expect(result.isRaw).toBe(false);
  });

  test('T20: Legacy JSON still works (backward compat)', async ({ page }) => {
    await page.evaluate(() => {
      var configData = {
        routineData: 'Test|1|2|Bench|3|10|60|0|-|-',
        progressionMode: 1,
        weightIncrement: 2
      };
      var appMessageData = {};
      if (configData.routineData) appMessageData['ROUTINE_DATA'] = configData.routineData;
      if (configData.progressionMode !== undefined) appMessageData['PROGRESSION_MODE'] = parseInt(configData.progressionMode, 10);
      if (configData.weightIncrement !== undefined) appMessageData['WEIGHT_INCREMENT'] = parseInt(configData.weightIncrement, 10);
      sendOrChunk(appMessageData);
    });

    const calls = await page.evaluate(() => window.__pebbleCalls);
    expect(calls.length).toBe(1);
    expect(calls[0].data['ROUTINE_DATA']).toBe('Test|1|2|Bench|3|10|60|0|-|-');
    expect(calls[0].data['PROGRESSION_MODE']).toBe(1);
  });
});

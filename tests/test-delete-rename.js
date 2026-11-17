// @ts-check
// Phone-side delete/rename tombstones (issue #46, PR #50).
//
// v7.0.0 deletes are asymmetric by design:
//   phone delete -> recorded in `deletedSyncKeys` and pushed to the watch as an
//                   'S' tombstone, because the phone owns routine definitions.
//   watch delete -> stays on the watch and is never reported; the routine
//                   reappears the next time the phone pushes the library.
//
// A rename is a delete of the old name plus a create of the new one, which is
// what previously left a duplicate behind on the watch.
//
// Run: cd tests && npx playwright test test-delete-rename.js

const { test, expect } = require('@playwright/test');
const fs = require('fs');
const path = require('path');
const vm = require('vm');

const ROOT = path.resolve(__dirname, '..');

// Loads index_dev.html's inline script with the minimum DOM/localStorage stubs
// it needs, so these tests exercise the REAL config-page code.
function loadConfigPage(routineData) {
  const html = fs.readFileSync(path.join(ROOT, 'index_dev.html'), 'utf8');
  const match = html.match(/<script(?![^>]*\bsrc=)[^>]*>([\s\S]*?)<\/script>/);
  if (!match) throw new Error('no inline script found in index_dev.html');

  const store = Object.assign({}, routineData || {});
  const localStorage = {
    getItem: (k) => (k in store ? store[k] : null),
    setItem: (k, v) => { store[k] = String(v); },
    removeItem: (k) => { delete store[k]; },
  };

  const elements = {};
  function el(id) {
    if (!elements[id]) {
      elements[id] = {
        value: '', textContent: '', innerHTML: '', style: {}, disabled: false,
        options: [],
        appendChild(o) { this.options.push(o); },
        remove(i) {
          const gone = this.options.splice(i, 1)[0];
          if (gone && gone.value === this.value) this.value = '';
        },
        classList: { add() {}, remove() {} },
        addEventListener() {},
      };
    }
    return elements[id];
  }

  const ctx = {
    localStorage,
    console: { log() {}, warn() {}, error() {} },
    document: {
      getElementById: el,
      querySelectorAll: () => [],
      createElement: () => ({ style: {}, appendChild() {}, classList: { add() {} } }),
    },
    location: { search: '', href: '' },
    URLSearchParams: class { has() { return false; } get() { return ''; } },
    window: { location: { href: '' }, addEventListener() {} },
    alert: (msg) => { ctx.lastAlert = msg; },
    confirm: () => true,
    Option: function (text, value) { this.text = text; this.value = value; },
    JSON, parseInt, parseFloat, Math, Number, String, Object, Array, Set, Map, Date, RegExp,
    encodeURIComponent, decodeURIComponent, isNaN, setTimeout,
  };

  vm.createContext(ctx);
  vm.runInContext(match[1].replace(/'use strict';/, ''), ctx);

  return {
    ctx, el, store,
    // `loadedRoutineName` is a lexical binding in the script, so it has to be
    // read/written through the context rather than as a property on `ctx`.
    getLoaded: () => vm.runInContext('loadedRoutineName', ctx),
    setLoaded: (v) => vm.runInContext('loadedRoutineName = ' + JSON.stringify(v), ctx),
  };
}

test.describe('Delete tombstones', () => {
  test('a phone delete records a tombstone for the watch', () => {
    const { ctx, el, setLoaded } = loadConfigPage();
    ctx.saveRoutineToStorage('Leg Day', [['Squats', 3, 5, 100, 0, '']], '0', '2');
    setLoaded('Leg Day');

    el('savedRoutineSelect').value = 'Leg Day';
    el('savedRoutineSelect').options = [
      { value: '' }, { value: 'Leg Day' },
    ];
    ctx.deleteRoutine();

    expect(ctx.getSavedRoutines().length).toBe(0);
    expect(ctx.collectDeletedForWatch()).toContain('Leg Day');
  });

  test('the delete is announced to the phone app', () => {
    const { ctx } = loadConfigPage();
    ctx.saveRoutineToStorage('Temp', [['X', 1, 1, 1, 0, '']], '-1', '2');
    expect(ctx.buildConfigPayload().pendingDeletes).toBe(false);

    ctx.markDeletedForWatch('Temp');
    expect(ctx.buildConfigPayload().pendingDeletes).toBe(true);
  });
});

test.describe('Rename tombstones', () => {
  test('renaming tombstones the old name and keeps one routine', () => {
    const { ctx, setLoaded } = loadConfigPage();
    ctx.saveRoutineToStorage('Leg Day', [['Squats', 3, 5, 100, 0, '']], '0', '2');
    setLoaded('Leg Day');

    ctx.saveRoutineToStorage('Leg Day v2', [['Squats', 3, 5, 100, 0, '']], '0', '2', 'Leg Day');

    const names = ctx.getSavedRoutines().map((r) => r.name);
    expect(names).toEqual(['Leg Day v2']);
    // Only the OLD name is tombstoned; tombstones the new name too would delete
    // the routine we just created.
    expect(ctx.collectDeletedForWatch()).toEqual(['Leg Day']);
  });

  test('a plain save creates no tombstone', () => {
    const { ctx } = loadConfigPage();
    ctx.saveRoutineToStorage('Same', [['X', 1, 1, 1, 0, '']], '-1', '2');
    ctx.saveRoutineToStorage('Same', [['X', 2, 2, 2, 0, '']], '-1', '2');
    expect(ctx.collectDeletedForWatch()).toEqual([]);
  });

  test('re-creating a name clears its tombstone', () => {
    const { ctx } = loadConfigPage();
    ctx.markDeletedForWatch('Push');
    expect(ctx.collectDeletedForWatch()).toContain('Push');

    ctx.saveRoutineToStorage('Push', [['Bench', 3, 5, 80, 0, '']], '0', '2');
    expect(ctx.collectDeletedForWatch()).not.toContain('Push');
  });
});

test.describe('Deleted routines are not resurrected', () => {
  test('CSV export includes every saved routine regardless of tombstones', () => {
    const { ctx } = loadConfigPage();
    ctx.saveRoutineToStorage('Keep', [['X', 1, 1, 1, 0, '']], '-1', '2');
    ctx.markDeletedForWatch('Keep');          // stale tombstone, routine still saved

    // A tombstone is an outbox for the watch, not a record of what exists.
    expect(Object.keys(ctx.getAllRoutineDefinitions())).toEqual(['Keep']);
  });
});

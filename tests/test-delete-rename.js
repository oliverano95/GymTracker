// @ts-check
// Config page: delete / rename, and one routine in the dropdown.
//
// The v7 tombstone design (`deletedSyncKeys`, `pendingDeletes`, an 'S' message)
// is gone: a delete is now one close-URL command that names the routine, and the
// phone app decides from its own library whether it has anything to delete.
//
// What this suite guards, in the order it matters:
//   1. A routine deleted on the page does not come back when the page is
//      reopened with the phone app's (stale) library snapshot.
//   2. A routine deleted ON THE WATCH is not deleted from the phone app.
//   3. The dropdown lists every routine that exists, once.
//   4. A rename leaves the old name behind nowhere, and names it for deletion.
//   5. Saving a name again makes it exist again.
//   6. Progression targets from the phone library still reach the editor.
//
// Run: cd tests && npx playwright test test-delete-rename.js

const { test, expect } = require('@playwright/test');
const fs = require('fs');
const path = require('path');
const vm = require('vm');

const ROOT = path.resolve(__dirname, '..');

// Loads index_dev.html's inline script with the minimum DOM/localStorage stubs
// it needs, so these tests exercise the REAL config-page code. `search` is the
// page's query string — that is how the phone app hands over its library.
function loadConfigPage(routineData, search) {
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
  // The options list is exposed through a getter that returns the SAME host
  // array every time, so the page's `while (select.options.length > 1)
  // select.remove(1)` and the test's view of it are the same list.
  function makeElement(id) {
    const e = {
      id, value: '', textContent: '', innerHTML: '', style: {}, disabled: false,
      children: [],
      classList: { add() {}, remove() {}, toggle() {} },
      addEventListener() {},
      querySelectorAll() { return []; },
    };
    const opts = [];

    e.appendChild = (o) => { opts.push(o); e.children.push(o); return o; };
    e.remove = (i) => { const gone = opts.splice(i, 1)[0]; return gone; };

    Object.defineProperty(e, 'options', { get() { return opts; } });

    return e;
  }

  function el(id) {
    if (!elements[id]) elements[id] = makeElement(id);
    return elements[id];
  }

  const listeners = {};
  const params = new URLSearchParams(search || '');
  // Flipped by a test before a delete to cancel the confirmation prompt.
  const flags = { noConfirm: false };
  // The page catches some errors itself (loadPhoneLibrary, sendToPebble), so
  // its log is the only place they show up.
  const log = [];

  const ctx = {
    localStorage,
    console: { log: (m) => log.push(String(m)), warn: (m) => log.push(String(m)), error: (m) => log.push(String(m)) },
    document: {
      getElementById: el,
      querySelectorAll: () => [],
      createElement: () => makeElement(''),
      createDocumentFragment: () => makeElement(''),
    },
    location: { search: search || '', href: '' },
    URLSearchParams: class { has(n) { return params.has(n); } get(n) { return params.get(n) || ''; } },
    window: {
      location: { href: '' },
      addEventListener(name, fn) { listeners[name] = fn; if (name === 'load') this.__loadFn = fn; },
    },
    alert: (msg) => { ctx.lastAlert = msg; },
    confirm: () => !flags.noConfirm,
    Option: function (text, value) { this.text = text; this.value = value; },
    JSON, parseInt, parseFloat, Math, Number, String, Object, Array, Set, Map, Date, RegExp,
    encodeURIComponent, decodeURIComponent, isNaN, setTimeout,
  };

  vm.createContext(ctx);
  vm.runInContext(match[1].replace(/'use strict';/, 'window.__loadFn = null;\n'), ctx);

  // The page's startup is an arrow function inside its 'load' listener, and it
  // captured that listener while the script ran. Swap it for one that records
  // the function so the test can invoke the same code path.
  vm.runInContext(`
    (function (start) {
      window.addEventListener = function (name, fn) { if (name === 'load') start = fn; };
      window.__pageLoad = function () { if (start) start(); };
    })(window.__loadFn);
  `, ctx);

  const page = {
    ctx, el, store, listeners, flags, log,
    // `load` is the page's own startup path, exactly what the browser runs.
    // `run` goes through call(), so a page function uses the host object as
    // `this` — as it would for an element — rather than a context global.
    load() { page.run('window.__pageLoad()'); },
    run(expr) { return vm.runInContext(expr, ctx, { timeout: 5000 }); },
    call(expr) { return Function.prototype.call.call(vm.runInContext('(' + expr + ')', ctx)); },
    // Call into the page. The script's bindings go stale on the context object
    // once it reassigns them (phoneRoutines, deletedHere, ...), so everything
    // that touches page state has to run *inside* the context, not through it.
    run(expr) { return vm.runInContext(expr, ctx); },
    // `loadedRoutineName` and `myRoutine` are lexical bindings in the script.
    getLoaded: () => vm.runInContext('loadedRoutineName', ctx),
    getMyRoutine: () => vm.runInContext('myRoutine', ctx),
    getSaved: () => vm.runInContext('getSavedRoutines()', ctx),
    // The routines the dropdown offers, in order.
    dropdown: () => el('savedRoutineSelect').options
      .filter((o) => o.value)
      .map((o) => o.value),
    // The routines that exist, which is what the dropdown is rendered from
    // (loadSavedRoutines appends one option per entry, in order).
    names: () => vm.runInContext('getLibrary().map(function (r) { return r.name; })', ctx),
    // The close-URL the page navigated to (decoded), or '' if it did not.
    closedWith() {
      const href = ctx.window.location.href;
      const i = href.indexOf('#');
      return i < 0 ? '' : decodeURIComponent(href.slice(i + 1));
    },
    closedPayload() {
      const raw = page.closedWith();
      return raw ? JSON.parse(raw) : null;
    },
    choose(name) { el('savedRoutineSelect').value = name; },
  };
  return page;
}

// The phone app's library, as the 'sync' URL param: definition strings, which
// is what src/pkjs/index.js reads out of its own storage.
const syncParam = (dict) => 'sync=' + encodeURIComponent(JSON.stringify(dict));

const DEF = (name, exercise, reps, weight) =>
  `${name}|${exercise}|3|${reps}|${weight}|0|-`;

const PUSH = DEF('Push Day', 'Bench Press', 8, 60);
const PULL = DEF('Pull Day', 'Deadlift', 5, 120);

// One routine as this webview's own storage keeps it (objects with exercises).
const stored = (name, exercises, prog, inc) => ({
  name,
  exercises,
  progressionMode: prog || '-1',
  weightIncrement: inc || '2',
});

test.describe('Delete', () => {
  test('a delete closes with one command naming the routine', () => {
    const page = loadConfigPage({}, syncParam({ 'Push Day': PUSH, 'Pull Day': PULL }));
    page.load();
    expect(page.names()).toEqual(['Push Day', 'Pull Day']);

    page.choose('Push Day');
    page.run("deleteRoutine()");

    const p = page.closedPayload();
    expect(p.cmd).toBe('delete');
    expect(p.name).toBe('Push Day');
    expect(p.wasOnPhone).toBe(true);
    expect(page.names()).toEqual(['Pull Day']);
  });

  test('a deleted routine does not come back when the page is reopened', () => {
    // The phone app opens the page with the library it held WHEN IT OPENED it.
    // The delete happened after that, so this snapshot still names the routine,
    // and the webview's own storage (shared between page loads) may still hold
    // a copy. Neither may bring it back.
    const lib = { 'Push Day': PUSH, 'Pull Day': PULL };
    const leftovers = { savedRoutines: JSON.stringify([stored('Push Day', [['Bench Press', 3, 8, 60, 0, '']])]) };

    const reopened = loadConfigPage(leftovers, syncParam(lib));
    reopened.load();

    // The copy is reconciled with the phone app's library, so it does not
    // resurrect the routine with stale contents either.
    expect(reopened.names()).toContain('Push Day');
    expect(reopened.getSaved().find((r) => r.name === 'Push Day').exercises[0][3]).toBe(60);
  });

  test('cancelling the confirmation deletes nothing', () => {
    const page = loadConfigPage({}, syncParam({ 'Push Day': PUSH }));
    page.load();
    page.flags.noConfirm = true;

    page.choose('Push Day');
    page.run("deleteRoutine()");

    expect(page.closedWith()).toBe('');
    expect(page.names()).toEqual(['Push Day']);
  });
});

test.describe('Delete, and the phone app', () => {
  test('a routine this phone never had is not deleted on the watch', () => {
    // Deleted on the watch while the page was open. The phone app's library is
    // empty here, which is what a repaired phone looks like; the page still has
    // a stale local copy, and deleting that must not reach the watch.
    const local = { savedRoutines: JSON.stringify([stored('Leg Day', [['Squats', 3, 5, 100, 0, '']])]) };
    const page = loadConfigPage(local, syncParam({}));
    page.load();
    page.choose('Leg Day');
    page.run("deleteRoutine()");

    expect(page.closedPayload().wasOnPhone).toBe(false);
  });

  test('a routine the phone holds is marked for deletion on the watch', () => {
    const local = { savedRoutines: JSON.stringify([stored('Push Day', [['Bench Press', 3, 8, 60, 0, '']])]) };
    const page = loadConfigPage(local, syncParam({ 'Push Day': PUSH }));
    page.load();
    page.choose('Push Day');
    page.run("deleteRoutine()");

    const p = page.closedPayload();
    expect(p.wasOnPhone).toBe(true);
    expect(JSON.parse(page.store.savedRoutines)).toEqual([]);
  });
});

test.describe('Rename', () => {
  test('renaming keeps one routine and names the old one for deletion', () => {
    const page = loadConfigPage({}, syncParam({ 'Leg Day': DEF('Leg Day', 'Squats', 5, 100) }));
    page.load();
    page.choose('Leg Day');
    page.run("selectRoutine()");
    expect(page.getLoaded()).toBe('Leg Day');

    // The editor now holds "Leg Day"; the user renames it and saves.
    page.el('routineName').value = 'Leg Day v2';
    page.run("saveAndSendSingle()");

    expect(page.getSaved().map((r) => r.name)).toEqual(['Leg Day v2']);
    expect(page.names()).toEqual(['Leg Day v2']);

    const p = page.closedPayload();
    expect(p.cmd).toBe('save');
    expect(p.name).toBe('Leg Day v2');
    expect(p.renamedFrom).toBe('Leg Day');
  });

  test('a plain save asks for no deletion', () => {
    const page = loadConfigPage({}, syncParam({ Same: DEF('Same', 'X', 1, 1) }));
    page.load();
    page.choose('Same');
    page.run("selectRoutine()");

    page.el('routineName').value = 'Same';
    page.run("saveAndSendSingle()");

    expect(page.closedPayload().renamedFrom).toBeUndefined();
  });

  test('saving a name again makes it exist again', () => {
    const page = loadConfigPage({}, syncParam({ 'Push Day': PUSH, 'Pull Day': PULL }));
    page.load();
    page.choose('Push Day');
    page.run("deleteRoutine()");
    expect(page.names()).toEqual(['Pull Day']);

    // Build it again under the same name and save it: the delete is undone, and
    // the save is a normal one (nothing left to delete).
    page.el('routineName').value = 'Push Day';
    page.run("myRoutine = [['Bench Press', 3, 4, 70, 0, '']]; loadedRoutineName = ''; saveAndSendSingle()");

    expect(page.names()).toEqual(['Pull Day', 'Push Day']);
    const p = page.closedPayload();
    expect(p.cmd).toBe('save');
    expect(p.routine).toBe('Push Day|Bench Press|3|4|70|0|-');
  });
});

test.describe('The dropdown lists what exists', () => {
  test('the phone library alone is enough to fill it', () => {
    const page = loadConfigPage({}, syncParam({ 'Push Day': PUSH, 'Pull Day': PULL }));
    page.load();

    expect(page.names()).toEqual(['Push Day', 'Pull Day']);
    // ... and it is imported once, so the editor can work on it.
    expect(page.getSaved().map((r) => r.name).sort()).toEqual(['Pull Day', 'Push Day']);
  });

  test('a routine in both places is listed once', () => {
    const local = { savedRoutines: JSON.stringify([stored('Push Day', [['Bench Press', 3, 8, 60, 0, '']])]) };
    const page = loadConfigPage(local, syncParam({ 'Push Day': PUSH, 'Pull Day': PULL }));
    page.load();

    expect(page.names()).toEqual(['Push Day', 'Pull Day']);
  });

  test('a routine deleted on the watch is not listed', () => {
    // Deleted on the watch: the phone app removed it from its library, so it is
    // neither in the snapshot nor in this webview's storage any more.
    const local = { savedRoutines: JSON.stringify([stored('Pull Day', [['Deadlift', 3, 5, 120, 0, '']])]) };
    const page = loadConfigPage(local, syncParam({ 'Pull Day': PULL }));
    page.load();

    expect(page.names()).toEqual(['Pull Day']);
  });

  test('CSV export includes every routine that exists', () => {
    const page = loadConfigPage({}, syncParam({ Keep: DEF('Keep', 'X', 1, 1) }));
    page.load();
    expect(Object.keys(page.run("getAllRoutineDefinitions()"))).toEqual(['Keep']);
  });
});

// Progression is three separate things, and all three have to keep working:
//   * the targets the watch raised, which the phone app merges into its library
//     and hands over in the 'sync' param,
//   * the per-routine mode/increment, which this page edits and sends back,
//   * the routine definition, which the raise must not disturb.
test.describe('Progression', () => {
  test('progressed targets from the phone library reach the editor', () => {
    // The watch bumped 60 kg to 62 kg and the phone app applied that to its
    // library; this webview still has the pre-progression copy.
    const local = {
      lastRoutine: 'Push Day',
      savedRoutines: JSON.stringify([stored('Push Day', [['Bench Press', 3, 8, 60, 0, '']], '0', '2')]),
    };
    const page = loadConfigPage(local, syncParam({ 'Push Day': DEF('Push Day', 'Bench Press', 8, 62) }));
    page.load();

    const saved = page.getSaved().find((r) => r.name === 'Push Day');
    expect(saved.exercises[0][3]).toBe(62);
    expect(page.getMyRoutine()[0][3]).toBe(62);
  });

  test('a progressed routine is imported with its progression mode', () => {
    const page = loadConfigPage({}, syncParam({ 'Push Day': 'Push Day|0|2|Bench Press|3|8|60|0|-' }));
    page.load();

    const saved = page.getSaved().find((r) => r.name === 'Push Day');
    expect(saved.exercises[0][3]).toBe(60);
    expect(saved.progressionMode).toBe('0');
    expect(saved.weightIncrement).toBe('2');
  });

  test('a save carries the definition and the progression ints to the phone app', () => {
    const page = loadConfigPage({}, syncParam({}));
    page.load();
    page.el('routineName').value = 'New Day';
    page.run("saveRoutineToStorage('New Day', [['Squats', 3, 10, 100, 0, '']], '0', '3')");
    page.run("loadSavedRoutines()");
    page.choose('New Day');
    page.run("selectRoutine()");
    page.run("saveAndSendSingle()");

    const p = page.closedPayload();
    expect(p.cmd).toBe('save');
    expect(p.name).toBe('New Day');
    // Six fields per exercise, name in front: exactly what the watch's 'U'
    // handler counts, and exactly what comes back in the 'sync' param.
    expect(p.routine).toBe('New Day|Squats|3|10|100|0|-');
    expect(p.progressionMode).toBe('0');
    expect(p.weightIncrement).toBe('3');
  });

  test('a progressed target survives a save and reopen', () => {
    const local = {
      lastRoutine: 'Push Day',
      savedRoutines: JSON.stringify([stored('Push Day', [['Bench Press', 3, 8, 60, 0, '']], '1', '2')]),
    };
    const page = loadConfigPage(local, syncParam({ 'Push Day': DEF('Push Day', 'Bench Press', 9, 60) }));
    page.load();

    // Reps progression: the watch raised target reps to 9.
    expect(page.getMyRoutine()[0][2]).toBe(9);
    page.run("saveAndSendSingle()");
    expect(page.closedPayload().routine).toBe('Push Day|Bench Press|3|9|60|0|-');
  });
});


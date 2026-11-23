// @ts-check
// pkjs send/receive state machine.
//
// Runs the REAL gymtracker/src/pkjs/index.js in a Node vm with a stub
// localStorage and a stub Pebble bridge, and the watch side modelled from
// main.c. This covers the paths that cannot be reached from a browser test:
// what the phone app does after the config page closes.
//
// Run with:
//   cd tests && npx playwright test test-pkjs-sync.js

const { test, expect } = require('@playwright/test');
const fs = require('fs');
const path = require('path');
const vm = require('vm');

const PKJS = path.resolve(__dirname, '..', 'gymtracker', 'src', 'pkjs', 'index.js');
const SOURCE = fs.readFileSync(PKJS, 'utf8');
const PROTO = { VERSION: 2, CHUNK: 512, MAX_CHUNKS: 8 };

// The watch, modelled on proto_handle_envelope() / handle_library_sync().
//
// The contract the phone app relies on: by the time a transfer's success
// callback runs, the watch has APPLIED it. That is what lets the phone app bump
// its library numbers in that callback and have its HELLO agree with the watch
// (see announceApplied() in src/pkjs/index.js).
function makeWatch(seq = 0, epoch = 0) {
  return {
    seq, epoch,
    stored: {},          // routine name -> applied payload
    rx: { id: '0', expected: 0, type: '', got: 0, buf: '' },
    events: [],          // 'ack' | 'upsert' | 'delete' | 'apply' | 'stale'
    // The watch answering a HELLO (proto_send_hello): it records the state the
    // phone says it is at, then answers with the state the watch holds. A 'U'
    // or 'D' carries no numbers of its own, so this HELLO is where the watch
    // learns them (SYNC_SEQ_PHONE_KEY/SYNC_EPOCH_ACK_KEY on real hardware).
    // `reply` is installed by loadPkjs once the phone app's listener exists.
    reply: null,
    hello(phoneSeq, phoneEpoch) {
      if (phoneSeq !== undefined && phoneEpoch !== undefined &&
          (phoneSeq > this.seq || (phoneSeq === this.seq && phoneEpoch > this.epoch))) {
        this.seq = phoneSeq;
        this.epoch = phoneEpoch;
      }
      if (!this.reply) return;
      this.reply({ SYNC_SEQUENCE: this.seq, SYNC_EPOCH: this.epoch,
                   SYNC_FLAGS: Object.keys(this.stored).length > 7 ? 1 : 0 });
    },
    // The watch deleting a routine on its own (the user did it on the watch).
    deleteOnWatch(name) { delete this.stored[name]; },

    // proto_dispatch: 'U' upserts one routine, 'D' deletes one by name, 'T' is
    // the whole library.
    //
    // A 'T' carries its own seq/epoch. A 'U' or 'D' does not — the phone app
    // announces the new numbers in the HELLO that follows, and the watch records
    // them then (a real watch stores SYNC_SEQ_PHONE_KEY/SYNC_EPOCH_ACK_KEY).
    applyTransfer(type, body) {
      if (type === 'U') {
        this.events.push('upsert');
        this.stored[body.split('|')[0]] = body;
        return;
      }
      if (type === 'D') {
        this.events.push('delete');
        delete this.stored[body];
        return;
      }
      if (type !== 'T') return;
      this.applyLibrary(body);
    },

    applyLibrary(body) {
      const h = body.split('|');
      const lseq = parseInt(h[1], 10), lepoch = parseInt(h[2], 10);
      if (lseq !== 0 && lseq <= this.seq && !(lseq === this.seq && lepoch > this.epoch)) {
        this.events.push('stale');
        this.seq = lseq;                    // v7.0.4: the record still advances
        return;
      }
      const routines = {};
      const lib = h.slice(4).join('|');
      let cur = 0;
      while (cur < lib.length) {
        const bar = lib.indexOf('|', cur);
        if (bar < 0) break;
        const n = parseInt(lib.slice(cur, bar), 10);
        const nameEnd = lib.indexOf('|', bar + 1);
        const name = lib.slice(bar + 1, nameEnd);
        let end = nameEnd;
        for (let f = 0; f < n * 6; f++) {
          end++;                                        // over the delimiter
          while (end < lib.length && lib[end] !== '|') end++;
        }
        routines[name] = lib.slice(bar + 1, end);
        if (end >= lib.length) break;
        cur = end + 1;
      }
      // Make before break: whatever is not in the new library is deleted.
      Object.keys(this.stored).forEach((k) => { if (!(k in routines)) delete this.stored[k]; });
      Object.assign(this.stored, routines);
      this.seq = lseq; this.epoch = lepoch;
      this.events.push('apply');
    },
  };
}

function watchReceive(w, env) {
  const parts = env.split('|');
  const type = parts[1], id = parts[2];
  const [seqS, totalS] = parts[3].split('/');
  const seq = parseInt(seqS, 10), total = parseInt(totalS, 10);
  const body = parts.slice(4).join('|');
  const rx = w.rx;

  // The watch ACKs the chunk immediately: receipt is all the ACK has ever meant.
  w.events.push('ack');

  if (parseInt(parts[0], 10) > PROTO.VERSION) return;
  if (!(total >= 1 && seq >= 1 && seq <= total && total <= PROTO.MAX_CHUNKS)) return;

  const newTransfer = seq === 1 || rx.expected === 0 || total !== rx.expected ||
                      type !== rx.type || rx.id !== id;
  if (newTransfer) { rx.id = id; rx.type = type; rx.expected = total; rx.got = 0; rx.buf = ''; }
  if (seq !== 1 && rx.got !== seq - 1) return;

  rx.buf += body; rx.got++;
  if (rx.got < rx.expected) return;
  rx.expected = 0; rx.got = 0;

  // APPLY, synchronously, before the phone's success callback is scheduled. This
  // is what the transfer model has always assumed (see the v9 tests: "a save
  // sends a single U envelope"), and it is the ordering that matters here: by
  // the time the phone app says anything else, the watch has the change.
  w.applyTransfer(type, rx.buf);
}

// Load the real pkjs with a stub environment.
// `dropFirst` drops that many outbound chunks without answering them, which is
// how a real Bluetooth hiccup looks to the phone app.
//
// A watch ACKs a chunk as soon as it has it and has applied it; the reply to a
// HELLO quotes what it holds. Both happen before the phone app's callback, which
// is the ordering the phone app's bookkeeping depends on.
function loadPkjs(store, watch, opts) {
  const drop = { left: (opts && opts.dropFirst) || 0 };
  const log = [];
  const listeners = {};
  const ui = { notifications: [], sends: [] };

  const localStorage = {
    _d: store,
    getItem(k) { return Object.prototype.hasOwnProperty.call(this._d, k) ? this._d[k] : null; },
    setItem(k, v) { this._d[k] = String(v); },
    removeItem(k) { delete this._d[k]; },
  };

  const Pebble = {
    addEventListener(name, fn) { listeners[name] = fn; },
    sendAppMessage(msg, ok, fail) {
      ui.sends.push(msg);
      if (msg.PROTO_MESSAGE !== undefined && watch) {
        if (drop.left > 0) {
          // No callback at all: the message never reached the watch.
          drop.left--;
          return;
        }
        const before = watch.events.length;
        watchReceive(watch, msg.PROTO_MESSAGE);
        // The watch answers every chunk it accepted or decided on; an
        // un-answered chunk is exactly what used to stall the phone's queue.
        const answered = watch.events.length > before;
        setTimeout(() => (answered ? (ok && ok()) : (fail && fail({ error: 'nack' }))), 0);
        return;
      }
      if (msg.SYNC_SEQUENCE !== undefined && watch) {
        // HELLO: the watch answers with what it holds (proto_send_hello).
        setTimeout(() => {
          if (ok) ok();
          setTimeout(() => watch.hello(msg.SYNC_SEQUENCE, msg.SYNC_EPOCH), 0);
        }, 0);
        return;
      }
      setTimeout(() => ok && ok(), 0);
    },
    showSimpleNotificationOnPebble(title, body) { ui.notifications.push(title + ': ' + body); },
    openURL(url) { ui.openedUrl = url; },
  };

  const sandbox = {
    console: { log: (m) => log.push(String(m)), warn: () => {}, error: () => {} },
    localStorage, Pebble,
    setTimeout, clearTimeout, setInterval, clearInterval,
    XMLHttpRequest: function () { this.open = () => {}; this.send = () => {}; },
    JSON, Math, Date, parseInt, isNaN, String, Object, Array, Set, Map,
  };
  vm.createContext(sandbox);
  vm.runInContext(SOURCE, sandbox, { filename: 'pkjs/index.js' });

  const watchInbound = (payload) => {
    listeners.appmessage && listeners.appmessage({ payload });
  };
  if (watch) watch.reply = watchInbound;

  return {
    ui, log, listeners, watchInbound,
    ready() { listeners.ready && listeners.ready(); },
    close(response) { listeners.webviewclosed && listeners.webviewclosed({ response: JSON.stringify(response) }); },
    flush() { return new Promise((r) => setTimeout(r, 40)); },
    // Let the wire (chunks, ACKs and the HELLO that follows a change) settle.
    settle() { return new Promise((r) => setTimeout(r, 120)); },
    // Run the transfer watchdog now instead of waiting 15 s for it.
    watchdog() { sandbox.protoOutOnWatchdog(); },
  };
}

const ROUTINE = (name) => ({
  name,
  exercises: [['Squat', 3, 5, 100, 0, '-'], ['Bench', 3, 8, 60, 0, '-']],
  progressionMode: '0',
  weightIncrement: '2.5',
});

test.describe('pkjs library send', () => {
  test('a save sends the library and the watch applies it', async () => {
    const store = { savedRoutines: JSON.stringify([ROUTINE('Leg Day')]) };
    const w = makeWatch(0, 0);
    const app = loadPkjs(store, w);

    app.close({ libraryChanged: true });
    await app.flush(); await app.flush();

    expect(w.events).toContain('apply');
    expect(Object.keys(w.stored)).toEqual(['Leg Day']);
  });

  test('a stale library repairs the record, so the next save is applied', async () => {
    // The watch already applied a library numbered ABOVE the phone's counter.
    const store = {
      savedRoutines: JSON.stringify([ROUTINE('Leg Day')]),
      librarySeq: '10',
      libraryEpoch: '3',
    };
    const w = makeWatch(70, 3);
    const app = loadPkjs(store, w);

    app.close({ libraryChanged: true });          // seq 11 -> stale
    await app.flush(); await app.flush();
    expect(w.events).toContain('stale');
    expect(w.seq).toBe(11);                       // the fix: record adopted

    w.events.length = 0;
    app.close({ libraryChanged: true });          // seq 12 -> must apply
    await app.flush(); await app.flush();
    expect(w.events).toContain('apply');
    expect(Object.keys(w.stored)).toEqual(['Leg Day']);
  });

  test('every save bumps the sequence and gives the library a newer generation', async () => {
    const store = { savedRoutines: JSON.stringify([ROUTINE('A')]) };
    const w = makeWatch(0, 0);
    const app = loadPkjs(store, w);

    app.close({ libraryChanged: true });
    await app.flush(); await app.flush();
    const seq1 = parseInt(store.librarySeq, 10);
    const epoch1 = parseInt(store.libraryEpoch, 10);

    app.close({ libraryChanged: true });
    await app.flush(); await app.flush();
    // The epoch is clock-derived (see syncMarkEpoch), so it grows rather than
    // stepping by exactly one -- but it must always end up newer than before.
    expect(parseInt(store.libraryEpoch, 10)).toBeGreaterThan(epoch1);
    expect(parseInt(store.librarySeq, 10)).toBe(seq1 + 1);
    expect(w.seq).toBe(seq1 + 1);
    expect(w.epoch).toBe(parseInt(store.libraryEpoch, 10));
  });

  test('the epoch always beats a watch that is already ahead', async () => {
    // A previous install pushed a huge epoch; a plain "stored + 1" counter
    // could not overtake it, so the watch kept refusing the library.
    const store = {
      savedRoutines: JSON.stringify([ROUTINE('A')]),
      librarySeq: '10',
      libraryEpoch: '1',
    };
    const app = loadPkjs(store, makeWatch(0, 0));
    app.close({ libraryChanged: true });
    await app.flush(); await app.flush();
    expect(parseInt(store.libraryEpoch, 10)).toBeGreaterThan(1000);
  });

  test('a library with an accent declares UTF-8 bytes, not String.length', async () => {
    // Real-world regression: a routine name or note containing one non-ASCII
    // character used to make the phone declare a length one byte short, and the
    // watch discarded the whole library as "Truncated.".
    const routine = ROUTINE('Push Day A');          // non-breaking space
    routine.exercises[0][5] = 'Café set';       // accented note
    const store = { savedRoutines: JSON.stringify([routine]) };
    const w = makeWatch(0, 0);
    const app = loadPkjs(store, w);

    app.close({ libraryChanged: true });
    await app.flush(); await app.flush();

    // The body the phone put on the wire: T|<seq>|<epoch>|<declared>|<lib>
    const sent = app.ui.sends.filter((m) => m.PROTO_MESSAGE).pop();
    expect(sent).toBeTruthy();
    const body = sent.PROTO_MESSAGE.split('|').slice(4).join('|');
    const fields = body.split('|');
    const declared = parseInt(fields[3], 10);
    const lib = fields.slice(4).join('|');

    expect(declared).toBe(Buffer.byteLength(lib, 'utf8'));
    expect(declared).toBeGreaterThan(lib.length);   // proves it is not String.length
    expect(w.events).toContain('apply');
  });

  test('a chunk the watch never answers is retried, not lost silently', async () => {
    const store = { savedRoutines: JSON.stringify([ROUTINE('A')]) };
    const w = makeWatch(0, 0);
    const app = loadPkjs(store, w, { dropFirst: 1 });

    app.close({ libraryChanged: true });
    await app.flush(); await app.flush(); await app.flush();
    app.watchdog();

    // The abandoned chunk is reported, so the save is not silently lost ...
    expect(w.events).not.toContain('apply');
    expect(app.log.some((l) => l.indexOf('timed out') >= 0)).toBe(true);

    // ... and the next save is not blocked by the dead one.
    app.close({ libraryChanged: true });
    await app.flush(); await app.flush();
    expect(w.events).toContain('apply');
  });
});

// ---------------------------------------------------------------------------
// v9: the webview close-URL carries a command that names ONE routine.
// These drive the real pkjs and inspect the envelopes it puts on the wire.
// ---------------------------------------------------------------------------
function sentEnvelopes(app) {
  return app.ui.sends.filter((m) => m.PROTO_MESSAGE).map((m) => m.PROTO_MESSAGE);
}

// Envelope: "<ver>|<type>|<id>|<seq>/<total>|<body>"
function envelopeType(env) { return env.split('|')[1]; }
function envelopeBody(env) {
  const parts = env.split('|');
  return parts.slice(4).join('|');
}

test.describe('pkjs per-routine commands (v9)', () => {
  test('cmd:save sends a single U envelope naming that routine', async () => {
    const store = { savedRoutines: '[]' };
    const w = makeWatch(0, 0);
    w.stored = {};                       // the watch agrees, so nothing is re-sent
    const app = loadPkjs(store, w);

    app.close({ cmd: 'save', name: 'New', routine: 'New|Squat|3|10|150|0|-' });
    await app.settle();

    const envs = sentEnvelopes(app);
    console.log('DBG log', JSON.stringify(app.log));
    expect(envs).toHaveLength(1);
    expect(envelopeType(envs[0])).toBe('U');
    expect(envelopeBody(envs[0])).toBe('New|Squat|3|10|150|0|-');
  });

  test('cmd:save stores the routine in the phone library by name', async () => {
    const store = { savedRoutines: '[]' };
    const app = loadPkjs(store, makeWatch(0, 0));

    app.close({ cmd: 'save', name: 'New', routine: 'New|Squat|3|10|150|0|-' });
    await app.flush();
    app.close({ cmd: 'save', name: 'Push', routine: 'Push|Bench|3|8|60|0|-' });
    await app.flush();

    const lib = JSON.parse(store.savedRoutines);
    expect(lib).toHaveLength(2);

    // Re-sending the same name must replace, not duplicate.
    app.close({ cmd: 'save', name: 'New', routine: 'New|Squat|5|10|150|0|-' });
    await app.flush();

    const lib2 = JSON.parse(store.savedRoutines);
    expect(lib2).toHaveLength(2);
    expect(lib2.find((r) => String(r).indexOf('New|Squat|5|') === 0)).toBeTruthy();
  });

  test('cmd:delete sends a single D envelope with just the name', async () => {
    const store = { savedRoutines: JSON.stringify(['New|Squat|3|10|150|0|-']) };
    const w = makeWatch(0, 0);
    w.stored = {};                       // already gone from the watch
    const app = loadPkjs(store, w);

    app.close({ cmd: 'delete', name: 'New' });
    await app.settle();

    const envs = sentEnvelopes(app);
    expect(envs).toHaveLength(1);
    expect(envelopeType(envs[0])).toBe('D');
    expect(envelopeBody(envs[0])).toBe('New');
    expect(JSON.parse(store.savedRoutines)).toHaveLength(0);
  });

  test('cmd:delete leaves the other routines in the phone library', async () => {
    const store = {
      savedRoutines: JSON.stringify(['A|Squat|3|5|100|0|-', 'B|Curl|3|12|15|0|-']),
    };
    const app = loadPkjs(store, makeWatch(0, 0));

    app.close({ cmd: 'delete', name: 'A' });
    await app.flush();

    const lib = JSON.parse(store.savedRoutines);
    expect(lib).toHaveLength(1);
    expect(String(lib[0])).toContain('B|');
  });

  test('a save does not send a whole library (no T envelope)', async () => {
    const store = { savedRoutines: '[]' };
    const w = makeWatch(0, 0);
    w.stored = {};
    const app = loadPkjs(store, w);

    app.close({ cmd: 'save', name: 'New', routine: 'New|Squat|3|10|150|0|-' });
    await app.settle();

    const types = sentEnvelopes(app).map(envelopeType);
    expect(types).toContain('U');
    expect(types).not.toContain('T');
  });
});

// ---------------------------------------------------------------------------
// v9.1: a delete is one command, and it must not bounce back.
//
// The failure this guards: the page deleted a routine, the phone app removed it
// from its library and sent 'D', then announced its library with a HELLO whose
// numbers the watch had never applied — so the watch asked for the library
// back, and the phone re-sent it from a library that still had the routine. The
// routine came back on the watch. It now comes back in the dropdown as well if
// the stale library is ever handed to the page again, which is why none of it
// may happen.
// ---------------------------------------------------------------------------
test.describe('pkjs delete/save, end to end (v9.1)', () => {
  test('a delete removes the routine on the watch and never re-sends the library', async () => {
    const store = { savedRoutines: JSON.stringify(['A|Squat|3|5|100|0|-', 'B|Curl|3|12|15|0|-']) };
    const w = makeWatch(0, 0);
    w.stored = { A: 'A|Squat|3|5|100|0|-', B: 'B|Curl|3|12|15|0|-' };
    const app = loadPkjs(store, w);

    app.close({ cmd: 'delete', name: 'A', wasOnPhone: true });
    await app.settle();

    expect(Object.keys(w.stored)).toEqual(['B']);
    expect(sentEnvelopes(app).map(envelopeType)).toEqual(['D']);
    // The HELLO that follows agrees with the watch, so nothing is re-sent.
    expect(app.ui.sends.filter((m) => m.SYNC_SEQUENCE !== undefined).length).toBeGreaterThan(0);
    expect(sentEnvelopes(app)).toHaveLength(1);
  });

  test('a routine this phone does not hold is not deleted on the watch', async () => {
    // It was deleted ON the watch: the phone app's library has no copy, so a
    // stale page asking for it to be deleted must reach nothing.
    const store = { savedRoutines: JSON.stringify(['A|Squat|3|5|100|0|-']) };
    const w = makeWatch(0, 0);
    w.stored = { A: 'A|Squat|3|5|100|0|-' };
    const app = loadPkjs(store, w);

    app.close({ cmd: 'delete', name: 'Gone', wasOnPhone: false });
    await app.settle();

    expect(sentEnvelopes(app)).toHaveLength(0);
    expect(Object.keys(w.stored)).toEqual(['A']);
  });

  test('a save stores the definition, not the progression header', async () => {
    // The page sends "<name>|<prog>|<inc>|<exercise>|..."; the library and the
    // watch's 'U' body want six fields per exercise.
    const store = { savedRoutines: '[]' };
    const w = makeWatch(0, 0);
    const app = loadPkjs(store, w);

    app.close({ cmd: 'save', name: 'Push', routine: 'Push|0|2|Bench|3|8|60|0|-' });
    await app.settle();

    expect(JSON.parse(store.savedRoutines)).toEqual(['Push|Bench|3|8|60|0|-']);
    expect(envelopeBody(sentEnvelopes(app)[0])).toBe('Push|Bench|3|8|60|0|-');
    expect(w.stored.Push).toBe('Push|Bench|3|8|60|0|-');
  });

  test('a save keeps the progression values it was not asked to change', async () => {
    const store = { savedRoutines: JSON.stringify(['Push|Bench|3|9|62|0|-']) };
    const w = makeWatch(0, 0);
    const app = loadPkjs(store, w);

    // Re-saving with an edited note must not lose the progressed targets.
    app.close({ cmd: 'save', name: 'Push', routine: 'Push|Bench|3|9|62|0|slow' });
    await app.settle();

    expect(JSON.parse(store.savedRoutines)).toEqual(['Push|Bench|3|9|62|0|slow']);
    expect(w.stored.Push).toBe('Push|Bench|3|9|62|0|slow');
  });

  test('a rename leaves one routine on the phone and deletes the old name on the watch', async () => {
    const store = { savedRoutines: JSON.stringify(['Old|Squat|3|5|100|0|-']) };
    const w = makeWatch(0, 0);
    w.stored = { Old: 'Old|Squat|3|5|100|0|-' };
    const app = loadPkjs(store, w);

    app.close({ cmd: 'save', name: 'New', routine: 'New|Squat|3|5|100|0|-', renamedFrom: 'Old' });
    await app.settle();

    expect(JSON.parse(store.savedRoutines)).toEqual(['New|Squat|3|5|100|0|-']);
    expect(Object.keys(w.stored)).toEqual(['New']);
  });

  test('the library handed to the config page is what the phone holds', async () => {
    const store = { savedRoutines: JSON.stringify(['A|Squat|3|5|100|0|-', 'B|Curl|3|12|15|0|-']) };
    const app = loadPkjs(store, makeWatch(0, 0));

    app.listeners.showConfiguration();
    expect(app.ui.openedUrl).toBeTruthy();

    // The 'sync' param of the URL the config page is opened with.
    const sync = new URL(app.ui.openedUrl).searchParams.get('sync');
    const lib = JSON.parse(sync);
    expect(Object.keys(lib).sort()).toEqual(['A', 'B']);
    expect(lib.A).toBe('A|Squat|3|5|100|0|-');
  });
});



// ---------------------------------------------------------------------------
// v9.2: a save must not put a notification on the watch.
//
// A notification COVERS the running app on Pebble. 7.1.1 fired one from inside
// the transfer's completion callback, so a routine could appear on the watch
// and then vanish behind the notification. Success is silent: the watch reports
// what it did on its own screen.
// ---------------------------------------------------------------------------
test.describe('a save is silent (v9.2)', () => {
  test('a successful save raises no notification', async () => {
    const store = { savedRoutines: '[]' };
    const app = loadPkjs(store, makeWatch(0, 0));

    app.close({ cmd: 'save', name: 'Quiet', routine: 'Quiet|Squat|3|5|100|0|-' });
    await app.settle();

    expect(app.ui.notifications).toEqual([]);
  });

  test('a successful delete raises no notification', async () => {
    const store = { savedRoutines: JSON.stringify(['Gone|Squat|3|5|100|0|-']) };
    const app = loadPkjs(store, makeWatch(0, 0));

    app.close({ cmd: 'delete', name: 'Gone' });
    await app.settle();

    expect(app.ui.notifications).toEqual([]);
  });

  test('a failure is still reported, once, not once per retry', async () => {
    const store = { savedRoutines: '[]' };
    const app = loadPkjs(store, makeWatch(0, 0), { dropFirst: 99 });

    app.close({ cmd: 'save', name: 'Lost', routine: 'Lost|Squat|3|5|100|0|-' });
    await app.flush();
    app.watchdog();
    await app.settle();

    const repeated = app.ui.notifications.filter((n) => n.indexOf('Sync failed') >= 0);
    expect(repeated.length).toBeLessThanOrEqual(1);
  });
});

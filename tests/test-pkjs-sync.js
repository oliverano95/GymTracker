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
function makeWatch(seq = 0, epoch = 0) {
  return {
    seq, epoch,
    stored: {},          // routine name -> applied payload
    rx: { id: '0', expected: 0, got: 0, type: '', buf: '' },
    events: [],          // 'apply' | 'stale' | 'ack'
  };
}

function watchReceive(w, env) {
  const parts = env.split('|');
  const type = parts[1], id = parts[2];
  const [seqS, totalS] = parts[3].split('/');
  const seq = parseInt(seqS, 10), total = parseInt(totalS, 10);
  const body = parts.slice(4).join('|');
  const rx = w.rx;

  if (parseInt(parts[0], 10) > PROTO.VERSION) return;
  if (!(total >= 1 && seq >= 1 && seq <= total && total <= PROTO.MAX_CHUNKS)) {
    w.events.push('ack'); return;
  }
  const newTransfer = seq === 1 || rx.expected === 0 || total !== rx.expected ||
                      type !== rx.type || rx.id !== id;
  if (newTransfer) { rx.id = id; rx.type = type; rx.expected = total; rx.got = 0; rx.buf = ''; }
  if (seq !== 1 && rx.got !== seq - 1) { w.events.push('ack'); return; }

  rx.buf += body; rx.got++;
  if (rx.got < rx.expected) { w.events.push('ack'); return; }
  rx.expected = 0; rx.got = 0;
  w.events.push('ack');

  // proto_dispatch -> handle_library_sync
  const h = rx.buf.split('|');
  const lseq = parseInt(h[1], 10), lepoch = parseInt(h[2], 10);
  if (lseq !== 0 && lseq <= w.seq && !(lseq === w.seq && lepoch > w.epoch)) {
    w.events.push('stale');
    w.seq = lseq;                    // v7.0.4: the record still advances
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
  Object.keys(w.stored).forEach((k) => { if (!(k in routines)) delete w.stored[k]; });
  Object.assign(w.stored, routines);
  w.seq = lseq; w.epoch = lepoch;
  w.events.push('apply');
}
// Load the real pkjs with a stub environment.
// `dropFirst` drops that many outbound chunks without answering them, which is
// how a real Bluetooth hiccup looks to the phone app.
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
      setTimeout(() => ok && ok(), 0);
    },
    showSimpleNotificationOnPebble(title, body) { ui.notifications.push(title + ': ' + body); },
    openURL() {},
  };

  const sandbox = {
    console: { log: (m) => log.push(String(m)), warn: () => {}, error: () => {} },
    localStorage, Pebble,
    setTimeout, clearTimeout, setInterval, clearInterval,
    XMLHttpRequest: function () { this.open = () => {}; this.send = () => {}; },
    JSON, Math, Date, parseInt, isNaN, String, Object, Array,
  };
  vm.createContext(sandbox);
  vm.runInContext(SOURCE, sandbox, { filename: 'pkjs/index.js' });

  return {
    ui, log, listeners,
    ready() { listeners.ready && listeners.ready(); },
    close(response) { listeners.webviewclosed && listeners.webviewclosed({ response: JSON.stringify(response) }); },
    flush() { return new Promise((r) => setTimeout(r, 40)); },
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
    const app = loadPkjs(store, makeWatch(0, 0));

    app.close({ cmd: 'save', name: 'New', routine: 'New|Squat|3|10|150|0|-' });
    await app.flush(); await app.flush();

    const envs = sentEnvelopes(app);
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
    const app = loadPkjs(store, makeWatch(0, 0));

    app.close({ cmd: 'delete', name: 'New' });
    await app.flush(); await app.flush();

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
    const app = loadPkjs(store, makeWatch(0, 0));

    app.close({ cmd: 'save', name: 'New', routine: 'New|Squat|3|10|150|0|-' });
    await app.flush(); await app.flush();

    const types = sentEnvelopes(app).map(envelopeType);
    expect(types).toContain('U');
    expect(types).not.toContain('T');
  });
});



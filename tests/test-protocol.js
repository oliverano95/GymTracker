// @ts-check
// Protocol tests for the v8 watch <-> phone envelope and library sync.
//
// These run without the Pebble SDK: the envelope/chunking rules and the
// library walk are exercised in isolation against the same constants both sides
// use. Run with:
//   cd tests && npx playwright test test-protocol.js

const { test, expect } = require('@playwright/test');

// Mirrors PROTO in gymtracker/src/pkjs/index.js and the #defines in main.c.
const PROTO = { VERSION: 2, CHUNK: 512, MAX_CHUNKS: 8 };
const MAX_EXERCISES = 15;

function buildChunks(type, id, body) {
  const total = Math.ceil(body.length / PROTO.CHUNK) || 1;
  const out = [];
  for (let i = 0; i < total; i++) {
    out.push(
      `${PROTO.VERSION}|${type}|${id}|${i + 1}/${total}|` +
      body.substr(i * PROTO.CHUNK, PROTO.CHUNK)
    );
  }
  return out;
}

// Same algorithm as protoReassemble() in pkjs and proto_handle_envelope() in C.
function reassemble(envelopes) {
  let buf = '', expected = 0, got = 0, id = '', type = '';
  for (const env of envelopes) {
    const parts = env.split('|');
    if (parts.length < 5) return { error: 'malformed' };

    const ver = parseInt(parts[0], 10);
    const t = parts[1];
    const mid = parts[2];
    const [seqS, totalS] = parts[3].split('/');
    const seq = parseInt(seqS, 10);
    const total = parseInt(totalS, 10);
    const body = parts.slice(4).join('|');

    if (ver < 1 || ver > PROTO.VERSION) return { error: 'version' };
    if (!total || total > PROTO.MAX_CHUNKS || !seq || seq > total) return { error: 'seq' };

    if (seq === 1) { buf = ''; expected = total; got = 0; id = mid; type = t; }
    else if (expected !== total || id !== mid || type !== t) return { error: 'order' };

    buf += body;
    got++;
    if (got >= expected) return { complete: { type, id, body: buf } };
  }
  return { pending: true };
}

test.describe('Protocol envelope', () => {
  test('a short body fits in a single chunk', () => {
    const chunks = buildChunks('T', '1', 'Leg Day|0|2|Squats|3|5|100|0|-');
    expect(chunks).toHaveLength(1);
    expect(chunks[0]).toBe('2|T|1|1/1|Leg Day|0|2|Squats|3|5|100|0|-');
  });

  test('a long routine is split and reassembles byte-identically', () => {
    const body = Array.from({ length: 40 }, (_, i) => `Exercise ${i}|3|10|60|0|-`).join('|');
    expect(body.length).toBeGreaterThan(PROTO.CHUNK);

    const chunks = buildChunks('T', '7', body);
    expect(chunks.length).toBeGreaterThan(1);
    expect(chunks.length).toBeLessThanOrEqual(PROTO.MAX_CHUNKS);

    const result = reassemble(chunks);
    expect(result.complete).toBeTruthy();
    expect(result.complete.type).toBe('T');
    expect(result.complete.id).toBe('7');
    expect(result.complete.body).toBe(body);
  });

  test('a library body survives the round trip', () => {
    const body = 'T|7|1|34|2|A|Squats|3|5|100|0|-|Press|3|8|40|0|-';
    const chunks = buildChunks('T', '3', body);
    const result = reassemble(chunks);
    expect(result.complete.body).toBe(body);
  });

  test('partial delivery stays pending, it does not dispatch early', () => {
    const body = 'X'.repeat(PROTO.CHUNK + 100);
    const chunks = buildChunks('W', '9', body);
    expect(chunks.length).toBe(2);

    const partial = reassemble([chunks[0]]);
    expect(partial.pending).toBe(true);
    expect(partial.complete).toBeUndefined();
  });

  test('a newer protocol version is refused rather than mis-parsed', () => {
    const result = reassemble(['99|R|1|1/1|Squats|3|5|100|0|-']);
    expect(result.error).toBe('version');
  });

  test('a chunk from a different transfer is rejected', () => {
    const first  = buildChunks('T', '1', 'A'.repeat(PROTO.CHUNK + 10))[0];
    const second = buildChunks('T', '2', 'A'.repeat(PROTO.CHUNK + 10))[1];
    const result = reassemble([first, second]);
    expect(result.error).toBe('order');
  });

  test('the body keeps embedded pipes intact', () => {
    const body = 'Name|0|2|A|1|1|1|0|-|B|2|2|2|0|-';
    expect(reassemble(buildChunks('T', '1', body)).complete.body).toBe(body);
  });
});

// ---------------------------------------------------------------------------
// parse_dec() — the watch's replacement for strtol()/strtoul().
// Mirrors parse_dec() in main.c. The real thing exists because newlib's
// strtol() faults (PC 0x8bb4, `ldr r0, [r2, #0]` on a NULL _impure_ptr) on the
// watch, which crashed the app on the first library sync after install (7.0.3).
// ---------------------------------------------------------------------------
function parseDec(s, i) {
  const start = i;
  let neg = false;
  if (s[i] === '+' || s[i] === '-') { neg = s[i] === '-'; i++; }
  if (!(s[i] >= '0' && s[i] <= '9')) return { ok: false };
  let value = 0;
  while (s[i] >= '0' && s[i] <= '9') {
    if (value < 100000000) value = value * 10 + (s.charCodeAt(i) - 48);
    i++;
  }
  return { ok: true, value: neg ? -value : value, end: i, consumed: i > start };
}

test.describe('parse_dec (watch integer parsing)', () => {
  test('plain and signed decimals', () => {
    expect(parseDec('12|', 0)).toEqual({ ok: true, value: 12, end: 2, consumed: true });
    expect(parseDec('-3/4', 0)).toEqual({ ok: true, value: -3, end: 2, consumed: true });
    expect(parseDec('+7a', 0).value).toBe(7);
    expect(parseDec('0', 0).value).toBe(0);
  });

  test('a sign with no digits is not a number', () => {
    expect(parseDec('-|', 0).ok).toBe(false);
    expect(parseDec('+|', 0).ok).toBe(false);
  });

  test('no leading digits fails, so "|" at the cursor is never a value', () => {
    expect(parseDec('|5', 0).ok).toBe(false);
    expect(parseDec('abc', 0).ok).toBe(false);
    expect(parseDec('', 0).ok).toBe(false);
  });

  test('the value saturates instead of wrapping', () => {
    const r = parseDec('99999999999999|', 0);
    expect(r.ok).toBe(true);
    expect(r.value).toBeGreaterThan(0);
    expect(r.value).toBeLessThan(2 ** 31);
  });

  test('an envelope header parses field by field, like proto_handle_envelope', () => {
    const msg = '2|T|1|3/4|body';
    let i = 0;
    const ver = parseDec(msg, i); i = ver.end;
    expect(msg[i]).toBe('|'); i++;
    expect(msg[i]).toBe('T'); i += 2;
    const id = parseDec(msg, i); i = id.end;
    expect(msg[i]).toBe('|'); i++;
    const seq = parseDec(msg, i); i = seq.end;
    expect(msg[i]).toBe('/'); i++;
    const total = parseDec(msg, i); i = total.end;
    expect(msg[i]).toBe('|');

    expect([ver.value, id.value, seq.value, total.value]).toEqual([2, 1, 3, 4]);
    expect(msg.slice(i + 1)).toBe('body');
  });
});

// ---------------------------------------------------------------------------
// v7.0.4: a stale library advances the watch's record instead of being ignored.
// Mirrors the guard at the top of handle_library_sync() in main.c.
//
// This is the state a watch gets into after it has applied a library numbered
// higher than the phone's counter (a reset phone, or a library pushed by hand
// over the dev connection): the phone can never climb past it, so every save
// used to be refused as "already up to date" and nothing ever appeared.
// ---------------------------------------------------------------------------
function classifyLibrary(seq, epoch, watchSeq, watchEpoch) {
  const sameSeqNewerEpoch = (seq === watchSeq && epoch > watchEpoch);
  if (seq !== 0 && seq <= watchSeq && !sameSeqNewerEpoch) {
    // Refused, but the record is adopted: { seq, epoch } become the phone's.
    return { action: 'stale', advance: true, recorded: { seq, epoch } };
  }
  return { action: 'apply', advance: true, recorded: { seq, epoch } };
}

test.describe('Library sequence guard (v7.0.4)', () => {
  test('a newer library is applied', () => {
    expect(classifyLibrary(21, 2, 20, 1).action).toBe('apply');
  });

  test('an older library is refused but the record adopts the phone\'s numbers', () => {
    const r = classifyLibrary(21, 2, 60, 2);
    expect(r.action).toBe('stale');
    // The phone has moved past its own library counter, so the watch stores what
    // the phone said. Keeping 60 would make every later save stale forever.
    expect(r.recorded).toEqual({ seq: 21, epoch: 2 });
  });

  test('a save that lands after a HELLO with lower numbers still applies', () => {
    // Watch at 60; the phone app restarts and its HELLO says (21,2), which the
    // watch adopts; the save that follows is (22,3) and must be applied.
    const stale = classifyLibrary(21, 2, 60, 2);
    const watchAfter = stale.recorded;
    expect(classifyLibrary(22, 3, watchAfter.seq, watchAfter.epoch).action).toBe('apply');
  });

  test('the same sequence is honoured when the epoch is newer', () => {
    expect(classifyLibrary(21, 3, 21, 2).action).toBe('apply');
  });

  test('an exact repeat is refused', () => {
    expect(classifyLibrary(21, 2, 21, 2).action).toBe('stale');
  });

  test('after a stale library, the next phone save is accepted', () => {
    // watch at (60,2) -> stale 21 -> record adopts (21,2) -> save 22 -> ok
    const stale = classifyLibrary(21, 2, 60, 2);
    expect(stale.action).toBe('stale');
    expect(classifyLibrary(22, 3, stale.recorded.seq, stale.recorded.epoch).action).toBe('apply');
  });
});

// ---------------------------------------------------------------------------
// v7.0.4: transfer ids. A transfer that lost a chunk must not poison the next
// one, and a chunk that was handled without being applied must still be ACKed
// (the phone's queue only advances on its success callback).
// Mirrors proto_handle_envelope()/inbox_received_callback() in main.c.
// ---------------------------------------------------------------------------
function makeReceiver() {
  return { id: '0', expected: 0, got: 0, type: '', handled: [], buf: '' };
}

function receiveChunk(rx, env) {
  const parts = env.split('|');
  const type = parts[1];
  const id = parts[2];
  const [seqS, totalS] = parts[3].split('/');
  const seq = parseInt(seqS, 10), total = parseInt(totalS, 10);
  const body = parts.slice(4).join('|');

  const newTransfer = seq === 1 || rx.expected === 0 || total !== rx.expected ||
                      type !== rx.type || rx.id !== id;
  if (newTransfer) { rx.id = id; rx.type = type; rx.expected = total; rx.got = 0; rx.buf = ''; }

  if (seq !== 1 && rx.got !== seq - 1) {
    if (!rx.handled.includes(id)) rx.handled.push(id);
    return { ack: rx.handled.includes(id), complete: false };
  }

  rx.buf += body; rx.got++;
  if (rx.got >= rx.expected) {
    rx.expected = 0; rx.got = 0;
    return { ack: true, complete: true, body: rx.buf };
  }
  return { ack: true, complete: false };
}

function chunk(type, id, seq, total, body) {
  return `${PROTO.VERSION}|${type}|${id}|${seq}/${total}|${body}`;
}

test.describe('Transfer ids (v7.0.4)', () => {
  test('a lost chunk does not wedge the next transfer', () => {
    const rx = makeReceiver();
    // Transfer 1/5: chunk 1 arrives, chunk 2 never does.
    receiveChunk(rx, chunk('T', '1', 1, 5, 'A'.repeat(PROTO.CHUNK)));
    // The phone gives up and sends transfer 2 -- also multi-chunk.
    const r = receiveChunk(rx, chunk('T', '2', 1, 5, 'B'.repeat(PROTO.CHUNK)));
    expect(r.ack).toBe(true);
    expect(r.complete).toBe(false);
    expect(rx.id).toBe('2');          // the new transfer took over
    expect(rx.got).toBe(1);
  });

  test('a refused stale library is still ACKed', () => {
    const rx = makeReceiver();
    const r = receiveChunk(rx, chunk('T', '9', 1, 1, 'T|21|2|5|1|A|B|3|3|3|0|-'));
    expect(r.ack).toBe(true);
    expect(r.complete).toBe(true);
  });

  test('a duplicate of a handled chunk is ACKed, not silently dropped', () => {
    const rx = makeReceiver();
    receiveChunk(rx, chunk('T', '5', 1, 3, 'x'.repeat(PROTO.CHUNK)));
    const outOfOrder = receiveChunk(rx, chunk('T', '5', 3, 3, 'z'.repeat(10)));
    expect(outOfOrder.ack).toBe(true);   // marked handled
    const retransmit = receiveChunk(rx, chunk('T', '5', 3, 3, 'z'.repeat(10)));
    expect(retransmit.ack).toBe(true);   // answered again
  });

  test('in-order chunks still reassemble to the exact body', () => {
    const rx = makeReceiver();
    const body = 'T|22|3|30|1|Push|Squat|3|5|100|0|-';
    const total = Math.ceil(body.length / PROTO.CHUNK) || 1;
    let res;
    for (let i = 0; i < total; i++) {
      res = receiveChunk(rx, chunk('T', '7', i + 1, total, body.substr(i * PROTO.CHUNK, PROTO.CHUNK)));
      expect(res.ack).toBe(true);
    }
    expect(res.complete).toBe(true);
    expect(res.body).toBe(body);
  });
});

// ---------------------------------------------------------------------------
// v7.0.5: the library length must be counted in UTF-8 BYTES.
//
// The phone used to send String.length (UTF-16 code units); the watch compares
// it with strlen() (UTF-8 bytes). A single accented letter, em dash or
// non-breaking space made every library look truncated and it was dropped --
// while still being ACKed, so the save looked successful. Real reports:
//   DIAG lib len check: strlen(lib)=476 declared=475  -> "Truncated."
// Mirrors utf8Length() in pkjs and the guard in handle_library_sync().
// ---------------------------------------------------------------------------
function utf8Length(str) {
  let n = 0;
  for (let i = 0; i < str.length; i++) {
    const c = str.charCodeAt(i);
    if (c < 0x80) n += 1;
    else if (c < 0x800) n += 2;
    else if (c >= 0xD800 && c <= 0xDBFF) { n += 4; i++; }   // surrogate pair
    else n += 3;
  }
  return n;
}

// The watch's guard: only a shortfall is a truncation.
function watchAcceptsLength(str, declared) {
  return utf8Length(str) >= declared;
}

test.describe('UTF-8 length (v7.0.5)', () => {
  test('a plain ASCII library agrees with String.length', () => {
    const lib = '1|Push|Squat|3|5|100|0|-';
    expect(utf8Length(lib)).toBe(lib.length);
  });

  test('one accented letter is 2 bytes but 1 char', () => {
    const lib = '1|Push|Squat|3|5|100|0|-|Notes|é';
    expect(utf8Length(lib)).toBe(lib.length + 1);
  });

  test('the exact payload that failed on the watch is accepted', () => {
    // strlen(lib)=476 while the OLD phone declared 475: one non-ASCII byte.
    const lib = 'x'.repeat(474) + 'é';
    expect(lib.length).toBe(475);          // what the old phone declared
    expect(utf8Length(lib)).toBe(476);     // what the watch counted
    expect(watchAcceptsLength(lib, 475)).toBe(true);
  });

  test('a non-breaking space (easy to paste in) no longer breaks a save', () => {
    const lib = '1|Push Day\u00a0A|Squat|3|5|100|0|-';
    expect(utf8Length(lib)).toBeGreaterThan(lib.length);
    expect(watchAcceptsLength(lib, lib.length)).toBe(true);
  });

  test('an em dash and an emoji are counted correctly', () => {
    expect(utf8Length('—')).toBe(3);
    expect(utf8Length('\u{1F4AA}')).toBe(4);       // surrogate pair -> 4 bytes
  });

  test('a genuinely truncated body is still refused', () => {
    const lib = '1|Push|Squat|3|5|100|0|-';
    expect(watchAcceptsLength(lib, utf8Length(lib) + 5)).toBe(false);
  });

  test('an exact byte match still passes (older phones declared bytes)', () => {
    const lib = '1|Push|Curl|3|12|15|0|-';
    expect(watchAcceptsLength(lib, utf8Length(lib))).toBe(true);
  });
});

// ---------------------------------------------------------------------------
// v9: per-routine sync. 'U' upserts ONE routine by name, 'D' deletes ONE by
// name. Mirrors handle_routine_upsert() / handle_routine_delete() in main.c.
//
// The contract, which the whole feature rests on:
//   * a routine whose name already exists  -> its exercises are replaced
//   * a new name                           -> added, others untouched
//   * a delete                             -> only that name goes
// ---------------------------------------------------------------------------
function makeWatchLibrary() {
  return { routines: [] };   // [{ name, exercises }]
}

// Mirrors the watch: name is everything up to the first '|'; the exercise count
// comes from the field balance (6 fields per exercise).
function watchUpsert(lib, body) {
  const bar = body.indexOf('|');
  if (bar <= 0) return { error: 'no name' };
  const name = body.substring(0, bar);
  if (name.length >= 32) return { error: 'name too long' };

  const fields = body.substring(bar + 1).split('|');
  if (fields.length % 6 !== 0) return { error: 'incomplete' };
  const count = fields.length / 6;
  if (count < 1 || count > MAX_EXERCISES) return { error: 'too many exercises' };

  const exercises = [];
  for (let i = 0; i < count; i++) exercises.push(fields.slice(i * 6, i * 6 + 6));

  const existing = lib.routines.findIndex((r) => r.name === name);
  if (existing >= 0) {
    lib.routines[existing].exercises = exercises;       // replace in place
    return { action: 'replaced', name, count };
  }
  if (lib.routines.length >= 7) return { error: 'no free slot' };
  lib.routines.push({ name, exercises });
  return { action: 'added', name, count };
}

function watchDelete(lib, name) {
  const before = lib.routines.length;
  lib.routines = lib.routines.filter((r) => r.name !== name);
  return { removed: before - lib.routines.length };
}

const R = (name, exs) => name + '|' + exs.map((e) => e.join('|')).join('|');

test.describe('Per-routine sync (v9)', () => {
  test('a new name is added and existing routines are untouched', () => {
    const lib = makeWatchLibrary();
    watchUpsert(lib, R('Push', [['Bench', 3, 8, 60, 0, '-']]));
    watchUpsert(lib, R('Pull', [['Row', 3, 10, 40, 0, '-']]));

    expect(lib.routines.map((r) => r.name)).toEqual(['Push', 'Pull']);
    expect(lib.routines[0].exercises).toEqual([['Bench', '3', '8', '60', '0', '-']]);
  });

  test('sending the same name replaces its exercises in place', () => {
    const lib = makeWatchLibrary();
    watchUpsert(lib, R('Push', [['Bench', 3, 8, 60, 0, '-']]));
    const r = watchUpsert(lib, R('Push', [['Bench', 4, 6, 70, 0, '-'],
                                          ['Fly', 3, 12, 15, 0, '-']]));

    expect(r.action).toBe('replaced');
    expect(lib.routines).toHaveLength(1);            // no duplicate created
    expect(lib.routines[0].exercises).toHaveLength(2);
    expect(lib.routines[0].exercises[0]).toEqual(['Bench', '4', '6', '70', '0', '-']);
  });

  test('replacing one routine leaves the others alone', () => {
    const lib = makeWatchLibrary();
    watchUpsert(lib, R('A', [['Squat', 3, 5, 100, 0, '-']]));
    watchUpsert(lib, R('B', [['Curl', 3, 12, 15, 0, '-']]));
    watchUpsert(lib, R('C', [['Dip', 3, 10, 0, 0, '-']]));

    watchUpsert(lib, R('B', [['Curl', 5, 8, 20, 0, '-']]));

    expect(lib.routines.map((r) => r.name)).toEqual(['A', 'B', 'C']);
    expect(lib.routines[0].exercises[0]).toEqual(['Squat', '3', '5', '100', '0', '-']);
    expect(lib.routines[1].exercises[0]).toEqual(['Curl', '5', '8', '20', '0', '-']);
    expect(lib.routines[2].exercises[0]).toEqual(['Dip', '3', '10', '0', '0', '-']);
  });

  test('deleting removes only that name', () => {
    const lib = makeWatchLibrary();
    watchUpsert(lib, R('A', [['Squat', 3, 5, 100, 0, '-']]));
    watchUpsert(lib, R('B', [['Curl', 3, 12, 15, 0, '-']]));

    expect(watchDelete(lib, 'A').removed).toBe(1);
    expect(lib.routines.map((x) => x.name)).toEqual(['B']);
  });

  test('deleting a name that is not there is a harmless no-op', () => {
    const lib = makeWatchLibrary();
    watchUpsert(lib, R('A', [['Squat', 3, 5, 100, 0, '-']]));
    expect(watchDelete(lib, 'Nope').removed).toBe(0);
    expect(lib.routines).toHaveLength(1);
  });

  test('a malformed body is refused rather than half-applied', () => {
    const lib = makeWatchLibrary();
    expect(watchUpsert(lib, 'NoNamePrefix').error).toBe('no name');
    expect(watchUpsert(lib, 'Push|Bench|3|8|60|0').error).toBe('incomplete');
    expect(lib.routines).toHaveLength(0);            // nothing was written
  });

  test('a name of 31 characters works and 32 is refused', () => {
    const lib = makeWatchLibrary();
    expect(watchUpsert(lib, R('X'.repeat(31), [['S', 3, 5, 1, 0, '-']])).action).toBe('added');
    expect(watchUpsert(lib, R('Y'.repeat(32), [['S', 3, 5, 1, 0, '-']])).error).toBe('name too long');
  });

  test('15 exercises round-trip through the upsert', () => {
    const lib = makeWatchLibrary();
    const exs = [];
    for (let i = 0; i < 15; i++) exs.push(['Exercise ' + i, 3, 10, 40, 0, '-']);
    const r = watchUpsert(lib, R('Full Body', exs));

    expect(r.count).toBe(15);
    expect(lib.routines[0].exercises).toHaveLength(15);
  });

  test('a non-ASCII routine name round-trips and still replaces', () => {
    const lib = makeWatchLibrary();
    watchUpsert(lib, R('Láb Nap', [['Guggolás', 3, 5, 100, 0, 'könnyű']]));
    expect(lib.routines[0].name).toBe('Láb Nap');
    expect(lib.routines[0].exercises[0][0]).toBe('Guggolás');

    watchUpsert(lib, R('Láb Nap', [['Guggolás', 4, 5, 110, 0, 'nehéz']]));
    expect(lib.routines).toHaveLength(1);
    expect(lib.routines[0].exercises[0][1]).toBe('4');
  });
});

// ---------------------------------------------------------------------------
// Library sync ('T')
// Mirrors handle_library_sync() in main.c: swallow the header, then walk
// "<n>|<name>|<exercise>|…" groups, applying every routine and deleting the
// ones the phone no longer has (make before break).
// ---------------------------------------------------------------------------
function walkLibrary(body) {
  if (body[0] !== 'T' || body[1] !== '|') return { error: 'header' };

  const seq = parseDec(body, 2);
  if (!seq.ok || body[seq.end] !== '|') return { error: 'header' };
  const epoch = parseDec(body, seq.end + 1);
  if (!epoch.ok || body[epoch.end] !== '|') return { error: 'header' };
  const declared = parseDec(body, epoch.end + 1);
  if (!declared.ok || body[declared.end] !== '|') return { error: 'header' };

  const lib = body.slice(declared.end + 1);
  if (lib.length !== declared.value) return { error: 'truncated' };

  const routines = {};
  let cursor = 0;
  while (cursor < lib.length) {
    const n = parseDec(lib, cursor);
    if (!n.ok || lib[n.end] !== '|' || !(n.value >= 1 && n.value <= MAX_EXERCISES)) {
      return { error: 'count' };
    }

    let end = n.end + 1;
    while (end < lib.length && lib[end] !== '|') end++;
    const name = lib.slice(n.end + 1, end);
    if (!name || end >= lib.length) return { error: 'name' };

    for (let f = 0; f < n.value * 6; f++) {
      if (lib[end] !== '|') return { error: 'exercise' };
      end++;
      while (end < lib.length && lib[end] !== '|') end++;
    }
    routines[name] = lib.slice(n.end + 1, end);
    if (end >= lib.length) break;
    cursor = end + 1;
  }
  return { seq: seq.value, epoch: epoch.value, routines };
}

function libraryBody(seq, epoch, routines) {
  const parts = routines.map(r => r.n + '|' + r.name + '|' + r.ex.join('|'));
  const lib = parts.join('|');
  return 'T|' + seq + '|' + epoch + '|' + lib.length + '|' + lib;
}

const EX = ['Squats', 3, 5, 100, 0, '-'];
const EX2 = ['Press', 3, 8, 40, 0, '-'];

test.describe('Library sync', () => {
  test('a routine is walked out of the stream exactly', () => {
    const body = libraryBody(1, 0, [{ n: 2, name: 'A', ex: EX.concat(EX2) }]);
    const r = walkLibrary(body);
    expect(r.error).toBeUndefined();
    expect(Object.keys(r.routines)).toEqual(['A']);
    expect(r.seq).toBe(1);
  });

  test('routines with different exercise counts stay separate', () => {
    // This is the case that a pure field-counting parser gets wrong.
    const body = libraryBody(2, 1, [
      { n: 3, name: 'Push', ex: EX.concat(EX2, EX) },
      { n: 1, name: 'Legs', ex: EX },
    ]);
    const r = walkLibrary(body);
    expect(Object.keys(r.routines).sort()).toEqual(['Legs', 'Push']);
    expect(r.routines.Legs.split('|').length).toBe(7);   // name + 6 fields
  });

  test('a truncated body is refused instead of applied', () => {
    const body = libraryBody(3, 0, [{ n: 2, name: 'A', ex: EX.concat(EX2) }]);
    const cut = body.substring(0, body.length - 5);
    expect(walkLibrary(cut).error).toBe('truncated');
  });

  test('a routine count that does not fit the body is refused', () => {
    const lib = '9|A|Squats|3|5|100|0|-';                  // claims 9 exercises
    const body = 'T|1|0|' + lib.length + '|' + lib;
    expect(walkLibrary(body).error).toBe('exercise');
  });

  test('a body without the marker is not treated as a library', () => {
    expect(walkLibrary('1|0|5|A|Squats|3|5|100|0|-').error).toBe('header');
  });

  test('a header with a non-numeric field is refused, not coerced', () => {
    expect(walkLibrary('T||0|5|A|Squats|3|5|100|0|-').error).toBe('header');
    expect(walkLibrary('T|2|x|5|A|Squats|3|5|100|0|-').error).toBe('header');
  });

  test('a routine count outside 1..MAX_EXERCISES is refused', () => {
    const lib = '0|A|Squats|3|5|100|0|-';
    expect(walkLibrary('T|1|0|' + lib.length + '|' + lib).error).toBe('count');
  });

  test('an empty note field is a valid field', () => {
    // The config page may send "" rather than "-" for a routine with no note.
    const body = libraryBody(4, 0, [{ n: 1, name: 'Notes', ex: ['Squats', 3, 5, 100, 0, ''] }]);
    const r = walkLibrary(body);
    expect(r.error).toBeUndefined();
    expect(r.routines.Notes.split('|')).toEqual(['Notes', 'Squats', '3', '5', '100', '0', '']);
  });

  test('the whole worst-case library fits the chunk budget', () => {
    const routines = [];
    for (let i = 0; i < 7; i++) {
      const ex = [];
      for (let e = 0; e < 10; e++) ex.push(['Exercise number ' + e, 3, 10, 32, 0, '-']);
      routines.push({ n: 10, name: 'Routine ' + i, ex });
    }
    const body = libraryBody(1, 0, routines);
    expect(Math.ceil(body.length / PROTO.CHUNK)).toBeLessThanOrEqual(PROTO.MAX_CHUNKS);
  });
});

// ---------------------------------------------------------------------------
// v9.2: 'U'/'D' are newer news than a library snapshot.
//
// The watch keeps one flag per slot for "this routine arrived by 'U' and no
// library has confirmed it yet" (UPSERT_FLAGS_KEY in main.c). The break phase
// of a library apply must keep such a routine even when the payload does not
// name it, because that payload was built before the upsert.
//
// Mirrors handle_library_sync()'s break phase; what this pins down is the rule,
// which is what the "routine appears then disappears" report came from.
// ---------------------------------------------------------------------------
function applyLibraryWithUpserts(state, body) {
  const r = walkLibrary(body);
  if (r.error) return r;

  // apply (make): everything the library names, in place; plus clear the flag —
  // the snapshot has caught up for those names.
  for (const [name, def] of Object.entries(r.routines)) state.stored[name] = def;
  for (const name of Object.keys(r.routines)) delete state.upserted[name];

  // break: drop what the library does not name, unless it arrived by 'U'
  for (const name of Object.keys(state.stored)) {
    if (name in r.routines) continue;
    if (state.upserted[name]) continue;      // newer news than this snapshot
    delete state.stored[name];
  }
  return r;
}

const newState = () => ({ stored: {}, upserted: {} });

function doUpsert(state, name, exStr) {
  state.stored[name] = name + '|' + exStr;
  state.upserted[name] = true;
}

const LIB = (seq, epoch, routines) => libraryBody(seq, epoch, routines);

test.describe('A library cannot undo an upsert (v9.2)', () => {
  test('a routine that arrived by U survives a stale library that omits it', () => {
    const s = newState();
    doUpsert(s, 'New One', 'Bench Press|3|8|60|0|-');

    // A payload built BEFORE the upsert: it only knows the old routine.
    applyLibraryWithUpserts(s, LIB(10, 100, [{ n: 1, name: 'Old One', ex: ['Squat', 3, 5, 100, 0, '-'] }]));

    expect(Object.keys(s.stored).sort()).toEqual(['New One', 'Old One']);
  });

  test('a library that does name it clears the protection', () => {
    const s = newState();
    doUpsert(s, 'New One', 'Bench Press|3|8|60|0|-');
    applyLibraryWithUpserts(s, LIB(11, 101, [
      { n: 1, name: 'New One', ex: ['Bench Press', 3, 8, 60, 0, '-'] },
    ]));
    expect(s.upserted['New One']).toBeUndefined();

    // Now absence DOES mean deleted, as it must.
    applyLibraryWithUpserts(s, LIB(12, 102, []));
    expect(Object.keys(s.stored)).toEqual([]);
  });

  test('an older routine is still deleted by a library that omits it', () => {
    const s = newState();
    s.stored['Old One'] = 'Old One|Squat|3|5|100|0|-';
    applyLibraryWithUpserts(s, LIB(13, 103, []));
    expect(Object.keys(s.stored)).toEqual([]);
  });

  test('a delete removes it for good, even on the next library', () => {
    const s = newState();
    doUpsert(s, 'Temp', 'Curl|3|12|15|0|-');
    delete s.stored['Temp'];
    delete s.upserted['Temp'];               // what delete_slot() does

    applyLibraryWithUpserts(s, LIB(14, 104, []));
    expect(Object.keys(s.stored)).toEqual([]);
  });
});

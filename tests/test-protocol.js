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
// Library sync ('T')
// Mirrors handle_library_sync() in main.c: swallow the header, then walk
// "<n>|<name>|<exercise>|…" groups, applying every routine and deleting the
// ones the phone no longer has (make before break).
// ---------------------------------------------------------------------------
function walkLibrary(body) {
  if (body[0] !== 'T' || body[1] !== '|') return { error: 'header' };
  const hdr = body.split('|').slice(1);              // seq|epoch|len|<n>|name|…
  const seq = +hdr[0], epoch = +hdr[1], declared = +hdr[2];
  const lib = hdr.slice(3).join('|');
  if (lib.length !== declared) return { error: 'truncated' };

  const routines = {};
  let cursor = 0;
  while (cursor < lib.length) {
    let q = cursor;
    while (q < lib.length && lib[q] !== '|') q++;
    const n = +lib.substring(cursor, q);
    if (!(n >= 1 && n <= MAX_EXERCISES) || q >= lib.length) return { error: 'count' };

    let end = q + 1;
    while (end < lib.length && lib[end] !== '|') end++;
    const name = lib.substring(q + 1, end);
    if (!name || end >= lib.length) return { error: 'name' };

    for (let f = 0; f < n * 6; f++) {
      if (lib[end] !== '|') return { error: 'exercise' };
      end++;
      while (end < lib.length && lib[end] !== '|') end++;
    }
    routines[name] = lib.substring(q + 1, end);
    if (end >= lib.length) break;
    cursor = end + 1;
  }
  return { seq, epoch, routines };
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

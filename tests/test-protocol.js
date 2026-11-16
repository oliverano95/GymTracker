// @ts-check
// Protocol tests for the v7.0.0 watch <-> phone envelope.
//
// These run without the Pebble SDK: the envelope/chunking logic is exercised
// in isolation against the same constants both sides use. Run with:
//   cd tests && npx playwright test test-protocol.js

const { test, expect } = require('@playwright/test');

// Mirrors PROTO in gymtracker/src/pkjs/index.js and the #defines in main.c.
const PROTO = { VERSION: 1, CHUNK: 512, MAX_CHUNKS: 8 };

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
    const chunks = buildChunks('R', '1', 'Leg Day|0|2|Squats|3|5|100|0|-');
    expect(chunks).toHaveLength(1);
    expect(chunks[0]).toBe('1|R|1|1/1|Leg Day|0|2|Squats|3|5|100|0|-');
  });

  test('a long routine is split and reassembles byte-identically', () => {
    const body = Array.from({ length: 40 }, (_, i) => `Exercise ${i}|3|10|60|0|-`).join('|');
    expect(body.length).toBeGreaterThan(PROTO.CHUNK);

    const chunks = buildChunks('R', '7', body);
    expect(chunks.length).toBeGreaterThan(1);
    expect(chunks.length).toBeLessThanOrEqual(PROTO.MAX_CHUNKS);

    const result = reassemble(chunks);
    expect(result.complete).toBeTruthy();
    expect(result.complete.type).toBe('R');
    expect(result.complete.id).toBe('7');
    expect(result.complete.body).toBe(body);
  });

  test('a batch payload survives the round trip', () => {
    const body = 'A|0|2|Squats|3|5|100|0|-~B|0|2|Press|3|8|40|0|-';
    const chunks = buildChunks('B', '3', body);
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
    const first  = buildChunks('R', '1', 'A'.repeat(PROTO.CHUNK + 10))[0];
    const second = buildChunks('R', '2', 'A'.repeat(PROTO.CHUNK + 10))[1];
    const result = reassemble([first, second]);
    expect(result.error).toBe('order');
  });

  test('the body keeps embedded pipes intact', () => {
    const body = 'Name|0|2|A|1|1|1|0|-|B|2|2|2|0|-';
    expect(reassemble(buildChunks('R', '1', body)).complete.body).toBe(body);
  });
});

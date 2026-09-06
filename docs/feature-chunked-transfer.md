# Feature Plan: Multi-Message Chunking for Routine Transfer

## Problem Statement

GymTracker's routine data is sent from the phone config page to the watch via a single AppMessage. The data rides the `pebblejs://close#` URL protocol, which has a hard limit of ~512 encoded characters (measured on real hardware). The current `MAX_CONFIG_RESPONSE = 450` is a conservative cap below this limit.

The payload includes:
- Routine name
- Exercise names, target sets, reps, weight, modifiers, comments
- Progression mode and weight increment

With 9 exercises using Hungarian names (e.g. `Oldalemel_dontott_torzs`), the encoded payload reaches 489 characters — **39 chars over the limit**. The phone app silently drops oversized payloads; the watch never receives them. The user gets no error, the routine just doesn't appear.

**Root cause:** A single-message protocol with a hard ~512-byte encoded ceiling cannot accommodate routines with moderate-length exercise names.

## Real-World Test Case: W1P:Váll-Kar

A user's actual 9-exercise shoulder/arm routine with Hungarian exercise names:

| Exercise | Sets | Reps | Weight | Modifier |
|----------|------|------|--------|----------|
| Oldalemelés | 5 | 8 | 10 | 3 |
| Oldalemelés döntött törzzsel | 4 | 10 | 10 | 3 |
| Nyak mögül nyomás rúddal | 5 | 8 | 5 | 3 |
| Bicepsz egyenes rúddal | 5 | 8 | 10 | 3 |
| Scott pad | 4 | 8 | 10 | 3 |
| Váltott karú bicepszezés | 5 | 8 | 12 | 3 |
| Tricepsz letolás csigán | 5 | 8 | 35 | 3 |
| Tricepsz franciarúddal | 5 | 8 | 10 | 3 |
| Tolódzkodás | 4 | 10 | 0 | 3 |

### Measured payload sizes

| Path | Encoded chars | Under 450? |
|------|--------------|------------|
| Batch send, full Hungarian names, no weights | 530 | No |
| Single send, full Hungarian names, no weights | 524 | No |
| Batch send, full names, with weights | 537 | No |
| Single send, full names, with weights | 531 | No |
| Batch send, shortened names, with weights | 447 | Yes (3 spare) |

### What "shortened" means in practice

To fit under 450, exercise names had to be truncated to 7-15 chars:

| Original | Shortened | Readability loss |
|----------|-----------|-----------------|
| Oldalemelés döntött törzzsel | Oldalem_dont_tzs | Moderate |
| Nyak mögül nyomás rúddal | Nyak_mog_nyomas_rud | Moderate |
| Váltott karú bicepszezés | Valtott_karu_bic | Significant |
| Tricepsz letolás csigán | Trip_letolas_csig | Significant |
| Tricepsz franciarúddal | Trip_francia_rud | Significant |

These names appear on a 144px watch screen during workouts — truncation degrades the experience.

### Key finding

**Even the single-send path (no BATCH~ prefix) exceeds 450** with 9 Hungarian exercises. The JSON wrapper overhead (`{"progressionMode":"-1","weightIncrement":"2","routineData":"..."}`) is the real bottleneck, not just the batch prefix. This means the limit affects all users with non-English routines, not just batch users.

## Goal

Remove the single-message size constraint so that any routine (up to the watch's memory limits) can be transferred from phone to watch reliably, with delivery confirmation.

## Constraints

| Constraint | Value | Source |
|-----------|-------|--------|
| Watch inbox buffer | 2048 bytes | `app_message_open(2048, 1024)` in `main.c:3935` |
| Watch total RAM | 64 KB | Pebble hardware (Basalt/Emery) |
| Phone outbox buffer | 1024 bytes | Pebble SDK default |
| Existing message keys | 6 | `package.json` |
| Max exercises per routine | 15 | `MAX_EXERCISES` in `main.c` |
| Max routine string length | 1300 bytes | `MAX_ROUTINE_LEN` in `index.html` |
| Current encoded limit | 450 chars | `MAX_CONFIG_RESPONSE` in `index.html:341` |
| True hardware limit | ~512 encoded chars | Measured on device (comment at line 336) |

## Requirements

### Functional Requirements

1. **FR-1:** A routine of up to 1300 raw characters (the existing `MAX_ROUTINE_LEN`) must be transferable from phone to watch.
2. **FR-2:** Transfer must include delivery confirmation — the phone must know the watch received each chunk.
3. **FR-3:** If a chunk fails to deliver, the phone must retry before sending the next chunk.
4. **FR-4:** The watch must reassemble chunks into a complete routine string before processing.
5. **FR-5:** The existing single-message path must continue to work for small routines (backward compatibility).
6. **FR-6:** The existing BATCH mode (`BATCH~` prefix) must continue to work unchanged.
7. **FR-7:** Progression mode and weight increment must be sent alongside the routine data (they currently ride the same AppMessage).

### Non-Functional Requirements

1. **NFR-1:** No more than 2 KB of static RAM allocated for the reassembly buffer.
2. **NFR-2:** Chunk transfer must complete within 10 seconds for a maximum-size routine.
3. **NFR-3:** The phone side must not block the UI during chunk transfer.
4. **NFR-4:** Failed transfers (after retries) must surface an error to the user.
5. **NFR-5:** No new Pebble SDK dependencies — use only existing AppMessage APIs.

### Out of Scope

- Compression or binary encoding (future optimization)
- Resuming a partially-completed transfer after app restart
- Transferring workout history or other data via chunks
- Changing the `pebblejs://close#` URL protocol itself

## Implementation Plan

### Phase 1: Protocol Design

#### 1.1 New Message Key

Add `CHUNK_TRANSFER` to `package.json` messageKeys:

```json
"messageKeys": [
    "ROUTINE_DATA",
    "WORKOUT_SUMMARY",
    "PROGRESSION_MODE",
    "WEIGHT_INCREMENT",
    "VOICE_ADD_EXERCISE",
    "NEW_EXERCISE_DATA",
    "CHUNK_TRANSFER"
]
```

This single key carries all chunk types. The first byte of each message is a type discriminator:

| First byte | Meaning | Payload |
|-----------|---------|---------|
| `0x01` | Chunk (middle or last) | `[chunk_index:1B][total_chunks:1B][data...]` |
| `0x02` | ACK (watch → phone) | `[next_expected_chunk:1B]` |
| `0x03` | NACK (watch → phone) | `[failed_chunk:1B][reason:1B]` |
| `0x04` | Meta (phone → watch) | `[total_length:2B][total_chunks:1B][flags:1B]` |

#### 1.2 Chunk Size Calculation

- Watch inbox buffer: 2048 bytes
- AppMessage dictionary overhead: ~10 bytes per key
- Available per chunk: ~2030 bytes
- Safe chunk size: **1800 bytes** (leaves headroom)
- Max routine size: 1300 bytes → **1 chunk** (fits in single message)
- For future-proofing: support up to 8 KB routine strings → **5 chunks max**

#### 1.3 Transfer Sequence

```
Phone                              Watch
  |                                   |
  |--- META (total_length, chunks) -->|
  |                                   | (allocates rx_buf, resets offset)
  |<---------- ACK (chunk 0) ---------|
  |                                   |
  |--- CHUNK 0 (index=0, data...) --->|
  |                                   | (appends to rx_buf)
  |<---------- ACK (chunk 1) ---------|
  |                                   |
  |--- CHUNK 1 (index=1, data...) --->|
  |                                   | (appends to rx_buf)
  |<---------- ACK (chunk 2) ---------|
  |                                   |
  |--- CHUNK 2 (index=2, data...) --->|  (last chunk)
  |                                   | (appends, calls parse_routine_string)
  |                                   | (saves to slot, refreshes UI)
  |<---------- ACK (chunk 0) ---------|  (0 = transfer complete)
  |                                   |
  | (transfer complete)               |
```

#### 1.4 Backward Compatibility

- If the total routine string fits in a single chunk (≤1800 bytes), skip META and send as one chunk with index=0, total=1. The watch processes it immediately without waiting for more chunks.
- The existing `ROUTINE_DATA` key path remains untouched for single-message transfers.
- The `BATCH~` prefix detection in `inbox_received_callback` is checked before chunk processing, so batch mode is unaffected.

### Phase 2: Watch-Side Changes (`main.c`)

#### 2.1 New Static Buffers and State

Add near the top of `main.c` (static scope):

```c
// --- Chunk transfer reassembly ---
#define CHUNK_BUF_SIZE 2048
#define CHUNK_MAX_chunks 8

static struct {
    char buf[CHUNK_BUF_SIZE];     // reassembly buffer
    int  offset;                  // current write position
    int  total_chunks;            // expected chunk count
    int  received_chunks;         // chunks received so far
    int  total_length;            // total data length expected
    bool active;                  // transfer in progress
} s_chunk;
```

Total static RAM: 2048 + ~20 bytes of state = **~2.1 KB**. Acceptable.

#### 2.2 Chunk Handling in `inbox_received_callback`

Add a new block at the top of `inbox_received_callback`, before the existing `ROUTINE_DATA` handler:

```c
Tuple *chunk_tuple = dict_find(iterator, MESSAGE_KEY_CHUNK_TRANSFER);
if (chunk_tuple && chunk_tuple->type == TUPLE_CSTRING) {
    uint8_t *raw = (uint8_t *)chunk_tuple->value->cstring;
    uint8_t type = raw[0];

    if (type == 0x04) {
        // --- META: initialize transfer ---
        s_chunk.total_length  = (raw[1] << 8) | raw[2];
        s_chunk.total_chunks  = raw[3];
        s_chunk.offset        = 0;
        s_chunk.received_chunks = 0;
        s_chunk.active        = true;
        memset(s_chunk.buf, 0, CHUNK_BUF_SIZE);

        // ACK: request chunk 0
        send_chunk_ack(0);
    }
    else if (type == 0x01 && s_chunk.active) {
        // --- CHUNK: append to buffer ---
        uint8_t chunk_idx = raw[1];
        // raw[2] = total_chunks (for verification)
        int data_len = chunk_tuple->length - 3; // minus type + idx + total
        if (s_chunk.offset + data_len < CHUNK_BUF_SIZE) {
            memcpy(s_chunk.buf + s_chunk.offset, raw + 3, data_len);
            s_chunk.offset += data_len;
            s_chunk.received_chunks++;

            if (s_chunk.received_chunks >= s_chunk.total_chunks) {
                // All chunks received — process
                s_chunk.buf[s_chunk.offset] = '\0';
                s_chunk.active = false;
                process_chunked_routine(s_chunk.buf);
                send_chunk_ack(0); // 0 = complete
            } else {
                // Request next chunk
                send_chunk_ack(s_chunk.received_chunks);
            }
        } else {
            // Buffer overflow — abort
            s_chunk.active = false;
            send_chunk_nack(chunk_idx, 0x01); // reason: overflow
        }
    }
}
```

#### 2.3 ACK/NACK Send Functions

```c
static void send_chunk_ack(uint8_t next_chunk) {
    DictionaryIterator *iter;
    if (app_message_outbox_begin(&iter) == APP_MSG_OK) {
        uint8_t payload[2] = { 0x02, next_chunk }; // type=ACK, next_chunk
        dict_write_data(iter, MESSAGE_KEY_CHUNK_TRANSFER, payload, 2);
        app_message_outbox_send();
    }
}

static void send_chunk_nack(uint8_t failed_chunk, uint8_t reason) {
    DictionaryIterator *iter;
    if (app_message_outbox_begin(&iter) == APP_MSG_OK) {
        uint8_t payload[3] = { 0x03, failed_chunk, reason }; // type=NACK
        dict_write_data(iter, MESSAGE_KEY_CHUNK_TRANSFER, payload, 3);
        app_message_outbox_send();
    }
}
```

#### 2.4 Process Chunked Routine

```c
static void process_chunked_routine(char *routine_str) {
    // Same logic as the existing single-message handler
    parse_routine_string(routine_str);

    int target_slot = s_app.storage.active_slots;
    bool slot_exists = false;

    for (int i = 0; i < s_app.storage.active_slots; i++) {
        if (strcmp(s_app.storage.slot_names[i], s_app.state.routine_name) == 0) {
            target_slot = i;
            slot_exists = true;
            break;
        }
    }
    if (target_slot > MAX_SLOTS - 1) target_slot = MAX_SLOTS - 1;

    // PRESERVE HISTORY (same as existing code)
    if (slot_exists) {
        for (int i = 0; i < s_app.state.total_exercises; i++) {
            for (int old_idx = 0; old_idx < s_app.storage.slot_counts[target_slot]; old_idx++) {
                Exercise old_ex;
                if (persist_read_data(ROUTINE_EX_BASE + (target_slot * MAX_EXERCISES) + old_idx,
                                      &old_ex, sizeof(Exercise)) > 0) {
                    if (strcmp(s_app.state.exercises[i].name, old_ex.name) == 0) {
                        for (int s = 0; s < 10; s++) {
                            s_app.state.exercises[i].actual_reps[s]   = old_ex.actual_reps[s];
                            s_app.state.exercises[i].actual_weight[s] = old_ex.actual_weight[s];
                        }
                        break;
                    }
                }
            }
        }
    }

    save_routine_to_slot(target_slot);
    refresh_directory();
    menu_layer_reload_data(s_app.ui.menu_layer);
    vibes_double_pulse();
}
```

#### 2.5 Modify `sendToPebble` Decision

In the existing `ROUTINE_DATA` handler (single-message path), add a length check. If the routine string exceeds a safe single-message threshold, request chunked transfer instead. This requires the phone to send a signal that chunked mode is needed.

**Alternative (simpler):** Always use chunked transfer from the phone side. The watch handles single-chunk transfers the same way (chunk 0 of 1 = process immediately). This avoids adding complexity to the existing C handler.

**Decision: Use the simpler alternative.** The phone always sends via chunked protocol. The watch always processes via the chunk handler. The existing `ROUTINE_DATA` single-message path remains as fallback for voice exercises and workout sync (which are small and don't need chunking).

### Phase 3: Phone-Side Changes (`index.js`)

#### 3.1 Chunked Send Function

Add to `index.js`:

```javascript
function sendChunkedToWatch(routineStr, progressionMode, weightIncrement) {
    var CHUNK_SIZE = 1800;
    var chunks = [];
    for (var i = 0; i < routineStr.length; i += CHUNK_SIZE) {
        chunks.push(routineStr.slice(i, i + CHUNK_SIZE));
    }
    var totalChunks = chunks.length;
    var totalLength = routineStr.length;
    var currentChunk = 0;

    console.log('Chunked transfer: ' + totalLength + ' chars, ' + totalChunks + ' chunks');

    function sendMeta() {
        // META: [type=0x04][total_length_high][total_length_low][total_chunks]
        var meta = new ArrayBuffer(4);
        var view = new Uint8Array(meta);
        view[0] = 0x04;
        view[1] = (totalLength >> 8) & 0xFF;
        view[2] = totalLength & 0xFF;
        view[3] = totalChunks;

        Pebble.sendAppMessage(
            { 'CHUNK_TRANSFER': arrayBufferToString(meta) },
            function() { console.log('META sent, waiting for ACK...'); },
            function(err) { console.log('META failed: ' + JSON.stringify(err)); }
        );
    }

    function sendChunk(index) {
        var data = chunks[index];
        // [type=0x01][chunk_idx][total_chunks][data...]
        var header = new ArrayBuffer(3);
        var headerView = new Uint8Array(header);
        headerView[0] = 0x01;
        headerView[1] = index;
        headerView[2] = totalChunks;

        var payload = arrayBufferToString(header) + data;

        Pebble.sendAppMessage(
            { 'CHUNK_TRANSFER': payload },
            function() { console.log('Chunk ' + index + '/' + totalChunks + ' sent'); },
            function(err) { console.log('Chunk ' + index + ' failed: ' + JSON.stringify(err)); }
        );
    }

    // Listen for ACKs from watch
    Pebble.addEventListener('appmessage', function chunkAckHandler(e) {
        var chunkData = e.payload['CHUNK_TRANSFER'];
        if (!chunkData) return;

        var raw = stringToArrayBuffer(chunkData);
        var type = raw[0];

        if (type === 0x02) { // ACK
            var nextChunk = raw[1];
            if (nextChunk === 0 && currentChunk >= totalChunks) {
                // Transfer complete
                console.log('Chunked transfer complete');
                Pebble.removeEventListener('appmessage', chunkAckHandler);
                // Send progression/increment separately (small, fits in one message)
                Pebble.sendAppMessage({
                    'PROGRESSION_MODE': parseInt(progressionMode, 10),
                    'WEIGHT_INCREMENT': parseInt(weightIncrement, 10)
                });
            } else {
                currentChunk = nextChunk;
                sendChunk(currentChunk);
            }
        } else if (type === 0x03) { // NACK
            console.log('NACK received for chunk ' + raw[1] + ', reason: ' + raw[2]);
            // Retry the failed chunk
            sendChunk(raw[1]);
        }
    });

    sendMeta();
}

// Helper: ArrayBuffer ↔ String conversion
function arrayBufferToString(buffer) {
    return String.fromCharCode.apply(null, new Uint8Array(buffer));
}
function stringToArrayBuffer(str) {
    var buf = new ArrayBuffer(str.length);
    var view = new Uint8Array(buf);
    for (var i = 0; i < str.length; i++) view[i] = str.charCodeAt(i);
    return buf;
}
```

#### 3.2 Modify `webviewclosed` Handler

In `pkjs/index.js`, the `webviewclosed` handler currently calls `sendAppMessage` directly. Modify to use chunked transfer when the routine data is large:

```javascript
// In webviewclosed handler, replace the direct sendAppMessage call:
var routineData = configData.routineData;
if (routineData && routineData.length > 1500) {
    // Large routine — use chunked transfer
    sendChunkedToWatch(
        routineData,
        configData.progressionMode,
        configData.weightIncrement
    );
} else {
    // Small routine — use existing single-message path
    var appMessageData = {};
    if (routineData) appMessageData['ROUTINE_DATA'] = routineData;
    if (configData.progressionMode !== undefined)
        appMessageData['PROGRESSION_MODE'] = parseInt(configData.progressionMode, 10);
    if (configData.weightIncrement !== undefined)
        appMessageData['WEIGHT_INCREMENT'] = parseInt(configData.weightIncrement, 10);

    if (Object.keys(appMessageData).length > 0) {
        Pebble.sendAppMessage(appMessageData, ...);
    }
}
```

**Threshold:** 1500 chars raw → ~3000 encoded → definitely over 450 limit. Below 1500, single message may work (depends on encoding expansion). Conservative threshold ensures we always chunk when needed.

#### 3.3 Remove `MAX_CONFIG_RESPONSE` Check

The `sendToPebble` function in `index.html` checks `MAX_CONFIG_RESPONSE` and shows an alert. With chunked transfer, this check is no longer needed for routine data. However, keep it for backward compatibility with other payload types.

**Option A (recommended):** Remove the check entirely from `sendToPebble`. The chunked protocol handles size.
**Option B:** Keep the check but raise the limit to 2000 (raw, before encoding) to allow single-message for small routines.

**Decision: Option A.** The chunked protocol is the single source of truth for transfer size.

### Phase 4: Config Page Changes (`index.html` / `index_dev.html`)

#### 4.1 Remove `MAX_CONFIG_RESPONSE` Alert

In `sendToPebble` (index.html:1052-1059), remove the length check:

```javascript
function sendToPebble(configData) {
    const returnTo = getQueryParam('return_to', 'pebblejs://close#');
    const encoded  = encodeURIComponent(JSON.stringify(configData));
    // MAX_CONFIG_RESPONSE check removed — chunked transfer handles size
    window.location.href = returnTo + encoded;
}
```

#### 4.2 Update `MAX_ROUTINE_LEN` Comment

The `MAX_ROUTINE_LEN = 1300` constant in index.html can remain. It's the max raw routine string length. With chunking, all routines up to 1300 bytes will transfer successfully.

### Phase 5: Testing

#### 5.1 Unit Tests (Playwright)

Add to `tests/test-chunking.js`:

| Test | What it validates |
|------|-------------------|
| Small routine single-message | Routines under 1500 chars use single-message path |
| Large routine triggers chunking | Routines over 1500 chars trigger chunked transfer |
| Chunk assembly correctness | Reassembled string matches original |
| META message format | Type byte, total_length, total_chunks are correct |
| ACK handling | Phone sends next chunk after receiving ACK |
| NACK retry | Phone retries failed chunk |
| Buffer overflow protection | Routine > 2048 bytes is rejected with error |
| Progression/increment sent | These values arrive after chunked transfer completes |

#### 5.2 Hardware Testing

- Build and install on emulator: `pebble install --emulator emery`
- Send routine via config page
- Verify routine appears in watch directory
- Test with routine at max size (1300 bytes, 15 exercises)
- Test with Hungarian characters (URL encoding expansion)
- Test interrupted transfer (turn off Bluetooth mid-transfer)

### Phase 6: Documentation

- Update `docs/beta-testing.md` with chunking notes
- Update `learnings.md` with AppMessage protocol details
- Update `automated-testing-plan.md` with new test descriptions
- Update PR description for the chunking feature

## File Changes Summary

| File | Change | Lines (est.) |
|------|--------|-------------|
| `gymtracker/package.json` | Add `CHUNK_TRANSFER` message key | +1 |
| `gymtracker/src/c/main.c` | Chunk buffer, reassembly, ACK/NACK, process function | +120 |
| `gymtracker/src/pkjs/index.js` | `sendChunkedToWatch`, helper functions, modify webviewclosed | +80 |
| `index.html` | Remove `MAX_CONFIG_RESPONSE` check in `sendToPebble` | -5 |
| `index_dev.html` | Same as above | -5 |
| `tests/test-chunking.js` | New test file | +150 |

**Total:** ~340 lines added, ~10 removed.

## Risk Assessment

| Risk | Likelihood | Impact | Mitigation |
|------|-----------|--------|------------|
| ACK messages lost (Bluetooth interference) | Medium | Transfer stalls | Phone-side timeout (5s) with retry |
| Watch RAM overflow with large reassembly buffer | Low | Crash | 2 KB static buffer, validated against CHUNK_BUF_SIZE |
| Backward incompatibility with old watch firmware | Low | Transfer fails | Old firmware won't have CHUNK_TRANSFER key — fails gracefully |
| Chunk ordering corruption | Low | Routine garbled | Index byte in each chunk, watch validates sequence |
| Phone JS thread blocked during transfer | Low | UI freezes | All sends are async with callbacks |

## Success Criteria

1. A routine with 9 exercises and Hungarian names (original 489-char encoded payload) transfers successfully.
2. A routine at `MAX_ROUTINE_LEN` (1300 bytes) transfers successfully.
3. Existing small routines continue to work without regression.
4. BATCH mode continues to work unchanged.
5. All existing tests pass (17 CSV export + 10 issue #46).
6. New chunking tests pass (8 tests).

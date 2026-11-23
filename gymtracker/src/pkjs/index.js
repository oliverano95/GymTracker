// src/pkjs/index.js

// ============================================================
// CONFIGURATION
// ============================================================
var CONFIG = {
  isDevMode: true,   // TEMP: beta always links to index_dev.html
  configUrlDev:  'https://oliverano95.github.io/GymTracker/index_dev.html',
  configUrlProd: 'https://oliverano95.github.io/GymTracker/',
  maxHistory: 15
};

// ============================================================
// STORAGE HELPERS
// Centralise all localStorage access so key names are never
// scattered as magic strings throughout the code.
// ============================================================
var Storage = {
  get: function(key, fallback) {
    var val = localStorage.getItem(key);
    return val !== null ? val : (fallback !== undefined ? fallback : null);
  },
  set: function(key, value) {
    localStorage.setItem(key, value);
  },
  remove: function(key) {
    localStorage.removeItem(key);
  },
  getJSON: function(key, fallback) {
    try {
      var val = localStorage.getItem(key);
      return val !== null ? JSON.parse(val) : fallback;
    } catch (e) {
      console.log('Storage.getJSON parse error for key "' + key + '": ' + e);
      return fallback;
    }
  },
  setJSON: function(key, value) {
    localStorage.setItem(key, JSON.stringify(value));
  }
};

// ============================================================
// GOOGLE SHEETS EXPORT
// Isolated so it can be called from anywhere without
// duplicating XHR boilerplate.
// ============================================================
function exportToGoogleSheets(rawData) {
  var scriptUrl = Storage.get('googleUrl');
  var scriptPwd = Storage.get('googlePwd');
  if (!scriptUrl || !scriptPwd) return;

  console.log('Google Sheets config found. Uploading...');
  var req = new XMLHttpRequest();
  req.open('POST', scriptUrl, true);
  req.setRequestHeader('Content-Type', 'application/json');

  req.onload = function() {
    console.log('Successfully logged workout to Google Sheets! Status: ' + req.status);
  };
  req.onerror = function() {
    console.log('Network error while uploading to Google Sheets.');
  };

  req.send(JSON.stringify({ token: scriptPwd, workoutData: rawData }));
}

// ============================================================
// WORKOUT HISTORY
// ============================================================
function saveWorkoutLocally(rawData) {
  // Validate payload size before storing to prevent localStorage bloat
  // (bumped to 8 KB: the summary now carries per-set HR + an HR time-series)
  if (!rawData || rawData.length > 8192) {
    console.log('Workout payload missing or oversized, skipping local save.');
    return;
  }

  var history = Storage.getJSON('workoutHistory', []);
  history.push({ timestamp: new Date().getTime(), data: rawData });

  while (history.length > CONFIG.maxHistory) {
    history.shift();
  }

  Storage.setJSON('workoutHistory', history);
  console.log('Workout saved locally. History count: ' + history.length);
}

// ============================================================
// WIRE PROTOCOL (v8 framing, v9 payloads)
// '<VER>|<TYPE>|<id>|<seq>/<total>|<body>'
//
// What can cross the wire, in full:
//   phone -> watch   PROTO_MESSAGE 'U'  ONE routine, upserted by name
//                    PROTO_MESSAGE 'D'  ONE routine, deleted by name
//                    PROTO_MESSAGE 'T'  the whole routine library (chunked);
//                                       used by HELLO to repair the watch
//                    PROGRESSION_MODE / WEIGHT_INCREMENT   two ints
//                    SYNC_SEQUENCE / SYNC_EPOCH            HELLO (two ints)
//   watch -> phone   PROTO_MESSAGE 'W'  one workout report (prefixed with the
//                                       progression delta, chunked)
//                    PROTO_ACK           received chunk <id>
//                    SYNC_SEQUENCE / SYNC_EPOCH  what the watch holds (HELLO)
//
// A save sends one routine, a delete sends one name, and neither touches the
// rest of the library on the watch. 'T' still exists for the one case that
// needs every routine: a watch that has none (fresh install, factory reset).
// ============================================================
var PROTO = {
  VERSION: 2,
  CHUNK: 512,
  // Must match PROTO_MAX_CHUNKS in main.c: 8 x 512 = the watch's 4096-byte
  // reassembly buffer, which is sized for 7 routines of MAX_ROUTINE_LEN.
  MAX_CHUNKS: 8
};

// Outbound: one transfer at a time. Pebble.sendAppMessage allows a single
// in-flight message, so the success callback sends the next chunk.
var protoOut = {
  queue: [],
  index: 0,
  id: '0',
  total: 0,
  retries: 0,
  onDone: null,
  // v7.0.4: transfers currently in flight. A transfer that never completes
  // (the watch went out of range mid-library) used to leave this non-zero
  // forever, so protoSendNext() ignored every later chunk and every Save after
  // that did nothing until the phone app was restarted.
  pending: 0,
  // Watchdog: if a transfer has not finished after this long, abandon it so
  // the next one may start. Real transfers take well under a second.
  watchdog: null,
  WATCHDOG_MS: 15000
};

var protoIn = {
  buf: '',
  expected: 0,
  got: 0,
  id: '',
  type: ''
};

var PROTO_MAX_RETRIES = 3;

function protoNextId() {
  var n = parseInt(Storage.get('protoNextId', '1'), 10);
  if (!n || n < 1) n = 1;
  Storage.set('protoNextId', String(n + 1));
  return String(n);
}

// Build the chunks for `body` and send them one after another.
// `onDone(failed)` fires when the transfer ends, either way.
function protoSend(type, body, onDone) {
  body = body || '';
  var total = Math.ceil(body.length / PROTO.CHUNK) || 1;

  if (total > PROTO.MAX_CHUNKS) {
    // The watch cannot reassemble it: say so instead of truncating.
    console.log('PROTO: ' + type + ' is ' + body.length + ' chars, over the ' +
                (PROTO.MAX_CHUNKS * PROTO.CHUNK) + '-char limit');
    protoNotify('Too much to send.\nShorten routines.', true);
    if (onDone) onDone(true);
    return;
  }

  // v7.0.4: log when a previous transfer is still running and replace it. It
  // used to be a silent no-op, which is why a second Save could do nothing.
  if (protoOut.pending > 0) {
    console.log('PROTO: replacing unfinished transfer ' + protoOut.id +
                ' (' + protoOut.index + '/' + protoOut.total + ') with a new one');
    protoOutClearWatchdog();
    protoOut.pending = 0;
    protoOut.onDone = null;
  }

  var id = protoNextId();
  protoOut.queue = [];
  for (var i = 0; i < total; i++) {
    protoOut.queue.push(
      PROTO.VERSION + '|' + type + '|' + id + '|' + (i + 1) + '/' + total + '|' +
      body.substr(i * PROTO.CHUNK, PROTO.CHUNK)
    );
  }
  protoOut.index   = 0;
  protoOut.id      = id;
  protoOut.total   = total;
  protoOut.retries = 0;
  protoOut.onDone  = onDone || null;
  protoOut.pending = 1;
  protoOutClearWatchdog();
  protoOut.watchdog = setTimeout(protoOutOnWatchdog, protoOut.WATCHDOG_MS);
  protoSendNext();
}

// End the current transfer and fire its completion callback exactly once.
function protoOutFinish(failed) {
  protoOutClearWatchdog();
  protoOut.pending = 0;
  var done = protoOut.onDone;
  protoOut.onDone = null;
  // Whatever the transfer asks for next (a HELLO after a delete, a settings
  // message) is queued behind it, so it goes out on the wire in that order.
  if (done) deferToWire(function () { done(failed); });
}

// ------------------------------------------------------------
// WIRE QUEUE
// ------------------------------------------------------------
// One thing at a time: protoSend() cancels whatever is in flight, so two
// messages sent back to back (the 'D' delete and the HELLO that follows it)
// would otherwise race, and the order the watch sees decides whether the
// phone hears back again.
var wireQueue = [];
var wireBusy  = false;
var wireTimer = null;

function deferToWire(fn) {
  wireQueue.push(fn);
  wirePump();
}

function wirePump() {
  if (wireBusy || protoOut.pending > 0) return;
  if (wireTimer) { clearTimeout(wireTimer); wireTimer = null; }
  if (wireQueue.length === 0) return;

  wireBusy = true;
  var fn = wireQueue.shift();
  try {
    fn();
  } catch (e) {
    console.log('WIRE: queued step threw: ' + e);
    wireBusy = false;
    wirePump();
    return;
  }
  wireBusy = false;

  // A step that started a transfer is finished when that transfer is; the
  // watchdog guarantees this is reached even when the watch stops answering.
  if (protoOut.pending > 0) {
    if (protoOut.watchdog) {
      wireTimer = setTimeout(wirePump, protoOut.WATCHDOG_MS + 50);
    }
    return;
  }
  wirePump();
}

function protoOutClearWatchdog() {
  if (protoOut.watchdog) {
    clearTimeout(protoOut.watchdog);
    protoOut.watchdog = null;
  }
}

// A transfer that never completed (watch out of range, phone app suspended
// mid-library). Give up so the next Save is not blocked: the sequence numbers
// still disagree, so HELLO on the next connect sends the library again.
function protoOutOnWatchdog() {
  protoOut.watchdog = null;
  if (protoOut.pending === 0) return;
  console.log('PROTO: transfer ' + protoOut.id + ' timed out at chunk ' +
              protoOut.index + '/' + protoOut.total + ', giving up');
  protoOut.index = protoOut.queue.length;   // stop the chain
  protoOutFinish(true);
}

function protoSendNext() {
  if (protoOut.pending === 0) return;       // this transfer was abandoned

  if (protoOut.index >= protoOut.queue.length) {
    console.log('PROTO: sent ' + protoOut.total + ' chunk(s) for ' + protoOut.id);
    protoOutFinish(false);
    return;
  }

  var msg = { PROTO_MESSAGE: protoOut.queue[protoOut.index] };

  Pebble.sendAppMessage(msg, function() {
    protoOut.index++;
    protoSendNext();
  }, function(err) {
    // Only retry chunks of the transfer that is still current.
    if (protoOut.pending === 0) return;

    // v7.0.5: the watch NACKs a body it could not apply (it used to ACK it and
    // silently keep the old routines). A resend cannot fix a malformed body, so
    // retrying 3x is pointless; report it once and let the next connect retry.
    if (err && err.data && err.data.nack) {
      console.log('PROTO: watch rejected the library body for ' + protoOut.id +
                  ' (reason ' + (err.data.reason || 'unknown') + ')');
      protoOut.index = protoOut.queue.length;
      protoNotify('Watch could not take the routines.', true);
      protoOutFinish(true);
      return;
    }

    if (protoOut.retries < PROTO_MAX_RETRIES) {
      protoOut.retries++;
      console.log('PROTO: chunk retry ' + protoOut.retries + ' for ' + protoOut.id);
      protoSendNext();
      return;
    }
    // Give up on this chunk. The watch's sequence numbers still disagree, so
    // the next connect (or the next save) sends the whole library again — a
    // failure is never silent, but it is also never fatal.
    console.log('PROTO: chunk failed permanently for ' + protoOut.id + ': ' + JSON.stringify(err));
    protoOut.index = protoOut.queue.length;
    protoNotify('Sync failed.\nWill retry.', true);
    protoOutFinish(true);
  });
}

function protoAck(id) {
  Pebble.sendAppMessage({ PROTO_ACK: String(id) },
    function() { },
    function(err) { console.log('PROTO: ack failed: ' + JSON.stringify(err)); });
}

function protoNotify(message, failed) {
  try {
    if (typeof Pebble.showSimpleNotificationOnPebble === 'function') {
      Pebble.showSimpleNotificationOnPebble(failed ? 'GymTracker: sync' : 'GymTracker',
                                             message.replace(/\n/g, ' '));
    }
  } catch (e) { /* notifications are best effort */ }
}

// Parse and reassemble an inbound envelope. Returns the completed body, or
// null while more chunks are outstanding.
function protoReassemble(envelope) {
  // Split only on the first four delimiters so the body is preserved verbatim.
  var parts = envelope.split('|');
  if (parts.length < 5) return null;

  var ver   = parseInt(parts[0], 10);
  var type  = parts[1];
  var id    = parts[2];
  var seqT  = parts[3].split('/');
  var seq   = parseInt(seqT[0], 10);
  var total = parseInt(seqT[1], 10);
  var body  = parts.slice(4).join('|');

  if (!ver || ver < 1 || ver > PROTO.VERSION) {
    console.log('PROTO: refusing envelope version ' + parts[0] + ' (we speak ' + PROTO.VERSION + ')');
    return null;
  }
  if (!total || total > PROTO.MAX_CHUNKS || !seq || seq > total) return null;

  if (seq === 1) {
    protoIn.buf = '';
    protoIn.expected = total;
    protoIn.got = 0;
    protoIn.id = id;
    protoIn.type = type;
  } else if (protoIn.expected !== total || protoIn.id !== id || protoIn.type !== type ||
             protoIn.got !== seq - 1) {
    // Out of sequence, or a retried duplicate of a chunk we already have.
    console.log('PROTO: unexpected chunk ' + seq + '/' + total + ' for ' + id + ', ignoring');
    return null;
  }

  protoIn.buf += body;
  protoIn.got++;

  if (protoIn.got >= protoIn.expected) {
    var complete = { type: protoIn.type, id: protoIn.id, body: protoIn.buf };
    protoIn.buf = '';
    protoIn.expected = 0;
    protoIn.got = 0;
    return complete;
  }
  return null;
}

// ============================================================
// PEBBLE EVENT LISTENERS
// ============================================================
// (the 'ready' handler lives with the sync code below, because that is where
// its only job — reconciling with the watch — belongs)

// 2. User tapped "Settings" on the watch
Pebble.addEventListener('showConfiguration', function() {
  var baseUrl = CONFIG.isDevMode ? CONFIG.configUrlDev : CONFIG.configUrlProd;

  var googleUrl      = Storage.get('googleUrl', '');
  var googlePwd      = Storage.get('googlePwd', '');
  // The library this phone holds, as routine definitions. The page lists the
  // dropdown from this, compares a save against it, and checks a delete
  // against it — so it is always the CURRENT library, never a cache.
  var syncedRoutines = JSON.stringify(libraryRoutineDict());
  var historyArr     = Storage.getJSON('workoutHistory', []);

  var SAFE_URL_LIMIT = 7000; // GitHub Pages rejects URLs over ~8KB

  // Build the URL, trimming history from oldest first until it fits.
  // NOTE: loop condition must be `> 0` not `>= 0` — an array length is
  // always >= 0 so `>= 0` is an infinite loop (the bug in 6.6.1).
  var url = '';
  while (true) {
    var params = [
      'googleUrl=' + encodeURIComponent(googleUrl),
                        'googlePwd=' + encodeURIComponent(googlePwd),
                        'history='   + encodeURIComponent(JSON.stringify(historyArr)),
                        'sync='      + encodeURIComponent(syncedRoutines)
    ];
    if (diagModeEnabled()) params.push('diag=1');
    url = baseUrl + '?' + params.join('&');

    if (url.length <= SAFE_URL_LIMIT) break;  // fits — send it

    if (historyArr.length > 0) {
      // Still too long — drop the oldest workout and try again
      historyArr.shift();
    } else {
      // History exhausted but URL still too long (very large sync dict).
      // Drop synced routines from the URL as a last resort.
      console.log('Warning: URL too long after clearing history. Dropping sync data from URL.');
      syncedRoutines = '{}';
      url = baseUrl + '?' + [
        'googleUrl=' + encodeURIComponent(googleUrl),
                        'googlePwd=' + encodeURIComponent(googlePwd),
                        'history='   + encodeURIComponent('[]'),
                        'sync='      + encodeURIComponent('{}')
      ].join('&');
      break;
    }
  }

  console.log('Opening config page. Final URL length: ' + url.length);
  Pebble.openURL(url);
});

// Beta diagnostic: whether to open the config page in close-URL probe mode.
//
// The flag lives in the phone app's own localStorage so it survives restarts.
// It has to be toggled from outside the page (the page cannot write pkjs's
// storage), so a tiny app message does it: send {"DIAG":1} to switch the probe
// on for the next Settings open, {"DIAG":0} to switch it off. The watch does
// not implement DIAG; only pkjs reads it, so nothing changes on the C side.
function diagModeEnabled() {
  try {
    return Storage.get('diagMode', '0') === '1';
  } catch (e) {
    return false;
  }
}

// 3. Configuration web page closed
Pebble.addEventListener('webviewclosed', function(e) {
  if (!e.response || e.response === 'CANCELLED' || e.response === '[]') return;

  // Log the raw response length. The config response rides the
  // pebblejs://close# URL, which the phone app silently truncates past a
  // platform limit, so this line is the diagnostic if a save ever goes missing.
  console.log('webviewclosed: raw response length = ' + (e.response ? e.response.length : 0));

  var configData;
  try {
    configData = JSON.parse(decodeURIComponent(e.response));
  } catch (err) {
    console.log('webviewclosed: failed to parse response (length=' + (e.response ? e.response.length : 0) + '): ' + err);
    return;
  }

  // --- Credentials ---
  if (configData.clearGoogle) {
    Storage.remove('googleUrl');
    Storage.remove('googlePwd');
    console.log('Google Sync credentials wiped.');
  } else {
    if (configData.googleUrl && configData.googleUrl.trim() !== '') {
      Storage.set('googleUrl', configData.googleUrl);
    }
    if (configData.googlePwd && configData.googlePwd.trim() !== '') {
      Storage.set('googlePwd', configData.googlePwd);
    }
  }

  // --- History ---
  if (configData.clearHistory) {
    Storage.setJSON('workoutHistory', []);
  }

  // --- Routines: the page may not overwrite the library wholesale ---
  // The library is owned here, and the page only ever sends one named command
  // (below). `updatedSync` is the old "here is my cached library" field: taking
  // it would overwrite the real library with whatever that page had cached,
  // which is how deleted routines came back.
  if (configData.updatedSync !== undefined) {
    console.log('webviewclosed: ignoring updatedSync (the phone owns the library)');
  }

  // --- Settings: two ints, their own message ---
  var settingsData = {};
  if (configData.progressionMode !== undefined) {
    settingsData['PROGRESSION_MODE'] = parseInt(configData.progressionMode, 10);
  }
  if (configData.weightIncrement !== undefined) {
    settingsData['WEIGHT_INCREMENT'] = parseInt(configData.weightIncrement, 10);
  }
  if (Object.keys(settingsData).length > 0) {
    Pebble.sendAppMessage(settingsData,
                          function()    { console.log('Progression settings sent.'); },
                          function(err) { console.log('Failed to send settings: ' + JSON.stringify(err)); }
    );
  }

  // --- Routines: one command, one routine ---
  // The webview and pkjs have SEPARATE localStorage, so the page cannot write
  // the library here; the routine has to arrive in this response. That is the
  // whole reason the old design failed, and why these commands exist.
  //
  //   save   : add the routine to this phone's library, then upsert it on the
  //            watch by name (replace exercises, or add a new routine). Other
  //            routines on the watch are not touched. `renamedFrom` names the
  //            routine this replaces, if the name was edited: it leaves the
  //            library and is deleted on the watch.
  //   delete : remove the routine from this phone's library and delete it from
  //            the watch by name — but only if this phone actually held it.
  //
  // Deleting ON THE WATCH is the opposite direction and is handled by the watch
  // alone: it leaves this library untouched, by design. The page says whether
  // the routine was in this library (`wasOnPhone`), and that is the authority
  // here: a routine deleted on the watch is not in this library, so a stale
  // page can never ask for it to be deleted from here.
  if (configData.cmd === 'save' && configData.name && configData.routine) {
    var renamedFrom = (configData.renamedFrom && configData.renamedFrom !== configData.name)
                      ? configData.renamedFrom : null;

    saveRoutineLocally(configData.name, configData.saved || configData.routine);
    if (renamedFrom && hasRoutineLocally(renamedFrom)) {
      deleteRoutineLocally(renamedFrom);
    }

    // One routine changed; the rest of the library did not, so only the watch
    // is told — by name, so nothing else on it can be touched. A rename is a
    // create plus a delete of the old name, in that order (upsert first, so a
    // failure leaves the user with the routine under both names rather than
    // neither).
    sendRoutineUpsert(configData.name, configData.routine,
                      renamedFrom ? function () { sendRoutineDelete(renamedFrom); }
                                  : null);
  } else if (configData.cmd === 'delete' && configData.name) {
    // This library is the authority: a routine deleted ON the watch is not in
    // it, so a page that says "delete X" for a routine this phone never had
    // must not reach the watch at all. `wasOnPhone` is only logged, because a
    // page held open across a change can be out of date.
    if (hasRoutineLocally(configData.name)) {
      deleteRoutineLocally(configData.name);
      sendRoutineDelete(configData.name);
    } else {
      console.log('LIB: "' + configData.name + '" was not on this phone (page said ' +
                  (configData.wasOnPhone === false ? 'it was not' : 'it was') +
                  '), nothing to delete');
    }
  } else if (configData.libraryChanged) {
    // Backwards compatibility with an older index page that still asks for a
    // whole-library push. New pages never send this.
    syncLibrary('config page saved', true);
  }
});

// 4. Message received FROM the watch
// ============================================================
// PROGRESSION DELTA  (watch -> phone, the only routine-shaped
// message that comes back from the watch)
// "<routine>|<prog>|<inc>|<name>|<reps'>|<weight'>|..."
// Only targets change: the exercise LIST is never replaced, so the
// phone stays the single source of truth for routine definitions.
// ============================================================
function applyProgressionDelta(body) {
  var parts = body.split('|');
  if (parts.length < 3) return;

  // The watch appends a djb2 checksum of everything before it. Comparing it
  // with the last one applied means a report that was resent (never ACKed,
  // then pushed again on reconnect) does not apply the same jump twice.
  var checksum = parts[parts.length - 1];
  if (/^[0-9a-f]{1,8}$/.test(checksum)) {
    if (Storage.get('lastAppliedDelta', '') === checksum) {
      console.log('Progression delta already applied, ignoring resend.');
      return;
    }
    parts = parts.slice(0, -1);

    // Same djb2 as the watch (main.c); a mismatch only means the payload was
    // damaged, in which case the numbers below are still the best we have.
    var payload = parts.join('|');
    var sum = 5381;
    for (var c = 0; c < payload.length; c++) sum = ((sum << 5) + sum) + payload.charCodeAt(c);
    if ((sum >>> 0).toString(16) !== checksum) {
      console.log('Progression delta checksum mismatch (corrupt); applying anyway.');
    }
    Storage.set('lastAppliedDelta', checksum);
  }

  var routineName = parts[0];
  var mode        = parseInt(parts[1], 10);
  var increment   = parseInt(parts[2], 10);

  var savedRoutines = Storage.getJSON('savedRoutines', []);
  var routine = null;
  for (var i = 0; i < savedRoutines.length; i++) {
    if (savedRoutines[i].name === routineName) { routine = savedRoutines[i]; break; }
  }
  if (!routine) {
    console.log('Progression delta for unknown routine, ignoring: ' + routineName);
    return;
  }

  if (!isNaN(mode))      routine.progressionMode = String(mode);
  if (!isNaN(increment)) routine.weightIncrement = String(increment);

  // Triples after the header: name, target reps, target weight.
  var applied = 0;
  for (var p = 3; p + 2 < parts.length; p += 3) {
    var exName = parts[p];
    var reps   = parseInt(parts[p + 1], 10);
    var weight = parseInt(parts[p + 2], 10);

    for (var e = 0; e < (routine.exercises || []).length; e++) {
      var ex = routine.exercises[e];
      if (String(ex[0]) !== String(exName)) continue;
      if (!isNaN(reps))   ex[2] = reps;
      if (!isNaN(weight)) ex[3] = weight;
      applied++;
      break;
    }
  }

  Storage.setJSON('savedRoutines', savedRoutines);
  Storage.set('lastRoutine', routineName);
  console.log('Applied progression delta to ' + routineName + ' (' + applied + ' exercise(s))');
}

// ============================================================
// LIBRARY SYNC (v8)
//
// The phone owns the routines, so it always sends the COMPLETE library, never
// a diff. That single decision removes most of the old machinery: deletes and
// renames are implied by what is absent, the watch needs no tombstones, and
// nothing has to be remembered between saves beyond two numbers.
//
//   librarySeq    bumped on every send, so a retransmitted library is dropped
//   libraryEpoch  bumped when the user presses Save, so re-saving an unchanged
//                 library still reaches the watch
//
// Both grow monotonically and are stored in localStorage, so they survive the
// phone app being killed mid-transfer.
// ============================================================
function syncInt(key, fallback) {
  var n = parseInt(Storage.get(key, String(fallback)), 10);
  return isNaN(n) ? fallback : n;
}

// A new library generation.
//
// v7.0.4: the epoch is derived from the clock, not from a local counter. A
// counter that only ever ticks up from whatever the phone has stored can tie
// with, or lose to, the epoch a previous install already pushed to the watch --
// and then the watch is entitled to refuse the library as "already applied"
// while the phone believes it sent something new. Minutes since 2024 are unique
// across reinstalls and app upgrades and stay far below 2^31.
function syncMarkEpoch() {
  var clockEpoch = Math.floor((new Date().getTime() - 1704067200000) / 60000); // 2024-01-01
  var stored = syncInt('libraryEpoch', 0);
  Storage.set('libraryEpoch', String(Math.max(clockEpoch, stored + 1)));
}

// Build the 'T' body:
//   "T|<seq>|<epoch>|<len>|<n>|<routine>|<n>|<routine>|…"
// Each routine is preceded by its exercise count. That is what makes the stream
// unambiguous: the receiver takes a name, reads n, then n groups of six fields.
// Working the boundary out from the fields alone is ambiguous, because an
// exercise's own sets/reps/weight fields line up with exercise boundaries.
// The leading 'T' keeps the body from being mistaken for an envelope header,
// and <len> is the length of everything after it **in UTF-8 bytes**.
//
// It must be bytes, not String.length: the watch compares it with strlen(), and
// JavaScript counts UTF-16 units. A single accented letter, em dash or
// non-breaking space (easily pasted in from a web page) made every library look
// "truncated" to the watch and it was discarded even though it had arrived
// intact -- see utf8Length().
function utf8Length(str) {
  var n = 0;
  for (var i = 0; i < str.length; i++) {
    var c = str.charCodeAt(i);
    if (c < 0x80) n += 1;
    else if (c < 0x800) n += 2;
    else if (c >= 0xD800 && c <= 0xDBFF) { n += 4; i++; }  // surrogate pair
    else n += 3;
  }
  return n;
}

function buildLibraryBody() {
  var routines = Storage.getJSON('savedRoutines', []);
  var parts = [];
  for (var i = 0; i < routines.length; i++) {
    var r = routines[i];
    if (!r || !r.name || !r.exercises || r.exercises.length === 0) continue;
    var ex = [];
    for (var e = 0; e < r.exercises.length; e++) {
      var x = r.exercises[e];
      var comment = (x[5] !== undefined && String(x[5]).trim() !== '') ? String(x[5]).trim() : '-';
      ex.push([x[0], x[1], x[2], x[3], (x[4] === undefined ? 0 : x[4]), comment].join('|'));
    }
    parts.push(r.exercises.length + '|' + r.name + '|' + ex.join('|'));
  }

  var library = parts.join('|');
  var seq   = syncInt('librarySeq', 0) + 1;
  var epoch = syncInt('libraryEpoch', 0);
  Storage.set('librarySeq', String(seq));
  return 'T|' + seq + '|' + epoch + '|' + utf8Length(library) + '|' + library;
}

// Send the whole library. `reason` is only for the log.
//
// v7.0.4: `bumpEpoch` marks this library as a new generation. The watch applies
// a same-sequence transfer only if its epoch is newer, so every deliberate save
// bumps it. Doing it here (rather than in webviewclosed) also covers the resend
// caused by a HELLO mismatch, which never touches the config page.
function syncLibrary(reason, bumpEpoch) {
  if (bumpEpoch) syncMarkEpoch();
  var body = buildLibraryBody();
  console.log('SYNC: sending library (' + reason + '), ' + body.length + ' chars');
  protoSend('T', body, function(failed) {
    if (!failed) console.log('SYNC: library handed to the phone app');
  });
}

// ------------------------------------------------------------
// PER-ROUTINE LIBRARY OPERATIONS (v9)
// ------------------------------------------------------------
// The phone keeps its own copy of every routine the user has sent, in
// localStorage, purely so HELLO can repair the watch and so the config page can
// be handed the list. Each routine is stored as the serialised string the watch
// understands, so there is exactly one format in the system.
//
//   savedRoutines : [ "Name|Ex|sets|reps|weight|mod|note|...", ... ]
//
// A routine is identified by its NAME, which is what makes "send the same name
// again" replace the exercises instead of creating a duplicate.

// Add or replace one routine in this phone's copy.
//
// The entry is the CANONICAL definition ("<name>|<exercise>|...", six fields per
// exercise), never the config page's full form: the library is what the watch's
// 'T' body and the page's 'sync' param are both built from, and the progression
// header belongs to the per-routine settings, not here.
function saveRoutineLocally(name, routineStr) {
  var routines = Storage.getJSON('savedRoutines', []);
  var replaced = false;
  var entry = routineDefinition(name, routineStr);

  for (var i = 0; i < routines.length; i++) {
    var r = routines[i];
    var rname = (r && typeof r === 'object') ? r.name : String(r).split('|')[0];
    if (rname === name) { routines[i] = entry; replaced = true; break; }
  }
  if (!replaced) routines.push(entry);

  Storage.setJSON('savedRoutines', routines);
  console.log('LIB: ' + (replaced ? 'replaced' : 'added') + ' "' + name +
              '" (' + routines.length + ' routine(s) on the phone)');
}

// Remove one routine from this phone's copy.
function deleteRoutineLocally(name) {
  var routines = Storage.getJSON('savedRoutines', []);
  var kept = [];

  for (var i = 0; i < routines.length; i++) {
    var r = routines[i];
    var rname = (r && typeof r === 'object') ? r.name : String(r).split('|')[0];
    if (rname !== name) kept.push(r);
  }

  Storage.setJSON('savedRoutines', kept);
  console.log('LIB: deleted "' + name + '" (' + kept.length + ' routine(s) on the phone)');
}

// Does this phone's library hold `name`?
function hasRoutineLocally(name) {
  var routines = Storage.getJSON('savedRoutines', []);
  for (var i = 0; i < routines.length; i++) {
    var r = routines[i];
    var rname = (r && typeof r === 'object') ? r.name : String(r).split('|')[0];
    if (rname === name) return true;
  }
  return false;
}

// The definition of one routine, exactly as the watch takes it:
// "<name>|<exercise>|<sets>|<reps>|<weight>|<modifier>|<comment>|..."
//
// Entries are kept in that canonical form, so this is also what the config
// page is given (the 'sync' URL param) and what it hands back on a save.
// `routineStr` is accepted in either form: a definition, or a config page
// routine carrying the "<name>|<prog>|<inc>|" header (which is dropped, because
// progression travels in the settings ints and the watch counts fields six at a
// time).
function routineDefinition(name, routineStr) {
  if (!name) return null;
  var s = String(routineStr || '');
  if (s.indexOf(name + '|') === 0 && isProgressedDefinition(s)) {
    s = name + '|' + s.split('|').slice(3).join('|');
  }
  if (s.indexOf(name + '|') !== 0) s = s ? name + '|' + s : name + '|';
  return s;
}

// True when the field after the name is a progression mode, i.e. the string
// starts "<name>|<prog>|<inc>|<exercise>|...".
function isProgressedDefinition(str) {
  var parts = String(str).split('|');
  return parts.length >= 3 && (parts[1] === '-1' || parts[1] === '0' || parts[1] === '1');
}

// Send one routine to the watch: 'U' = upsert by name. `then` runs after it is
// in, on the wire's turn (a rename deletes the old name right after).
function sendRoutineUpsert(name, routineStr, then) {
  // "<name>|<exercise>|...", six fields per exercise, which is what the config
  // page produces and what handle_routine_upsert() derives the exercise count
  // from. No reshaping beyond making sure the name is in front.
  var body = routineDefinition(name, routineStr) || name;
  console.log('SYNC: upsert "' + name + '" (' + body.length + ' chars)');
  protoSend('U', body, function (failed) {
    if (failed) return;                  // the next connect retries
    console.log('SYNC: routine "' + name + '" sent');
    if (then) { then(); return; }        // a follow-up step announces itself
    announceApplied();
  });
}

// Delete one routine on the watch: 'D' = delete by name.
function sendRoutineDelete(name) {
  console.log('SYNC: delete "' + name + '"');
  protoSend('D', name, function (failed) {
    if (failed) return;                  // the next connect retries
    console.log('SYNC: delete of "' + name + '" sent');
    announceApplied();
  });
}

// The watch has taken the change: advance the library numbers and tell it.
//
// This MUST happen after the transfer completes, never before. The numbers say
// "this is the state I am at", so they may only move once the watch is at that
// state too — announcing them first makes HELLO disagree with a watch that has
// not applied the change yet, and the watch asks for the whole library back.
// Bumping them here is what makes the HELLO agree, so a save or a delete is
// not followed by a 'T' that would undo it.
function announceApplied() {
  syncMarkChanged();
  sendHelloNow(false);
}

// HELLO: tell the watch what this phone has, and ask what it holds.
// The watch answers with its own (seq, epoch); whichever side is behind sends.
//
// It is queued behind any transfer in flight, so a HELLO that follows a delete
// goes out after the delete rather than cancelling it.
function sendHello(forceLibrary) {
  deferToWire(function () { sendHelloNow(forceLibrary); });
}

function sendHelloNow(forceLibrary) {
  var msg = {
    SYNC_SEQUENCE: syncInt('librarySeq', 0),
    SYNC_EPOCH:    syncInt('libraryEpoch', 0)
  };
  Pebble.sendAppMessage(msg, function() {
    // forceLibrary is the "send it regardless" case, so this one is a new
    // generation too: the watch only honours a same-sequence transfer whose
    // epoch is newer than the one it has applied.
    if (forceLibrary) syncLibrary('hello', true);
  }, function(err) {
    console.log('SYNC: hello failed: ' + JSON.stringify(err));
  });
}

// Announce the library to the watch after a change, WITHOUT sending routines.
//
// Sequence and epoch are monotonic bookkeeping: they say which state this phone
// is at, not how it got there. A save or a delete has already told the watch
// exactly what changed, so bumping them here means the HELLO that follows
// agrees with the watch and no whole-library 'T' is triggered — which would
// undo the delete (a 'T' coming from a stale library is how a deleted routine
// ends up on the watch again). A mismatch still repairs a watch that was out of
// range, because that HELLO carries numbers the watch has not applied.
function syncMarkChanged() {
  var seq = syncInt('librarySeq', 0) + 1;
  Storage.set('librarySeq', String(seq));
  syncMarkEpoch();
}

// Every routine in this phone's library, in the shape the watch's 'T' body and
// the config page's 'sync' param share: { name: "<name>|<exercise>|..." }.
//
// An entry is a definition string; an object entry (a page that stored a
// routine object before v9) has its exercises read out of it instead.
function libraryRoutineDict() {
  var routines = Storage.getJSON('savedRoutines', []);
  var dict = {};
  for (var i = 0; i < routines.length; i++) {
    var r = routines[i];
    if (!r) continue;

    if (typeof r === 'object') {
      if (!r.name) continue;
      var ex = [];
      for (var e = 0; e < (r.exercises || []).length; e++) {
        var x = r.exercises[e];
        var note = (x[5] !== undefined && String(x[5]).trim() !== '') ? String(x[5]).trim() : '-';
        ex.push([x[0], x[1], x[2], x[3], (x[4] === undefined ? 0 : x[4]), note].join('|'));
      }
      dict[r.name] = r.name + '|' + ex.join('|');
      continue;
    }

    var name = String(r).split('|')[0];
    if (!name) continue;
    dict[name] = routineDefinition(name, r);
  }
  return dict;
}

// ============================================================
// PEBBLE EVENT LISTENERS
// ============================================================

// The app is up: reconcile with the watch straight away. On a first run the
// watch reports (0, 0), so this is what puts the library on a new watch — and
// the same line repairs a factory reset or a reinstall, with no special case.
Pebble.addEventListener('ready', function() {
  console.log('PebbleKit JS ready.');
  sendHello(false);
});

// 4. Message received FROM the watch
Pebble.addEventListener('appmessage', function(e) {
  var payload = e.payload;

  // --- HELLO: what the watch holds --------------------------------------
  // The watch answers every hello with its persisted (seq, epoch). If it is
  // behind this phone, the library is sent again. Because the watch reports
  // what it has APPLIED (not what it was asked about), this can never loop.
  if (payload.SYNC_SEQUENCE !== undefined || payload.SYNC_EPOCH !== undefined) {
    var watchSeq   = payload.SYNC_SEQUENCE === undefined ? 0 : payload.SYNC_SEQUENCE;
    var watchEpoch = payload.SYNC_EPOCH    === undefined ? 0 : payload.SYNC_EPOCH;
    var mySeq      = syncInt('librarySeq', 0);
    var myEpoch    = syncInt('libraryEpoch', 0);

    console.log('SYNC: watch holds (' + watchSeq + ',' + watchEpoch + '), phone has (' +
                mySeq + ',' + myEpoch + ')' + (payload.SYNC_FLAGS ? ', watch reports a failure' : ''));

    if (payload.SYNC_FLAGS) {
      // The watch refused the library outright (too many routines for its
      // buffer). Resending cannot help; say what is wrong instead of looping.
      protoNotify('The watch could not take the routines.\nThey are too big.', true);
      return;
    }

    if (watchSeq !== mySeq || watchEpoch !== myEpoch) {
      // A new generation: if the watch already applied this very sequence with a
      // lower epoch (a phone whose storage was rebuilt), it is still honoured.
      syncLibrary('watch is behind', true);
    }
    // Anything else from the watch (the report is already in flight).
    return;
  }

  // --- protocol envelope (chunked) ---
  if (payload.PROTO_MESSAGE) {
    var complete = protoReassemble(payload.PROTO_MESSAGE);
    if (!complete) return;

    protoAck(complete.id);

    if (complete.type === 'W') {
      handleReport(complete.body);
    } else {
      // The watch never sends routine definitions (the phone is the source of
      // truth). Log rather than corrupt the library if a mismatched build does.
      console.log('Ignoring inbound envelope type ' + complete.type);
    }
    return;
  }

  // --- the watch received something we sent ---
  if (payload.PROTO_ACK) {
    console.log('PROTO: watch acknowledged ' + payload.PROTO_ACK);
    return;
  }

  // --- Beta diagnostic toggle: {"DIAG":1} / {"DIAG":0} ---
  // Lets the close-URL probe be switched on without rebuilding the app.
  if (payload.DIAG !== undefined) {
    Storage.set('diagMode', payload.DIAG ? '1' : '0');
    console.log('DIAG mode ' + (payload.DIAG ? 'ON' : 'OFF') +
                ' - open Settings to use the close-URL probe');
    return;
  }

  // --- Legacy key (v6 watch firmware) ---
  if (payload.WORKOUT_SUMMARY) {
    handleReport(String(payload.WORKOUT_SUMMARY));
  }
});

// A report body is "<delta>~<report>". The delta is usually empty, so the
// common case is a single leading '~'. Splitting on the FIRST '~' is
// unambiguous because neither part can contain one.
function handleReport(body) {
  body = String(body || '');
  var sep = body.indexOf('~');
  var delta  = sep >= 0 ? body.substring(0, sep) : '';
  var report = sep >= 0 ? body.substring(sep + 1) : body;

  if (delta && delta !== '') {
    applyProgressionDelta(delta);
  }

  if (report === '') return;
  console.log('WORKOUT REPORT. Received ' + report.length + ' chars.');
  saveWorkoutLocally(report);
  exportToGoogleSheets(report);
}

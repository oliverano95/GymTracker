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
// WIRE PROTOCOL (v8)
// '<VER>|<TYPE>|<id>|<seq>/<total>|<body>'
//
// What can cross the wire, in full:
//   phone -> watch   PROTO_MESSAGE 'T'  the whole routine library (chunked)
//                    PROGRESSION_MODE / WEIGHT_INCREMENT   two ints
//                    SYNC_SEQUENCE / SYNC_EPOCH            HELLO (two ints)
//   watch -> phone   PROTO_MESSAGE 'W'  one workout report (prefixed with the
//                                       progression delta, chunked)
//                    PROTO_ACK           received chunk <id>
//                    SYNC_SEQUENCE / SYNC_EPOCH  what the watch holds (HELLO)
//
// There is no staging, no batch envelope, no retransmit timer and no
// tombstone list: the phone always sends the complete library, so the watch
// can derive deletions by itself, and both sides hold one monotonically
// increasing sequence number that says whether they agree.
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
  onDone: null
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
  protoSendNext();
}

function protoSendNext() {
  if (protoOut.index >= protoOut.queue.length) {
    console.log('PROTO: sent ' + protoOut.total + ' chunk(s) for ' + protoOut.id);
    var done = protoOut.onDone;
    protoOut.onDone = null;
    if (done) done(false);
    return;
  }

  var msg = { PROTO_MESSAGE: protoOut.queue[protoOut.index] };

  Pebble.sendAppMessage(msg, function() {
    protoOut.index++;
    protoSendNext();
  }, function(err) {
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
    var done = protoOut.onDone;
    protoOut.onDone = null;
    protoNotify('Sync failed.\nWill retry.', true);
    if (done) done(true);
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
  var syncedRoutines = Storage.get('synced_routines', '{}');
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

  // --- Two-Way Sync routines ---
  if (configData.updatedSync !== undefined) {
    Storage.setJSON('synced_routines', configData.updatedSync);
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

  // --- Routines ---
  // The page has already written the edited routine into 'savedRoutines' (which
  // is what it renders from), so pkjs can read the library straight out of its
  // own storage. Nothing has to travel through the pebblejs://close# URL, and
  // nothing has to be parked by the webview either — the webview and pkjs do
  // not share storage, which is why the old 'pendingRoutine' hand-off silently
  // did nothing at all.
  if (configData.libraryChanged) {
    syncMarkEpoch();                       // a Save always reaches the watch
    syncLibrary('config page saved');
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

function syncMarkEpoch() {
  Storage.set('libraryEpoch', String(syncInt('libraryEpoch', 0) + 1));
}

// Build the 'T' body:
//   "T|<seq>|<epoch>|<len>|<n>|<routine>|<n>|<routine>|…"
// Each routine is preceded by its exercise count. That is what makes the stream
// unambiguous: the receiver takes a name, reads n, then n groups of six fields.
// Working the boundary out from the fields alone is ambiguous, because an
// exercise's own sets/reps/weight fields line up with exercise boundaries.
// The leading 'T' keeps the body from being mistaken for an envelope header,
// and <len> is the character length of everything after it.
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
  return 'T|' + seq + '|' + epoch + '|' + library.length + '|' + library;
}

// Send the whole library. `reason` is only for the log.
function syncLibrary(reason) {
  var body = buildLibraryBody();
  console.log('SYNC: sending library (' + reason + '), ' + body.length + ' chars');
  protoSend('T', body, function(failed) {
    if (!failed) console.log('SYNC: library handed to the phone app');
  });
}

// HELLO: tell the watch what this phone has, and ask what it holds.
// The watch answers with its own (seq, epoch); whichever side is behind sends.
function sendHello(forceLibrary) {
  var msg = {
    SYNC_SEQUENCE: syncInt('librarySeq', 0),
    SYNC_EPOCH:    syncInt('libraryEpoch', 0)
  };
  Pebble.sendAppMessage(msg, function() {
    if (forceLibrary) syncLibrary('hello');
  }, function(err) {
    console.log('SYNC: hello failed: ' + JSON.stringify(err));
  });
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
      syncLibrary('watch is behind');
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

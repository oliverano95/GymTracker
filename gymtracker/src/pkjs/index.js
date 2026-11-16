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
// ROUTINE PARSER (watch -> phone, legacy strings only)
// Extracted from the appmessage handler so it is testable and
// readable independently.
// ============================================================
function parseRoutineString(syncString) {
  var parts = syncString.split('|');
  if (parts.length < 2) return null;

  var routineName = parts[0];
  var progressionMode = '-1';
  var weightIncrement = '2';
  var startIndex = 1;

  // Detect optional progression header: parts[1] will be "-1", "0", or "1"
  // FIX: use explicit set membership instead of fragile string matching
  var PROGRESSION_VALUES = { '-1': true, '0': true, '1': true };
  if (parts.length > 2 && PROGRESSION_VALUES[parts[1]] !== undefined) {
    progressionMode = parts[1];
    weightIncrement = parts[2];
    startIndex = 3;
  }

  var exercises = [];
  for (var i = startIndex; i < parts.length; i += 6) {
    // Guard against incomplete trailing fields
    if (!parts[i] || i + 5 >= parts.length) break;
    exercises.push([
      parts[i],
      parseInt(parts[i + 1], 10),
      parseInt(parts[i + 2], 10),
      parseInt(parts[i + 3], 10),
      parseInt(parts[i + 4], 10),
      parts[i + 5] === '-' ? '' : parts[i + 5]
    ]);
  }

  return {
    name:            routineName,
    exercises:       exercises,
    progressionMode: progressionMode,
    weightIncrement: weightIncrement
  };
}

function autoSyncRoutine(syncString, routineName, parsedRoutine) {
  var savedRoutines = Storage.getJSON('savedRoutines', []);
  var existingIndex = -1;
  for (var i = 0; i < savedRoutines.length; i++) {
    if (savedRoutines[i].name === routineName) { existingIndex = i; break; }
  }

  if (existingIndex >= 0) {
    savedRoutines[existingIndex] = parsedRoutine;
  } else {
    savedRoutines.push(parsedRoutine);
  }

  Storage.setJSON('savedRoutines', savedRoutines);
  Storage.set('lastRoutine', routineName);
  console.log('Auto-synced routine updated in background: ' + routineName);
}

// ============================================================
// WIRE PROTOCOL (v7.0.0)
// '<VER>|<TYPE>|<id>|<seq>/<total>|<body>'
//
// The watch sends chunks as separate PROTO_MESSAGE keys; we reassemble,
// ACK, then dispatch. Outbound chunks are queued because Pebble.sendAppMessage
// has a single in-flight outbox: each success callback sends the next chunk.
// ============================================================
var PROTO = {
  VERSION: 1,
  CHUNK: 512,
  // Must match PROTO_MAX_CHUNKS in main.c: 4 x 512 = the watch's 2048-byte
  // reassembly buffer. Batches do not need a bigger buffer because they are
  // split into one transfer per routine ('M' / 'R' / 'R' / ... / 'Z').
  MAX_CHUNKS: 4
};

var protoOut = {
  queue: [],
  index: 0,
  id: '0',
  type: 'R',
  total: 0,
  retries: 0
};

var protoIn = {
  buf: '',
  expected: 0,
  got: 0,
  id: '0',
  type: ''
};

function protoNextId() {
  var n = parseInt(Storage.get('protoNextId', '1'), 10);
  if (!n || n < 1) n = 1;
  Storage.set('protoNextId', String(n + 1));
  return String(n);
}

// Build the chunk envelopes for `body` and start sending them.
// `onDone` (optional) fires once the last chunk has left the watch/phone, which
// is what lets callers chain a multi-message batch.
function protoSend(type, body, onDone) {
  body = body || '';
  var id = protoNextId();
  var total = Math.ceil(body.length / PROTO.CHUNK) || 1;
  if (total > PROTO.MAX_CHUNKS) {
    // Refuse instead of truncating: a silently shortened routine is exactly the
    // bug class this protocol exists to remove.
    console.log('PROTO: transfer of ' + body.length + ' chars exceeds the ' +
                (PROTO.MAX_CHUNKS * PROTO.CHUNK) + '-char limit');
    if (onDone) onDone();
    return;
  }

  protoOut.queue = [];
  for (var i = 0; i < total; i++) {
    var slice = body.substr(i * PROTO.CHUNK, PROTO.CHUNK);
    protoOut.queue.push(
      PROTO.VERSION + '|' + type + '|' + id + '|' + (i + 1) + '/' + total + '|' + slice
    );
  }
  protoOut.index   = 0;
  protoOut.id      = id;
  protoOut.type    = type;
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
    if (done) done();
    return;
  }

  var msg = {};
  msg.PROTO_MESSAGE = protoOut.queue[protoOut.index];

  Pebble.sendAppMessage(msg, function() {
    protoOut.index++;
    protoSendNext();
  }, function(err) {
    if (protoOut.retries < 3) {
      protoOut.retries++;
      console.log('PROTO: chunk retry ' + protoOut.retries + ' for ' + protoOut.id);
      protoSendNext();
    } else {
      console.log('PROTO: chunk failed permanently for ' + protoOut.id + ': ' + JSON.stringify(err));
      var done = protoOut.onDone;
      protoOut.onDone = null;
      if (done) done();
    }
  });
}

function protoAck(id) {
  Pebble.sendAppMessage({ PROTO_ACK: String(id) },
    function() { },
    function(err) { console.log('PROTO: ack failed: ' + JSON.stringify(err)); });
}

// Send a batch as 'M' (open) -> 'R' per routine -> 'Z' (commit).
// Transfers are chained one at a time because the AppMessage outbox allows a
// single in-flight message. The watch buffers one routine at a time and only
// commits the library when 'Z' arrives, so a batch can never half-wipe it.
function sendBatch(batchBody) {
  var parts = batchBody.split('~').filter(function(s) { return s.length > 0; });
  var queue = [{ type: 'M', body: String(parts.length) }];
  for (var i = 0; i < parts.length; i++) queue.push({ type: 'R', body: parts[i] });
  queue.push({ type: 'Z', body: '0' });

  storageBatchQueue = queue;
  protoSendNextBatch();
}

var storageBatchQueue = [];

function protoSendNextBatch() {
  if (!storageBatchQueue || storageBatchQueue.length === 0) {
    storageBatchQueue = [];
    return;
  }
  var next = storageBatchQueue.shift();
  protoSend(next.type, next.body, function() { protoSendNextBatch(); });
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

// PEBBLE EVENT LISTENERS
// ============================================================

// 1. JS environment ready
Pebble.addEventListener('ready', function() {
  console.log('PebbleKit JS ready.');
});

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
  // platform limit (observed: 252/279 chars deliver, 672 is dropped). The
  // length logged here reveals the exact limit on first contact, so the
  // MAX_CONFIG_RESPONSE cap in index.html can be tuned precisely.
  console.log('webviewclosed: raw response length = ' + (e.response ? e.response.length : 0));

  var configData;
  try {
    configData = JSON.parse(decodeURIComponent(e.response));
  } catch (err) {
    // FIX: guard against malformed JSON instead of crashing the JS runtime.
    // A truncated response (over the platform URL limit) lands here: the
    // settings save is silently lost. The length above is the diagnostic.
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

  // --- Settings (unchanged, always tiny) ---
  // Sent as their own messages: they are two ints, so chunking them with the
  // routine would only waste an envelope round trip.
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

  // --- Routine / batch / tombstones (v7.0.0 protocol, chunked) ---
  // The config page parks the routine in localStorage instead of the
  // pebblejs://close# URL, so it is no longer limited by the URL truncation
  // that silently dropped large routines (issues #42/#43).
  //
  // A batch is sent as ONE 'M' (batch-open) message followed by one 'R' per
  // routine and a 'Z' (batch-close) message. That way the watch only ever has
  // to buffer a single routine (2 KB) instead of a whole batch (~9 KB), which
  // matters a lot on 64 KB devices. The watch validates as it goes and only
  // commits the library when 'Z' arrives.
  var routineStr = Storage.get('pendingRoutine', '');
  if (configData.pendingRoutine || (configData.routineData && configData.routineData !== '')) {
    if (!routineStr && configData.routineData) routineStr = configData.routineData;

    if (routineStr) {
      if (routineStr.indexOf('BATCH~') === 0) {
        sendBatch(routineStr.substring(6));
      } else {
        protoSend('R', routineStr);
      }
      Storage.remove('pendingRoutine');
    }
  }

  // Deletes made on the phone are the authoritative ones, so they are the only
  // deletion that is pushed to the watch (the watch's own deletes stay local).
  var pendingDeletes = Storage.getJSON('pendingDeletedRoutines', []);
  if (configData.pendingDeletes && pendingDeletes.length > 0) {
    protoSend('S', pendingDeletes.join('|'));
    Storage.setJSON('pendingDeletedRoutines', []);
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
// PEBBLE EVENT LISTENERS
// ============================================================

// 4. Message received FROM the watch
Pebble.addEventListener('appmessage', function(e) {
  var payload = e.payload;

  // --- v7.0.0 protocol envelope (chunked) ---
  if (payload.PROTO_MESSAGE) {
    var complete = protoReassemble(payload.PROTO_MESSAGE);
    if (!complete) return;

    protoAck(complete.id);

    if (complete.type === 'W') {
      // Workout report: same payload shape as the old WORKOUT_SUMMARY, so the
      // existing CSV/Sheets consumers keep working unchanged.
      console.log('WORKOUT REPORT. Received ' + complete.body.length + ' chars.');
      saveWorkoutLocally(complete.body);
      exportToGoogleSheets(complete.body);
    } else if (complete.type === 'P') {
      applyProgressionDelta(complete.body);
    } else if (complete.type === 'R' || complete.type === 'B') {
      // A v7 watch never sends routine definitions. If one arrives it is from
      // a buggy/mismatched build; log it rather than corrupt the library.
      console.log('Ignoring inbound routine envelope type ' + complete.type + ' (phone is source of truth)');
    }
    return;
  }

  // --- v7.0.0 acknowledgement of something we sent ---
  if (payload.PROTO_ACK) {
    console.log('PROTO: watch acknowledged ' + payload.PROTO_ACK);
    return;
  }

  // --- Legacy keys (v6 watch firmware) ---
  if (payload.WORKOUT_SUMMARY) {
    var rawData = payload.WORKOUT_SUMMARY;
    console.log('WORKOUT COMPLETE. Received ' + rawData.length + ' chars.');
    saveWorkoutLocally(rawData);
    exportToGoogleSheets(rawData);
  }

  if (payload.ROUTINE_DATA) {
    var syncString  = payload.ROUTINE_DATA;
    var routineName = syncString.split('|')[0];

    // Persist the raw string as a fallback for the legacy seeding path.
    var syncedRoutines = Storage.getJSON('synced_routines', {});
    syncedRoutines[routineName] = syncString;
    Storage.setJSON('synced_routines', syncedRoutines);

    var parsed = parseRoutineString(syncString);
    if (parsed) autoSyncRoutine(syncString, routineName, parsed);
  }
});

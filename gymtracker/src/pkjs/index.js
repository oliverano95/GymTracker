// src/pkjs/index.js

// ============================================================
// CONFIGURATION
// ============================================================
var CONFIG = {
  isDevMode: false,
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
  if (!rawData || rawData.length > 4096) {
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
// ROUTINE PARSER (Two-Way Sync)
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
  var synced = Storage.getJSON('synced_routines', {});

  // 1. Purge deleted keys
  if (Array.isArray(configData.deletedKeys)) {
    configData.deletedKeys.forEach(function(key) {
      delete synced[key];
    });
  }

  // 2. Apply updated/renamed keys
  if (configData.updatedSync) {
    Object.keys(configData.updatedSync).forEach(function(key) {
      synced[key] = configData.updatedSync[key];
    });
  }

  Storage.setJSON('synced_routines', synced);

  // --- Send data to watch ---
  var appMessageData = {};

  if (configData.routineData && configData.routineData !== '') {
    appMessageData['ROUTINE_DATA'] = configData.routineData;
  }
  if (configData.progressionMode !== undefined) {
    appMessageData['PROGRESSION_MODE'] = parseInt(configData.progressionMode, 10);
  }
  if (configData.weightIncrement !== undefined) {
    appMessageData['WEIGHT_INCREMENT'] = parseInt(configData.weightIncrement, 10);
  }

  if (Object.keys(appMessageData).length > 0) {
    Pebble.sendAppMessage(appMessageData,
                          function()    { console.log('Data sent to watch successfully.'); },
                          function(err) { console.log('Failed to send data to watch: ' + JSON.stringify(err)); }
    );
  }
});

// 4. Message received FROM the watch
Pebble.addEventListener('appmessage', function(e) {
  var payload = e.payload;

  // --- Workout complete ---
  if (payload.WORKOUT_SUMMARY) {
    var rawData = payload.WORKOUT_SUMMARY;
    console.log('WORKOUT COMPLETE. Received ' + rawData.length + ' chars.');
    saveWorkoutLocally(rawData);
    exportToGoogleSheets(rawData);
  }

  // --- Two-Way Sync: routine sent from watch ---
  if (payload.ROUTINE_DATA) {
    var syncString  = payload.ROUTINE_DATA;
    var routineName = syncString.split('|')[0];

    // Always persist the raw string as a safety fallback
    var syncedRoutines = Storage.getJSON('synced_routines', {});
    syncedRoutines[routineName] = syncString;
    Storage.setJSON('synced_routines', syncedRoutines);
    console.log('Two-Way Sync saved for: ' + routineName);

    // Always apply progression updates to savedRoutines
    var parsed = parseRoutineString(syncString);
    if (parsed) {
      autoSyncRoutine(syncString, routineName, parsed);
    }
  }

  // --- Voice Add Exercise: parse spoken text and beam a new exercise back to the watch ---
  if (payload.VOICE_ADD_EXERCISE) {
    var spokenText = payload.VOICE_ADD_EXERCISE.toLowerCase();
    console.log('Voice dictation received: ' + spokenText);

    // Convert written numbers to digits for easier regex matching
    var numWords = {
      'one':1,'two':2,'three':3,'four':4,'five':5,'six':6,'seven':7,
      'eight':8,'nine':9,'ten':10,'eleven':11,'twelve':12,
      'fifteen':15,'twenty':20,'thirty':30,'forty':40,'fifty':50
    };
    for (var word in numWords) {
      spokenText = spokenText.replace(new RegExp('\\b' + word + '\\b', 'gi'), numWords[word]);
    }

    var exerciseName = 'Unknown Exercise';
    var sets = 3, reps = 10, weight = 0;

    var setsMatch   = spokenText.match(/(\d+)\s*sets?/);
    if (setsMatch) sets = parseInt(setsMatch[1], 10);
    if (sets > 10) sets = 10;  // the watch stores at most 10 sets per exercise
    if (sets < 1)  sets = 1;

    var repsMatch   = spokenText.match(/(\d+)\s*reps?/);
    if (!repsMatch) repsMatch = spokenText.match(/sets? of (\d+)/);
    if (repsMatch)  reps = parseInt(repsMatch[1], 10);

    var weightMatch = spokenText.match(/(\d+)\s*(kilos?|kg|lbs?|pounds?|weight)/);
    if (weightMatch) {
      weight = parseInt(weightMatch[1], 10);
    } else if (spokenText.indexOf('bodyweight') !== -1 || spokenText.indexOf('body weight') !== -1) {
      weight = 0;
    }

    var nameMatch = spokenText.match(/^(.*?)\s*\d+\s*sets?/);
    if (nameMatch && nameMatch[1]) {
      exerciseName = nameMatch[1].trim();
    } else {
      exerciseName = spokenText.substring(0, 20).trim();
    }

    // Capitalise first letter of each word
    exerciseName = exerciseName.replace(/\b\w/g, function(l) { return l.toUpperCase(); });

    var newExString = exerciseName + '|' + sets + '|' + reps + '|' + weight + '|0|-';
    console.log('Parsed exercise: ' + newExString);

    Pebble.sendAppMessage(
      { 'NEW_EXERCISE_DATA': newExString },
      function()    { console.log('Sent parsed exercise back: ' + newExString); },
                          function(err) { console.log('Failed to send parsed exercise: ' + JSON.stringify(err)); }
    );
  }
});

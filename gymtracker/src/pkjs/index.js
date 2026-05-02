// src/pkjs/index.js

var isDevMode = true;

// 1. Tell us when the JS environment is ready
Pebble.addEventListener('ready', function(e) {
  console.log('PebbleKit JS is ready and running on the phone!');
});

// 2. Open the configuration web page when the user clicks "Settings"
Pebble.addEventListener('showConfiguration', function(e) {
  // Your live GitHub Pages link
  var myConfigUrl = isDevMode ? 'https://silentjay.github.io/solid-lamp/index_dev.html' : 'https://silentjay.github.io/solid-lamp/';
    
  // Retrieve saved settings and workout history from the phone's local memory
  var googleUrl = localStorage.getItem('googleUrl') || '';
  var googlePwd = localStorage.getItem('googlePwd') || '';
  var history = localStorage.getItem('workoutHistory') || '[]';
  
  // V5.0 NEW: Retrieve the synced routines for Two-Way Sync
  var syncedRoutines = localStorage.getItem('synced_routines') || '{}';

  // Safely pass this data to the webpage via URL parameters
  var url = myConfigUrl + '?googleUrl=' + encodeURIComponent(googleUrl) + 
            '&googlePwd=' + encodeURIComponent(googlePwd) + 
            '&history=' + encodeURIComponent(history) +
            '&sync=' + encodeURIComponent(syncedRoutines);
            
  Pebble.openURL(url);
});

// 3. Catch the data when the web page closes
Pebble.addEventListener('webviewclosed', function(e) {
  if (e.response && e.response !== 'CANCELLED' && e.response !== '[]') {
    var configData = JSON.parse(decodeURIComponent(e.response));
    
    // If the user clicked "Clear Credentials" on the site, wipe them from the phone
    if (configData.clearGoogle) {
      localStorage.removeItem('googleUrl');
      localStorage.removeItem('googlePwd');
      console.log('Google Sync credentials wiped from phone.');
    } else {
      // Otherwise, save new Google Settings (only if they typed something)
      if (configData.googleUrl !== undefined && configData.googleUrl.trim() !== "") {
        localStorage.setItem('googleUrl', configData.googleUrl);
      }
      if (configData.googlePwd !== undefined && configData.googlePwd.trim() !== "") {
        localStorage.setItem('googlePwd', configData.googlePwd);
      }
    }
    
    if (configData.clearHistory) {
      localStorage.setItem('workoutHistory', '[]');
    }

    // V5.0 NEW: Catch the updated synced routines dictionary and save it to the phone!
    if (configData.updatedSync !== undefined) {
      localStorage.setItem('synced_routines', JSON.stringify(configData.updatedSync));
    }

    // NEW: Build a combined data package to send to the watch
    var appMessageData = {};

    if (configData.routineData && configData.routineData !== "") {
      appMessageData["ROUTINE_DATA"] = configData.routineData;
    }
    
    // Grab silentjay's progression variables
    if (configData.progressionMode !== undefined) {
      appMessageData["PROGRESSION_MODE"] = parseInt(configData.progressionMode);
    }
    if (configData.weightIncrement !== undefined) {
      appMessageData["WEIGHT_INCREMENT"] = parseInt(configData.weightIncrement);
    }

    // Send the combined payload
    if (Object.keys(appMessageData).length > 0) {
      Pebble.sendAppMessage(appMessageData, function() {
        console.log("Data sent to watch successfully!");
      }, function(err) {
        console.log("Failed to send data to watch: " + JSON.stringify(err));
      });
    }
  }
});

// 4. Catch data coming FROM the watch and handle saving/exporting
Pebble.addEventListener('appmessage', function(e) {
  if (e.payload.WORKOUT_SUMMARY) {
    var rawData = e.payload.WORKOUT_SUMMARY;
    console.log("WORKOUT COMPLETE! Received Data: " + rawData);
    
    // --- DEFAULT BEHAVIOR: SAVE LOCALLY TO PHONE ---
    var historyStr = localStorage.getItem('workoutHistory') || '[]';
    var historyArr = [];
    try { historyArr = JSON.parse(historyStr); } catch(err) {}
    
    // Add the new workout (with a timestamp)
    historyArr.push({
        timestamp: new Date().getTime(),
        data: rawData
    });
    
    // Cap history at the last 30 workouts so the URL doesn't get too long to load
    if (historyArr.length > 30) historyArr.shift();
    localStorage.setItem('workoutHistory', JSON.stringify(historyArr));
    console.log("Workout saved locally to phone!");


    // --- OPTIONAL BEHAVIOR: SEND TO GOOGLE SHEETS ---
    var scriptUrl = localStorage.getItem('googleUrl');
    var scriptPwd = localStorage.getItem('googlePwd');
    
    if (scriptUrl && scriptPwd) {
      console.log("Optional Google Sheets config found. Uploading...");
      var req = new XMLHttpRequest();
      req.open("POST", scriptUrl, true);
      req.setRequestHeader("Content-Type", "application/json");
      
      req.onload = function() {
        console.log("Successfully logged workout to Google Sheets!");
      };
      
      // THE VIP PASS: Packaged exactly how your Google Apps Script expects it!
      var payload = {
        token: scriptPwd, 
        workoutData: rawData
      };
      
      // Send the secured envelope
      req.send(JSON.stringify(payload));
    }
  }
  
  // V5.0 TWO-WAY SYNC INTERCEPTOR
  if (e.payload.ROUTINE_DATA) {
      var syncString = e.payload.ROUTINE_DATA;
      var routineName = syncString.split('|')[0];

      // Save it permanently to the phone's local storage
      var syncedRoutines = JSON.parse(localStorage.getItem('synced_routines') || '{}');
      syncedRoutines[routineName] = syncString;
      localStorage.setItem('synced_routines', JSON.stringify(syncedRoutines));
      console.log("Two-Way Sync Saved for: " + routineName);

      // Auto-sync: If enabled, also update saved routine locally
      var autoSyncEnabled = localStorage.getItem('autoSyncEnabled') === 'true';
      if (autoSyncEnabled) {
          var parts = syncString.split('|');
          var exercises = [];
          var startIndex = 1;

          // Parse progression settings if present
          var progressionMode = "2";
          var weightIncrement = "2.5";
          if (parts[1] === "-1" || parts[1] === "0" || parts[1] === "1") {
              progressionMode = parts[1];
              weightIncrement = parts[2];
              startIndex = 3;
          }

          // Parse exercises
          for (var i = startIndex; i < parts.length; i += 6) {
              if (parts[i]) {
                  exercises.push([
                      parts[i],
                      parseInt(parts[i + 1]),
                      parseInt(parts[i + 2]),
                      parseInt(parts[i + 3]),
                      parseInt(parts[i + 4]),
                      parts[i + 5] === "-" ? "" : parts[i + 5]
                  ]);
              }
          }

          // Update saved routines
          var savedRoutines = JSON.parse(localStorage.getItem('savedRoutines') || '[]');
          var existingIndex = savedRoutines.findIndex(function(r) { return r.name === routineName; });
          var routineData = {
              name: routineName,
              exercises: exercises,
              progressionMode: progressionMode,
              weightIncrement: weightIncrement
          };

          if (existingIndex >= 0) {
              savedRoutines[existingIndex] = routineData;
          } else {
              savedRoutines.push(routineData);
          }

          localStorage.setItem('savedRoutines', JSON.stringify(savedRoutines));
          localStorage.setItem('lastRoutine', routineName);
          console.log("Auto-synced routine updated: " + routineName);
      }
  }
});
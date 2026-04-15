// src/pkjs/index.js

// 1. Tell us when the JS environment is ready
Pebble.addEventListener('ready', function(e) {
  console.log('PebbleKit JS is ready and running on the phone!');
});

// 2. Open the configuration web page when the user clicks "Settings"
Pebble.addEventListener('showConfiguration', function(e) {
  // Your live GitHub Pages link
  var myConfigUrl = 'https://silentjay.github.io/solid-lamp/';
  
  // Retrieve saved settings and workout history from the phone's local memory
  var googleUrl = localStorage.getItem('googleUrl') || '';
  var googlePwd = localStorage.getItem('googlePwd') || '';
  var history = localStorage.getItem('workoutHistory') || '[]';

  // Safely pass this data to the webpage via URL parameters
  var url = myConfigUrl + '?googleUrl=' + encodeURIComponent(googleUrl) + 
            '&googlePwd=' + encodeURIComponent(googlePwd) + 
            '&history=' + encodeURIComponent(history);
            
  Pebble.openURL(url);
});

// 3. Catch the data when the web page closes
Pebble.addEventListener('webviewclosed', function(e) {
  if (e.response && e.response !== 'CANCELLED' && e.response !== '[]') {
    var configData = JSON.parse(decodeURIComponent(e.response));
    
    // NEW: If the user clicked "Clear Credentials" on the site, wipe them from the phone
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

    if (configData.routineData && configData.routineData !== "") {
      var appMessageData = {
        "ROUTINE_DATA": configData.routineData
      };
      
      // Send progression settings if provided
      if (configData.progressionMode !== undefined) {
        appMessageData["PROGRESSION_MODE"] = parseInt(configData.progressionMode);
      }
      if (configData.weightIncrement !== undefined) {
        appMessageData["WEIGHT_INCREMENT"] = parseInt(configData.weightIncrement);
      }
      
      Pebble.sendAppMessage(appMessageData, function() {
        console.log("Routine sent to watch successfully!");
      }, function(err) {
        console.log("Failed to send routine to watch: " + JSON.stringify(err));
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
});
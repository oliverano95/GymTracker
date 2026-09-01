/**
 * Pebble Gym Tracker - Google Sheets Integration
 * * This script catches the webhook from your Pebble smartwatch,
 * * verifies your secret password, and organizes your workout data
 * * into clean, individual rows for each set.
 * *
 * * Per-set HR now replaces the old workout-wide Max/Avg HR, and the
 * * session HR time-series is written to a dedicated "HR_SERIES" tab.
 */

function doPost(e) {
  try {
    // 1. Check if we actually received data
    if (!e || !e.postData || !e.postData.contents) {
      return ContentService.createTextOutput("Error: No payload received.");
    }

    var parsedData = JSON.parse(e.postData.contents);

    // 2. Verify Password (Make sure this exactly matches what you type in the app!)
    var secretPassword = "CHANGE_THIS_TO_YOUR_PASSWORD";
    if (parsedData.token !== secretPassword) {
      return ContentService.createTextOutput("Access Denied: Invalid Token");
    }

    // 3. Connect to the DATA sheet
    var ss = SpreadsheetApp.getActiveSpreadsheet();
    var sheet = ss.getSheetByName('DATA');
    if (!sheet) {
      throw new Error("Cannot find a tab named 'DATA'. Check your spelling!");
    }

    // 4. Parse the workout string
    var rawString = parsedData.workoutData;
    var parts = rawString.split('|');

    var routine = parts[0];
    var date = parts[1];
    var duration = parts[2];
    var currentExercise = "";
    var setNum = 1;

    var sensation = "", accuracy = "", density = "", maxHr = "", avgHr = "";
    var startIndex = 3;
    var sensationTitles = ["Unstoppable", "Strong", "Normal", "Exhausted", "Struggled"];

    // New-format workouts carry a reserved "@HR" time-series section and encode
    // each set as 4 tokens (reps, weight, set peak HR, set avg HR). Older
    // workouts used workout-wide maxHr/avgHr + 2-token sets.
    var isNewFormat = parts.indexOf('@HR') !== -1;

    // 5. Dynamic check for extended watch stats
    var moodScore;
    if (isNewFormat) {
      moodScore = parseInt(parts[3], 10);
      sensation = sensationTitles[5 - moodScore] || "Unknown";
      accuracy = parts[4];
      density = parts[5];
      startIndex = 6;
    } else if (parts.length > 8 && !isNaN(parts[3]) && parts[3] !== "") {
      moodScore = parseInt(parts[3], 10);
      sensation = sensationTitles[5 - moodScore] || "Unknown";

      accuracy = parts[4];
      density = parts[5];
      maxHr = parts[6];   // legacy workout-wide values
      avgHr = parts[7];
      startIndex = 8;
    }

    // 6. Create empty memory arrays to hold our rows
    var rowsToAppend = [];
    var hrSeriesRows = [];

    // 7. Loop through exercises and sets, pushing them into our memory array
    for (var i = startIndex; i < parts.length; i++) {
      // Reserved HR time-series marker: the next token is "sec,bpm;sec,bpm;..."
      if (parts[i] === '@HR') {
        var series = parts[i + 1] || "";
        if (series) {
          var samples = series.split(";");
          for (var s = 0; s < samples.length; s++) {
            var pair = samples[s].split(",");
            if (pair.length === 2 && pair[0] !== "") {
              hrSeriesRows.push([date, routine, pair[0], pair[1]]);
            }
          }
        }
        break;
      }

      if (parts[i] === "") { continue; }   // safety: skip stray empty tokens

      if (isNaN(parts[i])) {
        currentExercise = parts[i];
        setNum = 1;
      } else {
        var reps = parts[i];
        var weight = parts[i + 1];

        if (isNewFormat) {
          maxHr = parts[i + 2];   // per-set peak HR
          avgHr = parts[i + 3];   // per-set average HR
        }

        rowsToAppend.push([date, routine, duration, currentExercise, setNum, reps, weight, sensation, accuracy, density, maxHr, avgHr]);

        setNum++;
        i += isNewFormat ? 3 : 1; // Skip the set's remaining tokens
      }
    }

    // 8. BATCH INSERTION: Inject the memory array into the true bottom of the sheet
    if (rowsToAppend.length > 0) {
      // Find the true last row by reading only Column A
      var columnA = sheet.getRange("A:A").getValues();
      var trueLastRow = 0;
      for (var row = columnA.length - 1; row >= 0; row--) {
        if (columnA[row][0] !== "") {
          trueLastRow = row + 1;
          break;
        }
      }

      // Inject all rows simultaneously (Extremely fast and ignores stray formatting!)
      sheet.getRange(trueLastRow + 1, 1, rowsToAppend.length, rowsToAppend[0].length).setValues(rowsToAppend);
    }

    // 9. Write the HR time-series to its own sheet (auto-created on first use).
    if (hrSeriesRows.length > 0) {
      try {
        var hrSheet = ss.getSheetByName('HR_SERIES');
        if (!hrSheet) {
          hrSheet = ss.insertSheet('HR_SERIES');
        }
        var hrColA = hrSheet.getRange("A:A").getValues();
        var hrLastRow = 0;
        for (var r = hrColA.length - 1; r >= 0; r--) {
          if (hrColA[r][0] !== "") {
            hrLastRow = r + 1;
            break;
          }
        }
        if (hrLastRow === 0) {
          hrSheet.appendRow(['Date', 'Routine', 'Time (s)', 'Heart Rate (BPM)']);
          hrLastRow = 1;
        }
        hrSheet.getRange(hrLastRow + 1, 1, hrSeriesRows.length, hrSeriesRows[0].length).setValues(hrSeriesRows);
      } catch (hrError) {
        // Don't fail the whole workout if the time-series sheet has an issue.
        var dbgSheet = ss.getSheetByName('DEBUG');
        if (dbgSheet) {
          dbgSheet.appendRow([new Date(), "HR SERIES WARN", hrError.toString()]);
        }
      }
    }

    return ContentService.createTextOutput("Success");

  } catch (error) {
    // FATAL ERROR CATCHER
    var fallbackSs = SpreadsheetApp.getActiveSpreadsheet();
    var debugSheet = fallbackSs.getSheetByName('DEBUG');
    if (debugSheet) {
      var rawPayload = e && e.postData ? e.postData.contents : "No contents";
      debugSheet.appendRow([new Date(), "CRASH ERROR", error.toString(), rawPayload]);
    }
    return ContentService.createTextOutput("Script Error Caught and Logged");
  }
}

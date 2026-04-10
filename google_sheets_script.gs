function doPost(e) {
  var sheet = SpreadsheetApp.getActiveSpreadsheet().getActiveSheet();
  
  // Parse the incoming data from the Pebble App
  var payload = JSON.parse(e.postData.contents);
  
  // 🔒 YOUR SECRET PASSWORD: Change this to whatever you want!
  var mySecretPassword = "iron"; 
  
  // Security check: Make sure it's actually your watch sending the data
  if (payload.token !== mySecretPassword) {
    return ContentService.createTextOutput("Error: Incorrect Password").setMimeType(ContentService.MimeType.TEXT);
  }
  
  // The data arrives as a pipe-delimited string: "RoutineName|Date|Duration|Ex1|Sets|Reps..."
  var rawData = payload.workoutData; 
  
  // Split the string into individual columns
  var dataArray = rawData.split('|');
  
  // Add a fresh timestamp to the beginning of the row
  dataArray.unshift(new Date());
  
  // Append the new row to the bottom of your Google Sheet!
  sheet.appendRow(dataArray);
  
  return ContentService.createTextOutput("Success").setMimeType(ContentService.MimeType.TEXT);
}

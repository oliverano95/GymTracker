![GymTracker_OnPT2](https://github.com/user-attachments/assets/fbe0bffa-2d7b-4158-b36e-307a761a8b5a)

# GymTracker - Smart Gym Tracker for Pebble

A fully-featured, standalone workout tracker for Pebble smartwatches with smart rest timers, haptic coaching, and automatic Google Sheets export. 

You can install the app directly from the App Store here: https://apps.repebble.com/0664a987078943a28f196064

You can configure your routines using the companion website here: https://oliverano95.github.io/solid-lamp/

## Features
* **Phone-Free Tracking:** Log reps and weight directly from your wrist.
* **Smart Rest Coach:** Automatic rest timers with custom haptic feedback.
* **Routine Builder:** Create up to 7 custom routines via the web interface.
* **Data Privacy:** Workouts are saved locally to your phone for CSV export, with an optional Google Sheets auto-sync for power users.

## Developer Note & Disclaimer
This app was built entirely with the help of AI (Gemini). I have done my best to ensure the code is as secure, lean, and optimized as possible. However, if you are an experienced C or Pebble developer and see ways to make the architecture or memory management even better, I am always eager to learn! 

Please feel free to open an Issue, submit a Pull Request, or reach out with any feedback or improvements.

## ⌚ How to Use the App

### 1. Building Your Routine (Phone)
1. Open the **Pebble App** on your phone, go to your watchfaces/apps, and click the **Settings** gear icon next to this app.
2. This opens the **Companion Website**. Use the dropdowns to add exercises, target sets, reps, and weight.
3. Drag and drop exercises using the `≡` handle to reorder them.
4. Click **Save & Send to Pebble**. The routine will instantly beam to your watch via Bluetooth!

### 2. Working Out (Watch)
Leave your phone in your locker! All controls are designed to be used without looking, using the Pebble's physical buttons:
* **Up / Down Buttons:** Adjust the actual reps or weight you lifted.
* **Short-Press Select:** Toggle between editing your Reps and your Weight.
* **Long-Press Select (500ms):** Log the set! The watch will give a double-vibration and start your automatic Rest Timer.
* **Skip Rest:** If you are ready to lift before the timer finishes, just short-press Select to skip the rest period.

### 3. Downloading Your Data (Local CSV)
By default, every completed workout is saved to your phone's local storage.
1. Open the app's **Settings** page on your phone.
2. Under the **Workout History** section, click **Download CSV**.
3. A `.csv` file containing your raw workout data will download to your device, perfect for Excel, Apple Numbers, or any custom tracking software you use.

## ☁️ Advanced: Google Sheets Cloud Sync Setup

For power users, you can bypass the manual CSV download and have the watch beam your workouts directly to a private Google Sheet the moment you finish your last set.

### Step 1: Prepare the Google Sheet
1. Create a brand new [Google Sheet](https://sheets.google.com).
2. Name it something like "Pebble Gym Logs".
3. In the very first row of your sheet (Row 1), type out the following headers across the columns: **Date** | **Routine** | **Duration (s)** | **Exercise** | **Set #** | **Reps** | **Weight**.
4. In the top menu, click **Extensions** > **Apps Script**.

### Step 2: Add the Code
1. Delete any code currently in the script editor.
2. In this GitHub repository, open the file named `google_sheets_script.gs`.
3. Copy all the code from that file and paste it into your Google Apps Script editor.
4. On line 8 of the code, change the `mySecretPassword` variable to a secure password of your choice.
5. Click the **Save** icon (the floppy disk).

### Step 3: Deploy as a Web App
1. In the top right corner of the Apps Script editor, click the blue **Deploy** button > **New deployment**.
2. Click the gear icon ⚙️ next to "Select type" and choose **Web app**.
3. Fill out the deployment settings exactly like this:
   * **Description:** Pebble Sync
   * **Execute as:** Me (your email)
   * **Who has access:** Anyone *(Don't worry, your secret password protects it!)*
4. Click **Deploy**. *(Google will ask you to authorize access. Click "Review permissions", select your account, click "Advanced", and click "Go to project" to allow it).*
5. Under "Web app", copy the **Web App URL** (It should end in `/exec`).

### Step 4: Connect to the Pebble App
1. Open the app's **Settings** configuration page on your phone.
2. Scroll down to **Optional: Google Sheets Auto-Export**.
3. Paste your **Web App URL** into the first box.
4. Type your **Secret Password** (the one you set in Step 2) into the second box.
5. Click **Save & Send to Pebble**.

You're done! Your phone will securely remember this link. The next time you finish a workout on your watch, it will automatically populate the workout in your Google Sheet!

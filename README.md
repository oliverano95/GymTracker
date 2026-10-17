![GymTracker_OnPT2](https://github.com/user-attachments/assets/fbe0bffa-2d7b-4158-b36e-307a761a8b5a)

# GymTracker - Smart Gym Tracker for Pebble

A fully-featured, standalone workout tracker for Pebble smartwatches with smart rest timers, advanced modifiers (Drop Sets & Supersets), haptic coaching, and automatic Google Sheets export. 

You can install the app directly from the App Store here: https://apps.repebble.com/0664a987078943a28f196064

You can configure your routines using the companion website here: [https://oliverano95.github.io/GymTracker/](https://oliverano95.github.io/GymTracker/)

## ✨ Features
* **Phone-Free Tracking:** Log reps and weight directly from your wrist.
* **Advanced Modifiers:** Support for **Drop Sets** (automatically reduces target weight and doubles sets) and **Supersets** (links exercises together and bounces between them with custom transition timers).
* **Auto-Progression:** Automatically increases targets when workout goals are met. You can set it to auto-increase weight or reps for your next workout if you successfully hit your targets. 
* **Smart Rest Coach:** Automatic rest timers with custom haptic feedback for regular sets, exercises, and superset transitions.
* **On-Watch Variations:** Double-click to append tags like `(Seated)`, `(Dumbbell)`, or `(Cable)` to exercises on the fly without touching your phone.
* **Add Set On the Fly:** The double-click menu also offers **Add Set** to bolt an extra set onto the current exercise mid-workout (supersets and giant sets grow together; drop sets grow by a pair).
* **Routine Builder:** Create up to 7 custom routines via the web interface.
* **Data Privacy:** Workouts are saved locally to your phone for CSV export, with an optional Google Sheets auto-sync for power users.

## 🤝 Developer Note & Credits
This app was built entirely with the help of AI (Gemini). I have done my best to ensure the code is as secure, lean, and optimized as possible for the Pebble's memory constraints. 

**Special Shoutout:** A massive thank you to GitHub user **@silentjay** for building and contributing the Auto-Progression feature! 

If you are an experienced C or Pebble developer and see ways to make the architecture even better, I am always eager to learn. Please feel free to open an Issue, submit a Pull Request, or reach out with any feedback.

## ⌚ How to Use the App

### 1. Building Your Routine (Phone)
1. Open the **Pebble App** on your phone, go to your watchfaces/apps, and click the **Settings** gear icon next to this app.
2. This opens the **Companion Website**. Use the dropdowns to add exercises, target sets, reps, and weight.
3. *Optional:* Set an advanced modifier (Drop Set or Superset) for your exercise.
4. Drag and drop exercises using the `≡` handle to reorder them.
5. Click **Save & Send to Pebble**. The routine will instantly beam to your watch via Bluetooth!

### 2. Working Out (Watch)
Leave your phone in your locker! All controls are designed to be used without looking, using the Pebble's physical buttons:
* **Up / Down Buttons:** Adjust the actual reps or weight you lifted.
* **Short-Press Select:** Toggle between editing your Reps and your Weight.
* **Double-Click Select:** Open the "Variations" menu to add modifiers to your current exercise name.
* **Long-Press Select (500ms):** Log the set! The watch will give a double-vibration and start your automatic Rest Timer.
* **Skip Rest:** If you are ready to lift before the timer finishes, just short-press Select to skip the rest period.

### 3. Downloading Your Data (Local CSV)
By default, every completed workout is saved to your phone's local storage.
1. Open the app's **Settings** page on your phone.
2. Under the **Workout History** section, click **Download CSV**.
3. A `.csv` file containing your raw workout data will download to your device, perfect for Excel, Apple Numbers, or any custom tracking software you use.

## Screenshots

### Emery
| Workout Execution | Rest Timer | Routine Selector | Routine Selector | App Settings |
| :---: | :---: | :---: | :---: | :---: |
| <img width="200" alt="1_Emery_GymTracker_ExerciseWindow" src="https://github.com/user-attachments/assets/8e0ba7f5-e8c6-4f05-983d-87344572b9fd" /> | <img width="200" alt="2_Emery_GymTracker_RestWindow" src="https://github.com/user-attachments/assets/5df3bdf7-9a44-441b-a4b3-4d2d9c88ce69" /> | <img width="200" alt="3_Emery_GymTracker_RoutineWindow2" src="https://github.com/user-attachments/assets/bb86e32a-91e2-4caa-840c-5e8d0990b6ec" /> | <img width="200" alt="4_Emery_GymTracker_RoutineWindow" src="https://github.com/user-attachments/assets/53086efd-9e6d-4f52-90ea-03d576a2a30b" /> | <img width="200" alt="5_Emery_GymTracker_SettingsWindow" src="https://github.com/user-attachments/assets/f162c33a-bdef-4e03-b1c4-81c7ad09e826" /> |

### Diorite
| Workout Execution | Rest Timer | Routine Selector | Routine Selector | App Settings |
| :---: | :---: | :---: | :---: | :---: |
| <img width="144" alt="1_Diorite_GymTracker_ExerciseWindow" src="https://github.com/user-attachments/assets/818f120c-0f22-46fc-9218-318e6d246bcf" /> | <img width="144" alt="2_Diorite_GymTracker_RestWindow" src="https://github.com/user-attachments/assets/0741bbce-5359-4767-b3ea-ac2051105c80" /> | <img width="144" alt="3_Diorite_GymTracker_RoutineWindow2" src="https://github.com/user-attachments/assets/0598d477-6659-46db-bf5c-1ea3b98b7434" /> | <img width="144" alt="4_Diorite_GymTracker_RoutineWindow" src="https://github.com/user-attachments/assets/4b04778d-d21d-4ee7-be02-04a92596256c" /> | <img width="144" alt="5_Diorite_GymTracker_SettingsWindow" src="https://github.com/user-attachments/assets/d018d21f-490e-4a5b-9996-3b7511a13714" /> |

### Gabbro
| Workout Execution | Rest Timer | Routine Selector | Routine Selector | App Settings |
| :---: | :---: | :---: | :---: | :---: |
| <img width="260" height="260" alt="1_Gabbro_GymTracker_ExerciseWindow" src="https://github.com/user-attachments/assets/bb6829d6-a5fa-4900-9c3c-6d7a85010da7" /> | <img width="260" height="260" alt="2_Gabbro_GymTracker_RestWindow" src="https://github.com/user-attachments/assets/95fd1e74-87e2-4e0f-be38-ffe291450743" /> | <img width="260" height="260" alt="3_Gabbro_GymTracker_RoutineWindow2" src="https://github.com/user-attachments/assets/16840707-6f10-4b6c-b552-e9ff8a10ba34" /> | <img width="260" height="260" alt="4_Gabbro_GymTracker_RoutineWindow" src="https://github.com/user-attachments/assets/83051461-95dd-4207-8937-7932d13f299e" /> | <img width="260" height="260" alt="5_Gabbro_GymTracker_SettingsWindow" src="https://github.com/user-attachments/assets/2277a49d-b21f-4942-9266-58f4df737c7a" /> |

### Chalk
| Workout Execution | Rest Timer | Routine Selector | Routine Selector | App Settings |
| :---: | :---: | :---: | :---: | :---: |
| <img width="180" height="180" alt="1_Chalk_GymTracker_ExerciseWindow" src="https://github.com/user-attachments/assets/bd5c482a-2588-4956-a3c5-77d9dd793e3d" /> | <img width="180" height="180" alt="2_Chalk_GymTracker_RestWindow" src="https://github.com/user-attachments/assets/5a38638a-7a60-44b9-b236-eacc1aa2e03e" /> | <img width="180" height="180" alt="3_Chalk_GymTracker_RoutineWindow2" src="https://github.com/user-attachments/assets/8eb73c9f-e4e4-4fe7-83f1-78d5d6022adf" /> | <img width="180" height="180" alt="4_Chalk_GymTracker_RoutineWindow" src="https://github.com/user-attachments/assets/b41b80cd-2269-4b77-84da-7f57657686bb" /> | <img width="180" height="180" alt="5_Chalk_GymTracker_SettingsWindow" src="https://github.com/user-attachments/assets/a4669477-e804-486e-b85f-ca1c6da22de0" /> |

## ☁️ Advanced: Google Sheets Cloud Sync Setup

For power users, you can bypass the manual CSV download and have the watch beam your workouts directly to a private Google Sheet the moment you finish your last set.

We highly recommend using our official Dashboard Template, but a manual setup is also available if you prefer building your own spreadsheet from scratch.

### Option 1: Official Dashboard Template (Recommended)

We have built a fully automated Google Sheets Dashboard that calculates your Estimated 1RM, tracks your total volume, and visualizes your heart rate and consistency over time.

**Step 1: Copy the Template**

1. Click here to automatically generate a private copy of the template in your Google Drive: **[Make a copy of the GymTracker Dashboard](https://docs.google.com/spreadsheets/d/1OMDMDUWcBv-GxIOSn7Y4pr-RS03WCVGdcy6IUYD48H4/copy)**

**Step 2: Secure Your Script**

1. In the top menu of your new Google Sheet, click **Extensions** > **Apps Script**.
2. Locate the `var secretPassword = "CHANGE_THIS_TO_YOUR_PASSWORD";` variable near the top of the code.
3. Change the text inside the quotes to a secure password of your choice.
4. Click the **Save** icon (the floppy disk).

**Step 3: Deploy as a Web App**

1. In the top right corner of the Apps Script editor, click the blue **Deploy** button > **New deployment**.
2. Click the gear icon ⚙️ next to "Select type" and choose **Web app**.
3. Fill out the deployment settings exactly like this:
* **Description:** GymTracker Sync
* **Execute as:** Me (your email)
* **Who has access:** Anyone *(This is required for the watch to reach it!)*


4. Click **Deploy**. *(Google will ask you to authorize access. Click "Review permissions", select your account, click "Advanced", and click "Go to project" to allow it).*
5. Under "Web app", copy the **Web App URL** (It should end in `/exec`).

**Step 4: Connect to the Pebble App**

1. Open the app's **Settings** configuration page on your phone.
2. Scroll down to **Google Sheets Auto-Export**.
3. Paste your **Web App URL** into the first box.
4. Type your **Secret Password** (the one you set in Step 2) into the second box.
5. Click **Save & Send to Pebble**.

---

### Option 2: Manual Setup (Fallback)

If you prefer to start with a blank canvas and build your own data processing logic, follow these steps.

**Step 1: Prepare the Google Sheet**
1. Create a brand new [Google Sheet](https://sheets.google.com).
2. Name it something like "Pebble Gym Logs".
3. In the very first row of your sheet (Row 1), type out the following headers across the columns: **Date** | **Routine** | **Duration (s)** | **Exercise** | **Set #** | **Reps** | **Weight**.
4. If you want to include any of the additional fields: Sensation, Accuracy, Density, Per-Set Peak HR, or Per-Set Avg HR add them to Row 1 and uncomment the corresponding code (see step 5 below). The script can also append the session HR time-series.
5. In the top menu, click **Extensions** > **Apps Script**.

**Step 2: Add the Code**
1. Delete any code currently in the script editor.
2. In this GitHub repository, open the file named [`google_sheets_script.gs`](https://github.com/oliverano95/GymTracker/blob/main/google_sheets_script.gs).
3. Copy all the code from that file and paste it into your Google Apps Script editor.
4. On line 15 of the code, change the `mySecretPassword` variable to a secure password of your choice.
5. If you want to include any of the additional fields: Sensation, Accuracy, Density, Per-Set Peak HR, or Per-Set Avg HR, uncomment the corresponding lines in the code. To also write the session HR time-series to your sheet, uncomment the `HR Time Series` block in the script.
6. Click the **Save** icon (the floppy disk).

**Step 3: Deploy as a Web App**
1. In the top right corner of the Apps Script editor, click the blue **Deploy** button > **New deployment**.
2. Click the gear icon ⚙️ next to "Select type" and choose **Web app**.
3. Fill out the deployment settings exactly like this:
   * **Description:** Pebble Sync
   * **Execute as:** Me (your email)
   * **Who has access:** Anyone *(Don't worry, your secret password protects it!)*
4. Click **Deploy**. *(Google will ask you to authorize access. Click "Review permissions", select your account, click "Advanced", and click "Go to project" to allow it).*
5. Under "Web app", copy the **Web App URL** (It should end in `/exec`).

**Step 4: Connect to the Pebble App**
1. Open the app's **Settings** configuration page on your phone.
2. Scroll down to **Optional: Google Sheets Auto-Export**.
3. Paste your **Web App URL** into the first box.
4. Type your **Secret Password** (the one you set in Step 2) into the second box.
5. Click **Save & Send to Pebble**.

You're done! Your phone will securely remember this link. The next time you finish a workout on your watch, it will automatically populate the workout in your Google Sheet!

## License
This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

# Pebble SDK Emulator

How to use the local Pebble SDK emulator for testing GymTracker without a physical watch.

## Prerequisites

```bash
pebble --version   # should print: Pebble Tool v5.0.40, SDK 4.33.1
```

## Core Commands

### Build & Install

```bash
cd gymtracker/

pebble build                     # compile C + JS → build/gymtracker.pbw
pebble install --emulator basalt # install + launch on emulator
pebble install                   # install on connected physical watch
```

`--emulator` options: `basalt` (Pebble Time), `chalk` (Time Round), `diorite` (Pebble 2), `emery`, `aplite` (original Pebble — not supported by GymTracker).

### Logs

```bash
pebble logs --emulator basalt    # stream logs from emulator (Ctrl+C to stop)
```

Logs print `app_log()` output from C code and `console.log()` from pkjs/index.js.

### Screenshot

```bash
pebble screenshot --emulator basalt screenshot.png
```

Opens the image automatically (disable with `--no-open`).

### Send AppMessage (simulate phone → watch)

```bash
# Key 0 = ROUTINE_DATA (first entry in package.json appKeys array)
pebble send-app-message --emulator basalt \
  --string 0="Push Day|-1|2|Bench Press|3|10|60|0|-|OHP|3|8|50|0|-"
```

This sends a message to the watch's `inbox_received_callback` in `main.c`. Keys are numeric indices matching the order in `package.json` `appKeys`:

### Emulator Controls

```bash
pebble emu-button --emulator basalt SELECT  # press SELECT button
pebble emu-button --emulator basalt UP      # press UP
pebble emu-button --emulator basalt DOWN    # press DOWN
pebble emu-button --emulator basalt BACK    # press BACK
```

### Kill Emulator

```bash
pebble kill --emulator basalt
```

## Typical Workflow

```bash
# 1. Build
pebble build

# 2. Install + launch on emulator
pebble install --emulator basalt

# 3. Watch logs
pebble logs --emulator basalt &

# 4. Send test data (key 0 = ROUTINE_DATA)
pebble send-app-message --emulator basalt \
  --string 0="Test|-1|2|Squat|4|8|100|0|-"

# 5. Take screenshot
pebble screenshot --emulator basalt test.png

# 6. Kill emulator when done
pebble kill --emulator basalt
```

## What the Emulator Can and Can't Test

### Can test
- C code logic (routine parsing, exercise storage, UI rendering)
- AppMessage handling (`inbox_received_callback`)
- Button navigation
- Persistent storage (`persist_write_*` / `persist_read_*`)
- Visual layout (screenshots)

### Can't test
- Bluetooth connection to phone (QEMU + pypkjs can partially simulate this)
- Config page → phone flow (requires real phone or QEMU + pypkjs)
- Health API (step counting, heart rate)
- Voice dictation
- Accelerometer/compass hardware

## GymTracker Message Keys

From `package.json` (appKeys array — index = key number for `send-app-message`):

| Index | Key | Direction | Purpose |
|-------|-----|-----------|---------|
| 0 | `ROUTINE_DATA` | Phone → Watch | Send routine string (name, exercises, progression) |
| 1 | `WORKOUT_SUMMARY` | Watch → Phone | Workout completion data |
| 2 | `PROGRESSION_MODE` | Phone → Watch | Progression mode setting |
| 3 | `WEIGHT_INCREMENT` | Phone → Watch | Weight increment setting |
| 4 | `VOICE_ADD_EXERCISE` | Watch → Phone | Voice-dictated exercise |
| 5 | `NEW_EXERCISE_DATA` | Watch → Phone | New exercise from user |

## Testing with QEMU + pypkjs (full stack)

For testing the phone ↔ watch communication without a real phone:

```bash
# Install with pypkjs (starts both emulator and pypkjs WebSocket)
pebble install --emulator basalt --pypkjs --platform basalt --logs
```

This connects the emulator to a local pypkjs instance, which runs `pkjs/index.js` and can serve the config page.

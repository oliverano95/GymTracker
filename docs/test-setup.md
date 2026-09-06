# Test Environment Setup

Prerequisites for running GymTracker's automated tests.

## Two Test Layers

| Layer | What it tests | Needs Pebble SDK? | Needs emulator? |
|-------|--------------|-------------------|-----------------|
| `test-config-page.js` | Config page UI, localStorage, JS functions | No | No |
| `emulator.js` | C code, AppMessage, full stack on emulator | Yes | Yes |

## Layer 1: Config Page Tests (test-config-page.js)

### Requirements

- **Node.js** >= 18
- **Playwright** (bundled Chromium, no separate browser install needed)

### Setup

```bash
cd tests/
npm install
```

### Verify

```bash
npx playwright test test-config-page.js
# Should print: 7 passed
```

No Pebble SDK, no emulator, no watch needed. Tests open `index_dev.html` directly in headless Chromium.

## Layer 2: Emulator Tests (emulator.js)

### Requirements

1. **Node.js** >= 18
2. **Pebble SDK** installed and on PATH
3. **Pebble CLI** accessible as `pebble`

### Installing the Pebble SDK

```bash
# Install the Pebble tool
pip install pebble-tool

# Install SDK 4.33.1 (the version GymTracker uses)
pebble sdk install 4.33.1

# Set it as active
pebble sdk activate 4.33.1
```

### Verify

```bash
pebble --version
# Expected: Pebble Tool v5.0.40, SDK 4.33.1

pebble sdk list
# Should show installed SDKs with 4.33.1 marked as active

# Verify build toolchain works
cd gymtracker/
pebble build
# Should print: 'build' finished successfully
```

### What the emulator tests verify

| Test | What it does |
|------|-------------|
| Build | `pebble build` compiles C + JS into `build/gymtracker.pbw` |
| Install + Launch | `pebble install --emulator basalt` starts emulator, app loads, produces heap usage logs |
| Send AppMessage | `pebble send-app-message --string 0="..."` sends routine data to watch's `inbox_received_callback` |
| Screenshot | `pebble screenshot` captures the emulator display to a PNG file |

### What the emulator CAN'T test

- Bluetooth connection to a real phone (use QEMU + pypkjs for partial simulation)
- Config page → phone data flow (requires `pebblejs://close#` URL protocol)
- Health API (step counting, heart rate sensors)
- Voice dictation
- Accelerometer / compass hardware

### Emulator platforms

GymTracker supports these platforms. The emulator defaults to `basalt` (Pebble Time):

| Platform | Pebble model | Color | Notes |
|----------|-------------|-------|-------|
| `basalt` | Pebble Time | Yes | Default, best for testing |
| `chalk` | Pebble Time Round | Yes | Round display |
| `diorite` | Pebble 2 | No | Black and white |
| `emery` | Pebble Time 2 | Yes | Larger screen |
| `gabbro` | — | Yes | Current SDK target |

## Quick Start (Both Layers)

```bash
# 1. Install test dependencies
cd tests/
npm install

# 2. Run config page tests (no SDK needed)
npx playwright test test-config-page.js

# 3. Install Pebble SDK (if not already)
pip install pebble-tool
pebble sdk install 4.33.1

# 4. Run emulator tests
npx playwright test emulator.js

# 5. Run all tests
npx playwright test
```

## Troubleshooting

### "pebble: command not found"

The Pebble tool isn't on your PATH. Check where it was installed:

```bash
pip show pebble-tool | grep Location
# Add that directory's bin/ to your PATH
```

### "SDK 4.33.1 not found"

```bash
pebble sdk list          # see what's installed
pebble sdk install 4.33.1  # install the right version
```

### Emulator won't start

```bash
# Kill any stale emulator processes
pebble kill --emulator basalt

# Try again
pebble install --emulator basalt
```

### "No package.json found" in tests/

```bash
cd tests/
npm install
```

### Playwright browsers not installed

Playwright bundles its own Chromium. If it's missing:

```bash
cd tests/
npx playwright install chromium
```

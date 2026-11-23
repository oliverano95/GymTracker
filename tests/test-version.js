// @ts-check
// Version consistency.
//
// The watch shows APP_VERSION in the Settings window header, but there is no
// "version" in a Pebble app's appinfo that C code can read at compile time, so
// it is a string literal in main.c. This suite keeps that literal and
// package.json's "version" in lockstep: bumping one without the other fails.
//
// Run with:
//   cd tests && npx playwright test test-version.js

const { test, expect } = require('@playwright/test');
const fs = require('fs');
const path = require('path');

const ROOT = path.resolve(__dirname, '..');
const MAIN_C = path.join(ROOT, 'gymtracker', 'src', 'c', 'main.c');
const PACKAGE_JSON = path.join(ROOT, 'gymtracker', 'package.json');

function watchVersion() {
  const src = fs.readFileSync(MAIN_C, 'utf8');
  const m = src.match(/^#define\s+APP_VERSION\s+"([^"]+)"/m);
  expect(m, 'APP_VERSION is not defined in main.c').toBeTruthy();
  return m[1];
}

function packageVersion() {
  return JSON.parse(fs.readFileSync(PACKAGE_JSON, 'utf8')).version;
}

test.describe('App version', () => {
  test('APP_VERSION matches package.json', () => {
    expect(watchVersion()).toBe(packageVersion());
  });

  test('the version is a valid Pebble versionLabel', () => {
    // appinfo.json only allows digits and dots, each part 0-255.
    const v = watchVersion();
    expect(v).toMatch(/^\d+(\.\d+){1,2}$/);
    for (const part of v.split('.')) {
      expect(Number(part)).toBeGreaterThanOrEqual(0);
      expect(Number(part)).toBeLessThanOrEqual(255);
    }
  });

  test('the Settings header draws the version', () => {
    const src = fs.readFileSync(MAIN_C, 'utf8');
    expect(src).toMatch(/settings_version_text\s*=\s*build_text_layer/);
    expect(src).toMatch(/text_layer_set_text\(s_app\.ui\.settings_version_text,\s*APP_VERSION\)/);
    // The layer has to be torn down with the window, or it leaks on every open.
    expect(src).toMatch(/text_layer_destroy\(s_app\.ui\.settings_version_text\)/);
  });
});

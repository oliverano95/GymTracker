// @ts-check
const { defineConfig } = require('@playwright/test');
const path = require('path');

module.exports = defineConfig({
  testDir: '.',
  // Only suites that exist on this branch. (test-protocol.js ships with the
  // watch/pkjs changes on `beta`; add it here when those are merged.)
  testMatch: ['test-config-page.js', 'emulator.js', 'test-csv-export.js', 'test-e2e.js', 'test-delete-rename.js'],
  timeout: 60_000,
  expect: { timeout: 5_000 },
  fullyParallel: false,
  retries: 0,
  reporter: 'list',
  use: {
    headless: true,
    viewport: { width: 400, height: 800 },
    actionTimeout: 5_000,
    ignoreHTTPSErrors: true,
  },
  projects: [
    {
      name: 'chromium',
      use: { browserName: 'chromium' },
    },
  ],
});

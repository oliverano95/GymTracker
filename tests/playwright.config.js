// @ts-check
const { defineConfig } = require('@playwright/test');
<<<<<<< HEAD
const path = require('path');

module.exports = defineConfig({
  testDir: '.',
  testMatch: ['test-config-page.js', 'emulator.js', 'test-issue-46.js'],
=======

module.exports = defineConfig({
  testDir: '.',
  testMatch: ['test-issue-46.js'],
>>>>>>> e9e0d45 (test: add Playwright harness for tombstone persistence (issue #46))
  timeout: 60_000,
  expect: { timeout: 5_000 },
  fullyParallel: false,
  retries: 0,
  reporter: 'list',
  use: {
    headless: true,
<<<<<<< HEAD
    viewport: { width: 400, height: 800 },
=======
    viewport: { width: 1280, height: 720 },
>>>>>>> e9e0d45 (test: add Playwright harness for tombstone persistence (issue #46))
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

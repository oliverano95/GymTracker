// @ts-check
const { defineConfig } = require('@playwright/test');
  testDir: '.',
  testMatch: ['test-config-page.js', 'emulator.js', 'test-chunked-transfer.js', 'test-pkjs-chunking.js', 'test-beta-bugfix.js', 'test-c-parser.js'],
  testMatch: ['test-config-page.js', 'emulator.js'],
  testDir: '.',
  testMatch: ['test-chunked-transfer.js', 'test-pkjs-chunking.js', 'emulator.js', 'test-beta-bugfix.js', 'test-c-parser.js'],
  timeout: 60_000,
  expect: { timeout: 5_000 },
  fullyParallel: false,
  retries: 0,
  reporter: 'list',
  use: {
    headless: true,
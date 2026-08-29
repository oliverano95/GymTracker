// @ts-check
// Playwright wrapper that compiles and runs the host-side C unit tests for
// the shared routine parser (routine_parse.c). This is the fast, dependency-
// free way to verify the Bug-2 fix (C parser must skip the prog/inc header)
// without booting the Pebble emulator.
//
// Run: cd tests && npx playwright test test-c-parser.js

const { test, expect } = require('@playwright/test');
const { execSync } = require('child_process');
const path = require('path');
const fs = require('fs');

const ROOT = path.resolve(__dirname, '..');
const SRC = path.join(ROOT, 'gymtracker', 'src', 'c');
const BIN = path.join('/tmp', `c_parser_test_${process.pid}`);
const EXE = path.join('/tmp', `c_parser_test_bin_${process.pid}`);

test.describe('C routine parser (routine_parse.c)', () => {
  test('C6: header-skip + legacy parsing via host compile', () => {
    fs.mkdirSync('/tmp', { recursive: true });

    const cc = process.env.CC || 'cc';
    const compile = execSync(
      `${cc} -I ${SRC} ${path.join(ROOT, 'tests', 'test-c-parser.c')} ${path.join(SRC, 'routine_parse.c')} -o ${EXE}`,
      { encoding: 'utf8' }
    );

    const run = execSync(EXE, { encoding: 'utf8' });
    console.log(run);
    expect(run).toContain('PARSER TESTS PASSED');
    expect(run).not.toContain('FAIL:');
  });

  test.afterAll(() => {
    try { fs.unlinkSync(BIN); } catch {}
    try { fs.unlinkSync(EXE); } catch {}
  });
});
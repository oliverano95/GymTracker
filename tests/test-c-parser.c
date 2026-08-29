/*
 * Host-side unit tests for the shared routine parser.
 *
 * Compiles the SAME source the watch uses (gymtracker/src/c/routine_parse.c)
 * on the host and asserts the header-skip/fix behavior that was broken in
 * beta testing. No Pebble toolchain required.
 *
 * Build & run:
 *   cc -I ../gymtracker/src/c tests/test-c-parser.c ../gymtracker/src/c/routine_parse.c -o /tmp/c_parser_test && /tmp/c_parser_test
 *
 * Or via Playwright:
 *   cd tests && node test-c-parser.js
 */

#include "routine_parse.h"

#include <stdio.h>
#include <string.h>

#define rp_clear(r) memset(r, 0, sizeof(*(r)))

static int g_fail = 0;

#define CHECK(cond, msg) \
  do { if (!(cond)) { printf("FAIL: %s\n", msg); g_fail = 1; } else { printf("ok:   %s\n", msg); } } while (0)

int main(void) {
  RpResult r;

  /* THE BUG: prog/inc header used to be parsed as exercise[0]'s fields. */
  routine_parse_string("Big R|-1|2|Bench Press|3|10|60|0|-|OHP|3|8|40|0|warmup", &r);
  CHECK(strcmp(r.routine_name, "Big R") == 0, "name extracted");
  CHECK(r.total_exercises == 2, "two exercises parsed");
  CHECK(strcmp(r.exercises[0].name, "Bench Press") == 0,
        "ex0 name is exercise (not 'prog' header)");
  CHECK(r.exercises[0].target_sets == 3, "ex0 sets = 3");
  CHECK(r.exercises[0].target_reps == 10, "ex0 reps = 10");
  CHECK(r.exercises[0].target_weight == 60, "ex0 weight = 60");
  CHECK(r.exercises[0].modifier == 0, "ex0 modifier = 0");
  CHECK(strcmp(r.exercises[1].name, "OHP") == 0, "ex1 name = OHP");
  CHECK(r.exercises[1].modifier == 0 && strcmp(r.exercises[1].comment, "warmup") == 0,
        "ex1 comment = warmup");

  /* Legacy format (no header): first token is name, then ex starts at 1. */
  rp_clear(&r);
  routine_parse_string("Legacy|Squat|3|10|80|0|-|", &r);
  CHECK(strcmp(r.routine_name, "Legacy") == 0, "legacy name");
  CHECK(r.total_exercises == 1, "legacy one exercise");
  CHECK(strcmp(r.exercises[0].name, "Squat") == 0 && r.exercises[0].target_weight == 80,
        "legacy ex0 = Squat/80");

  /* Progression mode '1' and weight inc '2.5' header. */
  rp_clear(&r);
  routine_parse_string("Push|1|2.5|Bench|3|10|60|0|-|", &r);
  CHECK(r.total_exercises == 1 && strcmp(r.exercises[0].name, "Bench") == 0,
        "prog '1' header skipped");

  /* Truncated trailing record must not create a phantom exercise. */
  rp_clear(&r);
  routine_parse_string("T|-1|2|Bench|3|10|60|0", &r);
  CHECK(r.total_exercises == 0, "incomplete trailing record dropped");
  CHECK(strcmp(r.routine_name, "T") == 0, "name preserved on malformed input");

  /* Empty / NULL input is safe. */
  rp_clear(&r);
  routine_parse_string(NULL, &r);
  CHECK(r.total_exercises == 0 && r.routine_name[0] == '\0', "NULL input safe");

  /* ---- BATCH validation (routine_parse_batch_validate) ----
     The payload after "BATCH~" is split on '~'. Validation must succeed for a
     well-formed batch, and fail for malformed/truncated/empty segments. */

  {
    /* Well-formed batch: 2 routines. */
    char buf[512];
    strcpy(buf, "One|-1|2|Bench|3|10|60|0|-|~Two|-1|2|Squat|3|10|80|0|-|");
    int n = routine_parse_batch_validate(buf);
    CHECK(n == 2, "valid 2-routine batch returns 2");
  }

  {
    /* Legacy-format routine inside a batch (4-field exercises, no header). */
    char buf[512];
    strcpy(buf, "A|Bench|3|10|60|0|-|~B|Squat|3|10|80|0|-|");
    int n = routine_parse_batch_validate(buf);
    CHECK(n == 2, "batch with legacy routines returns 2");
  }

  {
    /* Truncated second routine → no exercises → must be rejected. */
    char buf[512];
    strcpy(buf, "One|-1|2|Bench|3|10|60|0|-|~Two|-1|2");
    int n = routine_parse_batch_validate(buf);
    CHECK(n == -1, "truncated batch segment rejected (-1)");
  }

  {
    /* Empty segment between separators → rejected. */
    char buf[512];
    strcpy(buf, "One|-1|2|Bench|3|10|60|0|-|~~Two|-1|2|Squat|3|10|80|0|-|");
    int n = routine_parse_batch_validate(buf);
    CHECK(n == -1, "empty batch segment rejected (-1)");
  }

  {
    /* Single-routine batch. */
    char buf[512];
    strcpy(buf, "Only|-1|2|Bench|3|10|60|0|-|");
    int n = routine_parse_batch_validate(buf);
    CHECK(n == 1, "single-routine batch returns 1");
  }

  {
    /* Empty payload (nothing after a batch that is just "BATCH~"). */
    char buf[2];
    buf[0] = '\0';
    int n = routine_parse_batch_validate(buf);
    CHECK(n == 0, "empty batch returns 0 (no routines to commit)");
  }

  {
    /* NULL input safe. */
    int n = routine_parse_batch_validate(NULL);
    CHECK(n == -1, "NULL batch input returns -1");
  }

  {
    /* No trailing separator: last routine has no '~' after it. */
    char buf[512];
    strcpy(buf, "One|-1|2|Bench|3|10|60|0|-|~Two|-1|2|Squat|3|10|80|0|-");
    int n = routine_parse_batch_validate(buf);
    CHECK(n == 2, "batch with no trailing '~' returns 2");
  }

  printf("\n%s\n", g_fail ? "PARSER TESTS FAILED" : "PARSER TESTS PASSED");
  return g_fail;
}
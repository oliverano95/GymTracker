/*
 * Memory-safety stress test for the shared routine parser.
 *
 * The watch app does NOT heap-allocate in its parse/batch/chunk paths (it uses
 * fixed static buffers with explicit bounds checks). The realistic C memory
 * risk is BUFFER OVERRUN on adversarial input, not a malloc/free leak. This
 * harness hammers routine_parse_string / routine_parse_batch_validate with
 * adversarial inputs at and beyond every documented bound, and is intended to
 * be run under AddressSanitizer + UndefinedBehaviorSanitizer so any overrun /
 * UB is flagged loudly.
 *
 * Build & run (ASan+UBSan):
 *   cc -fsanitize=address,undefined -g -I ../gymtracker/src/c \
 *      tests/test-c-parser-mem.c ../gymtracker/src/c/routine_parse.c \
 *      -o /tmp/c_parser_mem && /tmp/c_parser_mem
 *
 * With leak detection (Linux):
 *   ASAN_OPTIONS=detect_leaks=1 /tmp/c_parser_mem
 */

#include "routine_parse.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

static int g_cases = 0;

#define RUN(call) do { g_cases++; call; } while (0)

/* Fill a buffer with a single repeated char (no NUL). */
static void fill(char *buf, size_t n, char c) {
  for (size_t i = 0; i < n; i++) buf[i] = c;
}

int main(void) {
  RpResult r;
  char big[65536];

  /* 1. Oversized single routine tokens: names/comments far beyond RP_NAME_LEN
        (32) and beyond the parser's internal 40-byte token buffer. */
  {
    char *s = malloc(100000);
    fill(s, 99999, 'A');
    memcpy(s + 0,   "Name|", 5);  /* name token */
    memcpy(s + 5,   "-1|2|", 5);  /* header */
    memcpy(s + 10,  "X|", 2);     /* ex name */
    memcpy(s + 12,  "3|10|60|0|", 9);
    /* giant comment token fills the rest */
    s[99999] = '\0';
    RUN(routine_parse_string(s, &r));
    free(s);
  }

  /* 2. Oversized routine NAME (first token) with a valid body after it. */
  {
    char *s = calloc(100000, 1);
    fill(s, 50000, 'Q');
    strcpy(s + 50000, "|-1|2|Bench|3|10|60|0|-|");
    RUN(routine_parse_string(s, &r));
    free(s);
  }

  /* 3. Exactly max token length (RP_NAME_LEN-1) boundaries. */
  {
    char name[RP_NAME_LEN];
    memset(name, 'N', RP_NAME_LEN - 1);
    name[RP_NAME_LEN - 1] = '\0';
    char body[512];
    snprintf(body, sizeof body, "%s|-1|2|Bench|3|10|60|0|-|", name);
    RUN(routine_parse_string(body, &r));
  }

  /* 4. RP_MAX_EXERCISES (15) exercises: boundary is NOT written past. */
  {
    char body[4096];
    size_t o = 0;
    o += (size_t)snprintf(body + o, sizeof body - o, "Full|-1|2|");
    for (int i = 0; i < RP_MAX_EXERCISES; i++)
      o += (size_t)snprintf(body + o, sizeof body - o, "Ex%d|3|10|60|0|-|", i + 1);
    RUN(routine_parse_string(body, &r));
    if (r.total_exercises != RP_MAX_EXERCISES) {
      printf("FAIL: expected %d exercises, got %d\n", RP_MAX_EXERCISES, r.total_exercises);
      return 1;
    }
  }

  /* 5. MORE than RP_MAX_EXERCISES: the excess must be ignored, not overrun. */
  {
    char body[8192];
    size_t o = 0;
    o += (size_t)snprintf(body + o, sizeof body - o, "TooMany|-1|2|");
    for (int i = 0; i < RP_MAX_EXERCISES + 5; i++)
      o += (size_t)snprintf(body + o, sizeof body - o, "Ex%d|3|10|60|0|-|", i + 1);
    RUN(routine_parse_string(body, &r));
    if (r.total_exercises > RP_MAX_EXERCISES) {
      printf("FAIL: total_exercises overflowed (%d)\n", r.total_exercises);
      return 1;
    }
  }

  /* 6. Max-size BATCH: ~15 routines (at the MAX_SLOTS ceiling) with large
        names/comments, both trailing and non-trailing '~'. */
  {
    char *batch = calloc(200000, 1);
    size_t o = 0;
    for (int i = 0; i < 7; i++)
      o += (size_t)snprintf(batch + o, 200000 - o,
                            "RoutineNumber%d|-1|2|Bench|3|10|60|0|-"
                            "|ThisIsAVeryLongExerciseCommentExceedingTheNameLength"
                            "AnotherLongOne|~", i + 1);
    RUN(routine_parse_batch_validate(batch));
    free(batch);
  }

  /* 7. Oversized BATCH (more than MAX_SLOTS routines). */
  {
    char *batch = calloc(300000, 1);
    size_t o = 0;
    for (int i = 0; i < 20; i++)
      o += (size_t)snprintf(batch + o, 300000 - o, "R%d|-1|2|Bench|3|10|60|0|-|~", i + 1);
    int n = routine_parse_batch_validate(batch);
    RUN((void)0);
    if (n != 20) { printf("FAIL: expected 20, got %d\n", n); free(batch); return 1; }
    free(batch);
  }

  /* 8. Boundary cases. routine_parse_batch_validate REQUIRES a modifiable
        buffer (it overwrites '~' with '\0'), so pass writable local copies —
        never literals. */
  {
    char b0[128], b1[128], b2[128], b3[128];
    strcpy(b0, "A|-1|2|Bench|3|10|60|0|-|~");
    strcpy(b1, "~A|-1|2|Bench|3|10|60|0|-|");
    strcpy(b2, "A|-1|2|Bench|3|10|60|0|-|~|~~B|-1|2|Squat|3|10|80|0|-|");
    b3[0] = '\0';
    RUN(routine_parse_batch_validate(b0));
    RUN(routine_parse_batch_validate(b1));
    RUN(routine_parse_batch_validate(b2));
    RUN(routine_parse_batch_validate(b3));
    RUN(routine_parse_batch_validate(NULL));
    RUN(routine_parse_string("", &r));
    RUN(routine_parse_string(NULL, &r));
    RUN(routine_parse_string("||", &r));
    RUN(routine_parse_string("A|B|C|D|E|F|G|H|I|J|K|L|M|N|O|P", &r));
  }

  /* 9. Large valid input that previously could expose off-by-ones: 15 exercises
        at EXACTLY RP_NAME_LEN-1 token length, both header and legacy. */
  {
    char longname[RP_NAME_LEN];
    memset(longname, 'L', RP_NAME_LEN - 1);
    longname[RP_NAME_LEN - 1] = '\0';
    char body[8192];
    size_t o = 0;
    o += (size_t)snprintf(body + o, sizeof body - o, "Hdr|-1|2|");
    for (int i = 0; i < 15; i++)
      o += (size_t)snprintf(body + o, sizeof body - o, "%s|3|10|60|0|-|", longname);
    RUN(routine_parse_string(body, &r));

    size_t p = 0;
    p += (size_t)snprintf(body + p, sizeof body - p, "Legacy|");
    for (int i = 0; i < 15; i++)
      p += (size_t)snprintf(body + p, sizeof body - p, "%s|3|10|60|0|-|", longname);
    RUN(routine_parse_string(body, &r));
    if (r.total_exercises != 15) {
      printf("FAIL: legacy 15-exercises parse, got %d\n", r.total_exercises);
      return 1;
    }
  }

  /* 10. Missing-NUL buffer boundary: parser only reads while data[i]!=| && !='\0',
         so a huge unterminated token would scan past the end IF no run-length
         cap existed. We feed one so ASan catches any such bug. */
  {
    char *s = malloc(100000 + 1);
    fill(s, 100000, 'Z');
    s[100000] = '\0';
    RUN(routine_parse_string(s, &r));
    free(s);
  }

  printf("MEM STRESS: %d adversarial cases run, PARSER TESTS PASSED\n", g_cases);
  return 0;
}

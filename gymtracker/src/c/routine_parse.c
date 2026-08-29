// routine_parse.c
//
// See routine_parse.h for the format. This file is intentionally free of
// Pebble SDK dependencies so the same code can be compiled on a host for
// unit testing (tests/test-c-parser.c).

#include "routine_parse.h"

#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

// Splits a "BATCH~r1~r2~..." payload into its routine segments and validates
// each one. `data` MUST point just after the "BATCH~" prefix and MUST be a
// modifiable local copy — every '~' separator is overwritten with '\0' so the
// routines can later be read back by advancing data by strlen(data)+1.
//
// Returns the number of valid routine segments (>= 0), or -1 if ANY segment
// is empty or fails to parse to a routine with a name and at least one
// exercise. Callers MUST NOT commit anything (wipe existing routines) unless
// this returns a valid count, so a malformed/truncated batch can never destroy
// previously synced routines.
int routine_parse_batch_validate(char *data) {
  if (!data) return -1;

  int   count = 0;
  char *seg   = data;

  while (*seg) {
    char *end = strchr(seg, '~');
    if (end) *end = '\0';            // mutate in place
    if (*seg == '\0') return -1;     // empty segment (bad batch)

    RpResult scratch;
    routine_parse_string(seg, &scratch);
    if (scratch.routine_name[0] == '\0' || scratch.total_exercises == 0)
      return -1;

    count++;
    if (!end) break;
    seg = end + 1;
  }

  return count;
}

void routine_parse_string(const char *data, RpResult *out) {
  memset(out, 0, sizeof(*out));
  if (!data) return;

  int i = 0, token_count = 0, t_idx = 0;
  int header_offset = 0;      // 3 => "Name|prog|inc|...", 1 => legacy
  char temp[40];

  while (true) {
    if (data[i] == '|' || data[i] == '\0') {
      temp[t_idx] = '\0';
      if (token_count == 0) {
        int n = (int)strlen(temp);
        if (n > RP_NAME_LEN - 1) n = RP_NAME_LEN - 1;
        memcpy(out->routine_name, temp, n);
        out->routine_name[n] = '\0';
      } else if (token_count == 1) {
        // Detect the optional progression header exactly like pkjs
        // parseRoutineString: parts[1] is "-1","0", or "1".
        if (strcmp(temp, "-1") == 0 || strcmp(temp, "0") == 0 || strcmp(temp, "1") == 0)
          header_offset = 3;
        else
          header_offset = 1;
      }

      if (token_count >= header_offset && token_count > 0) {
        int ex_idx = (token_count - header_offset) / 6;
        int field  = (token_count - header_offset) % 6;

        if (ex_idx < RP_MAX_EXERCISES) {
          RpExercise *ex = &out->exercises[ex_idx];
          if      (field == 0) {
            int n = (int)strlen(temp);
            if (n > RP_NAME_LEN - 1) n = RP_NAME_LEN - 1;
            memcpy(ex->name, temp, n);
            ex->name[n] = '\0';
          } else if (field == 1) ex->target_sets   = atoi(temp);
          else if (field == 2) ex->target_reps    = atoi(temp);
          else if (field == 3) ex->target_weight  = atoi(temp);
          else if (field == 4) {
            ex->modifier = atoi(temp);
            if (ex->modifier == 4) { ex->modifier = 0; ex->target_weight = 0; }
            if (ex->modifier == 6) { ex->target_weight = 0; }
            if (ex->modifier == 1) ex->target_sets *= 2;
            if (ex->target_sets > 10) ex->target_sets = 10;
            if (ex->target_sets < 1)  ex->target_sets = 1;
            ex->current_set = 1;
          } else if (field == 5) {
            if (strcmp(temp, "-") == 0) {
              ex->comment[0] = '\0';
            } else {
              int n = (int)strlen(temp);
              if (n > RP_NAME_LEN - 1) n = RP_NAME_LEN - 1;
              memcpy(ex->comment, temp, n);
              ex->comment[n] = '\0';
            }
            out->total_exercises = ex_idx + 1;
          }
        }
      }
      token_count++;
      t_idx = 0;
      if (data[i] == '\0') break;
    } else {
      if (t_idx < 39) temp[t_idx++] = data[i];
    }
    i++;
  }
}
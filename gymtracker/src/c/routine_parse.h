// routine_parse.h
//
// Standalone routine-sync-string parser shared by the watch app (main.c) and
// the host-side Playwright C unit test (tests/test-c-parser.c).
//
// Input format (pipe-delimited):
//   "Name|prog|inc|ex1|sets|reps|weight|mod|cmt|ex2|sets|reps|weight|mod|cmt|..."
//   or legacy (no header):
//   "Name|ex1|sets|reps|weight|mod|cmt|ex2|..."
//
// The optional progression header ("-1","0", or "1") is detected just like
// pkjs parseRoutineString so that watch->phone->watch round-trips stay in
// sync. Exercises occupy fixed 6-token records after the header.

#ifndef ROUTINE_PARSE_H
#define ROUTINE_PARSE_H

#define RP_MAX_EXERCISES 15
#define RP_NAME_LEN 32

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  char name[RP_NAME_LEN];
  char comment[RP_NAME_LEN];
  int  target_weight;
  int  target_sets;
  int  target_reps;
  int  modifier;
  int  current_set;
} RpExercise;

typedef struct {
  char routine_name[RP_NAME_LEN];
  int  total_exercises;
  RpExercise exercises[RP_MAX_EXERCISES];
} RpResult;

// Parses `data` into `out` (zeroed first). Safe on truncated/malformed input:
// an incomplete trailing exercise record is dropped.
void routine_parse_string(const char *data, RpResult *out);

// Splits a "BATCH~r1~r2~..." payload into routine segments and validates each
// one. `data` begins just after the "BATCH~" prefix and must be a modifiable
// local copy (every '~' is overwritten with '\0'). Returns the count of valid
// routines, or -1 if any segment is empty/malformed. See routine_parse.c.
int routine_parse_batch_validate(char *data);

#ifdef __cplusplus
}
#endif

#endif // ROUTINE_PARSE_H
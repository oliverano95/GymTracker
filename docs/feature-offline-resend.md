# Offline Workout Resend

## Problem

Finishing a workout with no phone connection lost the session: the export
fired once, failed, and the completed sets lived only in memory.

## Behavior

- Every finished workout now stages its export payload in watch storage
  before the single send attempt (chunked, transient — only a pending
  workout occupies space).
- Failed sends keep the staged payload. The summary screen says
  "Sync Failed. Will retry." instead of losing the session.
- The payload resends itself on Bluetooth reconnect and on every boot
  with the phone nearby. A "Resend Last Workout" main-menu item covers
  the rest.
- When storage is too full for the whole payload, only the sets are kept
  (the heart-rate series is dropped); that prefix is still a valid payload
  downstream. If nothing fits, behavior is unchanged from before.
- Resends carry only the log phase through the normal message keys, so
  the phone side and the server need no changes (the server dedupes by
  hash). Routine progression already persisted locally at build time.
- Known limitation: only the latest unsent workout is kept — finishing a
  second offline session supersedes the first.

## Verification

- Build passes on all platforms (`pebble build`).
- Boot smoke via the emulator screenshot test (new init code runs there).
- Manual acceptance: one phone-free session, confirm "Will retry",
  reconnect, confirm arrival, confirm the menu row disappears.

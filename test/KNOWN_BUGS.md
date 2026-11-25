# Known bugs

Bugs found by the test suite that are not fixed yet. Each bug has an ID, and
that ID appears in the test that covers it. Run `grep -rn "<ID>" test` to
find the cases to re-enable once a bug is fixed.

How each kind of test marks a known bug:

- **Dart**: the case is marked `skip: 'bug <ID>: ...'`. Run it anyway with
  `flutter test --run-skipped <file>`.
- **Native**: a failing check prints `KNOWN BUG <ID> (...)` and does not fail
  the run. Pass `--strict` (or set `OSDEC_STRICT=1` for the OS decoder tests)
  to make these checks fail.

**When a bug is fixed:**

1. Remove the marker: delete the `skip:`, or turn the `KNOWN_BUG` check into
   a normal `CHECK`.
2. Check that the test passes, also with `--asan` where it applies.
3. Delete the bug's entry from this file.

Line numbers are approximate. IDs already used and retired: E1–E9, F1–F5,
O1–O3, S1–S9, K1–K2, P1–P9, A1–A35, B1–B26, C1–C7, H1–H2, J1–J18, L1, L2,
L4, L5, L6, M1–M8.

## Android

| ID | Bug | Where | Test |
|---|---|---|---|
| L3 | Android streaming of loud AC-3 in MP4 on Dolby's decoder (S22): PCM after seek differs from the sequential decode by ~0.03 (length, placement and quiet content are right) | the platform decoder: it adds ~6 dB and limits, and the limiter's state survives a flush and depends on what came long before; no exposed parameter turns it off | android/native_stream_decoder_android_test, android/native_stream_test (S22) |

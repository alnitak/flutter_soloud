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
- **Failing as is**: the test fails and is excluded in CI through
  `KNOWN_FAILING_NATIVE` in `.github/workflows/tests.yaml`.

**When a bug is fixed:**

1. Remove the marker: delete the `skip:`, or turn the `KNOWN_BUG` check into
   a normal `CHECK`.
2. Check that the test passes, also with `--asan` where it applies.
3. Delete the bug's entry from this file.

Line numbers are approximate.

## Open bugs

None at the moment. Add new ones here as `| ID | Bug | Where | Test |` rows,
with an ID that has not been used before (used so far: E1–E9, F1–F5, O1–O3,
S1–S9, K1–K2, P1–P9).

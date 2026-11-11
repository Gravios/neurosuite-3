# Klusters headless self-test harness

`klusters_selftest.cpp` lets the **built** Klusters drive its own document machinery without a
display: `NS3_KLUSTERS_SELFTEST=<spec.json>` (with `QT_QPA_PLATFORM=offscreen`) opens the spec's
`.clu`, runs the spec's steps one at a time -- each once the app is idle (no realign, nudge,
save, plugin or pending return to a space) -- writes a JSON report and quits (0: every step
succeeded; 1: a step failed; 2: the spec or the open failed). A modal dialog opened by a step
is closed with its default button and recorded in the report, so a run never hangs.

Steps: `{"show": method}`, `{"realign": cluster}`, `{"nudge": [cluster, delta]}`,
`{"supersample": factor}`, `{"follow": true}`, `{"save": true}`, `{"saveas": "<path.clu>"}`,
`{"dump": true, "cluster": id}` (the spaces, their files and working copies, the loaded times
and phases of the cluster's spikes). A realign's completion is recorded as a `realignDone`
entry with its shift count and log. Spec keys `realignFromAnySpace`, `realignVerbose` and
`supersampleMode` set those preferences for the run (a private `QSettings` under the work
directory, never the user's).

Tests: `test_follow_supersampled.py` (a `_U4`/`_U2` space follows a realign made in the `.clu`'s
space, and the reverse), `test_realign_in_shown_space.py` (realign and nudge computed in the
shown `_U4` space with the `.clu` in `standard`: sub-sample phases, the base space follows,
alignment quality), `test_realign_base_from_supersampled_clu.py` (Save As into `_U4`, a
base-rate realign with the `.clu` in `_U4`).

The tests here (`test_*.py`) build a synthetic session (`session.py`: a `.fil`/`.dat`, two
units on four channels placed at sub-sample offsets with a jittered `.res`, and the
`standard` space's `.res/.clu/.spk/.pca/.fet`), run the binary through `run.py` and check
the files afterwards with numpy. They run from ctest as `klusters_selftest_*` when
`NS_BUILD_TESTS=ON` and the `klusters` target is built, or by hand:

    KLUSTERS_BIN=build/klusters/src/klusters python3 src/klusters/test/selftest/test_follow_supersampled.py

Without `KLUSTERS_BIN` (or numpy) a test skips with exit 77.

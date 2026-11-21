# Klusters headless self-test harness

`klusters_selftest.cpp` lets the **built** Klusters drive its own document machinery without a
display: `NS3_KLUSTERS_SELFTEST=<spec.json>` (with `QT_QPA_PLATFORM=offscreen`) opens the spec's
`.clu`, runs the spec's steps one at a time -- each once the app is idle (no realign, nudge,
save, plugin or pending return to a space) -- writes a JSON report and quits (0: every step
succeeded; 1: a step failed; 2: the spec or the open failed). A modal dialog opened by a step
is closed with its default button and recorded in the report, so a run never hangs.

Steps: `{"show": method}`, `{"realign": cluster}`, `{"nudge": [cluster, delta]}`,
`{"supersample": factor}`, `{"follow": true}`, `{"burstFollowers": [a, b]}`, `{"save": true}`, `{"saveas": "<path.clu>"}`,
`{"dump": true, "cluster": id}` (the spaces, their files and working copies, whether each is held in memory, the loaded times
and phases of the cluster's spikes). A realign's completion is recorded as a `realignDone`
entry with its shift count and log. Spec keys `realignFromAnySpace`, `realignVerbose`,
`supersampleMode` and `supersampleInterp` (0 cubic spline, 1 Lanczos-3, 2 smoothing spline;
the tests that compare ticks to raw samples exactly set 0) set those preferences for the run (a private `QSettings` under the work
directory, never the user's).

Tests: `test_follow_supersampled.py` (a `_U4`/`_U2` space follows a realign made in the `.clu`'s
space, and the reverse), `test_realign_in_shown_space.py` (realign and nudge computed in the
shown `_U4` space with the `.clu` in `standard`: sub-sample phases, the base space follows,
alignment quality), `test_realign_base_from_supersampled_clu.py` (Save As into `_U4`, a
base-rate realign with the `.clu` in `_U4`), `test_detached_space.py` (a detached space --
a per-spike table with no basis, `.fet.manifold.1` -- neither blocks a realign in the space
it shares waveforms with nor loses its features; its time column follows; a realign asked for
in it is refused), `test_supersample_interp_detect.py` (a `_U4` built with the exact spline is
realigned under the smoothing-spline preference: the file's interpolator is detected and used),
`test_realign_kernel.py` (a correlation window -- Preferences > Realignment > Correlation window,
`realignKernel` / `realignKernelWidth` in the spec -- keeps a collision in the tail of the window
from steering the lag: more collided spikes on their true time than with the whole window),
`test_realign_center_window.py` (the post-alignment centering under that window -- `realignMode`
in the spec: 1 PCA refine, 2 RMS recenter -- the whole-window RMS recenter carries the collided
cluster into the collision tail, the windowed one leaves it on its true time),
`test_residual_strip_chunks.py` (Strip Residual Outliers per time chunk -- the `residualScan`
step, `chunkSeconds` -- on a unit that drifts across the probe: the chunked scan finds the
contaminants a session-wide template hides; small chunks borrow their neighbours; a seed is
read per chunk), `test_residual_strip_children.py` (the same action on children -- `"children":
true`, `"apply": true` -- over a `.clc` / `.clp` the test writes: children are scored whole on
the child layer, the split makes one sibling child per parent, and the parents keep their spikes),
`test_multilasso_landing.py` (the selection after a multi-lasso split -- the `select`, `multiLasso` and
`palette` steps, `autoRenumber` in the spec -- is the pieces and never a neighbouring cluster),
`test_move_to_end_follows.py` (after T -- the `moveToEnd` step -- the parent palette scrolls to the moved
clusters; `palette` reports the ids inside the list's viewport),
`test_recluster_klustakwik.py` (a KlustaKwik recluster through the `recluster`, `reclusterLog` and
`reclusterChunkArgs` steps: the time column is the sorter's time axis whatever `-UseFeatures` says, and
the *Time chunks* preference reaches the command line; needs `KiloKlustaKwik_cpu` -- `KKK_BIN`, else the
build tree's -- and skips without it)),
`test_supersampled_on_demand.py` (a `_U` space is not held in memory after its build or at open in
super-sample mode, the session stays in the `.clu`'s space; it is held once shown and stays held),
`test_child_ids.py` (the numbering of children, disjoint from the parents': an old-numbering `.clc`
converted on load and written on Save, the names kept through parent renumbering, sorting, merging,
child merging, promotion, Flatten, undo and a parent split -- the `hierarchy` step),
`test_pref_pages.py` (the Preferences dialog at 1000 x 700: every page in a scroll area and drawn at
the height it was laid out for, the Cluster view page scrolling to its last group),
`test_core_cut.py` (the `coreCut` step, Actions > Split Off Spurious Spikes: a unit with 100 spikes of
another in it loses those and few of its own; a unit alone keeps three quarters or more; the same on
children, the new child under the source's parent; its key is Ctrl+Space, the cut sensitivity of
Preferences > Splitting reaches the analysis, and the page has the action's group -- the `prefPage`
step, which builds the Preferences dialog without showing it),
`test_burst_followers.py` (the `burstFollowers` step, Actions > Split Burst Followers: the followers of
one cluster's bursts found in another are split into one new cluster and nothing else moves; a correct
clustering is left alone; with `"mixed": c`, two references sort a third cluster into one new cluster
per reference; with `"children": true` on the child layer, the new child placed under the parent its
spikes came from and the `.clu` unchanged),
`test_hover_spike.py` (the `hover` step puts the cursor on a spike in the cluster view: the spike tracked
is one drawn there, every waveform view overlays its `.spk` samples in a contrasting colour in its cluster's
column, and the overlay goes when the cursor leaves or `Shift+V` turns the tracking off; the
Preferences > Cluster view knobs -- on/off, ring radius and line width, trace width -- change what is
painted, through the step's `prefs` key)).

The tests here (`test_*.py`) build a synthetic session (`session.py`: a `.fil`/`.dat`, two
units on four channels placed at sub-sample offsets with a jittered `.res`, and the
`standard` space's `.res/.clu/.spk/.pca/.fet`), run the binary through `run.py` and check
the files afterwards with numpy. They run from ctest as `klusters_selftest_*` when
`NS_BUILD_TESTS=ON` and the `klusters` target is built, or by hand:

    KLUSTERS_BIN=build/klusters/src/klusters python3 src/klusters/test/selftest/test_follow_supersampled.py

Without `KLUSTERS_BIN` (or numpy) a test skips with exit 77.

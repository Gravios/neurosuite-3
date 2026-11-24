# Klusters headless self-test harness

`klusters_selftest.cpp` lets the **built** Klusters drive its own document machinery without a
display: `NS3_KLUSTERS_SELFTEST=<spec.json>` (with `QT_QPA_PLATFORM=offscreen`) opens the spec's
`.clu`, runs the spec's steps one at a time -- each once the app is idle (no realign, nudge,
save, plugin or pending return to a space) -- writes a JSON report and quits (0: every step
succeeded; 1: a step failed; 2: the spec or the open failed). A modal dialog opened by a step
is closed with its default button and recorded in the report, so a run never hangs.

Steps: `{"show": method}`, `{"realign": cluster}` (with `"children": true` a child, from the child view), `{"tabRefresh": true}`, `{"clickParent": cluster}`, `{"waveformQuality": {"show", "children", "nbSpikes", "overlay", "prefs", "saveTo", "windowSize"}}`, `{"nudge": [cluster, delta]}`,
`{"supersample": factor}`, `{"follow": true}`, `{"burstFollowers": [a, b]}`, `{"save": true}`, `{"saveas": "<path.clu>"}`,
`{"dump": true, "cluster": id}` (the spaces, their files and working copies, whether each is held in memory, the loaded times
and phases of the cluster's spikes). A realign's completion is recorded as a `realignDone`
entry with its shift count and log. Spec keys `realignFromAnySpace`, `realignVerbose`,
`supersampleMode` and `supersampleInterp` (0 cubic spline, 1 Lanczos-3, 2 smoothing spline;
the tests that compare ticks to raw samples exactly set 0) set those preferences for the run (a private `QSettings` under the work
directory, never the user's).

**Every `test_*.py` here is a `ctest` test** (`klusters_selftest_<name>`, label `selftest`): a new
file is registered by being here, at the next configure. `ctest -L selftest` runs them alone, in
about three minutes; they need python3 with numpy and exit 77 (skipped) without the built
binary, and the reclustering ones without `KiloKlustaKwik_cpu` (the build tree's, or `KKK_BIN`).
By hand: `KLUSTERS_BIN=<build>/klusters/src/klusters python3 test_<name>.py`.

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
`test_child_realign_scope.py` (a child realigned from the child view stays shown on the child layer,
and the refresh a realign ends with -- the `tabRefresh` step -- never leaves that layer: the
`hierarchy` step's `childScope`, `shown` and `layerSwitches`),
`test_child_view_parent_click.py` (from the child view a click on the curated parent in the parent
palette -- the `clickParent` step -- shows that parent on the parent layer),
`test_collision_rows.py` (rows whose `.eap` row names a partner unit are served with it subtracted --
the `collisionRows` step: 40 planted collisions corrected, the `.spk` untouched, rows without a
partner served raw, the preference, the `.spkc`, and the rows rebuilt after a nudge),
`test_recluster_against_references.py` (Recluster > Recluster Against References, the
`reclusterAgainst` step: a cluster of 150 + 150 spikes of two units pooled with the units' own
clusters -- the references keep their rows and ids, the mixed cluster becomes the two units' pieces,
the summary names the right reference for each; an ordinary recluster replaces the references too),
`test_realign_collision_rows.py` (a realign measures a collision row on its corrected record: 60
planted collisions 3-7 samples from the spike, every true time known -- 1 of 60 ends off its true
time, against 33 of 60 with the preference off; the records written are raw cuts),
`test_time_sort_collision_files.py` (the `timeSort` step, Spikes > Sort Spikes by Time: 60 rows out
of order in every file -- the `.eap` and the `.spkc` come back byte for byte with the `.res`, files
over another spike count are skipped, and the reopened session corrects the same 40 rows),
`test_waveform_quality.py` (the waveform view's quality dots -- the `waveformQuality` step: values equal
to numpy's on the `.spk` for parents and children, colours by the thresholds, the hollow dot of a
small cluster, where the dots are, the preference),
`test_pref_pages.py` (the Preferences dialog at 1000 x 700: every page in a scroll area and drawn at
the height it was laid out for, the Cluster view page scrolling to its last group),
`test_core_cut.py` (the `coreCut` step, Split > Split Off Spurious Spikes: a unit with 100 spikes of
another in it loses those and few of its own; a unit alone keeps three quarters or more; the same on
children, the new child under the source's parent; its key is Ctrl+Space, the cut sensitivity of
Preferences > Splitting reaches the analysis, and the page has the action's group -- the `prefPage`
step, which builds the Preferences dialog without showing it),
`test_burst_followers.py` (the `burstFollowers` step, Split > Split Burst Followers: the followers of
one cluster's bursts found in another are split into one new cluster and nothing else moves; a correct
clustering is left alone; with `"mixed": c`, two references sort a third cluster into one new cluster
per reference; with `"children": true` on the child layer, the new child placed under the parent its
spikes came from and the `.clu` unchanged),
`test_hover_spike.py` (the `hover` step puts the cursor on a spike in the cluster view: the spike tracked
is one drawn there, every waveform view overlays its `.spk` samples in a contrasting colour in its cluster's
column, and the overlay goes when the cursor leaves or `Shift+V` turns the tracking off; the
Preferences > Cluster view knobs -- on/off, ring radius and line width, trace width -- change what is
painted, through the step's `prefs` key)).

**Undo, the curation log and cluster information:** `test_undo_pairing.py` (what the data
reverts, the one history of both layers and the curation log agree, step by step -- the `undo`,
`redo` and `hierarchy` steps), `test_child_recluster_undo.py` (a child recluster is one step, undone
and redone whole), `test_annotation_undo.py` (a change of a cluster's information is a step -- the
`annotate` step), `test_annotation_survives_moves.py` (a move of spikes between clusters keeps every
cluster's information), `test_palette_merge_children.py` (after the palette's Group -- the `group`
step -- the children are filed under the merged parent), `test_curation_log.py` (what started an
action, what it did, what was looked at and left -- the `curationLog` step),
`test_undo_depth_migration.py` (a stored depth of 2, the old default, becomes 20 once).

**Jobs:** `test_edit_lock.py` (no editing command is enabled during a recluster or a realign --
`actionsEnabled`, and `"whileBusy": true` on a step to run it during the job; a key command that
edits does nothing -- `keyCommand`; the same commands are enabled afterwards), `test_cancel_job.py`
(Edit > Cancel Running Job -- `cancelJob`, with `{"when": job}` to wait for one).

**The child view:** `test_child_view_matrix_click.py` (a click in a matrix of parents returns to the
parents -- `matrixSelect`), `test_purge_child_view.py` (Purge Small Clusters follows the view it is
run from -- `purge`, `childPalette`).

**Commands and preferences** (the tables of
[COMMANDS_AND_PREFERENCES.md](../../docs/COMMANDS_AND_PREFERENCES.md)): `test_command_table.py`
(a snapshot of every menu command -- `commandDump`, `commands.golden.json`; `--update` rewrites it),
`test_command_ids_stable.py` (every command id of before 2026-11-24 still resolves),
`test_dissolve_key_binding_migration.py` (a stored key binding moves to the key command's own id,
once), `test_settings_file.py` (a snapshot of every key of the settings file after read + write, in
seven cases -- `configWrite`, with `"dialog"` / `"defaults"` to pass through Preferences;
`settings.golden.json`; `--update` rewrites it), `test_realign_threshold_migration.py` (a stored
0.70, the old default, becomes 0.10 once), `test_pref_keyboard.py` (Preferences from the keyboard,
its two levels -- `prefKeys`, a list of key names sent to the dialog one by one).

The tests here (`test_*.py`) build a synthetic session (`session.py`: a `.fil`/`.dat`, two
units on four channels placed at sub-sample offsets with a jittered `.res`, and the
`standard` space's `.res/.clu/.spk/.pca/.fet`), run the binary through `run.py` and check
the files afterwards with numpy. They run from ctest as `klusters_selftest_*` when
`NS_BUILD_TESTS=ON` and the `klusters` target is built, or by hand:

    KLUSTERS_BIN=build/klusters/src/klusters python3 src/klusters/test/selftest/test_follow_supersampled.py

Without `KLUSTERS_BIN` (or numpy) a test skips with exit 77.

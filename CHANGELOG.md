# Changelog — neurosuite-3

Top-level changelog for this fork, consolidating every change relative
to the original Neurosuite toolchain.  Entries are grouped by date from
most recent at top.  Deep per-topic technical notes live in
`doc/design/<topic>.md`, indexed at the end.

Each entry is folded — click **Details** to expand it. Newest first.

| Date | Change |
|---|---|
| 2026-11-27 | [Klusters: the core cut and the PCA refine on HIP and SYCL](#2026-11-27--klusters-the-core-cut-and-the-pca-refine-on-hip-and-sycl) |
| 2026-11-27 | [Build: the HIP backends build](#2026-11-27--build-the-hip-backends-build) |
| 2026-11-27 | [Klusters: a collision is marked only when it is there in every waveform space](#2026-11-27--klusters-a-collision-is-marked-only-when-it-is-there-in-every-waveform-space) |
| 2026-11-27 | [Klusters: Add Residual Spikes says whether the spikes look like their partner](#2026-11-27--klusters-add-residual-spikes-says-whether-the-spikes-look-like-their-partner) |
| 2026-11-27 | [Klusters: a .spkr for every .spk method at Save](#2026-11-27--klusters-a-spkr-for-every-spk-method-at-save) |
| 2026-11-27 | [Klusters: Add Residual Spikes no longer crashes on a stderiv session](#2026-11-27--klusters-add-residual-spikes-no-longer-crashes-on-a-stderiv-session) |
| 2026-11-27 | [Klusters: the residual spikes' features, saved as .fetr](#2026-11-27--klusters-the-residual-spikes-features-saved-as-fetr) |
| 2026-11-27 | [Klusters: Add Residual Spikes grows every feature space](#2026-11-27--klusters-add-residual-spikes-grows-every-feature-space) |
| 2026-11-27 | [Formats: .fetr, the residual spikes in a feature space](#2026-11-27--formats-fetr-the-residual-spikes-in-a-feature-space) |
| 2026-11-27 | [Klusters: Add Residual Spikes as Clusters](#2026-11-27--klusters-add-residual-spikes-as-clusters) |
| 2026-11-27 | [Klusters: a stage that holds its own rows opens on them](#2026-11-27--klusters-a-stage-that-holds-its-own-rows-opens-on-them) |
| 2026-11-26 | [Decollide does not grow a spike that is a row already](#2026-11-26--decollide-does-not-grow-a-spike-that-is-a-row-already) |
| 2026-11-26 | [Klusters: Remove Duplicate Spikes](#2026-11-26--klusters-remove-duplicate-spikes) |
| 2026-11-26 | [Klusters: the residual of a collision row, shown and saved as .spkr](#2026-11-26--klusters-the-residual-of-a-collision-row-shown-and-saved-as-spkr) |
| 2026-11-26 | [Formats: .spkr, the residual spike of a collision row](#2026-11-26--formats-spkr-the-residual-spike-of-a-collision-row) |
| 2026-11-26 | [Klusters: Mark Collisions shows what chance would give for each partner](#2026-11-26--klusters-mark-collisions-shows-what-chance-would-give-for-each-partner) |
| 2026-11-26 | [Klusters: collision rows as a child, and marks taken back](#2026-11-26--klusters-collision-rows-as-a-child-and-marks-taken-back) |
| 2026-11-26 | [Klusters: Mark Collisions tries a cluster only where it is firing](#2026-11-26--klusters-mark-collisions-tries-a-cluster-only-where-it-is-firing) |
| 2026-11-26 | [Formats: the .eap and .tcl pages say what the two implementations do](#2026-11-26--formats-the-eap-and-tcl-pages-say-what-the-two-implementations-do) |
| 2026-11-26 | [Klusters: the .tcl Mark Collisions writes is as wide as the .eap](#2026-11-26--klusters-the-tcl-mark-collisions-writes-is-as-wide-as-the-eap) |
| 2026-11-26 | [Klusters: Mark Collisions gives a moved row its own cluster's class](#2026-11-26--klusters-mark-collisions-gives-a-moved-row-its-own-clusters-class) |
| 2026-11-26 | [The .eap, .tcl and .spkc are written whole or not at all](#2026-11-26--the-eap-tcl-and-spkc-are-written-whole-or-not-at-all) |
| 2026-11-26 | [Klusters: Mark Collisions](#2026-11-26--klusters-mark-collisions) |
| 2026-11-26 | [Docs workflow: the deploy is a job of its own](#2026-11-26--docs-workflow-the-deploy-is-a-job-of-its-own) |
| 2026-11-26 | [A recluster leaves no KlustaKwik model file](#2026-11-26--a-recluster-leaves-no-klustakwik-model-file) |
| 2026-11-26 | [A local PCA of the selected clusters on Shift+Y](#2026-11-26--a-local-pca-of-the-selected-clusters-on-shifty) |
| 2026-11-26 | [Docs: the in-app handbook is the generator's output again](#2026-11-26--docs-the-in-app-handbook-is-the-generators-output-again) |
| 2026-11-26 | [Self-test: Quit's key is the platform's](#2026-11-26--self-test-quits-key-is-the-platforms) |
| 2026-11-26 | [Windows build: the realign refine search reduces on local counters](#2026-11-26--windows-build-the-realign-refine-search-reduces-on-local-counters) |
| 2026-11-25 | [Super-sample mode asks before it builds the waveforms](#2026-11-25--super-sample-mode-asks-before-it-builds-the-waveforms) |
| 2026-11-25 | [Self-test: the command table does not depend on the Qt version](#2026-11-25--self-test-the-command-table-does-not-depend-on-the-qt-version) |
| 2026-11-25 | [Windows build: pi from the standard library in supersample.hpp](#2026-11-25--windows-build-pi-from-the-standard-library-in-supersamplehpp) |
| 2026-11-24 | [Klusters sources: eight subdirectories by layer](#2026-11-24--klusters-sources-eight-subdirectories-by-layer) |
| 2026-11-24 | [Klusters texts: artifact, not artefact](#2026-11-24--klusters-texts-artifact-not-artefact) |
| 2026-11-24 | [Klusters code: parent and child, not fiber and atom](#2026-11-24--klusters-code-parent-and-child-not-fiber-and-atom) |
| 2026-11-24 | [Klusters code: artifact, not artefact](#2026-11-24--klusters-code-artifact-not-artefact) |
| 2026-11-24 | [Glossary: one word per thing in Klusters](#2026-11-24--glossary-one-word-per-thing-in-klusters) |
| 2026-11-24 | [Matrix views: one base for the grid, the pan and zoom, the follow](#2026-11-24--matrix-views-one-base-for-the-grid-the-pan-and-zoom-the-follow) |
| 2026-11-24 | [Self-test: the four matrix views under the mouse](#2026-11-24--self-test-the-four-matrix-views-under-the-mouse) |
| 2026-11-24 | [Realign: realignSpikes reads as its stages](#2026-11-24--realign-realignspikes-reads-as-its-stages) |
| 2026-11-24 | [Realign: the new times, the verification and the row plan are functions](#2026-11-24--realign-the-new-times-the-verification-and-the-row-plan-are-functions) |
| 2026-11-24 | [Realign undo: from any feature space](#2026-11-24--realign-undo-from-any-feature-space) |
| 2026-11-24 | [Realign undo: the memory it may use is a preference](#2026-11-24--realign-undo-the-memory-it-may-use-is-a-preference) |
| 2026-11-24 | [Realign can be undone](#2026-11-24--realign-can-be-undone) |
| 2026-11-24 | [Realign: each rewritten row keeps what was there](#2026-11-24--realign-each-rewritten-row-keeps-what-was-there) |
| 2026-11-24 | [Realign: the rows are computed first, then written](#2026-11-24--realign-the-rows-are-computed-first-then-written) |
| 2026-11-24 | [Realign: the PCA refinement is two functions](#2026-11-24--realign-the-pca-refinement-is-two-functions) |
| 2026-11-24 | [Audit: where the split of realignSpikes stands](#2026-11-24--audit-where-the-split-of-realignspikes-stands) |
| 2026-11-24 | [Realign: a realign that cannot run says why](#2026-11-24--realign-a-realign-that-cannot-run-says-why) |
| 2026-11-24 | [Realign: the final scores and their summary are library functions](#2026-11-24--realign-the-final-scores-and-their-summary-are-library-functions) |
| 2026-11-24 | [Realign: reading a cluster's rows is three plain functions](#2026-11-24--realign-reading-a-clusters-rows-is-three-plain-functions) |
| 2026-11-24 | [Realign: the pass loop is a library function](#2026-11-24--realign-the-pass-loop-is-a-library-function) |
| 2026-11-24 | [Klusters developer docs: commands and preferences](#2026-11-24--klusters-developer-docs-commands-and-preferences) |
| 2026-11-24 | [Klusters: the rest of the Preferences pages are filled from the table](#2026-11-24--klusters-the-rest-of-the-preferences-pages-are-filled-from-the-table) |
| 2026-11-24 | [Klusters: Preferences pages are filled from the preference table](#2026-11-24--klusters-preferences-pages-are-filled-from-the-preference-table) |
| 2026-11-24 | [Klusters: a settings file is read within each preference's limits](#2026-11-24--klusters-a-settings-file-is-read-within-each-preferences-limits) |
| 2026-11-24 | [Klusters: the preferences are described in one table](#2026-11-24--klusters-the-preferences-are-described-in-one-table) |
| 2026-11-24 | [Klusters: Defaults gave the wrong super-sampling interpolation](#2026-11-24--klusters-defaults-gave-the-wrong-super-sampling-interpolation) |
| 2026-11-24 | [Klusters: a snapshot test of the settings file](#2026-11-24--klusters-a-snapshot-test-of-the-settings-file) |
| 2026-11-24 | [Klusters: Preferences from the keyboard](#2026-11-24--klusters-preferences-from-the-keyboard) |
| 2026-11-24 | [Klusters: the menus are arranged by what a command does](#2026-11-24--klusters-the-menus-are-arranged-by-what-a-command-does) |
| 2026-11-24 | [Klusters: the editing keys refuse while a job runs](#2026-11-24--klusters-the-editing-keys-refuse-while-a-job-runs) |
| 2026-11-24 | [Klusters: two commands shared the id hierarchy.dissolveParent](#2026-11-24--klusters-two-commands-shared-the-id-hierarchydissolveparent) |
| 2026-11-24 | [ndmanager-plugins: ndm_alignspikes_pca removed](#2026-11-24--ndmanager-plugins-ndmalignspikespca-removed) |
| 2026-11-24 | [Klusters: the realign threshold defaults to 0.10](#2026-11-24--klusters-the-realign-threshold-defaults-to-010) |
| 2026-11-24 | [Klusters: the edit lock in the concurrency reference](#2026-11-24--klusters-the-edit-lock-in-the-concurrency-reference) |
| 2026-11-24 | [Klusters: the undo history is one object](#2026-11-24--klusters-the-undo-history-is-one-object) |
| 2026-11-24 | [Klusters: the menu commands are described in one table](#2026-11-24--klusters-the-menu-commands-are-described-in-one-table) |
| 2026-11-24 | [Klusters: a snapshot test of every command, and 24 command names cut short](#2026-11-24--klusters-a-snapshot-test-of-every-command-and-24-command-names-cut-short) |
| 2026-11-24 | [Klusters: unused code removed](#2026-11-24--klusters-unused-code-removed) |
| 2026-11-24 | [Audit tool: the unused-function count was three times too high](#2026-11-24--audit-tool-the-unused-function-count-was-three-times-too-high) |
| 2026-11-24 | [Klusters: every self-test is a ctest test](#2026-11-24--klusters-every-self-test-is-a-ctest-test) |
| 2026-11-24 | [Klusters: legibility and organization audit](#2026-11-24--klusters-legibility-and-organization-audit) |
| 2026-11-24 | [Klusters: Purge Small Clusters in the child view](#2026-11-24--klusters-purge-small-clusters-in-the-child-view) |
| 2026-11-24 | [Klusters: a click in a parent matrix leaves the child view](#2026-11-24--klusters-a-click-in-a-parent-matrix-leaves-the-child-view) |
| 2026-11-24 | [Klusters: Cancel Running Job](#2026-11-24--klusters-cancel-running-job) |
| 2026-11-24 | [Klusters: the cluster information dialog keeps its cluster](#2026-11-24--klusters-the-cluster-information-dialog-keeps-its-cluster) |
| 2026-11-24 | [Klusters: nothing edits the clustering while a job runs](#2026-11-24--klusters-nothing-edits-the-clustering-while-a-job-runs) |
| 2026-11-24 | [Klusters: a move of spikes no longer wipes the cluster information](#2026-11-24--klusters-a-move-of-spikes-no-longer-wipes-the-cluster-information) |
| 2026-11-24 | [Klusters: the children follow a palette merge of their parents](#2026-11-24--klusters-the-children-follow-a-palette-merge-of-their-parents) |
| 2026-11-24 | [Klusters: realign stays not undoable, and the manual says so](#2026-11-24--klusters-realign-stays-not-undoable-and-the-manual-says-so) |
| 2026-11-24 | [Klusters: a change of cluster information is an undo step](#2026-11-24--klusters-a-change-of-cluster-information-is-an-undo-step) |
| 2026-11-24 | [Klusters: the undo depth counts steps, not levels](#2026-11-24--klusters-the-undo-depth-counts-steps-not-levels) |
| 2026-11-24 | [Klusters: an undone child edit is written bad in the curation log](#2026-11-24--klusters-an-undone-child-edit-is-written-bad-in-the-curation-log) |
| 2026-11-24 | [Klusters: a saved undo depth of 2 is raised to 20](#2026-11-24--klusters-a-saved-undo-depth-of-2-is-raised-to-20) |
| 2026-11-24 | [Klusters: Repair Nesting and an atom compaction are undo steps](#2026-11-24--klusters-repair-nesting-and-an-atom-compaction-are-undo-steps) |
| 2026-11-24 | [Klusters: one undo history for both layers](#2026-11-24--klusters-one-undo-history-for-both-layers) |
| 2026-11-24 | [Klusters: undo and redo, five faults from an audit](#2026-11-24--klusters-undo-and-redo-five-faults-from-an-audit) |
| 2026-11-24 | [Klusters: the curation log covers the child layer](#2026-11-24--klusters-the-curation-log-covers-the-child-layer) |
| 2026-11-24 | [Klusters: the curation log says what started an action, what it did, and what was looked at and left](#2026-11-24--klusters-the-curation-log-says-what-started-an-action-what-it-did-and-what-was-looked-at-and-left) |
| 2026-11-23 | [Roadmap brought up to date](#2026-11-23--roadmap-brought-up-to-date) |
| 2026-11-23 | [Klusters: Recluster Against References](#2026-11-23--klusters-recluster-against-references) |
| 2026-11-23 | [Roadmap: what the collision-row work left open, and reclustering against references](#2026-11-23--roadmap-what-the-collision-row-work-left-open-and-reclustering-against-references) |
| 2026-11-23 | [Self-test: test_recluster_klustakwik read the chunk count off any line](#2026-11-23--self-test-test_recluster_klustakwik-read-the-chunk-count-off-any-line) |
| 2026-11-23 | [Klusters: a line of a child process is no longer cut where the other stream speaks](#2026-11-23--klusters-a-line-of-a-child-process-is-no-longer-cut-where-the-other-stream-speaks) |
| 2026-11-23 | [Klusters: realign measures a collision row on its corrected record](#2026-11-23--klusters-realign-measures-a-collision-row-on-its-corrected-record) |
| 2026-11-23 | [Core: a reference .spkc and shared cases for the collision fit](#2026-11-23--core-a-reference-spkc-and-shared-cases-for-the-collision-fit) |
| 2026-11-23 | [Klusters: Save As takes the stage's .eap with it](#2026-11-23--klusters-save-as-takes-the-stages-eap-with-it) |
| 2026-11-23 | [Klusters: Sort Spikes by Time carries the .eap and the .spkc](#2026-11-23--klusters-sort-spikes-by-time-carries-the-eap-and-the-spkc) |
| 2026-11-23 | [Klusters: collision-row state is dropped with the document](#2026-11-23--klusters-collision-row-state-is-dropped-with-the-document) |
| 2026-11-23 | [Klusters: a corrected collision row is placed by its corrected features](#2026-11-23--klusters-a-corrected-collision-row-is-placed-by-its-corrected-features) |
| 2026-11-23 | [Klusters: collision rows, four faults found in a review](#2026-11-23--klusters-collision-rows-four-faults-found-in-a-review) |
| 2026-11-23 | [Klusters: the quality dot of a cluster without a value is a white circle](#2026-11-23--klusters-the-quality-dot-of-a-cluster-without-a-value-is-a-white-circle) |
| 2026-11-23 | [Klusters: collision offsets follow realign and nudge, and are saved](#2026-11-23--klusters-collision-offsets-follow-realign-and-nudge-and-are-saved) |
| 2026-11-23 | [Klusters: a collision row is shown as its own unit's spike](#2026-11-23--klusters-a-collision-row-is-shown-as-its-own-units-spike) |
| 2026-11-23 | [Core: collision rows with the partner subtracted, and the sparse `.spkc` that holds them](#2026-11-23--core-collision-rows-with-the-partner-subtracted-and-the-sparse-spkc-that-holds-them) |
| 2026-11-22 | [Klusters: the quality dot has no perimeter](#2026-11-22--klusters-the-quality-dot-has-no-perimeter) |
| 2026-11-22 | [Klusters: a quality dot per cluster in the waveform view](#2026-11-22--klusters-a-quality-dot-per-cluster-in-the-waveform-view) |
| 2026-11-21 | [Klusters: in the child view, a click on the curated parent returns to the parent view](#2026-11-21--klusters-in-the-child-view-a-click-on-the-curated-parent-returns-to-the-parent-view) |
| 2026-11-21 | [Klusters: the refresh after a realign no longer takes the child view to the parents](#2026-11-21--klusters-the-refresh-after-a-realign-no-longer-takes-the-child-view-to-the-parents) |
| 2026-11-21 | [Klusters: child ids are disjoint from parent ids — a parent's own child is parent + 10000, every other child 20000 and up](#2026-11-21--klusters-child-ids-are-disjoint-from-parent-ids--a-parents-own-child-is-parent--10000-every-other-child-20000-and-up) |
| 2026-11-21 | [Klusters: Preferences pages scroll instead of being squeezed](#2026-11-21--klusters-preferences-pages-scroll-instead-of-being-squeezed) |
| 2026-11-20 | [Klusters: the spike under the cursor is a preference — on or off, the ring's radius and line, the trace's width](#2026-11-20--klusters-the-spike-under-the-cursor-is-a-preference--on-or-off-the-rings-radius-and-line-the-traces-width) |
| 2026-11-20 | [Klusters: Split Off Spurious Spikes on Ctrl+Space, its knobs in Preferences ▸ Splitting](#2026-11-20--klusters-split-off-spurious-spikes-on-ctrlspace-its-knobs-in-preferences--splitting) |
| 2026-11-20 | [Klusters: Split Off Spurious Spikes orders on the GPU (CUDA)](#2026-11-20--klusters-split-off-spurious-spikes-orders-on-the-gpu-cuda) |
| 2026-11-20 | [Klusters: Split Off Spurious Spikes — a cluster's core kept, the rest split off](#2026-11-20--klusters-split-off-spurious-spikes--a-clusters-core-kept-the-rest-split-off) |
| 2026-11-20 | [Klusters: Split Burst Followers on children](#2026-11-20--klusters-split-burst-followers-on-children) |
| 2026-11-20 | [Klusters: Split Burst Followers with two references and a mixed cluster](#2026-11-20--klusters-split-burst-followers-with-two-references-and-a-mixed-cluster) |
| 2026-11-20 | [KiloKlustaKwik: a heap overflow in the split trial with -MaxPossibleClusters below 13](#2026-11-20--kiloklustakwik-a-heap-overflow-in-the-split-trial-with--maxpossibleclusters-below-13) |
| 2026-11-20 | [KiloKlustaKwik, process_denoiseuniform: a binary `.fet` or `.clu` whose count has a low byte of 0x30–0x39 is no longer read as text](#2026-11-20--kiloklustakwik-process_denoiseuniform-a-binary-fet-or-clu-whose-count-has-a-low-byte-of-0x300x39-is-no-longer-read-as-text) |
| 2026-11-20 | [Klusters: Split Burst Followers — two similarly shaped cells told apart by their bursts](#2026-11-20--klusters-split-burst-followers--two-similarly-shaped-cells-told-apart-by-their-bursts) |
| 2026-11-20 | [Klusters: a super-sampled space is read when it is first shown, not at open; super-sample mode no longer shows it](#2026-11-20--klusters-a-super-sampled-space-is-read-when-it-is-first-shown-not-at-open-super-sample-mode-no-longer-shows-it) |
| 2026-11-20 | [Klusters: the spike under the cursor in the feature view is drawn over the waveform view](#2026-11-20--klusters-the-spike-under-the-cursor-in-the-feature-view-is-drawn-over-the-waveform-view) |
| 2026-11-19 | [Klusters: a Time chunks preference for KlustaKwik reclusters](#2026-11-19--klusters-a-time-chunks-preference-for-klustakwik-reclusters) |
| 2026-11-19 | [KiloKlustaKwik: the time column is always the time axis](#2026-11-19--kiloklustakwik-the-time-column-is-always-the-time-axis) |
| 2026-11-19 | [Klusters: the cluster palette follows clusters moved to the end](#2026-11-19--klusters-the-cluster-palette-follows-clusters-moved-to-the-end) |
| 2026-11-19 | [Klusters: a multi-lasso split no longer selects a neighbouring cluster](#2026-11-19--klusters-a-multi-lasso-split-no-longer-selects-a-neighbouring-cluster) |
| 2026-11-19 | [Klusters: Strip Residual Outliers applies without a confirmation box](#2026-11-19--klusters-strip-residual-outliers-applies-without-a-confirmation-box) |
| 2026-11-18 | [Klusters: Strip Residual Outliers works on children — scored whole, split into a sibling child](#2026-11-18--klusters-strip-residual-outliers-works-on-children--scored-whole-split-into-a-sibling-child) |
| 2026-11-18 | [Klusters: Strip Residual Outliers runs per time chunk — the Template view's partition, or the default template chunk length](#2026-11-18--klusters-strip-residual-outliers-runs-per-time-chunk--the-template-views-partition-or-the-default-template-chunk-length) |
| 2026-11-18 | [Input preferences: Reset restores the default, a second press unbinds — and an unbound command stays unbound](#2026-11-18--input-preferences-reset-restores-the-default-a-second-press-unbinds--and-an-unbound-command-stays-unbound) |
| 2026-11-18 | [Klusters: the radial menu keeps an open ring while the cursor is moving](#2026-11-18--klusters-the-radial-menu-keeps-an-open-ring-while-the-cursor-is-moving) |
| 2026-11-18 | [Klusters: the RMS recenter and the PCA refine run under the correlation window](#2026-11-18--klusters-the-rms-recenter-and-the-pca-refine-run-under-the-correlation-window) |
| 2026-11-18 | [Klusters: a seed cluster for the residual strip -- the template AND the threshold from a carved core](#2026-11-18--klusters-a-seed-cluster-for-the-residual-strip--the-template-and-the-threshold-from-a-carved-core) |
| 2026-11-18 | [Klusters: Strip Residual Outliers -- the per-spike gain divided out, the residual split on-channel / off-channel](#2026-11-18--klusters-strip-residual-outliers--the-per-spike-gain-divided-out-the-residual-split-on-channel--off-channel) |
| 2026-11-12 | [Klusters: a renumber-to-end (T) or a sort between an edit and its post-edit chain left the parked landing and the queued realign on the old ids -- a neighbouring cluster joined the selection](#2026-11-12--klusters-a-renumber-to-end-t-or-a-sort-between-an-edit-and-its-post-edit-chain-left-the-parked-landing-and-the-queued-realign-on-the-old-ids----a-neighbouring-cluster-joined-the-selection) |
| 2026-11-12 | [Klusters: the Space box rescans the session's feature spaces when opened](#2026-11-12--klusters-the-space-box-rescans-the-sessions-feature-spaces-when-opened) |
| 2026-11-11 | [Klusters: a super-sampled space is re-cut with the interpolator it was built with (detected from its rows), not the preference](#2026-11-11--klusters-a-super-sampled-space-is-re-cut-with-the-interpolator-it-was-built-with-detected-from-its-rows-not-the-preference) |
| 2026-11-11 | [Klusters: a detached feature space (no basis -- `.fet.manifold`) no longer blocks the realign of the spaces it shares waveforms with; its time column follows, realign in it is refused](#2026-11-11--klusters-a-detached-feature-space-no-basis----fetmanifold-no-longer-blocks-the-realign-of-the-spaces-it-shares-waveforms-with-its-time-column-follows-realign-in-it-is-refused) |
| 2026-11-11 | [`ndm_pca` on super-sampled waveforms: the basis in ticks, the `.fet` with the phase column; a shared `.res` is never a `_U` tick `.res`](#2026-11-11--ndm_pca-on-super-sampled-waveforms-the-basis-in-ticks-the-fet-with-the-phase-column-a-shared-res-is-never-a-_u-tick-res) |
| 2026-11-11 | [Super-sampled extraction through the ndm engines; the smoothing spline; the `_U` stderiv from the interpolant's derivative; the spline's halved curvature fixed](#2026-11-11--super-sampled-extraction-through-the-ndm-engines-the-smoothing-spline-the-_u-stderiv-from-the-interpolants-derivative-the-splines-halved-curvature-fixed) |
| 2026-11-11 | [Klusters: the realign shift cap in a super-sampled space, phase-aware currency of spaces (realign-in-shown-space, step 3 of 3)](#2026-11-11--klusters-the-realign-shift-cap-in-a-super-sampled-space-phase-aware-currency-of-spaces-realign-in-shown-space-step-3-of-3) |
| 2026-11-11 | [Klusters: realign and nudge run in the shown feature space; the `.clu`'s own space follows (realign-in-shown-space, step 2 of 3)](#2026-11-11--klusters-realign-and-nudge-run-in-the-shown-feature-space-the-clus-own-space-follows-realign-in-shown-space-step-2-of-3) |
| 2026-11-11 | [One tick `.res` per factor for all spaces, named after the shared `.res` (`.res.stderiv_S3_U4.6`); every `_U<f>` space carries the same phases](#2026-11-11--one-tick-res-per-factor-for-all-spaces-named-after-the-shared-res-resstderiv_s3_u46-every-_uf-space-carries-the-same-phases) |
| 2026-11-11 | [Klusters: the working set of a feature space; the follow after a realign is per-file geometry and can follow the `.clu`'s own space (realign-in-shown-space, step 1 of 3)](#2026-11-11--klusters-the-working-set-of-a-feature-space-the-follow-after-a-realign-is-per-file-geometry-and-can-follow-the-clus-own-space-realign-in-shown-space-step-1-of-3) |
| 2026-11-11 | [Klusters: the token of a `.pending` working copy -- the shown `.spk` was read as untagged (super-sample built `_U4` over stderiv waveforms; `%spk` was empty; a `_U` session opened at the base geometry)](#2026-11-11--klusters-the-token-of-a-pending-working-copy----the-shown-spk-was-read-as-untagged-super-sample-built-_u4-over-stderiv-waveforms-spk-was-empty-a-_u-session-opened-at-the-base-geometry) |
| 2026-11-11 | [Klusters: super-sampling an untagged legacy session built a bare `_U4` token](#2026-11-11--klusters-super-sampling-an-untagged-legacy-session-built-a-bare-_u4-token) |
| 2026-11-11 | [Super-sampled spike times: `.res.<token>_U<f>.N` in ticks; the shared `.res` is always base-rate; Klusters exports a `_U` session at the recording's sampling](#2026-11-11--super-sampled-spike-times-restoken_ufn-in-ticks-the-shared-res-is-always-base-rate-klusters-exports-a-_u-session-at-the-recordings-sampling) |
| 2026-11-11 | [Klusters: an atom's realign lands on the atom again -- the landing's parent selection no longer counts as a click](#2026-11-11--klusters-an-atoms-realign-lands-on-the-atom-again----the-landings-parent-selection-no-longer-counts-as-a-click) |
| 2026-11-11 | [Klusters: the curation matrices follow the palette selection; a matrix lost off screen comes back](#2026-11-11--klusters-the-curation-matrices-follow-the-palette-selection-a-matrix-lost-off-screen-comes-back) |
| 2026-11-11 | [Klusters: a delete at the end of the child list lands on the new last row, in list order](#2026-11-11--klusters-a-delete-at-the-end-of-the-child-list-lands-on-the-new-last-row-in-list-order) |
| 2026-11-11 | [Klusters: an atom's realign lands back on the atom](#2026-11-11--klusters-an-atoms-realign-lands-back-on-the-atom) |
| 2026-11-11 | [Klusters: one curation-matrix refresh per hierarchy gesture (no child -> fiber -> child flash)](#2026-11-11--klusters-one-curation-matrix-refresh-per-hierarchy-gesture-no-child---fiber---child-flash) |
| 2026-11-11 | [Klusters: a fiber clicked over an atom selection leaves the child view](#2026-11-11--klusters-a-fiber-clicked-over-an-atom-selection-leaves-the-child-view) |
| 2026-11-11 | [Klusters: the super-sample MODE builds the space on open; Save As adopts an existing space; `%spk`](#2026-11-11--klusters-the-super-sample-mode-builds-the-space-on-open-save-as-adopts-an-existing-space-spk) |
| 2026-11-11 | [Klusters: super-sample mode -- waveform spaces at 2x / 4x the sampling, realign and nudge at sub-sample resolution](#2026-11-11--klusters-super-sample-mode--waveform-spaces-at-2x--4x-the-sampling-realign-and-nudge-at-sub-sample-resolution) |
| 2026-11-11 | [core: the `_U<factor>` super-sampled token, `supersample.hpp`, a super-sampled cut, `pca_fit.hpp`](#2026-11-11--core-the-_ufactor-super-sampled-token-supersamplehpp-a-super-sampled-cut-pca_fithpp) |
| 2026-11-10 | [Klusters: the Refine mode -- a plugin's children under the same parents](#2026-11-10--klusters-the-refine-mode--a-plugins-children-under-the-same-parents) |
| 2026-11-10 | [Klusters: a plugin descriptor that does not parse says so](#2026-11-10--klusters-a-plugin-descriptor-that-does-not-parse-says-so) |
| 2026-11-10 | [Klusters: feature spaces an engine writes in the sub-session come back as session-level spaces](#2026-11-10--klusters-feature-spaces-an-engine-writes-in-the-sub-session-come-back-as-session-level-spaces) |
| 2026-11-09 | [ndm_extractspikes / ndm_detectspikes `--group N[,M...]`](#2026-11-09--ndm_extractspikes--ndm_detectspikes---group-nm) |
| 2026-11-09 | [Klusters: every sort of the wheel works at the atom level](#2026-11-09--klusters-every-sort-of-the-wheel-works-at-the-atom-level) |
| 2026-11-09 | [Klusters: sorting the curation matrices and the child palette at the atom level](#2026-11-09--klusters-sorting-the-curation-matrices-and-the-child-palette-at-the-atom-level) |
| 2026-11-07 | [Klusters: the Reclustering preferences page around the engine](#2026-11-07--klusters-the-reclustering-preferences-page-around-the-engine) |
| 2026-11-07 | [Klusters: %tokens in plugin parameter values](#2026-11-07--klusters-tokens-in-plugin-parameter-values) |
| 2026-11-07 | [Klusters: the active recluster engine, and the radial menu's Recluster ▸ Plugin ring](#2026-11-07--klusters-the-active-recluster-engine-and-the-radial-menus-recluster--plugin-ring) |
| 2026-11-07 | [Klusters: recluster plugins on the sub-session, typed descriptors, Preferences → Plugins](#2026-11-07--klusters-recluster-plugins-on-the-sub-session-typed-descriptors-preferences--plugins) |
| 2026-11-07 | [Klusters: Recluster with fiber-kit -- a plan on the selected clusters](#2026-11-07--klusters-recluster-with-fiber-kit--a-plan-on-the-selected-clusters) |
| 2026-11-07 | [Klusters: leaving the child palette for the parent palette clears the atom selection](#2026-11-07--klusters-leaving-the-child-palette-for-the-parent-palette-clears-the-atom-selection) |
| 2026-11-07 | [Klusters: the child palette follows a parent-level landing](#2026-11-07--klusters-the-child-palette-follows-a-parent-level-landing) |
| 2026-11-07 | [Klusters: child-level delete and watershed, and where the selection lands after a split](#2026-11-07--klusters-child-level-delete-and-watershed-and-where-the-selection-lands-after-a-split) |
| 2026-11-07 | [Klusters curation log: the hierarchy commands and the atom layer](#2026-11-07--klusters-curation-log-the-hierarchy-commands-and-the-atom-layer) |
| 2026-11-07 | [Klusters curation log: checkpoint on save, refractory and cell-type statistics, pair evidence](#2026-11-07--klusters-curation-log-checkpoint-on-save-refractory-and-cell-type-statistics-pair-evidence) |
| 2026-11-06 | [ndm_extractspikes reads the .res ndm_detectspikes writes](#2026-11-06--ndm_extractspikes-reads-the-res-ndm_detectspikes-writes) |
| 2026-11-06 | [ndm: tab completion of options, their values and spike groups](#2026-11-06--ndm-tab-completion-of-options-their-values-and-spike-groups) |
| 2026-11-05 | [Man pages for every ndm step, generated from the command reference](#2026-11-05--man-pages-for-every-ndm-step-generated-from-the-command-reference) |
| 2026-11-05 | [ndm: one command for the NDManager plugins](#2026-11-05--ndm-one-command-for-the-ndmanager-plugins) |
| 2026-11-04 | [ndm_estimategeometry: the estimate goes into the probe file, no report](#2026-11-04--ndm_estimategeometry-the-estimate-goes-into-the-probe-file-no-report) |
| 2026-11-04 | [ndm_estimategeometry: a group never calibrates its own estimate](#2026-11-04--ndm_estimategeometry-a-group-never-calibrates-its-own-estimate) |
| 2026-11-04 | [ndm_estimategeometry: a named group is estimated even with a geometry; wire-bundle templates do not count](#2026-11-04--ndm_estimategeometry-a-named-group-is-estimated-even-with-a-geometry-wire-bundle-templates-do-not-count) |
| 2026-11-04 | [ndm_estimategeometry: command-line options as in ndm_pca](#2026-11-04--ndm_estimategeometry-command-line-options-as-in-ndm_pca) |
| 2026-11-04 | [ndm_estimategeometry: wire-bundle site layout from channel coherence](#2026-11-04--ndm_estimategeometry-wire-bundle-site-layout-from-channel-coherence) |
| 2026-11-02 | [NeuroScope: the spectral view updates when a setting changes](#2026-11-02--neuroscope-the-spectral-view-updates-when-a-setting-changes) |
| 2026-10-29 | [Klusters: the matrices are shown on open by default](#2026-10-29--klusters-the-matrices-are-shown-on-open-by-default) |
| 2026-10-29 | [Settings move to ~/.config/neurosuite-3](#2026-10-29--settings-move-to-configneurosuite-3) |
| 2026-10-29 | [process_extractemg: the CPU FastICA step is faster per core](#2026-10-29--process_extractemg-the-cpu-fastica-step-is-faster-per-core) |
| 2026-10-29 | [ndm_extractemg: the output gives the time of FastICA and of each pass](#2026-10-29--ndm_extractemg-the-output-gives-the-time-of-fastica-and-of-each-pass) |
| 2026-10-29 | [process_extractemg: event detection no longer copies the EMG trace](#2026-10-29--process_extractemg-event-detection-no-longer-copies-the-emg-trace) |
| 2026-10-29 | [process_extractemg: the streaming pass uses every thread for one probe too](#2026-10-29--process_extractemg-the-streaming-pass-uses-every-thread-for-one-probe-too) |
| 2026-10-29 | [process_extractemg: no second copy of the whitened data on the CPU](#2026-10-29--process_extractemg-no-second-copy-of-the-whitened-data-on-the-cpu) |
| 2026-10-29 | [process_extractemg: with several probes, FastICA uses every thread](#2026-10-29--process_extractemg-with-several-probes-fastica-uses-every-thread) |
| 2026-10-29 | [process_extractemg: FastICA gives the same result on every run and for any thread count](#2026-10-29--process_extractemg-fastica-gives-the-same-result-on-every-run-and-for-any-thread-count) |
| 2026-10-28 | [Docs: CUDA on Ubuntu 26.04](#2026-10-28--docs-cuda-on-ubuntu-2604) |
| 2026-10-28 | [Build: without a visible GPU, CUDA architectures the toolkit cannot compile are left out](#2026-10-28--build-without-a-visible-gpu-cuda-architectures-the-toolkit-cannot-compile-are-left-out) |
| 2026-10-28 | [ndmanager-plugins: the CUDA filters, thresholds and grouper are built for the configured GPU architectures](#2026-10-28--ndmanager-plugins-the-cuda-filters-thresholds-and-grouper-are-built-for-the-configured-gpu-architectures) |
| 2026-10-28 | [Klusters: the CUDA code is built for the configured GPU architectures](#2026-10-28--klusters-the-cuda-code-is-built-for-the-configured-gpu-architectures) |
| 2026-10-28 | [Build: parallel jobs are limited to the installed memory](#2026-10-28--build-parallel-jobs-are-limited-to-the-installed-memory) |
| 2026-10-27 | [Klusters: Hierarchy ▸ Break Up Parent by Template sends each child to the large parent it matches](#2026-10-27--klusters-hierarchy--break-up-parent-by-template-sends-each-child-to-the-large-parent-it-matches) |
| 2026-10-27 | [Klusters: Merge Orphan Parents says why when it merges nothing](#2026-10-27--klusters-merge-orphan-parents-says-why-when-it-merges-nothing) |
| 2026-10-27 | [Klusters: Undo Child-Layer Edit is enabled right after Merge Orphan Children](#2026-10-27--klusters-undo-child-layer-edit-is-enabled-right-after-merge-orphan-children) |
| 2026-10-27 | [Klusters: Merge Orphan Children leaves the orphans that do not match alone](#2026-10-27--klusters-merge-orphan-children-leaves-the-orphans-that-do-not-match-alone) |
| 2026-10-27 | [Klusters: Merge Orphan Parents matches against every parent not being merged, and leaves the unmatched alone](#2026-10-27--klusters-merge-orphan-parents-matches-against-every-parent-not-being-merged-and-leaves-the-unmatched-alone) |
| 2026-10-26 | [Klusters: Hierarchy ▸ Merge Orphan Parents gathers the tiny parents by template](#2026-10-26--klusters-hierarchy--merge-orphan-parents-gathers-the-tiny-parents-by-template) |
| 2026-10-26 | [Klusters: a recluster or drift job no longer adds a tab-change connection](#2026-10-26--klusters-a-recluster-or-drift-job-no-longer-adds-a-tab-change-connection) |
| 2026-10-26 | [Klusters: a recluster that ends early removes its temporary files](#2026-10-26--klusters-a-recluster-that-ends-early-removes-its-temporary-files) |
| 2026-10-26 | [Klusters: the drift actions do not start while another job runs](#2026-10-26--klusters-the-drift-actions-do-not-start-while-another-job-runs) |
| 2026-10-26 | [Klusters: the end of a drift job no longer locks Klusters](#2026-10-26--klusters-the-end-of-a-drift-job-no-longer-locks-klusters) |
| 2026-10-26 | [Klusters: an output tab's last line gives the program's exit code](#2026-10-26--klusters-an-output-tabs-last-line-gives-the-programs-exit-code) |
| 2026-10-26 | [Klusters: a recluster opens its output tab only if it fails](#2026-10-26--klusters-a-recluster-opens-its-output-tab-only-if-it-fails) |
| 2026-10-26 | [Klusters: View ▸ Show Child Cluster Palette and Show Template Palette](#2026-10-26--klusters-view--show-child-cluster-palette-and-show-template-palette) |
| 2026-10-26 | [Klusters: View ▸ Show Reserved Chunk Clusters hides them from the palette and the displays](#2026-10-26--klusters-view--show-reserved-chunk-clusters-hides-them-from-the-palette-and-the-displays) |
| 2026-10-26 | [Klusters: reserved chunk clusters are shown as R0 … R99](#2026-10-26--klusters-reserved-chunk-clusters-are-shown-as-r0--r99) |
| 2026-10-26 | [Klusters: Align All and the automatic realign skip reserved chunk clusters](#2026-10-26--klusters-align-all-and-the-automatic-realign-skip-reserved-chunk-clusters) |
| 2026-10-26 | [Klusters: the matrices leave reserved chunk clusters out](#2026-10-26--klusters-the-matrices-leave-reserved-chunk-clusters-out) |
| 2026-10-26 | [Klusters: renumbering and sorting keep the reserved chunk clusters' ids](#2026-10-26--klusters-renumbering-and-sorting-keep-the-reserved-chunk-clusters-ids) |
| 2026-10-26 | [Klusters: a reserved chunk cluster gains no spikes, and only reclustering and feature-space selections take spikes out of it](#2026-10-26--klusters-a-reserved-chunk-cluster-gains-no-spikes-and-only-reclustering-and-feature-space-selections-take-spikes-out-of-it) |
| 2026-10-26 | [Formats: the .clu header is the number of distinct ids](#2026-10-26--formats-the-clu-header-is-the-number-of-distinct-ids) |
| 2026-10-25 | [Klusters: new clusters are numbered below the reserved chunk clusters](#2026-10-25--klusters-new-clusters-are-numbered-below-the-reserved-chunk-clusters) |
| 2026-10-24 | [Klusters: the waveform sorts can use mean waveforms](#2026-10-24--klusters-the-waveform-sorts-can-use-mean-waveforms) |
| 2026-10-24 | [Klusters: a cluster sort logs one Sort All Clusters entry](#2026-10-24--klusters-a-cluster-sort-logs-one-sort-all-clusters-entry) |
| 2026-10-23 | [ndm_* commands: -h / --help everywhere, and no step runs on a flag it does not know](#2026-10-23--ndm_-commands--h----help-everywhere-and-no-step-runs-on-a-flag-it-does-not-know) |
| 2026-10-22 | [Klusters: the Profile template metric survives a restart](#2026-10-22--klusters-the-profile-template-metric-survives-a-restart) |
| 2026-10-22 | [Klusters: Preferences ▸ Default resets every setting](#2026-10-22--klusters-preferences--default-resets-every-setting) |
| 2026-10-22 | [Klusters: Preferences regrouped into General, Views and Curation](#2026-10-22--klusters-preferences-regrouped-into-general-views-and-curation) |
| 2026-10-21 | [macOS: the `ndm_*` steps work with the BSD tools](#2026-10-21--macos-the-ndm_-steps-work-with-the-bsd-tools) |
| 2026-10-21 | [macOS: the `ndm_*` steps run with Homebrew's bash](#2026-10-21--macos-the-ndm_-steps-run-with-homebrews-bash) |
| 2026-10-21 | [New icons for neurosuite-3, Klusters, NeuroScope and NDManager](#2026-10-21--new-icons-for-neurosuite-3-klusters-neuroscope-and-ndmanager) |
| 2026-10-21 | [macOS: checking the Metal backend (kernel lint, on-device compile test, `NS_DISABLE_METAL`, `metal_validate.sh`)](#2026-10-21--macos-checking-the-metal-backend-kernel-lint-on-device-compile-test-ns_disable_metal-metal_validatesh) |
| 2026-10-21 | [Klusters: Align All realigns as Realign Spikes does](#2026-10-21--klusters-align-all-realigns-as-realign-spikes-does) |
| 2026-10-21 | [Klusters: Align All and post-edit realigns update the other feature spaces](#2026-10-21--klusters-align-all-and-post-edit-realigns-update-the-other-feature-spaces) |
| 2026-10-21 | [ndm_pca: `--force` refits and overwrites stale features](#2026-10-21--ndm_pca---force-refits-and-overwrites-stale-features) |
| 2026-10-21 | [ndm_extractspikes: a session-file check per method, and `--allMethods`](#2026-10-21--ndm_extractspikes-a-session-file-check-per-method-and---allmethods) |
| 2026-10-21 | [ndm_detectspikes / ndm_extractspikes: each spike group cut with its own window](#2026-10-21--ndm_detectspikes--ndm_extractspikes-each-spike-group-cut-with-its-own-window) |
| 2026-10-21 | [macOS: KiloKlustaKwik on the Apple GPU (CEM steps)](#2026-10-21--macos-kiloklustakwik-on-the-apple-gpu-cem-steps) |
| 2026-10-21 | [KiloKlustaKwik GPU builds: the spatial phase of CEMTwoPhase computes the right distances](#2026-10-21--kiloklustakwik-gpu-builds-the-spatial-phase-of-cemtwophase-computes-the-right-distances) |
| 2026-10-21 | [macOS: Klusters on the Apple GPU (realignment, error matrix)](#2026-10-21--macos-klusters-on-the-apple-gpu-realignment-error-matrix) |
| 2026-10-21 | [Klusters realign: moved spikes get a fresh window from the .fil, never the rolled copy](#2026-10-21--klusters-realign-moved-spikes-get-a-fresh-window-from-the-fil-never-the-rolled-copy) |
| 2026-10-21 | [ndm_extractspikes: noise statistics for the clusterers' whitener (`.nst`)](#2026-10-21--ndm_extractspikes-noise-statistics-for-the-clusterers-whitener-nst) |
| 2026-10-21 | [process_medianfilter: CPU builds no longer corrupt recordings larger than one chunk](#2026-10-21--process_medianfilter-cpu-builds-no-longer-corrupt-recordings-larger-than-one-chunk) |
| 2026-10-21 | [macOS: Metal paths for the high-pass filters and channel grouping](#2026-10-21--macos-metal-paths-for-the-high-pass-filters-and-channel-grouping) |
| 2026-10-21 | [macOS: a Metal (Apple GPU) backend, first for the noise thresholds](#2026-10-21--macos-a-metal-apple-gpu-backend-first-for-the-noise-thresholds) |
| 2026-10-21 | [macOS: Apple Accelerate for the spectrogram FFT and the EMG kernel](#2026-10-21--macos-apple-accelerate-for-the-spectrogram-fft-and-the-emg-kernel) |
| 2026-10-21 | [CI: one workflow per operating system, with a status badge each](#2026-10-21--ci-one-workflow-per-operating-system-with-a-status-badge-each) |
| 2026-10-21 | [CI: a macOS build](#2026-10-21--ci-a-macos-build) |
| 2026-10-21 | [Klusters: the curation log is `<base>.cur.<method>.<group>`](#2026-10-21--klusters-the-curation-log-is-basecurmethodgroup) |
| 2026-10-19 | [Pipeline: the KiloKlustaKwik calls match KiloKlustaKwik](#2026-10-19--pipeline-the-kiloklustakwik-calls-match-kiloklustakwik) |
| 2026-10-19 | [NDManager: Add offers the parameters a plugin reads](#2026-10-19--ndmanager-add-offers-the-parameters-a-plugin-reads) |
| 2026-10-19 | [Docs: every page checked against the code; current in-app handbooks; the site covers all documentation](#2026-10-19--docs-every-page-checked-against-the-code-current-in-app-handbooks-the-site-covers-all-documentation) |
| 2026-10-19 | [Docs: one installation guide for every platform](#2026-10-19--docs-one-installation-guide-for-every-platform) |
| 2026-10-19 | [Windows: OpenMP code that MSVC accepts; the CI build gets further](#2026-10-19--windows-openmp-code-that-msvc-accepts-the-ci-build-gets-further) |
| 2026-10-19 | [Windows: the ndm_* steps run from cmd and PowerShell through Git Bash](#2026-10-19--windows-the-ndm_-steps-run-from-cmd-and-powershell-through-git-bash) |
| 2026-10-19 | [Windows: the install puts the programs on PATH](#2026-10-19--windows-the-install-puts-the-programs-on-path) |
| 2026-10-19 | [Windows: one script installs the build dependencies](#2026-10-19--windows-one-script-installs-the-build-dependencies) |
| 2026-10-19 | [Windows: the ndmanager-plugins programs build natively, and CI checks it](#2026-10-19--windows-the-ndmanager-plugins-programs-build-natively-and-ci-checks-it) |
| 2026-10-19 | [Windows: `cmake --install` gives a folder that runs](#2026-10-19--windows-cmake---install-gives-a-folder-that-runs) |
| 2026-10-19 | [CI: GitHub actions moved to their Node.js 24 releases](#2026-10-19--ci-github-actions-moved-to-their-nodejs-24-releases) |
| 2026-10-19 | [Windows: fixes from the first Windows configure](#2026-10-19--windows-fixes-from-the-first-windows-configure) |
| 2026-10-18 | [Klusters: the Intel GPU (SYCL) backend can be configured with icx on Windows](#2026-10-18--klusters-the-intel-gpu-sycl-backend-can-be-configured-with-icx-on-windows) |
| 2026-10-18 | [Windows: the GUI programs build with MSVC, and CI checks it](#2026-10-18--windows-the-gui-programs-build-with-msvc-and-ci-checks-it) |
| 2026-10-18 | [CI builds and tests on Ubuntu 24.04, 25.10, 26.04 and 26.10](#2026-10-18--ci-builds-and-tests-on-ubuntu-2404-2510-2604-and-2610) |
| 2026-10-18 | [CI installs SciPy; the plugins' SciPy dependency documented as it is](#2026-10-18--ci-installs-scipy-the-plugins-scipy-dependency-documented-as-it-is) |
| 2026-10-18 | [CI builds again: two libklustersshared tests missed the export header](#2026-10-18--ci-builds-again-two-libklustersshared-tests-missed-the-export-header) |
| 2026-10-18 | [Docs: roadmap and collision-decomposition workflow brought up to date](#2026-10-18--docs-roadmap-and-collision-decomposition-workflow-brought-up-to-date) |
| 2026-10-18 | [Klusters: one table for action icons](#2026-10-18--klusters-one-table-for-action-icons) |
| 2026-10-18 | [Klusters: edit the radial menu in Preferences](#2026-10-18--klusters-edit-the-radial-menu-in-preferences) |
| 2026-10-17 | [Klusters: radial context menu (proof of concept)](#2026-10-17--klusters-radial-context-menu-proof-of-concept) |
| 2026-10-17 | [Spike extraction: one .spk row per .res time; shared extractor code](#2026-10-17--spike-extraction-one-spk-row-per-res-time-shared-extractor-code) |
| 2026-10-17 | [Reference files for the template-library formats; one copy of the extractor helpers; plugins configure on their own](#2026-10-17--reference-files-for-the-template-library-formats-one-copy-of-the-extractor-helpers-plugins-configure-on-their-own) |
| 2026-10-17 | [Core fixes: long tokens and groups, resolveAny order, int16 saturation, deterministic t-SNE; old resolver removed](#2026-10-17--core-fixes-long-tokens-and-groups-resolveany-order-int16-saturation-deterministic-t-sne-old-resolver-removed) |
| 2026-10-17 | [kiloklustakwik: heap overflow and -PriorFile fixes; KK_chunked.cpp split; driver comparison](#2026-10-17--kiloklustakwik-heap-overflow-and--priorfile-fixes-kk_chunkedcpp-split-driver-comparison) |
| 2026-10-17 | [Qt-free code moves into libneurosuite-core, with tests; merge ranking fix](#2026-10-17--qt-free-code-moves-into-libneurosuite-core-with-tests-merge-ranking-fix) |
| 2026-10-17 | [neurosuite-3 3.1.0: a version contract with fiber-kit and ndm plugins](#2026-10-17--neurosuite-3-310-a-version-contract-with-fiber-kit-and-ndm-plugins) |
| 2026-10-17 | [Recipes and canonical references; process_pca records the right input channel count; Qt build fix](#2026-10-17--recipes-and-canonical-references-process_pca-records-the-right-input-channel-count-qt-build-fix) |
| 2026-10-16 | [One stderiv transform, one token rule: shared tables for both](#2026-10-16--one-stderiv-transform-one-token-rule-shared-tables-for-both) |
| 2026-10-16 | [CI runs the tests and a lint job; HDF5 1.10 build fix; generated files removed](#2026-10-16--ci-runs-the-tests-and-a-lint-job-hdf5-110-build-fix-generated-files-removed) |
| 2026-10-16 | [Build and docs: one C++ standard, standard test layout, how to run the tests](#2026-10-16--build-and-docs-one-c-standard-standard-test-layout-how-to-run-the-tests) |
| 2026-10-16 | [Spike detection: sdiffPairs as arithmetic expressions (stderiv_C6)](#2026-10-16--spike-detection-sdiffpairs-as-arithmetic-expressions-stderiv_c6) |
| 2026-10-16 | [ndManager: export a session as a template, apply a template to a session](#2026-10-16--ndmanager-export-a-session-as-a-template-apply-a-template-to-a-session) |
| 2026-10-16 | [ndManager: parameter tables by keyboard, typed values; plugins listed by purpose](#2026-10-16--ndmanager-parameter-tables-by-keyboard-typed-values-plugins-listed-by-purpose) |
| 2026-10-16 | [Units: brain region, division, atlas and region ID](#2026-10-16--units-brain-region-division-atlas-and-region-id) |
| 2026-10-16 | [Klusters: saves are all-or-nothing and skip unchanged files; autosave covers hierarchical sessions; editing locked during a save](#2026-10-16--klusters-saves-are-all-or-nothing-and-skip-unchanged-files-autosave-covers-hierarchical-sessions-editing-locked-during-a-save) |
| 2026-10-16 | [Klusters: save-system fixes; Promote to Hierarchical Session](#2026-10-16--klusters-save-system-fixes-promote-to-hierarchical-session) |
| 2026-10-16 | [ndManager: audit fixes — saves, Pipeline tab, Probes tab, Probe Maker](#2026-10-16--ndmanager-audit-fixes--saves-pipeline-tab-probes-tab-probe-maker) |
| 2026-10-15 | [ndManager: New from Project Template; saves keep comments, inline lists and quoting](#2026-10-15--ndmanager-new-from-project-template-saves-keep-comments-inline-lists-and-quoting) |
| 2026-10-15 | [Audit fixes: ndmanager keeps unknown YAML keys; aligners and re-extraction follow the .spk convention; re-extraction threshold base](#2026-10-15--audit-fixes-ndmanager-keeps-unknown-yaml-keys-aligners-and-re-extraction-follow-the-spk-convention-re-extraction-threshold-base) |
| 2026-10-15 | [Klusters: t-SNE of the oblique coordinates](#2026-10-15--klusters-t-sne-of-the-oblique-coordinates) |
| 2026-10-14 | [ndManager: empty program parameters are no longer written as `~`](#2026-10-14--ndmanager-empty-program-parameters-are-no-longer-written-as-) |
| 2026-10-14 | [ndm_detectspikes (stderiv): refuse one-channel groups and flat signals](#2026-10-14--ndm_detectspikes-stderiv-refuse-one-channel-groups-and-flat-signals) |
| 2026-10-14 | [Klusters: crash on opening a session whose probe has no .probe file; leftover .pending copies](#2026-10-14--klusters-crash-on-opening-a-session-whose-probe-has-no-probe-file-leftover-pending-copies) |
| 2026-10-14 | [Klusters: watershed split on the noise cluster](#2026-10-14--klusters-watershed-split-on-the-noise-cluster) |
| 2026-10-14 | [Klusters: multi-lasso tool; zoom and pan in the t-SNE / oblique view](#2026-10-14--klusters-multi-lasso-tool-zoom-and-pan-in-the-t-sne--oblique-view) |
| 2026-10-12 | [Klusters: Shift+H density heatmap over the feature view](#2026-10-12--klusters-shifth-density-heatmap-over-the-feature-view) |
| 2026-10-12 | [NeuroScope: closing a session no longer deletes or overwrites parameter-file values](#2026-10-12--neuroscope-closing-a-session-no-longer-deletes-or-overwrites-parameter-file-values) |
| 2026-10-12 | [ndm_plx2dat: Plexon .plx / .pl2 → .dat + .evt with header metadata in the session YAML](#2026-10-12--ndm_plx2dat-plexon-plx--pl2--dat--evt-with-header-metadata-in-the-session-yaml) |
| 2026-10-12 | [Klusters: sync other feature spaces to the current spike times](#2026-10-12--klusters-sync-other-feature-spaces-to-the-current-spike-times) |
| 2026-10-11 | [Klusters: view one .clu through another method's feature space; spikes never change order](#2026-10-11--klusters-view-one-clu-through-another-methods-feature-space-spikes-never-change-order) |
| 2026-10-10 | [Input-binding overhaul completed across Klusters and NeuroScope; Qt 6.4–6.12 compatibility; documentation](#2026-10-10--input-binding-overhaul-completed-across-klusters-and-neuroscope-qt-64612-compatibility-documentation) |
| 2026-10-09 | [NeuroScope: two silently dead shortcuts fixed](#2026-10-09--neuroscope-two-silently-dead-shortcuts-fixed) |
| 2026-10-08 | [ndm_hipass: zero-phase Butterworth filter (new default)](#2026-10-08--ndm_hipass-zero-phase-butterworth-filter-new-default) |
| 2026-10-08 | [ndm_extractspikes filter: check the raw source window](#2026-10-08--ndm_extractspikes-filter-check-the-raw-source-window) |
| 2026-10-08 | [ndm_pca: a `_D` run adds the lag space to existing features](#2026-10-08--ndm_pca-a-_d-run-adds-the-lag-space-to-existing-features) |
| 2026-10-08 | [ndm_extractspikes: post-extraction flat-run / duplicate filter](#2026-10-08--ndm_extractspikes-post-extraction-flat-run--duplicate-filter) |
| 2026-10-08 | [ndm_detectspikes (stderiv): flat-run rejection and duplicate removal](#2026-10-08--ndm_detectspikes-stderiv-flat-run-rejection-and-duplicate-removal) |
| 2026-10-08 | [custody: `.spk`/`.res` of a `_D` feature-space token resolve to its waveforms](#2026-10-08--custody-spkres-of-a-_d-feature-space-token-resolve-to-its-waveforms) |
| 2026-10-08 | [ndm_pca: lagged feature space (`_D<lag><dims>`)](#2026-10-08--ndm_pca-lagged-feature-space-_dlagdims) |
| 2026-10-08 | [stderiv detection: threshold noise base, re-centring, hybrid arm](#2026-10-08--stderiv-detection-threshold-noise-base-re-centring-hybrid-arm) |
| 2026-10-08 | [Klusters: rebindable keyboard + mouse, and the Overview interaction redesign](#2026-10-08--klusters-rebindable-keyboard--mouse-and-the-overview-interaction-redesign) |
| 2026-10-06 | [Klusters: paint-structure refactor](#2026-10-06--klusters-paint-structure-refactor) |
| 2026-10-05 | [Klusters: template curation in the feature space](#2026-10-05--klusters-template-curation-in-the-feature-space) |
| 2026-06-08 | [Klusters high-cluster-count rendering perf, PCA-Center Align All profiling + setup hoist, cross-cluster GPU-batch design](#2026-06-08--klusters-high-cluster-count-rendering-perf-pca-center-align-all-profiling--setup-hoist-cross-cluster-gpu-batch-design) |
| 2026-05-16 | [Cluster-mean alignment stack + .res/.spk/.fet consistency](#2026-05-16--cluster-mean-alignment-stack--resspkfet-consistency) |
| 2026-04-27 | [ndm_start root + `.ndm.<n>.pipeline` files + empty-graph default](#2026-04-27--ndm_start-root--ndmnpipeline-files--empty-graph-default) |
| 2026-04-27 | [ndm_start as the graph root (replaced)](#2026-04-27--ndm_start-as-the-graph-root-replaced) |
| 2026-04-26 | [graphical Pipeline tab in ndmanager (superseded)](#2026-04-26--graphical-pipeline-tab-in-ndmanager-superseded) |
| 2026-04-26 | [graphical Pipeline tab in ndmanager (original)](#2026-04-26--graphical-pipeline-tab-in-ndmanager-original) |
| 2026-04-25 | [per-probe empirical KlustaKwik priors](#2026-04-25--per-probe-empirical-klustakwik-priors) |
| 2026-04-22 | [process_shadowcluster inflation guard + dedicated KK tier for sub-clustering](#2026-04-22--process_shadowcluster-inflation-guard--dedicated-kk-tier-for-sub-clustering) |
| 2026-04-22 | [process_reextractspikes_stderiv writes transformed .spkD (correctness)](#2026-04-22--process_reextractspikes_stderiv-writes-transformed-spkd-correctness) |
| 2026-04-22 | [fetD completion, klusters focus fixes, Pipeline C nudge correctness](#2026-04-22--fetd-completion-klusters-focus-fixes-pipeline-c-nudge-correctness) |
| earlier | [KlustaKwik](#earlier-changes--klustakwik) |
| earlier | [ndmanager-plugins](#earlier-changes--ndmanager-plugins) |
| earlier | [jg05-20120316.yaml (sample session)](#earlier-changes--jg05-20120316yaml-sample-session) |

---

## 2026-11-27 — Klusters: the core cut and the PCA refine on HIP and SYCL

Both had a CUDA kernel only. Each now has HIP and SYCL kernels, and a test compares whichever backend runs with the CPU.

<details>
<summary>Details</summary>

- **Before,** the core cut's ordering loop (`corecut_cuda.cu`) and the realignment's PCA refine
  (`pca_refine_cuda.cu`) had a CUDA kernel only; HIP and SYCL builds ran the CPU loops.
- **Now** each has a HIP and a SYCL kernel beside it (`corecut_hip.hip`, `corecut_sycl.cpp`,
  `pca_refine_hip.hip`, `pca_refine_sycl.cpp`), dispatched CUDA → HIP → SYCL. The HIP files are
  the CUDA kernels line for line (the PCA refine checks AMD's 64 KB of LDS instead of NVIDIA's
  opt-in). The SYCL core cut keeps the double running products and needs a GPU with fp64 —
  without it (the Arc iGPU of Meteor Lake) the CPU loop runs. The SYCL PCA refine is float;
  it sums each candidate's energy in a work-group tree instead of a shared atomic add, so a
  float near-tie between two shifts may go the other way.
- **`klusters_test_gpu_parity`** runs both on the backend the dispatcher picks and compares
  them with the CPU: the core cut with `corecut::detail::orderCpu` (the same order up to a
  near-tie, steps within 1e-4), the PCA refine with a direct evaluation of the kernel's sum
  (raw and stderiv). Without a GPU it says so and passes.
- **Checked:** the HIP kernels compile for gfx1030/gfx1100/gfx90a/gfx908 with Ubuntu's ROCm 5.7
  and link into Klusters; the CPU build is unchanged. **Not checked:** any of the new kernels
  running — there is no GPU here; the SYCL files are not compiled (no oneAPI compiler here).
  Build with oneAPI and run the parity test on the Arc laptop, and on an AMD card if there is one.

</details>

---

## 2026-11-27 — Build: the HIP backends build

With ROCm installed the configure failed, and the HIP code of KiloKlustaKwik had never compiled. HIP is now one CMake language for the whole build.

<details>
<summary>Details</summary>

- **Before,** a build with ROCm installed did not configure: the HIP object libraries set
  `CXX_COMPILER` as a target property, which CMake does not have, so their kernel files went to
  the C++ compiler; KiloKlustaKwik's `.hip` file had no language in its directory ("can not
  determine linker language"); Klusters linked `hip::device`, which adds `-x hip` to every C++
  source. Behind that, `KK_hip.cpp` called `hipLogf` (no such function) and `KK_hip.h` had a
  member line outside its struct: the HIP code of KiloKlustaKwik had not been compiled.
- **Now** the top-level configure enables CMake's HIP language once, before the packages
  (`ns_gpu_enable_hip` in `cmake/GpuBackends.cmake`). Kernel files are `LANGUAGE HIP`, compiled
  by ROCm's clang for `CMAKE_HIP_ARCHITECTURES` (one list for every package, default
  `gfx1030;gfx1100;gfx90a;gfx908`; `KK_EXP_HIP_ARCHS` and `EXTRACTEMG_HIP_ARCHS` are gone); the
  C++ compiler compiles and links the rest with `hip::host`. No relocatable device code.
  `process_extractemg` builds its HIP path only when rocBLAS is found.
- **Checked** with Ubuntu 24.04's ROCm 5.7 packages (`-DROCM_PATH=/usr
  -DCMAKE_HIP_COMPILER=/usr/lib/llvm-17/bin/clang++`): KiloKlustaKwik and Klusters compile for
  the four targets and link against `libamdhip64`; both start without a GPU. The CPU build is
  unchanged. **Not checked:** a run on an AMD GPU; `process_extractemg` with rocBLAS (not
  installed here).

</details>

---

## 2026-11-27 — Klusters: a collision is marked only when it is there in every waveform space

A partner fitted in the shown space and absent from another is no spike. Mark Collisions now confirms in every space, and marks made before can be cleaned by the same rule.

<details>
<summary>Details</summary>

- **What was wrong.** A collision was confirmed in the shown waveform space alone. On
  jg05-20120312 group 8, of 24,679 marked rows 20,465 had their partner fitted with `stderiv_C5`
  shown and about 24,600 with `standard` shown: which rows were corrected, and which residual
  spikes existed, depended on the view.
- **What the difference is.** The stderiv transform is linear, so a partner's spike found in
  the standard `.spk` is in the stderiv `.spk` at the same lag and amplitude. Measured on that
  session (4,998 marked rows, the waveform files of 19 Nov): 76.3 % accepted in both spaces,
  19.0 % in `standard` only, 0.3 % in `stderiv_C5` only. For the rows accepted in both, the
  standard fit applied in stderiv removes 13.2 % of the residual energy there (the stderiv fit:
  15.6 %); for the standard-only rows it removes nothing (median −1.4 %, 2.8 % of them reach
  5 %). What the standard fit explained in those rows is no spike. They name mostly a few small
  clusters (72 % of the marks naming cluster 14, 57 % of 59, 43 % of 8, 41 % of 9) — the ones
  whose residuals did not lie with them in feature space.
- **Mark Collisions** now fits each confirmed hit again in every other `.spk` of the group's
  rows and keeps it only when it is accepted in all of them (the fit and bounds the corrected
  rows are made with). The dialog says how many were left out, for which partners.
  `CollisionScanOptions::everySpace` (on).
- **Clusters → Remove Collision Marks Not in Every Space…** (`action.removeUnconfirmedCollisionMarks`)
  does the same for the marks a selection already holds: fitted in every space, the shown one
  included; the rows not accepted in all are offered for removal, counted by space and by
  partner. Written at Save.
- `KlustersDoc::confirmCollisionsInSpaces`, `unconfirmedCollisionRows`
  (`klustersdoc_collisionspaces.cpp`).
- **Checked** in `test_collision_spaces.py` with a second waveform space: identical records —
  the same hits, none left out (the control); the partner missing from the second space in 20
  planted rows — the scan leaves out exactly those 20, the cleanup finds exactly those 20, all
  counted against the second space, every other mark stands, the `.eap` is written; one space —
  nothing is called unconfirmed. **Not checked:** the action on the real session (the numbers
  above come from the same fit computed outside Klusters on older waveform files); a stderiv
  space in the self-test (its second space is another copy of standard-domain records).

</details>

---

## 2026-11-27 — Klusters: Add Residual Spikes says whether the spikes look like their partner

Each residual spike is scored against every cluster in feature space; the dialog shows the share that fits the partner named and lists the partners most of whose spikes fit another cluster.

<details>
<summary>Details</summary>

- **Why.** On jg05-20120312 group 8 the saved marks named some small clusters more often than
  those clusters have spikes, and of the residuals named for clusters 9, 8 and 59 only 5 %, 1 %
  and 3 % lay with that cluster in feature space (51 % over all partners; 1 % for a random
  cluster named). Adding such spikes as "the partner's" would mislead.
- **Now** each spike found is scored, in the session's own feature space, against every cluster
  with 50 or more clean spikes: a Gaussian per cluster with independent columns (squared
  distance over the cluster's variance plus the log variance). Per partner the dialog shows the
  share of its spikes that the partner fits best; a partner with 20 or more spikes and fewer
  than one in five fitting it best is listed as doubtful, with the cluster most of them fit.
- Nothing is left out because of it: the spikes become new clusters as before.
  `ResidualSpikes::matchOf`, `doubtful`; per spike `looksLike`, `partnerRank`.
- **Checked** in `test_residual_spikes.py`: 39 of 39 planted spikes fit the partner best and it
  is not doubtful; with the partner's own spikes moved elsewhere in feature space 0 of 39 fit
  it, it is listed as doubtful and the other cluster is named. **Not checked:** the dialog
  text itself; the in-app score on the real session (the numbers above are from the same
  score computed outside Klusters on the saved `.fetr`).

</details>

---

## 2026-11-27 — Klusters: a .spkr for every .spk method at Save

The `.spkr` of a method that was not shown stayed as an earlier Save left it, beside a `.fetr` of another set of rows. Every method's is now written together.

<details>
<summary>Details</summary>

- **Found on jg05-20120312 group 8:** `.spkr.standard` held 24,607 records from a Save made with
  the standard space shown, while `.fetr.standard`, written later with `stderiv_C5` shown, held
  the 20,463 of that Save. Only the shown method's `.spkr` was rewritten, so the two files of one
  space no longer described the same rows.
- **Now** a Save writes the `.spkr` of every `.spk` method the recording can be cut in: the
  shown space's rows, offsets, classes, kinds and twins, each method's own windows (cut again
  with the same neighbours and displacement — the records the `.fetr` of that method's spaces
  are projected from). A `.spkr` of the stage in a method no record can be made in (no
  recording, a super-sampled method) is removed; another stage's or group's is left.
- The set of rows is the shown space's. Which partners are fitted depends on the waveforms the
  fit is made in (here 20,465 of 24,679 marks in `stderiv_C5`, about 24,600 in `standard`); the
  README says so.
- **Checked** in `test_collision_residuals.py`: a stale `.spkr` of the stage is removed at Save,
  another stage's and another group's are not. **Not checked:** the `.spkr` of a second method
  itself — the self-test session has one `.spk` method; the re-cut it uses ran on the real
  session for the `.fetr` but its values were not verified.

</details>

---

## 2026-11-27 — Klusters: Add Residual Spikes no longer crashes on a stderiv session

The action ended in a segmentation fault when the shown waveforms were stderiv with a session pattern: a cut specification was copied and its copy pointed into the original.

<details>
<summary>Details</summary>

- **The crash.** Clusters → Add Residual Spikes as Clusters… ended in a segmentation fault on a
  session whose shown waveforms are stderiv with a session pattern (`stderiv_C5`): right after
  the residuals were brought up to date, before anything was written.
- **Why.** The specification a window is cut with (`SpikeCutSpec`) holds a `SpikeWindowSpec`
  that points into the specification's own vectors (the pattern's reference sets, its
  expressions). The residual code filled one per `.spk` method and then *copied* it into a
  table: the copy's pointers still named the original, which was gone. A `standard` `.spk` has
  no such vectors, so the self-tests — and the re-cut in `standard` at Save — ran.
- **Now** the specifications are filled where they stay (held by pointer), and `SpikeCutSpec`
  can no longer be copied: code that tries does not compile. No other place copied one.
- **Checked:** the residual self-tests pass as before. **Not checked:** the action on a stderiv
  session with a pattern — the self-test session cannot build one, and I have no recording of
  the session it crashed on. The guard is the compile-time one.
- Nothing had been written when it crashed: the open stage's files are as they were.

</details>

---

## 2026-11-27 — Klusters: the residual spikes' features, saved as .fetr

Save writes, beside the `.spkr`, a `.fetr` for every feature space: the residual records projected as the space's `.fet` rows are.

<details>
<summary>Details</summary>

- **At Save,** beside the `.spkr`, Klusters writes `<base>.fetr.<method>.<group>[.<stage>]` for
  every feature space of the group that has a basis: the `.spkr`'s rows, each record through
  that space's `.pca`, the columns the basis does not give from the row's own `.fet` row, the
  residual spike's time (row time + offset) last.
- **A space over another `.spk` method** gets the record cut again from the recording in that
  method — same neighbours, same displacement. Without a recording only the spaces over the
  shown waveforms get a `.fetr`.
- **No `.fetr`** for a space with no basis, a super-sampled one, or one whose basis does not fit;
  a file left there from before is removed. With no residual left the files are written empty,
  like the `.spkr`.
- A `.fetr` is not rewritten while the records it was written for are unchanged. Sort Spikes
  by Time removes the group's `.fetr` files with its `.spkr`.
- `KlustersDoc::writeFetrFiles`, `fetrPathFor`. Nothing reads the files yet.
- **Checked** in `test_collision_residuals.py` with three spaces: a `.fetr` for the two with a
  basis and none for the third; rows are the `.spkr`'s; values equal the projection of the
  `.spkr` records through each basis; times; the carried column; 39 of 39 planted residuals lie
  nearer the partner's cluster centre than the row's own; empty after the marks are removed;
  written without a recording. `test_time_sort_collision_files.py`: removed by the sort.
  **Not checked:** a space over a second `.spk` method (the recut path), a lag or stderiv
  space, the cost at Save on a real session.

</details>

---

## 2026-11-27 — Klusters: Add Residual Spikes grows every feature space

The new stage holds the `.fet` of every feature space, not only the `.clu`'s own, and the other spaces of a stage with its own rows read the stage's files.

<details>
<summary>Details</summary>

- **Before,** the `_rs` stage held the `.fet` of the `.clu`'s own space only; every other
  space kept the old rows and did not fit the stage.
- **Now** each new row gets a feature row in every usable space: through the space's own basis
  (`.pca.<method>`) from the record of the space's waveform method, with the columns the basis
  does not give copied from the source row; in a space with no basis, the source row's features
  with the spike's own time. Each space's `.fet` is written under the stage's tag.
- **In a stage with rows of its own** the feature-space list, a switch and a sync now use the
  stage's own `.fet` and `.spk` of the other spaces too (`KlustersDoc::ownOfStage`); before,
  only the `.clu`'s own space did.
- A `.spk` method the recording cannot be cut in (super-sampled) no longer refuses the whole
  action: it is left out with the spaces over it, and named in the status line.
- **Checked** in `test_residual_spikes.py` with three spaces (the session's, a second
  projection with a column of its own, a detached table): rows of each as described, old rows
  unchanged, all three usable in the new stage on the stage's files. **Not checked:** a lag
  (`_D`) space, a stderiv space, a sync or a realign inside an `_rs` stage.

</details>

---

## 2026-11-27 — Formats: .fetr, the residual spikes in a feature space

The companion of the `.spkr`, as the `.fet` is of the `.spk`: each residual record's features, one file per feature space.

<details>
<summary>Details</summary>

- **What it is.** The `.spkr` holds the partner's spike of a collision row as a waveform
  record. The `.fetr` holds that record in a feature space: one file per space,
  `<base>.fetr.<method>.<group>[.<stage>]`, a row laid out like a row of the space's `.fet`
  (the same columns, the time last — the residual spike's time).
- **Layout.** 32-byte header (`FTR\x01`; nRows, nFeatures, nSpikes, group, flags), the index
  of `.fet` rows (int64, strictly ascending — the `.spkr`'s rows), then nRows × nFeatures
  int64. Offset, class, kind and twin of a row stay in the `.spkr`.
- `neurofileio::readFetr` / `writeFetr` / `fetrFind`, written whole or not at all like the
  `.spkr`; `formats/fetr.md`. Nothing writes the file yet.
- **Checked** in `core_test_neurofileio_robust`: round trip, file size, the refusals (table
  length, index order, row beyond the `.fet`, a single column, a header naming more rows than
  the file holds), an empty file, and that it is not read as a `.spkr` or `.spkc`.

</details>

---

## 2026-11-27 — Klusters: Add Residual Spikes as Clusters

The partners' spikes that collision rows hold and no row of their own — the second spike of a collision, lost to the detector's dead time — are aligned at their peak and added as rows of new clusters, one per partner, in a new stage.

<details>
<summary>Details</summary>

- **Why.** A collision row holds its own unit's spike and a partner's. The detector does not fire
  twice within its dead time, so the partner's is nearly always a row of no cluster — on session
  jg05-20120312 group 8, about 99 % of cluster 78's residuals. The residual display showed those
  spikes; nothing made rows of them.
- **Clusters → Add Residual Spikes as Clusters…** takes the *recovered* residuals of the selected
  clusters (children, in the child view), aligns each at its own peak — the extremum, within 6
  samples, on the channel and with the sign of the partner template's peak — cuts it from the
  recording at that time, less the source row's own unit, and adds it as a row.
- **New clusters, one per partner named**, not the partners' clusters: the name comes from a
  fit, and the new cluster is to be looked at beside the partner before the two are merged. In
  a hierarchical session each new cluster is one child, its self child.
- **A new stage, `<stage>_rs`**, with its own `.res`, `.clu` (`.clc`, `.clp`), every `.spk`
  method, the shown `.fet` and the `.eap`; it is opened, the open stage is not changed. Old
  rows keep order and content; a new row sits beside its source row on the side its time says.
  Features: the record projected on the shown space's `.pca`. A new row's `.eap` row names no
  class.
- **Not added:** a residual that is a row already (*detected*, *repeated*), one that after the
  alignment lies within 6 samples of any row or of another new spike of the same partner, one
  whose class has no cluster. The confirmation counts each.
- `KlustersDoc::alignedResidualSpikes` (the spikes), `KlustersApp::writeResidualStage` (the
  files); the residual code's window cutting and neighbour choice are shared with the display
  (`cutWindows`, `targetLessMedian`, `residualNeighbours`). A record made from the `.spk` is
  now made again once a recording is there.
- **Checked** by `test_residual_spikes.py` on planted spikes: one new row per marked planted
  spike, within 1 sample of where it was planted; its record is the planted spike (median
  error 11 % of its energy); features equal the projection; spikes that are rows already are
  not added; old rows byte for byte; the stage opens and, asked again, adds nothing; two
  partners give two clusters; the child layer is written. **Not checked:** a real recording
  (the test session's `.fil` is synthetic), stderiv methods, more than one `.spk` method,
  the dialog itself.

</details>

---

## 2026-11-27 — Klusters: a stage that holds its own rows opens on them

A stage that adds rows — a decollide's — writes its own `.spk`, `.fet` and `.res`; opening its `.clu` read the shared ones and was refused. It now opens on its own.

<details>
<summary>Details</summary>

- **The fault.** Klusters resolved the `.spk`, `.fet` and `.res` of an opened `.clu` by base,
  method and group, never by stage. A stage that adds rows writes its own under its tag
  (decollide: `<stage>_dc`), so its `.clu` was read against the shared `.spk` and refused:
  "it holds N entries but the spike file implies M". The stage a decollide had just written
  did not open.
- **Now** a tagged `.clu` (or `.clc`) opens on `<shared name>.<stage>` for each of the three
  when that file exists, and on the shared file otherwise — the case of every stage that only
  relabels.
- **Checked** by `test_stage_own_rows.py`: a stage that is a `.clu` alone opens on the shared
  files (the control), a stage with ten rows more opens with all of them at the times of its
  own `.res`, and the untagged session beside both is unaffected. Not checked: a decollide run
  end to end.
- `formats/naming.md` states the rule.

</details>

---

## 2026-11-26 — Decollide does not grow a spike that is a row already

A decollided stage held the partner's spike twice when the partner had also been detected on its own. Such collisions are now left as they are.

<details>
<summary>Details</summary>

The lasso decollide (`D`) and the batch apply replace a collision row by one row per constituent
in a new stage. When the partner had been detected on its own, a few samples from where the fit
puts it, the grown stage held that spike twice.

- `decollide::dropKnownConstituents(res, decomps, tolerance)` (core, header-only) leaves out a
  decomposition one of whose constituents lies within the tolerance of a row that stays, or of a
  constituent of the same unit of an earlier kept decomposition. Dropping one puts its row back
  among those that stay, so it repeats until nothing changes: two rows that are one collision
  seen from both sides, both selected, are both left.
- `KlustersApp::decollideCommit` applies it with 0.2 ms (the tolerance of Remove Duplicate Spikes)
  after the reserved-cluster filter, and the status line counts the collisions left.
- The grow engine itself (`planGrow`, `applyMethod`) is unchanged, so its parity with fiber-kit's
  twin stands. fiber-kit's own decollide does not have this filter.

`core_test_decollide`: a decomposition whose partner is a row 2 samples from the fitted place is
dropped, one 9 samples away is kept; a constituent near its own row is not a duplicate of it; a
collision selected from both sides is left whole (as given, the plan held two rows more than
there are spikes); order and out-of-range entries are preserved; tolerance 0.

Not tested end to end: there is no self-test of the decollide commit, and none was added.

</details>

---

## 2026-11-26 — Klusters: Remove Duplicate Spikes

Rows of a cluster closer in time than a neuron fires twice are one spike held more than once. A new action finds them and moves all but one of each to the artefact cluster.

<details>
<summary>Details</summary>

**Clusters → Remove Duplicate Spikes…** (`action.removeDuplicateSpikes`, no key), on the selected
clusters or, in the child view, the selected children.

- **Scan** (`KlustersDoc::scanDuplicateSpikes`, `document/duplicatespikes.h`): rows of one
  cluster within 0.2 ms of the first of their run are one spike held more than once. Of each run
  the row most like the cluster's template around it is kept (mean of up to 300 rows nearest in
  time, the runs' own left out), the lowest row on a tie.
- **Apply**: after a confirmation the rows too many go to the artefact cluster through
  `deleteArtifact` — an ordinary edit, undoable, in either scope.
- **Counted, not touched**: rows that near in time in two different clusters, by pair of clusters.

Measured.

- `test_duplicate_spikes` (synthetic, rows added to the session): 20 of 20 added duplicates found
  (12 at the very same time, 8 cut 3 samples late), none of the 4 cut 9 samples late, none in the
  cluster without duplicates, 12 with a tolerance of 0; the 24 near pairs across the two clusters
  are counted and left; after apply and Save exactly the 20 rows are in cluster 0.
  `klusters_test_duplicatespikes` covers the runs.
- sirotaA-jg-000005-20120312 group 8, every cluster of the `fc_unmix_t2_h` curation, on a session
  assembled here from an older `.fet` (its times predate the realigns): 18 spikes held twice, 10
  at the very same time, 14 of them in cluster 78; 85 near pairs across clusters, 22 of them
  between clusters 20 and 78. Counted on the curation's own `.res` instead: 38 pairs within 6
  samples in one cluster, 37 at the very same time, 28 in cluster 78. The scan takes under a second.

Not tested: apply on the real group; the child-scope apply.

</details>

---

## 2026-11-26 — Klusters: the residual of a collision row, shown and saved as .spkr

A waveform-view toggle draws each collision row as its partner's spike: re-cut from the recording around the partner, the row's own unit (the median of its neighbours in time and waveform) subtracted. Each says whether the spike is a row already.

<details>
<summary>Details</summary>

**Waveforms → Residual of Collision Rows** (`waveforms.residual`, a toggle, no key): every row
with a fitted partner is drawn as the partner's spike; every other row as it is.

- **The record** (`KlustersDoc::rebuildResiduals`, `klustersdoc_residuals.cpp`): the window the
  recording holds centred on the partner (row time + fitted lag), cut in the `.spk`'s own domain
  (`spikeCutSpecFor`, `extractSpikeWindows`), less the median of the row's neighbours' windows cut
  at the same displacement. Neighbours: of the 400 clean spikes of the row's cluster nearest in
  time, the 50 nearest in waveform to the row's corrected record. Not detrended.
- **Duplicates** (`residualtwins.h`): each record is *recovered*, *detected* (a row lies within 6
  samples of the residual's time; the partner's own by preference) or *repeated* (another
  residual of the same partner within 6 samples; the lowest row keeps it).
- **Shown** through `Data::setResidualRecords`, which only the waveform tables read
  (`WaveformData::read`): templates, features and tools never see a residual.
- **Kept** per row with what it was made for (time, cluster, partner, fitted lag): only changed
  rows are made again. While the display is on they follow every rebuild of the corrected rows.
- **Saved** as `<base>.spkr.<method>.<group>[.<stage>]` and read back at open. Sort Spikes by Time
  removes the group's `.spkr` (rows renumbered); the next Save rebuilds it.
- Without a recording the record is made from the `.spk` (the row's own window, offset 0).

Measured.

- `test_collision_residuals` (synthetic, `.fil` present): against the spike that was added to the
  recording, the residual's error is 3.9 % of that spike's energy (median of 39 rows, worst 5 %),
  the same for lags that put part of the partner outside the row's window. Edge level of the
  records 4.8 against 9.3 for the same rows' `.spk` records: no trend. The 18 rows whose partner
  spike is a row of its own are all *detected* with the right twin; with both clusters marked 15
  of 18 pairs name each other. The table the view draws holds the residual for marked rows and
  the `.spk` record for the others; Save, reopen (57 kept, 0 made again), unmark and the
  no-recording path are covered. `klusters_test_residualtwins` covers the verdicts, *repeated*
  included.
- sirotaA-jg-000005-20120312 group 8, cluster 78, 6,911 marked rows, **from the `.spk`** (no `.fil`
  in this copy): 3.3 s; edge level 68 against 70 for the `.spk` records. Only 68 of the 6,911
  residual spikes (1 %) have another row within 6 samples, 21 of them in the partner named: the
  partners' spikes in these collisions are, almost all, rows of no cluster.

Not tested: the cut from a real `.fil` (timing, and the stderiv domain); *repeated* end to end
(the synthetic session has no spike in two rows' windows).

</details>

---

## 2026-11-26 — Formats: .spkr, the residual spike of a collision row

A new sparse file beside the .spkc: where the .spkc holds a collision row less its partner, the .spkr holds the partner's spike, re-cut from the recording around the partner with the own unit subtracted.

<details>
<summary>Details</summary>

`neurofileio::SpkrFile`, `readSpkr`, `writeSpkr`, `spkrFind`; `formats/spkr.md`.

The `.spkc` holds a collision row with the partner taken out (the own spike). The `.spkr` holds the
partner's spike: a window re-cut from the recording centred on the partner, less the own unit's
waveform as it lies there. The layout is the `.spkc`'s with its own magic (`SPR\x01`) and two
tables of one int32 a row: the offset from the row's `.res` time to the record's centre, and the
`.eap` class it is centred on. Written through the temporary-file-and-rename path.

**Duplicates.** The partner's spike may be a row already, so each record carries a kind and a
twin row: *recovered* (no row holds the spike), *detected* (the partner's unit has it as its own
row) or *repeated* (an earlier record of the file is the same spike). A tool that turns residual
spikes into rows takes the recovered ones only.

`core_test_neurofileio_robust`: round trip; a `.spkc` is not read as a `.spkr` nor the reverse;
tables of the wrong length, an unsorted index, a row beyond the `.spk`, a twin that does not fit
its kind, an unknown kind and a header naming more rows than the file holds are refused.

Nothing writes the file yet: the next patch has Klusters build and show the records. fiber-kit
does not know the format; no golden file is shared for it.

</details>

---

## 2026-11-26 — Klusters: Mark Collisions shows what chance would give for each partner

Beside each partner's count the dialog shows the rows two independent units would share. On cluster 78, a quarter of the marks name partners at more than three times chance.

<details>
<summary>Details</summary>

The scan reports, per partner tried, the number of rows two independently firing units would
share: for each row searched, the partner's rate around it (its own spikes within the presence
window, the window cut at the ends of the session) times the duration of the lags searched.
`CollisionScan::expectedOf`; the dialog shows it in brackets beside each partner's count.

Measured.

- `test_mark_collisions`: 8.9 rows for two units of 400 spikes in 18 s, which is 400 x 22.2 /s x
  20 lags at 20 kHz. (The generator puts both units on one grid of 40 samples, which doubles the
  coincidences: 17 and 19 are found.)
- sirotaA-jg-000005-20120312 group 8, cluster 78, 6,911 marks: cluster 64 is named 3,613 times
  against 2,517 by chance (1.4x); 100, 40, 126 and 125 are at 0.7-1.1x. Clusters 68 (305 against
  30), 102 (229 against 8), 14, 95, 103, 86, 30 are at 8-30x. Partners named at least 20 times and
  more than three times chance account for 1,877 of the marks (27 %).

What it does not say: units that fire together (in the same network events) share more rows than
independent ones, so a ratio of 2 or 3 is not evidence of a misfit; 10 to 30 is hard to get that
way. No ground truth on the real group.

</details>

---

## 2026-11-26 — Klusters: collision rows as a child, and marks taken back

Two companions of Mark Collisions: the rows that name a partner become a child of their cluster to be looked at, and Remove Collision Marks takes partners out again.

<details>
<summary>Details</summary>

For the last look at marked collision rows.

- **Clusters → Collision Rows to a Child** (`action.collisionRowsToChild`): the rows of the
  selected clusters or children that name a partner (two classes or more in the membership,
  whoever wrote them) become one new child per parent. No row leaves its cluster; rows that are a
  child of their own already are left. Needs a child layer. Uses the builder the other child
  splits use, so it is undone like them.
- **Clusters → Remove Collision Marks…** (`action.removeCollisionMarks`): those rows keep the
  class that is their own cluster's (`decollide_fit::splitRow`, as the corrected rows read it)
  and no other. Held in memory and written at Save like a mark (`PendingCell` -128); it removes
  saved marks and a sorter's as well as this session's.
- `KlustersDoc::collisionRowsOf`, `removeCollisionMarks`, `hasPendingCollisionMarks` (Save now
  writes when only removals are pending, with the preference off too).

`test_collision_curation`: 57 marked rows of cluster 2, from both of its children, become one
child of 57 rows with cluster 2 still at 400 spikes; a second run makes nothing; removing the
marks of that child (child scope) leaves no row corrected and every row served raw; saved marks
are removed in a later session; removing cluster 3's marks leaves cluster 2's.

Not tested on the real group.

</details>

---

## 2026-11-26 — Klusters: Mark Collisions tries a cluster only where it is firing

A cluster is tried as a partner only where it has spikes of its own within five minutes of the row, and a cluster's templates are cut by time as well as by rows.

<details>
<summary>Details</summary>

The scan took each cluster's template at the row's place in the session, but tried every cluster
everywhere: one with no spike of its own around the row's time was fitted with its nearest
template, from another part of the session.

- **Presence.** A cluster is tried as a partner of a row only where it has spikes of its own
  within 5 minutes of the row (`CollisionScanOptions::presenceSeconds`, 300; 0 = anywhere).
- **Templates cut by time too.** A stretch was about 3000 rows, so a cluster under 6000 rows had
  one template for the whole session. A stretch is now also at most ten minutes of the cluster's
  life (`stretchSeconds`, 600), never fewer than 100 rows, at most 64 per cluster
  (`collisionscan::stretchCount`).
- `scanCollisions` takes a `CollisionScanOptions`; the scan reports the row-cluster pairs not
  tried and the number of templates per cluster.

Measured.

- `test_mark_collisions`: with cluster 3's spikes of the first 7 s moved to the noise and a 2 s
  window, 0 of the 11 planted rows before 4.5 s are called and 22 of 22 after 8 s; with the window
  off, 11 of 11 early ones are called (from a template of another time). With 4 s per stretch
  cluster 2 gets 4 templates (1 by rows) and the planted rows are still found.
- sirotaA-jg-000005-20120312 group 8, cluster 78: of the 6,741 rows marked before, 864 (13 %)
  named a partner with no spike of its own within 5 minutes (clusters 68, 23, 106, 99, 31, 95
  most). Now 6,911 rows are marked, 3,613 with cluster 64 (3,300 before); 6,459 rows are in both
  sets, 4,838 with the same partner. 4.5 million row-cluster pairs are not tried, and the scan
  takes 73 s on two cores (110 s). Clusters with one template: 15 of 96 (the median has 10).
- Side effect: with more templates per cluster, more pairs of clusters reach the look-alike
  cosine at some time. For 78 the clusters not tried went from 13 to 16 (20, 23 and 92 added);
  23 had been named 403 times.

Not tested: other window lengths on the real group; whether the 452 rows marked only now are
collisions (no ground truth).

</details>

---

## 2026-11-26 — Formats: the .eap and .tcl pages say what the two implementations do

tcl.md described a lenient reader the code no longer has; eap.md listed one of fiber-kit's writers. Both corrected, with the limits the contract check found.

<details>
<summary>Details</summary>

- `tcl.md` described a lenient reader (rows with too few fields skipped, an unknown status read as
  `free`). `readTcl` has failed the whole file on either since the registry was indexed by column;
  the page says so, and says that fiber-kit's reader is the lenient one.
- `eap.md` named `fiber-decollide` as fiber-kit's only writer and said every access goes through
  `neurofileio`. It lists the writers and readers of both sides, says the two implementations are
  held together by the shared golden files, and says which tools read the base table when the
  stage has none.
- New in `eap.md`: one `.res` per group against one `.eap` per stage (a realign saved from one
  stage leaves the other stages' offsets behind), and what a class is across stages (a column is
  found by `provenance_clu` alone, so it is one unit within a stage's table and not across them).

No code. From `claude/eap-contract-fk-ns3-2026-11-26.md`, items 3, 5, 6, 7 and 10.

</details>

---

## 2026-11-26 — Klusters: the .tcl Mark Collisions writes is as wide as the .eap

With no .tcl beside a wide .eap, Save wrote a 128-slot registry. It now has the table's width.

<details>
<summary>Details</summary>

When Mark Collisions made a class and the session had no `.tcl`, Save wrote a registry of 128
slots (or up to the new class), whatever the width of the `.eap`. `formats/tcl.md`: a writer that
finds no `.tcl` next to an `.eap` starts from all-free slots of the `.eap`'s width. Every reader
reconciles the two widths, so nothing misread the files; the registry was simply narrower than
the table it describes.

`writePendingClasses` takes the `.eap`'s width and writes at least that many slots.
`test_mark_collisions`: a 256-column `.eap` with no `.tcl` -> the `.tcl` written has 256 slots
(128 before).

The contract note (`claude/eap-contract-fk-ns3-2026-11-26.md`, item 9) described this as the two
growing by different steps. That was wrong: the table saved grows only to the columns in use, and
so did the registry. The case that differed is the one above.

</details>

---

## 2026-11-26 — Klusters: Mark Collisions gives a moved row its own cluster's class

A row that still names the class of the cluster it was moved out of is no longer skipped: marking it takes that class out and names the row's own cluster's.

<details>
<summary>Details</summary>

A sorter that writes an `.eap` names every spike's class (fiber-unmix `--eap all`: every kept
spike, its own class at offset 0). Nothing renames that class when the spike is moved to another
cluster in Klusters, so after curation a cluster holds rows naming other clusters' classes. Mark
Collisions left such rows unmarked.

- A marked row that names one class of another cluster has that class taken out and gets its own
  cluster's at offset 0; when the class it named is the partner's, the cell becomes the partner's
  offset. The `.clu` is the authority on whose spike a row is.
- Rows that are not marked keep what they name. The corrected-row code already copes with them
  (a class's cluster is read off the membership vote).
- `PendingCell` value -128 takes a class out of a row; `CollisionMarks::strayRows` now counts the
  rows re-owned, and the status line says so.

`test_mark_collisions`: on a session whose `.eap` gives every row a class, 19 of 20 planted rows
that named cluster 3's class (ten its first, ten its second) are marked, name cluster 2's class
and cluster 3's and no other in the saved `.eap`; unmarked rows are saved as they were.

Not measured on a real fiber-kit `.eap`: how many rows of a curated stage this applies to.
(`claude/eap-contract-fk-ns3-2026-11-26.md`, item 4.)

</details>

---

## 2026-11-26 — The .eap, .tcl and .spkc are written whole or not at all

The three collision-layer files are written to a temporary file and renamed over the target, so a failed Save leaves the old table and a hard-linked twin is no longer rewritten with it.

<details>
<summary>Details</summary>

`neurofileio::writeEap`, `writeTcl` and `writeSpkc` opened the target with `std::ofstream`, which
truncates it first. Two consequences, both met by a fiber-kit session in Klusters:

- a crash or a full disk during a Save left half a table;
- a second name of the same file was rewritten with it. `fiber-unmix-children` put the stage's
  `.eap` and `.spkc` under `<tag>_h` as hard links (until fiber-kit 0.71.21), so saving the
  hierarchical stage from Klusters rewrote the flat stage's offsets and corrected rows.

They now write `<target>.tmp<pid>` and rename it over the target (`ReplacingFile` in
`neurofileio.cpp`, on `fileops.hpp`'s `replaceFile`). A symbolic link is followed: the file it names
is replaced and the link stays, so a sub-session that links to the session's `.eap` still writes
the session's. Every caller goes through these three functions (`initEap`, the plugins, Klusters'
Save, the template-class store, Mark Collisions).

`core_test_neurofileio_robust`: for each of the three files, a second hard link of the old file
keeps the old contents after a rewrite; a write through a symbolic link replaces its target and
leaves the link; a refused write leaves the table; no temporary file is left.

Found while checking the `.eap` contract with fiber-kit
(`claude/eap-contract-fk-ns3-2026-11-26.md`, item 2).

</details>

---

## 2026-11-26 — Klusters: Mark Collisions

Clusters → Mark Collisions finds the rows of the selected clusters that hold a second cluster's spike and names the partner in the `.eap`, so they are shown with it subtracted. Nothing changes on disk before Save.

<details>
<summary>Details</summary>

**Clusters → Mark Collisions…** (`action.markCollisions`, no key) works on the selected clusters,
or in the child view on the children selected in the child palette.

- **Scan** (`KlustersDoc::scanCollisions`, `document/collisionscan.h`): the row's own cluster's
  template is taken out; every other cluster with at least 200 rows is tried in the residual at
  every lag that keeps its peak 6 samples inside the window, at 0.5–1.6 of its template. The same
  search with the templates negated is the control, and the threshold is what the control reaches
  in 1 % of the rows. A candidate is confirmed by `decollide_fit::fitPartner` (partner at 0.5–1.6,
  removing at least a fifth of what the own template leaves). Templates are per stretch of about
  3000 rows of a cluster. Clusters too like the row's own (cosine ≥ 0.85 between any two of their
  templates) are not tried.
- **Mark** (`KlustersDoc::markCollisions`): the row's `.eap` row gets its own cluster's class and
  the partner's at the fitted offset; a cluster without a class is given one. The marks are held
  in memory (`PendingMarks`) and applied to every read of the `.eap` (`currentEap`), so the rows
  are corrected at once by the existing collision-row code; Save writes the `.eap` and the `.tcl`
  (`writeCollisionOffsets`, now also with the preference off, marks only). Closing without saving
  drops them. Not on the undo stack.
- `rebuildCollisionRows` reads the table through `currentEap` and takes the class homes from
  `collisionClassHomes` (the block it had inline, plus the classes not yet saved).

Measured.

- `test_mark_collisions` (synthetic, 2 units of 400 rows): 39 of 40 planted rows found in cluster
  2 and 30 of 30 in cluster 3, the partner within a sample of where it was planted; the 17–19 rows
  per cluster where the generator itself put both units at one time are found as well; no other
  row is called, in the planted session or in the one with nothing planted. Marked rows are served
  with a median error of 2 % of the raw row's against the record before the partner was added.
  Marks survive a nudge before Save and are written for the saved times; marks made after a nudge
  on a session that has an `.eap` are stored where found.
- sirotaA-jg-000005-20120312 group 8 (815,728 spikes, 139 clusters, no `.eap`), cluster 78 (193,558 rows): 110 s
  on two cores; 6,741 rows marked (3.5 %), 3,300 of them with cluster 64; the control is over the
  threshold in 1,935 rows. 59 classes made; the rebuild corrected all 6,741 rows in 0.5 s. An
  independent numpy search of the same cluster (one template per cluster, no joint fit) called
  6,316 rows; 5,273 are in both.

Not tested: a session whose `.eap` was written by fiber-kit; Save on the real group; more than two
cores.

</details>

---

## 2026-11-26 — Docs workflow: the deploy is a job of its own

The Pages deploy could not find the artifact the same job had just uploaded.

<details>
<summary>Details</summary>

- The docs workflow built the site, uploaded it and deployed it in one job. `deploy-pages` looks
  the artifact up by name; asked a fraction of a second after the upload finalized, the listing was
  empty: "No artifacts named github-pages were found for this workflow run"
  (actions/deploy-pages issue 451). A re-run uploads a second artifact and fails differently.
- Two jobs now: `build` (convert, check, build, upload) and `deploy` (`needs: build`).

</details>

---

## 2026-11-26 — A recluster leaves no KlustaKwik model file

Klusters turns the model file off for a KlustaKwik recluster and removes one left under the temporary name.

<details>
<summary>Details</summary>

- KlustaKwik writes `<base>.model.<group>` (the fitted Gaussians, as text) with `-fSaveModel 1`;
  the classic KlustaKwik does so by default. A recluster runs under a temporary base name, so the
  file was left in the session folder under a name nothing refers to.
- A KlustaKwik recluster now carries `-fSaveModel 0` unless the Arguments (Preferences >
  Reclustering) name the flag, and a model file under the temporary name is removed with the other
  temporary files, also when the Arguments asked for one.
- `KlustersApp::reclusterModelArguments`; `test_recluster_klustakwik.py` checks the flag and that no
  `.model.` file is left, by default and with `-fSaveModel 1` in the Arguments.
- Not changed: `ndm_klustakwik`, whose `fSaveModel` parameter still defaults to 1 for a pipeline run.

</details>

---

## 2026-11-26 — A local PCA of the selected clusters on Shift+Y

A feature space fitted on the selection alone, in place of the session's features until the key is pressed again.

<details>
<summary>Details</summary>

- **View > Local PCA of the Selection (Shift+Y).** Fits a per-channel PCA on the spikes of the
  selected clusters alone and projects every spike on it. The parents shown are the selection, or
  the children selected in the child view. The result takes the place of the features on screen:
  same dimension count, the shown space's window and components per channel, the time column kept.
- It is a borrowed space named *local PCA* in the Space box, held in memory; no file is written.
  Shift+Y again, or any other space, leaves it for the space it was entered from.
- Cutting, grouping and Recluster work in it. Realign and nudge leave it first and run where they
  would have. Save As is off, as in any borrowed space.
- The fit uses at most 20,000 of the selection's spikes, evenly thinned; the selection needs 20.
  The waveforms are the shown `.spk` as it is in memory.
- `KlustersDoc::showLocalPcaSpace`, `KlustersApp::slotToggleLocalPca`; the child layer loads the
  table in memory when the hierarchy is rebuilt while the space is shown.
- `test_local_pca_space.py`: the features equal a basis fitted in numpy on the selection's rows
  (parent, and one child in the child view), the key toggles back value for value, a realign
  leaves the space, nothing is written. `commands.golden.json`: one command more.

</details>

---

## 2026-11-26 — Docs: the in-app handbook is the generator's output again

The docs check failed on one re-wrapped paragraph of the NeuroScope handbook.

<details>
<summary>Details</summary>

- The docs workflow regenerates `src/neuroscope/doc/en/handbook.html` and fails when the result is
  not the committed file. The paragraph on Reset and Unbind had been edited in the HTML by hand and
  was wrapped differently from what pandoc writes.
- The file is `doc/site/gen.sh`'s output again; the text is unchanged.

</details>

---

## 2026-11-26 — Self-test: Quit's key is the platform's

The command table test compared the key of File > Quit, which differs with the Qt version.

<details>
<summary>Details</summary>

- `test_command_table` failed on the Ubuntu 26.04 runner: `file.quit` had no key in the snapshot
  and the Exit key there. The command takes `QKeySequence::Quit`, which is what the Qt version and
  the platform theme make of it.
- The test no longer compares Quit's key or its registry default. Every other key is compared as
  before.

</details>

---

## 2026-11-26 — Windows build: the realign refine search reduces on local counters

The MSVC build stopped in the PCA refine search of the realign; the counts are the same.

<details>
<summary>Details</summary>

- `klustersdoc_realign.cpp` reduced on `nRefined` and `nClamped`, which are reference parameters of
  `realignPcaRefineSearch`. A reduction variable may not have reference type: MSVC stops with C3030;
  GCC and Clang accept it.
- The parallel block reduces on two local counters and adds them to the parameters after it.

</details>

---

## 2026-11-25 — Super-sample mode asks before it builds the waveforms

With the mode on and the super-sampled .spk missing, Klusters asks; No turns the mode off and continues.

<details>
<summary>Details</summary>

Until now a super-sample mode (Preferences > Curation > Realignment > Super-sampling,
×2 or ×4) built the super-sampled space of every session that opened without one,
unasked: a `.spk` two or four times the size of the session's.

Now, when the mode is on and the session's `.spk.<token>_U<f>.N` is not there,
Klusters asks. The box names the file, the spike count, the size of the new `.spk`
and where the waveforms come from (re-cut from the recording, or interpolated from
the stored windows).

- **Yes** (the default) builds the `.spk`, its PCA basis and `.fet`, as before.
- **No** builds nothing, sets the mode to *off*, saves the preferences (so the next
  session does not ask again), and the session carries on at the recording's
  sampling. The status bar says where to turn it back on.

With the `.spk` there and only the `.fet` missing (waveforms built elsewhere,
`ndm_extractspikes --supersample`) nothing is asked: fitting the basis and the `.fet`
writes no large file.

The same question is asked when a super-sampled `.clu` is opened without its `.spk`
(the session is opened through its base clustering to rebuild it): the box says so,
and with No the session stays in the base clustering.

**Spikes > Super-sample ×2 / ×4…** is unchanged (it always asked).

Code: `buildAndShowSupersampledSpace` takes `bool* declined`; `applySupersampleMode`
turns the preference off on a No. Self-test harness: `"answerNo": [texts]` in the
spec answers No to the message boxes holding one of the texts (the default button is
taken otherwise, as before). New `test_supersample_mode_asks.py` (19 checks): No, Yes,
`.spk` present, and the super-sampled `.clu` without its `.spk`.

Not looked at on a screen: the box was exercised through the offscreen harness only.

</details>

---

## 2026-11-25 — Self-test: the command table does not depend on the Qt version

A tool tip Qt derives from the text differs between Qt 6.4 and newer; the dump no longer records it.

<details>
<summary>Details</summary>

`klusters_selftest_command_table` failed on Ubuntu 26.04 and 26.10 (Qt 6.10, 6.12)
and passed on 24.04 (Qt 6.4).

A `QAction` nobody gave a tool tip returns its text without the mnemonic. Qt 6.4
drops a trailing `...` from it; newer Qt also drops a trailing `…`. The dump writes
an empty tool tip when the tip is only the text, and decided that by comparing with
`actionid::plainText`, which drops both. So for the seven commands whose text ends
in `…` and which have no tip of their own (Open, Save, Save As, Purge Small Clusters,
Auto-Merge Similar Clusters, Strip Feature Outliers, Keyboard Shortcuts), Qt 6.4
reported `"Open…"` — which is what the snapshot stored — and newer Qt reported `""`.

The dump now compares both sides without a trailing ellipsis of either kind, so the
tool tip of those seven is empty under every Qt. `commands.golden.json` changes in
those 14 lines (seven commands, two states) and nowhere else.

Checked: with Qt 6.4 the test fails on exactly those 14 lines before the snapshot is
refreshed and passes after. With Qt 6.12 (PySide6, not the Klusters binary) the tool
tip Qt derives for each of the 272 command texts of the snapshot is recognised as
the text by the new rule. Not run: the Klusters self-test itself under Qt 6.10 or
6.12 — if it still fails there, the lines the test prints name what differs.

</details>

---

## 2026-11-25 — Windows build: pi from the standard library in supersample.hpp

`M_PI` is not C++ and MSVC does not declare it; the Lanczos kernel uses `std::numbers::pi`.

<details>
<summary>Details</summary>

The Windows build (MSVC, CI) stopped at `supersample.hpp(194): error C2065: 'M_PI':
undeclared identifier`, in every target that includes the header:
`libklustersshared`, `process_extractspikes`, `process_extractspikes_sdiff`,
`process_extractspikes_stderiv` (through `spike_extract.hpp`).

`M_PI` is a POSIX addition to `<cmath>`, not C++; MSVC declares it only under
`_USE_MATH_DEFINES`. The header now includes `<numbers>` and uses
`std::numbers::pi` (C++20, the tree's standard), as `process_extractemg.cpp`
already does. The value is the same double, so the Lanczos weights do not change:
`supersample_test` and the whole suite pass unchanged on Linux.

Not verified: no MSVC here. The log shows this one error; anything it was hiding
shows on the next run. KiloKlustaKwik still uses `M_PI` in four files; the Windows
workflow skips it (`NS_SKIP_KILOKLUSTAKWIK=ON`), so they are left as they are.

</details>

---

## 2026-11-24 — Klusters sources: eight subdirectories by layer

The source files of Klusters moved from one directory into subdirectories by layer. No change in behaviour.

<details>
<summary>Details</summary>

**What moved.** The 279 source files of `src/klusters/src/` — they were all in that one
directory — are in eight subdirectories by layer:

| Directory | Files | For |
|---|---|---|
| `app/` | 27 | the window, menus, command table, slots, radial menu, self-test harness |
| `document/` | 39 | `KlustersDoc`, undo, hierarchy, saving, feature spaces, realign, curation log |
| `data/` | 28 | `Data`, providers, `.spk` readers, template stores |
| `views/` | 53 | the views, the four matrices, the palettes |
| `jobs/` | 33 | job pool, queues, threads, the process runner |
| `compute/` | 29 | grouping assistant, PCA-refine and core-cut kernels, auto-merge |
| `dialogs/` | 6 | dialogs that are not preference pages |
| `prefs/` | 64 | configuration, preference table, the Preferences pages |

`autoMerge.*` and `clusterPalette.*`, the two camelCase names, are `automerge.*` and
`clusterpalette.*`.

**What did not change.** Includes are still by bare file name: every subdirectory is on the
include path of the application and of the tests, so no source line changed except the
includes of the two renamed headers (20 files). The resources, the build file and the icons
stay at the top of `src/klusters/src/`.

**For anyone with work in progress.** A patch against the old paths no longer applies as it
is; `git am -3` or a rebase follows the renames.

`src/klusters/docs/SOURCE_LAYOUT.md` is the reference: what goes where, the rules (bare
includes, unique lowercase file names, how to add a file), and a table of which directory
includes which, counted now. That table shows the layers are names, not yet a rule: the
document includes the application 11 times and the views 32 times.

**Checked.** A configure from nothing, a complete rebuild and the whole suite. Paths in the
documents and scripts are updated, except in the changelog, the history pages and the earlier
audit, which describe the tree as it was, and in comments of kiloklustakwik's sources.

The organization audit's last step is closed with this.

</details>

---

## 2026-11-24 — Klusters texts: artifact, not artefact

The texts shown to the curator and the manual use the spelling the code now uses.

<details>
<summary>Details</summary>

**What changes for the curator.** The texts of Klusters and its manual spell *artifact* one
way: the tool tips of the sort and renumber commands, the palette's label of cluster 0, a
handful of status-bar messages, two preference texts, and the manual's pages. They mixed
*artefact* and *artifact*.

Unchanged, because files and other programs hold them: the curation log's action names
`DELETE_ARTEFACT` and `DELETE_REGION_ARTEFACT`, the command id `tools.deleteArtefact`, the
image file names. Key bindings and settings are not affected.

`commands.golden.json` is updated; its diff is the 16 tool tips' spelling and nothing else.
The audit's step 7 is closed in `ORGANIZATION-AUDIT.md`.

</details>

---

## 2026-11-24 — Klusters code: parent and child, not fiber and atom

Identifiers and comments in the Klusters source use one pair of words for the two cluster layers. No change in behaviour; no text shown to the curator changed.

<details>
<summary>Details</summary>

In `src/klusters/` the identifiers that said *atom* or *fiber* say *child* or *parent*: 30
names in about 200 places (`atomIds` → `childIds`, `nextFreeAtom` → `nextFreeChild`,
`fiberOfSelfChild` → `parentOfSelfChild`, `atomsOf` → `childrenOfParent`, the fields `atom` and
`fiber` of the saved-hierarchy record → `child` and `parent`, ...). The comments follow: *atom*
and *fiber* as common words became *child* and *parent* (about 650 places).

Kept: everything inside a string — the menus, tool tips, messages and log lines still say
*fiber* and *atom*, the curator's words — and *fiber-kit*, the program's name, with the
identifiers named after it (`slotReclusterFiberKit`, `fiberKitReclusterCommand`). A
capitalised *Atom* or *Fiber* in a comment was left: there it names a menu entry or a palette.

**How it was done.** By a script, outside string literals; a check that no function held both
the old and the new name (one did, `upgradeChildIds`, and was renamed by hand); a comparison
of every string literal before and after (none changed). The build and the whole suite are the
check. The comments were rewritten word for word and read in a sample, not all: a sentence may
read awkwardly.

</details>

---

## 2026-11-24 — Klusters code: artifact, not artefact

Identifiers and comments in the Klusters source use one spelling. No change in behaviour.

<details>
<summary>Details</summary>

In `src/klusters/` every identifier and comment spelled *artefact* is now *artifact*: 171
places in 36 files, among them `moveClustersToArtifact`, `slotMoveClustersToArtifact`,
`slotDeleteArtifact`, `deleteArtifactCursor`, the view mode `DELETE_ARTIFACT` and
`DELETE_ARTIFACT_COLOR`. `deleteArtifact` was already spelled so.

Left as they are, because a stored file or another program holds the name: the curation
log's actions `DELETE_ARTEFACT` and `DELETE_REGION_ARTEFACT` with their enumerators, the
command id `tools.deleteArtefact`, the image files `delete_artefact*.png`. Texts shown to the
curator are not touched by this patch.

The rename was made outside string literals by a script; the build and the whole suite are
the check (the command and settings snapshots are unchanged).

</details>

---

## 2026-11-24 — Glossary: one word per thing in Klusters

The glossary records which of two words the Klusters code uses for the cluster layers and for the artifact bin.

<details>
<summary>Details</summary>

`doc/GLOSSARY.md` gains the two words the audit found used double in Klusters and what was
decided for each: the code says **parent** and **child** (the manual and the menus keep
*fiber* and *atom*, the curator's words), and **artifact**, not artefact. The table lists
where the other word stays because a stored name holds it. The entry for *cluster* is
corrected: id 0 is the artifact bin and id 1 the noise bin (it said 0 = noise, 1 = MUA).
Text only; the renames follow one word per patch.

</details>

---

## 2026-11-24 — Matrix views: one base for the grid, the pan and zoom, the follow

The error, template, residual and drift matrices share `MatrixViewBase` in place of four copies of their geometry and navigation. No change in behaviour.

<details>
<summary>Details</summary>

**What changed in the code.** The four curation matrices were four copies of the same grid.
`MatrixViewBase` (`matrixviewbase.h`) now holds what they share: the clusters, the pan and
zoom state, the cell under a point, the display position of a cluster, zooming about a point,
reset, a state taken from a linked view, the bring-back of a grid panned out of sight, the
follow of a cluster, the zoom floor that lets a large grid fit, and the id labels. It is not
a widget: each view derives from its widget class and from it, and supplies what differs
(where the grid sits, the display order, how to repaint, its signal) through small overrides.

Ten functions that existed in all four files, and the zoom floor that two of them had, exist once. The four views are 556 lines
shorter; the base is 248.

**Checked.**

- The four views sent the same 18 events (Ctrl+wheel, Ctrl+drag, click, shift-click, follow,
  restored state) leave the same zoom, pan and selection as before, and the same picture
  after every event (the view's pixels, hashed), built before and after.
- `klusters_test_matrixviewbase` (29 checks) on a view that is no widget: the cell under a
  point with pan and zoom; zooming about a point keeps the point's cell; the zoom limits and
  the lowered floor for 400 clusters; the display order; the follow (at once, deferred while
  a result is pending, lapsing when the cluster is absent); a state from another view is not
  announced; a lost grid comes back.
- The whole suite passes.

**Not checked.** Two views linked to each other on screen, a resize, a matrix of thousands of
clusters in the real widgets, the child scope's display order, and printing.

</details>

---

## 2026-11-24 — Self-test: the four matrix views under the mouse

A harness step drives a curation matrix with wheel, drag and click events, and a snapshot test pins what the four views do with them. No change to the application's behaviour.

<details>
<summary>Details</summary>

**Why.** The four curation matrices (error, template, residual, drift) were copied from one
another and the audit's next step gives them a shared base. Nothing tested what they do under
the mouse, so nothing would have shown a slip in that work.

**What is new.** A step of the self-test harness, `{"matrixView": {...}}`, opens one of the
four views at a fixed size, computes it, and sends it the events a curator sends: Ctrl+wheel,
Ctrl+left drag, click, shift-click, a follow of a cluster, a restored view state. After each it
reports the zoom and pan the view announced, the clusters the display shows, and a hash of the
view's picture.

`test_matrix_views.py` sends the same 18 events to each view and compares zoom, pan and shown
clusters with `matrix_views.golden.json` (`--update` rewrites it). The picture's hash is not in
the snapshot: fonts differ between machines.

**Seen while writing it, not changed.** The views do not agree on the wheel: the error and
template matrices zoom one step per wheel event whatever its size; the residual and drift
matrices zoom in proportion to it (a fast flick of three notches is one step in the first two
and three in the others).

</details>

---

## 2026-11-24 — Realign: realignSpikes reads as its stages

The last self-contained stages of `KlustersDoc::realignSpikes` are functions; the function is 777 lines, from 2,085. No change in behaviour.

<details>
<summary>Details</summary>

**What moved.** The remaining stages of `KlustersDoc::realignSpikes` that do not need the
document, each to a function above it, their text unchanged: the stderiv transform of the
space (`realignStderivSpec`), the centroid de-jitter (`realignCentroidShifts`), the feature
row (`realignFeatureRow`), the two mean waveforms, the details for the curation log
(`realignRecordDetails`) and the opt-in timing (`RealignTiming`, `realignLogTiming`). The
comment at the head of the function, which called it "one large function by design", is now
the list of its stages and where each lives.

`realignSpikes` is 777 lines; it was 2,085 at the start of the series. The audit's step 5 is
closed in `ORGANIZATION-AUDIT.md`, with what the function still holds and what was not done
(a cancel before the application; `nudgeClusterTimestamps`).

**Checked.** The same whole realign log and the same bytes in every per-spike file, before
and after, for the 27 realigns of the previous entry; the timing line under
`NS3_REALIGN_TIMING=1` has the same form; the whole suite passes (it compares the curation
log's realign entry).

**Not run.** A stderiv space, so `realignStderivSpec` beyond its default branch.

</details>

---

## 2026-11-24 — Realign: the new times, the verification and the row plan are functions

Three more stages of `KlustersDoc::realignSpikes` are functions of their own. No change in behaviour.

<details>
<summary>Details</summary>

**What moved.** Three stages of `KlustersDoc::realignSpikes`, each to a function of its own in
`klustersdoc_realign.cpp`, with their text unchanged:

- `realignNewTimes` — each spike's new time (and phase, in a super-sampled space), and the
  warning when two spikes of the cluster change order;
- `realignVerifyStoredRows` — the check that the stored `.spk` rows are the recording's windows
  at their `.res`, with its diagnosis (rolled, stale, all-pairs, unexplained);
- `realignPlanRows` — the new row of every spike: the window re-cut at the new time, the time,
  the feature row.

They share `RealignCut`, which says how a window of the shown space is cut from the recording,
and `realignCutStored`, the cut itself. `realignSpikes` is 957 lines (2,085 before the series).

**Checked.** The application before and after writes the same whole realign log and the same
bytes in every `.res`, `.spk`, `.fet`, `.clu` and `.clc` for 27 realigns: the modes and windows
at the recording's rate, a super-sampled space with both interpolators (the re-cut through the
interpolating kernel), a child realigned from the child view, a parent with children, and
Align All. The whole suite passes; it includes a realign the verification refuses
(`test_supersample_interp_detect.py`).

**Not run.** A stderiv space, and a moved spike that cannot be re-cut.

</details>

---

## 2026-11-24 — Realign undo: from any feature space

Undo and Redo of a realign that ran in another feature space show that space for the step and come back.

<details>
<summary>Details</summary>

**What changes for the curator.** `Ctrl+Z` on a realign that ran in another feature space no
longer stops with a message. Klusters shows that space, undoes the realign there, and comes
back to the space that was on screen, which follows the moved spikes; `Ctrl+Y` the same. If
the realign's space cannot be shown, nothing is undone and the status bar says so.

**In the code.** `KlustersApp::runUndoOrRedo` asks the document which space the next step's
realign ran in (`nextStepRealignSpace`, over `UndoHistory::newestStep`) and uses the way a
realign started from another space returns (`realignReturnSpace`, `returnFromRealignSpace`).

**Checked.** `test_realign_undo.py` (38 checks): a realign in the super-sampled space, the
base space shown, Undo: the step is undone, the base space is shown again, and every space's
saved files are those of the session without the realign; Redo from there gives the realign's
files. `undohistory_test` covers `newestStep`.

**Not checked.** A step whose realigns ran in different spaces (the first one's space is
visited; the others are refused as before) and a space that cannot be shown.

</details>

---

## 2026-11-24 — Realign undo: the memory it may use is a preference

The cap on the rows kept for the undo of realigns is a preference on the Session page, 4 GB by default, in place of a constant of 1 GB.

<details>
<summary>Details</summary>

**What changes for the curator.** **Preferences → General → Session → Undo** has a second
setting, *Memory for the undo of realigns (MB)*: what the rows kept by undoable realigns may
take together. It was a fixed 1 GB; it is 4096 MB unless changed, from 0 to 262144.

- Beyond it the oldest realign steps are dropped (the edits of the same step stay undoable).
- A single realign or Align All larger than it cannot be undone; the status bar says so when
  it ends.
- With 0 no realign is kept, and the status bar says that instead.
- A lower value takes effect at the next realign.

**In the code.** One row of the preference table (`realignUndoMemoryMB`) and a spin box named
in it; the constant `KlustersDoc::realignUndoBudgetBytes` is gone.

**Checked.** `test_realign_undo.py`: with 0 MB a realign is no undo step, the status bar gives
the reason and Undo changes nothing; with 1 MB two small realigns are both kept and undone.
`test_settings_file.py` and its snapshot have the key in all seven cases (117 keys). The
whole suite passes. Not checked: the drop of the oldest step by a real overflow (the generated
clusters take well under 1 MB); that path has a unit test only.

</details>

---

## 2026-11-24 — Realign can be undone

A realign is a step of the undo history: Undo writes back the rows it rewrote, Redo writes them again. Checked on generated sessions only.

<details>
<summary>Details</summary>

**What changes for the curator.**

- `Ctrl+Z` after a realign undoes the realign: every row it rewrote goes back as it was — the
  spike time, the waveform, the features — and `Ctrl+Y` writes the realigned rows again. Until
  now `Ctrl+Z` stepped over a realign to the edit before it.
- **Align All** is one step. The automatic realign after a merge is part of the merge's step:
  undoing the merge puts the spikes back as well as the labels.
- It works across a Save (undo, then save again).
- A realign done in another feature space is undone in that space. With a different space
  shown, Undo stops at that step and the status bar says which space to show.
- A **nudge** is still not undoable, and it ends the undo of the realigns before it.
- The kept rows are in memory: the old and the new waveform of each spike of a realigned
  cluster. Realign steps count against the undo depth, and the oldest are dropped once they
  hold more than 1 GB together.

**How it works.** The realign's record of its rows (`KlustersDoc::PendingRealign`, the patch
before this one) becomes an entry of the undo history when the job is over
(`recordRealignUndo`). Undo and Redo hand it to `applyRealignBatch`, which writes the rows
through `realignApplyRows` — the function the realign writes with — to the working files, the
tables of both layers and the waveform overlay, records the moved rows so that the other
spaces follow, and refreshes what the end of a realign job refreshes. The curation log gets an
UNDO / REDO entry that points at the realign's.

**Checked** (`test_realign_undo.py`, 33 checks, each on the saved files byte for byte):
realign, undo, save leaves the untouched session; undo, redo leaves the realign's files; two
clusters and one cluster twice, newest first; across a save; the PCA, RMS and centroid modes;
with a child layer, from the parent view and from the child view; in a super-sampled space
shown over another `.clu` space, every space's files; the refusal with another space shown;
Align All as one step; a merge with its automatic realign as one step, labels and spike times
both back; a nudge ending the realign steps. `undohistory_test` covers the new bookkeeping.
The whole suite passes.

**Not checked.** No real session: `ROADMAP.md` said this had to be validated on one before it
went near `Ctrl+Z`, and it has not been. `check_realign_undo_on_session.py` (beside the
self-tests) makes the same byte comparison on a copy of a real group and writes nothing to
the session. Also not exercised: a stderiv space, the 1 GB cap end to end, the views after an
undo (the tables are written by the same code as the files; no test looks at the screen), and
an undo while the collision rows wait for a rebuild.

</details>

---

## 2026-11-24 — Realign: each rewritten row keeps what was there

The record of a realign now holds the old and the new waveform row beside the old and new time and features, and one function writes a row either way. No change in behaviour.

<details>
<summary>Details</summary>

**What changed in the code.** The record a realign keeps of each row it rewrote
(`KlustersDoc::PendingSpkRecord`) had the new time and feature row and the old time and
features. It now also has the two waveform rows: the one written, which is the record the
waveform overlay already shows (shared, not copied), and the one that was there. A row whose
waveform did not change shares one record for both. `realignApplyRows` writes from the records
alone, and takes a direction: forward writes each row as realigned, backward the same row as
it was, by the same code.

**Why.** It is what an undo of a realign needs, and nothing calls the backward direction yet.

**Cost.** Until the next save, a realign keeps one more copy of the waveforms of the spikes it
moved.

**Checked.** Forward is what a realign did before: the application before and after writes
the same whole realign log and the same bytes in every `.res`, `.spk`, `.fet` and `.clu` for 18
realigns of the synthetic session, and the realign self-tests of the child layer, the collision
rows, a shown space and a super-sampled space pass.

</details>

---

## 2026-11-24 — Realign: the rows are computed first, then written

The loop that computed and wrote each spike's new row is now a loop that computes all rows and a function that writes them. No change in behaviour.

<details>
<summary>Details</summary>

**What changed in the code.** The last loop of `KlustersDoc::realignSpikes` did two things per
spike, interleaved: it computed the spike's new row (the window re-cut from the recording at
the new time, the new time, the feature row) and it wrote that row to the `.spk`, `.res` and
`.fet` working files, to the in-memory tables of the parent and the child layer, and to the
waveform overlay. It is now two loops. The first computes every row and changes nothing. The
second is a function, `realignApplyRows`, and is the first thing a realign changes.

**Why.** That line is the boundary the audit asked for between the plan of a realign and its
application. Up to it a realign can be abandoned without a trace; what an undo has to restore
is exactly what the application wrote. Neither a cancel nor an undo is built on it yet.

**Checked.** A row never depends on another spike's row having been written (each spike reads
and writes only its own), so the order of work changes and the result does not: the
application before and after writes the same whole realign log and the same bytes in every
`.res`, `.spk`, `.fet` and `.clu` for 18 realigns of the synthetic session, and the realign
self-tests of the child layer, the collision rows, a shown space and a super-sampled space
pass, as does the whole suite.

**Not run.** A stderiv space; a recording too short to re-cut a moved spike.

</details>

---

## 2026-11-24 — Realign: the PCA refinement is two functions

The PCA refinement moved out of the body of `KlustersDoc::realignSpikes` into two functions of its own. No change in behaviour.

<details>
<summary>Details</summary>

**What moved.** The PCA refinement (realign mode "PCA refine", `--pca-refine`) was 480 lines in
the middle of `KlustersDoc::realignSpikes`. It is now two functions above it in
`klustersdoc_realign.cpp`:

- `realignRefineBasis` — the copy of the space's basis the candidates are scored on,
  restricted to the selected channels and weighted by the correlation window;
- `realignPcaRefineSearch` — the search itself (GPU when there is one and the cluster is large
  enough, else the CPU loop, parallel over spikes), which takes everything it reads in one
  `RealignRefineInput` and changes only the shifts and the waveform buffer.

The text of both is the old text; `realignSpikes` opens the recording, fills the input and
calls them. It is now 1,360 lines (2,085 before this series).

**Checked.** The application before and after writes the same whole realign log and the same
bytes in every `.res`, `.spk`, `.fet` and `.clu` for 18 realigns of the synthetic session, ten
of them with the refinement (alone, with threshold 0.95, and under the gauss, hann and
template windows; two clusters each). The whole suite passes.

**Not run.** The GPU path (no GPU on the build machine), the refinement in a stderiv space and
under a channel selection (the test sessions have neither). Those branches moved as text.

</details>

---

## 2026-11-24 — Audit: where the split of realignSpikes stands

The organization audit records the progress of its step 5.

<details>
<summary>Details</summary>

`doc/klusters/ORGANIZATION-AUDIT.md`, order of work, step 5: what of `realignSpikes` has moved
out (the passes, the final scores, the reads), what the function still holds, and where the
plan / application boundary will be. Text only.

</details>

---

## 2026-11-24 — Realign: a realign that cannot run says why

The reason a realign refused was written and then dropped before it reached the Realign panel. It is now sent on every return.

<details>
<summary>Details</summary>

**The fault.** `KlustersDoc::realignSpikes` writes its log into a buffer and sends it on at
chosen points. Most of its refusals wrote the reason and returned without sending it: a cluster
that is not there, a missing file, a row of the `.spk`, `.res` or `.fet` that cannot be read, a
failed correlation, pending files that cannot be opened. The Realign panel showed the lines up
to the last one sent and stopped. The closing line of a realign that worked
(`Done. N shifted. (pending save)`) was dropped the same way.

**The fix.** What is written and not yet sent goes out when the function returns, by whatever
way.

**What changes for the curator.** A realign that cannot run now says why. Every realign's log
ends with its `Done.` line, so Align All shows one more line per cluster.

**Checked.** `test_realign_failure_is_logged.py`: a cluster that does not exist and a `.res`
too short for the cluster's rows each leave their `ERROR:` line in the log and change no file;
both checks fail on the build before this change. For eighteen realigns of the synthetic
session the log differs from before by the `Done.` line only and the files are the same. The
other refusals were not provoked.

</details>

---

## 2026-11-24 — Realign: the final scores and their summary are library functions

The scoring that closes a realign's log moved out of `KlustersDoc::realignSpikes` into `realign_pass`, where it shares the mean template and the channel mask with the pass loop. No change in behaviour.

<details>
<summary>Details</summary>

**What moved.** When the passes are over, the realign scores every spike against the cluster's
mean and writes the minimum, maximum and mean score, the number under the threshold and the
range of the shifts to its log. That computation repeated the mean template and the
channel mask of the passes inside `KlustersDoc::realignSpikes`. It is now
`realign_pass::finalScores` and `realign_pass::summarize` beside the pass loop, which share
the mean and the mask with it; `realignSpikes` is 90 lines shorter.

**Checked.** `klustersshared_realign_pass_test` holds the old computation as well and requires
the same scores and the same summary, value for value, in its 1,080 cases; three deliberate
changes to the new functions fail it. The application before and after writes the same whole
realign log and the same files for the eighteen realigns of the previous entry.

</details>

---

## 2026-11-24 — Realign: reading a cluster's rows is three plain functions

The file reads at the start of a realign moved out of `KlustersDoc::realignSpikes` into `realignio.h` and have a unit test. No change in behaviour.

<details>
<summary>Details</summary>

**What moved.** Reading a cluster's rows for a realign is now three functions on plain paths
in `src/klusters/src/realignio.h`, with no document and no Qt: `readWaveforms` (the rows of the
`.spk`, turned channel-major), `readTimesAndExtraFeatures` (the times from the `.res` and the
feature columns that are not projections from the `.fet`) and `toChannelMajor`.
`KlustersDoc::realignSpikes` calls them and is 90 lines shorter. The lines they add to the
realign log are the ones the function wrote before.

**Checked.**

- `klusters_test_realignio` (19 checks) on files it writes: values land at
  [spike][channel][sample] in the order of the rows asked; times and extra columns; a `.fet`
  whose header disagrees with the session is read by its own width with a warning; a row past
  the end of the `.spk` or `.res` is an error naming the row; a feature past the end of the
  `.fet` is a warning and stays 0; a missing file.
- The application before and after writes the same whole realign log and the same bytes in
  every `.res`, `.spk`, `.fet` and `.clu` for realigns of two clusters of the synthetic session
  under nine settings (thresholds 0.10, 0.70, 0.95; the three windows; the three modes).

**Noticed, not changed.** A read that fails makes `realignSpikes` return without sending the
error line it has just written to the log: the curator sees the realign fail and not why. The
same holds for several other early returns of that function.

</details>

---

## 2026-11-24 — Realign: the pass loop is a library function

The part of the realign that computes the shifts of a cluster's spikes moved out of `KlustersDoc::realignSpikes` into `libklustersshared` as `realign_pass::clusterShifts`, with a test that requires the old loop's results. No change in behaviour.

<details>
<summary>Details</summary>

**What moved.** The loop inside `KlustersDoc::realignSpikes` that turns a cluster's waveforms
into one shift per spike is now `realign_pass::clusterShifts`
(`src/libklustersshared/src/xcorr/realign_pass.h`): the mean template, its roll onto the
alignment sample, the top-channel mask, the correlation window, the per-spike lag from the xcorr
kernel, the roll of the moved spikes, and the next pass. It takes a buffer and returns the
shifts, the scores and the number of spikes each pass moved; it reads no file and has no Qt.
`realign_pass::kernelWeights` (the correlation window) and `realign_pass::rollSpike` moved with
it. `realignSpikes` calls it and is about 130 lines shorter; everything around the passes is where it
was.

**Checked.**

- `klustersshared_realign_pass_test` holds a copy of the old loop as it stood and requires the
  same shifts, scores, rolled samples and pass counts from the new function, on ten generated
  clusters under 108 settings each (threshold 0, 0.10, 0.70; 1, 2, 4 passes; all, 2, 4
  channels; no window, gauss, hann, template). Four deliberate changes to the new function
  (rounding the mean, the template rolled the other way, the score summed, the early stop
  removed) each fail it.
- The application built before and after gives the same bytes in every `.res`, `.spk`, `.fet`
  and `.clu` file and the same pass lines in the log for realigns of two clusters of the
  synthetic session under seven settings: the whole window, the gauss, hann and template
  windows, and the PCA, RMS and centroid modes. The harness cannot set the pass count, the
  largest shift or the channel count, and the thresholds tried (0, 0.70) changed nothing on
  that session, so those are compared by the unit test only.
- The seven realign self-tests and the whole suite pass.

**Not done.** `realignSpikes` is still one function for the rest: reading the waveforms, the
refinement and centering passes, writing the rows, the child layer, the journal. The sorter's
own port (`kiloklustakwik/src/klusters_realign.cpp`) does not call the shared function; it
differs in two details and goes with the pruning of that program. Not run on a recording: the
comparison is on generated data.

</details>

---

## 2026-11-24 — Klusters developer docs: commands and preferences

A new developer reference describes the command table and the preference table; the other developer documents point to it. Documentation only.

<details>
<summary>Details</summary>

- New internal reference `src/klusters/docs/COMMANDS_AND_PREFERENCES.md`: the command
  table and the preference table — what a row holds and what is built from it, the menu
  layout, why ids and keys are permanent and how old ones still resolve, limits, changing a
  default, the page binder and its kinds of control, what is still by hand and why, the
  dialog's flow and keyboard levels, the tests, and the mistakes to avoid.
- The Developer Guide lists it in place of the unwritten "Preferences & persistence".
- `INPUT_SYSTEM.md`: a menu command is made rebindable by its row's category; a key
  command that edits refuses while a job runs; one id, one command.
- `STANDARDIZATION.md`: §6.9 (one thing written in several places) and §6.10 (a changed
  default without the stored values) among the mistakes to avoid; the snapshot tests in
  §7.2; the list of Klusters' references.
- `RECIPES.md`, `CLAUDE.md`, the library reference (`PageKeyNavigator`,
  `BindingRegistry::replacedIds`), `DOCUMENT_MODEL.md` §4.4 and the audit point to it.
- The self-test README lists the 17 tests added since 2026-11-23 and their harness steps.

</details>

---

## 2026-11-24 — Klusters: the rest of the Preferences pages are filled from the table

The binder handles numbers typed into fields, colours, radio buttons and lists of values; 74 settings now need only their control and one row. No behaviour changes.

<details>
<summary>Details</summary>

- The binder of the Preferences pages learns four more kinds of control: a number typed
  into a line edit (the t-SNE settings), a colour, radio buttons (the realign mode), and a
  combo box whose entries stand for values (super-sampling off / ×2 / ×4). 26 more settings
  are bound, the whole Cluster view page among them: **74 settings** now need a control and
  one row.
- `prefdialog.cpp` is 482 lines (771 before the two conversions); the Cluster view page
  loses 134 lines of accessors, in which each number field repeated its default as the
  value to use when the text could not be read.
- **No behaviour changes**: the settings snapshot is the same in its seven cases, and the
  test now also sends the last entry of a combo box of values, the last radio button, a
  colour and typed numbers through the dialog and back.
- **By hand, on purpose:** the recluster engine, its chunking and the fiber-kit command
  (their controls do more when set, or are built in code), and the three settings structs
  (the core cut, the hover spike, the quality dot), whose pages set them as a whole.

</details>

---

## 2026-11-24 — Klusters: Preferences pages are filled from the preference table

A setting on a Preferences page now needs its control and one row of the table: loading, applying, Defaults and enabling Apply are done from the row. 48 of 78 page settings are converted; the pages look the same.

<details>
<summary>Details</summary>

- **Before.** A setting shown on a Preferences page was written by hand in the page (a
  `set…` and a `get…`) and four times in the dialog: load, apply, Defaults, and the
  connection that enables Apply.
- **Now.** A row of the preference table names its control (`.on("undoSpinBox")`) and
  `PrefBinder` does all four for a check box, a spin box, a combo box or a line edit. 48 of
  the 78 settings the pages show are bound this way, on eight pages; `prefdialog.cpp` goes
  from 771 lines to 589 and the pages lose 96 accessors. The pages look as they did: their
  `.ui` files are untouched.
- **Adding a setting** to a page is now the control in the `.ui` and one row
  (RECIPES.md §2.8).
- **No behaviour changes**: `test_settings_file.py` — now seven cases, with "Preferences
  opened and OK pressed" — gives the same settings file in each, and fails if a row names a
  control no page has.
- **Still by hand (30 settings):** the Cluster view page (numbers typed into line edits),
  colours, the realign mode's radio buttons, combo boxes that hold named values (the
  recluster engine, the chunking), and two check boxes that also enable other controls.
  They need a few more kinds of binding.

</details>

---

## 2026-11-24 — Klusters: a settings file is read within each preference's limits

A value outside its range in the settings file (edited by hand, or damaged) is now brought into range when read, as it would be when set in Preferences. Values in range are read as before.

<details>
<summary>Details</summary>

- **Before.** For 34 preferences the settings file was read straight into the member, past
  the limits their setter has: a file edited by hand, or damaged, could give an undo depth
  of -7, a realign threshold of 100000, a KNN `k` of 0.
- **Now** every row of the preference table that has a setter with limits reads through
  it (35 rows; for 5 of them the limit was written twice, in the reader and in the setter,
  and twice differently for the super-sampling interpolation). The four that had no limit
  anywhere but their page's spin box take that range: the undo depth 0-30, the crash
  recovery interval, the waveform gain 200-6000, the time interval 60-6000.
- A value in range is read as before: the default file, the old-defaults file and the
  Defaults button give the same settings file. `test_settings_file.py`: 65 lines of the
  snapshot change, 63 in the two out-of-range cases and two values the "changed" case
  had pushed out of range.
- Still without an upper limit, because their setter has none: the realign iterations,
  kernel width and maximum shift, the t-SNE iterations, learning rate, exaggeration and
  spike cap, and a few sizes.

</details>

---

## 2026-11-24 — Klusters: the preferences are described in one table

Each preference of the settings file is now one row giving its key, default and limits; reading, writing and the defaults are built from the rows. No behaviour changes.

<details>
<summary>Details</summary>

- **Before.** A preference was written in `Configuration` in up to six places: the member,
  the setter, the getter, the default (a constant, or a number repeated in the reader and
  in the default getter), a line of `read()` and a line of `write()`. 116 keys; `read()`
  and `write()` were 370 lines of them.
- **Now.** `Configuration::prefTable()` (`configuration_prefs.cpp`, `prefspec.h`) has one
  row per preference: group, key, member, default, limit. `read()` and `write()` are a
  loop each, and 29 default getters that repeated a number look it up in the table. The
  one-time changes of old defaults are together in `applyOldDefaults()`.
  `configuration.cpp`: 557 lines to 285; the table with its helpers is 213, so the
  total is about the same — what changes is that a default and a limit exist once.
- **No behaviour changes**: `test_settings_file.py` gives the same settings file, key by
  key, in all six cases (defaults, changed values, both limits, old defaults, the Defaults
  button).
- **Not done (step two):** the Preferences pages. A preference still needs its widget, the
  page's get / set and four lines of the dialog; RECIPES.md §2.8 lists them.
- **Left as found:** a stored value outside a preference's limits is taken as it is for the
  preferences whose reader never applied the setter's limits (the undo depth, the realign
  threshold and others). With the table that is a small change per row.

</details>

---

## 2026-11-24 — Klusters: Defaults gave the wrong super-sampling interpolation

Preferences ▸ Defaults now sets the super-sampling interpolation to the smoothing spline, the value a new installation has; it set the plain spline.

<details>
<summary>Details</summary>

- Preferences ▸ Defaults set *Curation ▸ Realignment ▸ Super-sampling ▸ Interpolation* to
  the first entry (spline); a new installation starts with the third (smoothing spline). The
  default was written in two places with two values. Defaults now gives the smoothing spline.
- Found by `test_settings_file.py`; its snapshot changes by that one line.

</details>

---

## 2026-11-24 — Klusters: a snapshot test of the settings file

A test now pins what every preference becomes when the settings file is read and written: defaults, limits, the one-time changes of old defaults, and the Defaults button. No behaviour changes.

<details>
<summary>Details</summary>

- `test_settings_file.py` runs six cases through `Configuration::read()` and `write()` and
  compares the settings file that results, key by key, with `settings.golden.json`: no file
  (every default), every value changed, every number at -7 and at 100000 (the limits), the
  values of old defaults without the "migrated" flags, and the changed file after
  Preferences ▸ Defaults. 116 keys.
- It is the check for the preference table that follows: the table must give the same file
  in every case.
- Taking it showed, in the code as it is:
  - **Defaults sets the super-sampling interpolation to 0** (linear); a new installation has
    2 (smoothing spline). The default is written twice, with two values. Fixed next.
  - **Reading does not apply the limits the setters have** for some settings: a settings
    file can hold an undo depth of -7 or a realign threshold of 100000 and both are taken.
    Recorded in the snapshot, not changed.
  - Three settings are on no page and so are not reset by Defaults:
    `includeTimeInAutoSelect`, `matrixFollowSelection` (the View menu's toggle),
    `mergeRecommendMaxShift`.
- For the test, the Defaults button's work is `PrefDialog::restoreDefaults()` (the question
  stays in `slotDefault`), and the harness has the step `configWrite`.

</details>

---

## 2026-11-24 — Klusters: Preferences from the keyboard

The Preferences dialog has two keyboard levels: the page list, and the settings of a page. Enter goes in, Tab goes round the settings of the page, Space toggles, Esc goes back; Enter no longer closes the dialog.

<details>
<summary>Details</summary>

- **Before.** Tab went through the page list, every setting of the page and the dialog's
  buttons in one round, and Enter anywhere pressed OK and closed the dialog.
- **Now**, two levels (`PageKeyNavigator`, libklustersshared):
  - the dialog opens on the **page list**. Enter goes into the page shown (on a group: opens
    it on its first page). Tab goes round the list and the dialog's buttons. Esc closes.
  - in a **page**, Tab / Shift+Tab go round its settings and stay in the page, which scrolls
    to the setting. Space toggles a check box. Enter toggles a check box, presses a button,
    opens a list of choices or commits a number, and never presses OK. Esc goes back to the
    list. Ctrl+Enter is OK from anywhere, as it was.
  - a line under the pages names the keys.
- A key editor of the Input page keeps Enter and Esc (keys it can record); Tab leaves it.
- Klusters' Preferences only. NeuroScope's uses the same dialog class and could take it
  with one line; not done.
- Test: `test_pref_keyboard.py` (harness step `prefKeys`). Not tried by hand: the scripted
  keys reach the focused widget as real ones do, but the look of the focus mark and of the
  hint line were not seen.

</details>

---

## 2026-11-24 — Klusters: the menus are arranged by what a command does

The Actions menu is divided into Clusters, Split, Recluster and Spikes; Waveforms, Correlations and Traces are under View; Preferences is under Edit and the Settings menu is gone. Keys, key bindings and radial-menu layouts are unchanged.

<details>
<summary>Details</summary>

- **Before.** One **Actions** menu held 54 commands of every kind; **Waveforms**,
  **Correlations** and **Traces** were on the menu bar; a **Settings** menu held Preferences
  and the toolbar toggles.
- **Now**, by what a command does:
  - **Clusters** — group, delete, renumber, purge, auto-merge, templates, *Sort Clusters*;
  - **Split** — watershed, DipSplit, KNN, by time, burst followers, spurious spikes, the four
    strip commands, the oblique basis;
  - **Recluster** — the recluster commands and their *Engine*;
  - **Spikes** — realign, align all, sort by time, sync feature spaces, super-sample, export,
    drift;
  - **View** gains the time chunks, Next / Previous Feature Space, Update Display, Update
    Error Matrix, the submenus *Waveforms*, *Correlations*, *Traces* and *Toolbars and
    Panels*, the point size and the update mode;
  - **Edit** gains Undo / Redo Child-Layer Edit (from Hierarchy) and **Preferences**.
  The menu bar: File, Edit, View, Clusters, Split, Recluster, Spikes, Hierarchy, Tools,
  Displays, Plugins, Help. No command was added, removed or renamed, and no key changed.
- **Key bindings are unaffected**: they name a command by its id, and no id of a bindable
  command changed. In Preferences ▸ Input the commands are grouped by the new menus.
- **Radial-menu layouts are unaffected.** 14 commands had no id but their menu path
  (`menu:Settings/Preferences`), which the move changes. Every command of the table is now
  named by its row's id (`edit.preferences`, `tools.newCluster`) wherever it is; the old
  paths still resolve. `test_command_ids_stable.py` checks all 134 ids of before.
- **In the code** the commands of the six menus are created without a place and one block
  of lists at the end of `createMenus` lays the menus out.
- The manual, the workflow and realign pages, the status messages and the comments name
  the new paths; the manual has a table of the menus. The **Actions** toolbar keeps its name.
- Not tried by hand in the GUI; the snapshot of all commands (menu, order, text, keys) is the
  check.

</details>

---

## 2026-11-24 — Klusters: the editing keys refuse while a job runs

T, the tool keys, Enter on a lasso or a watershed preview and the child-view keys could edit during a recluster or a realign: they are not menu commands, so the edit lock did not hold them. They now refuse.

<details>
<summary>Details</summary>

- **Found** while checking the input customization against the edit lock of 2026-11-24. The
  lock disables the menu commands that edit, and with them their keys, rebound or not. A key
  the input resolver dispatches is not a menu command, and 17 of those edit: T (move the
  selection to the end), the five tool keys, Enter on a pending lasso, on the kept lassos and
  on a watershed preview, D (decollide), and the four child-view keys. They stayed live
  during a recluster, a realign, a save or a plugin run: T renumbered a cluster while a
  realign was writing its spikes.
- **Now** each of them refuses while a job runs and says so in the status bar.
- Test: `test_edit_lock.py` runs T during a realign (new harness step `keyCommand`); without
  the change the cluster ids change, with it they do not, and T works again afterwards.
  The other 16 keys take the same guard and were not each run.
- `CONCURRENCY.md` §7.4 lists them and the rule for a new key command.

</details>

---

## 2026-11-24 — Klusters: two commands shared the id hierarchy.dissolveParent

The Hierarchy menu's Dissolve Parent into Children was missing from Preferences ▸ Input: a key command of the same id replaced it. Each now has its own id.

<details>
<summary>Details</summary>

- **Found** while checking the input customization against the changes of 2026-11-24 (not
  caused by them; present at 943e7d9). The child-view key command "Dissolve parent into
  children" (Ctrl+Shift+Down) was registered under `hierarchy.dissolveParent`, the id of the
  Hierarchy menu's *Dissolve Parent into Children*. The registry keeps one command per id, so
  the key command replaced the menu command:
  - the menu command was not listed in Preferences ▸ Input and could not be given a key;
  - rebinding the key command also set that key as the menu action's shortcut, so one press
    reached the command by two paths.
- **Now** the key command is `hierarchy.dissolveParentKey`; the menu command keeps its id (the
  radial menu's layout refers to it) and is listed under Hierarchy.
- **Stored bindings.** A binding stored under the old id was made for the key command: once,
  it moves to the new id, in the bindings and in the saved keymap layouts
  (`dissolveParentKeySplit`). Test: `test_dissolve_key_binding_migration.py`.
- **Guard.** `BindingRegistry::replacedIds()` lists ids registered twice;
  `test_command_table.py` fails when it is not empty. No other id is.

</details>

---

## 2026-11-24 — ndmanager-plugins: ndm_alignspikes_pca removed

The pipeline step that realigned spikes within a cluster after sorting is removed; Klusters and fiber-kit do this. A session plan that still lists the step must drop it.

<details>
<summary>Details</summary>

- **Removed.** The pipeline step `ndm_alignspikes_pca` and its program
  `process_alignspikes_pca` (1,294 lines): the realignment of spikes within a cluster after
  sorting. Klusters (Realign Spikes, with its PCA refine and RMS recentre) and fiber-kit
  (`fiber-realign`; `fiber_pca.py` has the per-cluster PCA-energy shift) do the same, and the
  program carried a cross-correlation of its own beside the shared kernels.
- **Nothing called it**: no other script, no program, no test, not fiber-kit. Gone with it:
  its row in the `ndm` dispatcher and in the Pipeline designer, its block in
  `templates/template.yaml` (7 parameters), its section of the alignment page of the command
  reference, and the mentions in eleven other documents and comments.
- **A session YAML that still lists the step** will stop at it: `ndm` no longer knows the
  name. Remove the step from the plan.
- Tests: 157 pass; the ndmanager-plugins tests that keep scripts, template, designer and
  documents in step agree.

</details>

---

## 2026-11-24 — Klusters: the realign threshold defaults to 0.10

Realign Spikes now moves a spike when its best correlation with the cluster's template is at least 0.10 (was 0.70), so the low-scoring spikes of a cluster are no longer left behind. A saved 0.70 that was the old default is lowered once.

<details>
<summary>Details</summary>

- **Before.** A spike whose best correlation with its cluster's template was under 0.70 was
  left where it was. In a cluster of low amplitude that is most of the spikes, and in a
  cluster whose template is off its peak sample the cluster separates into the spikes that
  moved and the ones that did not (measured on g6 0316, 2026-10-24: up to 94 % of a unit
  left behind at 0.70, at most 2.4 % at 0.10; the same on synthetic clusters, 2026-11-24).
- **Now.** The default is 0.10: Preferences ▸ Curation ▸ Realignment ▸ Threshold, its
  *Defaults* button, and `realignSpikes` when no `--threshold` is given.
- **Saved settings.** Every save of the preferences stores the threshold, so settings
  written before hold a 0.70 nobody chose. Once, a stored 0.70 becomes 0.10; any other value
  is kept, and so is a 0.70 set afterwards (`realignThresholdLoweredFromOldDefault`).
  Test: `test_realign_threshold_migration.py`.
- **Not changed.** fiber-kit's `fiber-realign` still defaults to 0.70
  (`FK_REALIGN_MIN_SCORE`), and so does `ndm_alignspikes_pca`.

</details>

---

## 2026-11-24 — Klusters: the edit lock in the concurrency reference

The internal threading reference now describes the edit lock and Cancel, and what they do not cover. Documentation only.

<details>
<summary>Details</summary>

- `src/klusters/docs/CONCURRENCY.md` §7.4 (new): which jobs hold the lock and why they are
  not covered by the snapshot rule, the one predicate (`editingBlocked`), the one set of
  commands (the rows marked `.editing()`), how the lock is held over later enables, Cancel
  and what it can and cannot stop, the tests. Contributor rule 9: a job whose result is
  applied to the clustering adds its flag to `editingBlocked`.
- It also states what the lock does not cover: it is whole-document, and only five slots
  (Save, Save As, Undo / Redo, the child undo / redo) refuse when called directly; the
  others rely on their action being disabled, so an armed mouse tool, a context menu, the
  Recommended-merges panel and the template palette are not stopped.
- RECIPES.md §2.7 said every editing slot must begin with `refuseWhileBlocked()`. That is
  not what the code does; it now says when the guard is needed.

</details>

---

## 2026-11-24 — Klusters: the undo history is one object

The bookkeeping of Undo and Redo moved out of the document class into a small class of its own with a unit test. No behaviour changes.

<details>
<summary>Details</summary>

- **Before.** The history Undo and Redo walk was two lists and four counters among the
  ~390 members of `KlustersDoc`, and its code was in `klustersdoc_hierarchy.cpp`, where the
  child undo had started. Undoing an entry and redoing it were two functions that differed
  in four words.
- **Now.** `UndoHistory` (`undohistory.h`) owns the entries, the commands that group them
  and the depth rule; it holds no cluster data and can be tested without a session
  (`undohistory_test.cpp`, 27 checks: steps, nested commands, a joined entry, the depth,
  annotations, the entry cap, the forced child undo, a lowered depth). `KlustersDoc` keeps
  one `undoHistory`; pushing and applying entries moved to `klustersdoc_undo.cpp`, and
  `stepOneEntry(direction)` / `stepDispatch(direction)` replace the undo / redo pairs.
- **No behaviour changes.** The 156 tests pass, among them the scripted undo self-tests.
- **Reference.** `src/klusters/docs/DOCUMENT_MODEL.md` §4.4 and §5 said the depth trims
  three stacks in lockstep and that the child layer has its own undo stack; both were out
  of date since the single history. §5.3 now describes it.
- **Noticed, not changed.** Lowering the depth in Preferences keeps the newest N *entries*,
  while everywhere else the depth counts *steps*; a command of several entries can lose its
  oldest ones at that moment.

</details>

---

## 2026-11-24 — Klusters: the menu commands are described in one table

Each menu command of Klusters is now one row of a table that gives its id, text, key, slot and whether it edits; the menus, the Preferences list and the edit lock are built from it. No behaviour changes.

<details>
<summary>Details</summary>

- **Before.** A menu command was written in three places: built in `createMenus` (text, key,
  tips, connection — 1,368 lines), given its id and category in `registerInputBindings`
  (716 lines), and named again in the edit lock's list. Leaving a command out of the last one
  left it enabled during a recluster.
- **Now.** `KlustersApp::commandTable()` (new `klusters_commands.cpp`, with `commandspec.h`)
  has one row per command: id, category, text, key, tips, check state, the slot, and
  `.editing()`. `createMenus` places a command with `addCommand(menu, id)`;
  `registerInputBindings` registers every row that has a category in one loop;
  `collectEditLockActions` takes the rows marked `.editing()`. 132 commands; `createMenus` is
  688 lines and `registerInputBindings` 593. The total number of lines is about the same.
- **Not changed.** What the menus contain and in which order, every text, key and tip, every
  id and category, the set of 72 commands the edit lock holds: `test_command_table.py` shows
  no difference from the snapshot taken before. The connections cannot be seen by that test;
  they were compared textually, old `createMenus` against the table: the same 121
  action-to-slot pairs and the same 6 lambdas.
- **One difference.** The commands are registered in menu order, not in the order of the old
  list. Preferences ▸ Input sorts them by category and label, so nothing shown changes; the
  snapshot test now compares the registry by id.
- **Not done.** The per-state lists of `slotStateChanged`, the icon table and the `QAction`
  members are still separate. Adding a command: RECIPES.md §2.7.

</details>

---

## 2026-11-24 — Klusters: a snapshot test of every command, and 24 command names cut short

The key-binding names of 24 commands were truncated.

<details>
<summary>Details</summary>

- **A fault found while preparing the command table:** the name a command has in the key
  bindings (Preferences, the cheat sheet) is the menu text without its mnemonic and its
  ellipsis, and the ellipsis -- one character -- was cut off as three.  24 commands were named
  short: "Open…" was "Op", "Save…" "Sa", "Purge Small Clusters…" "Purge Small Cluste".
- **`test_command_table.py`** and `commands.golden.json`: every command of the menus in menu
  order (id, menu, text, keys, tips, checkable, checked, enabled, held by the edit lock), and
  the binding registry's commands in their order, in two states.  136 commands.  A change of a
  menu, a key or a tip is from now on a change of that file (`--update`), to be read and
  committed with it.  It is the check under the rewrite that follows.
- Self-test step `commandDump`.  Taken on Linux; elsewhere the keys are not compared.

</details>

---

## 2026-11-24 — Klusters: unused code removed

Three files and 28 functions that nothing reached.

<details>
<summary>Details</summary>

- Step 2 of the organization audit: 1,365 lines that nothing reaches.
- **Files:** `waveformcomparewidget` (in no build file), and two dialog classes that were built
  but never created, `RealignReviewDialog` and `SpikeRealignDialog` -- all three left from the
  realign review that was taken out of the application.
- **28 unused functions** of four lines or more, each checked to have no caller in the
  sources, the tests or the shared library: among them `KlustersDoc::rejectLastRealign`,
  `EventsProvider::loadData` / `renameEvent` / `initializeEmptyProvider`, `Data::saveClusters`,
  `Data::setUserClusterInformation`, the `maskClusters` / `unmaskClusters` pair,
  `KlustersDoc::updateAllViews` / `refreshAllViews`, `ProcessWidget::maybeScrollToBottom`.
  Five comments that pointed at them are corrected.
- **Left on purpose** (listed in `doc/klusters/ORGANIZATION-AUDIT.md` §7): the two QtXml files
  (`ARCHITECTURE.md` records a decision to leave them, `ROADMAP.md` says to delete one: the two
  disagree); `Data::clusterEnvelopeOverlap`, which six comments use to state a contract;
  `KlustersDoc::isChildScopeHidden`, the only reader of a filter that is still written;
  `Data::revertLastSpkOverlayBatch`, which an undo of a realign needs; the `BinFile` helpers;
  64 functions of one to three lines.
- No behaviour changes.  124 unit tests and 36 self-tests pass; the lint gate passes.
- Not checked: data members that only the removed functions used.

</details>

---

## 2026-11-24 — Audit tool: the unused-function count was three times too high

The organization audit of this day overstated the unused functions.

<details>
<summary>Details</summary>

- `scripts/audit/dead_and_dupes_textual.py` counted a function as unused when its name stood
  at most twice in the code: once for the definition, once for the declaration.  A function
  defined inside its class body has no separate declaration, so one that is called exactly
  once was counted too.
- The audit's figure of 388 unused functions (1,408 lines) is **107 (716 lines)**, and
  `AutoMerge::computeProposalsByErrorMatrix`, which it named, is in use.
- The script now tells the two kinds of definition apart, and prints the whole list.

</details>

---

## 2026-11-24 — Klusters: every self-test is a ctest test

Thirty of the 36 scripted self-tests were not part of the test suite.

<details>
<summary>Details</summary>

- Step 1 of the organization audit.  Six of the 36 scripted Klusters self-tests were listed by
  name in `src/klusters/test/CMakeLists.txt`; the other thirty ran only when someone ran them
  by hand.
- Every `src/klusters/test/selftest/test_*.py` is now a `ctest` test
  (`klusters_selftest_<name>`, label `selftest`), found by a glob: a new file registers itself
  at the next configure.  `ctest -L selftest`: 36 tests in about 3 min one at a time, under 2
  min with `-j2`.  The three that read the clock or catch a running job (`cancel_job`,
  `curation_log`, `edit_lock`) run alone.
- The four reclustering tests exit 77 without KiloKlustaKwik, so `ctest` reports them skipped;
  they printed SKIP and returned 0, which reads as passed.
- CI runs the suite on every push: it now runs these too.  Not tried on the CI machines.

</details>

---

## 2026-11-24 — Klusters: legibility and organization audit

An audit of the Klusters source at 114k lines; documentation and two measuring scripts, no code change.

<details>
<summary>Details</summary>

- `doc/klusters/ORGANIZATION-AUDIT.md`: measured on 114k lines, nothing changed.  Ten findings,
  each with its evidence and what a fix would be, and an order of work.
- The heaviest: five functions of 774 to 2,085 lines (`KlustersDoc::realignSpikes`, cyclomatic
  complexity 349); `KlustersApp` and `KlustersDoc` hold half the application with ~390 members
  each; an action is described in four places; the self-test harness is inside the application
  class and 30 of the 36 self-tests are not registered with `ctest`.
- Also: 27% comment lines of which 309 narrate history; parent / child in the code against
  fiber / atom in the manual; three source files outside the build and 388 candidate unused
  functions; the four matrix views as copies of one another.
- Found on the way: `src/klusters/docs/DOCUMENT_MODEL.md` §5.2 still describes two undo stacks.
  The undo and lock patches of this day changed the manual and not the internal references.
- Tools: `scripts/audit/function_metrics.py` (needs `lizard`) and
  `scripts/audit/dead_and_dupes_textual.py`.

</details>

---

## 2026-11-24 — Klusters: Purge Small Clusters in the child view

Run from the child view, the purge did nothing and crashed.

<details>
<summary>Details</summary>

- In the child view the purge collected the small children of **every** parent (the active
  layer's clusters) and handed their ids to the parent-layer delete, which knows none of them:
  "cluster 20001 is in the delete list but absent from clusterInfoMap", nothing purged, and a
  **crash** (reproduced on a scripted session).
- It now follows the view.  Parent view: the parents below the number, as before.  Child view:
  the children below it **of the parents being curated** (the ones the child palette lists),
  dropped to the noise bin by `dropChildrenToBin` -- one undo step.  Other parents' children
  are not touched; the confirmation says which it is.
- Self-test step `purge`; `test_purge_child_view.py` (6 checks).
- Not looked at: the other commands "over all clusters" in the child view (the outlier strips,
  Partition by Time).

</details>

---

## 2026-11-24 — Klusters: a click in a parent matrix leaves the child view

From the child view, a click in a matrix of parents could leave the child selected.

<details>
<summary>Details</summary>

- In the child view (a child selected in the child palette, the views on the child layer) with
  the matrices showing parents, a click on a cell selected the cell's parents in the parent
  palette but, whenever the cell held the curated parent, **left the child selected and the
  views on the child layer** -- showing the cell's parent ids as if they were children.  Only a
  cell without the curated parent left the child view.
- A click (or Ctrl-click) in a matrix of parents now drops the child selection, returns the
  views to the parents and shows the cell's parents (`KlustersDoc::parentViewRequestedFromMatrix`,
  emitted by `selectFromMatrix` / `addFromMatrix` when the matrices are not scoped).  A matrix
  scoped to the children (V) selects children, as before.
- Self-test steps `childPalette` (show it, click a child) and `matrixSelect` (a cell click);
  `test_child_view_matrix_click.py` (8 checks).  The scoped matrices were not re-tested (their
  branch returns before the new line).

</details>

---

## 2026-11-24 — Klusters: Cancel Running Job

A running job could only be stopped by closing the session.

<details>
<summary>Details</summary>

- There was no way to stop a job short of closing the session, and three messages pointed at an
  "Abort Realignment" command that did not exist.
- **Edit ▸ Cancel Running Job** (Ctrl+., command `edit.cancelJob`) and a **Cancel job** button
  in the status bar, shown only while there is something to cancel.  The edit lock leaves it on.
  - A recluster, drift or plugin program is stopped; its result is discarded, the clustering
    and the undo history are as they were, no failure box.
  - A realign of several clusters (after an edit, Align All) stops after the cluster in
    progress; what was realigned stays realigned.
  - One cluster's realign, a nudge and a save are not interrupted: the command says so.
- `RealignWorker::cancelRequested` is atomic (set on the GUI thread, read on the worker's).
- Self-test step `cancelJob` (`{"when": <job>}` waits for that job); `test_cancel_job.py`
  (9 checks: a recluster; the realign of the 7 clusters a recluster left stops after 1).
- Not tested: a plugin (none installed here), a drift job, Align All through its question box,
  the button on a screen.

</details>

---

## 2026-11-24 — Klusters: the cluster information dialog keeps its cluster

The dialog wrote through a palette item that may be gone when it closes.

<details>
<summary>Details</summary>

- The dialog (right click on a cluster) applied its result, and updated the text, through the
  palette item it was opened on.  A job ending while it is open rebuilds the palette: the item
  is then gone, or stands for another cluster.
- It keeps the cluster id it was opened on, and the palette re-reads what it shows.
- Read in the code, not reproduced.

</details>

---

## 2026-11-24 — Klusters: nothing edits the clustering while a job runs

Editing commands stayed enabled during a recluster and a realign.

<details>
<summary>Details</summary>

- Audit (`claude/klusters-job-locks-audit-2026-11-24.md` in the project): each job state switched
  off a hand-kept list of actions.  Measured while the job ran: during a **recluster** every
  Hierarchy command, Save / Save As, Watershed, DipSplit, KNN split, the four Strip commands,
  Split Off Spurious, Split Burst Followers, Partition by Time, Recluster Against References
  and the reordering sorts stayed enabled; during a **realign** the same split / strip / sort
  commands and the feature-space switch.  Anything that enabled an action afterwards undid
  the lists: every change of the undo history re-enabled Undo, the selection re-enabled the
  Hierarchy menu.
- Seen: merging, or promoting from, the parent being reclustered applied the edit and threw the
  recluster's result away on arrival ("The temporary file containing the new clusters contains
  incorrect data").  The labels survived only because the spike count no longer fitted.
- **One lock** (`KlustersApp::editingBlocked`, `refreshEditLock`): while a recluster, plugin or
  drift job, a realign in any lane (worker thread, batch, queue), a nudge or a save runs, every
  editing action is disabled and held off over whatever re-enables it; when the job ends each
  returns to what was last asked of it.  The display, view and navigation commands stay.
- Guards where an action's state is not what stands in the way: Undo / Redo and the child
  undo refuse; Save refused only for a busy realign queue, now for every lane and for a
  running recluster or plugin; Close, Open and Quit are refused while a realign runs (its
  worker thread holds the document; they closed under it).
- **Change of behaviour**: with the automatic realign after a merge on, the next edit waits for
  it.  Edits to other clusters during a realign would need a lock per cluster: not done.
- Self-test steps `actionsEnabled` and `"whileBusy": true`; `test_edit_lock.py` (6 checks).
- Not covered: a mouse tool already armed when a job starts; the context menus; the
  Recommended-merges panel; the template palette.  An edit under a realign was not reproduced
  as a fault (a thread race), only closed off.

</details>

---

## 2026-11-24 — Klusters: a move of spikes no longer wipes the cluster information

Promote Children or Drop to Noise erased the information of every cluster.

<details>
<summary>Details</summary>

- Found by the undo run on a real session: after Drop to Noise on one child, the quality
  entered for two parents was gone.
- `Data::moveSpikeSubset` built the new cluster table from position and count alone, so **one
  move wiped the structure, type, quality and notes of every cluster of the layer**, the
  untouched ones included.  It is behind Promote Children, Drop to Noise, Dissolve, Break Up,
  Merge Orphans and a child dragged to another parent.  The information now rides along.
- `Data::restoreClusterLabels` (the undo of the child layer) did the same to the children.
- Not affected: merge, split, core cut, renumber, compact (checked).  A destination cluster
  created by the move starts empty, as before.
- **Sessions curated before this**: information entered before one of these commands and saved
  after it is not in the `.cli`.
- `test_annotation_survives_moves.py` (5 checks).

</details>

---

## 2026-11-24 — Klusters: the children follow a palette merge of their parents

After the palette's Group the children stayed filed under the merged-away parents.

<details>
<summary>Details</summary>

- Found by the undo run on a real session (jg05-20120312 group 8, the curated triple): with
  **automatic renumbering off**, the report taken after a palette merge of parents 72 and 74
  still filed their 19 children under 72 and 74, and their self children kept the old names.
- Hierarchy ▸ Merge Parents re-derives the child maps and renames the folded parents' self
  children; the palette's Group calls `groupClusters` directly and did neither.  The next
  hierarchy operation, or the automatic renumbering, put it right, so with the renumbering on
  (the default) nothing showed.
- Both now end with `KlustersDoc::refreshChildrenAfterParentMerge`.
- `test_palette_merge_children.py` (4 checks).  Not checked: what a Save in the stale state
  wrote before this.

</details>

---

## 2026-11-24 — Klusters: realign stays not undoable, and the manual says so

The manual promised an undo of a realignment that the application does not have.

<details>
<summary>Details</summary>

- Undoing a realign was looked at and **not done**.  `KlustersDoc::rejectLastRealign` exists
  but is unused and untested, does not restore the child layer's rows, and re-seeds the working
  `.spk` / `.res` / `.fet` from the saved files, which wipes the rows of every earlier unsaved
  realign; the nudge writes no journal entry.  What a correct one needs is in
  `doc/dev/ROADMAP.md`.  A wrong row written back is saved into the `.spk` and `.res`, so it
  wants a raw recording and a real session before it goes near Ctrl+Z.
- The manual said Ctrl+Z of a realignment restores the working copies.  It does not, and has
  not since the review dialog went: corrected.
- `test_child_recluster_undo.py` (6 checks): a child recluster is one step, undone and redone
  whole.

</details>

---

## 2026-11-24 — Klusters: a change of cluster information is an undo step

Undoing an edit silently took back the cluster information entered after it.

<details>
<summary>Details</summary>

- The information of a cluster (structure, type, quality, notes; saved in the `.cli`) is held
  with the labelling, and an Undo restores the level from before an edit with the information
  as it was then.  A change made after a merge was therefore dropped when the merge was
  undone, and came back with the redo (read in `Data::undo`; not reproduced on the old build).
- The palette's dialog now goes through `KlustersDoc::setClusterAnnotation`: one step of the
  undo history, undone on its own before anything older.  It holds no labelling and does not
  count against the depth, so annotating a session does not push the edits out of the history.
- OK on an unchanged dialog is no step.  The palette re-reads the shown information after an
  undo or redo.
- Self-test step `annotate`; `test_annotation_undo.py` (6 checks, parent layer; the child
  layer goes through the same call and is not covered).

</details>

---

## 2026-11-24 — Klusters: the undo depth counts steps, not levels

A command with more changes than the undo depth could only be partly undone.

<details>
<summary>Details</summary>

- The depth of the preferences trimmed four lists of LEVELS (the labellings in `Data`, the
  colours, the edit descriptors, each view's list).  A command pushes one level per change, so
  a Break Up or a promotion with more changes than the depth lost its first ones, and one Undo
  left it half reverted.
- The history now drops whole steps, the oldest first, and the four lists keep the levels of the
  steps it holds (`Data::undoLevelCap`, set by `KlustersDoc::pushUndoEntry`).  The curation
  log's buffer follows, so every move of an undone command is written `bad`.
- Bound: ten times the depth in levels (200 at the default).  Only a command larger than that
  is partly undone; the boxes of Break Up and Merge Orphans warn from there, not from the depth.
- Cost: a large command holds one labelling per change until it leaves the history.
- `test_undo_pairing.py` scenario 8 (depth 2: a child merge, then a promotion of three).

</details>

---

## 2026-11-24 — Klusters: an undone child edit is written bad in the curation log

The status of a child edit never changed.

<details>
<summary>Details</summary>

- A child edit's entry was exempt from the undo status: it stayed `good` when undone, and a
  reader had to join the `UNDO` record's `reverts_action_idx`.
- It is now held in the buffer like a parent action and flipped by its index when its step of
  the undo history is undone or redone (`CurationLogger::markCurrentActionRevertedByIndex`,
  `setUndone`).  The walk that pairs parent undos with parent entries passes over it.
- A child edit counts against the buffer depth, as its step counts against the undo depth.
- **Logs written before this**: undone child edits are `good`; join `reverts_action_idx`.
- `klusters_test_curationlogger` (2 more checks), `test_undo_pairing.py` (1 more).

</details>

---

## 2026-11-24 — Klusters: a saved undo depth of 2 is raised to 20

The default depth became 20, but a saved setting kept the old 2.

<details>
<summary>Details</summary>

- Every write of the settings stores the depth, so every installation holds a saved 2 and the
  new default of 20 reached nobody.
- A stored 2 is raised to 20 once (`General/nbUndoRaisedFromOldDefault`).  Any other stored
  value is kept, and so is a 2 set afterwards.  **A 2 chosen on purpose before this cannot be
  told from the old default and is raised too**: set it again in Preferences ▸ General ▸ Session.
- The self-test report carries `nbUndo`; `test_undo_depth_migration.py` (4 checks).

</details>

---

## 2026-11-24 — Klusters: Repair Nesting and an atom compaction are undo steps

Two changes to the children were outside the undo history.

<details>
<summary>Details</summary>

- An undo puts the whole atom labelling back as it was before the edit it reverts.  A change
  the history did not record was therefore reverted together with any older edit.
- **Hierarchy ▸ Repair Nesting** run by hand, and **Compact Cluster IDs** when it moves only
  atoms, are now each one step (`KlustersDoc::recordChildChange`).  A repair that finds
  nothing to re-cut is no step, and the status bar says which it was.
- Self-test: hierarchy ops `compact`, `repairNesting`; `test_undo_pairing.py` scenario 7
  (compaction).  The repair step is not covered by a test: no scripted edit leaves an atom
  across two parents.

</details>

---

## 2026-11-24 — Klusters: one undo history for both layers

The gaps the undo audit left open, closed: the parent and the child layer kept separate undo
stacks, and the default depth was 2.

<details>
<summary>Details</summary>

- **One history.**  Every edit, on the parents or on the children, is one entry of one list;
  Undo and Redo take the newest, whichever layer is shown.  Before, Ctrl+Z took from the layer
  on screen, so the order of two edits on different layers was lost.
- **A parent undo restores the children.**  Each parent entry holds the child labelling from
  before the edit and the undo puts it back.  A parent split across children no longer leaves
  the fragments behind after it is undone, and a parent split, delete, watershed or recluster
  no longer drops the child history (nor do Repair Nesting and the id compaction).
- **A hierarchy command is one step**: Promote, Drop to Noise, Dissolve, Break Up, Merge
  Orphans, however many children moved.  So are a merge or an edit and the renumbering Klusters
  runs after it.
- **Ctrl+Shift+Z / Y** (child-layer undo / redo) acts only when the newest edit is a child
  edit; it does not step over a newer parent edit.
- **Default depth 20** (was 2).  A depth saved in the settings keeps its value: check
  Preferences ▸ General ▸ Session.
- The boxes of Break Up and Merge Orphans, and the manual, say what holds now: one Undo for the
  command, and a command creating more parents than the depth is only partly undone.
- Self-test: spec key `nbUndo`, step `redo`; the hierarchy `undo` / `redo` ops report
  `undoSteps` / `redoSteps`.  `test_undo_pairing.py` is 23 checks over six scenarios.
- Each level now also holds one child labelling (4 bytes a spike).
- Left, in `doc/dev/ROADMAP.md`: a command larger than the depth; realign, nudge and the
  annotations are not undoable; changes the history does not record (Repair Nesting run by
  hand) are reverted with an older edit.  Tested with scripted synthetic sessions only.

</details>

---

## 2026-11-24 — Klusters: undo and redo, five faults from an audit

The undo paths were read and probed with scripted sessions at the default depth of 2.

<details>
<summary>Details</summary>

- **The log held one undo fewer than the data.**  An action without an undo twin -- a realign, a
  nudge, a rejected split, a `CONSIDERED` -- is begun like any other and marked not-undoable at
  its end; while open it counted against the depth and evicted the oldest merge.  Klusters
  realigns after every merge, so at depth 2 only the last merge's undo was recorded: merge,
  merge, undo, undo left the first `GROUP` written `good`.  The buffer is now trimmed before a
  new entry joins it.  **`status` in a log written before this undercounts undone merges.**
- **A child undo past the depth** reported success, logged an `UNDO` and changed nothing: the
  child stack was unbounded, the child history is `nbUndo` deep.  Capped.
- **A child undo after a parent undo** reverted the re-cut that undo had made and left a child
  across two parents ("1 atom(s) span more than one parent").  The child stacks are dropped when
  that re-cut changed the child labelling, as they already are after a parent edit.
- Ctrl+Z with nothing to undo no longer tells the log an undo happened;
  `compactAllClusterIds` clears the child redo stack with the undo stack.
- A child `UNDO` / `REDO` record carries `reverts_action_idx` (the edit's own status stays `good`).
- `test_undo_pairing.py` (10 checks), `klusters_test_curationlogger` (3 more).
- Left open, listed in `doc/dev/ROADMAP.md`: a hierarchy command is several undo steps; a parent
  undo does not restore the child layer; a parent split or delete drops the child undo history;
  the default depth of 2; realign and nudge are not undoable.

</details>

---

## 2026-11-24 — Klusters: the curation log covers the child layer

The pair evidence and the `CONSIDERED` record were parent-only; most of a hierarchical curation
happens among the children of one parent (444 `mergeChildren` in the log of 2026-11-20).

<details>
<summary>Details</summary>

- **`CONSIDERED` on the child layer**: children selected in the child palette, shown for the
  dwell and left, with `layer: child`, `parent_a`, `parent_b`, `same_parent`.
- **`mergeChildren`** gets the pair record of what it merged, measured before the merge: every
  pair up to six children, each against the largest above that; `dwell_s`, `selection_matches`.
  It had the ids only.
- **`promoteChildren`, `dropChildrenToBin`**: each child taken out against the largest sibling
  that stays (`role: leaving_vs_largest_staying`) -- what the curator separated it from.  Not for
  the promotions a Dissolve or Break Up runs inside itself, and at most 12 per command.
- Children merged or taken out are not also logged as `CONSIDERED`.
- Self-test: `hierarchy` steps `showChildren` and `dropChildren`; `test_curation_log.py` gains 7
  checks on a session with six children.  The template fields were `null` there (child means
  are cached when drawn, and the offscreen run does not draw them), so their values on the child
  layer are not checked.

</details>

---

## 2026-11-24 — Klusters: the curation log says what started an action, what it did, and what was looked at and left

Reading the log of a real curation (0312 g8, 1,203 actions) gave three wrong or unanswerable
readings; each is now in the log itself.

<details>
<summary>Details</summary>

- **`trigger` on every action line**: `manual`, `after_merge`, `after_edit`, `align_all`.  660 of
  that log's 1,203 actions were realigns -- one after each of 261 merges -- and nothing said they
  were Klusters' own (Preferences > Realignment, on by default), so they read as the curator's.
  `SESSION_OPEN` records the two automatic-after-merge settings as well.
- **`CONSIDERED`**, a new action: two to four parents shown together for 2 s and then left
  without a merge, with the same pair record a `GROUP` gets.  A merge rule needs the pairs that
  were looked at and kept apart; the log held only the merges, and of 3,377 unit pairs alike
  enough to be candidates it could not say which had ever been compared.
- **`GROUP` pair record**: `template_lag` and `template_cos_lag0` (27 % of the merged pairs were
  misaligned), `profile_cos` and `amp_ratio` (position and size apart from shape),
  `coactive_overlap` (`time_overlap` compares extents and read 1 for 95 % of the merges),
  `cross_refrac_observed`, `dwell_s`, `selection_matches`, and `recommended` (the
  Recommended-merges panel had selected the pair).
- **`REALIGN` detail**: outcome, spikes shifted, how far.  A realign that returned without
  changing anything left a `before` line, nothing saying so, and the log block open for the next
  action's `after` to land on; it now closes with `outcome: not_applied`.
- **`RECLUSTER` detail**: mode (plain / against references), sources, the clusters returned and
  their sizes.
- **`SESSION_OPEN.hierarchical`** said `false` for every hierarchical session (the line is
  written before the child layer is loaded).  It now says whether a `.clc` is there, and a
  `SESSION_CONTEXT` line follows the load with the parent and child counts.
- **`SESSION_CLOSE`**: `duration_s` and `n_by_action`.
- Self-test steps `group`, `wait`, `curationLog`; `test_curation_log.py` (21 checks, with a
  refused realign and a control at a one-hour dwell); `klusters_test_curationlogger` gains 7.
- Not logged still: a comparison in the child view, one that includes cluster 0 or 1, and which
  rank a recommended pair had in the panel.  `doc/klusters/README.md`, Curation logging.

</details>

---

## 2026-11-23 — Roadmap brought up to date

`doc/dev/ROADMAP.md` only; it was last updated on 2026-10-19.

<details>
<summary>Details</summary>

- **Done** gains Recluster Against References, a by-theme summary of November's entries of this
  file (super-sampled spaces, recluster engines and plugins, curation tools, hierarchy, ndm,
  KiloKlustaKwik), and fiber-kit's `.mti`/`.mtf` readers -- that planned item was already done.
- The `process_pca` `setFailed()` item said no exit path calls it; three do.  Reworded.
- The real-session item records that the run was started and what the flag steps reported.
- Checked against the code: the two items above.  Not re-checked: the other planned and
  considered items from October (Metal, the schema migrator, the KiloKlustaKwik audit and sweeps,
  the Template Library tab, templates per feature space, the session-YAML writers, events from
  spikes) and the cross-cutting debt lists.

</details>

---

## 2026-11-23 — Klusters: Recluster Against References

A new action for a cluster that is a mixture of known cells: it is sorted pooled with the clean
clusters of those cells, and only the mixed cluster is replaced.  (The user's design.)

<details>
<summary>Details</summary>

- **Actions → Recluster → Recluster Against References…** (`recluster.against`, no default key).
  Select the mixed cluster and its references; a dialog asks which of them is the mixed one.
- `Data::createFeatureFileAgainst` writes the pool: the mixed cluster whole, each reference up to
  `KlustersDoc::reclusterReferenceCap` spikes (three times the mixed cluster, at least 1000) --
  those inside the mixed cluster's time span, thinned evenly, or the nearest outside it.
- `Data::integrateReclusteredClusters(.., against)`: after the sorter's labels are read, where each
  reference's spikes fell is noted and the table is cut down to the mixed cluster's entries; from
  there on it is an ordinary recluster of that one cluster.  The references are among the clusters
  copied as they are, so the log, the undo step, the palette and the hierarchy see one cluster
  reclustered.
- The summary (output tab and status bar) names each piece with the reference it was sorted with,
  the spikes pooled per reference, a reference the sorter split, and a mixed cluster that came
  back in one piece.  Klusters renumbers after every edit, so the pieces are named by the id they
  have at the settled point (found through one of their rows), not by the id they were created
  under.
- It runs the KiloKlustaKwik command of the Reclustering preferences whatever engine is checked.
- `test_recluster_against_references.py`: a cluster of 150 + 150 spikes of two synthetic units,
  references of 250 each.  The references keep their rows and ids; the two large pieces are the
  two units (140 and 141 spikes, both pure; 19 spikes in a third, the sorter's noise); the summary
  names the right reference for each; two Undos restore the mixed cluster; an ordinary recluster
  of the same selection replaces the references as well; with 240 of 250 pooled per reference the
  split is the same.
- **Measured on the way:** with 150 pooled per reference (600 spikes, 13 dimensions)
  KiloKlustaKwik returns one cluster and the mixed cluster comes back whole; with 240 (780 spikes)
  it separates.  The pool has to be large enough for the sorter.
- **Not tested:** the dialog itself (the self-test step answers it), a session with a child layer
  (the code takes the same path as a recluster of one atom), a real session, and a mixture that a
  plain recluster of the mixed cluster alone cannot split -- the two test units are well apart.

</details>

---

## 2026-11-23 — Roadmap: what the collision-row work left open, and reclustering against references

`doc/dev/ROADMAP.md` only; no code.

<details>
<summary>Details</summary>

- **Next:** the collision loop on a real session (the first run is group 8 of jg05-20120312) and
  what to read off it.
- **Planned:** reclustering a mixed cluster against reference clusters (a new Klusters action:
  pool, sort, keep only the mixed cluster's pieces, references untouched); collision rows in
  super-sampled spaces.
- **Considered:** the smaller open points of the collision rows, and fiber-kit's open points
  (it has no roadmap of its own).
- **Done:** collision rows, `.eap` + sparse `.spkc`.

</details>

---

## 2026-11-23 — Self-test: test_recluster_klustakwik read the chunk count off any line

`test_recluster_klustakwik.py` failed on some runs (4 of 6 at one point, then none of 15) at
"chunks of 0.1 min: the flag, and several chunks over 20 s".

<details>
<summary>Details</summary>

- The test took "sorted as one chunk" to be the text `1 chunks` anywhere in the sorter's log, and
  "several chunks" to be its absence.  In a failing run the log did hold `mode: chunked ... ~3
  chunks`, so it was the `1 chunks` half that failed.  KiloKlustaKwik prints a chunk count on
  other lines too ("DipSplit per-chunk: N splits across M chunks", "noise -- N spikes across M
  chunks"), and those differ from run to run.  The matching line itself was not captured: the
  failure did not come back while I was printing it.  The cause is therefore inferred.
- The test now reads the count from the one line that states it, `[Stage 1] Chunking (.., N
  chunks, ..)`: 1 for the one-chunk cases, 2 or more for the chunked one.
- An earlier note of mine blamed a truncated log; that was wrong (the pane is not truncated).

</details>

---

## 2026-11-23 — Klusters: a line of a child process is no longer cut where the other stream speaks

The output pane (recluster, plugins) showed some lines of a program in two pieces.

<details>
<summary>Details</summary>

- `ProcessLineMaker` keeps one buffer per stream and emits a line at its newline.  When data
  arrived on one stream it first emitted whatever the OTHER stream had buffered, finished or not:
  a stderr line read in two pieces with a stdout line between them came out as two lines.
- Each stream's unfinished line now waits for its newline.  What is left when the process has
  exited or was killed is emitted then (before, a last line without a newline was shown only if
  the other stream happened to speak after it).  `reset()` clears both buffers.
- `klusters_test_processlinemaker` (ctest, POSIX): a shell child writes a stderr line and a stdout
  line each in two pieces around a line of the other stream, and ends both streams without a
  newline.  All lines arrive whole and in order; on the old code 4 of 5 checks fail.
- Found while looking for the cause of the intermittent failure of `test_recluster_klustakwik.py`;
  it was not that cause (next entry up).

</details>

---

## 2026-11-23 — Klusters: realign measures a collision row on its corrected record

A realign took each spike's lag from its raw record.  For a collision row that record holds two
spikes, and the lag that fits the unit's mean best is pulled toward the partner.

<details>
<summary>Details</summary>

- **Measured first** (`analysis/realign-collision-rows` in the project): 19,800 collisions made on
  real spikes of jg05-20120312 group 8 (a real record of one curated unit plus another unit's mean
  at a known offset, 0.7-1.2 of its amplitude).  Against the lag of the record before the partner,
  the raw collision record gives another lag in 44.2 % (by 2 samples or more in 16.6 %); the
  corrected record in 8.9 % (0.3 %).  Worst where the partner is larger than the spike (66 % /
  40 %) or within 6 samples of it (67 % / 29 %).  The partners there are made, with the template
  known exactly; real collisions were not measured.
- `realignSpikes` puts the corrected record of every collision row of the cluster into its
  measuring copy, after the stored records are set aside.  The cross-correlation passes, the mean
  they align to and the centroid mode all read that copy.  What is written is unchanged: a fresh
  cut of the recording at the new time, or the stored (raw) record for a row that does not move.
- The optional PCA refinement compares raw cuts at candidate lags; collision rows are left out of
  it (CPU and GPU path) and keep the lag their corrected record gave.
- Only when corrected rows are shown (the preference) and built on the present clustering.  The
  log names the count; the corrected rows are rebuilt at the new times as after any realign.
- `test_realign_collision_rows.py`: 60 planted collisions 3-7 samples from the spike, in a session
  whose true spike times are known.  Off their true time after the realign: 1 of 60 measured
  corrected, 33 of 60 (6 by two samples or more) with the preference off; the rows without a
  partner end the same (5 and 6 of 340 off).  Synthetic session.

</details>

---

## 2026-11-23 — Core: a reference .spkc and shared cases for the collision fit

The `.spkc` format and the partner fit are each implemented in both repositories, and their
agreement had only been measured by hand.

<details>
<summary>Details</summary>

- `golden.spkc.standard.1` in `test/golden/`: written by `writeSpkc`, read field by field here and
  in fiber-kit, and reproduced byte for byte by fiber-kit's `write_spkc`.
- `spkc_fit_cases.bin`: 26 collision records (true partners at whole and fractional offsets, stored
  offsets up to 4 samples off, amplitudes inside and outside the accepted range, partners that are
  not in the record, a look-alike partner, a partner at the window's edge), each with the result
  expected: accepted or not, the offset, the corrected record.  Written by fiber-kit
  (`tools/gen_spkc_fit_cases.py`); `core_test_golden_formats` runs `decollide_fit.hpp` on it.
- Result here: 26 of 26 agree on accepted and offset, and 26 of 26 corrected records are identical
  to fiber-kit's.  The test allows a corrected sample to differ by one unit (the two sum in a
  different order); accepted and offset must agree exactly.
- Both files are in `vectors:` of `contract.yaml` and in `test_shared_contract.py`.  No contract
  version change.

</details>

---

## 2026-11-23 — Klusters: Save As takes the stage's .eap with it

A session read under a stage name with its own `.eap`, saved under a new stage name, left the new
stage without one: it read the base `.eap` (other offsets, or no partners at all) or none.

<details>
<summary>Details</summary>

- After a Save As the `.eap` read under the old name is copied to the new name's own
  (`<base>.eap.<group>[.<stage>]`), written beside it and swapped in, unless the new name already
  reads that same file.  With the preference on or off.  The fitted offsets are then written to it
  as after any save.
- The copy follows the save, like the offsets and the `.spkc`; it is not part of the save's file
  transaction.  If it fails a warning is logged and the save stands.
- `test_collision_rows.py`: stage `a` (its `.eap` names 40 partners, the base `.eap` none) saved
  as stage `b` -- `b` has the same table byte for byte and corrects the 40 rows, at once and when
  opened later; four checks fail with the copy disabled (0 rows corrected under `b`).

</details>

---

## 2026-11-23 — Klusters: Sort Spikes by Time carries the .eap and the .spkc

The sort moved the rows of the `.res`, `.spk`, `.fet`, `.clu` and `.clc` and listed the `.eap` as
"not reordered (regenerate it)"; the `.spkc` it did not know.  After a sort the membership table
named the wrong rows.

<details>
<summary>Details</summary>

- **`.eap`**: one record of `nClasses` bytes per spike after the 32-byte header, permuted like any
  other row file.  A cell is an offset from the row's own time, so the rows move unchanged.  Every
  `.eap` of the group (stage copies too) with the group's spike count; another count is skipped.
- **`.spkc`**: each listed row is renamed to where it moved and the records put in the new
  ascending order.  A file over another spike count, or unreadable, is skipped; another group's is
  not touched.
- Both are written beside the originals and swapped in with the rest: on a failure every file is
  put back, as before.  The `.col` sidecar is still only reported.
- The close / rewrite / reopen part of the action is `KlustersApp::sortSpikesByTime`, which the new
  self-test step `timeSort` runs without the dialog.
- `test_time_sort_collision_files.py`: 60 rows (collision rows among them) out of order in all six
  files; after the sort each is byte for byte the time-ordered file; the reopened session corrects
  the same 40 rows and serves the same records.  That last check needs the reset of the entry
  below (without it: 0 of 40 corrected after the reopen).  Synthetic session only.

</details>

---

## 2026-11-23 — Klusters: collision-row state is dropped with the document

What Klusters holds about collision rows is indexed by row -- the times the `.eap`'s offsets were
written for, the feature values put aside for corrected rows, the last fits.  It was kept when the
document was closed and used for the next one whenever the `.eap` had the same name and the group
the same spike count.

<details>
<summary>Details</summary>

- Close, let a tool rewrite the session (a realign that moves a unit and writes the new offsets),
  open it again in the same Klusters: every moved row counted as moved once more, each partner was
  sought that many samples off, and feature values of the closed document were put into rows of
  the new one.  In the test below 19 of 40 collision rows were left uncorrected and the other 21
  were corrected wrongly (error at 1.0 of the raw row's).
- `KlustersDoc::resetCollisionRows()` at close and before the first rebuild at open.
- Reopening the same unchanged files was not affected (the stale times were the right ones), which
  is why the earlier tests did not show it.
- Self-test step `reopen` (close and open again in the same process; `replace` overwrites files
  in between).  `test_collision_rows.py`: unit 2 moved 8 samples later on disk between close and
  open -- 40 rows corrected, none counted as moved, error 0.056 (median) of the raw row's; with
  the reset disabled the three checks fail as described.

</details>

---

## 2026-11-23 — Klusters: a corrected collision row is placed by its corrected features

A collision row was drawn with its partner subtracted in the waveform views, but in the cluster
views it still sat where the sum of the two spikes had been projected.  Its features are now the
projection of the corrected record.

<details>
<summary>Details</summary>

- After each rebuild of the corrected rows, every corrected record is projected onto the PCA basis
  of the open space (its `.pca`) and the row's PCA columns are replaced, in memory.  The clusters
  touched are re-featurized and their views refreshed.
- **In memory only**, like the corrected records: the `.fet` on disk stays the projection of the
  raw `.spk`.  The file values of every replaced row are held; they come back exactly when the
  preference is turned off or the row stops being corrected, and Save writes them, not the
  corrected ones.  A realign or nudge rewrites a row's features from its raw record; the rebuild
  that follows takes that as the new file value and projects the new corrected record.
- A space without a `.pca`, or whose basis does not fit the records (channels, window, number of
  feature columns), keeps the raw features; the report counts the rows as skipped.
- Self-test `test_collision_rows.py`: the features of the 40 corrected rows equal the projection
  of their corrected records exactly; their distance to where the rows sat before the partner was
  added is 0.063 (median) and 0.375 (worst) of the collision's displacement; rows not corrected
  keep the file's values; preference off restores all 40 exactly; after nudges the features follow
  the new corrected records; the saved `.fet` holds the raw projections.  Synthetic session only.

</details>

---

## 2026-11-23 — Klusters: collision rows, four faults found in a review

A read-through of the collision-row patches of this day, on both sides (fiber-kit 0.71.2 has the
same rules).

<details>
<summary>Details</summary>

1. **An edit of the clustering left the corrected rows as they were.**  They are built on the
   clustering -- which unit a row is, which rows make a template -- and were rebuilt after a
   realign, a nudge, a save, but not after a move, a merge, a split or an undo.  The application
   now checks when idle whether the membership table is still the one they were built on
   (`KlustersDoc::collisionRowsOutdated`) and rebuilds.
2. **A class's home could be a mixture.**  It was the cluster holding most of the rows naming only
   that class.  A sort that leaves many of a unit's spikes outside it -- fiber-unmix's cluster of
   one-unit spikes holds every unit's -- makes that cluster the largest holder, and its mean the
   "template".  `decollide_fit::preferProvenance`: the cluster the `.tcl` says the class was made
   from, while it still holds at least 30 of those rows.
3. **A partner was fitted, and subtracted, without the row's own template.**  Fitted alone, a
   look-alike partner within reach of the peak takes the own spike for itself.  `correctRow` now
   subtracts nothing from a row whose own unit has no template; the report counts such rows.
4. **Save.**  A partner the fit never placed kept its old offset in the `.eap` although its row
   had moved -- right in memory, wrong after a reopen: its offset is now carried to the saved times
   too.  A stage reading the base `.eap` no longer gets a copy of it when nothing changed.  With
   the preference off, an existing `.spkc` is left alone instead of being rewritten empty.

The `collisionRows` self-test step no longer rebuilds: it reports the rebuild the application made
itself and how many there have been, so a missing hook shows.  `test_collision_rows.py` counts
them (open, preference, each nudge, Save) and adds a core cut of the partner's unit: rebuilt, 40
rows corrected.  Klusters and fiber-kit still write the same `.spkc`, byte for byte, on a session
where three in four spikes of both units sit in one mixed cluster.

Looked at and left: time-sort / dedup and the `.eap` (a known gap); realign finds a row's peak on
the raw record; a Save As to a new stage name does not carry the stage's `.eap`.

</details>

---

## 2026-11-23 — Klusters: the quality dot of a cluster without a value is a white circle

<details>
<summary>Details</summary>

A cluster holding fewer than 30 spikes has no quality value; its dot is an empty circle.  It was
drawn in the view's text colour and is now white.  `test_waveform_quality.py` counts the white
pixels in each dot's bounding square (the step reports `whitePixels`): some for the empty circle,
none for a dot with a value.

</details>

---

## 2026-11-23 — Klusters: collision offsets follow realign and nudge, and are saved

A collision row keeps its partner however far the row, or the partner's unit, is moved; Save writes
the fitted offsets to the `.eap`.

<details>
<summary>Details</summary>

The `.eap` says where in a row's window the partner is, relative to the spike times it was written
for.  The fit searches 4 samples either side, so a row nudged five times -- or realigned by more --
lost its partner, and so did every row naming a unit that had been moved.

`rebuildCollisionRows` now keeps the times the `.eap` holds for (taken when it is first read, and
again when the offsets are written back) and starts each partner's search from

    stored offset - (the row's time change) + (the partner unit's time change)

the unit's change being the median over its rows that are not collisions.  At Save
`writeCollisionOffsets` puts the offsets the last rebuild fitted -- they are relative to the times
being saved -- into the stage's own `.eap` (a stage that read the base file gets its own), then the
rows are rebuilt from the files.

`test_collision_rows.py`: after five one-sample nudges of the unit (the planted offsets were
already written up to 2 samples off) all 40 rows are still corrected, median error 0.3 % of the raw
row's; after two more of the PARTNER's unit, still 40; after Save the `.eap` holds planted - 5 - 2
for at least 38 of the 40 (the others one sample off), the own classes and a partner that was never
found are untouched, a rebuild finds no row moved and the fits where the file says, the saved `.spk`
is the raw cut at the saved times, and the `.spkc` holds the 40 rows.

Not done: features of collision rows; a row whose own class is not at offset 0 is not followed
(realign re-centres a row on its own unit); super-sampled spaces.  Synthetic session only.

</details>

---

## 2026-11-23 — Klusters: a collision row is shown as its own unit's spike

A spike row whose `.eap` row names a partner unit is served with that partner's fitted template
subtracted.  The `.spk` is not changed.

<details>
<summary>Details</summary>

Both Data layers hold the corrected records in memory beside the realign overlay
(`Data::setSpkcOverlay`; `ClusteringSnapshot::readSpk` serves a corrected row first, `readSpkRaw`
never does), so the waveform view, the mean and median waveforms and every tool that reads a record
through a snapshot see the row as its own unit's spike.  `KlustersDoc::rebuildCollisionRows`
(`klustersdoc_spkc.cpp`) builds them from the raw records, the stage's `.eap` and the clustering
with `core/decollide_fit.hpp`: own class and home cluster from the membership, templates from the
300 non-collision rows of a unit nearest in time.

They are rebuilt on open, after a realign, a nudge, a rejected realign, a space switch, a save and
a preference change, and written as a sparse `.spkc` at Save.  *Preferences > Cluster view >
Subtract the partner from collision rows* (on by default) switches to the raw records.  The corrected
rows are tied to the `.spk` reader they were built against: after a file is re-opened or the space
changes they are not served until rebuilt.

`test_collision_rows.py` (the `collisionRows` self-test step), two synthetic units, 40 collisions
planted in the recording at 9-13 samples, the `.eap` offsets written up to 2 samples off either way:
all 40 corrected, the error against the record before the partner was added a median 0.4 % (worst
4.5 %) of the raw row's; at most 2 of 40 fitted offsets off the planted one, by a sample.  Negative
controls: a row whose `.eap` names a partner that is not in it, rows without a partner, an `.eap`
without partners, no `.eap` -- nothing corrected, raw records served.  After a nudge of the unit by
one sample the rows are corrected from the re-cut records (median error 0.4 %; the records served
before the nudge would be at 55 %).

Running that test found a fault in the first version of `decollide_fit::fitPartner`: the search
never moved to a shift EARLIER than the stored one.  Fixed in the core patch, with a check there.

Not done: the features of a collision row are still those of its raw record; fitted offsets are not
written back to the `.eap` (a row moved more than about 4 samples in all loses its partner); a
super-sampled space shows raw records; time-sort and dedup do not carry the `.spkc`; `spkc` is not
a custody type token.  No real session has been through it.

</details>

---

## 2026-11-23 — Core: collision rows with the partner subtracted, and the sparse `.spkc` that holds them

A spike row whose `.eap` row names two units holds both spikes.  The first piece of showing it as
its own unit's spike: the subtraction and the file for its result.  Nothing uses either yet.

<details>
<summary>Details</summary>

**`.spkc`** (`neurofileio::readSpkc` / `writeSpkc` / `spkcFind`, `kSpkcVersion` 1,
`doc/ndmanager-plugins/formats/spkc.md`): a row index and the corrected records of
those rows only.  The `.spk` stays the raw cut of the recording, so every tool that re-cuts it
keeps working and a row cannot be subtracted twice; a reader takes row *i* from the `.spkc` when it
is listed.  `contract.yaml` lists the format (`contract: 3`).

**`core/decollide_fit.hpp`** (header-only): `fitPartner` fits a partner's template into a record
within a few samples of the offset the `.eap` holds -- jointly with the row's own template, to a
fraction of a sample -- and returns the amplitude and the shift; `subtractPartner` removes it;
`correctRow` does both for every partner of a row.  A fit is accepted only for a plausible
amplitude (0.3-2.5) that removes at least 5 % of the residual energy.  `homeClusters` and
`splitRow` read off the membership which cluster a class now is and which class is a row's own, so
neither depends on cluster ids surviving a renumbering.

Nothing but the `.eap` has to be kept in step when a row or a unit moves: the amplitudes are not
stored, and the fit returns the offset it found.

Measured in `core_test_decollide_fit` (synthetic 52 x 8 records, noise sd 12): a partner planted
at 11.4 samples and 0.8 of its template, searched from a stored offset of 8 and of 14, is found within 0.2
sample and 5 % of its amplitude, and the corrected row is the own spike to within a quarter of the
noise energy.  Negative controls: 200 rows with no partner and 200 with a partner at 0.15 of its
template -- none accepted, none changed.  With a partner 3 samples from the own trough, fitting it
alone misses its amplitude by more than three times the joint fit's error.

Not done: no real data has been through it; the partner's template ends where the window does, so
the part of its spike outside its own window stays in the row; fiber-kit does not read or write
`.spkc` yet (its `contract.yaml` and the reference file come with that patch).

</details>

---

## 2026-11-22 — Klusters: the quality dot has no perimeter

<details>
<summary>Details</summary>

The dot was ringed in its cluster's colour.  The ring is gone: a dot is the quality colour alone.
The dot of a cluster with too few spikes for a value stays an empty circle, now drawn in the
view's neutral text colour instead of the cluster's.

`test_waveform_quality.py` counts the pixels of the cluster's own colour in a 20-pixel dot's
bounding square (the step reports `clusterColourPixels`): 19-20 with the ring, none without.

</details>

---

## 2026-11-22 — Klusters: a quality dot per cluster in the waveform view

<details>
<summary>Details</summary>

A coloured dot in the upper right corner of each cluster's waveforms -- green, yellow, orange, red
-- for parents and children alike.  Its colour is the normalised residual variance of the spikes
the view holds for the cluster about their median waveform (`waveformquality.h`):

    value = sum (mean squared difference from the per-point median - background) / sum median^2

over the window less its first and last tenth, and all channels; the background of a channel is the
mean square at those edge samples.  A cluster with fewer than 30 spikes held gets a hollow dot.
The tool tip of a dot gives the value and the number of spikes.

**Where it is computed.**  With the mean and standard deviation the waveform cache already keeps
per cluster (`Data::WaveformData::calculateMean`), so it costs one median per point of the spikes
the view holds and follows the view: sample mode, time frame, either layer.  The view repaints its
overlays when the statistics job of a plain fetch finishes (that completion used to be silent).

**Preferences ▸ Waveform view ▸ Cluster quality dot**: on/off, the three thresholds (0.12, 0.27,
0.52) and the diameter (12 px).

**What it is worth** (measured before it was built; `analysis/cluster-quality-dot/` in the
project).  On `sirotaA-jg-000005-20120312` group 8, 594 pipeline parents and children of at least
100 spikes were scored against the curated file, on the 100 spikes the view shows by default:

| space | AUC, under 90 % pure against 98 % or more | impure in the lowest fifth | in the highest fifth |
|---|---|---|---|
| `stderiv_C5` | 0.71 | 15 % | 55 % |
| `standard` | 0.73 | 16 % | 60 % |

So green does not mean pure.  Variants tried and not better: the mean in place of the median,
removing each spike's amplitude first (worse: the amplitude spread is itself the best single
sign, AUC 0.79-0.82), normalising by the background instead of the waveform's energy (no better
in `standard`, near chance in `stderiv_C5`), the four largest channels only.  The thresholds are
the 40 %, 60 % and 80 % quantiles of the `stderiv_C5` values of those pipeline clusters.

Two things to know when reading it on a curated session.  The curated file's parent 253, which
holds two cells of similar shape, reads 0.29 in `stderiv_C5` against 0.21 and 0.18 for its two
cells alone -- a step, but inside the spread of single cells -- and -0.01 in `standard`, where the
cells alone read -0.02 and -0.06.  And the curated parents, which span the whole session, read
higher than the pipeline's clusters: of the 163 with at least 100 spikes, 22 % are green, 17 %
yellow, 18 % orange and 42 % red at the defaults (median 0.38), because a cell's waveform changing
over hours counts as residual.  Time frame mode measures a stretch of the session instead.

**Tests.**  `klusters_test_waveformquality`: one unit reads about 0, a 20 % amplitude spread about
0.04, a second unit more the larger its share, the value is independent of gain and sample type,
the median of an even count, the cases without a value, the colours.
`test_waveform_quality.py` (the `waveformQuality` step): the dots' values equal numpy's on the
`.spk`, for parents and for children; colours follow the thresholds and are what is painted; a
20-spike cluster is hollow; position side by side and overlaid; off and on; the Preferences group.

**Not tested**: the appearance at a working window size (the harness's views are about 60-110
pixels wide; one grab of the real session was looked at, at that size).

</details>

---

## 2026-11-21 — Klusters: in the child view, a click on the curated parent returns to the parent view

<details>
<summary>Details</summary>

While children are shown, the parent palette keeps their parent selected.  A click on that parent
did nothing: the list's selection does not change, so the palette emitted nothing.  The way back
to the parent was to click another parent and then this one.

The palette's list now reports a plain left click on the item that is already the whole selection
(`selectedItemClicked`, passed on as `ClusterPalette::selectionClickedAgain`).  In child scope
Klusters answers it as it answers a click on any other parent: the parent layer is shown, with
that parent.  On the parent layer the click is ignored as before, and Ctrl and Shift clicks are
not affected.

**Tests.**  `test_child_view_parent_click.py` (the `clickParent` step sends the press and release
to the palette's list): from the child view a click on the curated parent shows it on the parent
layer, a second click changes nothing, a click on another parent still shows that one.  The first
check fails without the change.

</details>

---

## 2026-11-21 — Klusters: the refresh after a realign no longer takes the child view to the parents

<details>
<summary>Details</summary>

**What happened.**  A child realigned from the child view could leave the views on the parent
layer, showing a parent that had nothing to do with it.  It happened only to a child whose number
was also a parent's.

**Why.**  Every realign ends by restoring the menus (`noRealignState`), and that ends with the
refresh a tab switch makes (`slotTabChange`).  The refresh handed the ids the view shows to the
parent palette.  In the child view those ids are children:

- a child numbered like a parent selected THAT parent, and the palette's emission adopted it:
  the views went to the parent layer and the child palette was rebuilt for the wrong parent;
- any other child selected nothing.  The empty emission still took the views to the parent
  layer; the child palette then brought the child view back, so nothing was left to see but two
  layer switches and a rebuild of the child palette on every refresh.

Since the child ids became disjoint from the parent ids (the entry below) only the second case
is left, apart from the two bins and the reserved chunk ids.

**The change.**  In child scope the refresh leaves the parent palette alone: it already holds
the parent being curated.  Nothing changes in parent scope.

**Tests.**  `test_child_realign_scope.py`: a child and a self child realigned from the child view
are still shown on the child layer, the refresh on its own does not switch the layer even in
passing (`layerSwitches`, a count the `hierarchy` step now reports; this check fails without the
change), and a parent realign still ends on the parent.  The self-test's `realign` step takes
`"children": true`, and `tabRefresh` runs the refresh.  With `NS3_TRACE_SCOPE` set,
`setPendingParentSelection` now prints what it was asked for and what it kept.

</details>

---

## 2026-11-21 — Klusters: child ids are disjoint from parent ids — a parent's own child is parent + 10000, every other child 20000 and up

<details>
<summary>Details</summary>

**Why.**  The two layers of a hierarchical session shared one set of numbers.  A parent's only
child carried the parent's own id on purpose, and beyond that child 7 and parent 7 could be
unrelated.  Any code that asked "does this id exist?" accepted an id from the wrong layer, and a
child that shared a parent's number could make the child view fall back to the parent view.

**The numbering** (`hierarchyfiles.h`):

| Ids | |
|---|---|
| 0, 1 | the artefact and noise bins; each bin's one child carries the bin's id |
| 2 … 9999 | parents only |
| 10002 … 19999 | a parent's **self child**: parent + 10000 |
| 20000 … | every other child, numbered upwards as made |

- **Old files load.**  A `.clc` with any id in 2 … 9999 is in the old numbering.  On load a child
  numbered like the parent it sits under becomes that parent's self child, a parent's only child
  likewise, and the rest take 20000, 20001, … in ascending order of their old id.  No spike
  changes child.  The session is marked changed; **Save overwrites the `.clc` and `.clp`** in the
  new numbering (previous files kept as `.bak`).
- **The names are kept true** by one pass, `KlustersDoc::normaliseChildIds`, run after the
  re-cut that follows parent edits, after a parent is renumbered, sorted or moved to the end,
  after parents or children are merged, after a child is promoted, after a child undo or redo,
  and on load.  It renames only; it restores labels and puts nothing on the undo stack, so undo
  stays one press per edit.
- **Fresh children** come from `Data::setFreshIdFloor(20000)` on the child layer; every path that
  numbers new clusters goes through `highestIdForAllocation()`.
- **A parent id of 10000 or more** has no room: the child layer is not loaded and the message
  says to renumber the clusters.  Reserved chunk clusters keep their own id for their one child.
- **`.clp`** is indexed by child id as before, so it now has an entry for every id up to the
  highest child (about 100 kB for 5,000 children).

**Checked on a real session** (`jg05-20120312` group 8, a curated clustering: 815,728 spikes,
318 parents, 5,092 children in the old numbering): loaded, saved, and compared with the files
before.  The `.clu` is unchanged; old and new child ids map one to one; 148 parents with one
child each have it at parent + 10000; the other 4,942 children are 20000 … 25084; no child id
is a parent id; the `.clp` agrees with the document's map for every child.

Tests: `klusters_test_hierarchyfiles` (the helpers and the upgrade); self-test
`test_child_ids.py` (load, save and reopen; parents renumbered, sorted with a chain through an
id in use, moved to the end; parents merged; children merged; a child promoted; Flatten; undo
and redo of a child merge; a parent split with the child layer loaded), through a new
`hierarchy` self-test step.  `test_burst_followers.py` and `test_core_cut.py` now load an
old-numbering `.clc` and check the saved one; `test_residual_strip_children.py` uses the new ids.

**Not done / not tested**
- fiber-kit still writes the old numbering; its files load through the conversion.
- A curation log written before this names children by their old ids; nothing translates it.
- Not exercised: recluster of children, drop to noise/artefact, dissolve parent, the joint
  palette and matrix scope across a rename, the `NS3_CHILD_PRIMARY` backend, and the GUI by hand.
- The reported symptom (child view reverting to the parent view) was never reproduced here, so
  it is not shown fixed; what is shown is that the two layers no longer share an id.
- `test_recluster_klustakwik.py` failed one check in one run and passed in the next, on the same
  build: `"1 chunks" not in log` meets "1 chunks affected", a per-cluster line of a three-chunk
  run that the sorter prints only sometimes.  That session has no child layer, so this change is
  not on its path.  The test is left as it is here.

</details>

## 2026-11-21 — Klusters: Preferences pages scroll instead of being squeezed

<details>
<summary>Details</summary>

**Symptom.**  The "Spike under the cursor" group added to Preferences ▸ Cluster view the day
before could not be found on the page.

**Cause.**  The dialog put each page straight into its stack, with no scrolling.  The Cluster
view page is laid out for 930 px; the dialog asked for 780 and a 1080-line screen gives it about
that.  Qt then squeezes the page to its minimum size hint, which for these group boxes is far
below what they can be read at: at 780 px the t-SNE "Advanced" group was a strip without its
fields and the "Template projection scope" rows lay on top of one another.  The new group, last
on the page, is where a larger font or a shorter screen runs out first.

**Fix.**  Every sub page sits in a frameless `QScrollArea` and keeps the height it was laid out
for.  With room to spare nothing changes and no scroll bar shows; without, the page scrolls.
The dialog's minimum height drops from 780 px to 297 px.

Self-test `test_pref_pages.py` (the `prefPage` step now renders the whole dialog at a given
size): at 1000 × 700 the Cluster view page is drawn at its 930 px with a 288 px scroll range and
its last group is inside the dialog once scrolled; Splitting, Waveform view and Display fit and
have nothing to scroll.

**Not verified:** that this was what hid the group on the reporting machine.  It was reproduced
as a squeezed, partly unreadable page in an off-screen render, where the group itself was still
in view; on that machine the font, style and screen height are unknown.  Not tested: the Input,
Radial menu, Session and Plugins pages by eye.

</details>

## 2026-11-20 — Klusters: the spike under the cursor is a preference — on or off, the ring's radius and line, the trace's width

<details>
<summary>Details</summary>

**Preferences ▸ Cluster view ▸ Spike under the cursor**:

| Knob | Default | |
|---|---|---|
| Ring the spike under the cursor and draw its waveform | on | off: no feature view tracks the cursor |
| Ring radius | point size + 4 px | the ring in the feature view |
| Ring line width | 2.0 px | |
| Waveform trace width | 2.0 px | the spike drawn over the waveform view; screen pixels at any zoom |

The defaults draw what was drawn before.  Apply sets the tracking in every open feature view and
repaints; a view opened later starts from the preference.  Shift+V is unchanged: it toggles the
view it is pressed in, and the next Apply sets every view back to the preference.

Self-test `test_hover_spike.py`: the defaults; a 6 px trace paints three times the pixels of a
2 px one; a larger radius widens the ring and a wider line paints more of it; off, a view tracks
nothing; the page has the group and writing the dialog back changes nothing.  The page was looked
at as an off-screen render only.

</details>

## 2026-11-20 — Klusters: Split Off Spurious Spikes on Ctrl+Space, its knobs in Preferences ▸ Splitting

<details>
<summary>Details</summary>

**Ctrl+Space** now runs Actions → Split Off Spurious Spikes (rebindable as
`action.splitOffSpurious`), and **Preferences ▸ Curation ▸ Splitting** has a group for it:

| Knob | Default | |
|---|---|---|
| Cut sensitivity | × 5.0 | how sharp the kurtosis rise must be, against its size over 20–50% of the order; lower splits off more, higher less or nothing |
| Window: rise, open, fall, closed | −0.154, −0.031, 0.184, 0.430 ms | the part of the waveform compared, relative to the first half crossing; edges out of order fall back to the default window |
| Largest centring shift | 0.184 ms | 0 turns the centring off |
| Order at most | 40,000 spikes | above it an evenly spaced subset is ordered |
| Smallest cluster | 200 spikes | |
| Order on the GPU when there is one | on | |

The defaults are the ones the action shipped with (a `static_assert` ties
`Configuration::CoreCut` to `corecut::Params`), so nothing changes until a knob is moved.

Self-test `test_core_cut.py`: the key is Ctrl+Space; the sensitivity at its largest leaves a
mixed cluster whole; the Splitting page has the group, renders, and writing the dialog back
leaves the cut where it was (new `prefPage` step).  Not tested: any knob other than the
sensitivity for its effect on a cut; the page was looked at as an off-screen render only.

</details>

## 2026-11-20 — Klusters: Split Off Spurious Spikes orders on the GPU (CUDA)

<details>
<summary>Details</summary>

The ordering loop of Split Off Spurious Spikes is the action's whole cost: for n spikes of d
values, n steps of n × d multiply-adds.  In a CUDA build with a device, clusters of 4,000 spikes
or more now run it there.

- **On the device:** the spikes (column-major), their squared norms, the running products with
  the mean in double, a taken flag per spike.  One kernel launch per spike taken: every thread
  folds the spike taken last into its product and each block reduces to its nearest spike left;
  the per-block minima are copied back and the host takes their minimum.
- **Same order as the CPU loop** by construction: float products, double sums, ties to the lower
  index.  The float product's rounding can differ, so a near-tie may be taken the other way round.
- **Falls back** to the OpenMP loop on any failure, below 4,000 spikes, and in HIP, SYCL and Metal
  builds, which have no kernel for it.  The status line says "ordered on the GPU" when it was.

**Not run.**  No NVIDIA device or `nvcc` was available where this was written.
`corecut_cuda.cu` passes clang's CUDA front-end against the CUDA 12.9 headers and its device code
assembles with `ptxas` (sm_75); it was not linked, and the kernel has never executed.  The
dispatcher, its stub and the fallback are built and tested in the CPU build
(`klusters_test_corecut` exercises the ordering hook).  First run on a GPU: compare the status
line's counts for one cluster with a CPU build's.

</details>

## 2026-11-20 — Klusters: Split Off Spurious Spikes — a cluster's core kept, the rest split off

<details>
<summary>Details</summary>

**Actions → Split Off Spurious Spikes** takes each selected cluster (or child) and splits the
spikes that are not its core off into one new cluster.  No dialog, no confirmation box, one undo
step per cluster.

- **Centre.**  Each spike is moved, sub-sample, so that the point where its falling flank crosses
  half the trough depth on the cluster's primary channel lands where the median waveform's does.
- **Window.**  A tapered window asymmetrically around that crossing: rising over 0.15 ms before
  it, flat to 0.18 ms after, falling to 0.43 ms.  Every channel, unweighted.
- **Order.**  From the spike nearest the median waveform, always the spike that changes the
  cumulative mean least (the one nearest the running mean).
- **Cut.**  Per channel, the excess kurtosis of the spikes taken so far and its change over 2% of
  the order.  The cut is the earliest rank, past half the order, where that change exceeds five
  times its size between 20% and 50%.  No such rank on any channel: nothing is split off.
- **Size.**  The order is quadratic.  Up to 40,000 spikes are ordered (OpenMP); above that an
  evenly spaced 40,000 are, and the rest are kept when within the cut's step of the kept mean.
  Over 400,000 spikes the action refuses.

Measured on `jg05-20120312` group 8 against a curated clustering, spikes identified by row, with
the Python prototype (project notes, `analysis/core-cut`): the share of a parent in the curator's
largest unit, before and after, and how much of that unit stays.

| Parent | Spikes | Before | After | Unit kept | |
|---|---|---|---|---|---|
| 153, 0–10 min | 8,414 | 75.3% | 97.4% | 80% | recipe arrived at here |
| 153, 10–20 min | 8,222 | 65.5% | 79.8% | 85% | recipe arrived at here |
| 32 | 12,506 | 56.4% | 78.4% | 76% | recipe arrived at here |
| 153, 300–310 min | 3,622 | 79.2% | 89.7% | 87% | not looked at before |
| 186 | 4,572 | 82.7% | 96.7% | 57% | not looked at before |
| 226 | 9,439 | 81.6% | 89.9% | 82% | not looked at before |
| 155 | 2,620 | 74.5% | 88.5% | 61% | not looked at before |

- **It over-splits.**  A ten-minute piece the curator had left as one unit lost 45% of its spikes.
- **The cut is not stable to small changes.**  The C++ here centres on the median waveform's own
  crossing and interpolates with a Catmull-Rom cubic where the prototype used a fixed sample and
  a spline.  On four of the eight parents it cuts within 1% of the prototype; on the others the
  cut moved by 6 to 22% of the order (153 10–20 min: 49% against 70%, purity 85.6% against 79.8%).
  The purity rose on all eight with the C++ too (to between 78.6% and 100%); where the cut falls is what moved.
- **Assumes** one unit is more than half the cluster, and a negative trough.
- **Not tested:** super-sampled spaces (the rate is taken as the rate times the factor), groups
  of other channel counts than 8 and 4, other sessions.

`corecut.h` is Qt-free (`klusters_test_corecut`: a unit with a quarter of a neighbour in it, a
clean unit, a subset, an ordering hook); self-test `test_core_cut.py` (parent and child scope).
The numbers above are from the prototype on the standard space; the action reads whichever
space is shown.

</details>

## 2026-11-20 — Klusters: Split Burst Followers on children

<details>
<summary>Details</summary>

Split Burst Followers refused in child scope.  With children selected it now runs on the child
layer, in both forms: two children, or two reference children and a child that holds spikes of both.

- **The scan** reads rows and times from the `.clc` layer (`detectBurstFollowers(..., children)`);
  the children may have different parents.
- **The split** makes a new child beside the child the spikes came from, under that child's parent.
  The parent clustering is not touched: a follower found in a child of parent 3 behind the bursts of
  a child of parent 2 becomes a new child of 3, not of 2.  Moving it across is left to the curator.
- Children of the artefact and noise bins and of reserved chunk clusters are left out of the
  selection, as in the residual strip.
- Self-test: `test_burst_followers.py`, the two-children case (one new child holding exactly the
  150 followers, parent 3 in the saved `.clp`, `.clu` unchanged).  The two-references form on
  children shares the code and has no case of its own.

</details>

## 2026-11-20 — Klusters: Split Burst Followers with two references and a mixed cluster

<details>
<summary>Details</summary>

Split Burst Followers took two clusters, each a cell.  With **three** clusters selected it now
takes two reference cells and a third cluster that holds spikes of both, and sorts the third.

- **Which is the mixed one** cannot be read off a selection, so the action asks (one list, three
  entries).
- **The rule.**  The mixed cluster is not a cell, so no burst is its own: only its spikes behind a
  reference's burst leader are taken.  Each reference is scanned against the mixed cluster with the
  two-cluster analysis and the same two chance counts; the spikes behind its leaders become one new
  cluster, when above chance.  A spike behind leaders of both references stays where it is.
- **Not taken.**  A spike of the mixed cluster that *leads* a burst a reference then continues:
  taking spikes on either side of a reference spike would drop the order test, the one that keeps
  cells that merely fire together from being read as one cell.
- Self-test: `test_burst_followers.py` gains the case (100 followers of each of two references and
  200 other spikes in one cluster: two new clusters of exactly 100).  The chooser itself is not
  exercised by a test, and no real pair of references was tried.

</details>

## 2026-11-20 — KiloKlustaKwik: a heap overflow in the split trial with -MaxPossibleClusters below 13

<details>
<summary>Details</summary>

`KK::TrySplits` runs a sub-CEM on each cluster to see whether it splits, and started it with 13
classes (noise and 12) whatever the limits.  The per-class arrays of that sub-problem are sized
`MaxPossibleClusters`, so with `-MaxPossibleClusters` below 13 the M-step counted members past
their end (`KK.cpp`, `nClassMembers[Class[p]]++`).  The heap damage showed up later and elsewhere:
`free(): invalid next size (fast)`, `munmap_chunk(): invalid pointer` in `KK::~KK`, a segmentation
fault, or nothing.

- **Fix.**  The sub-CEM starts with `min(13, MaxPossibleClusters)` classes.
- **How it was found.**  A 600-spike run with `-MaxPossibleClusters 5` aborted; an
  AddressSanitizer build named the read (4 bytes past a 20-byte `Array<int>`, allocated in
  `AllocateArrays` from `TrySplits`).  With the fix that build runs the case clean, and a
  49-column one.
- **A Klusters self-test was failing on current sources.**  `test_recluster_klustakwik.py`,
  "chunks of 0.1 min", failed 4 of 4 runs with a freshly built `KiloKlustaKwik_cpu` and passes with
  the fix.  It had been passing against a binary built before the last KiloKlustaKwik changes:
  `ninja klusters` does not rebuild the sorter the test runs.
- **Not looked at.**  The other fixed starting counts (`KK_split.cpp`: 2 and 3) against a
  `-MaxPossibleClusters` of 1 or 2.

</details>

## 2026-11-20 — KiloKlustaKwik, process_denoiseuniform: a binary `.fet` or `.clu` whose count has a low byte of 0x30–0x39 is no longer read as text

<details>
<summary>Details</summary>

KiloKlustaKwik's `.fet` and `.clu` loaders, and `process_denoiseuniform`'s `.clu` reader, decided
text or binary from the file's first byte: an ASCII digit meant text.  The first byte of a binary
file is the low byte of its int32 count, so 48 to 57 columns or clusters (and 304 to 313, and so
on) were read as text.  A binary `.fet` of 49 columns -- 16 channels with 3 components each, plus
time -- stopped KiloKlustaKwik at load with `Array::SetSize: n == 0`.

- **One detector.**  `neurofileio::isBinaryCountHeader`: binary when the file holds at least four
  bytes and one of the first four cannot occur in the text form (digits, a sign, `.`, `e`, `E`,
  whitespace).  A count below 2^24 has a zero high byte, so every plausible binary header is
  detected.  The three readers call it; their text paths are unchanged.
- **`process_denoiseuniform`** now links `Neurosuite::core` for it.
- **Checked.**  `core_test_neurofileio_robust` (62 checks): counts 48–57, 304, 313, 560 and 12592
  are binary, the text forms are text.  KiloKlustaKwik on a 600-spike binary `.fet` of 49 columns
  now gets past the load.
- **Not fixed here.**  That run, and the same one with 25 columns (which loaded before as well),
  then aborts in stage 2.1 with a heap error (`free(): invalid next size`, `munmap_chunk(): invalid
  pointer`) on 600 random spikes in 2 chunks.  It is older than this change and not yet looked into.

Found by the pipeline audit of 2026-11-20 (H3).

</details>

## 2026-11-20 — Klusters: Split Burst Followers — two similarly shaped cells told apart by their bursts

<details>
<summary>Details</summary>

A bursting cell's later, smaller spikes are easily clustered with a second cell of similar
waveform.  **Actions → Split Burst Followers**, with the two clusters selected, uses the timing
to say which cell each such spike should be with.

- **The rule.**  The two clusters' spikes are pooled in time; a burst is a run with consecutive
  intervals of at most 10 ms; its first spike's cluster is the burst's reference cell; the later
  spikes of the burst that are in the other cluster are followers.  A burst in which the two
  clusters fall closer than 2 ms is left alone.
- **The result.**  Up to two new clusters, one per reference cell, shown beside the two selected.
  Not merged into the reference: the split is a hypothesis to look at.  One undo step each, no
  confirmation box.
- **Two chance counts**, each to be exceeded twofold at p < 0.001, counted in bursts (followers
  come several at a time, so a test on spikes calls clean pairs): one train shifted against the
  other by 40 to 70 ms, and the train run backwards.  The second is there because the first alone
  failed on real data: on session `jg05-20120312` group 8 it called pairs of clean units that fire
  together (32/186: 79 bursts against 32.9 shifted, but 64 run backwards).  With both, none of the
  21 pairs among seven clean units is called, and 14 of 8,556 directions over the 93 clusters of
  100 spikes or more are.
- **What was not shown.**  That the 14 are misplaced spikes: their mean waveforms lie between the
  reference's and the source's (correlation with the reference 0.85 to 0.99, with the rest of the
  source 0.94 to 0.98), clearly nearer the reference in one pair.  The pairs were not chosen for
  similar shape, which is what the action is for.

`burstfollowers.h` is Qt-free (`klusters_test_burstfollowers`: recall and precision above 0.9 on
a simulated pair, nothing called on a clean or a synchronous pair); self-test
`test_burst_followers.py`.

</details>

## 2026-11-20 — Klusters: a super-sampled space is read when it is first shown, not at open; super-sample mode no longer shows it

<details>
<summary>Details</summary>

Every usable feature space was read into memory when a document opened, the super-sampled (`_U2`,
`_U4`) ones included -- a `.spk` of two or four times the waveforms each, read whether or not the
space was ever looked at.  In super-sample mode the session also switched to that space at open.

- **At open.**  The `.clu`'s own space is loaded and every other base-rate space is preloaded, as
  before.  A super-sampled space is listed but not read.
- **On demand.**  Its `.fet` and `.spk` are read the first time it is shown (the *Space* box,
  Ctrl+Tab, Actions → Super-sample); the status bar says so while it reads, and the *Space* tooltip
  marks the spaces not in memory.  Once shown it is held until the document closes, so switching
  back and forth stays instant.  A space never shown holds nothing: what a build, a sync or a Save
  reads of it on the way is dropped at the next preload.
- **Super-sample mode.**  The mode still builds the space at open when its files are missing, and
  still saves the clustering under its token with *Also save the clustering under the super-sampled
  token*.  It no longer shows the space: the session stays in the `.clu`'s own space.  Changing the
  mode in Preferences no longer switches the shown space either.
- **Not changed.**  A `.clu` saved under a `_U` token opens in that space: it is the `.clu`'s own,
  and its files are the ones the document loads.  Realign, nudge, the follow and Save write a
  super-sampled space that is not in memory through its files, as they did for any space not held.

Self-test: `test_supersampled_on_demand.py` (the dump step now reports `held` per space).

</details>

## 2026-11-20 — Klusters: the spike under the cursor in the feature view is drawn over the waveform view

<details>
<summary>Details</summary>

Finding out what a point in the feature view looks like meant lassoing it into a cluster of its own.
Now the feature view tracks the spike drawn nearest the cursor and the waveform views draw that one
spike over its cluster's waveforms.

- **What is tracked.**  The nearest drawn point within 32 pixels of the cursor, in the feature scatter
  and in the oblique / t-SNE embedding alike; spikes hidden by the temporally-restricted projection
  are passed over.  A ring marks the spike in the feature view, and the status bar names it
  (`spike 4012 of cluster 37`, the number being its row in the `.spk` / `.fet`).  Nothing is tracked
  while a button is held (a lasso, a pan or a boundary drag), and the spike is dropped when the
  cursor leaves the view or the points change (an edit, another projection, the embedding closed).
- **How it is drawn.**  Two pixels wide, in the cluster's column (or over the stack in the overlay
  presentation), in the colour that stands out most from both the cluster's colour and the
  background: of white, yellow, cyan, magenta, orange and green, the one furthest in RGB from the
  nearer of the two.  The ring uses the same colour.  It is a live overlay: the cached waveform
  drawing is not redone, so following the cursor costs one positioned read of the `.spk` per spike
  and a repaint.
- **Cost of the search.**  The scatter is searched in world units scaled to pixels, one multiply per
  spike, so a 250,000-spike cluster costs about a millisecond per mouse move.
- **Toggle.**  `Shift+V` (command `cluster.hoverSpike`, rebindable in *Preferences > Input*), on by
  default.

`test_hover_spike.py` drives it through the self-test's new `hover` step: the tracked spike is one
drawn under the cursor, the overlay's samples are that spike's `.spk` row, its colour is at least 150
RGB units from the cluster's and the background's, it is painted in its cluster's column, it is gone
when the cursor leaves, a cluster that is not shown cannot be hovered, and the toggle stops and
restarts it.  Not exercised by a test: the embedding (the search there is over the embedding's own
points) and the temporally-restricted projection.

</details>

## 2026-11-19 — Klusters: a Time chunks preference for KlustaKwik reclusters

<details>
<summary>Details</summary>

A recluster never said how KiloKlustaKwik should chunk, so the sorter used its own default of 7
minutes: a cluster spanning a six-hour session was sorted as about fifty chunks and merged across
them, with `-MaxClusters` applying to each chunk.  *Preferences > Curation > Reclustering > Time
chunks* now chooses:

| Setting | Flag | |
|---|---|---|
| As the sorter decides | none | the default: behaviour unchanged |
| One chunk: the whole selection | `-ChunkMinutes 1000000` | the chunked sorter, uncut |
| Chunks of *N* min | `-ChunkMinutes N` | 0.1 to 1440 |
| Off: classic two-phase pass | `-ChunkMinutes 0` | |

- The flag is added only for a `KiloKlustaKwik*` executable (the classic KlustaKwik has no such
  option and would stop on it), and only when the Arguments name neither `-ChunkMinutes` nor
  `-ChunkFile`: a flag written there wins.  `Shift+M` / `Shift+C` use it too; fiber-kit and plugin
  engines do not.
- Settings: `reclusterChunking` (0-3) and `reclusterChunkMinutes`.
- On the two-unit fixture the single chunk recovers both units (381 / 381); the classic pass
  splits them into nine clusters, and 0.1-minute chunks into six.  The fixture is 20 seconds
  long, so it says nothing about which setting suits a long recording.

Self-test steps `recluster` (`""` / `"median"` / `"channel"`, with `opts` for the executable, the
argument template, the feature controls and the chunking), `reclusterLog` (the command and the
sorter's output) and `reclusterChunkArgs` (the flag for a given command line, nothing run); a
recluster in progress now counts as busy for the harness.  `test_recluster_klustakwik.py`: 11
checks covering the previous entry's time axis and this preference; it needs `KiloKlustaKwik_cpu`
(`KKK_BIN`, else the build tree's) and skips without it.  The preferences page was rendered
off-screen and looked at; the dialog was not driven by hand.

</details>

## 2026-11-19 — KiloKlustaKwik: the time column is always the time axis

<details>
<summary>Details</summary>

KiloKlustaKwik reads spike time at the **last selected** column (`timeDimIdx = nDims - 1`, some
seventy sites: the chunker, the refractory tests, the drift model) and clusters on the columns
before it.  That holds only if `-UseFeatures` ends in `1`.  A caller that switched the time
column off handed its role to the last feature it had selected: that feature dropped out of the
clustering and was read as time.

Klusters does this in every recluster mode but the plain one -- auto-select with the toolbar's
**time** box unticked, `Shift+C`, `Shift+M`, the mean-subtracted sub-dimensions.  On two clean
units (400 spikes each) merged into one cluster:

| `-UseFeatures` | before | now |
|---|---|---|
| 12 features + time | 381 / 381 / 38 | the same |
| 12 features, time off | 7 clusters (381, 223, 81, 77, 19, 17, 2) | 381 / 381 / 38 |
| `Shift+C` | (as above) | 381 / 381 / 38 |
| `Shift+M` | 30 clusters: 800 spikes "spanning 426 min", cut into 24 chunks by a residual component | 2 clusters, one chunk |

**Fix** (`KK_io.cpp`, both the binary and the text loader): the `.fet`'s last column is always
selected, with a NOTE in the log when the string had left it out; a string shorter than the file
is padded with `0` first, as before.  `-UseFeatures` now only chooses the feature columns.

- In the chunked sorter the time column was never a clustering dimension, so nothing else changes
  there.  In the two-phase pass (`-ChunkMinutes 0`) time takes part in phase 2, as it always did
  when selected; there is no way to keep it out of that pass, and there was none that worked before.
- Auto-select with 6 of 12 features still over-splits this fixture (8 clusters, with or without the
  time bit): that is the feature choice, not this fault.

Tested through Klusters (`test_recluster_klustakwik.py`, next entry).  The GPU builds share this
loader; only the CPU build was run.

</details>

## 2026-11-19 — Klusters: the cluster palette follows clusters moved to the end

<details>
<summary>Details</summary>

**T** (move the selected clusters to the end) renumbers them to the highest ids and keeps them
selected, but the list did not move: it stayed on the gap they came from, or -- once the post-edit
renumber had rebuilt it -- jumped back to the top.  With more clusters than the palette shows, the
selection was out of view.

- The palette now scrolls to the moved clusters (`ClusterPalette::scrollToSelection`: the first
  selected item, then the last, so a selection taller than the list ends on its tail).  The same
  for children moved to the end in the child palette.
- **The request holds through the rebuilds that follow.**  The post-edit automation clears and
  refills the list (its renumber), which resets the scroll; the palette repeats the scroll after
  each rebuild and each programmatic selection until the user scrolls or selects something.
- Two reasons a plain `scrollToItem` did nothing, both handled: a rebuilt list lays its items out
  lazily, and in batches, so the tail has no geometry yet -- the whole list is laid out once
  before the scroll.

Tested headlessly: `moveToEnd` step, and `palette` now reports the ids inside the viewport.
`test_move_to_end_follows.py`: 18 checks over 120 clusters -- one cluster, three adjacent, two far
apart, with the automation on and all off.  Before the change the outcome depended on timing (5 of
12 probe runs left the selection out of view).  Not checked by hand in the GUI; the child palette's
path is called but has no test.

</details>

## 2026-11-19 — Klusters: a multi-lasso split no longer selects a neighbouring cluster

<details>
<summary>Details</summary>

After *Split by Lassos* the selection was the new pieces **plus one untouched cluster** -- the one
just above the cluster the lassos were drawn in.  It happened whenever auto-renumber was on (the
default) and the source was not the last cluster in the list, which is why it looked occasional.

- **Cause.**  A multi-lasso split carves each lassoed region *and* each source's remainder out as
  new clusters, so the source is emptied and its id disappears.  The landing parked that id along
  with the pieces, on the note that "the landing drops anything emptied".  The auto-renumber that
  runs first compacts the ids: the emptied number is handed to the next cluster up, the parked id
  has no old id to translate from, and the landing then finds a cluster under it -- the neighbour.
- **Fix.**  `KlustersDoc::setPendingParentSelection` parks only ids that hold spikes at the moment
  of parking.  This is the one place every landing goes through, so the watershed commit on an
  embedding and the residual strip, which build their landing the same way, are covered too.
- With auto-renumber off the old behaviour was already right (the emptied id stays empty and is
  dropped); it is unchanged.

Tested headlessly: new self-test steps `select`, `multiLasso` (lassos given as spike sets) and
`palette` (ids in list order, the selected ones, spike counts), and `autoRealign` / `autoRenumber` /
`autoMatrices` in the spec.  `test_multilasso_landing.py`: 12 checks -- two lassos in a middle, the
first and the last cluster, overlapping lassos, lassos that consume the cluster, a lasso across two
clusters, each with auto-renumber on and off.  Before the fix 5 of the 6 failed with it on.  Not
checked by hand in the GUI, and not on a child-layer (atom) multi-lasso: that landing goes through
the child palette's own queue, which this patch does not touch.

</details>

## 2026-11-19 — Klusters: Strip Residual Outliers applies without a confirmation box

<details>
<summary>Details</summary>

After the options dialog and the scan, the action asked "Split N flagged spike(s) off into …?" with
*No* as the default button.  That question is gone: OK in the options dialog scans and splits in one
go.  The split is one undo step, as before.

- What the box reported is kept.  The status line names the new cluster(s) and the count by term
  ("… (412 on-channel, 96 off-channel, 31 both, 4 no signal).  Undo restores them."); the breakdown,
  the seed, an off-channel term with nothing to read and the per-cluster medians and tails go to the
  log under `[residual-strip]`, where the per-cluster lines already went.
- Two boxes remain, because neither is a question: the warning when the scan could not run, and the
  note when nothing was flagged (with the per-cluster diagnostics that say how far the thresholds are
  from the data).
- Cancel in the options dialog is now the only way to back out before the split.

Checked: builds; the two residual-strip self-tests pass (they drive the scan and the apply step, not
the dialog, so the removed box itself was not exercised by a test).

</details>

## 2026-11-18 — Klusters: Strip Residual Outliers works on children — scored whole, split into a sibling child

<details>
<summary>Details</summary>

With children selected in the child palette the action refused ("switch to the parent clustering
first (the strip does not operate on atoms)").  It now runs on **one or more children**:

- **The scan reads the child (.clc) layer.**  The ids, the seed and the report are children
  (`child 4`); the children of the artefact and noise bins are skipped as the bins are, and the
  children of reserved chunk clusters are left out.
- **No time chunks.**  Each selected child is scored whole, against its own median waveform (or
  the seed child's) and its own tails.  The *Time chunks* row shows "None — each child is scored
  whole" and is fixed: a child is already one piece of its parent, and cutting it again by the
  session's chunks would judge a piece of a piece on a handful of spikes.  The parent-scope
  choice is remembered separately and is not changed by a run on children.
- **The split makes a sibling child.**  The flagged spikes become a new child under the SAME
  parent (three with *Keep the terms apart*); the parent's membership does not change.  Children
  of different parents are never pooled into one new child — a child belongs to one parent — so
  each bucket is cut per parent.  Undo is the child layer's (Ctrl+Shift+Z).

Two faults in the apply step, found by driving it headless and fixed here (both could bite the
parent scope too, with *Keep the terms apart*):

- the sources of each new cluster were the ids the SCAN saw.  A split can be followed by an
  auto-renumber, and a stale id makes the builder cut the wrong cluster and drop the bucket; the
  sources are now re-read from the layer for each bucket;
- the layer was not re-asserted between buckets.  After the first split of children the landing
  can leave the PARENT active, and the next bucket was then cut out of the parent: in the test,
  the flagged spikes of parent 3's only child became a new PARENT cluster (parent 3: 1200 → 1154).
  Child scope is now re-asserted before every bucket.

Measured on the drifting synthetic unit of the time-chunk entry, parent 2's 1200 spikes cut by
time into three children: scored as one parent over the whole session 50 % of the 116
contaminants are found (0.6 % of the clean spikes flagged); scored as its three children, 95 %
(0.6 %) — each child spans a third of the drift.  Applied: one new child of 116 spikes, parent 2;
parent 2 still 1200; its four children add up to 1200.  Synthetic fixture; the dialog path with a
real child-palette selection was not exercised by hand.

- `KlustersDoc::detectResidualOutliers(..., onChildLayer)`; `KlustersApp::applyResidualStrip`
  (the apply step, out of the slot, shared with the selftest).
- Selftest: `residualScan` takes `"children"` and `"apply"`; `test_residual_strip_children.py`
  (18 checks).

</details>

---

## 2026-11-18 — Klusters: Strip Residual Outliers runs per time chunk — the Template view's partition, or the default template chunk length

<details>
<summary>Details</summary>

The strip judged each cluster against ONE median waveform over the whole session.  A cell that
drifts has no such waveform: the session median is the shape of no moment in particular, and the
spread around it is drift plus contamination — the tail is sited on the sum and the contaminants
sit under it.

The scan now runs **per time chunk**.  The dialog's new *Time chunks* row offers:

- **Template view partition** — the session drift partition of an open Template view (the
  regions set there, or the ones it seeded).  The default when there is one.
- **Uniform, N min** — equal chunks of the default template chunk length (*Preferences ▸ Cluster
  view ▸ Scope granularity*, 12 min as shipped), tiled exactly as the Template view seeds a fresh
  partition (`ceil(T / N)` equal regions).  The default otherwise.
- **Whole session** — one template per cluster, the action as first shipped.

Per chunk, a cluster's template and its two tails come from its spikes in that chunk.  A chunk
with fewer of the cluster's spikes than *Min spikes* borrows its neighbours: its reference window
grows one chunk each way until it holds enough; template and tails come from the window, and
only the chunk's own spikes are flagged from it, so every spike is judged once.  A **seed** is
read per chunk the same way (in the seed's own window), and with *Threshold from the seed too*
each chunk gets the seed's cut for that stretch of time.  The cluster as a whole must still reach
*Min spikes*.  The confirmation and the log say how many chunks were used and how many pooled.

Measured on a synthetic unit whose channel profile moves linearly across the probe over an 80 s
session, 116 contaminants among 1200 spikes (8 % with a weak foreign waveform added, plus the
other unit's spikes that fall inside a window), defaults (k 3 / 3, min 40), 10 s chunks:

| drift | scan | contaminants found | clean spikes flagged |
|---|---|---|---|
| none | whole session | 100 % | 0.0 % |
| none | 8 chunks | 100 % | 1.1 % |
| half | whole session | 50 % | 0.6 % |
| half | 8 chunks | 97 % | 1.0 % |
| full | whole session | 11 % | 1.6 % |
| full | 8 chunks | 57 % | 0.6 % |
| half | 40 chunks of 2 s (39 pooled) | 96 % | 1.7 % |
| half | seed, fixed cut, whole session | 56 % | 4.5 % |
| half | seed, fixed cut, 8 chunks | 100 % | 1.3 % |

What the table does not hide: **without drift, chunking costs** about a point of clean spikes
(smaller samples, noisier tails), and more with shorter chunks (1.7 % at 5 s) — so *Whole
session* stays on offer.  Under full drift the chunked scan still misses 43 %: the drift within
a 10 s chunk is itself a tenth of the way across, and shorter chunks did not help (56 % at 5 s).
Larger *Min spikes* with short chunks acts as a sliding window (at half drift, min 400 over 10 s
chunks: 99 %, 0.0 %).  All of this is one synthetic fixture; nothing here was run on a recording.

Limits: the partition is taken from an OPEN Template view; a `.wtl` on disk with no Template
view open is not read, and the uniform chunks are used.  Chunk boundaries are in seconds of the
shown space's time column.

- `KlustersDoc::detectResidualOutliers(..., chunkBoundsSec)`; `Data::spikeTimesInRecordingUnits`;
  `TemplateView::driftPartition`.
- Selftest: the `residualScan` step (the scan with no dialog, nothing applied) and
  `test_residual_strip_chunks.py` (10 checks).

</details>

---

## 2026-11-18 — Input preferences: Reset restores the default, a second press unbinds — and an unbound command stays unbound

<details>
<summary>Details</summary>

Preferences ▸ Input (the page Klusters and NeuroScope share).  A row's **Reset** could only put
the shipped binding back; there was no way to leave a command that ships with a key without
one.  Emptying the field looked like it worked and did not: an empty editor was committed as
"no override", so the default was live again and showed again when the page reopened.

- **One button, two steps.**  On a row that is off its default a press restores the default.
  On a row that is at its default the button reads **Unbind**, and a press clears the binding.
  So: Reset, then Unbind; a third press brings the default back.  The label and tooltip always
  say what the next press does.  A command that ships unbound has one step only (clear a custom
  key); its button is disabled while there is nothing to clear.
- **Unbound is a binding.**  It is an override holding the invalid chord: the registry already
  resolved nothing for one and listed it as *(unbound)*; what dropped it was the page's commit
  and the persistence.  It is stored as `unbound` (`Chord::toStored` / `fromStored` — a
  malformed string is an invalid chord too, so `fromString` cannot carry it), in the settings
  and in keymap layouts (`app.prefs = unbound`).  A build that predates the token reads it as
  unreadable and falls back to the default.
- **Mouse rows** get a *(none)* choice in the button / wheel list.  Unbinding keeps the row's
  modifiers, phase and modifier-match policy, so choosing a button again — or Reset — rebuilds
  the command's own kind of gesture, also on a page opened with the row already unbound.
- **A menu action follows a reset at once.**  `applyInputOverridesToActions` only touched
  actions that HAVE an override, so an action reset to its default kept the rebound key until
  the next start.  It now puts the shipped chord back on an action whose override is gone
  (Klusters and NeuroScope).  An unbound action has no shortcut.

Behaviour change to know: clearing a key field by hand now unbinds the command (it used to
revert silently to the default on Apply).

Tests: `prefinput_test.cpp` (27 checks on the real page, offscreen: key / button / wheel rows
through custom → default → unbound → default, commit, the stored form, reload, a command that
ships unbound); the stored form in `bindingregistry_test.cpp`; a layout with an unbound command
in `keymapprofile_test.cpp`.  Both applications build.  Not exercised by hand in the GUI.

</details>

---

## 2026-11-18 — Klusters: the radial menu keeps an open ring while the cursor is moving

<details>
<summary>Details</summary>

Hovering a group wedge opens its entries as the next ring out, centred on the wedge and widened
to a minimum wedge angle — so the child ring is wider than its parent, and the straight path
from the parent to most of its children runs over a neighbouring parent wedge.  Every mouse move
retargeted the rings at once, so that crossing closed the ring being aimed at and opened the
neighbour's: the hand had to go radially out first and then around.

A move that would **close an open ring** is now held until the cursor settles:

- onto a different parent wedge, back onto an ancestor ring (its grandchildren would close), or
  into the hub;
- *settled* = the cursor stays within 4 px of where the dwell began for 100 ms.  The dwell
  restarts whenever it has travelled further, so a moving cursor never completes one and a
  creeping one (under ~40 px/s) does;
- reaching the open ring first drops the held move — nothing happened;
- a press, a release or a wheel step acts on the wedge under the cursor at once, so a
  press-drag-release flick onto a root leaf and a click are never delayed;
- moves that close nothing (along a ring, outward into a new ring) are immediate as before.

`radialmenu_test.cpp` (15 checks, offscreen): a continuous move from a parent across its
neighbour to the far child of the parent's own ring never retargets and lands on that child;
stopping on the neighbour hands over to it; passing through the hub keeps the rings, stopping
there collapses them; a click on a held wedge picks it.  With the hold disabled, 7 of the 15
fail.  The two constants (`kSettleRadius`, `kSettleMs`) are compiled in; they are a first
guess, not tuned by hand on a real pointer — the test drives synthetic events.

</details>

---

## 2026-11-18 — Klusters: the RMS recenter and the PCA refine run under the correlation window

<details>
<summary>Details</summary>

The correlation window (`--kernel`, Preferences > Realignment > Correlation window) weighted the
cross-correlation passes and nothing after them.  The two post-alignment modes that centre a
cluster then measured on the whole window again, so what the passes had been told to ignore voted
on the final position.

**RMS circular recenter.**  The centroid is now that of the windowed energy `w[t]·E[t]` (the
window weights quadratic forms in the correlation, so it multiplies the energy as it stands).
On the selftest's collided unit (40 % of the cluster's spikes carry a second, 2.5× larger spike
6–12 samples after their peak), share of spikes within one sample of their true time:

| window | before (collided / clean) | after (collided / clean) |
|---|---|---|
| none | 0 % / 0 % | 0 % / 0 % (unchanged: no window) |
| Gaussian ±5 | 0 % / 0 % | 100 % / 100 % |
| template energy | 0 % / 0 % | 16 % / 2 % |

Before, the whole-window centroid sat at sample 22 (target 15, R 0.57) and the recenter moved
every spike of the cluster 6–7 samples into the collision tail — the clean ones included, after
the windowed passes had aligned them.  The same holds with the cluster cut 3 samples early or
late.  The template-energy window only halves the error (median +2): the collisions are in the
mean waveform it is built from, so its weight in the tail is small but not zero, against an
energy there that is larger than the spike's own.  For a cluster with collisions, use a bell.

A bell is **followed**: measured once under a window sitting on the target, the centroid is
pulled toward the window's centre (a lobe at offset d reads d·σw²/(σw²+σe²)), so the window is
moved onto the centroid it measured until it settles — a mean shift from the peak sample, with a
fractional offset (rolled by whole samples it stalls up to a sample short as soon as a pedestal
dilutes the last step).  At the fixed point a uniform noise floor is symmetric about the answer
and cancels, as it does without a window.  A lobe the window cannot see from the target is not
followed.  The log prints the whole-window centroid, R and shift beside the windowed ones.
R under a bell is high by construction (0.95 vs 0.57 above), so `--rmin` is a weaker two-lobe
guard there; read the whole-window R in the log for that.

Without collisions the two agree to a sample: the whole-window recenter shifts the fixture's
cluster +1 (the unit's late positive phase is in its centroid), the windowed one 0; 99–100 % of
spikes are within one sample either way.

**PCA projection refine.**  The projection is taken under the window, `Σ w·ev·(x − mean)`, by
scaling the refine's private copy of the basis — where the channel restriction already goes, so
the CPU loop and the GPU kernel both honour it and the basis the features are recomputed through
is untouched.  **No accuracy gain is claimed for this half.**  On the selftest fixture the refine
misplaces the cluster with or without a window (0 % within a sample; clean spikes at −6 samples
unwindowed, +4/+5 windowed), collisions or none: with a centred basis it maximises the energy of
the deviation from the basis mean, which on a two-unit session grows as a spike is slid AWAY
from where the mean has it (projection energy at shift 0 is the minimum, 0.49 of the value at
−6).  That is the refine's own criterion, not the window, and it is not addressed here.  The
shifts the binary picks under the window are consistent with an independent computation of
the windowed energy (median +5 samples in both).  Unlike the correlation, the refine is not normalised by the windowed spike energy, so
a larger second spike slid onto the window's centre still scores high.

The per-spike centroid mode (`--align-centroid`) skips the correlation passes and does not use
the window.  KiloKlustaKwik's RMS recenter calls the unwindowed function and is unchanged.

- `realign_center::circularRecenterShiftWindowed` (libklustersshared), 6 new checks in
  `realign_kernels_test.cpp` (28/28).
- `klustersdoc_realign.cpp`: `realignRmsRecenter` and the refine take the window.
- Selftest: `realignMode` in the spec; `test_realign_center_window.py`.

</details>

---

## 2026-11-18 — Klusters: a seed cluster for the residual strip — the template AND the threshold from a carved core

<details>
<summary>Details</summary>

`Strip Residual Outliers` as first shipped is self-referential: each cluster supplies both the
shape it is judged against (its own median waveform) and the spread it is judged by (its own
median + k·1.4826·MAD).  Tested in the GUI it "works really well, so long as the initial cluster
is well defined" — which is exactly the predicted boundary, because a mixture poisons both halves
at once.  Measured on the validated unit (`jg05-20120312` group 6, exp_psame cluster 3, the parent
of the curated 785), with the template built from a deliberately contaminated pool:

| contamination of the pool | `onr` | `offr` |
|---|---|---|
| 0 % | 0.896 | 0.803 |
| 10 % | 0.881 | 0.783 |
| 20 % | 0.852 | 0.754 |
| 30 % | 0.807 | 0.720 |
| **50 %** | **0.624** | **0.622** |

Graceful to ~30 %, gone at 50 % — and that is not a coincidence.  The template is a per-point
**median**, whose breakdown point is exactly 50 %; past it the median waveform is no longer the
dominant cell's, and the statistic is measuring the wrong template.

**Iterating the self-template is not the fix.**  Score the pool against its own median, drop the
union of the two tails at k=3, re-median the survivors, repeat: that recovers about half the lost
ground in the 10–30 % band (at 30 %, 0.807 → 0.866 after three passes against a clean ceiling of
0.896) but **nothing at all at 50 %** (0.624 → 0.626) — once the median is on the wrong side of the
breakdown point, iterating strips the tail of the wrong distribution, which is the real cell.  It
also sheds 14–24 % of the pool, and 16.4 % of a *clean* pool for a gain of +0.000 `onr`.  Not
built.

**The threshold is the half an AUC cannot see.**  AUC is threshold-free, so the table above
measures only the ranking.  With a clean seed template but each scored cluster still siting its own
MAD tail, on 4,176 real keepers plus a varying fraction of junk:

| junk | rule | thr (on) | flagged | precision | recall | keepers lost |
|---|---|---|---|---|---|---|
| 10 % | own | 0.159 | 586 | 0.54 | 0.68 | 6.5 % |
| 10 % | seed | 0.143 | 686 | 0.49 | 0.72 | 8.4 % |
| 30 % | own | 0.224 | 1049 | 0.88 | 0.52 | 2.9 % |
| 30 % | seed | 0.143 | 1590 | 0.78 | 0.69 | 8.4 % |
| 50 % | own | 0.344 | 1408 | **0.96** | **0.32** | 1.5 % |
| 50 % | seed | 0.143 | 3221 | 0.89 | **0.69** | 8.4 % |

The own-cluster rule does not *misfire* on a dirty cluster once the template is clean — it gets
**conservative**.  The cut climbs 0.147 → 0.344 as the junk goes 5 % → 50 %, precision climbs to
0.96, recall collapses to 0.32.  A benign failure that converges under repeated passes, but it
leaves two thirds of the contamination in place.  Taking the tail from the seed holds it flat by
construction, for a stable recall of 0.69 and a stable ~8.4 % loss of real keepers at k=3 — and on
the seed's own distribution k *is* a direct false-positive rate, a better-behaved knob than k on a
mixture.

So `detectResidualOutliers` takes two more arguments.  `templateCluster > 1` designates a **seed**
— the clean core the curator has already carved, which is how they work — and its median becomes
the template for every scored cluster; the seed is removed from the scored list and is never
stripped itself.  `thresholdFromTemplate` additionally sites both cuts on the seed's own score
distribution, fixed across every scored cluster.  Seeding the template fixes the ranking; seeding
the threshold is what restores recall on a cluster that is half junk.

In the dialog: **Seed (template from)** — a combo over the current selection, defaulting to
"(none) — each cluster scores against itself", which keeps the shipped behaviour as the default
because a seed is a *claim* that one of those clusters is clean; and **Threshold from the seed
too**, on by default and disabled until a seed is picked.  The confirm dialog names the seed and
says whether the cut is fixed.  Per-cluster diagnostics gain a `template …` line carrying the
seed's medians and the fixed tail.

The caveat that comes with it, stated in the header: the seed's threshold never looks at the
scored cluster, so a **contaminated seed** inflates the cut and the action quietly under-strips.
The seed has to be the clean one — which is the assumption the curator is already making by
designating it.

Internals: the median-template build, the taper, the per-channel weights and the per-spike scoring
move into a file-local `ResidTemplate` plus `residBuildTemplate` / `residScoreCluster`, since the
template is now built either once from the seed or per scored cluster and the scoring runs on both
the seed and the scored clusters.  That also pays off part of the duplication debt the previous
patch recorded, though `detectWaveformOutliers` still has its own copy.

Built: `ninja -C build klusters` compiles and links clean (Qt6 available); `-Wall -Wextra
-Wshadow` report nothing in the new code.  The metric itself is unchanged, so the 2e-13 agreement
with the offline Python still holds.

`src/klusters/src/klustersdoc.h`, `src/klusters/src/klustersdoc_strip.cpp`,
`src/klusters/src/klusters.cpp`.

</details>

---

## 2026-11-18 — Klusters: Strip Residual Outliers — the per-spike gain divided out, the residual split on-channel / off-channel

<details>
<summary>Details</summary>

`Strip Waveform Outliers` (Shift+X) flags the robust tail of one distance: the kernel-weighted
residual of a spike against its cluster's median waveform, `D = sqrt(sum w (x-T)^2 / sum w T^2)`
with `w[p] = |T[p]|`.  Two things that costs on a contaminated unit:

* **amplitude counts as distance.**  There is no per-spike gain, so a bursting cell's own small
  spikes sit in the tail beside genuine intruders.  Measured on the unit this was built for
  (`jg05-20120312` group 6, the `exp_psame` parent of the curated cluster 785 — an interneuron at
  3.3 Hz, medium-low waveform energy), amplitude ALONE separates the unit from its siblings at
  held-out AUC 0.635.  A gain-blind distance is partly an amplitude cut wearing a shape cut's
  clothes.
* **the `|T|` point kernel is blind exactly where an intruder shows itself.**  It weights each
  point by the template's own magnitude, so the off-template channels — where a spatially
  displaced contaminant lands — carry almost no weight in the pooled `D`.

The new action fits a per-spike gain on the template's own channels, takes `r = x - a*T`, and
splits the residual's power by how much of the template each channel carries.  Per cluster, over
the document's channel selection, with `T` the cluster's own per-point median waveform:

```
h[t]  = SQUARED Hamming taper of `window` samples centred on the template's peak,
        0 outside (1 everywhere when 0).  Squared because the taper is an amplitude
        weight and every form below is quadratic -- the same statistic as tapering
        x and T and then squaring, which is what the offline measurement did.
        CHANNEL-FREE, unlike |T|, so it survives off-template; its job is to drop
        the baseline flanks, where the residual is pure noise.
E_c   = sum_t h T^2                      per-channel template energy
u_c   = E_c / max_c E_c in [0,1]         the on-channel weight
Eon   = sum_c u_c E_c
a     = sum_c u_c sum_t h x T / Eon      per-spike gain, fitted ON-channel only so an
                                         intruder's off-channel energy cannot inflate it
                                         and shrink its own off-residual
q_c   = sum_t h (x - a T)^2              per-channel residual power
onr   = sum_c u_c       q_c / (a^2 Eon)
offr  = sum_c (1 - u_c) q_c / (a^2 Eon)
```

Both dimensionless and gain-free; 1 means "residual as large as the template", the same
convention as `D`.  `u_c` *weights* rather than splits, so a flank channel at half the energy
feeds both terms in proportion.  `offr` is normalized by the ON-channel energy on purpose: it
asks how much signal there is where this cell has none, relative to this cell's own size, and so
does not blow up on a channel whose own template energy is near zero.

**The two terms are thresholded separately and the flagged sets union.**  Each gets its own
robust tail `median + k*1.4826*MAD` (`kOn`, `kOff`; 0 disables that term).  They are
complementary, not two views of one quantity.  Held out on that unit — template from half of the
curated cluster, scored on the other half against its siblings — AUC conditional on amplitude:
on-channel 0.887, off-channel 0.791, half-against-half sanity 0.504–0.511.  Per sibling they come
apart: one is a shape difference (on-channel 0.943), another is spatially displaced (off-channel
0.786 while amplitude alone is blind at 0.522).  Their **sum** scores 0.869, *below the
on-channel term alone* — summing dilutes whichever term carries the signal for that intruder.
Same statistic in PCA feature space (what `Data` holds, no waveform read): on 0.892, off 0.759,
sanity 0.504.

Spikes whose fitted gain falls below `minGain` carry no template-shaped signal, cannot be
normalized by `a^2`, and are flagged outright and counted apart rather than scored.  A channel
selection holding only the template's own channels leaves every `u_c` near 1 and the off-channel
term nothing to see; that is reported (`offBlind`) rather than returned as a silent zero.

A **sibling of Shift+X, not a replacement** — that gesture stays calibrated to fiber-kit's shed
knee (`FK_CONS_SHED_K`), so the two can be run on the same cluster and compared.  Dialog-driven
(five knobs, unit-dependent) and deliberately unbound; bind `action.stripResidualOutliers` in the
input registry if it earns a chord.  Non-mutating scan → confirm (default No) → one undoable
`createNewCluster` per product, the lasso's row-named builder.  "Keep the terms apart" makes up to
three products — on-channel only, off-channel only, both-or-no-gain — each its own undo step, so a
test run can judge the two terms separately.  The shed spikes land in a fresh cluster, not the
artefact bin: a self-outlier strip is a hypothesis to review.  Per-cluster medians and sited
thresholds go to the confirm dialog and to `qDebug` (`[residual-strip]`), because a flagged count
alone does not say whether the thresholds are anywhere near the data.

Caveat, stated because it decides how to read a run: those AUCs are *ranking* scores against
labelled siblings.  The action has no labels — it cuts each term's robust tail, so what it flags
is the tail of the unit's own distribution, which is the contamination only if the contamination
is a minority and far enough out.  Judge a run by inspecting the product.

The median-template construction is duplicated from `detectWaveformOutliers` rather than shared;
folding both onto one helper is the obvious follow-up, deliberately not done in the same patch
that introduces an unproven metric.

`klustersdoc.h` (`ResidualFlag`, `ResidualOutlierResult`, `detectResidualOutliers`),
`klustersdoc_strip.cpp`, `klusters.h`, `klusters.cpp` (`mStripResidualOutliers`,
`slotStripResidualOutliers`, `action.stripResidualOutliers`).

</details>

---

## 2026-11-12 — Klusters: a renumber-to-end (T) or a sort between an edit and its post-edit chain left the parked landing and the queued realign on the old ids -- a neighbouring cluster joined the selection

<details>
<summary>Details</summary>

Seen in a super-sampled session (`NS3_TRACE_SCOPE`): split 539 → 540, the landing parked as
{540, 539} and 540 queued for the auto-realign; then T renamed the source 539 → 541; the batch
ran, the auto-renumber compacted the ids and translated the parked landing through *its* map --
translating ids that a partial rename had already invalidated (`pending=539 540 539`, and
after the next split `pending=540 541 482`).  The landing selected whichever clusters held
those numbers: a neighbour of the old cluster.  `applyClusterRename` (renumber-to-end, the
sorts) translated the S-pins and the matrix scope through its partial map but not
`pendingParentSelection` nor `modifiedParents`; `renumberPendingParentSelection` now
translates both and dedups them (a rename can make two entries equal), and
`applyClusterRename` calls it.  Not a race: the chain after an edit is long in a `_U` session
(realign in `_U4`, the follow of every space), so a T lands inside it there.

</details>

---

## 2026-11-12 — Klusters: the Space box rescans the session's feature spaces when opened

<details>
<summary>Details</summary>

The *Space* box was filled when the session opened and after a build Klusters made itself; a
space written beside an open session by `ndm_extractspikes --supersample` / `ndm_pca` /
fiber-kit (`.fet.standard_U4.6`, `.spk.standard_U4.6`) did not show until a reopen.  Clicking
the box now rescans the directory (`populateFeatureSpaceCombo`, a listing and a few file sizes)
and preloads a space that is new.

</details>

---

## 2026-11-11 — Klusters: a super-sampled space is re-cut with the interpolator it was built with (detected from its rows), not the preference

<details>
<summary>Details</summary>

A `standard_U4` extracted by `ndm_extractspikes --supersample 4` (the `smooth` interpolator)
failed the realign's raw-source verification under a Klusters whose saved *Super-sampling →
Interpolation* preference was the cubic spline ("unexplained ... RMS difference up to 8.4%"):
the verification and every re-cut used the preference, and the saved setting outlives a
changed default.  Now `KlustersDoc::supersampleInterpFor(spk, method, factor)` re-cuts a
sample of an existing super-sampled `.spk`'s rows from the recording with each interpolator
(the preference first) and takes the one that reproduces them, cached per file; realign,
nudge, the follow and the sync re-cut with it, and the log says when it differs from the
preference.  The preference decides new spaces only.  A file no interpolator reproduces (a
`_U` space built before 0965's spline fix, another raw source) still fails the verification
with its diagnosis.  `test_supersample_interp_detect.py` (a `_U4` built with the exact spline,
realigned under the smoothing-spline preference: runs, the moved rows re-cut with the
spline); the other four self-tests pass; `doc/klusters/README.md`, the preference tooltip.

</details>

---

## 2026-11-11 — Klusters: a detached feature space (no basis -- `.fet.manifold`) no longer blocks the realign of the spaces it shares waveforms with; its time column follows, realign in it is refused

<details>
<summary>Details</summary>

Realign in `standard` on the 20120312 g6 session was refused: *"standard could not be
brought to the session's spike times: standard (it shares .spk.standard.6 with manifold):
not synced -- no readable basis .pca.manifold.6"*.  The sync a realign needs first groups
every usable space over the same `.spk`, and fiber-manifold's `.fet.manifold.6` -- a
per-spike table (snr, lag, position, ...), not a projection -- is listed as a space over the
standard waveforms (custody's fallback); with no basis the whole group was refused, and a
follow after any move left the group behind for the same reason.

- `FeatureSpace::projection` (false when no readable `.pca.<token>` resolves): a DETACHED
  space.  It is shown and cut like any space; in a sync (`syncOneSpace`) and in the follow
  (`followMovedSpikes`) its features stay and only its time (and phase) column is rewritten
  at the session's times; it never leads a group's re-cut (a projection member does; a group
  of detached spaces alone re-cuts nothing); it is never a reason to refuse.
- Realign and nudge are refused in a detached space ("not a projection of waveforms (no
  basis); show a waveform space first"); the *Space* tooltip says a space is detached.
- `RealignWorker` now emits its `logLine`s (the single-cluster job's log never reached the
  headless self-test's record).

`test_detached_space.py` (realign in `standard` with `.fet.manifold.1` beside it: runs,
the manifold features untouched, its time column the saved `.res`; a realign asked for in
`manifold` refused, nothing moved); the other three self-tests pass.  `doc/klusters/README.md`
"Detached feature spaces".

</details>

---

## 2026-11-11 — `ndm_pca` on super-sampled waveforms: the basis in ticks, the `.fet` with the phase column; a shared `.res` is never a `_U` tick `.res`

<details>
<summary>Details</summary>

Completes the ndm path of a super-sampled space (0965 wrote the waveforms and the tick
`.res`): `ndm_pca --method standard_U4` / `stderiv_C5_U4` fits the basis on the ticks
(`nSamples`, `peakSampleIndex`, `before`, `after` scaled by the factor; the `.pca` window in
ticks) and writes the `.fet.<token>_U4.N` with each spike's sub-sample **phase** as the last
feature column before the time -- Klusters' layout of a `_U` space, what its realign, nudge
and sync read and move -- taken from the group's tick `.res` (`process_fet_phase.py`, after
`process_mergefeatures`; the tick `.res` must exist).  So a session can be super-sampled
entirely outside Klusters (`ndm_extractspikes --supersample 4` for each method, then
`ndm_pca --method <method>_U4`), and Klusters shows the space as one it built.  A lag space
over super-sampled waveforms (`_U4_D34`) is refused by `ndm_pca` (the lag is in base-rate
samples).  `ndm_shared_res`: a `_U` preference asks for its base-rate token, and the scan
never returns a `_U` tick `.res` as the group's shared `.res` (it could, in a session whose
detect node names no method).  `ndm_test_extract_supersample` +12 (the PCA binaries passed
by ctest); `doc/ndmanager-plugins/commands/pca.md`.

</details>

---

## 2026-11-11 — Super-sampled extraction through the ndm engines; the smoothing spline; the `_U` stderiv from the interpolant's derivative; the spline's halved curvature fixed

<details>
<summary>Details</summary>

The `_U4` stderiv waveforms looked jagged in Klusters.  Three causes, all in the
super-sampled cut (`core/supersample.hpp`, `core/spike_extract.hpp`), fixed together:

- **The natural cubic spline used half its curvature.**  `naturalSplineCoeffs` solved
  Burden–Faires' tridiagonal system, whose unknowns are `c_j = M_j / 2`, and `splineAt`
  used them as the second derivatives `M_j` of the Hermite form: the curve between samples
  was a cross between the spline and the chord -- ~1 % off on a spike trough (max error 10
  of 1000 on a Gaussian of sd 2.5 samples; 0.24 now) and visibly kinked.  Every `_U` space
  built before this is affected; rebuild them.
- **The super-sampled stderiv took its temporal part as the difference of two int16-rounded
  ticks**: at ×4 a quarter of the base-rate difference with the same ±½ LSB rounding error
  on each operand, i.e. ~4× the relative rounding noise, and 4× smaller in amplitude than
  `stderiv_C5`.  Now the transform runs on the interpolant in double: the spatial derivative
  of the unrounded ticks, the temporal part the interpolant's **derivative per raw sample**
  (centred at the tick, base-rate amplitude scale: tick `4k` stands where sample `k`'s
  derivative stands), rounded once.  `supersampleChannel(..., deriv)` gives the derivative of
  each interpolator (the spline analytically, Lanczos by a central difference of the kernel).
- **A smoothing interpolator, the new default**: `Interp::SmoothSpline` = the natural cubic
  spline through Whittaker-smoothed samples (penalised second differences, `kSmoothLambda`
  0.15: half power at ~0.6 Nyquist, 9.6 kHz at 32 kHz; a 0.3 ms trough keeps > 99 % of its
  depth) -- the spike untouched, the sample-to-sample noise an exact interpolant reproduces
  between the samples not.  `spline` (exact) and `lanczos3` stay.  Klusters' *Super-sampling →
  Interpolation* preference gains it (default), and the saved preference now reaches the
  document from the start (it only reached it on a change in the dialog).

**Super-sampled extraction through the ndm engines** (`ndm_extractspikes --supersample 4
[--interp smooth|spline|lanczos3]`, `supersample` / `interp` on the node, or a `_U4` token in
`--method`, which `--allMethods` passes for an existing `_U` space): the raw window is cut
with a margin and interpolated on the raw `.fil` to `nSamples × 4` ticks *before* the
method's transform, so every derived `.spk` comes from the interpolated raw signal; written
under the `_U4` token (`standard_U4`, `stderiv_C5_U4`, the `.nst` in ticks), with the group's
tick `.res` (`.res.<resToken>_U4.N`, one per factor for all spaces) written once (phase 0)
and read for the phases by every later `_U4` extraction.  The engines' `-R` grows `-X factor
-I interp -K 0|1` (`extractspikes_reextract.h`; `-U` was taken by the stderiv engine's
dedup).  The post-extraction filter is not run on a super-sampled re-extraction.
`ndm_custody`: `ndm_supersampled_token`.  Klusters' space builder reuses a `.spk.<token>_U4.N`
that exists without its `.fet` (fits the basis and the `.fet` on it; size-checked) instead of
refusing.  `noise_stats.hpp`: a super-sampled spec's `.nst` counts ticks.

Tests: `core_test_supersample` +12 (the smoothing spline near the samples, twice as smooth,
closer to the truth; every interpolator's derivative; the ×4 stderiv on the base-rate scale,
≈ the centred base-rate difference, smooth; the spline now within 0.1 % between samples),
`ndm_test_extract_supersample` (the real engines through `ndm_extractspikes`: the files, the
tick `.res` written once and read back, the token form, the refusals, the exact spline
reproducing the samples), the Klusters self-tests pass `supersampleInterp: 0` (they compare
ticks to raw samples exactly).  Docs: `doc/ndmanager-plugins/commands/spike-detection.md`,
`doc/klusters/README.md`, the three engines' DocBook pages, `ndm_extractspikes`' header.

</details>

---

## 2026-11-11 — Klusters: the realign shift cap in a super-sampled space, phase-aware currency of spaces (realign-in-shown-space, step 3 of 3)

<details>
<summary>Details</summary>

- The realign preference's *Max shift* is in samples of the recording; in a super-sampled
  space the same cap now applies in that space's ticks (×2 / ×4) -- it was taken as ticks, a
  quarter of the intended range at ×4.  The automatic cap (half the peak position) was
  already in the space's ticks.  Tooltip says so.
- A space's currency (whether a realign's follow touches only the moved rows or the space
  gets a full sync; whether a working copy can be seeded by a plain copy) now includes the
  phases: a super-sampled space of the written space's factor must carry the same phases at
  the unmoved spikes, since a sub-sample realign can change a phase and no whole sample.
  `KlustersDoc::sharedTicksOnDisk(factor)`.
- `doc/klusters/README.md` "Keeping spaces in sync" rewritten for the shown-space realign.

Self-tests 3/3 on the built binary.  This closes the plan
`claude/klusters-realign-in-shown-space-plan-2026-11-11.md`.

</details>

---

## 2026-11-11 — Klusters: realign and nudge run in the shown feature space; the `.clu`'s own space follows (realign-in-shown-space, step 2 of 3)

<details>
<summary>Details</summary>

*Realign and nudge in the shown feature space; every other space follows* (the renamed
`realignFromAnySpace` preference, on by default): a realign, Align All or nudge now runs **in
the space shown** -- cross-correlation, PCA refinement and re-projection on that space's
waveforms and basis, the lags in that space's ticks -- and rewrites that space's `.spk/.fet`
working copies (`KlustersDoc::SpaceFiles`, seeded by `prepareShownSpaceForRealign` and
loaded before the job starts) and the shared `.res`.  Every other space follows when the job
is done, the `.clu`'s own included (0961's follow); no swap to the `.clu`'s space and back.
So with the `.clu` in `standard` and `standard_U4` shown, the alignment is sub-sample and the
phases land in the `_U4` column and the tick `.res`; with the `.clu` in `_U4` and `standard`
shown, it is a whole-sample alignment that keeps every phase.  Refused in a lag (`_D`) space
(show its waveform space).  The raw-source verification before a realign cuts a super-sampled
`.spk` as one (it compared raw frames against interpolated ticks and refused every `_U`
realign).  A Save As into another space refuses while that space holds an unsaved realign.
The realign journal records the space it ran in; undo (unused by the app) refuses otherwise.

Found by the self-test and fixed on the way: a Save As that adopts an existing space never
reopened the session there (`finishSave` reset the plan before the app read
`saveAdoptedSpace`), and a reopen that met a deferred close (a view job still running --
the common case right after a save) was dropped with a "reopen it yourself" message; the
reopen now waits for that close (`mReopenUrlAfterClose`).

Self-test: `test_realign_in_shown_space.py` (realign cluster 2 and nudge cluster 3 in
`standard_U4` with the `.clu` in `standard`: 233/300 spikes get a non-zero phase, the input
jitter's spread of 1.4 samples goes to <0.3 after the ×4 alignment, the `.clu` space follows,
`.res.standard_U4.1 == 4×.res + phase`) and `test_realign_base_from_supersampled_clu.py`
(Save As into `_U4`, a ×4 realign there, then a base-rate realign with `standard` shown: the
`_U4` origin follows, phases kept); `test_follow_supersampled.py` updated for the `_U4`-shown
case now aligning there.  Self-test steps `saveas` and a `realignDone` record; ctest hooks for
the three.  **Run here on the built binary: 3/3 PASS (60 checks).**  Preferences page text,
`doc/klusters/README.md`.

</details>

---

## 2026-11-11 — One tick `.res` per factor for all spaces, named after the shared `.res` (`.res.stderiv_S3_U4.6`); every `_U<f>` space carries the same phases

<details>
<summary>Details</summary>

0958 named the tick `.res` after each `_U` space (`.res.standard_U4.6`, `.res.stderiv_C5_U4.6`,
...).  Spike times are one thing for every space, so there is **one base-rate `.res` and one
tick `.res` per factor for the whole group**, the latter named after the former:
`.res.stderiv_S3.6` → `.res.stderiv_S3_U4.6` (`standard_U4` for an untagged `.res`).
`custody::supersampledRes(base, group, factor, preferred)` with the Python / bash / fiber-kit
mirrors (`supersampled_res`, `ndm_supersampled_res`; fiber-kit also `read_res_ticks`);
`custody_vectors.tsv` +7 rows of a new `supersampled_res` kind in all four conformance tests;
`formats/naming.md`.

Klusters: the super-sample build writes the tick `.res` only as the first space at its factor
and otherwise **takes its phases from it** (waveforms cut at them, phase column set) when it
agrees with the shared `.res`; every save rewrites it once per factor from a `_U<f>` space's
columns (`supersampledResPath(factor)` / `supersampledTicks(factor)`); the follow after a
realign in a super-sampled space hands that space's phases to every other `_U` file (scaled
to its factor), and a sync takes a `_U` target's phases from the tick `.res` when it agrees --
so all `_U<f>` spaces carry one set of sub-sample times.  `test_follow_supersampled` builds
×4 and ×2, checks exactly `rat.res.standard_U2.1` and `rat.res.standard_U4.1` exist and both
follow the moved `.res` (×2, ×4).  Run on the built binary: 32/32 (both cases).

</details>

---

## 2026-11-11 — Klusters: the working set of a feature space; the follow after a realign is per-file geometry and can follow the `.clu`'s own space (realign-in-shown-space, step 1 of 3)

<details>
<summary>Details</summary>

Groundwork for running realign and nudge *in the shown space* (plan:
`claude/klusters-realign-in-shown-space-plan-2026-11-11.md`).  No change to what the user
sees except one: a super-sampled `_U` space now **follows** a realign or nudge made in the
`.clu`'s space (it was left stale -- "a basis ... does not fit" -- because the follow sized
every waveform file by the shown space's geometry; it now works per waveform file at that
file's factor, re-cutting at each spike's own phase).

- `KlustersDoc::SpaceFiles` / `workingSetOf(method, error, seed)`: the committed
  `.spk/.fet/.pca` of a space and the working copies a realign in it would rewrite -- the
  pending triple for the `.clu`'s own space, the synced copies (`syncedPending`) for another,
  seeded on demand (a plain copy when the space's feature times are the session's, a full
  `syncOneSpace` when it is behind); a space over the `.clu`'s own waveforms shares the live
  pending `.spk`.
- `followMovedSpikes(log, writtenMethod)`: the space the realign ran in is a parameter (the
  `.clu`'s own by default); every other space follows, the `.clu`'s own included when it is
  not the one written -- its pending triple is patched in place and its held table with it,
  never deleted on a failure (it holds the unsaved realigns), and a `.clu` space left behind
  is reported rather than silently skipped.  Spaces are grouped per waveform file with that
  file's geometry (`nSamples × factor`, phases from the space's own column).

- The tick `.res` (0958) is written at every save for **every** `_U` space whose working
  copy this session synced or followed, not only the `.clu`'s own
  (`supersampledResPath(method)` / `supersampledTicks(method)`, from the space's own table).
- **Headless self-test harness**: `klusters_selftest.cpp` --
  `NS3_KLUSTERS_SELFTEST=<spec.json>` with `QT_QPA_PLATFORM=offscreen` drives the built
  binary through `show / realign / nudge / supersample / follow / save / dump` steps and writes
  a JSON report (a modal is closed with its default, never waited for); `test/selftest/`
  holds a synthetic-session generator (`session.py`), a runner and the first test,
  `test_follow_supersampled.py`: super-sample ×4, realign cluster 2 with `standard` shown and
  again with `standard_U4` shown (today's swap-and-return), Save -- the shared `.res` moved
  for cluster 2 only, the base `.spk` equals a fresh cut at the moved times, the `_U4`
  `.spk` followed (tick 4k == sample k for every spike, moved ones included), its `.fet` time
  column is the moved `.res` with every phase 0, its tick `.res` is 4× the moved `.res`, and the
  shown space is back. ctest `klusters_selftest_follow_supersampled` (skips without python3 /
  numpy / the built binary).  **Run here on the built binary: 26/26.**

</details>

---

## 2026-11-11 — Klusters: the token of a `.pending` working copy -- the shown `.spk` was read as untagged (super-sample built `_U4` over stderiv waveforms; `%spk` was empty; a `_U` session opened at the base geometry)

<details>
<summary>Details</summary>

The session's live `.spk` is its `.pending` working copy
(`<base>.spk.stderiv_C5.7.pending`), and `custody::methodOf` wants the group last, so on
that path it returns "" (contract: `klustersdoc_internal.h` `featureMethod` says exactly
this).  Three places and `Data` asked `methodOf` of the live path anyway: *Super-sample* on a
`.clu.stderiv_C5_D34.6.manifold` session derived `_U4` (0959 would have made it
`standard_U4` -- over stderiv waveforms, wrong); the plugin context's `%spk` was empty, so
`fiber-kit-manifold` got no space; and `Data::applyWaveformSpaceGeometry` took a `_U4`
session's pending `.spk` for factor 1.  `custody::stripWorkingSuffix / methodOfLive`
(`.pending`, `.pending.sync`, `.pending.tmp`, `.saving`; `core_test_supersample` +6),
`featureMethod` built on it, and the four sites use it: the super-sampled token of the shown
`stderiv_C5` space is `stderiv_C5_U4`, `%spk` is `stderiv_C5`, a `_U` session's geometry is
its own.  0959's empty-token default stays for a truly untagged `.spk`.

</details>

---

## 2026-11-11 — Klusters: super-sampling an untagged legacy session built a bare `_U4` token

<details>
<summary>Details</summary>

*Actions ▸ Super-sample ×4* on a session whose `.spk` is untagged (`<base>.spk.N`, method
token "") derived the token `_U4` and the re-extract refused it ("unsupported .spk method
'_U4'").  `custody::supersampledToken("")` now treats the empty token as the default method
(`standard_U4`; `core_test_supersample` +4), both Klusters call sites do the same before asking,
and the basis of an untagged session (`<base>.pca.N`) is found as the last resort when the
space is built.

</details>

---

## 2026-11-11 — Super-sampled spike times: `.res.<token>_U<f>.N` in ticks; the shared `.res` is always base-rate; Klusters exports a `_U` session at the recording's sampling

<details>
<summary>Details</summary>

**Contract** (`custody.hpp`, `ndm_resolve_io.py`, `ndm_custody`, fiber-kit `neuro_io` 0.66.7;
`custody_vectors.tsv` +19 rows, kind `base_rate_token` in all four conformance tests).  A `.res`
under a `_U<factor>` token holds the spike times in **ticks** -- `sample × factor + phase`, int64,
one per spike in the shared `.res`'s row order: the sub-sample spike times of a super-sampled
session.  `baseRateToken(m)` drops the `_U` (kind and lag kept: `stderiv_C5_U4_D34` →
`stderiv_C5_D34`); `resolve()` takes a `_U` method's `.res` at the base rate (its `.spk` stays its
own) and `resolveAny()` tries a `_U` preferred as its base-rate token and never scans to a `_U`
copy, so no consumer of the shared `.res` or `.spk` can pick up ticks or another geometry.
`doc/ndmanager-plugins/formats/naming.md`.

**Klusters.**  *Actions ▸ Super-sample* writes `.res.<token>_U<f>.N` (`time × factor`, phase 0)
beside the `.spk/.pca/.fet`; every save of a `_U` session rewrites it from the loaded times and
the phase column in the same transaction (`KlustersDoc::supersampledResPath / supersampledTicks`),
so it never disagrees with the shared `.res`.  *Actions ▸ Export at the Recording's Sampling…*
(`KlustersDoc::exportAtBaseRate`; enabled when the `.clu` itself is under a `_U` token, saved):
copies the saved `.clu` with its `.clc/.clp/.cli` under the base-rate token through `filecommit`,
beside the base-rate `.res` (the shared one, moved by whole samples) and the base space's
`.spk/.fet` (re-cut by the realign-from-any-space sync); the session stays open.  Row operations
(sort by time, sub-sessions) already carry every `.res.*.N` of the group, the tick res included;
fiber-kit's `read_res` resolves past it to the base-rate copy.  `doc/klusters/README.md`.  Not run
in the GUI.

</details>

---

## 2026-11-11 — Klusters: an atom's realign lands on the atom again -- the landing's parent selection no longer counts as a click

<details>
<summary>Details</summary>

Since 0952 (a parent clicked over selected children exits the child view), the landing after an
atom's realign (0954) was undone by its own first step: it selects the atom's parent in the parent
palette, and `ClusterPalette::selectItems` emits the same `updateShownClusters` a click does, so
`adoptCuratedParents` saw "children selected, a parent picked" and dropped the child view -- the
focus moved to the parent once the realign completed.  The palette now flags the one emission a
programmatic selection makes (`ClusterPalette::selectionIsProgrammatic`, true while the
`updateShownClusters` from `selectItems` is delivered), and only a user's selection exits the child
view; a landing or a rebuild's restore keeps it.  The comment in `slotUpdateShownClusters` claiming a
programmatic `selectItems` never reaches it was wrong and is corrected.  Not run in the GUI.

</details>

---

## 2026-11-11 — Klusters: the curation matrices follow the palette selection; a matrix lost off screen comes back

<details>
<summary>Details</summary>

**View ▸ Follow Selection in Matrices** (on by default; persisted as `matrixFollowSelection`).
A fiber picked in the parent palette, or an atom picked in the child palette, centres every
open curation matrix (error / template / residual / drift) on that cluster's diagonal cell:
the view pans so the cell is in the middle of the matrix area, the zoom is kept.  With
several selected it is the palette's current item (the one clicked or reached with the
keyboard) that is centred.  A matrix whose result is still on its way (or held back by a
hierarchy gesture) centres when the result lands; a cluster absent from the matrix shown (a
fiber picked while the atom matrix is up) waits for the next result -- the parent matrix
coming back, which then centres on it -- and lapses with that result otherwise.  A selection that
came from a matrix cell click does not move the matrix under the cursor
(`KlustersDoc::selectingFromMatrix`).

Independently of the mode, a matrix panned completely off screen (nothing of the grid in
view, after a drag, a resize or a scope swap restoring an old pan) is brought back to its
upper-left corner at the current zoom, and the cross-connected matrices follow.

`MatrixViewport::centerOnDiagonal / gridOutOfView / bringBackIfLost` (unit test
`klusters_test_matrixviewport`), `ErrorMatrixView::followCluster` and the three siblings,
`KlustersView::followMatrixSelection`, `KlustersApp::followMatrixSelection` from
`slotUpdateShownClusters` / `slotChildSelectionChanged`, `ClusterPalette::currentClusterId`.
`doc/klusters/README.md`.  Not run in the GUI.

</details>

---

## 2026-11-11 — Klusters: a delete at the end of the child list lands on the new last row, in list order

<details>
<summary>Details</summary>

`dropChildrenToBin` chose the landing sibling by id: the next id above the lowest dropped
atom, else the highest id.  Under a child-scope sort the palette lists atoms in the held
order, so "next" by id could be anywhere in the list.  The landing now follows the listed
order (the held `scopeSortOrder` when there is one, else ids): the next surviving row after
the anchor, or -- when the dropped atom was the last row -- the row before it, the new last.
`ClusterPalette::selectItems` scrolls the current item into view, so a landing at the end of
a long list is visible.  `doc/klusters/hierarchical-clustering.md`.  Not run in the GUI.

</details>

## 2026-11-11 — Klusters: an atom's realign lands back on the atom

<details>
<summary>Details</summary>

*Realign* with the child view showing takes the first shown ATOM (the doc resolves the id in
the active layer), but its completion (`applyRealignResult`) treated the id as a parent: it
selected the atom's number in the parent palette (whichever parent carried it, or nothing),
moved the keyboard focus there and the views to the parent layer, and handed the parent-keyed
extrema pass (`refeaturizeRealignedClusters`) the atom's number.  `startRealignForCluster`
now records that the target is an atom and its parent (`realignTargetIsChild`,
`realignTargetParent`); on success the extrema pass takes the parent, the matrices are told
about the atom and the parent, and the landing stays in the child view: the parent selected
silently in the parent palette, the atom selected in the child palette with the focus, the
child scope kept.  `doc/klusters/hierarchical-clustering.md`.  Not run in the GUI.

</details>

## 2026-11-11 — Klusters: one curation-matrix refresh per hierarchy gesture (no child -> fiber -> child flash)

<details>
<summary>Details</summary>

Dropping an atom (or merging, promoting, dissolving, undoing) launched the four curation
matrices three times: the parent-layer move (`updateSimilarityMatrices` with the scope as
it was), the hierarchy rebuild (`resolveMatrixScope` → `matrixScopeChanged`) and the deferred
consolidation (`autoPostClusterEdit`'s renumber + `slotUpdateErrorMatrix`), the views swapping
scope state in between -- the child → fiber → child flash.  A **matrix hold**
(`KlustersDoc::beginMatrixHold` / `endMatrixHold`) now spans each gesture: while held,
`updateSimilarityMatrices` and the views' `update*Matrix` only note the request and
`resolveMatrixScope` keeps its signal; the release emits the held scope change once (the
app's deferred recompute) or runs one refresh.  `KlustersApp::holdMatricesForGesture` opens
it with the busy cursor and queues the release on a zero-timer AFTER the gesture's own
consolidation timer, so renumber and landing happen inside the hold.  Applied to drop to
noise, merge / promote / group children, merge / dissolve / break up parents, atom undo and
redo.  Not run in the GUI.

</details>

## 2026-11-11 — Klusters: a fiber clicked over an atom selection leaves the child view

<details>
<summary>Details</summary>

With atoms selected in the child palette, clicking a fiber in the fiber palette used to
keep the child view: the rebuild restored the atom selection, and with the same fiber the
V scope stayed on, so the matrices and the views kept showing the atoms.  Now that click is
the user leaving the atoms: `adoptCuratedParents` turns the matrix scope off (the curation
matrices return to the fiber scope) and marks the child selection to be dropped
(`dropChildSelectionOnRebuild`), which `repopulateChildPalette` honours instead of
restoring it; the clicked fiber holds the selection.  Without an atom selection nothing
changes: the same fiber keeps the child view, another fiber drops it as before.
`doc/klusters/hierarchical-clustering.md`.  Not run in the GUI.

</details>

## 2026-11-11 — Klusters: the super-sample MODE builds the space on open; Save As adopts an existing space; `%spk`

<details>
<summary>Details</summary>

The gate becomes a **mode** (*Preferences → Realignment → Super-sampling*: off / ×2 / ×4).
In a mode, every session that opens gets the super-sampled space over its `.clu`'s space
built when its files are missing and shown (`KlustersApp::applySupersampleMode`,
`buildAndShowSupersampledSpace`, shared with the actions); with *Also save the clustering
under the super-sampled token* it is saved as `<base>.clu.<token>_U4.N` and reopened there.
A super-sampled `.clu` opened while its `.spk` is missing is opened through the base space
(its `.clu` written from this one if absent), the space rebuilt, the clustering saved back
under the asked-for name and reopened.  **Save As into a method whose space exists now
ADOPTS it** (`KlustersDoc::saveDocument`, `SavePlan::adoptedSpace`): the `.clu` moves, the
space's files stay -- before, the origin's pending `.spk/.res/.fet` were staged over the
target's and a different geometry failed the size check, so the Save As offered by 0950
could not succeed; an unsaved realign in the origin refuses the Save As; the app reopens
the session after it.  The plugin context gains **`%spk`** (the shown `.spk`'s method token)
and `%supersample`, so an engine that fits waveforms follows the space Klusters is in
(fiber-kit 0.66.6: `FK_MANIFOLD_SPK` defaults to `%spk`); `%nsamp` stays the recording's
nSamples whatever space is shown (an engine's `--nsamp` names the raw window), `%ticks` is the
shown space's.  README "Super-sample mode",
`PLUGIN_API.md`.  Compile-checked; not run in the GUI.

</details>

## 2026-11-11 — Klusters: super-sample mode -- waveform spaces at 2x / 4x the sampling, realign and nudge at sub-sample resolution

<details>
<summary>Details</summary>

For high-end machines: **Actions → Super-sample ×2 / ×4…** (gated by *Preferences →
Realignment → Super-sampling*, off by default, with the interpolator: cubic spline or
Lanczos-3) builds over the shown space `<base>.spk.<token>_U<f>.N` (every spike re-cut
from the recording and interpolated, `KlustersDoc::buildSupersampledSpace` /
`reextractAllSpikesFromFil` through the kernel; from the stored windows without a
recording), a basis fitted on it (`pca_fit.hpp`; the shown basis's window and component
count scaled) and `.fet.<token>_U<f>.N` (projections, a **phase** column, time), then shows
it and offers *Save As* under the token so realign and nudge run in it once reopened.

The waveform geometry is now per space: `Data::applyWaveformSpaceGeometry` scales the
group's `nSamples` / peak by the `.spk` token's `_U<factor>` at open and on every switch
(`replaceFeatureSpace`, `availableFeatureSpaces` size check), the waveform view takes it
again on a switch, and `spikeCutSpecFor` / sync cut each target at the recording's sampling
times its own factor.  In a super-sampled `.clu` space a realignment's lag is in ticks: the
`.res` moves by whole samples and the rest becomes the spike's phase (the last feature
column), the window is re-cut through the extraction kernel at that fractional time
(`recutWindowCM`), the stderiv transform in the super-sampled domain, the phase column
updated; a nudge keeps the phase; a sync re-cuts at the phases.  `doc/klusters/README.md`
"Super-sample mode".  Compile-checked; **not run in the GUI**.

</details>

## 2026-11-11 — core: the `_U<factor>` super-sampled token, `supersample.hpp`, a super-sampled cut, `pca_fit.hpp`

<details>
<summary>Details</summary>

The ground for Klusters' super-sample mode (next entry), all Qt-free and in
`libneurosuite-core`:

- **Token grammar**: `<family>[_<kind><order>][_U<factor>][_D<lag><dims>]`.  `_U<factor>`
  (one digit, 2-9) marks a super-sampled waveform space: `factor` ticks per acquisition
  sample, so its `.spk` holds `factor × nSamples` samples per waveform with the peak at
  0-based tick `(peakSampleIndex − 1) × factor`.  It belongs to the waveform token (`waveformToken` keeps it:
  `stderiv_C5_U4_D34` → `stderiv_C5_U4`); `supersampleOf` gives the factor, `supersampledToken`
  builds the token.  Repeated, `_U0`, `_U1` or two digits: opaque.  The four parsers --
  `custody.hpp`, `ndm_resolve_io.py` (`supersample_of`), `ndm_custody` (`ndm_supersample_of`,
  `NDM_M_UP`) and fiber-kit's `neuro_io` (1058) -- against 19 new rows of `custody_vectors.tsv`
  (`supersample_of`, `method_token`, `waveform_token` kinds).
- **`supersample.hpp`**: natural cubic spline (default) and Lanczos-3 interpolation of a raw
  window with a margin of raw samples on either side (`kSupersampleMargin` 3), per-spike phase in
  ticks, int16 rounding with saturation, a stored-window variant (no margin), `splitTicks`.
- **`spike_extract.hpp`**: `SpikeWindowSpec::supersample`, `interp`, `phaseTicks`: the cut takes
  the margins the recording holds, interpolates each channel to `nSamp × factor` ticks (tick k at
  raw time `start + (k + phase) / factor`) and emits `nChan × nSamp × factor` entries; the stderiv
  transform runs in the super-sampled domain, seeded from the tick before the window.
- **`pca_fit.hpp`**: `fitPca` -- per-channel means and leading eigenvectors by cyclic Jacobi, into
  a `PcaBasis` that `writePca` stores and every PCAE reader projects like one from `process_pca`.
- `core_test_supersample` (24 checks); `CORE_API.md` §3.1, §3.9, §3.9a, §3.9b; `naming.md`.

</details>

## 2026-11-10 — Klusters: the Refine mode -- a plugin's children under the same parents

<details>
<summary>Details</summary>

A recluster engine replaces the selection with new clusters, renumbered above the highest id -- the
right thing for a re-sort, the wrong thing for fiber-kit's `fiber-manifold`, which returns the unit as
it was with its children redrawn: cluster 5 would have come back as cluster 13717.  A plugin now
declares `<integration>refine</integration>` and Klusters runs it as a REFINE: on the atom layer
whatever the scope (from the parent scope the selected parents become their atoms), the seed `.clu`
carrying the parent ids and a seed `.clc`/`.clp` the atoms, and the engine's final `.clc` staged as the
`.clu` the atom-layer recluster integrates -- the children land under the parents they belong to,
one child-layer undo step, the parents' ids untouched, the manifold feature space carried back as
before.  The wheel's *Recluster ▸ Plugin* ring gains **Refine** beside KlustaKwik and fiber-kit
(*Actions ▸ Recluster ▸ Engine ▸ Refine: fiber-kit manifold*, `recluster.engine.manifold`; enabled
once `fk install klusters-plugin` has put the descriptor in place); *Preferences ▸ Reclustering*
names a refine engine as such.  A refine on a flat session says it needs a hierarchical one.  The
sub-session's name lists at most 12 ids (`-and<N>more`), since a refine of a large unit selects
hundreds of atoms.  `KlustersPlugin::refines`, `createReclusterSubsession(..., refine)`,
`KlustersDoc::stageRefinedChildren`; `PLUGIN_API.md` "Result integration".  Needs fiber-kit 0.66.3
(the descriptor declares `refine`).  Not run in the GUI.

</details>

## 2026-11-10 — Klusters: a plugin descriptor that does not parse says so

<details>
<summary>Details</summary>

A descriptor that is not well-formed XML (fiber-kit's shipped `fiber-kit-recluster.xml` and
`fiber-kit-manifold.xml` carried a `--` inside their header comment, which XML forbids, so neither
loaded) used to vanish without a word: *Plugins* showed "(no plugins found)" and nothing said why.
Now `PluginRegistry::parseDescriptor` records the parser's message with its line and column (or the
missing program `<n>`) in `KlustersPlugin::error`, `reload()` keeps the failures in `broken()` and
warns on the console, the *Plugins* menu lists each as a disabled `<file> — not loaded` entry (the
message in the tooltip and status tip), *Preferences → Plugins* lists them with the message, and
*Reload Plugins* reports "N loaded, M NOT loaded -- file: message".  `pluginregistry_test` gains 3
checks (25).  The descriptors themselves are fixed in fiber-kit 0.66.2.

</details>

## 2026-11-10 — Klusters: feature spaces an engine writes in the sub-session come back as session-level spaces

<details>
<summary>Details</summary>

A recluster plugin that writes a feature file of its own in the sub-session
(`<sub>.fet.<method>.<g>` -- fiber-kit's `fiber-manifold` stage writes
`.fet.manifold.<g>`: per-spike field lag, position, decay, amplitude, SNR,
window, class, ISI flag) now leaves it in the session: after the engine's
`.clu` is integrated, every sub-session `.fet` variant that is neither one
Klusters copied in nor its own `.fet.klusters.<g>` is scattered into
`<base>.fet.<method>.<g>` -- the sub-session's rows at their spikes, zeros
(with the time column) elsewhere, an existing file of the same shape kept so
successive refinements of different units accumulate in one space -- and the
*Space* selector is refreshed, so the new space can be opened at once.
Parent- and child-scope runs alike; the children are integrated after, as
before.  `subsession::scatterFetRows`, `KlustersDoc::carryBackSubsessionFeatures`
(`PLUGIN_API.md` §3b); `subsessionfiles_test` gains 4 checks (25).

</details>

## 2026-11-09 — ndm_extractspikes / ndm_detectspikes `--group N[,M...]`

<details>
<summary>Details</summary>

`ndm_extractspikes session.yaml --group 6` (also `-g`, `--group=6,7`,
repeatable) re-extracts only the named spike groups and does so whether or
not their `.spk` exists -- a group named by hand is meant to be redone,
typically after its `.res` changed (a realign committed from Klusters).  The
other groups' files are untouched, the session-file check (`--check`) covers
only those groups, and a group number outside the session, a non-number or a
group with no channels is an error before any engine call.  `ndm_detectspikes
--group` does the same for detection.  Before, the only way to redo one group
was `--force` on all of them (or deleting its `.spk`).  The engines' `-R` was
never a user flag; `ndm_extractspikes -R 6` now fails with a usage line that
names `--group`.  New test `test_extract_group.sh` (18 checks).

</details>

## 2026-11-09 — Klusters: every sort of the wheel works at the atom level

<details>
<summary>Details</summary>

The four sorts that were refused under the child scope ("this sort renumbers
parents") -- *Sort by Error p-value*, *Sort by Residual (gated)*, *Waveform
NN* and *Waveform Spectral*, all in the right-click wheel's *Sort* ring --
now sort the scope's atoms there like the others: display-only through the
held order (matrices and child palette), dropped at the scope's exits.  They
take their cluster set and layer from `clustersToSort` / `sortMetricData`
(the error and residual matrices' rows are the atoms under the scope); at the
fiber level they renumber as before.  Not run in the GUI.

</details>

## 2026-11-09 — Klusters: sorting the curation matrices and the child palette at the atom level

<details>
<summary>Details</summary>

Under the child scope (`V`), a sort -- *Sort Clusters* by any metric, or
*Reorder by Similarity* (`Shift+S`) from the error OR the template matrix --
now rearranges **all four curation matrices** (error, template, residual,
drift) and **the child palette** to the same atom order.  Display only: no
atom is renumbered.  The document holds the sorted atom ids
(`KlustersDoc::scopeSortOrderList`, signal `scopeSortOrderChanged`); each
matrix derives its row/column permutation from them through the shared
`MatrixDisplayOrder` (`matrixdisplayorder.h`: specials at the front, held ids
in order, unnamed rows appended) on the signal and after every accepted
recompute, and the child palette lists in the same order on rebuild.  The
order lasts while the child view does and is dropped at its exits (`V`, or
another curated parent), so coming back shows the natural id order.  Before,
only the error matrix followed a child-scope sort, a `Shift+S` sourced from
the template matrix was refused there, and the child palette never moved.
New test: `klusters_test_matrixdisplayorder` (23 checks).  Not run in the GUI.

</details>

## 2026-11-07 — Klusters: the Reclustering preferences page around the engine

<details>
<summary>Details</summary>

*Preferences → Curation → Reclustering* now opens with the **engine** --
KlustaKwik, fiber-kit, or any installed `recluster` plugin (the same setting as
*Actions → Recluster → Engine* and the wheel's Plugin ring; Apply re-checks the
menu action) -- a summary of what the chosen engine runs (a plugin's command
and its current parameter values, stored or descriptor defaults), and
*Parameters…*, which jumps to the plugin's page under *Preferences → Plugins*.
The KlustaKwik settings follow as that engine's group, marked inactive when
another engine is active; a fiber-kit group holds the fallback command only
while fiber-kit's descriptor is not installed (with it, a note names the
installed `fiber-kit-klusters.yaml` the plugin runs).  *Default* returns the
engine to KlustaKwik.  Not run in the GUI.

</details>

## 2026-11-07 — Klusters: %tokens in plugin parameter values

<details>
<summary>Details</summary>

A `%token` inside a plugin parameter's value is substituted from the run's
context like the `<command>` template's (`PluginRegistry::paramArgs`), so a
descriptor default of `%variant` means "the open clustering's variant" --
fiber-kit's `method` parameter (fiber-kit 0.65.0), whose plugin now runs with
the session-agnostic `fiber-kit-klusters.yaml` installed beside its descriptor
(README "Recluster with fiber-kit", PLUGIN_API §3b).  pluginregistry test: 22.

</details>

## 2026-11-07 — Klusters: the active recluster engine, and the radial menu's Recluster ▸ Plugin ring

<details>
<summary>Details</summary>

- **An active recluster ENGINE** (setting `reclusterEngine`: `klustakwik` |
  `fiber-kit` | `plugin:<name>`), chosen in *Actions ▸ Recluster ▸ Engine* -- a
  checkable, exclusive group: KlustaKwik and fiber-kit built in, one entry per
  installed `recluster` descriptor (rebuilt on *Reload Plugins*; a vanished
  plugin falls back to KlustaKwik).  **Recluster (`Shift+R`) runs the active
  engine**: KlustaKwik as before, fiber-kit through the sub-session (the
  `fiber-kit-recluster` plugin's Preferences defaults, else the preferences'
  command), a plugin through its descriptor with its Preferences defaults.
  `Shift+M` / `Shift+C` stay KlustaKwik on other features; `Shift+Q` is
  fiber-kit explicitly.  The status bar names the engine on a switch, and the
  Recluster action's status tip carries it.
- **The radial wheel**: the shipped Recluster group holds the Recluster action
  and a **Plugin** ring with the two built-in engine entries
  (`recluster.engine.klustakwik`, `recluster.engine.fiberkit`); the active one
  shows the wheel's checked dot, and a flick switches it.  Plugin engine entries
  are placeable from the catalogue (*Actions ▸ Recluster ▸ Engine*).  The
  Plugins menu's actions carry the bare plugin name (kind in the tips), so their
  ids are `menu:Plugins/<name>`; *Reload Plugins* rebuilds the wheel, which held
  the menu's actions.  A stored layout is untouched.  radiallayout_test updated.

</details>

## 2026-11-07 — Klusters: recluster plugins on the sub-session, typed descriptors, Preferences → Plugins

<details>
<summary>Details</summary>

The sub-session (the temp-file method of the previous entry) becomes the plugin
contract for any sorter, and plugin configuration moves into Preferences.

- **`recluster-integrate` is wired** (PLUGIN_API phase 3): a descriptor of kind
  `recluster` (or `<input>subsession</input>`) runs on the selection's sub-session
  through the recluster path and its `%final` `.clu` is integrated as a KlustaKwik
  result (`KlustersApp::startSubsessionRecluster`).  Descriptors gain a
  **`<command>` template** (`%base %group %final %selection %params …`) beside the
  positional `<consumes>`, and **typed parameters** (`<type>` bool / int / real /
  path / choice / text, `<min>` `<max>` `<step>` `<choices>` `<help>`; inferred from
  the default otherwise).  A bool is passed as a bare flag.
- **Preferences → Plugins**: a group with one page per installed descriptor,
  generated from it — kind, what it runs on, command, path, and the parameter
  defaults the run dialog opens with (`PluginSettings`, stored per plugin as
  `Plugins/<name>/<parameter>`, only values that differ from the descriptor;
  *Reset to descriptor defaults*).  The run dialog uses the same typed editors
  (`PluginParamForm`), prefilled document value > stored default > descriptor, with
  *Remember these values as the plugin's defaults*.  *Reload Plugins* rebuilds the
  pages.
- `Shift+Q` runs the `fiber-kit-recluster` descriptor (fiber-kit 0.64.0,
  `fk install klusters-plugin`) with the Preferences defaults when it is installed,
  else the `fiberKitReclusterCommand` setting as before.
- `klusters_test_pluginregistry`: 21 checks (typed parse, inference, template
  argv, the defaults store).  Not run in the GUI.

</details>

## 2026-11-07 — Klusters: Recluster with fiber-kit — a plan on the selected clusters

<details>
<summary>Details</summary>

**Actions → Recluster → Recluster with fiber-kit** (`Shift+Q`) runs a whole
fiber-kit pipeline on the selected clusters -- or, from the child palette, on
the selected atoms -- and replaces them with the clusters it finds, as a
recluster does (one undo step, in the layer the selection came from).

- Klusters writes the selection as a **sub-session** (`KlustersDoc::
  createReclusterSubsession`, `subsessionfiles.{h,cpp}`): the selected spikes'
  rows of every `.res`/`.spk`/`.fet` of the group in time order, a seed `.clu`
  with their current ids, copies of `.yaml`/`.xml`, symlinks to `.fil`/`.dat`
  and the group's `.nst`/`.pca`/`.eap`/`.tcl`.  `Data::createFeatureFileSorted`
  fills the recluster table in the same order, so the engine's final `.clu`
  maps back by position through the existing `integrateReclusteredClusters`.
- It runs the command of the new setting `fiberKitReclusterCommand` (default
  `fk run %electrodeGroupID --session %fileBaseName --final-clu
  %fileBaseName.clu.klusters.%electrodeGroupID`, fiber-kit 0.63.0), so the plan
  and knobs are the session folder's `fiber-kit.yaml`.  Every temporary file,
  including the stages' outputs, is removed when the run ends.
- A hierarchical plan's atoms come back too (`integrateReclusteredChildren`):
  in a parent-scope recluster the final `.clc` (copied beside the `.clu` by
  `--final-clu`) becomes the new parents' atoms, renumbered above the atom
  layer's highest id; an atom-scope recluster takes the `.clu`'s units as atoms.
  The sub-session's realigned times are not written back -- the new clusters go
  through Klusters' own post-recluster realign.
- The plugin runner passes the child palette's atoms to a descriptor with
  `<selection>children</selection>` (it passed the parent palette's clusters).
- `klusters_test_subsessionfiles`: 19 checks (group files, record subsets, the
  seed .clu written and read back, removal).  The fiber-kit side (`fk subsession`, `--session`,
  `--final-clu`) is tested there on a synthetic session.  Not run in the GUI.

</details>

## 2026-11-07 — Klusters: leaving the child palette for the parent palette clears the atom selection

<details>
<summary>Details</summary>

`Shift+Tab` from the child palette to the main palette (or `Tab` wrapping
round) used to move only the keyboard focus: the selected atoms stayed
selected, so the views kept showing them (a child selection *is* the shown
clustering), the curation matrices stayed child-scoped, and the parent
palette's arrow keys drove a selection the views were not showing.  The
focus move now deselects the atoms and re-drives the views from the fiber
selection, takes the matrices back to the parent level as `V` does (with the
status-bar note), and gives the main palette the list focus.  The template
palette is no longer a stop in the `Tab` ring (parent palette, child palette,
toolbar fields); click it to focus it.  Not run in the GUI.

</details>

## 2026-11-07 — Klusters: the child palette follows a parent-level landing

<details>
<summary>Details</summary>

Found while reading for the previous entry; confirmed in the code, not run in
the GUI.

- **The child palette follows the landing.** After a parent split, recluster,
  watershed or merge the parent palette landed on the new clusters, but the
  child palette kept listing the children of the *previous* curated parent --
  or nothing, when the operation had consumed it.  The landing is applied
  with the palette's silent `selectItems`, which never reaches
  `slotUpdateShownClusters`, the only place a parent selection adopted the
  curated parent.  That adoption is now a helper (`adoptCuratedParents`)
  called from both; when the curated slot names a parent that no longer
  exists, the first landed parent takes it even under a joint selection.
- **`deleteClusters` in child scope delegates to `dropChildrenToBin`.**  Its
  own child branch relabelled the atoms to atom id 1 *inside* their parent and
  moved no spike in the parent layer -- a delete in neither layer.  The UI no
  longer reached it; the branch is gone.
- **Drop Child to Noise is one gesture** for all the selected atoms (one undo
  step per parent), not one per atom.
- **Multi-basin watershed and Split by Lassos pin the layer** they were
  approved on for every carve, instead of reading it from the child palette
  mid-loop while the rebuilds of each carve repopulate that palette.
- An unused variable in `decollideLassoAgainstBasis` (the one `-Wall` warning).

</details>

## 2026-11-07 — Klusters: child-level delete and watershed, and where the selection lands after a split

<details>
<summary>Details</summary>

Curating the atom layer, the five gestures a curator makes most, made to
land where the next gesture starts.

- **Delete at the child level.** `Delete` / `Shift+Delete` with the child
  palette focused now drop the selected atoms to noise / artefact
  (`KlustersDoc::dropChildrenToBin`, one gesture; *Drop Child to Noise* is
  the same operation).  The atoms are reclassified: their spikes leave the
  parent in the parent layer and keep their atom id, as a child-primary
  session defines a delete.  The selection lands on the next sibling (the
  one above the lowest deleted id, else the one below); when the last atom
  went, the parent is gone with it and the selection lands on the next
  parent, which the curated parent follows.  Before, `Delete` with the child
  palette focused deleted the PARENT palette's selection, and did so in the
  child scope, where `deleteClusters` looked those parent numerals up in the
  atom id space; the parent delete now forces the parent scope.
- **The deferred parent landing no longer steals the child palette.**
  `applyPendingParentSelection` -- the settled point after the automatic
  realign / renumber -- pulled the views and the keyboard focus up to the
  parent after every child drop, so arrow-key navigation in the child
  palette was broken after each one.  An operation that parked a child
  landing now keeps the child scope there.
- **Watershed on the t-SNE embedding works on atoms.**  The refusal is
  lifted: the basins are committed through the atom layer's own split path,
  restricted to the selected atoms' spikes so no basin can straddle parents.
  The embedding survives its own commit (recoloured in place instead of
  dropped on the first carve), and the selection lands on every new piece --
  each basin's split used to park only its own result, so the last one won.
- **Child recluster lands on the new atoms.**  The landing is parked for the
  child palette (the same fix the child split needed), and the palette is
  rebuilt for the restored parent after the recluster-output tab deselected
  it.  Parent watershed lands on all its new clusters, applied at the settled
  point like the recluster's, and its new clusters are queued for the post-op
  realign.
- **Split by Lassos makes the remainder a new cluster too.**  What the lassos
  left in a source is one more piece: it is carved out under a new id (log
  algorithm `manual_multi_lasso_rest`) and the original id goes away, so
  every piece goes through the same new-cluster path and the selection ends
  on all of them.  The noise and artefact bins and the reserved chunk
  clusters survive their lassos as before.

Nine translation units compile warning-free under `-Wall` against Qt 6.4.
Not run in the GUI.

</details>

## 2026-11-07 — Klusters curation log: the hierarchy commands and the atom layer

<details>
<summary>Details</summary>

The second half of the curation-log audit.  The log recorded the parent layer
only: every child-scope edit wrote nothing, and the hierarchy commands
appeared as the parent primitives they are built from (one `MOVE_SPIKES` per
atom, one `GROUP` per merge) with nothing tying the fragments to the command
or recording the template scores it used.

- **`layer` on every snapshot line** (`parent` / `child`).  Child ids collide
  with parent numerals; readers key on (`layer`, `cluster`).
- **Child-scope edits are logged**: split, `SPLIT_N`, watershed, delete and
  recluster of atoms, and `mergeChildren` (`HIERARCHY_EDIT`), with snapshots
  computed on the atom layer.  They are not undo-paired (the atom stack is
  invisible to the logger's walk), so their status stays `good`; an atom-layer
  undo / redo is logged as its own `UNDO` / `REDO` detail with the edit's ids.
- **`GESTURE` entries** for `promoteChildren`, `dropChildToNoise`,
  `dissolveParent`, `breakUpParents`, `mergeOrphanParents`,
  `mergeOrphanChildren`, `collapseToSelfChildren`, `flattenHierarchyToClu`,
  `compactAllClusterIds` and `promoteToHierarchy`: the plan (for the template
  commands, every candidate with its correlation and target) as the first
  detail, the outcome as the last; every primitive run inside carries the
  gesture's index in `gesture_idx`.  Gestures nest.
- The ring trim never flushes an open gesture (found by the new test: with a
  small ring the outer gesture of a Break Up was written before its end).

`klusters_test_curationlogger`: 32 checks.  Eight translation units compile
warning-free under `-Wall -O2` against Qt 6.4.  Not run in the GUI.

</details>

## 2026-11-07 — Klusters curation log: checkpoint on save, refractory and cell-type statistics, pair evidence

<details>
<summary>Details</summary>

An audit of the curation log (`CurationLogger`, `Data::computeSnapshot`, every
mutation path in `KlustersDoc`) found three holes and a set of missing
statistics.  Fixed here:

- **Nothing was written before close.**  The log is a ring flushed at overflow
  or document close, so a crash lost the whole session's decisions (and realign
  has crashed).  Every Save now writes a *checkpoint*: each buffered entry with
  its current status and `"final": false`, then a `CHECKPOINT` event; the ring is
  kept so a later undo still flips the entry, which is then written again with
  `"final": true` when it leaves the ring.  Readers keep the last line per
  (`action_idx`, `phase`, `cluster`).
- **Two paths opened a log entry and never closed it.**  `createNewCluster` and
  `createNewClusters` with a selection that yields no cluster ran `logBefore`
  and returned; the open entry then swallowed the NEXT action's after-snapshots
  (the `logActionOpen` guard).  Both now close the entry as not undoable.
- **`SESSION_OPEN` could not be tied to a `.clu`.**  It now records `method`,
  `stage`, `clu_file`, `n_spikes`, `n_clusters`, `hierarchical` and
  `klusters_version`.
- **A `QVariantList` detail was written as `""`** (`toString()` of a list);
  it is a JSON array now.
- **Statistics the decision needs that the snapshot lacked:**
  `refrac_ratio` / `refrac_expected` (pairs 0.6–1.5 ms apart over chance, with
  power -- a violation percent means nothing alike for a 10 Hz and a 0.5 Hz
  cell), `burst_index` (ACG 3–10 ms over 40–50 ms), `occupied_hz` (rate over the
  bins the cluster occupies, not first-to-last), `waveform_dom_chan`,
  `waveform_width_ms` (trough-to-peak, polarity-aware; `waveform_width_samp`
  was sign-blind) and `waveform_chan_amp` (the per-channel profile fiber-cpos
  localises from).
- **GROUP gets pair evidence.**  One `{"detail": "pair"}` record per pair of
  sources, before the merge: `cross_refrac_ratio` / `_expected`,
  `template_cos` (cached means, ±4 samples) and `time_overlap` -- the decision
  variables two single-cluster snapshots cannot give back.

Documented as not logged (doc/klusters/README.md, "What is not logged"): every
child-scope edit, the hierarchy commands as gestures (they appear as their
parent-layer primitives), the waveform section of realign entries when the
cache is cold, and the full time sort.

New `klusters_test_curationlogger` (QtCore; 24 checks; fails to compile
against the previous logger).  `data.cpp`, `klustersdoc_edit.cpp`,
`klustersdoc_io.cpp`, `curationlogger.cpp` compile warning-free under
`-Wall -O2` against Qt 6.4.

</details>

## 2026-11-06 — ndm_extractspikes reads the .res ndm_detectspikes writes

<details>
<summary>Details</summary>

`ndm_extractspikes` took a group's spike times from whichever `.res` the
resolver found first: its own method's tag, `standard`, `stderiv`, `sdiff`,
then any other `SESSION.res.<token>.N` in byte order.  A session with
`ndm_extractemg`'s EMG events `SESSION.res.emg.N` and spike times under
`.res.stderiv_S3.N` extracted waveforms at the 68 EMG times (`emg` sorts
before `stderiv_S3`), without a word.  A leftover `.res` tagged with the
extraction method won the same way over the one detection wrote.

Now the `.res` is named from the session file: `SESSION.res.<M>.N`, `M`
being the `method` of the `ndm_detectspikes` node (or of `general`), and no
other file is used.

- **Missing:** when that `.res` is missing, the step stops before any engine
  runs, naming the expected file, the detection method and the `.res` files
  that are present; `--check` reports the same.
- **Printed:** each group's `.res` is printed with its spike count.
- **No detection method set** (a session detected before `ndm_detectspikes`,
  or by hand): the old order, but only waveform-method tokens (`standard`,
  `sdiff`, `stderiv[_S|C<n>]`), so `.res.emg.N` is never read as spike times;
  the untagged `SESSION.res.N` still works.
- **Same rule** in `ndm_pca`, `ndm_refeaturize` and `ndm_reextractspikes`,
  which read the `.res` next to the `.spk`.  `ndm_alignspikes` and
  `ndm_alignspikes_pca` are unchanged: their engines resolve the `.res`
  themselves (`custody::resolveAny`).

New helpers in `ndm_functions`: `ndm_shared_res` and `ndm_shared_res_missing`.
`test/test_extract_res_name.sh` (14 checks; 11 fail on the old scripts).

</details>

## 2026-11-06 — ndm: tab completion of options, their values and spike groups

<details>
<summary>Details</summary>

The `ndm` tab completion goes beyond the commands and steps:

- **Step options:** `ndm <step> -<Tab>` offers the step's options and
  `--help`. They are read from the usage line the step prints on a wrong
  command line, so a new step or option completes without an edit.
  `detectspikes` and `extractspikes` take theirs from `ndm_process_spikes`.
- **Option values:**
  - `--method`: `standard sdiff stderiv`, or the choices the usage line names.
  - `--methodOrder`: 0–3.
  - `--layout`: `bundle free`.
  - `--inputExtension`: the extensions of the session's own files.
- **Spike groups:** for the steps whose usage names a group, a positional
  argument completes to the session's group numbers. A session file is
  also offered in the first position.
  - The steps are `alignspikes_pca`, `applydrift`, `autoklusta`,
    `denoiseuniform`, `estimategeometry`, `klustakwik`, `localise`, `pca`,
    `pca_multichannel`, `refeaturize`, `spikecleaner` and
    `subcluster_unmatched`.
  - The session is the one on the command line, else the folder's own.
  - One group is offered for most steps; `applydrift`'s target groups
    repeat.
  - After `--`, files are offered.

`ndm list groupids [session]` prints one line per group, `<number><TAB>
<channels>`, and is what the completion reads.

`test_ndm_cli.sh` gains 14 checks (39 in all), and 11 of them fail with
the old completion script. Also checked by pressing Tab in an interactive
bash 5 with bash-completion loading the file by name:
`ndm klustakwik <Tab><Tab>` listed `1 2 3 4 5 rat01.yaml`.

Files: `src/ndmanager-plugins/scripts/{ndm,ndm.bash-completion,ndm_list.py,
ndm.docbook}`, `test/test_ndm_cli.sh`,
`doc/ndmanager-plugins/commands/ndm.md`.

</details>

## 2026-11-05 — Man pages for every ndm step, generated from the command reference

<details>
<summary>Details</summary>

Only 9 of the ~50 `ndm` commands had a man page: the ones with a hand-written
`.docbook`. Of those, `ndm_xml2yaml.docbook` was never registered. Every page
also had `[FIXME: source]` / `[FIXME: manual]` in its header and footer.

- **`src/ndmanager-plugins/cmake/md2man.py`** (Python 3, standard library):
  renders a command's page in `doc/ndmanager-plugins/commands` as roff.
  - It finds the command on its own page, or as a `## \`name\`` section of a
    shared page (signal-processing, spike-detection, pca, …). From a shared
    page it also takes the sections that belong to no command, the page's
    introduction (as NOTES) and its See also.
  - It handles headings, lists, tables (a tagged paragraph per row), code,
    quotes, links and emphasis.
  - Non-ASCII characters are written as `\\[uXXXX]`, so groff and mandoc read
    them without preconv.
- **`add_md_manpage()`** (in `NdmManPage.cmake`). In
  `scripts/CMakeLists.txt` every `ndm*` command in the install list now gets
  its `.docbook` page when it has one, else the generated page. That is 60 pages
  installed, against 13 before, and `man ndm_detectspikes` works.
- **Header and footer:** the DocBook pages take "neurosuite-3 3.1.0" and
  "NDManager Plugins" from a generated `ndm-manpage-meta.xml` through the
  stylesheet's profile parameters. The generated pages use the same values.
- **Rebuilds:** `gzip -k` keeps the `.1` files, so an unchanged page is no
  longer rebuilt on every build.
- **Docs:** RECIPES (no man page work for a new step), the Linux install
  optional packages (`xsltproc docbook-xsl-ns`), and the command index.

Test `test_manpages.py`:

- every installed `ndm` command has a page source;
- every generated page is clean roff (groff `-ww`) with no Markdown left;
- inline cases;
- a DocBook page has no FIXME in its header.

</details>

## 2026-11-05 — ndm: one command for the NDManager plugins

<details>
<summary>Details</summary>

New command `ndm`, installed with the steps:

| Command | What it does |
|---|---|
| `ndm <step> [arguments]` | runs `ndm_<step>` with the arguments unchanged (`ndm detectspikes`, `ndm pca 3 --method stderiv`) |
| `ndm configure [session]` | starts ndmanager on the session |
| `ndm list probes [session]` | `probes:` entries: the file each resolves to (searched as `ndm_setupgroups` does) or "not found", model, shanks, sites, layout, substrate, the groups on it, and estimated / template / `geometry_backup` flags |
| `ndm list spikegroups [session]` | channels as ranges, `nSamples`, peak, `nFeatures`, probe and shank, where the site positions come from, skipped channels |
| `ndm list programs [session]` | the steps under `programs:` and the values set |
| `ndm list steps`, `ndm help` | every step by stage with one line each; `ndm help <step>` prints the step's own help |

The session is the YAML given (or its folder), else the one named after the
current folder, as for the steps.

- A mistyped step is refused with the closest names, and a helper
  (`ndm_functions`) is refused as not a step.
- Deprecated aliases still run but are not listed.
- Bash completion is installed as
  `share/bash-completion/completions/ndm` (Unix only).

Files:

- `src/ndmanager-plugins/scripts/{ndm,ndm_list.py,ndm.bash-completion,CMakeLists.txt,ndm.docbook}`;
- `doc/ndmanager-plugins/commands/{ndm,README}.md`;
- `doc/ndmanager-plugins/README.md`;
- `src/ndmanager-plugins/test/test_ndm_cli.sh` (25 checks; the step table
  must match the install list).

</details>

## 2026-11-04 — ndm_estimategeometry: the estimate goes into the probe file, no report

<details>
<summary>Details</summary>

The step wrote two new files per group, `SESSION.bundle.N.probe` and a report
`SESSION.bundle.N.yaml`, and left the group's own probe file alone. Requested:
no extra YAML file; update the probe file, keeping the old geometry as
`geometry_backup`.

- **The probe file bound to the group** (`probeId` / `shankIndex`) is updated
  in place:
  - its sites for the group's channels, centred where they were;
  - `sites.geometry_backup`: the earlier layout, written on the first run
    only;
  - for a bundle, `substrate.material: wire-bundle` and
    `sites.plane: cross-section`;
  - `probeFile.estimate.<shank>`: the run's weights, stress, per-site SD,
    nearest neighbours, low-coherence wires and the agreement with the previous
    layout.

  The file's leading comment lines are kept.
- **A group without a probe file** gets a new `SESSION.probe.<id>.probe`. The
  log gives the `probeId` and `channelOffset` to attach it with. A rerun finds
  it rather than writing another.
- **A probe file outside the session folder** (the shared library) is not
  changed. The session YAML is not changed either; a `sitePositions_um` left
  stale is reported.
- **Already estimated** groups (probe file with `estimate`, or an unattached
  estimate) are skipped, named or not, unless `--force` / `overwrite: 1`.
- **`--estimateKnown`** writes nothing; the log gives the check.
- **No report:** the weights, positions with SD and the agreement with the
  previous layout are printed.

`test_estimategeometry.py`:

- rewritten for the probe file: 24 checks;
- new checks: in-place update with the backup kept across `--force`, header
  comment kept, rerun skip, unattached estimate found again, library file not
  changed, no report file.

`test_estimategeometry_cli.sh` reads the estimate from the probe file.

</details>

## 2026-11-04 — ndm_estimategeometry: a group never calibrates its own estimate

<details>
<summary>Details</summary>

With `--estimateKnown`, a group with a geometry was both estimated and one of
the calibration groups. Its estimate then weighted the measures by how well
they matched its own attached layout, and was compared with that same layout.
Seen on a Plexon octrode with a Probe Maker placeholder, where the log read
"estimating group(s) [1]; calibration from known geometry: [1]".

Calibration is now per estimated group and leaves that group out. A group with
no other calibration group gets split-half weights, and the log says so. The
other estimated groups still calibrate on it.

`test_estimategeometry.py`: 2 new checks (20 in all). On the previous helper,
the self-calibration check fails.

</details>

## 2026-11-04 — ndm_estimategeometry: a named group is estimated even with a geometry; wire-bundle templates do not count

<details>
<summary>Details</summary>

Reported on a Plexon octrode session: `ndm_estimategeometry 1` estimated
nothing. Group 1 had a probe file attached, a Probe Maker placeholder (a
20 µm grid marked `silicon`), and any group with a geometry was skipped, even
when named.

- **A group named** by `groups` or on the command line is estimated whatever
  geometry it has, gets `SESSION.bundle.N.probe`, and is not used as its own
  calibration. Only the named groups are estimated.
- **A wire-bundle template** — a probe file with `substrate.material:
  wire-bundle` not written by this step, such as the library's
  `general/tetrode.probe` — no longer counts as a geometry. Its group is
  estimated by default and does not calibrate.
- A placeholder marked `silicon` cannot be told from a measured shank: name its
  group, and keep it out of the calibration with `calibrationGroups` when other
  groups are estimated.
- The `validation` section now says whether the attached layout is a template.
  Its note no longer claims to check the estimate unless the layout was
  measured.
- The skip messages name `--estimateKnown` and `--force`, the wrapper's
  options; they named the helper's `--estimate-known 1` and `overwrite: 1`.

`test_estimategeometry.py`: 5 new checks (named group, template, the shank
still calibrating); 18 in all.

</details>

## 2026-11-04 — ndm_estimategeometry: command-line options as in ndm_pca

<details>
<summary>Details</summary>

`ndm_estimategeometry` read only `[parameter-file] [electrodeGroup]`. It now
reads its command line like `ndm_pca` and `ndm_detectspikes`:

- At most two arguments: `[parameter-file] [electrodeGroup]`. Without a
  parameter file, the session is the folder's name, and a group number can be
  given alone.
- Options: `--force` (redo groups whose report exists), `--inputExtension`,
  `--wireDiameterUm`, `--layout`, `--estimateKnown`. Each wins over the YAML
  value; `--option=value` works too.
- An unknown option, a group that is not a number, a bad `--layout` or a third
  argument stops the step with the usage before anything runs. Before, such
  arguments were read as a group number and failed inside the Python helper.

New test `test_estimategeometry_cli.sh`: 9 checks. On the previous wrapper, 5
fail.

Files: `src/ndmanager-plugins/scripts/ndm_estimategeometry`,
`doc/ndmanager-plugins/commands/ndm_estimategeometry.md`,
`templates/template.yaml`,
`src/ndmanager-plugins/test/test_estimategeometry_cli.sh`.

</details>

## 2026-11-04 — ndm_estimategeometry: wire-bundle site layout from channel coherence

<details>
<summary>Details</summary>

New step `ndm_estimategeometry` (helper `process_estimategeometry.py`) for
tetrodes, octrodes and other wire bundles recorded without a geometry. For
every pair of a group's channels it measures:

- coherence of the raw `.dat` in bands, spike-free from 300 Hz up;
- coherence of the `.fil` background on spike-free windows;
- how the spikes' amplitude profiles and waveforms co-vary.

The pairs are combined into one ordering from close to far. The wires are then
placed in the bundle's cross-section by ordinal stress, no two closer than
`wireDiameterUm` and packed. Segment resampling gives each site's uncertainty
and the stability of its nearest neighbours.

The probe file is optional. In a session that mixes silicon probes and wire
bundles, the groups with a geometry are not estimated; they calibrate how much
each measure counts (its Spearman correlation with the known distances).
Without them, the measures are weighted by split-half reliability.

Output per group: `SESSION.bundle.N.probe`, with `sites.plane: cross-section`,
and `SESSION.bundle.N.yaml`, the report. The session YAML is not changed.

Validation:

- **Synthetic octrode (15 µm wires):** recovered within 3 µm RMS. Negative
  control without spatial structure: 16 µm.
- **Real Buzsaki64L shank, estimated as unknown:** Spearman 0.97 with the true
  site distances.
- **Not validated:** a real bundle against a measured geometry.

Files: `src/ndmanager-plugins/scripts/{ndm_estimategeometry,
process_estimategeometry.py,CMakeLists.txt}`, `templates/template.yaml`,
`src/ndmanager/tools/pipeline_script_defs.py` and the generated
`pipelinescriptdefs.inc` / `parametercatalog.inc`,
`doc/ndmanager-plugins/commands/{ndm_estimategeometry,README}.md`,
`doc/ndmanager-plugins/formats/probe.md`,
`src/ndmanager-plugins/test/test_estimategeometry.py`.

</details>

## 2026-11-02 — NeuroScope: the spectral view updates when a setting changes

<details>
<summary>Details</summary>

The Spectral tab staged every change and recomputed only when `U` was
pressed. The debounce for automatic updates was already in
`SpectralView`, but nothing ever switched it on. `U` was also easy to
lose: after you edit a value, the focus stays in that spin box. A text
field takes `U` for itself, so the key did nothing until you clicked
somewhere else, such as the scrollbar.

The tab now opens with **update automatically** checked. A change is
applied 400 ms after the last edit, so typing or spinning a value
recomputes once. The image title shows "updating…" while a change waits.
Unchecking the box restores manual mode: the title then reads
"uncommitted changes (press U)", and `U` applies the change. Checking it
again applies any change already waiting. `U` still applies a change at
once in either mode. The choice is saved with the other spectral
settings as `autoUpdate`.

`SpectralView::setUpdateDelay` (never called) is replaced by
`setAutoUpdate` / `isAutoUpdate`.

Files: `src/neuroscope/src/spectralview.{h,cpp}`,
`spectralinspector.{h,cpp}`, `doc/neuroscope/README.md`.

</details>

## 2026-10-29 — Klusters: the matrices are shown on open by default

<details>
<summary>Details</summary>

Opening a document shows the Error, Template, Residual and Drift matrices (and the trace view) by default.

- **Default on.**  Preferences ▸ Views ▸ Matrices ▸ *Auto-show matrix displays on open* was off.
- **Existing settings.**  Every save of the preferences stores every value, so settings written before held the old
  default, `false`, as if it had been chosen.  A new `settingsRevision` (1) marks files written from now on;
  a file without it takes the new default once.  Turning the option off afterwards is kept.
- The checkbox's tooltip described an older layout (a separate Grouping Assistant display); it now says what
  happens and that the default is on.

Files: `src/klusters/src/configuration.{h,cpp}`, `src/klusters/src/prefmatriceslayout.ui`.

</details>

## 2026-10-29 — Settings move to ~/.config/neurosuite-3

<details>
<summary>Details</summary>

Klusters, NeuroScope and NDManager keep their settings in `~/.config/neurosuite-3/`.

- **Before.**  `~/.config/sourceforge/<program>.conf` (the organization name inherited from the original
  Neurosuite), the theme in `~/.config/neurosuite/appearance.conf`, and data such as NDManager's session templates
  in `~/.local/share/sourceforge/<program>/`.
- **Now.**  `~/.config/neurosuite-3/<program>.conf` and `appearance.conf`, data in
  `~/.local/share/neurosuite-3/<program>/` (macOS: `com.neurosuite-3.*` plists; Windows:
  `HKEY_CURRENT_USER\Software\neurosuite-3`).
- **Moving over.**  The first start of each program copies its old settings, the theme and its data folder, when
  it has none of its own yet, and prints a line saying so.  Nothing is overwritten or deleted; the old files stay
  for older builds.  INSTALL.md has a table of the locations.
- **Code.**  `neurosuite::initApplicationIdentity()` (`libklustersshared`, `appsettings.h`) replaces the
  organization/domain/application lines in the three `main.cpp`; `theme.cpp` uses the same organization.  Test:
  `klustersshared_appsettings_test`.

Files: `src/libklustersshared/src/klustersshared/appsettings.{h,cpp}` (new), `theme.cpp`,
`src/libklustersshared/CMakeLists.txt`, `src/libklustersshared/src/CMakeLists.txt`,
`src/libklustersshared/test/appsettings_test.cpp` (new), `src/{klusters,ndmanager,neuroscope}/src/main.cpp`,
`doc/INSTALL.md`, `doc/ndmanager/README.md`, `src/klusters/docs/PLUGIN_API.md`,
`src/libklustersshared/docs/LIBRARY.md`.

</details>

## 2026-10-29 — process_extractemg: the CPU FastICA step is faster per core

<details>
<summary>Details</summary>

The CPU FastICA step is faster per core: the dot product in it uses four partial sums instead of one serial chain.

- One 64-channel probe, 20 s ID window: 21.2 → 17.6 ms per iteration on 1 thread, 9.4 → 6.4 ms on 2 threads.
- Results stay the same for any thread count, but differ in the last bits from the previous entry.  On the test
  data the loading, unmixing, threshold and events were identical; the number of FastICA iterations changed, as
  it does after any last-bit change (it depends on the data, not on this change).

Files: `src/ndmanager-plugins/src/process_extractemg/process_extractemg_gpu_cpu.cpp`.

</details>

## 2026-10-29 — ndm_extractemg: the output gives the time of FastICA and of each pass

<details>
<summary>Details</summary>

`ndm_extractemg` (`process_extractemg -v`) reports how long each part took: FastICA per probe (seconds, ms per
iteration, N) and, at the end, identification, streaming, and events with waveforms.  With these, `backend: cpu`
and a GPU backend can be compared on one's own recording.  The command page says how, and how the CPU threads are
used.

Files: `src/ndmanager-plugins/src/process_extractemg/process_extractemg.cpp`,
`doc/ndmanager-plugins/commands/ndm_extractemg.md`.

</details>

## 2026-10-29 — process_extractemg: event detection no longer copies the EMG trace

<details>
<summary>Details</summary>

EMG event detection holds one less full-length copy of each probe's trace.

- **Before.**  `detectEvents` copied the float32 EMG trace into a double vector before smoothing: 8 bytes per
  sample per probe, for all probes at once, on top of the trace (4 B) and the envelope (8 B).  For a 351-minute
  recording at 32,552 Hz that copy is 5.5 GB per probe.
- **Now.**  The boxcar reads the float trace and widens each sample as it goes; the numbers are the same.  Heap peak
  on a 5-minute, 96-channel test file (one 64-channel probe): 195.4 → 117.3 MB.  `ndm_extractemg.md` now counts
  the envelope in its memory estimate, which it left out.

Files: `src/ndmanager-plugins/src/process_extractemg/process_extractemg.cpp`,
`doc/ndmanager-plugins/commands/ndm_extractemg.md`.

</details>

## 2026-10-29 — process_extractemg: the streaming pass uses every thread for one probe too

<details>
<summary>Details</summary>

The streaming pass (`CLEAN`: EMG projection, subtraction, rounding) uses every thread for a single probe too.

- **Before.**  Each chunk was split by probe, so a one-probe session (the per-probe mode with one probe) ran the
  whole recording through one thread.
- **Now.**  Each chunk is split by blocks of 4,096 samples.  Results are bit-identical.  On a 5-minute, 96-channel
  test file with one 64-channel probe and 2 threads, the pass took about 0.95 s instead of 1.5 s without
  `-emgclean.dat`.  With the cleaned file written, the disk dominates, so the end-to-end gain is smaller.

Files: `src/ndmanager-plugins/src/process_extractemg/process_extractemg.cpp`.

</details>

## 2026-10-29 — process_extractemg: no second copy of the whitened data on the CPU

<details>
<summary>Details</summary>

On the CPU, FastICA no longer keeps a second copy of the whitened data.

- **Before.**  With `--backend cpu`, and whenever a GPU kernel was not available, the Cpu pow3 kernel copied the
  whitened matrix (N × nch doubles) and ran the same step as the inline code on the copy.
- **Now.**  The inline code runs the step; only a GPU kernel gets the matrix.  Peak memory for one 64-channel probe
  with a 20 s ID window: 487 → 381 MiB; at the default 60 s, about 330 MB less per probe.  Results unchanged.

Files: `src/ndmanager-plugins/src/process_extractemg/process_extractemg.cpp`.

</details>

## 2026-10-29 — process_extractemg: with several probes, FastICA uses every thread

<details>
<summary>Details</summary>

With two or more probes, FastICA uses every thread.

- **Before.**  Nested parallelism was switched off (`omp_set_max_active_levels(1)`), so each probe's pow3 step ran
  on one thread: P probes used P threads during identification however many the machine had (two probes, 8
  threads: 2 busy).  Since the step reads the whole whitened matrix every iteration, that left most of the memory
  bandwidth unused.
- **Now.**  The threads are split: min(P, threads) probes at a time, and threads ÷ that for each probe's pow3 step
  (two probes on 16 threads: 2 × 8).  `-v` prints the split.  A single probe is unchanged (all threads in the
  step), as are the results: the step is order-independent since the previous entry.

Files: `src/ndmanager-plugins/src/process_extractemg/process_extractemg.cpp`.

</details>

## 2026-10-29 — process_extractemg: FastICA gives the same result on every run and for any thread count

<details>
<summary>Details</summary>

`process_extractemg` gives the same FastICA result on every run and for any number of threads.

- **Before.**  The pow3 step summed per-thread partial sums in a critical section, in whatever order the threads
  arrived.  With 3 or more threads, one-ulp differences sent FastICA down a different path on every run: on a
  synthetic 64-channel probe, four runs with 4 threads took 3,601, 4,569, 4,913 and 11,666 iterations and wrote
  four different loadings.  The per-group seed ("so reruns reproduce") only held up to 2 threads.
- **Now.**  `pow3StepCpu()` sums the rows in fixed blocks of 4,096 and adds the block sums in order.  The same probe
  takes 7,668 iterations with 1 or 4 threads, run after run, with identical outputs.  The inline FastICA loop and
  the Cpu kernel share it; an unused N-length buffer is gone.
- **Results change once:** the summation order differs from the old serial order, so loadings differ in the last
  bits from earlier runs (event times and the `.emg` trace were the same on the test data).
- **Test:** `ndm_test_extractemg_pow3` checks the step is bit-identical for 1, 2, 3, 4 and 7 threads.

Files: `src/ndmanager-plugins/src/process_extractemg/process_extractemg.cpp`, `process_extractemg_gpu.h`,
`process_extractemg_gpu_cpu.cpp`, `process_extractemg_pow3_test.cpp`.

</details>

## 2026-10-28 — Docs: CUDA on Ubuntu 26.04

<details>
<summary>Details</summary>

`doc/gpu/README.md` has an Ubuntu 26.04 section: which CUDA toolkits compile neurosuite-3's CUDA code there (Ubuntu's
CUDA 12.4: yes up to sm_89 here, no Blackwell; NVIDIA 13.0: no, glibc 2.43 `rsqrt` clash; NVIDIA 13.4: yes, with
GCC 15.2), how the architectures are chosen, and what a build that makes the machine stop responding means.

Files: `doc/gpu/README.md`.

</details>

## 2026-10-28 — Build: without a visible GPU, CUDA architectures the toolkit cannot compile are left out

<details>
<summary>Details</summary>

With no GPU visible at configure time, the CUDA architecture list is cut to what the installed `nvcc` can compile.

- **Before.**  The fallback list `75;86;89;100;120` was used as it is.  sm_100 and sm_120 need CUDA 12.8, so with an
  older toolkit (Ubuntu 26.04's packaged CUDA 12.4) CMake's compiler check failed: "The CUDA compiler … is not able
  to compile a simple test program".
- **Now.**  `ns_gpu_cuda_arch_supported()` asks `nvcc --list-gpu-arch` and leaves out what it does not list, saying
  so in the configure output.  With CUDA 12.4: `75;86;89`.  With 13.4 the list is unchanged.  `native`, used when
  `nvidia-smi` sees a GPU, is not affected.

Files: `cmake/GpuBackends.cmake`.

</details>

## 2026-10-28 — ndmanager-plugins: the CUDA filters, thresholds and grouper are built for the configured GPU architectures

<details>
<summary>Details</summary>

`process_butterworth`, `process_medianfilter`, `process_medianthreshold` and `process_spikegrouper` are built for
the architectures the rest of the build uses.

- **Before.**  Each set `CMAKE_CUDA_ARCHITECTURES` to `86 89 100 120` in its own directory, overriding `native`
  and `-DCMAKE_CUDA_ARCHITECTURES=`.  With Ubuntu 26.04's packaged CUDA 12.4 they stopped at
  `nvcc fatal : Unsupported gpu architecture 'compute_100'`.
- **Now.**  In the full build they keep the top-level choice.  A standalone configure of the plugins
  (`cmake -S src/ndmanager-plugins`) still uses `86 89 100 120`.

Files: the four `src/ndmanager-plugins/src/process_*/CMakeLists.txt`, `doc/ndmanager-plugins/install/linux-cuda.md`,
`doc/gpu/README.md`.

</details>

## 2026-10-28 — Klusters: the CUDA code is built for the configured GPU architectures

<details>
<summary>Details</summary>

Klusters' CUDA code is built for the architectures the rest of the build uses.

- **Before.**  `src/klusters/src/CMakeLists.txt` set `CUDA_ARCHITECTURES "75;86;89;120"` on the target.  That
  overrode `native` (the GPU `nvidia-smi` sees), the fallback list and `-DCMAKE_CUDA_ARCHITECTURES=`, and it asked
  for sm_120, which CUDA before 12.8 refuses: with Ubuntu 26.04's packaged CUDA 12.4 the build stopped at
  `nvcc fatal : Unsupported gpu architecture 'compute_120'`.  On an RTX 3060 it also compiled three architectures
  the card does not use.
- **Now.**  The target takes `CMAKE_CUDA_ARCHITECTURES`: `native` when a GPU is visible at configure time.

Files: `src/klusters/src/CMakeLists.txt`, `src/libklustersshared/docs/LIBRARY.md` (known issue 9 marked fixed).

</details>

## 2026-10-28 — Build: parallel jobs are limited to the installed memory

<details>
<summary>Details</summary>

A full build no longer takes the machine down.  On Ubuntu 26.04 a first build could swap the machine until the desktop
session (ThinLinc) and sshd stopped answering; the last lines printed were Klusters' "Building CUDA object …".

- **Cause.**  The quick start's `cmake --build build --parallel` has no job count, and with the default Makefile
  generator CMake turns that into `make -j` with **no** limit.  One Klusters source file takes 0.7–0.9 GiB to
  compile with GCC 15, Qt 6 and LTO, and Klusters has 122.  The `.cu` files are only where the output stopped:
  they come last in Klusters' source list, and each peaks at 0.2–0.6 GiB (CUDA 12.4 and 13.4 on Ubuntu 26.04).
- **Ninja.**  Configure puts every compile step into one job pool and every link step into another, sized from the
  installed memory: memory ÷ `NS_COMPILE_JOB_MIB` (1536) compile jobs and memory ÷ `NS_LINK_JOB_MIB` (4096) link
  jobs, each at most the core count.  The configure output prints the depths.  `-DNS_JOB_POOLS=OFF` turns it off.
- **Makefiles.**  A pool cannot be expressed there.  Configure warns and prints a `-j` that fits.
- **Docs.**  The README, INSTALL.md and BUILD.md build with `-G Ninja`; BUILD.md §9 has the symptom.

Files: `cmake/NsJobPools.cmake` (new), `CMakeLists.txt`, `README.md`, `CLAUDE.md`, `doc/INSTALL.md`, `doc/BUILD.md`.

</details>

## 2026-10-27 — Klusters: Hierarchy ▸ Break Up Parent by Template sends each child to the large parent it matches

<details>
<summary>Details</summary>

A new Hierarchy entry, *Break Up Parent by Template…*, for a parent that holds atoms of several units.

- **Input.**  It works on the parents selected in the main palette.  It asks for a spike count and a
  correlation (defaults 100 and 0.90).
- **Targets.**  Every other parent with *more than* that many spikes is a target, its template being its mean
  waveform.  The parents being broken up are never targets, nor are the noise, artefact and reserved chunk
  clusters.
- **Matching.**  Each child is matched by its own mean waveform to the target it correlates with best (Pearson,
  as for the orphan commands).  A child that reaches the threshold moves there whole, keeping its id.  The other
  children become parents of their own, as *Dissolve Parent* makes them, and the largest of them keeps the
  parent's id.  A parent whose only child matches nothing is left as it is.
- **Plan.**  A box shows the plan before anything changes: how many children move and to how many parents, how
  many become parents, and which child keeps the id.  The details list every child with its best match and r.
- **Undo.**  The children going to the same target move in one step (`moveSpikeSubsetToCluster`), and each new
  parent is one more (`promoteChildren`).  The box says how many steps the plan takes when that is more than the
  undo depth.  The selection lands on every parent that received a child or now holds one.

On g7's fiber-session sort (minutes 168–204), at the defaults, 212 of the 777 children of the 149 parents with
two or more children would find a large parent.  Parent 510 (36 children, 1,943 spikes), for example, sends 6
children (1,254 spikes) to 5 parents and leaves 29 as parents of their own.

Files: `src/klusters/src/klustersdoc.h`, `klustersdoc_hierarchy.cpp` (`planBreakUpParents`,
`breakUpParents`), `klusters.{h,cpp}`, `klusters_hierarchy.cpp`, `klusters_state.cpp`,
`doc/klusters/hierarchical-clustering.md`.

</details>

## 2026-10-27 — Klusters: Merge Orphan Parents says why when it merges nothing

<details>
<summary>Details</summary>

When *Hierarchy ▸ Merge Orphan Parents (by template)* found nothing to merge, it said so only in the status bar,
which after two dialogs looked like a command that did nothing.  It now shows a box in each such case, and keeps
the status-bar line:

- **No orphan.**  No parent is below the spike count.  With two or more parents selected, only the selected
  ones can be orphans, and the box says so: select one parent, or none, to work on all of them.  A merge leaves
  its new parents selected, so a second run right after a merge looks only at those.
- **No other parent** to compare the orphans with.
- **Nothing to merge.**  No two orphans match the same parent.  The box gives:
  - the orphans' best correlations: lowest, highest and median;
  - how many reach the threshold;
  - the strictest threshold at which two of them would match the same parent;
  - details listing every orphan with its best match.

A parent with few spikes has a noisy mean waveform, so its correlation with a template stays well below 1 even
when it is the same unit.  In g7's fiber-session sort (minutes 168–204), the 67 parents below 25 spikes reach
at most 0.978 (median 0.850), so at 0.99 nothing matched and the command looked dead.  The strictest threshold
at which two of them share a parent is 0.94.  The correlations in the details and the box now have three
decimals, so an r of 0.995 no longer shows as 1.00 beside a threshold of 1.00.

Files: `src/klusters/src/klusters.cpp` (`slotMergeOrphanParents`),
`doc/klusters/hierarchical-clustering.md`.

</details>

## 2026-10-27 — Klusters: Undo Child-Layer Edit is enabled right after Merge Orphan Children

<details>
<summary>Details</summary>

*Hierarchy ▸ Merge Orphan Children* merges atoms, and the merge goes on the atom layer's undo stack, but
*Hierarchy ▸ Undo Child-Layer Edit* (`Ctrl+Shift+Z`) stayed disabled after it: the command did not refresh the
atom undo and redo entries, as *Merge Selected Children* does.  Until something else refreshed them,
`Ctrl+Shift+Z` did nothing.  They now follow the atom stack as soon as the command ends.

Files: `src/klusters/src/klusters.cpp` (`slotMergeOrphanChildren`),
`doc/klusters/hierarchical-clustering.md`.

</details>

## 2026-10-27 — Klusters: Merge Orphan Children leaves the orphans that do not match alone

<details>
<summary>Details</summary>

*Hierarchy ▸ Merge Orphan Children (by median waveform)* (`Shift+B`) judges a parent's children below the spike
count (the orphans) against the per-point median of the mean waveforms of its other children, the ones not
being merged.  The orphans that match are merged into one child, as before.  The orphans that do not match
were merged into a second child; they now stay as they are, each its own atom.  The higher the correlation
threshold, the fewer orphans matched, so a strict threshold gathered nearly all of them into that second
child: at 0.99 the command merged almost every orphan, whatever its shape.  *Merge Orphan Parents* had the
same change (the entry below).

The status bar says how many orphans were merged and how many stay as they are, and the correlation dialog
and the menu entry's tooltip describe the reference as the median of the children that are not being merged.

Files: `src/klusters/src/klustersdoc.h`, `klustersdoc_hierarchy.cpp`
(`mergeOrphanChildren`), `klusters.{h,cpp}`,
`doc/klusters/hierarchical-clustering.md`.

</details>

## 2026-10-27 — Klusters: Merge Orphan Parents matches against every parent not being merged, and leaves the unmatched alone

<details>
<summary>Details</summary>

Two changes to *Hierarchy ▸ Merge Orphan Parents (by template)* (`Ctrl+Shift+B`).

- **The orphans that match no template are no longer merged.**  Until now they were merged into one more new
  parent.  The higher the correlation threshold, the fewer orphans matched, so a strict threshold merged
  nearly all of them into that one parent: at 0.99 the command pushed almost everything together.  They now
  stay as they are, and the details list each one with its best match and correlation.
- **The templates are every parent that is not being merged.**  Until now they were every parent at or above
  the spike count, so with a selection, a small parent that was not selected was neither merged nor compared
  with.  The orphans are still the selected parents below the spike count, or every parent below it when fewer
  than two are selected.  Every other parent is a template, whatever its size and whether it is selected.
  Orphans are compared with those parents only, never with each other.
- The box says how many parents each orphan was compared with, and how many stay as they are.  The status bar
  reports the parents left as they were, and the menu entry's tooltip describes the new behaviour.

Files: `src/klusters/src/klustersdoc.h`, `klustersdoc_hierarchy.cpp`
(`planOrphanParents`, `mergeOrphanParents`), `klusters.{h,cpp}`,
`doc/klusters/hierarchical-clustering.md`.

</details>

## 2026-10-26 — Klusters: Hierarchy ▸ Merge Orphan Parents gathers the tiny parents by template

<details>
<summary>Details</summary>

The parent-layer counterpart of *Merge Orphan Children*, for the tiny parents
that splitting, reclustering and promotions leave in the main palette
(`Ctrl+Shift+B`).

- It asks for a spike count and a correlation (defaults 25 and 0.90). A parent
  below the spike count is an orphan; every parent at or above it is a
  template, its mean waveform.
- Each orphan is matched to the template its own mean waveform correlates with
  best (Pearson). Orphans matching the same template are merged into one new
  parent beside it, and the orphans matching none into one more. A group of
  one is left as it is.
- The template parents are not changed: folding a new parent into its template
  is left to the curator. The orphans' atoms stay apart under the new parent.
- A box shows the plan first (**Show Details…** lists every group with its
  correlations) and says when the plan needs more undo steps than the undo
  depth allows.
- It works on the selected parents, or on all parents when fewer than two are
  selected. The noise and artefact clusters and the reserved chunk clusters
  take no part.

Files: `src/klusters/src/klustersdoc.h`, `klustersdoc_hierarchy.cpp`
(`planOrphanParents`, `mergeOrphanParents`), `klusters.{h,cpp}`,
`klusters_hierarchy.cpp`, `klusters_state.cpp`,
`doc/klusters/hierarchical-clustering.md`.

</details>

## 2026-10-26 — Klusters: a recluster or drift job no longer adds a tab-change connection

<details>
<summary>Details</summary>

Each recluster and each drift job connected the display tabs' change signal to
`KlustersApp::slotTabChange` once more, so after N of them every tab change ran
`slotTabChange` N + 1 times (until a new display reset the connections).  The
connection is now made only when it is missing (`Qt::UniqueConnection`).

Files: `src/klusters/src/klusters_recluster.cpp`, `klusters_dipdrift.cpp`.

</details>

## 2026-10-26 — Klusters: a recluster that ends early removes its temporary files

<details>
<summary>Details</summary>

A recluster writes the selected clusters' features to a temporary
`<base>--<cluster ids>--<date>.fet.N` in the session folder, with a copy of the
session YAML under the same base, and the program writes `….clu.N` beside
them.  Three ways out of a recluster left them in the folder: a feature file
that could not be created (the YAML stayed), a program that could not be
started, and a program that wrote no cluster file.  They are now removed
however the recluster ends (`KlustersApp::removeReclusterTempFiles`, and the
missing-cluster-file path of `KlustersDoc::integrateReclusteredClusters`).

A program that wrote no cluster file was also reported twice, the first time
as "Could not delete the temporary cluster file used by the reclustering
program", which was not what had happened.  That box is gone.

Files: `src/klusters/src/klusters.h`, `klusters_recluster.cpp`,
`klustersdoc_recluster.cpp`.

</details>

## 2026-10-26 — Klusters: the drift actions do not start while another job runs

<details>
<summary>Details</summary>

Generate Probe Drift and Apply Drift + Recluster Siblings take over the
recluster's output widget by deleting it, and did so even while a recluster or
another drift job ran: that program was killed halfway ("QProcess: Destroyed
while process ... is still running"), the recluster reported a failure and its
result was lost.  Like a plugin, they now refuse to start while a recluster, a
drift job or a plugin runs (`KlustersApp::refuseWhileJobRuns`).

Files: `src/klusters/src/klusters.h`, `klusters_dipdrift.cpp`,
`doc/klusters/README.md`.

</details>

## 2026-10-26 — Klusters: the end of a drift job no longer locks Klusters

<details>
<summary>Details</summary>

Generate Probe Drift and Apply Drift + Recluster Siblings run their program in
an output tab, as a recluster does, but only a recluster's end marked the job
finished.  After a drift job the editing actions stayed disabled (Undo, Redo,
Split, Group, the Recluster entries, ...), a recluster waited for the job for
good, and File ▸ Close and quitting waited for it too, so the document could
not be closed.

The end of a drift job is now handled (`KlustersApp::slotDriftJobExited`): the
job is marked finished and the actions are restored.

Files: `src/klusters/src/klusters.h`, `klusters_dipdrift.cpp`.

</details>

## 2026-10-26 — Klusters: an output tab's last line gives the program's exit code

<details>
<summary>Details</summary>

The output tabs (Recluster output, the plugin and drift outputs) end with a
line saying how the program ended.  It was given the exit status where the exit
code belonged, so a program that exited with an error, and one that could not
be started, both ended with `*** Exited normally ***`.

- A program that exits with an error now ends with
  `*** Exited with status: N ***` (`ProcessWidget::slotOutputTreatmentOver`).
- A program that cannot be started ends with `*** Could not start ***`, after
  Qt's reason.

Files: `src/klusters/src/processwidget.cpp`.

</details>

## 2026-10-26 — Klusters: a recluster opens its output tab only if it fails

<details>
<summary>Details</summary>

Every recluster opened a **Recluster output** tab with KiloKlustaKwik's
output, and the tab stayed among the displays until the next recluster.  A new
preference, **Suppress the Recluster output tab** (**Settings ▸ Preferences ▸
Curation ▸ Reclustering**), checked by default, holds the tab back.

- The status bar says when a recluster starts (`Reclustering 5…`) and what it
  made (`Reclustering of 5 done: 3 new clusters.`).
- If the recluster fails (the program exits with an error or cannot be
  started, or its result cannot be read), the tab opens with the program's
  output, and the error message points at it.
- With the tab held back, the document's last display can be closed while a
  recluster runs: Klusters stops the program and closes the document once it
  has ended, as **File ▸ Close** does.  File ▸ Close now deletes a recluster
  output that is not a tab (held back, or its tab closed while it ran) instead
  of leaving it.
- Unchecked, a recluster opens the tab as before.

Files: `src/klusters/src/configuration.{h,cpp}`, `prefreclustering.{h,cpp}`,
`prefreclusteringlayout.ui`, `prefdialog.cpp`, `klusters.{h,cpp}`,
`klusters_recluster.cpp`, `doc/klusters/README.md`.

</details>

## 2026-10-26 — Klusters: View ▸ Show Child Cluster Palette and Show Template Palette

<details>
<summary>Details</summary>

The **View** menu gains two entries that show or hide the palettes under the
cluster list.

- **Show Child Cluster Palette** (hierarchical session).  Unchecked, the child
  palette is hidden and, as in a flat session, the selection, the displays and
  the matrices work on the parent clusters: the child-scoped matrices are
  turned off, the displays return to the parents, the Hierarchy menu's edits
  are disabled, and `V` says the child view needs the palette.  Checked again,
  the palette lists the children of the selected parent, as if it had just
  been picked.  A hierarchical session that opens while it is unchecked loads
  its atom layer but keeps the palette hidden.
- **Show Template Palette** (Templates mode) shows or hides the template
  palette.
- The Hierarchy menu's edits are enabled in one place,
  `KlustersApp::updateHierarchyEditActions`, which the open path, the end of a
  realign and the new entry share.
- Both entries start checked, hold until Klusters closes, and are registered
  commands (Preferences ▸ Input).

Files: `src/klusters/src/klusters.{h,cpp}`, `klusters_hierarchy.cpp`,
`klusters_state.cpp`, `doc/klusters/README.md`,
`doc/klusters/hierarchical-clustering.md`.

</details>

## 2026-10-26 — Klusters: View ▸ Show Reserved Chunk Clusters hides them from the palette and the displays

<details>
<summary>Details</summary>

A chunk run can leave up to 100 reserved chunk clusters (R0 … R99) in the
cluster palette.  The new **View** menu's **Show Reserved Chunk Clusters** hides
them.

- Unchecked, the parent palette leaves them out, every display drops the ones
  it shows, and Select All skips them (`KlustersDoc::setReservedClustersShown`).
  Checked again, they are listed again but not selected.
- The clustering does not change, nor do the operations over all clusters.  A
  child palette lists every atom: an atom is not a reserved cluster.
- The entry starts checked and holds for every document until Klusters closes.
  It is a registered command, so Preferences ▸ Input can give it a key.

Files: `src/klusters/src/klusters.{h,cpp}`, `klustersdoc.{h,cpp}`,
`clusterPalette.cpp`, `doc/klusters/README.md`.

</details>

## 2026-10-26 — Klusters: reserved chunk clusters are shown as R0 … R99

<details>
<summary>Details</summary>

A reserved chunk cluster's id, 16 777 116 + chunk, was shown as that
8-digit number: it took several cells of the palette and was cut off in the
correlogram labels.  Klusters now shows it as **R** and its chunk: R0 … R99,
the chunk as fiber-kit numbers it (`FK_CHUNKS` counts from 0), so chunk 14's
cluster, 16 777 130, is R14.

- `clusterLabel()` (`clusterlabel.h`) is the one rule: R<chunk> for a reserved
  id, the number for any other.  The Qt-free `reservedclusters::label` /
  `idFromLabel` behind it are tested in `klusters_test_reservedclusters`.
- The parent and child palettes, the correlogram, trace (raster), waveform and
  template-library views, the error, template, residual, drift and
  merge-recommendation matrices, the status-bar messages and the dialogs
  (Split by KNN Voting, Strip by Template, DipSplit, the realign dialogs, …)
  use it.  Hovering over R14 in the palette shows its id.
- Set Oblique Basis shows the labels and accepts R14 as well as 16777130.
- The files Klusters writes, the curation log and plugin arguments keep the
  number.

Files: `src/klusters/src/clusterlabel.h` (new), `reservedclusters.h`,
`clusterPalette.cpp`, `correlationview.cpp`, `traceview.cpp`,
`waveformview.cpp`, `templateview.cpp`, `errormatrixview.cpp`,
`templatematrixview.cpp`, `residualmatrixview.cpp`, `driftmatrixview.cpp`,
`mergerecommendview.cpp`, `clusterview.cpp`, `klusters.cpp`,
`klusters_dipdrift.cpp`, `klusters_realign.cpp`, `klusters_recluster.cpp`,
`klustersdoc.cpp`, `klustersdoc_dipsplit.cpp`, `klustersdoc_realign.cpp`,
`klustersdoc_strip.cpp`, `klustersview.cpp`, `autoMerge.cpp`, `data.cpp`,
`spikerealigndialog.cpp`, `realignreviewdialog.cpp`,
`src/klusters/test/reservedclusters_test.cpp`, `doc/klusters/README.md`,
`doc/ndmanager-plugins/formats/clu.md`.

</details>

## 2026-10-26 — Klusters: Align All and the automatic realign skip reserved chunk clusters

<details>
<summary>Details</summary>

Align All (`Shift+P`) and the automatic realign after an edit realigned a
reserved chunk cluster like a unit.  It holds every spike of a chunk a fiber-kit
chunk run left unsorted, so there is no template to align its spikes to, and
fiber-kit's realign leaves those spikes in place.  Both now skip reserved chunk
clusters.  Realign on a selected cluster still runs.

Files: `src/klusters/src/klusters_realign.cpp`, `klusters.cpp`,
`doc/klusters/README.md`.

</details>

## 2026-10-26 — Klusters: the matrices leave reserved chunk clusters out

<details>
<summary>Details</summary>

A reserved chunk cluster holds every spike of a chunk a fiber-kit chunk run
left unsorted: a mixture, not a unit.  The matrices compared it with every
unit, and the merge tools proposed merges with it.

- The template, residual and drift matrices leave reserved chunk clusters out
  of their cluster lists.
- So do the error matrix and everything built on it: the merge
  recommendations, Auto-Merge's error-matrix scores and the error-matrix sort.
  The change is in `GroupingAssistant::clusterInScope`, the model's one
  membership test.
- A reserved parent contributes no atoms to the scoped (child) matrices
  (`resolveMatrixScope`), and does not count toward a joint child view: a
  selection of one ordinary and one reserved fiber is the ordinary fiber's
  child view.
- On the atom layer an atom's own number says nothing (an atom can carry its
  parent's reserved id after a split), so the filters there go by parent.
- Auto-Merge's template scores and Split by KNN's reference clusters leave
  them out.

Files: `src/klusters/src/templatematrixthread.cpp`,
`residualmatrixthread.cpp`, `driftmatrixthread.cpp`,
`groupingassistant.{h,cpp}`, `klustersdoc_hierarchy.cpp`, `klusters.cpp`,
`autoMerge.cpp`, `data.cpp`, `reservedclusters.h`, `doc/klusters/README.md`,
`doc/ndmanager-plugins/formats/clu.md`.

</details>

## 2026-10-26 — Klusters: renumbering and sorting keep the reserved chunk clusters' ids

<details>
<summary>Details</summary>

Renumber (`R`), Renumber and Save, the automatic renumber after an edit and
Compact Cluster IDs gave the reserved chunk clusters small ids like any other
cluster, so a chunk run's clustering lost its record of which chunks were left
out.  A reserved chunk cluster now keeps its id and takes no place in the
numbering.

- `Data::renumber` copies a reserved chunk cluster as it is.
  `Data::renumberPartial` refuses a map that renames one or gives a cluster a
  reserved id.
- Renumber to End (`T`) and every sort leave reserved chunk clusters out:
  `renumberClustersToEnd`, `reorderClustersByPermutation`, and the cluster
  lists of the sorts, so no sort metric is computed for them.  `T` with only
  reserved chunk clusters selected does nothing and runs no post-edit
  automation.
- Repair Nesting numbered fresh atoms above every id of both layers, reserved
  ids included, so into the reserved block or past it.  It now numbers them
  below the block.

Files: `src/klusters/src/data.{h,cpp}`, `klustersdoc_renumber.cpp`,
`klusters_sort.cpp`, `klusters_dipdrift.cpp`, `klustersdoc_hierarchy.cpp`,
`reservedclusters.h`, `doc/klusters/README.md`,
`doc/klusters/hierarchical-clustering.md`,
`doc/ndmanager-plugins/formats/clu.md`.

</details>

## 2026-10-26 — Klusters: a reserved chunk cluster gains no spikes, and only reclustering and feature-space selections take spikes out of it

<details>
<summary>Details</summary>

fiber-kit's chunk runs (`FK_CHUNKS`) put every spike of a chunk they did not
sort into that chunk's **reserved chunk cluster**, ids 16 777 116 – 16 777 215.
Klusters now protects these clusters.

- **Nothing is added to one.**  `Data::prepareUndo`, the commit point of every
  edit, refuses an edit that would put a spike into a reserved chunk cluster it
  was not in.  It compares the row tables before and after, position by
  position (`Data::reservedClusterGaining`,
  `reservedclusters::firstReservedGain`); without reserved clusters it is two
  map lookups.  The atom layer of a hierarchical session is not checked
  (`Data::setProtectsReservedClusters`): an atom takes its parent's id, a
  reserved one included, when it is the parent's only atom.  Decollide refuses
  a reserved basis cluster and skips a collision that names one as a unit.
- **Only reclustering and feature-space selections take spikes out of one.**
  These are the Recluster menu, DipSplit, Watershed and the lasso tools
  (Decollide `D` included).  Every other edit leaves reserved chunk clusters out
  of the clusters it works on: Group, Delete cluster(s), Purge Small Clusters,
  both outlier strips, Partition by Time, the template matrix's Apply, the
  decollide plugin's batch apply, and the hierarchy's parent merges,
  promotions, dissolves and drops.  When the user selected one, the status bar names it
  (`KlustersDoc::leaveOutReserved`); Purge Small Clusters and the
  feature-outlier strip, which run over all clusters, skip them without a
  note.  Strip by Template's whole-scope donor pool leaves them out; a
  selected one still donates.  A Group left with fewer than two clusters runs
  no post-merge automation.
- `klusters_test_reservedclusters` tests the list filters and
  `firstReservedGain`.
- The Klusters manual has a new section, "Reserved chunk clusters".

Files: `src/klusters/src/reservedclusters.h` (new), `data.{h,cpp}`,
`klustersdoc.{h,cpp}`, `klustersdoc_edit.cpp`, `klustersdoc_hierarchy.cpp`,
`klusters.{h,cpp}`, `src/klusters/test/reservedclusters_test.cpp` (new),
`src/klusters/test/CMakeLists.txt`, `doc/klusters/README.md`,
`doc/klusters/hierarchical-clustering.md`,
`doc/ndmanager-plugins/formats/clu.md`.

</details>

## 2026-10-26 — Formats: the .clu header is the number of distinct ids

<details>
<summary>Details</summary>

fiber-kit 0.56.0–0.57.2 wrote the highest id + 1 as the header of the `.clu`
files it wrote.  For the files of a chunk run (`FK_CHUNKS`), whose reserved
clusters sit at 2^24 − 100 … 2^24 − 1, that is about 16.8 million, and Klusters
refused them: "Invalid or missing .clu header (nClusters=16777136)".  fiber-kit
0.57.3 writes the number of distinct ids, as Klusters does on Save, and its
`fiber-clu-header` rewrites the header of a file written before.

- `doc/ndmanager-plugins/formats/clu.md`: the header is the
  number of distinct ids, not the highest id + 1.  KiloKlustaKwik's highest id
  is the same number.  A new section, "Reserved ids", describes the block.
- `formats/clc.md`: the same rule for the
  atom ids.  Klusters loads the atom layer with the `.clu` loader, so the
  0–65536 check applies to a `.clc` too.
- `FILE_FORMATS.md` §3.2 says what the count counts.

No code changes: Klusters' Save, the `.clc` written on Save and
`process_drifttracker` already write the number of distinct ids.
`process_reassignspikes` and Klusters' decollide keep the header of the file
they read.

</details>

## 2026-10-25 — Klusters: new clusters are numbered below the reserved chunk clusters

<details>
<summary>Details</summary>

fiber-kit's chunk runs (`FK_CHUNKS`, fiber-kit 0.56.0) sort only some
chunks of a recording and put every spike of the other chunks in a
**reserved cluster**, one per chunk: ids 16 777 116 – 16 777 215
(2^24 − 100 … 2^24 − 1), the top 100 ids a `float` holds exactly.

- `types.h` defines `kReservedClusterLo`, `kReservedClusterHi` and
  `isReservedClusterId()`.  fiber-kit's `test_shared_contract` checks that
  they match `fiber_chunks.py`.
- `Data::highestClusterId()` skips reserved ids.  With reserved clusters
  loaded it returns the highest ordinary id, so `nextFreeClusterId()`, and
  every split, group, renumber and recluster that uses it, numbers new
  clusters below the block instead of past 2^24.
- Group and split no longer pass the new cluster id through a `float`
  (`klustersdoc_edit.cpp`).  The id is a `dataType`, and the colour hue
  uses integer `%`.

Without reserved clusters nothing changes: the last row of
`spikesByCluster` is still the answer.

Files: `src/libklustersshared/src/klustersshared/types.h`,
`src/klusters/src/data.h`, `src/klusters/src/klustersdoc_edit.cpp`.

</details>

## 2026-10-24 — Klusters: the waveform sorts can use mean waveforms

<details>
<summary>Details</summary>

**Preferences → Curation → Cluster order → Waveform sorts** chooses which
waveform stands for each cluster in Sort by Nearest-Neighbour Waveform
and Sort by Waveform (Global / Spectral):

- **Median waveform** (the default) is robust to the odd artefact spike.
  It holds each cluster's spikes in memory and sorts every sample.
- **Mean waveform** is a single running sum with nothing held, so it is
  lighter on very large clusters.

The setting is `waveformSortMean`, and **Default** resets it to median.
The status message and the curation log's `SORT_ALL` record name the
waveform used. `computeMedianWaveformDistances` is now
`computeWaveformDistances`.

On the synthetic group from the previous entry (80 clusters, 200 000
spikes), the NN sort took 0.70 s of CPU with the median and 0.13 s with
the mean. Both gave the same order, as expected for data without
artefact spikes.

Files: `src/klusters/src/configuration.{h,cpp}`,
`prefclusterorder{.h,.cpp,layout.ui}`, `prefdialog.cpp`,
`klusters_sort.cpp`, `klusters.{h,cpp}`,
`doc/klusters/cluster-sorting.md`, `doc/klusters/README.md`.

</details>

## 2026-10-24 — Klusters: a cluster sort logs one Sort All Clusters entry

<details>
<summary>Details</summary>

Every renumbering sort (**Actions → Sort Clusters** and `Shift+S`) took
most of its time writing the curation log. It logged the sort as a
`RENUMBER_PARTIAL` of every renamed cluster, with a full set of cluster
metrics before and after. Each set takes two passes over all the
group's spikes, including the L-ratio pass, one cluster at a time on
the GUI thread. A sort moves no spikes, so those metrics never changed.

A sort now writes one `SORT_ALL` (**Sort All Clusters**) record: the
sort's name, the cluster counts and the old → new id map
(`"old_to_new":{"12":3,...}`). The record is undo-paired like any other
action, so Ctrl+Z marks it `bad`. Detail values in the log can now be
JSON objects.

On a synthetic group of 80 clusters and 200 000 spikes, Sort by
Nearest-Neighbour Waveform with the curation log on went from 7.6 s of
CPU to 0.65 s. The time saved grows with the number of clusters and spikes.

The `T` key (renumber selected to end) still writes `RENUMBER_PARTIAL`
with snapshots; it renames only the selected clusters.

Files: `src/klusters/src/curationlogger.{h,cpp}`,
`klustersdoc_renumber.cpp`, `klustersdoc.h`, `klusters_sort.cpp`,
`doc/klusters/README.md`, `doc/klusters/cluster-sorting.md`.

</details>

## 2026-10-23 — ndm_* commands: -h / --help everywhere, and no step runs on a flag it does not know

<details>
<summary>Details</summary>

Of the 52 `ndm_*` commands only two answered `-h` and `--help`. Most took the
flag for nothing at all and ran the step on the session named after the
current folder, so `ndm_hipass -h` inside a session folder started filtering
it. NDManager's **Discover Plugins** had the same effect: it runs every
`ndm_*` on the PATH with `--ndm-version`, and the built-in steps ran.

- Every command now prints its description and usage for `-h` / `--help` and
  exits without running anything.
- The built-in steps decline `--ndm-version` / `--ndm-describe` (exit 1,
  nothing run), so plugin discovery skips them safely.
- `ndm_start --help` works (only `-h` did); `ndm_custody`, a library, says so
  when run.
- A new test runs every command with both flags inside a session folder and
  checks that nothing in it changes.

</details>

## 2026-10-22 — Klusters: the Profile template metric survives a restart

<details>
<summary>Details</summary>

Choosing **Profile (inter-channel)** as the template matrix metric lasted
only until Klusters was restarted: on loading the settings it became
**Fast-AP window**. The stored choice is now read back as stored.

</details>

## 2026-10-22 — Klusters: Preferences ▸ Default resets every setting

<details>
<summary>Details</summary>

**Default** in Klusters' Preferences left three template-matrix settings as
they were (the metric and the match slider's two thresholds) and reset
*Use white background when printing* to the current value rather than to
its default (on). It now resets all four, like every other setting.

</details>

## 2026-10-22 — Klusters: Preferences regrouped into General, Views and Curation

<details>
<summary>Details</summary>

The Preferences dialog is now a tree of three groups, and each setting sits
with the others of its kind. Stored preferences carry over unchanged.

| Group | Pages |
|---|---|
| General | Appearance, Session (now also re-extract on save and the curation log), Input, Radial menu |
| Views | Display, Cluster view, Waveform view, **Matrices** (new: when the matrices are computed, error-matrix compute, template matrix metric and slider, drift-slider limit, from Display and Refinement) |
| Curation | **Realignment** (was Refinement), **Splitting** (DipSplit and KNN, from Refinement), **Merging** (recommended merges and Auto-Merge), **Cluster order** (was Sorting; now also renumbering after edits), Reclustering |

The Klusters manual has a new Preferences section with this table, and its
references point to the new pages.

</details>

## 2026-10-21 — macOS: the `ndm_*` steps work with the BSD tools

<details>
<summary>Details</summary>

Several steps used options only GNU tools have. On macOS's BSD tools they
failed or, worse, did something else without saying so.

- `ndm_start` (writing the session date), `ndm_recolorchannels`,
  `process_renameevents_SPWR` / `_TMaze`: `sed -i` without a suffix, which
  BSD sed reads as "the next word is the backup suffix". The event renamers
  also wrote a `t` where a tab belonged, and `_SPWR` needed Perl's `rename`.
- `ndm_aom2dat`: `stat -c` and `grep -P` made it read the channel count and
  sampling rate as missing.
- `ndm_klustakwik`, `ndm_pca`, `ndm_subcluster_unmatched`: `nproc` is not on
  macOS, so they ran on one thread.
- `process_reorderclusters` (`mktemp -p`, `\n` in a sed replacement), `pad`
  (`\+`), and the helpers' `sed -r`.

`ndm_functions` gains `ndm_sed_inplace`, `ndm_ncpus` and `ndm_file_size`;
`scripts/lint.sh` refuses these options in the scripts under `src/`.

</details>

## 2026-10-21 — macOS: the `ndm_*` steps run with Homebrew's bash

<details>
<summary>Details</summary>

macOS's `/bin/bash` is bash 3.2; the `ndm_*` steps need 4 or newer.

- Every script now starts with `#!/usr/bin/env bash`, so it runs with the
  first bash on `PATH` -- Homebrew's, once `brew install bash` and Homebrew's
  shell setup are in place. Running a step as `bash ndm_hipass ...` is no
  longer needed.
- Started with bash 3.2 anyway, a step stops at once and says what to install
  (`ndm_functions`), instead of failing later on bash-4 syntax.
- `scripts/lint.sh` refuses a script that starts with `#!/bin/bash`.

</details>

## 2026-10-21 — New icons for neurosuite-3, Klusters, NeuroScope and NDManager

<details>
<summary>Details</summary>

One family: a navy tile with a recording trace and one spike running off
both edges; above it "NS3" for the suite, three clusters of points for
Klusters, three channel traces for NeuroScope, three processing steps for
NDManager.  Sources and palette: `doc/images/README.md`.

- The installed application icons (hicolor theme, 16–256 px and a
  scalable SVG) and NDManager's icons for the other two programs are the
  new ones.
- Klusters, NeuroScope and NDManager set their own window icon, so it shows
  in the title bar and taskbar on Windows and Linux and in the macOS Dock,
  and name their desktop file, so Wayland desktops find the installed
  icon.
- On Windows the `.exe` files carry the icons too, for Explorer, the Start
  menu and shortcuts.

</details>

## 2026-10-21 — macOS: checking the Metal backend (kernel lint, on-device compile test, `NS_DISABLE_METAL`, `metal_validate.sh`)

<details>
<summary>Details</summary>

Phase 5 of the Apple GPU plan (`doc/design/metal-backend.md` §14).

- `scripts/lint.sh` checks the Metal kernel signatures
  (`scripts/msl_lint.py`) on any platform: mixed scalar/vector position
  attributes, which Apple's compiler refuses, and duplicate buffer indices.
- `metal_kernels_compile` compiles every kernel in the tree on the Mac's
  GPU, so a kernel that does not compile fails one test instead of falling
  back to the CPU unnoticed.
- `NS_DISABLE_METAL=1` makes every program take its CPU path.
- `scripts/metal_validate.sh SESSION NCHANNELS SAMPLERATE [--kk GROUP]`
  compares the Metal and CPU results of each program on a recording.
- The Metal backend is complete; what is left is running
  `metal_validate.sh` on the reference sessions on a Mac.

</details>

## 2026-10-21 — Klusters: Align All realigns as Realign Spikes does

<details>
<summary>Details</summary>

"PCA-Center Align All Clusters" and the automatic realign after an edit
built their own arguments. They took Realign Spikes' threshold, iterations
and maximum shift, but replaced the post-alignment mode with a forced
`--pca-refine` on every cluster. The single-cluster command uses the mode set
in the preferences, and with a top-channel count it uses no post-alignment
pass at all.

So a batch moved spikes differently from the same cluster realigned on its
own, although the README calls it the same alignment. On
`sirotaA-jg-000005-20120316` group 6:
- the refine moved about 90% of each large cluster's spikes, by up to ±30
  samples in total;
- it halved the xcorr score against the cluster template;
- about half of each cluster's spikes ended up away from the template.

Both batch paths now run exactly Realign Spikes' settings, and the
confirmation shows them. The action is renamed **Align All Clusters**
(`Shift+P`, action id unchanged). For the PCA refine, choose it as the
post-alignment mode in Preferences → Refinement.

</details>

## 2026-10-21 — Klusters: Align All and post-edit realigns update the other feature spaces

<details>
<summary>Details</summary>

When a single-cluster realign finishes, the other feature spaces follow the
moved spikes: their `.spk` rows are re-cut and their `.fet` rows re-projected
at the new times. A realign started from another space then returns there.
The batch path, used by PCA-Center Align All and by the automatic realign
after an edit, never did either. The `.clu`'s own space was updated, but every
other space kept the old spike times and waveforms, and the display stayed in
the `.clu`'s space. A user who switched back saw no change.

The batch finish now runs the same follow-and-return step.

</details>

## 2026-10-21 — ndm_pca: `--force` refits and overwrites stale features

<details>
<summary>Details</summary>

`ndm_pca` skipped every group whose `.fet` existed, so features projected from
waveforms that have since been re-extracted could only be replaced by deleting
the files by hand. With `--force` the group is refit, and its `.fet` and `.pca`
are overwritten: for a `_D<lag><dims>` token, both the ordinary and the lag
pair. `--force` is not passed on to `process_pca`.

The refit basis follows the current settings, so the feature axes can move;
a clustering keeps its assignments. To keep the stored basis and only
re-project, use `ndm_refeaturize`, which does not handle `_D` tokens.

</details>

## 2026-10-21 — ndm_extractspikes: a session-file check per method, and `--allMethods`

<details>
<summary>Details</summary>

- **The session file is checked before anything runs.** This covers the
  geometry, channels within `nChannels`, and no one-channel group under a
  spatial order. It also checks that `stderiv_C<n>` has a pattern on every
  group, of order `<n>`, with one entry per channel and no index beyond the
  group, and that every group has a `.res`. Any error stops the step.
  - Until now, a group whose pattern was missing or did not fit was
    differenced all-pairs by the engine under a `_C` name, without a word.
  - `--check` runs the check alone.
- **`--allMethods`.** It re-extracts (overwrites) every group for every
  waveform method the session has `.spk`, `.nst`, `.fet`, `.pca` or `.clu`
  files for, a `_D` feature token standing for its waveform method. Each
  method is checked first; the ones the session file does not cover are
  reported and not run. At the end it lists the feature spaces to re-project.
  `--allMethods --check` only lists and checks.
- **Missing `.res`.** A group without a `.res` now stops extraction with
  "run ndm_detectspikes first". Before, the resolver's "expected path" answer
  was taken for a file, and the step failed with a shell arithmetic error.

</details>

## 2026-10-21 — ndm_detectspikes / ndm_extractspikes: each spike group cut with its own window

<details>
<summary>Details</summary>

The engines take one `-w`/`-p` (waveform length, peak index) for every group
they are given, and the driver passed the first group's values for all of
them, with only a warning. A group whose `nSamples` or `peakSampleIndex`
differed from group 1's got `.spk` windows of the wrong length or offset from
its `.res` by the difference in peak index. Klusters and every other reader
use the group's own values, so such a group looked shifted throughout: its
realign check (2026-10-21) reports the rows as "stale by s", with s that
difference, on every spike.

The driver now runs the engine once per distinct geometry, with that batch's
channels and `sdiffPairs`, and renames each output to its group. Sessions
whose groups all share one geometry run exactly as before (one engine call).

Affected files: `.spk` (and `.nst`) of groups whose geometry differs from the
first group's, written by `ndm_extractspikes`. Detection ran those groups with
group 1's window too; their `.res` is kept as it is. Re-extract them
with `ndm_extractspikes --force`, then re-project their `.fet`.

</details>

## 2026-10-21 — macOS: KiloKlustaKwik on the Apple GPU (CEM steps)

<details>
<summary>Details</summary>

Phase 4 of the Apple GPU plan (`doc/design/metal-backend.md` §13).

- macOS builds now install a `KiloKlustaKwik` with a Metal backend beside
  `KiloKlustaKwik_cpu`. On a Mac whose GPU cannot hold the session it runs
  on the CPU.
- E-, M- and C-steps, the score and the deletion loss run on the GPU in
  float. The M-step uses no atomics, so runs are deterministic (CUDA's are
  not).
- Template matching uses the Metal cross-correlation of Phase 3.
- The time-shift probe is disabled in every build; nothing was ported.
- The macOS CI runs `kk_test_metal_cem`. Clusterings still need comparing
  with `KiloKlustaKwik_cpu` on the reference sessions on a Mac.

</details>

## 2026-10-21 — KiloKlustaKwik GPU builds: the spatial phase of CEMTwoPhase computes the right distances

<details>
<summary>Details</summary>

`KK::CEMTwoPhase` first clusters on the spatial dimensions only: it lowers
`nDims` by one (the time column) while the arrays keep their loaded size.
The CPU E-step and every M-step followed KK's `nDims`; the CUDA, HIP and
SYCL E-steps used the dimensions they were allocated with, so in that phase
they read the means and Cholesky factors with the wrong stride and added
the time column to every distance.  The global preseed of a chunked run
(the master KK has the GPU) and every unchunked run go through this phase.

`gpu_estep` now takes KK's `nDims`/`nDims2`.  On a synthetic session
(20000 spikes, 12 Gaussian clusters) run through the same code path, the
old dimensions put every spike in the noise cluster; the fixed ones find
the 12 clusters.  Not yet run on an NVIDIA, AMD or Intel GPU: expect GPU
clusterings to change, and compare a before/after run on the reference
session.

</details>

## 2026-10-21 — macOS: Klusters on the Apple GPU (realignment, error matrix)

<details>
<summary>Details</summary>

Phase 3 of the Apple GPU plan (`doc/design/metal-backend.md` §12).

- **Realignment cross-correlation:** exact integer sums on the GPU, scored
  on the host as the OpenMP back end does, so shifts and scores are
  bit-identical to OpenMP.
- **Grouping Assistant error matrix (FP32, the default):** within 2e-3 of
  FP64, processed in chunks so it never holds the full posterior matrix.
  FP64 runs on the CPU.  HIP and SYCL builds no longer print "GPU
  error-matrix aggregation failed" on every matrix.
- **PCA refine:** stays on the CPU on a Mac.  The CUDA kernel now handles
  only the raw feature space, where it computes what the CPU computes.
- The macOS CI runs both new tests.

</details>

## 2026-10-21 — Klusters realign: moved spikes get a fresh window from the .fil, never the rolled copy

<details>
<summary>Details</summary>

- **The fault.**  Realign finds each spike's shift on a circularly rolled
  working copy of its window.  Before writing, it should cut a fresh window
  out of the `.fil` at the new time.  It looked for the `.fil` by cutting two
  suffixes off the `.spk` path.  That works only for an untagged, non-pending
  `S.spk.N`.  With the `.pending` copy, which always exists after open, or any
  method-tagged name, no file was found.  Realign then logged a warning and
  wrote the rolled copy.
- **The damage.**  The `.res` moved by the shift *s* but the stored window
  carried |*s*| samples from the other end of the old window.  The `.fet`
  was projected from that rolled window.  Repeated passes accumulated the
  error.  Found on sirotaA-jg-000005-20120312 group 7: 66.7% of the
  `.spk.stderiv_C5.7` windows were rolled.  Sibling `.spk` spaces re-cut by
  the follow were correct.
- **Now:**
  - The raw file is resolved once with `rawRecordingPath()` (the `.fil`, else
    the `.dat`, as re-extract and the follow use it).  The stderiv seed
    detection, the PCA refine and the write all use it.
  - With no raw file, realign stops before computing anything, changes
    nothing and says so.
  - A moved spike gets a fresh cut at its new time.  A spike whose window
    cannot be cut (outside the file, a read error) stays where it was, with
    its stored window, and is reported.  Unmoved spikes are written from the
    stored rows.  The rolled copy is never written.
  - The raw-source check compares the stored row, not the shifted working
    copy.
- **Behaviour changes:**
  - `--pca-refine` now runs; it never did, because it could not find the
    raw file.
  - The raw-source check now runs, and it refuses `.spk` files that are
    already rolled.  Repair them first.
- **Repair of existing files:**
  - **Affected:** every `.spk` a Klusters realign wrote as the open
    clustering's own waveforms, and the `.fet` of every space projected
    from it.
  - **Correct:** the `.res`, and the sibling spaces.
  - **Steps:**
    1. Back up the affected files.
    2. In Preferences → Refinement, turn on "Re-extract spike waveforms from
       .fil on save" and Save.  This rebuilds the `.spk` from the `.fil` at
       the current `.res`.
    3. Re-project the `.fet` PCA columns with each space's own `.pca`.
- **Checked:** with a harness linked against the Klusters objects, on a
  fixture session (4 channels, method-tagged names, the `.pending` copies,
  290 of 426 spikes offset by 1 to 3 samples):
  - after realign, every written `.spk` row equals a fresh cut at its `.res`;
    every `.fet` row equals its projection; every time is back at the truth;
  - the same holds with `--pca-refine`, `--recenter-rms` and a wider shift;
  - a spike whose new window would leave the file stays unmoved;
  - without the `.fil`, realign returns false and the pending files are
    byte-identical;
  - the old code, on the same fixture, leaves 290 rolled rows.

</details>

## 2026-10-21 — ndm_extractspikes: noise statistics for the clusterers' whitener (`.nst`)

<details>
<summary>Details</summary>

- `ndm_extractspikes` writes `<session>.nst.<method>.<group>` beside each
  `.spk`: the mean window and the covariance of random windows drawn clear
  of every spike, cut exactly like the `.spk` rows (same transform, raw /
  sdiff / stderiv with any `sdiffPairs`), with outliers rejected first --
  flat channels, any sample beyond `noiseRejectZ` robust SDs (median / MAD),
  window energy beyond the same.  Parameters `noiseWindows` (default
  20000; 0 = off), `noiseRejectZ` (5), `noiseGuard` (nSamples); the engines'
  flags are `-N` / `-E` / `-G` with `-R`.  A clusterer whitening with it
  uses the noise of the waveforms it sorts, not the `.fil` in another
  domain.
- Format NSTE v1 (`doc/ndmanager-plugins/formats/nst.md`), reader, writer
  and estimator in `core/noise_stats.hpp`; version-tagged in
  `contract.yaml`, which moves to contract 2 (fiber-kit 0.48.0 reads it).
- `nst` is a MethodSpecific custody type (`custody.hpp`, `ndm_custody`,
  `ndm_resolve_io.py`, `custody_vectors.tsv`); Klusters' time sort carries
  it over as a per-group, not per-spike, file.
- Tests: `core_test_noise_stats`, `ndm_test_extract_noise` (the three
  extractors on a synthetic `.fil`), `ndm_test_extract_noise_params` (the
  driver); `core_test_contract` covers NSTE.

</details>

## 2026-10-21 — process_medianfilter: CPU builds no longer corrupt recordings larger than one chunk

<details>
<summary>Details</summary>

- **Who is affected:** builds without CUDA (macOS, Windows, CPU-only Linux)
  that ran the median high-pass on recordings larger than one chunk (`-b`,
  default 128 MiB) since 2026-02-20 (commit `8856170c`).  That is
  `ndm_hipass` with `filter: median`, `ndm_redetectspikes` and
  `ndm_bandpass`.  From the first chunk boundary on, every chunk began with
  `w` frames of uninitialised memory and was shifted by `w` frames.  Those
  `.fil` files should be regenerated.  The default Butterworth filter is not
  affected.
- **Also fixed:** the CPU window was one frame off-centre, and the last frames
  read past the buffer.  The CUDA path had its own loop, which could use
  already-filtered samples as context at a chunk boundary and read garbage
  after the end of the file.  It now filters the CPU path's chunks.
- **Definition:** `out[t] = x[t] - median(x[t-w .. t+w])`, with zero outside
  the recording, stated in the source.  `test_medianfilter.py` checks it
  against numpy for one chunk, chunks of 37 frames, chunks of one frame and
  chunks smaller than a frame, and checks the GPU output against the CPU
  output byte for byte.

</details>

## 2026-10-21 — macOS: Metal paths for the high-pass filters and channel grouping

<details>
<summary>Details</summary>

Phase 2 of the Apple GPU plan (`doc/design/metal-backend.md` §11).

- **`process_medianfilter`:** byte-identical to the CPU.
- **`process_butterworth`:** single precision (`-p double` runs on the CPU),
  within 1 LSB of the CPU's single precision.  New `test_butterworth.py`:
  - CPU double equals scipy's `sosfiltfilt`;
  - the output does not depend on chunking;
  - single precision is within 1 LSB of double;
  - the GPU output is within 1 LSB of the CPU's.
- **`process_spikegrouper`:** noise medians and coincidence counts on the
  GPU, giving the same channel groups as the CPU.
- **`process_decomposecollisions`:** not ported.  Its CUDA GEMMs compute
  little that is used (§11), so there is nothing to accelerate.
- The macOS CI runs all four plugin tests.

</details>

## 2026-10-21 — macOS: a Metal (Apple GPU) backend, first for the noise thresholds

<details>
<summary>Details</summary>

Phase 1 of the Apple GPU plan (`doc/design/metal-backend.md`).

- **Build:** `USE_METAL` (default `ON`, macOS only).  Configure fetches Apple's
  metal-cpp headers from GitHub at a pinned commit (`NS_METAL_CPP_DIR` for an
  offline copy).  Kernels are embedded as source and compiled by the OS at run
  time, so neither full Xcode nor the Metal compiler is needed.
- **`Neurosuite::metal`:** the shared device context (`MetalContext`): pipeline
  compile and cache, shared CPU/GPU buffers, error reporting.  Tested by
  `core_test_metal`.
- **`process_medianthreshold`:** the per-channel medians on the GPU, exactly
  equal to the CPU path's (checked by `ndm_test_medianthreshold_metal`).  With
  no Metal device, or on a GPU error, the CPU computes them.
- **Found, not changed:** the CUDA path of `process_medianthreshold` differs
  from its CPU path.  For an even sample count it takes the upper middle value
  (the CPU averages the two middle values), and it treats -32768 as the largest
  value.
- **Not yet shown:** nothing here has run on a Mac.  The host code was
  syntax-checked against the real metal-cpp headers; the macOS CI job is the
  first build, and its two Metal tests print SKIPPED when the VM has no GPU.

</details>

## 2026-10-21 — macOS: Apple Accelerate for the spectrogram FFT and the EMG kernel

<details>
<summary>Details</summary>

Phase 0 of the Apple GPU (Metal) plan (`doc/design/metal-backend.md`): the CPU-side
work, using Apple's Accelerate framework, part of macOS.  New CMake option
`NS_USE_ACCELERATE` (default `ON`, macOS only).

- **Spectrogram FFT (vDSP).**  `RealFftPlan` gains a third provider beside
  FFTW and the in-tree radix-2 transform: vDSP's real DFT, at sizes 2^n and
  f·2^n (f = 3, 5, 15).  Auto order: FFTW at the exact size, vDSP at the
  exact size, else the next power of two with vDSP or radix-2.  The Homebrew
  build has no FFTW, so on a Mac NeuroScope's spectrogram moves from radix-2
  to vDSP; with FFTW installed nothing changes unless `NS_SPECTRAL_FFT=vdsp`
  (also `fftw`, `radix2`).  Each plan checks vDSP's packing and scale against
  a direct DFT at start-up and falls back, with one message, if they differ
  from Apple's documentation.
- **EMG kernel (BLAS).**  `process_extractemg --backend accelerate`: the
  FastICA pow3 step as two `cblas_dgemv` per cache-sized block of rows, so
  the data are still read once per step.  Not taken by `--backend auto`
  until a Mac benchmark shows it ahead of the OpenMP kernel.
- **Tests.**  `core_test_spectral` checks every built FFT provider against a
  direct DFT, re-planning, concurrent use and the override, and times each;
  the new `ndm_test_extractemg_pow3` checks the CPU and Accelerate kernels
  against a direct evaluation and times both (`--bench N nch` for a real
  size).  The macOS CI job runs both as a required step, then a timing run.
- **Not yet shown:** none of this was compiled against the real Accelerate
  when written (no Mac): the vDSP and BLAS paths were exercised on Linux
  against a mock header written from Apple's documentation.  The macOS CI
  job is the first real build; timings need an Apple-silicon Mac.

</details>

## 2026-10-21 — CI: one workflow per operating system, with a status badge each

<details>
<summary>Details</summary>

- `ci.yml` is split into `ci-linux.yml` (lint, and the Ubuntu build-and-test
  matrix), `ci-windows.yml` and `ci-macos.yml`.  GitHub's badges are per
  workflow, so the README now shows Linux, Windows and macOS separately,
  next to Docs.
- The Windows and macOS jobs no longer `continue-on-error`: in their own
  workflows they hold up nothing else, and their badges show the real
  result.  macOS's test step stays informational for now (the `ndm_*`
  scripts use `#!/bin/bash`, macOS's bash 3.2), so its badge reports the
  build.
- All three can also be started by hand (`workflow_dispatch`).

</details>

## 2026-10-21 — CI: a macOS build

<details>
<summary>Details</summary>

- New `macos` job in `ci.yml`: macOS 15 on Apple silicon, Apple clang,
  Homebrew Qt 6, yaml-cpp, GSL, libomp, libsamplerate, FFmpeg, HDF5 and
  bash, as `doc/INSTALL.md` installs them.  GPU backends off,
  `NS_NATIVE_ARCH=OFF`, every target built (`-k 0` lists all errors).
- Non-blocking for now: the job is `continue-on-error` until it has passed
  once, and its test step is informational (the `ndm_*` scripts use
  `#!/bin/bash`, macOS's bash 3.2).

</details>

## 2026-10-21 — Klusters: the curation log is `<base>.cur.<method>.<group>`

<details>
<summary>Details</summary>

- The curation log was `<base>.curation_log.<group>.<method>`, group before
  method, unlike every other per-group file.  Klusters now writes
  `<base>.cur.<method>.<group>[.<stage>]`, named for the `.clu` it records,
  stage included (`rat.clu.stderiv.5.final` → `rat.cur.stderiv.5.final`).
  Opening an unstaged `.clu` renames an existing log under the old name, so
  the session keeps appending to it.  Which sessions are logged is
  unchanged (not those sorted under `standard`).
- `ndm_autoklusta` (`curationLog: auto`) finds the new name first (unstaged,
  then staged) and the old one otherwise; `kk_build_prior.py`, the template help and the docs use
  the new name.  `cur` stays outside the custody type tables, like `.clp`.

</details>

## 2026-10-19 — Pipeline: the KiloKlustaKwik calls match KiloKlustaKwik

<details>
<summary>Details</summary>

KiloKlustaKwik ignores options it does not know, so mismatches went
unnoticed.  Every script that runs it was checked against its registered
options and its file names:

- `ndm_klustakwik` and `ndm_subcluster_unmatched` passed `-Phase15Iters` and
  `-SubspaceReclusterDepth`, which KiloKlustaKwik no longer has.  The
  parameters `phase15Iters` and `subspaceReclusterDepth` are removed; no sort
  changes (they were ignored).
- `-UseFeatures` was a hand-counted mask: one column too long by default
  (a warning per group), too short with `ndm_pca` `extra: true`, which dropped
  the timestamp the chunked CEM partitions by.  `ndm_klustakwik` and
  `ndm_applydrift` now pass `all`; `ndm_klustakwik` reads a `useFeatures`
  parameter (default `all`) for a mask.
- `ndm_subcluster_unmatched` and `ndm_applydrift` (`runKlustaKwik: true`) ran
  `KlustaKwik`, which is not installed; they run `KiloKlustaKwik`.  The
  sub-clustering sandbox uses the method-tagged names KiloKlustaKwik reads and
  writes, with `-Method`.
- `KlustaKwikRefine` refines `<session>.clu.<method>.N` (new `--method`,
  default `standard`) and passes `-Method`; it took the untagged `.clu.N`
  while KiloKlustaKwik wrote `.clu.standard.N`.  A failed run now restores the
  seed (`set -e` ended the script before the restore).
- `test_kk_options.py` checks every KiloKlustaKwik command line in the
  scripts against the options the program registers.

</details>

## 2026-10-19 — NDManager: Add offers the parameters a plugin reads

<details>
<summary>Details</summary>

- **Add** on a plugin's page (expert mode) opened an empty row, so a
  parameter could only be added by knowing its name; the ones the template
  leaves commented out (e.g. `ndm_hipass` `lowPassHz`, `order`, `precision`)
  were not visible anywhere in NDManager. It now opens a list of the
  parameters the plugin reads that the page does not show, from the catalog
  generated from `templates/template.yaml`, with the template's value and the
  parameter's help; several can be added at once. A commented-out parameter is
  added empty (the plugin's default): its commented value can be an example
  (`lowPassHz` 6000, where the default is 0). **Other Name…** keeps the empty
  row; plugins the template does not describe get it directly.
- Hovering over a parameter's name shows its part of the plugin's help.

</details>

## 2026-10-19 — Docs: every page checked against the code; current in-app handbooks; the site covers all documentation

<details>
<summary>Details</summary>

- **Review.** All ~220 documentation files (Markdown, DocBook handbooks and
  man pages; translations excluded) were checked claim by claim against the
  code: 121 high, 199 medium and 167 low-severity findings. The high ones told
  users to do things that fail: KiloKlustaKwik options that do not exist and
  are silently ignored (`-ChunkBoundaries`, `-SubspaceReclusterDepth`,
  `-ForceCPU`) and most of its defaults wrong; install guides missing yaml-cpp
  or GSL, so configure failed; workflow commands that cannot run (no
  `ndm_detectspikes`, methods that do not match, a non-existent
  `--threshold-factor`, the never-read `extraInfos` override); Klusters and
  NeuroScope keys that run a different action; NDManager described as running
  plugins; XML parameter files, KDE menus and Qt 4 builds.
- **Fixes**, one commit per file group: the Klusters, NeuroScope, NDManager,
  KiloKlustaKwik, GPU, plugin, man-page, workflow, developer and design
  documentation now describe the programs as they are. Historical notes carry
  dated status banners instead of being rewritten. Duplicated build
  instructions point to `doc/INSTALL.md`.
- **In-app handbooks (F1).** All three programs opened KDE-era HTML from
  2006-2011 (and NeuroScope's and NDManager's rules installed it where F1 did
  not look). NeuroScope now shows its handbook generated from the DocBook by
  `doc/site/gen.sh`; Klusters and NDManager show their Markdown manuals,
  rendered by the viewer. The pre-built HTML trees are removed.
- **Site.** Menu references and the 20 screenshots render again. The site now
  carries the whole user documentation (installation, Klusters, NDManager,
  pipeline, KiloKlustaKwik, workflows, formats, developer pages, changelog);
  links to source files go to GitHub. The docs workflow fails when the
  committed handbook output is stale.
- Code issues found during the review (not fixed here) are listed in the
  project's audit notes.

</details>

## 2026-10-19 — Docs: one installation guide for every platform

<details>
<summary>Details</summary>

- New `doc/INSTALL.md`: installing neurosuite-3 on Linux, Windows (the
  install-deps script, the `windows` / `windows-sycl` presets, PATH, the
  `ndm_*` launchers, WSL2 for KiloKlustaKwik, updating, uninstalling, a
  troubleshooting table) and macOS, with the status of each platform.
- The Klusters, NeuroScope and NDManager install pages (Linux, macOS,
  Windows) described building each program on its own after
  libklustersshared. That no longer worked (libneurosuite-core was never
  built) and is not how the project is built. They now point to INSTALL.md,
  keep the program's own notes and give the skip options for building one
  program. Two stale claims went with them: NeuroScope has no FFmpeg option,
  and no `.app` bundle or `uninstall` target is made.
- ndmanager-plugins' Windows guide leads with the native build and the `.bat`
  launchers; the KiloKlustaKwik Windows guides say it is not yet part of the
  Windows build.
- The handbook's Installation chapter (Qt 4.8, sourceforge, `make`) now
  describes the current build; `doc/site/gen.sh` regenerated
  `07-installation.md` (only that page changed). README, `doc/README.md` and
  BUILD.md link to INSTALL.md.

</details>

## 2026-10-19 — Windows: OpenMP code that MSVC accepts; the CI build gets further

<details>
<summary>Details</summary>

- The `windows` CI job (MSVC `cl`, which implements OpenMP 2.0) stopped on
  OpenMP constructs from later versions; Intel `icx` and GCC accept them,
  which is why local `icx` builds succeeded. Fixed where the log showed them:
  - Klusters' template, residual and drift matrix threads and the grouping
    assistant named data members in `default(none)` clauses (C3028). With
    MSVC they take a clause-free `parallel for`: everything is shared, as the
    members were, and the per-thread copies were only of `const` locals.
    Other compilers keep `default(none)`.
  - `klusters_sort`: the degree maximum used `reduction(max)` (C7660); it is
    now taken after the parallel loop, for every compiler.
  - The spectral engine's `#pragma omp simd` (C7660) is skipped for MSVC.
  - `process_extractemg` called `omp_set_max_active_levels` (OpenMP 3.0); with
    OpenMP 2.0 it calls `omp_set_nested(0)`.
- Checked on Linux: full build and tests, and the same with the MSVC branches
  forced on under GCC. The CI log showed these errors only up to the point
  where the build stopped (685 of 792 steps), so the next run may show more.

</details>

## 2026-10-19 — Windows: the ndm_* steps run from cmd and PowerShell through Git Bash

<details>
<summary>Details</summary>

- Typing `ndm_hipass` in PowerShell opened Windows' "Select an app" dialog:
  the steps are extension-less bash scripts. A Windows install now puts a
  `.bat` launcher beside each script (`src/ndmanager-plugins/cmake/NdmWindowsLaunchers.cmake`)
  that runs it with Git for Windows' bash, or with Python for the
  `#!/usr/bin/env python3` ones (`ndm_xml2yaml`, `process_mergespikes`, ...).
  The sourced libraries (`ndm_functions`, `ndm_custody`) get none, nor does
  `process_spikegrouper`, which has an `.exe` of the same name.
- `ndm-env.bat` finds bash (`NS_BASH`, then `Program Files\Git`, the per-user
  Git, or `..\bin\bash.exe` beside `git.exe`, never WSL's `System32\bash.exe`)
  and Python (`NS_PYTHON`, `py -3`, then `python.exe` unless it is the
  Microsoft Store placeholder), and puts the bin directory and `bin\ndm-bash`
  first on PATH. `bin\ndm-bash\python3` gives the scripts the `python3` they
  call, which the Windows Python installer does not provide.
- The script is passed as `C:/.../ndm_hipass`: the scripts name themselves
  with `basename $0`, which splits on `/` only, and read their parameters by
  that name.
- The scripts are installed with LF line endings: Git for Windows checks text
  out as CRLF by default, and bash stops on the `\r`.
- Checked under Wine's cmd with stand-in bash / Python programs: arguments
  (including a quoted one with a space) arrive intact, the script path has
  forward slashes, each detection order picks the expected program, the Store
  placeholder is skipped, and a missing bash is reported. Not yet run with
  real Git Bash, so the scripts themselves are untested on Windows.

</details>

## 2026-10-19 — Windows: the install puts the programs on PATH

<details>
<summary>Details</summary>

- `cmake --install` on Windows now adds `<prefix>\bin` to the current user's
  PATH, so Klusters, NeuroScope, NDManager and the `process_*` programs start
  from any prompt opened afterwards. `cmake/NsWindowsPath.cmake` runs
  `scripts/windows-path.ps1 -Add` after the DLL deploy step.
- The script edits `HKCU\Environment` only (no administrator rights), keeps
  PATH a `REG_EXPAND_SZ` with its `%VARIABLES%` unexpanded, and records the
  directory in `NEUROSUITE3_BIN`: installing again to the same prefix changes
  nothing, to another prefix replaces the old entry. `-Remove` takes it off,
  `-Show` reports it. Off with `-DNS_ADD_TO_PATH=OFF`; skipped for a staged
  install (`DESTDIR`); a failure is a warning, never an install failure.
- Checked: the PATH editing logic against nine cases (append, already
  present in another spelling, moved prefix, removal, empty entries) in
  PowerShell 7; the install step with a Windows (MinGW) toolchain: run on
  install, skipped with `DESTDIR` and with the option off, and a failing
  script only warns. The registry write itself has not run on Windows yet.

</details>

## 2026-10-19 — Windows: one script installs the build dependencies

<details>
<summary>Details</summary>

- `scripts\install-deps-windows.bat` (a wrapper for
  `scripts/install-deps-windows.ps1`, Windows PowerShell 5.1 or later) sets
  up a Windows machine from nothing, skipping what is already installed: Git
  and the Visual Studio 2022 Build Tools with the C++ workload (winget), Python
  when Qt has to be downloaded, vcpkg with yaml-cpp, GSL, libxml2 and
  libsamplerate (HDF5 and FFmpeg on request), and Qt 6.8.3 for MSVC through
  aqtinstall, with no Qt account. It writes a git-ignored
  `CMakeUserPresets.json`: `cmake --preset windows` (MSVC) or
  `windows-sycl` (Intel `icx`), installing to `C:\ns3`. `-DryRun` prints
  what it would do. The vcpkg inside Visual Studio (`VC\vcpkg`, the
  `VCPKG_ROOT` of its developer prompts) cannot install packages outside a
  `vcpkg.json` manifest, so it is passed over for `C:\vcpkg`.
- `-DNS_INSTALL_DEPS=ON` on Windows runs the same script for the libraries and
  Qt. Before, it set `CMAKE_TOOLCHAIN_FILE` after `project()`, where it has no
  effect, so the vcpkg packages it installed were not found; it now puts the
  vcpkg tree and Qt on `CMAKE_PREFIX_PATH`. It no longer builds FFmpeg by
  default (`-DNS_DEPS_FFMPEG=ON`, `-DNS_DEPS_HDF5=ON`) and installs Qt only
  when CMake finds none (`NS_QT_ROOT`, `NS_QT_VERSION`).
- Checked: PowerShell 7 parses the script; dry runs, and runs against stand-in
  vcpkg and Qt directories (including a failing vcpkg, which stops it with the
  exit code), take the expected steps; CMake lists the generated presets; the
  CMake branch, configured with a Windows (MinGW) toolchain against the
  stand-ins, ran the script and set the prefix path. Not yet run on Windows.

</details>

## 2026-10-19 — Windows: the ndmanager-plugins programs build natively, and CI checks it

<details>
<summary>Details</summary>

- The plugins' C++ programs used POSIX calls with no Windows equivalent and
  were skipped in Windows builds. They now build with MSVC; the `ndm_*`
  steps are bash scripts and still run under WSL2 (Git Bash is the next
  step).
- libneurosuite-core gains the portable pieces the programs share:
  `MappedFile` (`mmap`, or the Windows file mapping), `fileSize64()`,
  `fileops.hpp` (`replaceFile`, which replaces an existing file as
  `rename` does on POSIX; `syncFile`; `processId`) and `strsep.hpp`.
- In the programs: 64-bit file positions through `fileio64.hpp`; `MappedFile`
  and stdio instead of `mmap` / `pread` / `stat`; binary-mode streams for raw
  data (text mode on Windows rewrites `\n` and stops at 0x1A, so
  `process_extractchannels` wrote an empty file); no glibc `strdupa`,
  `M_PI` or `libm` on Windows; no OpenMP 3.1 `reduction(max)` and no
  variable-length array, which MSVC rejects. Klusters' saves use
  `replaceFile` too.
- `process_resample` compiles its bundled libsamplerate with CMake on
  Windows (or takes vcpkg's); `process_decomposecollisions` installs a copy
  instead of a symbolic link there; `process_extractemg` takes its CPU path
  in a Windows SYCL build; `cmake --install` also copies the plugins' vcpkg
  DLLs (GSL, libxml2, yaml-cpp).
- CI: the `windows` job builds the plugins (vcpkg GSL and libxml2 added;
  HDF5 and FFmpeg left out, so `process_aomconvert` and
  `process_extractleds` are skipped).
- Checked on Linux (Ubuntu 24.04): full build and tests pass, and the
  outputs of `process_butterworth`, `extractspikes`, `reextractspikes`,
  `reextractspikes_stderiv`, `extractemg` and `alignspikes` on a synthetic
  session are byte-identical before and after. MinGW-w64 builds of the same
  programs, and of `process_resample`, run under Wine give the same bytes as
  Linux. Not yet built with MSVC outside CI.

</details>

## 2026-10-19 — Windows: `cmake --install` gives a folder that runs

<details>
<summary>Details</summary>

- On Windows the installed Klusters, NeuroScope and NDManager did not start:
  the install put the programs and the project's two DLLs in `bin`, but not
  Qt, yaml-cpp or the oneAPI runtime. `cmake --install` now also runs Qt's
  `windeployqt` on the installed programs, copies the other DLLs they link
  (vcpkg's yaml-cpp; with `icx`, the oneAPI runtime) and, for a SYCL build,
  the SYCL runtime adapters (`ur_*.dll`), which are loaded by name and found
  by no dependency scan.
- `cmake/NsWindowsDeploy.cmake` (configure time) and
  `NsWindowsDeployInstall.cmake` (install time). Windows only; off with
  `-DNS_DEPLOY_RUNTIME=OFF`. A missing tool or DLL is a warning, never an
  install failure.
- Checked on Linux: configure unchanged (the step is not added); the
  install-time script, run in script mode on the Linux Klusters and NeuroScope
  binaries, copied their libraries from the given directories and the adapter
  files, skipped the program not installed, and warned for the missing
  `windeployqt`. Not yet run on Windows.

</details>

## 2026-10-19 — CI: GitHub actions moved to their Node.js 24 releases

<details>
<summary>Details</summary>

- GitHub forces Node.js 20 actions onto Node.js 24 and warns. `ci.yml` and
  `docs.yml` now use `actions/checkout@v7`, `actions/setup-python@v7`,
  `actions/configure-pages@v6`, `actions/upload-pages-artifact@v5` and
  `actions/deploy-pages@v5` (all Node.js 24 or composite; inputs and
  permissions unchanged). `ilammy/msvc-dev-cmd@v1` has no Node.js 24 release
  yet and stays.
- The docs deploy still needs GitHub Pages turned on for the repository
  (Settings ▸ Pages ▸ Source: GitHub Actions); no workflow change can do that
  with the default token.

</details>

## 2026-10-19 — Windows: fixes from the first Windows configure

<details>
<summary>Details</summary>

The first configure on Windows (MSVC environment, oneAPI 2025.1 `icx`, Qt 6.8,
vcpkg) stopped on two errors:

- `Cannot find source file: WIN32` for NeuroScope and NDManager: their
  `add_executable` passed `WIN32` through a generator expression, which CMake
  reads as a file name. Both now use the plain keyword, like Klusters.
- Intel's own `IntelSYCLConfig.cmake` in oneAPI 2025.x fails on Windows
  (`separate_arguments` / `SYCL_FEATURE_TEST_EXTRACT`; a known oneAPI bug).
  On Windows Klusters no longer loads Intel's IntelSYCL / IntelDPCPP packages;
  it already sets `-fsycl` itself, and now links the SYCL runtime import
  library (`sycl8.lib`, or `sycl7.lib` for 2024) directly.
- The first Windows build then failed in three Klusters sources inside Qt's
  `qmetatype.h` ("constexpr variable 'begin' must be initialized by a
  constant expression", from `qvariant_cast<QIcon>` in `qtablewidget.h` /
  `qlistwidget.h`). `-fsycl` was on every Klusters source, so each got a SYCL
  device pass that re-reads Qt's headers, where Qt's `__FUNCSIG__`-based
  type-name parsing fails. On Windows `-fsycl` now applies only to the two
  SYCL sources (`groupingassistant_sycl.cpp`, `realign_xcorr_sycl.cpp`, both
  Qt-free); the link keeps `-fsycl` and, with Ninja, runs through `icx`.

- The next build stopped on two more Windows-only gaps. NDManager linked
  `yaml-cpp` without finding the package itself (it now does), so Windows
  looked for a bare `yaml-cpp.lib`. NeuroScope called glibc's `fseeko64`,
  and Klusters `fseeko` / `ftello` with `off_t` offsets: MSVC has none of
  these, and its `off_t` is 32-bit. A new header,
  `neurosuite/core/fileio64.hpp`, gives `fseek64` / `ftell64` with
  `std::int64_t` offsets (`fseeko` / `ftello` on POSIX, `_fseeki64` /
  `_ftelli64` on Windows); Klusters and NeuroScope use it throughout.

Linux configures and builds as before.

</details>

## 2026-10-18 — Klusters: the Intel GPU (SYCL) backend can be configured with icx on Windows

<details>
<summary>Details</summary>

- Klusters' SYCL setup recognised the oneAPI compiler only by the name
  `icpx`, and otherwise switched the cached compiler to `icpx` part-way
  through configuring. On Windows the oneAPI compiler for an MSVC/Qt build is
  `icx`, so SYCL could not be enabled there. It now goes by the compiler id
  (`IntelLLVM`, covering `icx` and `icpx`); on Windows it never switches the
  compiler and says to configure with `-DCMAKE_C_COMPILER=icx
  -DCMAKE_CXX_COMPILER=icx`. Linux behaves as before.
- `ZenOptimizations.cmake` passes `-funroll-loops` / `-march=native`, and
  overrides the archiver with `llvm-ar`, only for GNU-style drivers, not for
  `icx`'s MSVC-style command line. GCC builds are unchanged.
- Not built here (no oneAPI on Windows available); checked that a GCC
  configure on Ubuntu 24.04 produces the same flags as before.

</details>

## 2026-10-18 — Windows: the GUI programs build with MSVC, and CI checks it

<details>
<summary>Details</summary>

- The Windows guides were never checked by CI, and the GUI code no longer
  compiled with MSVC. Removed: Klusters' unused `gettimeofday` timer
  (NeuroScope's runs on `std::chrono::steady_clock`); four unused
  `<unistd.h>` includes; `M_PI` (now `std::numbers::pi`, bit-identical).
- Ported: Klusters' save path (file stamps, flushes, `markInSync`'s mtime)
  and libklustersshared's terminal progress bar have Windows branches; the
  POSIX code is unchanged. `filecommit_test` now checks a stamp's fields.
- New CI job `windows`: Klusters, NeuroScope, NDManager and their libraries
  with MSVC 2022, Qt 6.8 and vcpkg's yaml-cpp; build only, non-blocking until
  its first green run. KiloKlustaKwik and ndmanager-plugins stay POSIX-only
  (WSL2 on Windows).
- Checked: full Linux build and tests on Ubuntu 24.04 (Qt 6.4); the Windows
  branches cross-compiled with MinGW-w64 and run under Wine (file-commit and
  hierarchy tests pass; a deliberately broken stamp fails). MSVC itself has
  not compiled this yet; the new job's first run will.

</details>

## 2026-10-18 — CI builds and tests on Ubuntu 24.04, 25.10, 26.04 and 26.10

<details>
<summary>Details</summary>

- The `build` job runs once per Ubuntu release, each in that release's
  `ubuntu:<release>` container with its own distribution packages: 24.04 (the
  reference, Qt 6.4), 25.10 (Qt 6.9), 26.04 LTS (Qt 6.10) and 26.10 (Qt 6.11).
  The newer releases bring GCC 15, CMake 4, Python 3.13/3.14 and NumPy 2.
- 25.10 reached end of life on 2026-07-09. Its job does not fail the run, and
  its apt sources fall back to old-releases.ubuntu.com once the archive moves.
- Checked before committing by running the job's steps in a root filesystem of
  each release, as root like the containers: 24.04, 26.04 and 26.10 build and
  pass all 82 tests (`kk_test_per_channel_split` is disabled) with no code
  changes.
- BUILD.md §1 and §8, CONTRIBUTING.md and the roadmap describe the matrix; Qt
  6.12 still has no job.

</details>

## 2026-10-18 — CI installs SciPy; the plugins' SciPy dependency documented as it is

<details>
<summary>Details</summary>

- With the build fixed, CI's test step failed on `ndm_test_adaptmodel`: the
  waveform-adaptation fit (`adaptmodel.py`, used by `process_reassignspikes`)
  imports `scipy.optimize` for its selftest, and the CI job installed only
  NumPy and PyYAML. CI now installs `python3-scipy`.
- The dependency table called SciPy optional, "sub-sample refinement in
  `ndm_estimatedrift`". `ndm_spikegrouper`, `ndm_autoklusta` and the adaptation
  fit need it; `ndm_estimatedrift`, `ndm_localise` and `process_subtractspikes`
  use it with a fallback. The table says so, and the Linux, macOS and Windows
  install guides now install NumPy and SciPy (they listed only PyYAML).
- Checked in a Python matching the runner (3.12.3, NumPy 1.26.4, SciPy 1.11.4,
  PyYAML 6.0.1): all 20 ndmanager-plugins tests pass; with SciPy hidden,
  `ndm_test_adaptmodel` fails exactly as in CI.

</details>

## 2026-10-18 — CI builds again: two libklustersshared tests missed the export header

<details>
<summary>Details</summary>

- `klustersshared_yamlformat_test` and `klustersshared_sessiontemplate_test`
  compile `yamlformat.cpp` / `sessiontemplate.cpp` directly, and their headers
  include `libklustersshared_export.h` (since 2026-10-15 / 10-16). That header
  is in `src/libklustersshared/src/`, which neither test had on its include
  path, so every CI build stopped there (the tests are built only with
  `NS_BUILD_TESTS=ON`, as CI does). Both now include `src/`.
- Reproduced the CI job locally (ubuntu-24.04, Qt 6.4.2, its package list,
  configure flags and fiber-kit checkout): before, the whole tree built except
  these two targets; after, it builds and `ctest` passes (82 tests, one
  disabled in the repository).

</details>

## 2026-10-18 — Docs: roadmap and collision-decomposition workflow brought up to date

<details>
<summary>Details</summary>

- `doc/workflows/collision-decomposition.md` described the pre-Klusters state:
  file names in the retired `.col.N` form, a `ndm_decomposecollisions --commit`
  flag that does not exist, viewer keys I / D as "info" / "delete" (they scale
  the waveforms), and viewer decisions as if they fed back into the `.col`
  (they go to `SESSION_decisions.csv`). It now describes both routes — *Plugins ▸
  process_decomposecollisions* in Klusters for the open group, and the shell
  command for every group — and step 4 as Klusters' decollide: `.eap` / `.tcl`
  membership on the current stage plus a new stage carrying the decomposed
  spikes.
- `ROADMAP.md`: the "considered" entry calling `process_decomposecollisions` an
  offline tool with no GUI hookup is replaced by a Done entry (Klusters has run
  it from the Plugins menu since 2026-10-02); Done entries for the radial menu
  and the action-icon standard; the live-pass item lists the radial-menu and
  icon-theme checks still owed on a real desktop.

</details>

## 2026-10-18 — Klusters: one table for action icons

<details>
<summary>Details</summary>

- **The standard** ([STANDARDIZATION.md](doc/dev/STANDARDIZATION.md) §2.11):
  an action's icon is a row in one table keyed by the action's stable id
  (`actionid.h`), applied in one pass — no `setIcon()` where the action is
  created. Generic actions use the desktop theme's icon by freedesktop name
  with the bundled copy as fallback; Klusters' own icons load as
  `:/icons/<alias>` with lower-case, hyphenated aliases; icons painted in code
  take their outline from the palette. Recipe: [RECIPES.md](doc/dev/RECIPES.md)
  §2.6.
- Klusters: `actionicons.h/.cpp` holds the 25 icons that were set at 25
  creation sites in `klusters.cpp`; `KlustersApp::applyActionIcons()` sets them
  at start-up and again after Preferences ▸ Apply. `actionid.h` /
  `klusters_actionids.cpp` take the action ids out of the radial-menu code so
  both use the same names.
- `klusters-icons.qrc` gives every icon a descriptive alias
  (`delete_artefact_tool.png` → `delete-artifact-spikes`, `backCluster.png` →
  `previous-spike`, …); the files are unchanged.
- Fix: *File ▸ Renumber and Save* never had an icon — it pointed at
  `filesave.png` in the working directory. It now uses `document-save`.
- *File ▸ Quit* asks the theme for `application-exit` (the freedesktop name for
  quitting), falling back to the bundled `window-close` it showed before.
- The two multi-lasso icons draw their loops in the palette's text colour, so
  they stay visible in the dark theme.
- New test `klusters_test_actionicons`: every row against the icons the
  program ships (both `.qrc` files compiled in) and the drawn icons' palette
  dependence; it fails on a renamed-away alias and on a fixed outline colour
  (negative controls). A build with `-DNS3_VERBOSE=1` logs table rows whose
  action is missing and toolbar buttons without an icon.

</details>

## 2026-10-18 — Klusters: edit the radial menu in Preferences

<details>
<summary>Details</summary>

- **Preferences ▸ Radial menu** edits the right-click wheel: add any menu-bar
  action (searchable catalogue by menu, plus the X / Y feature-axis wedges),
  drag rows to reorder them or move them between groups, add / rename / remove
  groups, give actions short wedge labels, and restore the default. A preview
  below the tree shows the wheel and opens the selected group's ring. Apply or
  OK rebuilds the wheel for the next right-click.
- The wheel's tree is now data (`radiallayout.h`): JSON in the Klusters
  settings (`radialMenu/layout`; absent = the shipped default, so a later
  change to the default reaches users who never edited theirs). Actions are
  named by their input-registry id (e.g. `sort.bySnr`) or, where they have
  none, by menu path (`menu:Tools/New Cluster`); an id that no longer resolves
  is skipped and shown in red in the editor. The shipped default is the wheel
  as before (Tools, Clusters, Recluster, Undo, Redo, Hierarchy, Sort, X, Y).
- `RadialMenu` gains an embedded mode (an ordinary child widget for the
  preview: hover opens rings, nothing is picked), a ring-depth limit, and an
  open path (the rings shown when the cursor is elsewhere).
- New test `klusters_test_radiallayout`: JSON round trip, settings fallback,
  malformed / wrong-version rejection, unknown kinds skipped, ring counting,
  menu-path ids.
- Tested in the running app on Xvfb (Qt 6.4.2), synthetic session, xdotool
  input: the default wheel resolves every id; removing a group, adding an
  action from the catalogue, renaming a group, a wedge label, and a drag into
  a group all show in the preview and, after OK, in the real wheel; the added
  action triggers; the layout survives a restart; Restore Default + OK
  removes the stored key and brings the shipped wheel back.

</details>

## 2026-10-17 — Klusters: radial context menu (proof of concept)

<details>
<summary>Details</summary>

- **Right-click** in a view opens a hierarchical radial menu: the root
  actions on a ring around the cursor, and each group's actions on the next
  ring out when hovered (up to four rings). It supports press-drag-release
  (pick by direction in one motion) and click mode, and Esc backs out one ring.
- `RadialMenu` (`radialmenu.h/.cpp`) lays out any `QMenu` tree, so a wedge is
  the existing `QAction`: enabled and checked state, icons and shortcuts are
  the live ones. Hidden actions and submenus they leave empty are skipped.
- The tree (`klusters_radialmenu.cpp`) reuses menu-bar actions: Tools,
  Clusters, Recluster, Undo, Redo, Hierarchy, Sort. Sort holds the Actions ▸
  Sort Clusters entries under short wedge labels (`RadialMenu::setLabel`; the
  menu bar keeps the full names and the status bar shows them on hover).
- Two value wedges, **X: n** and **Y: n**, set the feature dimensions: the
  mouse wheel over one steps that axis (the toolbar's Features (x,y)
  spinboxes, so the scatter redraws live behind the menu). A click never picks
  them, so the menu stays open (`RadialMenu::setAdjustable` / `adjusted()`). The chord is the app-scope command `app.radialMenu`, so
  it appears in Preferences ▸ Input and Help ▸ Shortcuts and can be rebound.
  Right while drawing a lasso still removes the last vertex, and the engaged
  lineage overlay keeps its own menu. Rebound away from Right, plain
  Right-click opens the older Sort Clusters list again.
- Fix: the views' right-click filter (`KlustersView::eventFilter`) opened Sort
  Clusters for every right press and swallowed it, so the radial menu never
  appeared. It now runs any registry-bound right press itself. A right-click
  on a panel's title bar is left to Qt (its panel show/hide list); the filter
  used to open its own menu there too, so two menus came up at once.
- Tested in the running app on Xvfb (Qt 6.4.2) with a synthetic session and
  real mouse input: cluster, waveform and correlogram views, flick and click
  picks, lasso undo-vertex. Not yet tried on a real desktop or the reference
  session; the lineage-overlay case is by code reading.

</details>

## 2026-10-17 — Spike extraction: one .spk row per .res time; shared extractor code

<details>
<summary>Details</summary>

- **Fix: re-extraction (`-R`, what `ndm_extractspikes` runs) skipped spikes
  near the ends of the recording.** A `.res` time whose window was not inside
  the `.fil` got no `.spk` row, so the `.spk` was shorter than the `.res`, and
  an early skip shifted every later waveform against its spike and `.clu`
  row. Each time now gets a row; a window outside the `.fil` is zeros, with a
  warning. The three extractors' `-R` code (one mmap copy each) is replaced by
  one helper on `core/spike_extract.hpp`, the code Klusters re-extracts with;
  `SpikeWindowSpec::temporalDiff = false` gives sdiff's spatial-only
  transform. Every other `.spk` byte is unchanged (39 regression runs).
- **Fix: `process_extractspikes` read overlapping windows late.** Its
  detection run's waveform pass reads the `.fil` forward; a window starting
  before the previous one ended (two spikes closer than the waveform length,
  in any groups) was read from the current position instead, and the error
  carried on to later windows: 4.5 % of waveforms wrong at the default
  threshold on a synthetic session, nearly all of them with a low threshold.
  The `.res` was right. The pipeline discards this `.spk` (`ndm_extractspikes`
  re-cuts it), but `ndm_redetectspikes` merges it into the session's `.spk`:
  re-extract after a redetection. `ndm_test_extract_alignment` checks every
  row of the three extractors' output against its `.res` time.
- **`process_extractspikes` uses the shared peak-search and argument
  helpers** too. Fixed on the way: a trailing empty electrode group
  (`-c 0,1,2,3:`) aborted the run (a `delete[]` on stack memory); the
  "basename -" stdin form, which never worked, is refused with a message.
- `ndm_functions`: `ndm_resolve_input`, unused, is removed (`ndm_resolve` /
  `ndm_resolve_any` resolve per-group files).

</details>

## 2026-10-17 — Reference files for the template-library formats; one copy of the extractor helpers; plugins configure on their own

<details>
<summary>Details</summary>

- **Reference files for `.wti`/`.wtf`/`.wtl`/`.eap`/`.tcl`**
  (`src/libneurosuite-core/test/golden/`, shared byte for byte with
  fiber-kit, hashes in `contract.yaml`). `golden.*` are written by the C++
  writers, `fk.*` by fiber-kit's `neuro_io`. `core_test_golden_formats`
  checks that the C++ writers still produce `golden.*` and reads every file
  field by field, including fiber-kit's v1 `.wtl`; fiber-kit's
  `test_golden_formats.py` does the mirror image. RECIPES §2.1 says how to
  regenerate them.
- **Fix: `.wtl` running means and stds keep full float precision.** They were
  written with 6 significant digits, so every save rounded them
  (1234.5678 → 1234.57). Now the shortest text that reads back as the same
  float. Found by the reference-file test.
- **One copy of the extractor helpers.** `process_extractspikes_sdiff` and
  `process_extractspikes_stderiv` shared 338 identical lines (peak search,
  refractory rule, `-c` parsing, argument checks). They are now in
  `src/ndmanager-plugins/src/extractspikes_common/extractspikes_detect.h`.
  Old and new binaries give byte-identical `.res`/`.spk` in 25 runs on a
  synthetic session.
- **ndmanager-plugins configure on their own** (`cmake -S
  src/ndmanager-plugins`). The plugin CMake files assumed the monorepo root
  was the top-level project; standalone configure failed. BUILD.md §5.
- **CORE_API §3.21** documents `channel_expr.hpp` (C21).

fiber-kit 0.45.0: the same reference files, `neuro_io.read_mti` /
`read_mtf` for the committed template model, `doc/` merged into `docs/`.

</details>

## 2026-10-17 — Core fixes: long tokens and groups, resolveAny order, int16 saturation, deterministic t-SNE; old resolver removed

<details>
<summary>Details</summary>

Known issues C3–C6 and C8–C10 in `CORE_API.md` §5 are fixed; each fix has a
shared-table row or a unit test that fails on the old code.

- **Method tokens with a long order.** An `S`/`C` order longer than 9 digits
  (`stderiv_C1234567890`) makes the token opaque. The C++ parser overflowed
  `int`; C++, Python, bash and fiber-kit now agree (`custody_vectors.tsv`).
- **`parseAnchor`: long or non-ASCII groups.** A group longer than 9 digits
  threw from `std::stoi`; it is now "not an anchor" in every mirror. Python
  no longer accepts non-ASCII digits (`٣`), and bash reads `007` as 7, not as
  octal.
- **`resolveAny` picks the same copy everywhere.** With several methods'
  copies present, the first in byte order of the file name wins, not the first
  the filesystem lists. bash no longer matches a dotted middle
  (`res.x.y.5`).
- **An out-of-range `.eap` class is refused** by `template_generate` (it read
  outside the matrix).
- **Decollide output saturates to int16** instead of wrapping.
- **t-SNE gives the same embedding on any number of threads**: the
  normalising sum is added in point order.
- **One `classForUnit`** in `templateclass.hpp`; `staleAfterRealign` (and its
  Python twin) and two unused drift helpers removed.
- **`neurofileio::resolveInput` / `preferDerived` / `preferCanonical`
  removed.** Their last caller, Klusters' spike-realign dialog, reported the
  PCA basis NOT FOUND for a suffixed stderiv session (`.pca.stderiv_C5.N`)
  that the realign itself found. The dialog now resolves through custody, as
  the realign does. (Not compiled here: no Qt.)
- `contract.yaml`: the contract number is bumped only when the protocol, the
  session schema or a format changes; a table change shows in its hash.

fiber-kit 0.44.1 carries the same token bound, eap-class check and
saturation.

</details>

## 2026-10-17 — kiloklustakwik: heap overflow and -PriorFile fixes; KK_chunked.cpp split; driver comparison

<details>
<summary>Details</summary>

- **Fix: heap overflow when `MaxClusters > MaxPossibleClusters`.** Every
  per-cluster array is sized `MaxPossibleClusters`, but a run starts up to
  `MaxClusters` clusters. With `-MaxPossibleClusters` below the default
  `MaxClusters` (200), MStep wrote past its buffer and KK crashed later
  (AddressSanitizer: KK.cpp:193). KK now refuses that combination with a
  message.
- **Fix: `-PriorFile` now replaces `MaxClusters` and `MergeThresh`.** The
  prior compared them against stale copies of the defaults (10 and 30, where
  the defaults are 200 and 0 = auto). It never replaced either, and it
  overrode a user who passed exactly those values. The defaults now have one
  home (`KlustaKwik.h`).
- **`KK_chunked.cpp` split** (7,463 → 5,070 lines). `MergeChunkModels` moves
  to `KK_chunk_merge.cpp` and `RunPhase2bMode3Chunk` to
  `KK_phase2b_mode3.cpp`, verbatim. Old and new binaries give a
  byte-identical `.clu` (and `.spk` with Phase 2b mode 3) on a synthetic
  chunked session.
- **The two `RunChunkedCEM` drivers have drifted**
  ([runchunkedcem-drivers.md](doc/design/runchunkedcem-drivers.md)).
  - With `-ChunkFile` there is no preseed, the deletion floor is the
    starting K, and `-FusePerChunkStages` and
    `-AlternatingSplitCooldownIters` are ignored.
  - With `-ChunkMinutes`, `-FiberStageEnable` is ignored.
  - The two drivers run the K-template split and DipSplit in opposite
    orders.

  Merging the drivers waits on decisions and on the reference sessions.
- `ARCHITECTURE.md` is brought up to date: the `KK.cpp` split is done and
  the line counts are current.

</details>

## 2026-10-17 — Qt-free code moves into libneurosuite-core, with tests; merge ranking fix

<details>
<summary>Details</summary>

Plain C++ that lived inside the apps moves into libneurosuite-core, where it
is built and tested without Qt. All the moves are unchanged code; each was
checked bit-identical against the old copy.

- **`dipsplit.hpp`**: the bimodality gate, PCs, two-way k-means and BIC.
  kiloklustakwik and Klusters each had a copy, and the copies had drifted.
  The header is their union.
- **`watershed2d.hpp`**: Klusters' 2D density watershed.
- **`Neurosuite::spectral`**: NeuroScope's multitaper library, as a static
  library in core. Its self-test is now `core_test_spectral` and runs
  without Qt; it never did before.
- **`tsne_embed.hpp`, `oblique_embed.hpp`, `waveformiou.hpp`,
  `mergerecommend.hpp`**: Klusters' t-SNE and oblique presentations and
  its merge ranking. All four said they were "Qt-free so it can be
  unit-tested" but had no test.
- **New tests** with known answers: `core_test_dipsplit`,
  `core_test_watershed2d`, `core_test_embed` and `core_test_merge`.
- **Fix: merge recommendations.** When every candidate pair had the same
  overlap (or the same error score), that witness gave every pair rank 0.5
  instead of abstaining as documented. At the default quality floor (0.90)
  the panel then showed nothing. Now the other witness decides.
- **Found, not changed** (CORE_API §5):
  - `valley_test` scores the valley beside a lone tail sample as deep
    (C22).
  - t-SNE repeats bit-identically only on one thread (C23).
- *Klusters and NeuroScope are not compiled here (no Qt)*. Their changes
  are includes, namespace prefixes and CMake.

</details>

## 2026-10-17 — neurosuite-3 3.1.0: a version contract with fiber-kit and ndm plugins

<details>
<summary>Details</summary>

- **Releases are numbered and tagged** from here on. This is 3.1.0 (the
  root `project()` version; it was 3.0.0 and never tagged). Its tag is
  `v3.1.0`. The matching fiber-kit release is 0.44.0 (`v0.44.0`).
- **`contract.yaml`** at the repository root records what neurosuite-3
  promises at its boundaries:
  - the ndm plugin protocol (1);
  - the session-schema version (`1.0`);
  - for `.pca`, `.wti`, `.wtl`, `.tcl` and `.eap`, the highest version read
    and the version written;
  - the hashes of the tables shared with fiber-kit.

  fiber-kit keeps its own. `core_test_contract` checks the file against
  `core/version_contract.hpp` and against what the writers actually put on
  disk. `core_shared_contract` checks the table hashes on every run; with a
  fiber-kit checkout it also checks that each side reads every version the
  other writes.
- **ndmanager warns about plugin format versions.** A plugin can list
  `format <name> <read> <write>` lines in its `--ndm-version` answer
  (fiber-kit 0.44.0 does). ndmanager logs any format one side writes and the
  other cannot read, and says so in the status bar. The plugin is still
  loaded. The protocol number and the session schema written by the YAML
  layer now come from the same header. *Not compiled here (no Qt).*
- **Standalone core tests.** `cmake -S src/libneurosuite-core
  -DNS_BUILD_TESTS=ON` now registers and passes its 23 tests.
- **Docs:** [RECIPES.md](doc/dev/RECIPES.md) §2.5 (changing a version),
  CORE_API.md §3.13.

</details>

## 2026-10-17 — Recipes and canonical references; process_pca records the right input channel count; Qt build fix

<details>
<summary>Details</summary>

- **`doc/dev/RECIPES.md`.** One page that names the canonical reference
  for each file format, shared rule and schema. It also gives step-by-step
  recipes for the changes made most often:
  - a row of a shared vector table;
  - a PCAE method;
  - a session-YAML section;
  - an ndmanager plugin step.

  Each recipe lists every file to touch and the test that proves the change.
  Linked from CLAUDE.md, the Developer Guide, STANDARDIZATION.md and
  tests/README.md.
- **Drifted copies replaced by links.** `doc/libklustersshared/README.md`
  carried an older `.probe` layout and an incomplete probe-library search
  order. Both now point to `formats/probe.md` and `ndm_setupgroups.md`. Its
  schema list gains `sourceRecording`, `probeLibraryPath` and `template`.
  CORE_API.md §3.6 now lists `StderivExpr` (9) and `SdiffOrder::Expr` (6).
- **`process_pca`** wrote `nInputChannels` one short for `--pca-method 7`
  and `8` (custom pairs and sets). It now takes the dropped-channel rule
  from `custody::sdiffDropsLastChannel`. Its usage text lists method 9.
  `test_subset_pca` checks the header for methods 0 and 4–9.
- **CI** also runs when only `doc/` or `templates/` change. The doc-link and
  template checks read those directories.
- **Qt build fix.** `channel_expr.hpp` had a method named `emit`, which is a
  macro in every Qt translation unit, so Klusters failed to compile. It is
  now `push`. A new ctest, `core_test_qt_keywords`, compiles every core header
  with Qt's keyword macros defined.

</details>

## 2026-10-16 — One stderiv transform, one token rule: shared tables for both

<details>
<summary>Details</summary>

- **One spatial derivative.** The stderiv transform was written out in nine
  places:
  - four sites in the extractor;
  - the re-extractor;
  - the sdiff extractor;
  - `process_pca_stderiv`;
  - the Klusters nudge;
  - KK's all-pairs helper.

  All now call `stderiv_transform.hpp`. Its new `spatialDerivBy` reads
  channels through an accessor, so detectors read the record in place.
  Every tool's output is byte-identical before and after on the synthetic
  session, over 13 extractor configurations. The extractor is about 8%
  faster, and the re-extractor is no slower.
- **One misfit rule.** A custom map that does not fit the group gives
  all-pairs for orders 4, 5 and 6. Core used to return the raw value for
  order 4. The Klusters nudge on an order-4/5 basis without a session
  pattern now follows the engines.
- **`process_pca_stderiv -d 4`** is a pass-through flag in the code, no
  longer a value that collided with order 4 (custom). The command line is
  unchanged.
- **New shared tables**, each run on both sides:
  - `stderiv_vectors.tsv`: 34 transform cases, including saturation,
    halves and maps that do not fit;
  - `sdiff_grammar_vectors.tsv`: 39 order-4/5 patterns, also run by
    `ndm_custody`.

  `custody_vectors.tsv` gains `waveform_token` and `drops_last` rows and
  more `method_token` rows.
- **Fixed drift these tables found:**
  - The method-token parsers disagreed on unknown, repeated and malformed
    suffixes, so `stderiv_X1_D34` had waveform token `stderiv_X1` in C++
    and bash. All four parsers now make such a token wholly opaque, as
    custody.hpp documented.
  - `process_pca_stderiv`'s dropped-channel rule is now
    `custody::sdiffDropsLastChannel`.
- **STANDARDIZATION.md §2.10** lists each shared rule's single home, its
  mirrors and its table.

</details>

---

## 2026-10-16 — CI runs the tests and a lint job; HDF5 1.10 build fix; generated files removed

<details>
<summary>Details</summary>

- **CI runs the regression suite.** `ci.yml` used to build but never test.
  It now configures with `NS_BUILD_TESTS=ON`, checks out fiber-kit, and runs
  ctest.
- **More tests under ctest.**
  - The 18 ndmanager-plugins script tests are registered as `ndm_test_*`.
  - `core_shared_contract` checks that the vector tables match fiber-kit's
    (`-DNS_FIBER_KIT_ROOT`).
  - Tests that cross-check a plugin binary are given this build's binary.
- **`test_subset_pca.py`** parsed the pre-PCAE `.pca` header and crashed
  on real output. It looked for `process_pca` at a path no build produces.
  Both are fixed.
- **`process_aomconvert` built only against HDF5 1.12+.** Ubuntu 24.04,
  the documented platform and the CI image, ships HDF5 1.10, where the
  five-argument `H5Oget_info_by_name` does not exist. It now calls the
  versioned function, so it builds on both.
- **Lint.**
  - `scripts/lint.sh` checks four things, in seconds and without a build:
    `bash -n` on every shell script, ruff's defect rules on every Python
    file, no generated file tracked, and the build invariants.
  - CI runs it as the `lint` job.
- **805 generated files removed:**
  - Doxygen HTML from the original authors' machines;
  - configure output inside the vendored libsamplerate (CMake builds a
    copy and configures it there);
  - clazy compile databases.

  `.gitignore` names them. Its `install/` rule no longer hides the
  `doc/*/install/` guides.
- **fiber-kit reads `.wtl` v2** (fiber-kit 0.41.0), so FILE_FORMATS.md no
  longer lists a format divergence. On the roadmap, only the `.mti`/`.mtf`
  reader remains.

</details>

---

## 2026-10-16 — Build and docs: one C++ standard, standard test layout, how to run the tests

<details>
<summary>Details</summary>

- **C++20 everywhere.** The 34 targets that still declared `cxx_std_17` (all
  of them were already built as C++20) now declare `cxx_std_20`. The rule
  that core headers had to stay C++17-clean is dropped, since nothing
  needed it. `process_smrconvert` and libsamplerate are untouched.
- **klusters tests** moved from `src/klusters/tests/` to
  `src/klusters/test/`, like every other subproject. Test names are
  unchanged.
- **`build_invariants`** gains two checks. One is for the C++ standard: every
  root has `CMAKE_CXX_STANDARD 20` and nothing asks for another standard.
  The other is for the layout: `test/` and never `tests/`, and no sources at
  a subproject root. The `cmake -DREPO=. -P tests/cmake_invariants.cmake`
  form now works.
- **STANDARDIZATION.md** now matches the tree:
  - the syntax gate is `-std=c++20`;
  - a new §2.9 sets out the standard;
  - libneurosuite-core is in the project map;
  - the hue formula is balanced;
  - §4 describes the patch-series loop in place of the old tarball one;
  - §9 has a tests gate.
- **[tests/README.md](tests/README.md)** is the one page on running tests. It
  covers:
  - ctest flags and test-name prefixes;
  - a Qt-free configure;
  - the ndmanager-plugins script tests and the fiber-kit contract check,
    which do not run under ctest;
  - where to add a test.

</details>

---

## 2026-10-16 — Spike detection: sdiffPairs as arithmetic expressions (stderiv_C6)

<details>
<summary>Details</summary>

A spike group's `sdiffPairs` may now give one formula per output channel, in
channel order, over the group's channels `c0`, `c1`, …:

```yaml
sdiffPairs: "c0 - (c1 + c2)/2, c1 - c0, c2 - 0.5*c3 - 0.5*c1, c3 - c2"
```

- `+ - * / ^` and parentheses in the usual order of precedence (`^`
  right-associative, binding tighter than a unary minus); numbers are
  constants; any arithmetic, so the transform need not be linear. A
  non-finite result is 0, one beyond ±10⁹ is held there, and the value is
  rounded and saturated to int16 as for every order.
- Spatial order 6, method token `stderiv_C6`, PCAE method 9. Every channel
  is kept for PCA. A group whose element count or channels do not fit is an
  error naming the group.
- The older forms keep their meaning (`a-b,…` order 4, `a-b+c,…` order 5);
  in a run where some group uses expressions, the others are written as the
  expressions they compute, with the same values to the last bit.
- Applied everywhere the transform is: detection and extraction, `-R`
  re-extraction, `ndm_reextractspikes`, both aligners, and Klusters' realign,
  nudge, re-extract on save and feature-space sync. The scripts pass the
  pattern quoted, so `*` and parentheses are never globbed.
- One grammar, `neurosuite/core/channel_expr.hpp`, with cross-language
  vectors (`sdiff_expr_vectors.tsv`) that fiber-kit runs against its port.
  Checked end to end: the older forms and their expressions give
  byte-identical `.res`/`.spk`, and fiber-kit's transform reproduces the
  engine's `.spk` for a nonlinear pattern.
- Also fixed on the way: `clampToInt16` saturates before rounding (a value
  beyond the range of `int` overflowed); fiber-kit's order-4 parser left the
  root channel at 0 where the extractor writes `x[root] − x[pred]` (fiber-kit
  patch).

See `doc/ndmanager-plugins/commands/spike-detection.md` — *Expressions
(order 6)*.

</details>

## 2026-10-16 — ndManager: export a session as a template, apply a template to a session

<details>
<summary>Details</summary>

- **File ▸ Export Session as Template…** saves chosen sections of the open
  session (unsaved edits included) with a name and description to your
  template folder (`<AppDataLocation>/templates/<name>.template.yaml`) or any
  file, in the session's layout. Session-specific sections (general
  information, source recording, units) start unchecked.
- **File ▸ Apply Template to Session…** lists your templates by name (or takes
  any file) and replaces the checked sections of the open session with the
  template's, keeping the rest. Processing sections (groups, display,
  plugins, LFP rate) start checked; the recording's setup (acquisition, files,
  video, probes) and units do not. Warns when groups made for another channel
  count would be applied. The session counts as modified until saved; the
  applied sections keep the template's comments.
- A template is an ordinary parameter file plus a `template:` block; a session
  saved from one does not carry the block. The rules are in
  `libklustersshared` (`sessiontemplate.h`, tested by
  `klustersshared_sessiontemplate_test`).

See `doc/ndmanager/README.md` — *Your own templates*.

</details>

## 2026-10-16 — ndManager: parameter tables by keyboard, typed values; plugins listed by purpose

<details>
<summary>Details</summary>

- **Tab never traps the keyboard.** In every ndManager table Tab and
  Shift+Tab move cell to cell and leave the table after the last cell (or
  before the first). An invalid entry is put back to its last good value with
  the reason beside the cell, instead of holding the focus until it is fixed.
  The help field, the pipeline canvas and the parameter page's buttons are in
  the Tab order.
- **Parameter tables edit like a spreadsheet:** double-click, F2 or typing
  starts an edit; Add opens the new row's name; **Move Up / Move Down**
  reorder rows.
- **Typed parameters.** A description's optional `type` (`bool`, `int`,
  `float`, `choice:a|b|c`), `default` and `hidden` keys are kept — through the
  table, the description file and the session file's `programs:` entry —
  instead of being dropped on the first save. The Value cell's editor follows
  the type (a list for bool and choice, a number-only field for int and
  float); a value the type does not take is refused, and an empty one means
  "not set". `hidden: true` hides a row outside expert mode.
- A value edited on a plugin's page reaches that plugin's nodes in the
  Pipeline graph.
- **Plugins by purpose.** The Plugins tree and the Pipeline palette list
  plugins in collapsible branches (Conversion, Preparation, Detection,
  Sorting, …). The branch comes from the description's `category`, else the
  built-in table, else Other; the palette no longer leaves out a category
  missing from a fixed list. Enter on a palette entry adds it to the graph.

See `doc/ndmanager/README.md` — *Editing a plugin's parameters* and *Plugins
by purpose*.

</details>

## 2026-10-16 — Units: brain region, division, atlas and region ID

<details>
<summary>Details</summary>

Units carry four more optional fields after structure, type, isolation
distance, quality and notes: **region** (e.g. hippocampal formation),
**division** (e.g. archicortex), **atlas** (where the terms come from, e.g.
WHS SD rat v4 or Allen CCF) and **regionId** (a stable atlas or UBERON id).
Klusters edits them in Cluster Information and shows them in the palette;
the `.cli` cluster information file is format v2 (eleven fields; v1 files
read unchanged); ndManager's Units page and Record Curated Units carry them;
the session YAML's `units` entries write each key only when it is set. See
`doc/ndmanager-plugins/formats/cli.md`.

</details>

## 2026-10-16 — Klusters: saves are all-or-nothing and skip unchanged files; autosave covers hierarchical sessions; editing locked during a save

<details>
<summary>Details</summary>

- **All or nothing.** A save writes every file it changes (`.clu`, `.clc`,
  `.clp`, realigned `.spk`/`.res`/`.fet`, synced feature spaces, the units in
  the `.yaml`) in full beside its target first, then installs them together
  under a journal. A failure puts back what was already replaced; a crash in
  the middle is completed on the next open.
- **Unchanged files are not rewritten.** The ids are compared with the files;
  each working copy carries a stamp from when it last matched its file, so an
  untouched `.spk` is recognised without being read. A save with nothing to
  change writes nothing, and the readers are rebuilt only when waveforms were
  committed.
- **Autosave for hierarchical sessions.** The rescue copy now holds the atom
  ids too (`.<clc>.autosave`), from the same snapshot as the unit ids, and is
  restored with them when it fits. Autosave runs on the GUI thread from a
  timer (the old thread read the live clustering), only while there are
  unsaved changes, and follows a Save As.
- **Cluster information is saved with the clustering, not into the session
  YAML.** Klusters writes it to `<base>.cli.<method>.<group>[.<tag>]` beside
  the `.clu` (one per stage, in the same commit) and reads it back on open. The
  YAML `units` table is the record of finished curation: ndManager fills it on
  request with **Actions ▸ Record Curated Units…**, choosing per group which
  clustering's file to record.
- **No editing during a save.** Input is held while a save runs, with a
  "Saving…" box for a slow one. Closing during a save waits for it ("Waiting
  for the save to finish before closing…"), then asks to save only if the
  clustering still differs from the files.

</details>

## 2026-10-16 — Klusters: save-system fixes; Promote to Hierarchical Session

<details>
<summary>Details</summary>

An audit of the Klusters save path (Save, Save As, the pending-file commit,
the hierarchy siblings, autosave).

**Data that was lost and is now kept.**
- Save As wrote the clustering as last *saved* under the new name: the
  pending `.clu` it committed over the new file had not been updated. Every
  edit since the last save was missing from the new file, and the save
  reported success.
- Save As of a session with cluster information failed as "not writable"
  (the parameter file was looked for inside the new `.clu`'s path).
- An edit made while a save was running was marked saved, so closing
  afterwards asked nothing.
- The pending-file commit removed each original before copying over it; an
  interruption between the two lost the original, and the next open then
  deleted the pending copy too. It now copies beside the original and renames.

**Hierarchical sessions.**
- The `.clc`/`.clp` are taken from the same snapshot as the `.clu`, on the GUI
  thread (the save used to read the hierarchy maps on the save thread while
  edits changed them), staged, and installed only after the `.clu` is
  committed; a failed save leaves all three as they were.
- A sibling that could not be written failed silently; it now fails the save.
  A `.clp` kept because atoms straddle units is reported after the save.
- Save As writes the new triple and continues on it (it used to write the
  `.clu` alone, then later Saves wrote the new map into the old `.clp`).

**New: Hierarchy ▸ Promote to Hierarchical Session.** Writes a `.clc` (one atom
per unit, same ids) and `.clp` for a flat `.clu` — or adopts an orphan `.clc`
that nests in it — saves first if needed, and reopens the session
hierarchical. See `doc/klusters/hierarchical-clustering.md`.

Also: the hierarchy menu's editing entries were enabled in flat sessions after
a realign; save errors name the failing file.

</details>

## 2026-10-16 — ndManager: audit fixes — saves, Pipeline tab, Probes tab, Probe Maker

<details>
<summary>Details</summary>

An audit of ndManager, every finding checked a second time before it was
fixed (`claude/ndmanager-audit-2026-10-16.md` in the project notes).

**Crashes fixed.** Saving a session with two or more units (the units table
was filled with sorting on, scrambling rows and leaving empty cells), saving a
file channel mapping whose ids start at 0 or have gaps, opening a probe file
with a per-shank channel map or null fields, and malformed plugin
descriptions.

**Data that was lost and is now kept.**
- Skip flags: the page and the file used different keys, so every save
  wrote `skip: 0`.
- Failed saves were silent; "Save" in the close prompt closed even when
  nothing was saved; Save as Default marked the session saved; Reload
  discarded edits without asking and could turn a template into the file
  Ctrl+S overwrites; New from Project Template and Save could replace an
  existing file without asking.
- The session's `programs:` list was written alphabetically, so `ndm_start`
  was no longer first and ran steps in the wrong order. It keeps its order now.
- Pipeline tab: a node added after loading a pipeline file could reuse an
  existing id and replace another plugin; Apply replaced the Plugins tab
  (tuned values, help, programs outside the graph); graph edits were not
  counted as unsaved (the close prompt now offers to save the pipeline).
- Probe Maker: only the first edit after loading a probe was written.
- Probe changes rebuilt every group with default nSamples/peak/features and
  dropped `sdiffPairs`; unchanged groups now keep their settings, and groups
  get `probeId`/`shankIndex`.
- Plugin scripts lost their `#!` line; a session save cleared the "script
  modified" flag.

**Behaviour changes to know about.**
- ndManager reads and writes YAML only; convert XML sessions with
  `ndm_xml2yaml`. Saves always end in `.yaml`/`.yml`.
- Session-local probe files live next to the session file
  (`<session>.probe.<id>.probe`), not in the directory ndManager was started
  from; a session must be saved before probes are added. A removed probe's
  copy is deleted only when the session is saved.
- The probe library is searched in the session directory, `SESSION/probes`,
  `probeLibraryPath`, `$NEUROSUITE_PROBE_PATH`, then every XDG data
  directory; a typed channel offset is kept.
- `ndm_setupgroups` reads `channelOffset` as absolute (it added it to a
  running offset, so a second probe landed on the wrong channels) and finds
  ndManager's session-local probe files.
- The Probe Maker keeps each site's channel, writes sites top first, and a
  shank-length change keeps sites fixed relative to the tip.
- Pipeline tab: Clear keeps the root, cycles are refused, Apply orders the
  Plugins tab without discarding it, a pipeline file that fails to load is
  reported and protected, and Save Pipeline needs a saved session.
- Actions ▸ Query works again, on YAML files (paths like
  `acquisitionSystem/samplingRate`).
- `ndmanager --new` and `--model FILE`; Save as Default writes where File ▸ New
  reads; session YAML files are registered as `application/nphys-yaml`.
- `NDM_PIPELINE` naming a pipeline file that does not exist is now an error
  in `ndm_start` (it used to run another pipeline silently). Save Pipeline As
  under another name leaves the graph "unsaved" for the close prompt.
- A one-shank `.probe` without `count_per_shank` holds every geometry site in
  every reader (it did only in the Probe Maker).
- The installed MIME database and icon cache are refreshed once after the
  whole install; the unused postinst/postrm scripts are gone.

</details>

## 2026-10-15 — ndManager: New from Project Template; saves keep comments, inline lists and quoting

<details>
<summary>Details</summary>

**File ▸ New from Project Template…** (Ctrl+Shift+N) creates a session
parameter file from `templates/template.yaml` — every plugin with every
parameter it reads, and the comments documenting them. ndmanager asks where to
save the file, fills in today's date and opens it. The template is installed as
`<datadir>/ndmanager/templates/session-template.yaml`;
`NEUROSUITE_SESSION_TEMPLATE` points at another one. The template's probe
entries now use `probeId`, the key every tool writes.

**Saves keep the file's layout.** ndmanager, NeuroScope and Klusters wrote
parameter files back in yaml-cpp's block style: comments (including the
template's commented-out parameters) were lost, `channels: [0, 1, 2, 3]` became
one line per channel, and `'false'` lost its quotes and became a boolean for
Python readers. They now keep comments with the key or list item below them,
end-of-line comments, inline lists, quoting and spacing; saving an unchanged
file rewrites the same text. New lists of plain values are written inline.

</details>

## 2026-10-15 — Audit fixes: ndmanager keeps unknown YAML keys; aligners and re-extraction follow the .spk convention; re-extraction threshold base

<details>
<summary>Details</summary>

An audit of the shared libraries and the spike tools, each finding checked a
second time against code, docs and history before it was fixed.

**Behaviour changes to know about**

- **`ndm_reextractspikes` threshold scale.** The stderiv re-extractor now
  measures noise the way detection does (`thresholdNoise`, default `sdiff`)
  instead of always on the stderiv signal. On a session that does not set
  `thresholdNoise`, a tuned `reextractThresholdFactor` therefore gives
  thresholds higher by each channel's median|sdiff| / median|stderiv| ratio
  (about 1.6 on the reference session). Divide the factor by that ratio, or
  set `thresholdNoise: stderiv`, to keep the old thresholds.
- **ndmanager saves keep what they do not model** — per-group `probeId`,
  `shankIndex`, `sitePositions_um`, `klustakwik:`, `nCells`, extra
  acquisition fields and blocks such as `sourceRecording`. They used to be
  dropped on every save. Probe entries are now written with `probeId`; every
  reader accepts `probeId` and `id`.
- **Aligned `.spk` files made before this change mix two conventions**
  (shifted rows were re-cut with a zero seed and truncation). Re-extract them
  at their `.res`. Both aligners and the re-extractor now detect the `.spk`'s
  convention and follow it.
- Aligners never move a window to sample 0 or past the end of the `.fil`; a
  bare `stderiv` token on a session with `sdiffPairs` now aligns with the
  pattern, and `ndm_alignspikes_pca` accepts `stderiv_C4`/`_C5`.
- The sdiff detector and the re-extractors refuse (skip, for re-extraction)
  one-channel groups; re-extraction rejects a spike exactly one refractory
  period after the previous one, like detection.
- Groups with no channels keep their number (YAML readers, `ndm_xml2yaml`)
  instead of renumbering the groups after them.
- Zero-energy spikes get shift 0 in every xcorr back end and in centroid
  alignment; GPU ties pick the lowest lag like the CPU.
- `.wti`/`.wtl` window times and `.evt` times are written at full precision;
  binary `.res` detection no longer mistakes ~4% of files for text; a
  truncated `.col` or malformed `.tcl` is refused instead of half-read.

**Crashes and memory errors fixed:** detection on groups wider than 64
channels (stack overrun), a read past the previous buffer in all three
detectors, an untransformed look-ahead row at every 50,000-sample chunk edge
(could move a timestamp), uncaught exceptions on corrupt `.fet`/`.wtl`/`.eap`
headers.

</details>

## 2026-10-15 — Klusters: t-SNE of the oblique coordinates

<details>
<summary>Details</summary>

The oblique view (Shift+O) decomposes every spike on the template axes, one
coordinate per template, but can only draw two directions of them. With a basis
of **three or more templates**, **F** in the oblique view now runs t-SNE on the
spikes' full oblique coordinates instead of the features, so groups the 2-D
picture overlaps can pull apart. The embedding replaces the oblique view and is
an ordinary t-SNE: ↑/↓ steps the perplexity on the same coordinates, the lasso,
multi-lasso, watershed, zoom/pan and heatmap all work, Shift+O returns to the
oblique view and F to the features. With two templates the oblique view already
shows both coordinates, so F leaves it, as before, and says why.

</details>

## 2026-10-14 — ndManager: empty program parameters are no longer written as `~`

<details>
<summary>Details</summary>

Saving from ndManager wrote a parameter left blank as `value: ~` (YAML null).
Every reader in the suite treats that as unset, but only if it is new enough
to know the convention, and an older installed `ndm_*` script read the text
`~` as a value (`excludeClusters=~`). An empty parameter is now saved with its
name and status only and no `value` key, which every reader sees as unset.
Files saved earlier are cleaned up the next time ndManager saves them.

</details>

## 2026-10-14 — ndm_detectspikes (stderiv): refuse one-channel groups and flat signals

<details>
<summary>Details</summary>

A spatial derivative needs neighbouring channels. On a one-channel group (a
Plexon single-electrode session, say) the order-3 derivative is identically
zero, so the threshold came out 0, every sample was "detected" (9.1 million on
one group) and the extraction then rejected them all as flat. The detector now
stops before any work and names the group, suggesting derivative order 0
(`--method stderiv --methodOrder 0`), the standard method, or regrouping. It
also stops when a channel's detection signal is flat over the threshold window,
which gave the same zero threshold.

</details>

## 2026-10-14 — Klusters: crash on opening a session whose probe has no .probe file; leftover .pending copies

<details>
<summary>Details</summary>

**Crash fixed.** A `probes:` entry in the session YAML with no `probeFile`
(as a Plexon import leaves it) made the drift view look for the probe
geometry at the session directory itself; reading a directory throws a stream
error that the YAML parser does not translate, and Klusters aborted as soon as
the drift view was shown. A probe without a readable `.probe` file now simply
has no geometry, and any read error in the geometry lookup is caught.

**Leftover working copies.** Opening a session from its `.fet` before any
`.clu` exists left `.spk.pending`, `.res.pending` and `.fet.pending` copies
beside the originals, because the `.clu` copy failed after they were made.
They are now removed when the set cannot be completed.

</details>

## 2026-10-14 — Klusters: watershed split on the noise cluster

<details>
<summary>Details</summary>

**Shift+W now works on the noise cluster (1).** Select noise on its own and the
preview shows its density basins as usual; Enter carves every basin except the
largest out as a new cluster, while the largest basin (the noise core) and the
spikes no basin claimed stay in noise, so the noise bin always survives. The
commit applies the basins the preview drew, not a recomputation. Each new
cluster is its own undo step. Noise selected together with other clusters is
refused, because the regular split dissolves its sources and noise must not be
dissolved. The artefact cluster (0) is still never split.

</details>

## 2026-10-14 — Klusters: multi-lasso tool; zoom and pan in the t-SNE / oblique view

<details>
<summary>Details</summary>

**Multi-lasso** (Tools ▸ Multi-Lasso, toolbar, Ctrl+5). Closing a lasso keeps it
instead of cutting; draw as many as needed, then **Split by Lassos** (toolbar, or
Enter with no lasso open). Every distinct combination of lassos a spike falls in
becomes one new cluster — two overlapping lassos give *A only*, *B only* and *A
and B* — and spikes in no lasso stay where they are. Backspace removes the last
lasso, Esc discards them all. A lasso is resolved to its spikes when it closes, so
lassos can be drawn across zooms, projections and the scatter/embedding switch and
still intersect. Each region is its own undo step; the selection lands on all the
new clusters.

**t-SNE / oblique navigation.** The embedding now zooms (Ctrl+wheel, toward the
cursor), pans (Ctrl+drag) and resets (double-click) like the feature scatter; the
lasso, the density heatmap and the watershed preview follow the zoom. The
embedding's title, previously black on the black background, is now readable.

</details>

## 2026-10-12 — Klusters: Shift+H density heatmap over the feature view

<details>
<summary>Details</summary>

**Shift+H** toggles a point-density heatmap over the cluster view, in the
feature scatter, the oblique projection and the t-SNE embedding alike. It
shows the density of the points the view draws (the shown clusters, with the
same out-of-scope filter), binned at screen resolution so it follows zoom and
pan, log-scaled and drawn semi-transparently: dense cores read warm, sparse
regions stay clear so their points remain visible. The key is rebindable
(`cluster.densityOverlay`). The smoothing kernel is a Cluster View preference
(**Density heatmap ▸ Smoothing kernel σ**, 0–30 px, default 3 px; 0 shows the
raw binned counts).

</details>

## 2026-10-12 — NeuroScope: closing a session no longer deletes or overwrites parameter-file values

<details>
<summary>Details</summary>

NeuroScope saves the session's parameter YAML on every close, modified or not,
through `ParameterYamlModifier`. Several of its setters rebuilt their section
from only the keys NeuroScope models, so opening and closing a session
silently deleted keys written by other tools:

- **Spike groups:** `sdiffPairs` (the stderiv custom-difference channel
  algebra), `probeId`, `shankIndex` and any other per-group key were dropped.
  An earlier fix kept only `nSamples`/`peakSampleIndex`/`nFeatures` by name.
  Group nodes are now carried across whole, paired by channel set and then by
  position.
- **`files:`:** ndmanager's per-file `channelMapping` was dropped. Entries are
  now updated in place, matched by extension.
- **Anatomical groups, channel colours and offsets:** extra keys were dropped,
  and a channel NeuroScope had no value for got the default instead of the
  file's value. The display trash (group 0) was also saved as an ordinary
  group, so it read back as group 1 and shifted every real group. The trash is
  no longer saved; the reader rebuilds it from the unlisted channels, as the
  XML reader did.
- **Acquisition:** `offset: 0`, and a non-integer `amplification` or
  `voltageRange` (e.g. `192.5`, `1000.0`), read as absent. NeuroScope then
  substituted its preference default and saved it over the file. A value that
  already denotes the same number is now left as written.
- **No-op saves:** a save that changes nothing no longer rewrites the file, so
  comments, formatting and mtime survive a plain open/close.

`klustersshared_yamlmodifier_test` (`NS_BUILD_TESTS`) round-trips a session
holding these keys through the same setters NeuroScope's `saveSession()` uses.

</details>

## 2026-10-12 — ndm_plx2dat: Plexon .plx / .pl2 → .dat + .evt with header metadata in the session YAML

<details>
<summary>Details</summary>

New acquisition converter for Plexon recordings, both containers (detected
from the file signature). **`process_plxconvert`** (C++20, no dependencies)
exports one continuous stream — `WB` by default; `stream` / `channels` pick
another or a subset — as raw int16 counts into `SESSION.dat`, and writes the
Plexon header metadata to `SESSION.plexon.yaml`; **`process_plxyaml.py`**
merges it into the session YAML (`acquisitionSystem` from the Plexon gain,
`generalInfo` date/description when empty, and a `sourceRecording` block:
headers, per-channel names / sources / µV per count, the `.dat` time origin
and pauses, spike- and event-channel summaries, OmniPlex filter settings,
findings). The timeline is rebuilt from block timestamps, so a paused
recording keeps its time base (gaps zero-filled and listed) and the `.dat`
stays aligned with Plexon's spike and event timestamps; channels without
data are kept as zero channels so channel numbering survives.
Readable text of the PL2 headers and settings region that the decoded fields
do not hold (device chain, electrode and input types, source configuration)
is kept as `headerText` / `deviceText`.

Every digital event (single-bit inputs, strobed words, keyboard, start/stop)
goes to a NeuroScope event file `SESSION.plx.evt` (id: `evtId`), time-sorted,
in ms from `.dat` sample 0, labelled by channel name (plus the value when a
channel's values vary, always for strobed words), with `gap start` / `gap
end` markers around zero-filled pauses. The command page lists what none of
the three outputs carries (other streams' samples, Plexon's online spikes and
sort definitions, the PL2 footer, unknown record types).

The other continuous streams with data (`extraStreams`, default all but
`SPKC`) go to `SESSION.px<stream>` files (`.pxai`, `.pxfp`, …): the `.dat`
layout at each stream's own rate, aligned to the `.dat` start on the stream's
own grid; files with the `.dat`'s channel count are listed under `files:` for
NeuroScope. Format: `doc/ndmanager-plugins/formats/pxstream.md`.

Plexon's online spikes (`exportSpikes`, default on) go to
`SESSION.pxres.N` / `.pxclu.N` / `.pxspk.N` per spike channel: `.dat` sample
times, cluster id = Plexon unit + 1, raw waveforms — the byte formats of text
`.res` / `.clu` and `.spk` under their own types, because `.res` is Shared
across method tokens and a Plexon copy would shadow the pipeline's. Format:
`doc/ndmanager-plugins/formats/pxspikes.md`.

PLX follows Plexon.h as read by python-neo; PL2 follows OpenReadout's
clean-room notes (Plexon publishes no PL2 specification), so every run
checks internal consistency and reports failures under
`sourceRecording.findings`. Throughput on a 2-core VM: 0.8 s for a 330 MB
PLX of 1.5 M blocks, 0.6 s for a 300 MB PL2. Test:
`src/ndmanager-plugins/test/test_plx2dat.py` (synthetic files from an
independent encoder; set `PROCESS_PLXCONVERT_BIN`). Reference:
`doc/ndmanager-plugins/commands/ndm_plx2dat.md`.

</details>

## 2026-10-12 — Klusters: sync other feature spaces to the current spike times

<details>
<summary>Details</summary>

**Sync.** Realign and nudge rewrite only the `.clu` space's `.spk`/`.fet`, so
the group's other method spaces drift from the shared `.res`. **Actions → Sync
Feature Spaces** re-extracts each waveform file the `.clu` space does not use
from `.fil` at the session's spike times (same window cutting and stderiv
transform as the pipeline; a `.spk` shared by several spaces — a `_D` space
reads its waveform token's — is rebuilt once) and re-projects each space's
`.fet` through its own `.pca` basis with the kernel realign uses — a `_D`
lag basis is an ordinary PCAE basis on a widened window, so lag spaces are
covered; extra non-PCA columns are carried over. A preference (Refinement →
*Sync a feature space … when switching to it*, off by default) does it for the
target space on every switch. Results are `.pending` working copies: the Space
box loads them, Save commits them, closing without saving, Save As or undoing a
realignment discards them. Reference: `doc/klusters/README.md` → Feature spaces.

**Neighbour clamp removed.** The 2026-10-11 clamp that held a realigned or
nudged spike short of its neighbour in `.res` is gone: what the files share is
the row index, which realign and nudge keep (each spike is rewritten in its own
row; the reorder step stays removed), and every `.spk`/`.fet` follows the one
`.res`, so the order of times across clusters does not matter. Every spike now
moves the full shift. `spikeorder.h`, its test and the held-back count are
removed.

**Sort Spikes by Time.** A new action, **Actions → Sort Spikes by Time**, puts a
group's rows back in time order. It sorts the saved `.res` by time (stable)
and applies the same row permutation to every `.spk` file, each method's
`.fet` and every `.clu`/`.clc`; a file shared by several spaces is permuted
once. It needs a saved document, swaps all files in together
(rolling back on any failure), and reopens the document. `.eap` membership is
reported, not reordered, and any other file naming the group is listed.

**Every space in memory.** Klusters now holds every usable feature space's
`.fet` table and `.spk` waveforms in memory, loaded at open and refreshed after
each switch, sync and Save (a file is re-read only when it changed). Switching
reads nothing from disk, Sync Feature Spaces works from the held tables, and
waveform reads come from memory (`SpkMemoryCache`; a file too large to hold
falls back to reading from disk). Realign and nudge edits are still read
through the overlay; at Save the readers are reloaded before it is cleared.

**Realign and nudge from any feature space.** A Refinement preference (on by
default) lets realign, Align All, nudge and the post-edit realign run while
another space is shown: Klusters swaps to the `.clu`'s own space, runs them,
and swaps back. Afterwards every other space follows the moved spikes only —
each is re-extracted from `.fil` into every other waveform file in that
space's domain (shared cutting with the full re-extract, `spikeCutSpecFor`)
and re-projected into every space's held table and `.pending` `.fet`. Each
space records the spike-times generation it is current to, so one that was
already behind gets a full sync instead of a partial patch. Switching is no
longer refused while a realignment is unsaved.

**Follow-ups.**
- Realign and nudge apply the session's `sdiffPairs` pattern only when the
  method token allows it (`_C<n>` or bare `stderiv`; not `_S<n>`), the same
  rule re-extraction uses.
- A synced space whose spike times changed since its sync (a later realign or
  nudge) is marked in the Space box tooltip as needing another sync.
- Save As drops synced copies on the GUI thread, before the save thread runs.
- Realign warns when a cluster's own spikes leave time order (correlograms and
  ISI assume they do not); Sort Spikes by Time repairs it.

**Fix.** A session working copy such as `x.spk.stderiv_C5.7.pending` was read
as the *standard* method (`custody::methodOf` needs the group as the last
token), so re-extract-on-save rebuilt a stderiv `.spk` as raw waveforms.

**Not yet verified in a build** (no Qt6 here): needs a workstation build and a
pass on the reference session — sync after a realign, switch, check the scatter
and waveforms line up; Save, reopen, switch again.

</details>

## 2026-10-11 — Klusters: view one .clu through another method's feature space; spikes never change order

<details>
<summary>Details</summary>

**Feature-space switcher.** A **Space** box at the far right of the Klusters
Parameters toolbar shows the method space whose features and waveforms are
loaded and lists every space the open group has on disk (`standard`,
`stderiv_C5`, a `_D` lag space, …). Picking another loads that space's `.fet`
and `.spk` under the clusters being curated — cluster assignment, unsaved edits
and undo history are kept — so one curation can be compared across feature
spaces. Save always writes the `.clu` under its original method. While another
space is shown, the operations that write the `.clu` space's own files are off
(realign, Align All, nudge, Save As, decollide, template generation; the
post-edit auto-realign is skipped); recluster works on the shown features.
Spaces whose `.fet`/`.spk` do not match the open spike count are listed greyed
out with the reason. Reference: `doc/klusters/README.md` → Feature spaces.

**Spikes never change order.** The switcher relies on every file of a group
being indexed by the same spike row, so realign and nudge no longer reorder
spikes. Each spike's shift is clamped between its neighbours in `.res`
(`spikeorder.h`: a neighbour from another cluster is a hard wall; a neighbour
that also moves splits the gap). Realign's old reorder step — it permuted a
cluster's rows by new timestamp, and could leave `.res` out of order against
other clusters' spikes — is removed; the log and the nudge status bar report how
many spikes were held back. New unit test `klusters_test_spikeorder`
(fixed cases plus a randomized never-reorders property check).

**Not yet verified in a build.** No Qt6 toolchain was available, so the Klusters
changes were checked by review only (the `spikeorder` test was built and passes).
Needs a workstation build and a pass on the reference session: switch spaces
and back with unsaved edits, Save while another space is shown, realign/nudge
held-back counts, and a recluster from a `_D` space (its feature mask uses the
YAML's features-per-channel).

</details>

## 2026-10-10 — Input-binding overhaul completed across Klusters and NeuroScope; Qt 6.4–6.12 compatibility; documentation

<details>
<summary>Details</summary>

**Input system — one engine, both GUIs.** The keyboard/mouse binding engine (the
`input::` registry: device-agnostic chords, self-describing scopes in a tiered
stack, Passive/Exclusive capture, and the gesture "seam" that keeps a drag/commit
body in the view while the registry owns the trigger) now lives in
`libklustersshared` and drives **both** Klusters and NeuroScope through one
process-wide `input::registry()`. In NeuroScope every menu/tool shortcut, the
TraceView tool gestures (zoom, select-channels, measure, select-time, select-event,
add-event, draw-line) and the Position/Spectral frame zoom resolve through the
registry; the old ~470-line `TraceView::mousePressEvent` switch is gone. Users get
a generated **Preferences ▸ Input** page (rebind any key or mouse binding, clash
highlighting, reset), saveable **keymap layouts** with a bundled read-only
*Default*, a registry-generated **Help ▸ Keyboard Shortcuts** cheat-sheet, and a
per-display active-tool indicator. References: `src/klusters/docs/INPUT_SYSTEM.md`
(the engine) and `src/neuroscope/docs/INPUT_SYSTEM.md` (NeuroScope's adoption).

**Qt 6.4–6.12 compatibility.** A static audit plus a real 6.4.2 build and a 6.10
check: the code uses only low-churn Qt modules and no API removed within the Qt 6
series, so it is compatible across the span. Fixed the one real break (reduced
transitive includes on Qt 6.9/6.10 — include what we use), a Qt 6.8+ teardown
warning (wildcard `disconnect` of `destroyed()`), retired `qAsConst` for
`std::as_const` (deprecated in 6.6), declared the **Qt 6.4 floor** in the
`find_package` calls, and bumped the stale `cmake_minimum_required(2.8.6)` in
`ndmanager-plugins/matlab`. Remaining: a CI build matrix for 6.4 / 6.10 / 6.12.

**Documentation.** Developer references for the input system (above) and
user-handbook sections (Preferences ▸ Input, keymap layouts, the shortcuts
cheat-sheet, the active-tool indicator) in `src/neuroscope/doc/en/index.docbook`.
Plus a prototype modern documentation system (`doc/site/`) — MkDocs + Material +
pandoc — that converts the DocBook handbook with a toolchain that installs
anywhere (replacing the retired KDE meinproc path), unifies it with the developer
docs, and publishes to GitHub Pages; see `doc/site/README.md`.

</details>

## 2026-10-09 — NeuroScope: two silently dead shortcuts fixed

<details>
<summary>Details</summary>

Mirroring NeuroScope's menu shortcuts into the shared input registry (above) let a
startup conflict audit see them for the first time, and it found two keys bound
twice. Qt treats a shortcut claimed by two main-window actions as ambiguous, so
**neither action fired reliably**:

- **Ctrl+Shift+A** — *Select All except 0 and 1* and *Autocenter Channels*.
- **Ctrl+Shift+S** — *Save As* (`QKeySequence::SaveAs`) and *Skip Channels*. Only
  on Linux/Windows, where the standard Save As sequence is Ctrl+Shift+S.

Each chord now stays on its first-listed action (**Select All except 0 and 1**,
**Save As**). **Autocenter Channels** and **Skip Channels** keep their menu entries
but no longer have a default shortcut; bind one in Preferences ▸ Input if wanted.
The startup audit now reports no conflicts. The Ctrl+Shift+S collision was
confirmed live on Qt 6.10 (it is invisible in a headless build, where
`QKeySequence::SaveAs` resolves empty).

</details>

## 2026-10-08 — ndm_hipass: zero-phase Butterworth filter (new default)

<details>
<summary>Details</summary>

**New `process_butterworth`** (CPU/OpenMP, CUDA when built with it and a device
is present): Butterworth high-pass and optional low-pass, designed in double
precision as second-order sections (bilinear transform, pre-warped corners,
per-section unit passband gain), run forward then backward (zero phase, −6 dB at
each corner, effective order 2 × `order`), odd-reflected at the file edges.  The
work is split into independent channel × block units with a settling margin taken
from the filter's impulse response (start-up error < 1e-3 LSB for any int16
input; 240 samples at 800 Hz, order 3, 32.5 kHz), so blocks run in parallel and
the result does not depend on the chunk or block size.  Precision `auto` = double
on the CPU, single on a GPU; `-p` overrides.

**`ndm_hipass` gains `filter: butterworth|median`, default `butterworth`**, with
`highPassHz` (800), `lowPassHz` (0 = off), `order` (3), `precision` (auto).
`filter: median` keeps the old median-subtraction filter; `windowHalfLength` is
now needed only for it.  Behaviour change: a session whose `ndm_hipass` node
does not set `filter` now gets the Butterworth `.fil`.

Why: the median high-pass is non-linear — `x[t] − median(window)` is exactly 0
whenever the centre sample is the window median (about 5% of samples, runs up to
~17 on spike downstrokes), its effective −3 dB corner at `windowHalfLength: 16`,
32.5 kHz is ~800 Hz (not ~600 as documented), and it adds broadband content above
10 kHz that the `.dat` does not contain (coherence 0.01 there).

Verified on the reference `.dat` chunk (group 6, 36–41 min, 8 channels, 300 s):
double output equals scipy `sosfiltfilt` rounded to int16 on every sample (high-
pass 800 Hz; band-pass 600–6000 Hz); identical across chunk/block sizes; single
differs from double on 0.04% of samples by 1 LSB (1 of 59,675 threshold
detections moved by 1 sample); single and double take the same time on the CPU
(0.9 s for the chunk on 2 threads).  The CUDA source compiles for sm_86/sm_89
(clang, device code through ptxas) and the CUDA-enabled binary falls back to the
CPU without a device; a host emulation of the kernel's unit mapping and scratch
layout is bit-identical to the CPU single path.  **The kernel has not been run on
a GPU**, and the Qt6/CMake build was not run in this environment.

</details>

## 2026-10-08 — ndm_extractspikes filter: check the raw source window

<details>
<summary>Details</summary>

The post-extraction flat-run test looked only at the extracted `.spk`, so with a
`stderiv`/`sdiff` method it missed dropped-out channels: the spatial derivative
mixes a dead channel with its live neighbours and the result is not flat.
`process_spikefilter.py` now also tests the raw window of the extraction source
(`<session>.<inputExtension>`, the standard-extraction window
`[t-(peak-1), t-(peak-1)+nSamples)`) on the group's channels, and drops a spike
that fails either test.  With one channel zeroed for 1 s and one stuck for 0.5 s,
a `stderiv_C5` extraction caught 0 of the 80 affected spikes in the `.spk` and
all 80 in the raw window.  Short exact-zero runs produced by the median high-pass
(`ndm_hipass` output is exactly 0 where the centre sample is the window median) mean `flatRunMax` ≈ 20 is the useful setting (1 other spike dropped,
against ~31% at 5).

</details>

## 2026-10-08 — ndm_pca: a `_D` run adds the lag space to existing features

<details>
<summary>Details</summary>

`ndm_pca --method stderiv_C5_D34` skipped every group whose ordinary
`.fet.stderiv_C5.N` already existed, so the lag space could not be added to a
session that had its ordinary features.  A `_D` run now writes only the missing
outputs: with the ordinary `.fet` present it refits, keeps the stored
`.fet`/`.pca` (the refit copy goes to a scratch directory), and writes
`.fet`/`.pca.<token>_D<lag><dims>.N`; it warns when the refit basis differs from
the kept `.pca`.  With the lag `.fet` present it writes the ordinary pair only;
with both present the group is skipped as before.  Runs without a `_D` suffix
are unchanged.

</details>

## 2026-10-08 — ndm_extractspikes: post-extraction flat-run / duplicate filter

<details>
<summary>Details</summary>

**`flatRunMax`, `dedupSamples` in the `ndm_extractspikes` node** (new
`process_spikefilter.py`, any method).  Each freshly extracted `.spk` is checked
in its own transform domain: more than N identical consecutive samples on any
channel, or a position within N samples of the previous kept spike (default 0 =
identical positions; -1 off).  The `.res` is shared by every waveform variant, so
the keep-mask is applied to the shared `.res` and every per-spike file of the
group still at the original count (`.spk`/`.fet`/`.clu`/`.clc` of any method,
post-group stages); other counts are reported and left; symlinked aliases are
filtered once; `<res>.prefilter` keeps the original times.  Verified end to end:
all filtered files equal the original rows under the mask and both `.spk`
variants are byte-identical to fresh re-extractions at the new `.res`.

</details>

## 2026-10-08 — ndm_detectspikes (stderiv): flat-run rejection and duplicate removal

<details>
<summary>Details</summary>

**`flatRunMax`** (engine `-F`, default off) rejects a spike whose raw window has
more than N identical consecutive samples on any channel (zero-filled gaps,
stuck lines).  **`dedupSamples`** (engine `-U`, wrapper default 0) drops a spike
re-centred onto (or within N samples of) the previous kept one — the historical
re-centring window collapses a few pairs onto one sample.  Also fixed: when the
waveforms are streamed to disk (larger than `memCapGB`), a rejected spike was
skipped instead of written as a placeholder row, so the compaction that drops
rejected rows by index shifted every later waveform by one (8,484 of 8,485
misaligned under `-F` in a test; latent before, since the stderiv flat-channel
test practically never fires).  Note: a median-high-passed `.fil` has frequent short
per-channel zero runs (filter output, not dropouts); `flatRunMax: 5` removes 31% of spikes
there — see `doc/ndmanager-plugins/commands/spike-detection.md`.

</details>

## 2026-10-08 — custody: `.spk`/`.res` of a `_D` feature-space token resolve to its waveforms

<details>
<summary>Details</summary>

**Shared-artifact resolution.**  `_D<lag><dims>` names a feature space over the
waveforms of the token without it, but `resolve()` fell back
method → standard → untagged, so Klusters opened on `.clu.stderiv_C5_D34.N`
read the raw `.spk.standard.N`.  Shared types now try the waveform token
(`stderiv_C5_D34` → `stderiv_C5`) right after the exact method, and offer that
path when nothing exists, so a Klusters realign or Save As writes the one real
`.spk`.  This replaces the `.spk.<…>_D34` copy/link: a link is renamed to
`.stalebkp` by fiber-kit's de-dup and replaced by an independent copy on a Klusters
realign save (leaving the `stderiv_C5` waveforms out of step with the updated
`.res`).  Same rule in `custody.hpp`, `ndm_custody` and `ndm_resolve_io.py`;
eight new rows in `custody_vectors.tsv` (95 checks, all three runners pass).

</details>

## 2026-10-08 — ndm_pca: lagged feature space (`_D<lag><dims>`)

<details>
<summary>Details</summary>

**ndm_pca — `method: <token>_D<lag><dims>`.**  The lagged feature space
(per channel PC1 at −lag/0/+lag samples, + PC2 when dims is 4) existed only in
fiber-kit (`fiber-session --feat-lag … --emit-fet --emit-pca`).  `ndm_pca` now
produces it: a `_D` suffix on the method (e.g. `stderiv_C5_D34`) reads the `.spk` of
the base token, writes the ordinary `.fet/.pca` as before, and also writes
`.fet/.pca.<token>_D<lag><dims>` from the same fitted, varimax-rotated
eigenvectors (`process_pca --lag/--lag-pc2/--lag-fet`).  The lag basis is an
ordinary PCAE file on the window widened by the lag (centered 0, zero means), so
the lag `.fet` is the plain projection of the `.spk` through it.  Verified equal,
value for value, to fiber-kit's `lag_basis`/`lag_project` on the reference chunk;
without `--lag` the outputs are byte-identical.  See
`doc/ndmanager-plugins/commands/pca.md`.

</details>

## 2026-10-08 — stderiv detection: threshold noise base, re-centring, hybrid arm

<details>
<summary>Details</summary>

**ndm_detectspikes (stderiv) — `thresholdNoise`.**  The stderiv detector
measured its noise σ on the spatial derivative but thresholded the spatial +
temporal derivative, whose noise is ~0.6× smaller on the reference session, so
`thresholdFactor` 0.8 meant ~5.3σ of the detection signal instead of 3.2σ.
New optional `thresholdNoise: stderiv` (engine `-T stderiv`) measures σ on the
signal actually thresholded — the convention `ndm_reextractspikes_stderiv`
already used.  Default `sdiff` keeps the historical behaviour (default run
byte-identical `.res`/`.spk` on the reference chunk).  Every run now prints the
effective threshold in detection-signal σ.  See
`doc/ndmanager-plugins/commands/spike-detection.md`.

**ndm_detectspikes (stderiv) — `recenterMode`, `recenterHalfWidth`.**  Where
each detection is placed before the shared `.res` is written: `stderiv`
(default, historical: max Σ|stderiv|) or `raw` (the group-referenced raw
trough), over the historical window or ±`recenterHalfWidth` samples (engine
`-C`, `-W`).  The historical window moves ~10% of detections by >5 samples on
the reference chunk.  `raw` ±8 sharply tightens units whose stderiv waveform
has several near-equal extrema (unit 26 stderiv-space dispersion 15.2 → 1.7)
with the median unit unchanged.  Also fixed: the engine wrote the re-centred
times to `.res` only when a spike was rejected or the re-centring reordered
spikes; otherwise `.res` kept the pass-1 detection times while the waveforms
were cut at the re-centred positions.  The final timestamps are now always
written.  **Behaviour change for existing sessions:** whether a group's `.res`
was re-centred depended on whether re-centring happened to reorder that group's
spikes.  On the reference chunk as one 8-channel group it did (output unchanged
by this fix); split into two 4-channel groups it did not, and 305 and 1287
spikes had `.res` ≠ waveform position.  Existing `.res` files may therefore be
un-re-centred for some groups; re-run detection to make them consistent.

**ndm_detectspikes (stderiv) — `detectArms: hybrid`.**  Optional second
detection pass on the group-referenced raw signal (engine `-H`), merged with the
stderiv pass by the refractory rule; each arm thresholds at
`thresholdFactor × 4σ` of its own signal (implies `thresholdNoise: stderiv`).
Recovers spikes whose footprint spans adjacent sites, which the spatial
derivative attenuates; on the reference chunk at ~5.3σ per arm it raises
detections 11.5k → 28.3k, mostly multi-unit activity, with genuine missed spikes
for at least one high-rate unit.  Default unchanged (byte-identical).

</details>

## 2026-10-08 — Klusters: rebindable keyboard + mouse, and the Overview interaction redesign

<details>
<summary>Details</summary>

Work from 2026-10-06 to 10-08 that put every Klusters key, mouse button and wheel
binding on the `input::` registry (the engine the 2026-10-10 entry describes) and,
on top of it, redesigned how the Overview Display is navigated. Reference:
`src/klusters/docs/INPUT_SYSTEM.md`.

**What changes for the curator:**

- **Navigation is uniform.** Ctrl+wheel zooms and Ctrl+left-drag pans in the
  feature, trace and matrix views. On the waveforms and the auto-correlogram
  Ctrl+wheel scales **amplitude** instead; on the trace, Ctrl+Shift+wheel scales
  amplitude. The trace Ctrl-pan scrolls on through the recording at the window edge,
  and trace amplitude auto-fits the data on first load.
- **No default tool.** The rubber-band Zoom tool is retired; the data views open
  with a plain pointer.
- **The four curation tools are toggles.** New Cluster (Ctrl+1), Split (Ctrl+2),
  Delete Artifact (Ctrl+3, new) and Delete Noise (Ctrl+4, new) are
  mutually-exclusive checkable toolbar/menu buttons; selecting the active one again
  returns to the pointer. Each display keeps its own tool and the toolbar tracks the
  active display.
- **A plain click picks.** In the feature view it scrolls the trace to the nearest
  spike, in any projection; the Select Time tool is gone. In the trace, a click
  selects the nearest spike's cluster from **all** clusters, and Shift+click adds it
  to the selection.
- **Smaller changes.** Trace double-click is disabled (it crashed after a click had
  changed the selection). A double Escape clears the cluster-palette selection.
  Trace Prev/Next spike is Ctrl+Shift+Right/Left and steps through the whole
  recording when no cluster is selected.

**Preferences ▸ Input** is generated from the registry. Every command is listed,
including menu actions that ship without a shortcut, with a key, mouse-button or
wheel editor per row, conflict highlighting, per-row reset and "Reset all to
defaults". **Keymap layouts** save and reuse a whole binding set, with a bundled
read-only *Default*. **Help ▸ Keyboard Shortcuts** gains a registry-generated Mouse
section.

**Internals.**
- The TraceView press monolith is split into one command per tool mode on a shared
  click-geometry preamble (`resolveClickGeometry`, `nearestChannelAt`).
- The four curation matrices share their Ctrl navigation by composition
  (`MatrixNavigator`, `registerMatrixNav<V>()`, `matrixResolveClick()`), not a
  common base class.
- Four structural key handlers (the Esc family, Left/Right tab switching,
  Tab/Backtab, PageUp/Down) stay inline in `eventFilter` by design;
  INPUT_SYSTEM.md explains why.

**Live-pass status.** Confirmed on the workstation: the Preferences focus and
conflict-tint fixes, the waveform Ctrl+click channel pick, and the trace
Ctrl-zoom/pan. Everything else is behaviour-preserving by construction but still
wants a live pass. If a binding seems to have regressed, check Preferences ▸ Input
first: a stale saved override can mask a binding and look exactly like a code bug.

</details>

## 2026-10-06 — Klusters: paint-structure refactor

<details>
<summary>Details</summary>

A staged refactor of the classic views' 2003-era paint machinery (ClusterView,
WaveformView, CorrelationView, ErrorMatrixView, TraceView), so overlays and new
views are cheap to add. Reference: `src/klusters/docs/VIEW_SYSTEM.md` §2.2.

- **Monotonic `invalidate(level)`.** All 150 call sites that raised the shared
  `drawContentsMode` flag now go through `BaseFrame::invalidate`, which only ever
  raises it toward a full redraw. A later refresh can no longer cancel a queued
  redraw within the same paint; that was the cause of the lineage overlay sometimes
  not drawing on entry, now fixed. The one legitimate lowering, each `paintEvent`'s
  terminal reset, stays direct.
- **`BufferedView` base** (`BaseFrame → BufferedView → {ViewWidget, TraceView}`). It
  owns the double buffer and a template-method `paintEvent`. WaveformView,
  ClusterView and CorrelationView are migrated. ErrorMatrixView and TraceView keep
  their own `paintEvent` by design (device-coordinate matrix; init/resize prelude),
  with a comment saying why. Incidental fix: the buffer is now sized to the
  viewport; it used to be 10 px larger, which rebuilt it on every paint.
- **Shared geometry.** WaveformView's per-channel baseline math (six open-coded
  copies) goes through `channelStep()` / `channelBaselineY()`.

No behaviour change intended beyond the redraw fix. Optional follow-ups (a
coordinate-space RAII guard, an overlay registry, folding the newer matrices onto a
shared base) are listed in `doc/dev/ROADMAP.md`.

</details>

## 2026-10-05 — Klusters: template curation in the feature space

<details>
<summary>Details</summary>

The curator now builds each template class by hand in Klusters, instead of
`fiber-template` inventing the drift/adaptation binning. The class is a lineage
tree: one drift root per session time region, with lower-amplitude
(adaptation) and locked-pair collision leaves. Native generation becomes the final
refinement step. Covers work from 2026-10-05 to 10-06. File references:
`doc/ndmanager-plugins/formats/wtl.md`, `wti.md`, `wtf.md`.

**Model and files.**
- **A node is a running `{mean, std, count}` summary, not a spike list.** Folding a
  selection combines summaries exactly with the parallel-variance formula. The
  **mean** is used, not the median, because it recombines exactly from summaries.
  Guarded by `core_test_running_stats`.
- **Separate files.** `.wtl` (v2) is the editable lineage source. `.wti`/`.wtf`
  (v2: a `parent` column and a `collision` link kind) remain the auto-generated
  library. The committed model renders to its own **`.mti`/`.mtf`**, using the same
  on-disk schema as `.wti`/`.spk`. This fixes a commit of an all-placeholder lineage
  overwriting the real library; such commits are now refused.
- **Native generation.** `template_generate.hpp` in neurosuite-core is a
  byte-validated port of `fiber_template.py`'s `generate()` and can run per cluster;
  the external plugin is now a fallback.

**Editing (Shift+E, in the feature view).**
- **Preferences ▸ Display ▸ Templates mode** is the master switch (on by default).
- The overlay forces time × the best energy axis and draws each region's root as a
  ribbon above the scatter, with leaves below. A fresh session is auto-seeded with
  empty placeholders.
- Shift+left-drag moves a region boundary. Right-click anywhere in a region adds
  drift/adapt/collision nodes, splits or commits. Edits auto-save and have their own
  Undo/Redo in that menu.
- A left-column **Template palette** lists the classes: ★ marks the primary, which
  `s` or a click toggles; New and Delete; an absolute/best-fit scale checkbox
  (absolute by default).
- The primary class's **mean±std band** draws over the live waveforms, in review
  mode too, and tracks amplitude scaling and cluster changes.

**Matrices.** Marking nodes (click, rectangle lasso, or right-click) adds
cluster×template rows/columns, in time order, to all four curation matrices:
- Template, Drift and Residual show their own statistic.
- Error is all grey; it has no template equivalent.
- Cells where the cluster has no spikes in the node's window are dimmed, not hidden.
- Clicking a template cell selects that cluster and overlays only that node.

The template strip now honours all six scoring modes, and the raw metric is
normalised in log space so pairs are distinguishable. The matrices' shared code was
consolidated into `MatrixTemplateStrip` and `MatrixViewport`, with a geometry unit
test.

**Fixes found in live runs.**
- A Sort-Clusters event filter swallowed every right-click before the lineage menu
  could open.
- Folding a selection read a non-existent stage-tagged `.spk` path, so regions never
  filled.
- Deleting a class left its ribbon on the scatter.
- Palette `s` was bound to oblique-basis pinning and eaten by list type-ahead.
- A per-node waveform stagger was tried and reverted.

**Open.** Most GUI items are compile + link verified and want a live run. The
Template Library tab is still present as the advanced-CRUD fallback. fiber-kit's
`.wtl` reader still speaks v1 and needs a separate fiber-kit patch.

</details>

## 2026-06-08 — Klusters high-cluster-count rendering perf, PCA-Center Align All profiling + setup hoist, cross-cluster GPU-batch design

<details>
<summary>Details</summary>

A performance session focused on sessions with thousands of clusters: the
remaining per-cell matrix render hotspot, palette rebuild cost, and an
investigation into why "PCA-Center Align All" runs sequentially and slows as it
progresses.

**Klusters — template matrix rendered as one image.** `TemplateMatrixView::drawMatrix`
filled every cell with its own `fillRect` (≈9 M calls at 3000 clusters) on every
paint — the same hotspot already fixed in the error matrix.  It now builds an
8-bit indexed `QImage` (one pixel per cell, `NB_COLORS` color table, diagonal →
black) by scanline and blits it once with `drawImage` scaled to the matrix rect
(smoothing off).  The white at/above-threshold outlines are kept as a separate
cheap scan pass (per-cell score compare, `drawRect` only for cells above
threshold) and the yellow selected-pair outlines are unchanged; color maths is
identical.  Rendering change — worth a visual confirm on a real session.

**Klusters — palette color swatches cached.** The palette rebuild created a
fresh `QPixmap` + `QPainter` per cluster just to produce a solid color swatch
(filling with `backgroundColor` then overpainting the whole 12×12 with the
cluster color).  Cluster colors cycle a small fixed palette, so the swatch is
now cached in a `QHash<QRgb,QPixmap>` and reused — a rebuild creates only as many
pixmaps as there are distinct colors.  `QPixmap` is implicitly shared, so reuse
is free; visually identical.

**Klusters — PCA-Center Align All: opt-in per-phase timing.** Investigation
confirmed the batch is sequential (one `RealignWorker` per cluster, the next
launched only after the prior finishes) and that the accept path is cheap
(`forceClusterRefresh` early-returns for clusters not currently shown).  To find
which phase actually costs, `realignSpikes` now emits one line per cluster —
`setup / compute / writeback / total` ms — when `NS3_REALIGN_TIMING=1` is set.
No-op (one cheap `QElapsedTimer` pair) otherwise.

**Klusters — PCA-Center Align All: PCA basis cached across the batch.**
`realignSpikes` re-read the PCA basis (`.pca`/`.pcaD`: header + per-channel means
and eigenvectors) from disk on every call.  A batch runs over one group whose
basis never changes, so this was N redundant reads.  The loaded basis is now
cached on the document, keyed by path + file mtime so it self-invalidates if the
basis is regenerated.  Scope note: the basis is the *only* batch-invariant part
of setup — each cluster's waveforms/timestamps are fetched by direct `fseeko` to
that cluster's own spike offsets, not a full-file scan, so they cannot be
hoisted; the win is bounded by basis-read time (small for compact bases, larger
for full-waveform multi-channel ones).

**Klusters — PCA-Center Align All: the slowdown was the curation log, not I/O.**
`NS3_REALIGN_TIMING` (with the per-phase + inter-cluster-gap breakdown added this
session) showed the realign itself is ~2 ms/cluster; the flat ~130 ms per cluster
and the steadily growing inter-cluster gap were both the curation snapshot.
`snapshotClusters()` — invoked by `logBefore` *and* `logAfter`, so twice per
cluster — runs `computeAllCentroids()`, a full O(all-spikes × dims) pass over the
entire session, only to populate the snapshot's nearest-neighbor field.  Over
~1000 clusters that is ~2000 whole-dataset scans.  Fixed by logging the whole
Align-All as a single curation action: `beginRealignBatchLog(clusters)` takes one
"before" snapshot, the complete/abort paths take one "after" snapshot, and
`realignSpikes` skips its per-cluster `logBefore`/`logAfter` while the batch
suppress flag is set (set before any worker starts, cleared after the last, so the
sequential workers never race).  `computeAllCentroids()` now runs twice per batch
instead of twice per cluster; undo treats the Align-All as one action.  Diagnosis
also added opt-in per-phase realign timing (setup/compute/writeback, then a
writeback sub-split and an inter-cluster gap, all via `NS3_REALIGN_TIMING`) and a
per-batch PCA-basis cache.

 Documented why "push the
whole Align-All onto the GPU" is bounded: even the existing GPU path is
CPU-read → GPU-compute → CPU-read/apply, and only the (spike × candidate)
PCA-energy evaluation runs on the device — the `.fil` window reads and the
re-read/apply are disk I/O that stays on the CPU.  The design specifies a batched
dispatcher API (`refineBatch` + `RefineBatchDesc`), a one-block-per-spike kernel
over all clusters concatenated, host-side chunking/double-buffering, a CPU-batched
fallback (validatable without a GPU), and a `G==1` correctness oracle.  Whether it
is worth building depends on the `NS3_REALIGN_TIMING` breakdown: it accelerates
the *compute* phase only, not disk I/O or the writeback/sort path.  See
`doc/design/realign-gpu-batch.md`.


A maintenance + investigation session spanning Klusters interaction bugs, a
threading race, build-system optimization hygiene, and the first piece of the
cluster data-model rework.

**Klusters — cluster palette Up/Down skipped rows.** Up/Down navigation chose
the geometrically nearest item by *column* first, breaking ties by row.  Because
palette items are cluster-number labels of differing widths on a free-flow grid,
their visual-rect centers do not align into columns, so a single keystroke could
jump several rows.  Reworked to pick the adjacent *row* first, then the nearest
column within it — movement is now provably one row.  Left/Right (list order)
and the wrap-around path are unchanged.

**Klusters — data race in min/max recomputation.** `Data::minMaxDimensionCalculation`
(run on `minMaxThread`) snapshotted the spike table into a block-scoped local
that was destroyed immediately, then read the *live* `spikesByCluster` in its
inner loop using offsets from the cluster-map snapshot.  A concurrent pointer
swap from a reassignment (`prepareUndo`) or undo/redo could make offsets and
data come from different tables — a stale result or an out-of-bounds read.  Now
the spike-table snapshot is held for the whole scan and read in place; the
worker runs entirely on private copies.  No other worker thread reads these
tables directly (they use the mutex-guarded dict + status-flag protocol).

**Klusters — redundant `minMaxThread` wait-loops.** The eleven
`while(!minMaxThread->wait()){}` join sites in `data.cpp` were dead loops around
a blocking `QThread::wait()` (Forever deadline never returns false); replaced
with a plain `wait()`.  Behavior-preserving.

**Build system — `-ffast-math` / `-march=native` made explicit per-target
opt-ins.** Investigation found the global flag block in
`cmake/ZenOptimizations.cmake` applies flags via
`set(CMAKE_<lang>_FLAGS_<cfg> … CACHE STRING … FORCE)`, which is a no-op (a
normal-variable shadow keeps the default), so the intended global
`-march=native` / `-funroll-loops` / `-ffast-math` were **never reaching the
compiler** — only per-target flags (KiloKlustaKwik, a couple of plugins, CUDA)
took effect; LTO (set via `CMAKE_INTERPROCEDURAL_OPTIMIZATION`) was verified to
be genuinely active.  Rather than silently flip everything global (non-portable
binaries), `-ffast-math` and `-march=native` are now opt-in `INTERFACE` targets
(`ns_fast_math`, `ns_native_arch`): CXX-only, Release/RelWithDebInfo only, gated
on `NS_ZEN_OPT`, always defined.  `ns_fast_math` is wired into the KlustaKwik
numeric core; `ns_native_arch` into the self-contained CPU signal-processing
plugins (extractspikes/reextract/refeaturize variants, denoiseuniform,
medianfilter, drifttracker).  `process_pca*` is deliberately excluded (its math
is in GSL — recompiling the plugin does nothing — and it is reproducibility-
sensitive).  Measurement on real kernels: `-march=native` alone is a modest,
loop-shape-dependent win (~5% on the denoise kernel, ~2.5× on long FP reduction
chains); the large speedups need `-ffast-math` (reassociation), which is left
opt-in for the numerically-insensitive kernels only.  The broken global
mechanism is documented in `ROADMAP.md` and left unfixed pending a portability
decision.

**Roadmap — performance and data-model debt recorded.**  Added a
performance/optimization section (profiling-first plan, memory-bandwidth
reduction via `float`-where-safe and pass fusion, V-cache blocking for
reuse-heavy drift/PCA math, GPU feed path, OpenMP scaling, PGO) and a Klusters
data-model debt entry.

**Klusters — `SpikeAssignment` sparse-set store (prototype, not yet wired).**
Cluster reassignment is currently O(total spikes): every edit rebuilds the whole
`spikesByCluster` table and pushes a full table copy onto the undo stack.  Added
a header-only, Qt-free `SpikeAssignment` (`clusterOf[]` label array == the `.clu`
column, packed per-cluster `members` sparse set, `slotOf[]` back-index) giving
O(spikes moved) edits and delta-based O(moved) undo/redo, plus a standalone test
(randomized equivalence vs an independent reference, invariant checks,
undo-depth trimming, and an O(moved)-vs-O(N) perf demonstration: ~50× at 5M
spikes vs a conservative full-copy baseline).  Built alongside the existing
model; integration into `Data` (replacing the table and the reader-thread
concurrency rework the in-place model requires) is deferred to the real build,
where it must be validated for bit-identical `.clu` output.

A companion `SelectionGrid2D` (also header-only, Qt-free, with a standalone
test) addresses the *other* half of reassignment latency — the O(spikes-in-
edited-clusters) region-test scan that identifies which spikes a lasso selects.
It is a uniform grid over the displayed 2D projection (rebuilt only when the
dimension pair changes) that tests only points in cells overlapping the polygon
bounding box; the test validates it returns exactly the brute-force result over
300 random polygons and shows a ~77× query speedup for a small lasso over 2M
points.  Predicate parity with `QRegion::contains` is a wiring-time concern,
documented in-header.

**Klusters — error matrix now highlights selected pairs.** `ErrorMatrixView`
already supported multi-pair selection (Ctrl-click to add/remove), but
`drawMatrix` drew no highlight, so the selected cells were invisible against the
probability colors — making it hard to see and curate pairs on a dense matrix.
Added a yellow cosmetic-pen outline for every pair in `selectedPairs`, placed by
the same world-coordinate cell layout the matrix uses (column = `pair.first`,
row = `pair.second`); a cosmetic pen keeps the outline a constant 2 px at any
zoom, and pairs whose cluster no longer exists are skipped.

**Klusters — template matrix multi-select + per-pair highlight.** The template
matrix was single-pair: a click set one `selectedA`/`selectedB` and only that
one cell was outlined.  It now accumulates a selection the same way the error
matrix does — Ctrl-click toggles a pair in/out (rebuilding the shown-cluster set
from what remains on toggle-off), a plain click replaces it with one pair — and
`drawMatrix` outlines every selected pair, not just the most recent.  This backs
the group-by-`G` workflow: the selected pairs' clusters are added to the active
view and `G` (a window-level shortcut) groups them, so no Apply step is needed
for grouping.  `selectedA`/`selectedB` still track the most-recent pair, so the
threshold slider / Apply single-pair refinement tool is unchanged.

All three matrix-view changes are syntax-checked against Qt headers but not yet
visually verified — this environment has no Qt6 build or display.

**Klusters — error matrix pan/zoom.** The error matrix descends from
`BaseFrame` (world-coordinate `ZoomWindow`) but had overridden the press handler
to repurpose clicks for pair selection, which also disabled the inherited
rubber-band zoom — leaving no way to magnify a dense matrix to see and pick
pairs.  Pan/zoom now drives the existing `ZoomWindow` without taking the
selection click, mirroring the template matrix's gestures: Ctrl + Left-drag pans
(armed on press, engaged only past a 3 px threshold so a quick Ctrl-click still
reaches the Ctrl-add selection path; the world delta is the difference of two
`viewportToWorld` conversions so the grabbed point tracks the cursor), Ctrl +
wheel zooms around the cursor, and double-click resets to the full matrix
(restoring `BaseFrame`'s standard gesture).  Plain click is unchanged.

**Build — Qt6 audit + dead Qt5 shim removal.**  Verified the whole C++ tree is
on Qt6: every `find_package` is Qt6, every link target is `Qt6::`, resources use
`qt_add_resources`, and there are no qmake `.pro`/`.pri` files.  A scan of all
585 source files found no Qt6-removed APIs in use — `QRegExp` appears only in a
comment, `setResizeMode(QListWidget::Adjust)` and `QLabel::setMargin` are the
still-valid `QListView`/`QLabel` members (checked against the Qt6 headers), every
`SkipEmptyParts` is `Qt::`-qualified, and no bare `endl` sits on a `QTextStream`.
The only Qt5 *code* artefacts were four orphaned `ECMQt4To5Porting.cmake` KDE
porting shims (klusters, libklustersshared, ndmanager, neuroscope); a whole-repo
grep confirmed nothing `include()`s them or calls their `qt5_*` macros, so they
were removed.  The remaining "Qt5" strings in the tree are provenance comments
only.  Syntax-validation for this fork now runs against real Qt6 headers (the
edited and patched Qt files — clusterPalette, data, errormatrixview,
templatematrixview — all pass a full Qt6 `-fsyntax-only` compile).

**Build — CMake pipeline audit fixes.**  A full audit of the CMake pipeline
(top-level + all subproject roots, configured end-to-end against real Qt6) drove
several fixes:

- **-march=native is now a global opt-out** (`NS_NATIVE_ARCH`, default ON)
  instead of an opt-in `ns_native_arch` target.  The project ships no binaries —
  every user builds from source — so compiling for the host ISA is the right
  default; pass `-DNS_NATIVE_ARCH=OFF` for a portable/cross build.  This also
  retired the dead `set(CMAKE_<LANG>_FLAGS_<CFG> … CACHE … FORCE)` block (a
  verified no-op) in favor of a working `add_compile_options()` (C/CXX,
  Release/RelWithDebInfo, GNU/Clang-family).  `-ffast-math` stays opt-in via
  `ns_fast_math` (it perturbs results, not just portability).
- **Optional converter plugins degrade gracefully.**  `process_extractleds`
  (FFmpeg) and `process_aomconvert` (HDF5) used hard `REQUIRED` dependency checks
  while being added unconditionally, so a single missing optional dependency
  aborted the whole monorepo configure.  They now probe quietly and skip with an
  informative message (mirroring `process_resample`).
- **nphys-data** (data-only) is declared `LANGUAGES NONE` so it no longer forces
  a compiler, and its `CPack` include is guarded to standalone builds.
- **kiloklustakwik** defers `CMAKE_CUDA_ARCHITECTURES` to the top-level value
  (incl. native auto-detect) when set, instead of overriding it with a different
  list.
- **Standardisation:** `cmake_minimum_required` unified to `3.22...3.31` across
  the top-level and every subproject root; the inert
  `ECHO_OUTPUT_VARIABLE ECHO_ERROR_VARIABLE` keywords in the `NS_INSTALL_DEPS`
  `execute_process` calls replaced with `COMMAND_ECHO STDOUT`.

Follow-up (now resolved, see below): four targets hardcoded `-march=native`
directly in their own flag lists, so they stayed native even under
`NS_NATIVE_ARCH=OFF`.

**Build — GPU-backend detection deduplicated.**  The CUDA architecture list, the
oneAPI/icpx search hints, the nvcc location search, and the pre-`project()` icpx
compiler auto-selection were duplicated between the monorepo top-level and the
kiloklustakwik subproject.  Factored into a new `cmake/GpuBackends.cmake` module
(`ns_gpu_find_cuda_compiler`, `ns_gpu_select_cuda_arch`,
`ns_gpu_select_sycl_compiler` + `NS_GPU_CUDA_ARCH_FALLBACK`, `NS_GPU_SYCL_HINTS`);
the two consumers shed ~110 net lines, with the logic extracted verbatim so
behavior is unchanged.  (klusters was subsequently folded in too — see below.)
The GPU-enabled probe branches (nvcc found, native-arch, icpx compiler switch)
are not exercised in a toolkit-less sandbox and should be verified on a GPU
toolchain.

**Build — klusters folded into the GPU-backend module.**  Since klusters is only
ever built within the repo, it now includes `cmake/GpuBackends.cmake` and its
SYCL probe uses `NS_GPU_SYCL_HINTS`, removing the last duplicated copy of the
oneAPI/icpx hint list.  klusters keeps its own SYCL control flow; only the hint
data is shared.

**Build — removed redundant hardcoded `-march=native`.**  With `-march=native`
now applied globally (gated on `NS_NATIVE_ARCH`), the per-target hardcoded copies
were redundant and defeated portable builds.  Removed them from the CXX/HIP/SYCL
`target_compile_options` of process_aomconvert, process_decomposecollisions,
process_resample, process_extractemg and kiloklustakwik (six sites) — all
CXX-language compiles the global already covers.  The vendored libsamplerate
ExternalProject (its own autotools `./configure`, not reached by the global) now
gates `-march=native` on `NS_NATIVE_ARCH` explicitly.  A portable build
(`-DNS_NATIVE_ARCH=OFF`) now has zero `-march=native` in any flags.make (was 4);
the default build still applies it to all 39 C/CXX targets via the global.

**Build — removed the vestigial `ns_native_arch` shim.**  Once `-march=native`
went global, `ns_native_arch` survived only as an empty INTERFACE target so the
plugins' opt-in links kept resolving.  With native now sourced entirely from the
global mechanism, the shim, the ten guarded no-op `target_link_libraries(…
ns_native_arch)` links, and their now-inaccurate comments were all dead weight
and have been removed (`ns_fast_math` is unchanged).  No references to
`ns_native_arch` remain.

**Test — opt-in regression suite (ctest).**  Wired the previously-unbuilt unit
tests into CTest and added a build-system invariants check, gated behind
`NS_BUILD_TESTS` (default OFF; addresses the audit's missing-test-wiring
finding).  Covers the kiloklustakwik algorithm modules (adapt_model,
cluster_hull_split, proxy_isi, wave_knn_split, xcorr_match, refine_logic,
klusters_realign) and the Qt-free klusters data structures (SelectionGrid2D,
SpikeAssignment), plus `build_invariants` — a static `cmake -P` check asserting
the CMake-audit invariants (NS_NATIVE_ARCH global opt-out, `ns_native_arch`
removed, `GpuBackends.cmake` present, graceful optional-dep skips, standardized
`cmake_minimum_required`, nphys-data `LANGUAGES NONE`, no hardcoded
`-march=native`).  10/10 run tests pass; `kk_test_per_channel_split` is
quarantined (`DISABLED`) for a pre-existing 1/30 assertion failure surfaced by
wiring it up.  See `tests/README.md`.

**Klusters — nudge jumped after alignment (stale `.res`).**  After any alignment
step, nudging a cluster moved its timestamps by more than one sample.
`KlustersDoc::realignSpikes` re-extracted each spike's `.spk` waveform at the
realigned window `extTs = oldTs + cumShift`, set the `.fet` timestamp column and
the in-memory timestamp to `extTs`, but wrote the *original* pre-shift timestamp
to `.res.pending` — so the on-disk `.res` was `cumShift` out of step with
`.spk`/`.fet`/memory.  `nudgeClusterTimestamps` then read that stale base and
added ±1, producing a jump of `(delta − cumShift)`.  The write loop now stores
`extTs` to `.res`, matching the nudge invariant's own self-check.  Trade-off:
`.res` event times now shift by `cumShift` (≤ maxShift) as intended; preserving
original detection times for correlograms would instead require a window-aware
nudge.

**Klusters — "PCA-Center Align All" honours the Top-Channels spin box.**  The
whole-document batch hardcoded `--topchannels 2`, ignoring the
`realignTopChanSpinBox` value the single-cluster realign already uses (via
`buildRealignArgs`).  It now reads the spin box (0 = all channels); `--pca-refine`
stays forced since the action is defined as PCA-centered.  Menu label, tooltip,
confirmation dialog and batch log header reflect the dynamic count.

**Klusters — parallelised the CPU realign fallback.**  The PCA-refine CPU path
(taken with no GPU, below the GPU spike-count threshold, or above the working-
buffer cap) ran the per-spike best-shift search single-threaded.  Each spike is
independent — it reads `.fil` and writes only its own `cumShift[i]`/`wavBuf[i]`
slot — so the body is now a `refineSpike` lambda dispatched under OpenMP with one
`.fil` handle and one scratch pair per thread (no shared file position or buffer
contention), `reduction(+:nRefined,nClamped)`, `schedule(dynamic,16)`, gated on
`N≥64` and >1 thread.  A serial fallback (identical results) runs when OpenMP is
absent or per-thread handles can't open.  `OpenMP::OpenMP_CXX` was already linked
to the klusters target; no CMake change.

**Klusters — overlap-aware denominator in template / auto-merge xcorr.**  The
normalized template xcorr (`tmNormXcorr`, and its verbatim duplicate `normXcorr`
in `autoMerge.cpp`) divided every lag by the full-length norms
`√(Σa²·Σb²)` computed once.  At nonzero lags the edge samples are dropped from
the numerator, so the dot product summed over fewer terms than the denominator
assumed — systematically under-counting off-center alignments.  Both copies now
accumulate the dot product and both squared norms over the same overlapping
window inside the lag loop, so each lag is a true cosine over the overlap.  Lag 0
and identical waveforms still give 1.0.  Note: this shifts matrix values at
off-center peaks, so auto-merge thresholds calibrated on the old metric may need
re-checking.

**Klusters — optional Pearson (mean-subtracted) template xcorr.**  An opt-in
Display-prefs checkbox (`templateXcorrPearson`, default off, persisted) switches
the normalized xcorr from cosine similarity to Pearson correlation: each
waveform's overlap-window mean is removed before normalizing (single pass via the
centering identity), so a shared DC offset no longer inflates the score.
`meanSubtract=false` is byte-identical to the cosine path.  All three consumers
(template matrix, pair-xcorr, auto-merge) read the flag once and pass it through,
so the matrix, pairwise xcorr and auto-merge stay consistent.

**Klusters — auto-merge pair scoring is cancellable + shows progress.**  The
O(nClusters²) pairwise xcorr in `computeProposals` ran with no `wasCanceled()`
check and no `processEvents()`, freezing the modal dialog with an unresponsive
Cancel for the whole scoring phase on large all-active runs (~1000 clusters).
The progress range is extended to `2·nClusters+2` and the outer scoring row loop
now polls cancel / advances the bar / pumps events once per row (the inner loop
stays the cancel granularity).  Interim fix; an async-thread migration remains
the longer-term option noted in the file header.

**Klusters — auto-merge median mode no longer truncates 32-bit samples.**  In the
median path, per-spike waveforms were stored in an int16 buffer and 32-bit
samples narrowed with `static_cast<int16_t>` before the per-element median, so on
a 32-bit recording (`isRecordingTwoBytes()==false` ⇔ `nbBits==32`, int32 `.spk`)
any sample beyond ±32767 was corrupted and median templates disagreed with mean
mode (which sums into a `double`).  The buffer is now int32 — exact for both the
int16 and int32 read paths — with the median collection vector widened to match;
median-mode memory for the subsampled spikes doubles, bounded by `medianK·nPts`.

**Klusters — shared `.spk` reader; analysis path committed to int16 (Design A).**
The three template/xcorr consumers (TemplateMatrixThread, PairXcorrThread,
auto-merge) each had their own copy of the spike read: `fseeko` + `fread` + a
2-vs-4-byte branch on `isRecordingTwoBytes()` + sample-major→channel-major
de-interleave.  The whole ndmanager-plugins extractor family writes `.spk`/`.spkD`
as int16 regardless of acquisition `nBits` (and the `.fil`/`.dat` inputs are
int16 too), so the 4-byte branch was dead for any data the toolchain produces —
and for a nominally-32-bit session it would have misread the int16 `.spk`.
Replaced the three copies with one `tmReadSpikeFloat()` that reads int16 and
returns a channel-major float buffer; the de-interleave indexing is verbatim, so
for real (int16) data the result is byte-identical.  The median-mode buffer that
the truncation fix had widened to int32 is now float (exact for int16, consistent
with mean mode).  This is the analysis-side of "cast at the I/O boundary"; the
core load path (`Data::loadClusters` sizing, `WaveformData<long>`) still branches
on `nBits` and is a separate, higher-blast-radius change.

**Klusters — error-matrix wheel zoom anchors the cursor.**  `ErrorMatrixView::
wheelEvent` passed the world point under the cursor straight to `ZoomWindow::
zoom(factor, center)`, whose second argument is the new window *center* — so each
Ctrl+wheel notch recentred the view on the cursor, jumping whatever was under the
pointer to the middle of the view.  Compute the center that keeps the pivot fixed
(`cNew = pivot − (1/factor)·(pivot − cOld)`) so the point under the cursor stays
put; `correctWindow()` still clamps to the matrix bounds.  Panning was already
correct.  (The sister TemplateMatrixView already did cursor-anchored zoom.)

**Klusters — O(1) exact cluster lookup in the palette (perf + correctness).**
`ClusterPalette::selectItems`, which runs on every selection sync, resolved each
requested cluster with `findItems(QString::number(id), MatchStartsWith)`: O(n)
per cluster (O(k·n) overall, sluggish at thousands of clusters) and matched by
label *prefix*, so selecting id 10 could land on "100 - noise".  It now builds a
cluster-id → item `QHash` once and resolves each id exactly in O(1).

**Klusters — O(1) ignore lookup in the error-matrix paint.**  The nbClusters²
cell loop called `ignoreClusterIndex.contains()` (linear over a `QList`) twice per
cell — millions of linear scans per repaint at high cluster counts.  Precompute a
flag vector indexed by cluster index and test it in O(1).

**Klusters — render the error matrix as one image, not per-cell drawRect.**
`drawContents` issued one `painter.drawRect()` per cell (≈9M calls at 3000
clusters) on every REDRAW, i.e. on every pan move and zoom notch — the dominant
cost behind the matrix feeling slow at high cluster counts.  Fill an 8-bit
indexed `QImage` (one pixel per cell, `colorMap` as the color table,
diagonal/ignored → black) by scanline and blit it once with `drawImage` scaled to
the matrix world rect (`SmoothPixmapTransform` off for crisp cells).  Color
computation is unchanged, so the matrix is visually identical; the red
out-of-date border and yellow selected-pair outlines are still drawn over it.

</details>

## 2026-05-16 — Cluster-mean alignment stack + .res/.spk/.fet consistency

<details>
<summary>Details</summary>

Consolidates patches 64, 69–87, spanning several weeks of work on
post-clustering spike alignment and the disk-commit pipeline that
follows it.  Detailed per-patch entries live in
`src/kiloklustakwik/CHANGES.md`; what follows is the integrating
summary.

**Symptom.** After `KlustaKwikExp` completed on a stderiv session,
cluster mean waveforms in Klusters showed a residual ±3 sample
temporal dispersion despite the alignment phases reporting success.
The dispersion was visible in the cluster-mean view, in the
template-matrix mismatch heat-map, and (subtly) in cluster-quality
metrics.

**Root causes — three independent.**

1. **Template never centered before xcorr** (patch64).
   `RunPhase2bMode3Chunk` built the per-cluster mean template, then
   ran xcorr of each spike against it — but never shifted the mean
   so its peak landed on `PeakSampleIndex`.  xcorr therefore pushed
   every spike toward the mean's *natural* peak, which itself was
   off-`PeakSampleIndex` by whatever residual detection jitter the
   chunk's spikes happened to share.  Spikes whose required shift
   exceeded `MaxTimeShift` were silently rejected and stayed at
   their detection positions, leaving cluster spikes scattered up
   to ±5 samples around their cluster mean.

2. **Live-nudge in Klusters had a raw-source ambiguity** (patches
   69, 72).  Both Klusters' `realignSpikes` and KKExp's Phase 2b m3
   alignment block did `.fil`-or-`.dat` silent fallback when picking
   the raw source for re-extraction.  When `.spk` was extracted from
   `.fil` but `.fil` had been deleted or recomputed with different
   filter settings, the code silently re-extracted from `.dat`,
   accumulating mixed-source content in `.spk` on every nudge.
   patch69 added an RMS-based verifier to Klusters; patch72 ported
   it into KKExp.

3. **`WritePhase15Checkpoint` wrote `.spk`/`.fet` but not `.res`**
   (patch85).  When alignment generated non-zero cumulative shifts,
   the checkpoint re-extracted shifted waveforms into `.spk`
   (anchored at `rawTs + sh - PeakSampleIndex`) and wrote shifted
   timestamps into the `.fet` last column, but **never updated
   `.res` itself**.  Klusters then displayed `.spk` windows whose
   peaks aligned with each other but whose stored timestamps
   contradicted the window content — visible as the ±3-sample peak
   scatter.

**Fix path — phased.**

`RunPhase2bMode3Chunk` was rewritten as an iterative xcorr loop
(patches 71–74) that pre-aligns the template to `PeakSampleIndex`
before correlating, accumulates cumulative shifts in `m_cumShift[]`,
re-projects features against the pre-shifted PCA basis at each
iter, and emits a post-pass `.res` monotonicity warning when adjacent
spikes' new timestamps cross.  An RMS-based raw-source verifier
guards the entry to the alignment block; verification failure aborts
this chunk's alignment without polluting `m_cumShift` from prior
phases.

Two `AlignPcaCenter` modes were added on top of the xcorr loop
(patches 83–84) to address residual dispersion that xcorr alone
couldn't reach:

- Mode 1: post-xcorr PCA-centering refine pass.  After the iter loop
  converges, for each spike sweep shifts in `[-maxShift, +maxShift]`
  and minimize the squared distance from the spike's PCA
  representation to the cluster mean's PCA centroid.  Writes
  `m_cumShift[]`, so the disk commit captures it.

- Mode 2: in-memory circular-shift PCA-centering replacement.
  Replaces the entire xcorr iter loop with a circular-shift sweep,
  picking each spike's argmin distance directly.  Does NOT write
  `m_cumShift[]` — purely diagnostic / corrective in memory.

`WritePhase15Checkpoint` was made `.res`-consistent (patch85): after
rewriting `.spk` and `.fet`, the checkpoint also reads the original
`.res`, writes a `.pending` companion with `rawTs + sh` for each
shifted spike, then atomically renames it into place.  All three
files now move together via the `.*.pending` mechanism.

**Residual dispersion after patch85 — stderiv sample-0 error**
(patches 86, 87).  Even with `.res`/`.spk`/`.fet` consistent on disk,
the user's stderiv pipeline still showed sub-sample-level dispersion
in cluster means.  Tracked down to `WritePhase15Checkpoint`'s
per-spike re-extract: for the stderiv variant, it has no cross-spike
state, so the temporal first-difference at the s=0 sample of every
shifted spike uses `prev_sdiff = 0` instead of the actual previous
sample's spatial derivative.  This injects a sample-0 error in every
shifted `.spkD` window — invisible per-spike but accumulates into
cluster-mean dispersion.

Fix: add a `-R` mode to all three `process_extractspikes` variants
(patch86) that reads existing `.res.<grp>` as authoritative
timestamps instead of detecting, then mmaps `.fil` and re-extracts at
exactly `(ts - timeBefore)` for each.  The stderiv variant reads one
extra raw sample at `(ws - 1)` to compute the correct `prev_sdiff`
for the s=0 temporal-diff.  No cross-chunk state needed because
mmap-and-jump is per-spike independent in this read pattern.

KKExp now invokes the appropriate extractor in `-R` mode
automatically at the end of `TimeShiftFinalize` (patch87) when
non-zero shifts were committed.  Pipeline detection via
`m_timeShiftBasis.isStderiv`; sdiff users set
`KKEXP_REEXTRACT_TOOL` explicitly.  Auto-invoke is disabled by
`KKEXP_AUTO_REEXTRACT=0`.

**Klusters polish — separate track, same dates.**  patches 75, 79–82
addressed UX issues surfaced by the alignment debugging work:

- Time dimension excluded from auto-selected features in
  `slotRecluster` (patch75) — including the timestamp column as a
  clustering feature was causing drift-induced spurious splits in
  recluster.
- Mean-subtracted subdimensional recluster mode added for
  single-cluster selection (patches 76–78), exposing sub-structure
  the canonical PCA basis can't separate.  Three patches because
  the first revealed two integration bugs (`reclusteringSpikesByCluster`
  not populated, and a return-value enum collision with
  `OpenSaveCreateReturnMessage`).
- Error and Template matrices now auto-show on document open (patch79),
  removing four mouse clicks from the standard curation entry path.
- Template matrix gained `Ctrl+drag` pan + `Ctrl+wheel` zoom
  (patch80) for sessions with cluster counts that overflow the
  default cell size.
- Session YAML is now staged at the recluster temp basename
  (patch81) — `KlustaKwikYaml.cpp` was looking up `<tempBase>.yaml`
  which never exists, silently defaulting `SamplingRate` to 20000.
- A PCA-projection-energy-maximizing alignment refine pass was added
  to `realignSpikes` (patch82), opt-in via a checkbox in the realign
  dialog.  Per-spike fresh `.fil` re-extract per candidate shift,
  argmax of energy in the basis.

### Files changed

| Area | Files |
|---|---|
| KKExp alignment + disk commit | `src/kiloklustakwik/KK.cpp`, `KK.h`, `KlustaKwik.cpp` |
| extractspikes `-R` mode | `src/ndmanager-plugins/src/process_extractspikes/`, `..._sdiff/`, `..._stderiv/` |
| Klusters interactions | `src/klusters/src/klusters.cpp`, `klustersdoc.cpp`/`.h`, `data.cpp`/`.h`, `templatematrixview.cpp`/`.h`, `spikerealigndialog.cpp`/`.h`, `prefgenerallayout.ui`, `configuration.cpp`/`.h` |
| Live-nudge raw-source verify | `src/klusters/src/klustersdoc.cpp` |

### Environment knobs introduced

| Variable | Effect | Default |
|---|---|---|
| `KKEXP_AUTO_REEXTRACT` | `0` disables patch87 auto-invoke | unset (= enabled) |
| `KKEXP_REEXTRACT_TOOL` | Override tool binary name (e.g. `process_extractspikes_sdiff`) | auto from `isStderiv` |
| `KKEXP_REEXTRACT_SDIFF_ORDER` | Pass `-d <0..3>` to stderiv tool | `3` (ALLPAIRS) |
| `KKEXP_REEXTRACT_VERBOSE` | Pass `-v` to the tool | unset |

### CLI knobs introduced

`process_extractspikes`, `process_extractspikes_stderiv`,
`process_extractspikes_sdiff` all accept:

- `-R` — skip detection; read `<basename>.res.<grp>` and re-extract
  waveforms at those exact timestamps.  Threshold / refractory /
  peak-search args ignored in this mode.

KKExp recognises:

- `-AlignPcaCenter <0|1|2>` — 0 disables PCA-centering (xcorr only);
  1 adds it as a refine pass after xcorr (writes `m_cumShift`); 2
  replaces xcorr entirely with in-memory circular shifts (no
  `m_cumShift` writes).

### Migration notes

For sessions clustered with pre-patch85 KKExp where the dispersion
is already on disk: the cleanest recovery is to apply all of 85–87
then re-run KKExp in mode 1 (`-AlignPcaCenter 1`).  Mode 1 writes
`m_cumShift`, which triggers `WritePhase15Checkpoint`, which now
keeps `.res`/`.spk`/`.fet` consistent, which the patch87 auto-invoke
then refreshes from `.fil` via the patch86 `-R` path.  Mode 2 alone
won't fix existing-disk dispersion (it never writes to disk by
design).

### Known limitations

- After re-extracting `.spkD`, the `.pcaD` basis is still computed
  from the pre-alignment dispersed data.  For maximally-clean
  features, run `ndm_pca_stderiv` + recompute `.fetD` after the
  KKExp run; this is not chained automatically.
- The sdiff pipeline (`.spk` produced by `process_extractspikes_sdiff`)
  cannot be auto-detected by KKExp — it shares the `.spk` extension
  with the vanilla pipeline.  sdiff users must export
  `KKEXP_REEXTRACT_TOOL=process_extractspikes_sdiff`.

</details>

## 2026-04-27 — ndm_start root + `.ndm.<n>.pipeline` files + empty-graph default

<details>
<summary>Details</summary>

Restores the editable Pipeline Designer (rolled back the toggle-only
view shipped on 2026-04-26) and finishes the design with three changes:

**1. Sticky `ndm_start` root.** Every graph has `ndm_start` pinned at
position 0. Undeletable, indegree-0, single-instance, gold "★ ROOT"
badge in the header. The legacy seven flag parameters (`wideband`,
`events`, `video`, `concatenation`, `spikes`, `lfp`, `clean`) live
on the root node's params for backward-compat fallback.

**2. Pipeline files in their own format.** The graph is no longer
serialized inside the session YAML's `programs:` block — instead it
lives in a sibling file:

```
session.yaml                             ← session schema (unchanged)
session.ndm.default.pipeline             ← auto-loaded on document open
session.ndm.best.pipeline                ← named variants via Save As
session.ndm.experimental.pipeline
```

The `.ndm.` infix namespaces our pipeline files so they don't collide
with `.pipeline` files that might be generated by other tools (Klusters,
snakemake, nextflow, etc.) in the same session directory.

New File-menu actions:
- **Save Pipeline** (Ctrl+Alt+P) — writes `<session>.ndm.default.pipeline`
- **Save Pipeline As…** (Ctrl+Alt+Shift+P) — prompts for a name, writes
  `<session>.ndm.<n>.pipeline`
- **Load Pipeline…** — file dialog filtered to `*.pipeline`

The Pipeline tab toolbar also has matching Save / Save As buttons.

Sanitisation rules: pipeline names are lowercased, whitespace collapses
to underscore, all characters outside `[a-z0-9_-]` are stripped.
Existing-file overwrite prompts on Save As.

**3. Empty-graph default.** Loading a session that doesn't already
have a saved pipeline (no `programs[0]==ndm_start` entry) now starts
with an *empty graph containing only the `ndm_start` root*, instead
of being pre-populated with every plugin from the YAML's parameter
pool. The user builds the pipeline from scratch via the palette;
their work is saved separately to `<session>.ndm.default.pipeline`.

**Bash dispatcher priority:**
1. `<template>.ndm.${NDM_PIPELINE:-default}.pipeline` (env-var
   selectable for CI)
2. In-YAML graph (`programs[0]==ndm_start`) — legacy form, kept for
   backward compat
3. Hard-coded flag-driven `do_*` sequence — original ndm_start,
   kept for sessions with no graph at all

Per-plugin failures are warnings, not aborts. A graph reference to a
plugin not on PATH → warning + skip. Existing session YAMLs continue
to work unchanged.

See `doc/design/ndm-start-root.md` for the full design and
`doc/ndmanager/README.md#pipeline-tab` for the operational
walkthrough.

</details>

## 2026-04-27 — ndm_start as the graph root (replaced)

<details>
<summary>Details</summary>

*Replaced by the entry above on the same day.* This earlier draft
serialized pipelines inside the session YAML's `programs:` block;
the final design moved them to separate `<session>.ndm.<n>.pipeline`
files so the graph is independent of the session schema and named
variants can coexist.

</details>

## 2026-04-26 — graphical Pipeline tab in ndmanager (superseded)

<details>
<summary>Details</summary>

*Superseded by the 2026-04-27 entry above.* The toggle-only view
shipped here was rolled back in favor of the editable node-graph
designer that already existed in the repo (commit `4e0de6e`),
extended with sticky-root semantics for `ndm_start`. The
`PipelinePage` class introduced by this entry was deleted; the
parameter-page signal additions (`parameterValueChanged`,
`setParameterValue`) were retained for potential future use.

</details>

## 2026-04-26 — graphical Pipeline tab in ndmanager (original)

<details>
<summary>Details</summary>

New **Pipeline** entry in ndmanager's parameter tree showing
`ndm_start` and its seven dispatch branches (`wideband`, `events`,
`video`, `concatenation`, `spikes`, `lfp`, `clean`) as a clickable
node graph. Each branch corresponds to one of the boolean flags
read by `ndm_start`'s bash, with the sub-step plugins it invokes
(`ndm_resample`, `ndm_extractspikes`, `ndm_pca`, …) shown as
read-only context nodes underneath.

**Bidirectional sync.** Toggle a branch in the graph and the
matching cell in the `ndm_start` ProgramPage's parameter table
updates immediately; edit the table cell directly and the graph
follows. Saving the document writes the new flag values under
`programs[ndm_start].parameters` — no schema changes.

**Default handling.** Sessions that don't include `ndm_start` in
their `programs:` block show the graph in defaults-only read-only
mode. Adding `ndm_start` via Plugins → Add binds the page
automatically. Toggling a flag that was relying on the script
default (e.g. `clean` not present) appends an explicit row so the
next CLI run sees the GUI's choice.

The orchestration order itself stays hard-coded in `ndm_start`'s
bash — only the on/off flags are GUI-editable. Re-ordering would
require making `ndm_start` data-driven and is intentionally deferred.

See `doc/design/pipeline-tab.md` for the full design and
`doc/ndmanager/README.md#pipeline-tab` for the operational
walkthrough.

</details>

## 2026-04-25 — per-probe empirical KlustaKwik priors

<details>
<summary>Details</summary>

Adds `kk_build_prior.py` / `kk_resolve_prior.py` and wires them into
`ndm_subcluster_unmatched`. Lets you train empirical KlustaKwik
defaults once per probe-type-and-shank from accumulated Klusters
curation logs, then reuse them silently across sessions, animals, and
re-extraction passes.

**The workflow:**

1. Curate a few sessions on a shank in Klusters as usual. Annotate
   cluster quality (Good / Uncertain) — that gates which clusters
   feed the prior. Klusters writes `session.curation_log.<group>.jl`
   for every editing operation.
2. Run `kk_build_prior.py logs… --session-yaml … --electrode-group N
   --out-dir ~/.ndm/priors`. One YAML per shank.
3. `ndm_subcluster_unmatched` invokes `kk_resolve_prior.py` silently
   for every shank as it runs. When a prior matches, four headline
   values (`MinClusters`, `MaxClusters`, `MergeThresh`, `PenaltyMix`)
   become the defaults for `read_kk_param`. Per-session `extraInfos`
   overrides still win.
4. New animal on the same probe → same probe_id resolves the same
   prior automatically. New probe type → resolver misses; falls back
   to script defaults; build a new prior after curating.

**Probe identity:** SHA-256 of the canonicalised
`(nChannels, [{channels:sorted, nSamples, peakSampleIndex, nFeatures}…])`
tuple from the session YAML — sample rate deliberately excluded since
it's acquisition config, not probe identity. When the signature
exactly matches an installed `Template-*.yaml`, the friendly name
(e.g. `4-octrodes-32552Hz`) is used as the human-readable id;
hex-hash fallback otherwise. The hash is stored in every prior YAML
as `probe_signature_hash` for fail-loud verification.

**Best-effort everywhere.** Missing resolver, missing prior file, or
hash mismatch all fall through cleanly to script-builtin defaults.
The behavior with no priors built is exactly the previous behavior.

See `doc/design/kk-prior.md` for the full design and
`doc/workflows/empirical-priors.md` for the operational
walkthrough.

</details>

## 2026-04-22 — process_shadowcluster inflation guard + dedicated KK tier for sub-clustering

<details>
<summary>Details</summary>

**Symptom.** `ndm_reextractspikes_stderiv` on `jg05-20120316` group 7:

```
  eligible parents : 13 / 16 clusters
  new spikes       : 162365
    clu 2  (size=282)  shadow=18  +1257 new
    clu 4  (size=710)  shadow=20  +19 new
    clu 6  (size=1868)  shadow=22  +108912 new         ← 60× inflation
    clu 11 (size=4388) shadow=27  +1 new
  unmatched        : 52176
```

108 912 new spikes (67 %) force-matched into a single shadow
(`clu 6`, whose reference size was 1 868).  These spikes bypass the
unmatched bin entirely, so `ndm_subcluster_unmatched` can't touch them:
they surface in Klusters as one giant opaque shadow attached to an
otherwise unremarkable cluster.

**Root cause.** `process_shadowcluster` v1.0 did pure Mahalanobis 1-NN
with a global χ² gate (`p=0.9999`, `d²=54.234` on `df=21`).  A
reference cluster with a heavy-tailed member distribution has its
per-dim MAD × 1.4826 σ's over-estimated; the resulting ellipsoid is
over-wide and absorbs whatever falls nearby.  No downstream parameter
— KlustaKwik or otherwise — can correct this after the fact because
the absorbed spikes are tagged with the parent's shadow id before any
clustering step runs.

### Fix — three coordinated changes

**1. Inflation guard in `process_shadowcluster` (v1.1).**
New CLI args `--inflationRatio R` (default 5.0) and `--inflationCap C`
(default 500).  A parent may absorb at most
`max(C, R × |parent|)` new spikes.  Implementation:

- Pass 1 (parallel, expensive): compute `bestK/bestD²` per new spike;
  record in `tentativeBestK / tentativeBestD2`.
- Pass 2 (serial, cheap): for each over-absorbing parent, sort
  candidate spikes by `d²` ascending, keep the closest `cap_k`, demote
  the tail to the unmatched bin.

Serial cost is `O(n_assigned log n_assigned)` per over-absorbing
parent; typically 1-3 parents trip the cap per group, so the overhead
is negligible relative to the main assignment loop (nFeatDim × nEligible
distance evaluations per spike).  Verbose output now reports both
accepted and demoted counts per parent and a total-demoted summary.
Setting either knob to 0 disables the guard (preserves v1.0 behavior
bit-for-bit).

**2. Tighter default `chi2P`.**  0.9999 → 0.999.  `d²=54.234` vs
`d²≈46.80` on `df=21`.  With robust MAD σ's the 0.9999 gate proved
over-permissive in practice; 0.999 is still generous — robust-stats
literature typically uses 0.99 for outlier rejection — while being
significantly more selective.  The inflation guard is the hard
backstop; this is the per-spike gate.

**3. Caller-block tier in `read_kk_param`.**  `ndm_functions` gains
an optional `-b <caller_block>` flag.  Resolution order is now four
tiers when `-b` is given:

```
  1. spikeDetection.channelGroups[g].klustakwik.<n>    (per-group)
  2. programs[<caller_block>].parameters.<n>           (caller override)
  3. programs[ndm_klustakwik].parameters.<n>           (global fallback)
  4. built-in default                                  (safety net)
```

Tier 2 is skipped when `-b` is absent or equals `ndm_klustakwik`,
preserving backward compatibility.  `ndm_subcluster_unmatched` now
passes `-b ndm_subcluster_unmatched` at every call site (27 retrofit
sites).

### New YAML block: `programs[ndm_subcluster_unmatched]`

Populated in `templates/template.yaml` with outlier-bin-tuned defaults
that deliberately diverge from `programs[ndm_klustakwik]`:

| knob                      | ndm_klustakwik | ndm_subcluster_unmatched | rationale |
|---------------------------|---------------:|-------------------------:|-----------|
| `maxPossibleClusters`     |            500 |                     1000 | outlier bin holds dozens of low-SNR units |
| `mergeThresh`             |           42.0 |                     30.0 | below χ²(21, 0.99)≈38.9; prevents collapse of distinct low-SNR units |
| `nStarts`                 |              3 |                        5 | more CEM local minima in outlier bin |
| `phase15Iters`            |              1 |                        2 | sharper xcorr realignment in low-SNR regime |
| `subspaceRecluster`       |              0 |                        1 | split sub-units that coalesce in full 21D |
| `subspaceReclusterDepth`  |              0 |                        2 | moderate recursion depth |
| `splitRecurseDepth`       |              1 |                        1 | (unchanged — existing default is already right here) |
| `templateMatchScore`      |            0.0 |                     0.85 | waveform-similarity assist (safe in sparse bin) |
| `penaltyMix`              |              0 |                     0.25 | AIC mix counters disproportionate BIC pressure |

Users can override any subset per-session; unset keys fall through to
`programs[ndm_klustakwik]` (tier 3).

### New re-extract YAML parameters

Added to both `programs[ndm_reextractspikes]` and
`programs[ndm_reextractspikes_stderiv]`:

| name                        | default | effect |
|-----------------------------|--------:|--------|
| `reextractChi2`             |   0.999 | **CHANGED** from 0.9999 |
| `reextractInflationRatio`   |     5.0 | new — passed as `--inflationRatio` to `process_shadowcluster` |
| `reextractInflationCap`     |     500 | new — passed as `--inflationCap` |
| `reextractAutoSubcluster`   |       0 | new — when 1, `ndm_subcluster_unmatched` runs automatically after the merge |

XML descriptors (`descriptions/ndm_reextractspikes{,_stderiv}.xml`)
updated in lock-step.

### Files touched

```
src/ndmanager-plugins/src/process_shadowcluster/process_shadowcluster.cpp
src/ndmanager-plugins/scripts/ndm_functions
src/ndmanager-plugins/scripts/ndm_subcluster_unmatched
src/ndmanager-plugins/scripts/ndm_reextractspikes
src/ndmanager-plugins/scripts/ndm_reextractspikes_stderiv
src/ndmanager-plugins/descriptions/ndm_reextractspikes.xml
src/ndmanager-plugins/descriptions/ndm_reextractspikes_stderiv.xml
templates/template.yaml
```

### Backward compatibility

- `process_shadowcluster` without `--inflationRatio`/`--inflationCap`
  on the command line defaults to `5.0`/`500` (guard active).  Sessions
  that must preserve exact v1.0 behavior should set
  `reextractInflationRatio: 0` (or `reextractInflationCap: 0`) in the
  session YAML.
- `read_kk_param` without `-b` preserves the prior three-tier order
  bit-for-bit.
- Existing sessions with no `programs[ndm_subcluster_unmatched]` block
  continue to work — tier 2 is empty, tier 3 (`programs[ndm_klustakwik]`)
  supplies every parameter as before.  Only sessions regenerated from
  the new `template.yaml` or hand-edited to add the block get the
  outlier-bin-tuned defaults.
- `.clu.N` cluster-id convention (shadow-offset and unmatched-bin id)
  is unchanged.

### Expected post-fix behavior for the `jg05-20120316` g7 case

- `clu 6` shadow capped at `max(500, 5 × 1868) = 9340` new spikes
  (down from 108 912; the other ~99 572 are demoted to the unmatched
  bin).
- Unmatched bin grows from 52 176 → ~151 748, and now contains the
  population that was previously hidden inside `clu 6`'s shadow.
- `ndm_subcluster_unmatched` (auto-invoked if
  `reextractAutoSubcluster=1`, otherwise manual) runs KlustaKwik with
  the outlier-bin-tuned parameter block on that 151 748-spike bin and
  discovers its sub-structure rather than leaving it as one amorphous
  pile.
- In Klusters, the operator now sees a realistic distribution of
  low-amplitude units instead of one giant green shadow attached to
  cluster 6.

</details>

## 2026-04-22 — process_reextractspikes_stderiv writes transformed .spkD (correctness)

<details>
<summary>Details</summary>

**Value-space mismatch** between `.spkD` files produced by the main
pipeline and those produced by the re-extract pipeline:

- `process_extractspikes_stderiv` (main) writes **stderiv-transformed**
  waveforms to `.spkD.N` (spatial derivative across group channels
  + temporal first-difference with `sdiff[-1]=0` boundary).
- `process_reextractspikes_stderiv` (re-extract second pass) was
  writing **raw ADC** waveforms to the same `.spkD.N` at the new-spike
  rows, creating a file that contained two different value spaces
  interleaved — reference rows stderiv, new rows raw.

Downstream consequences:

- `process_shadowcluster::projectSpike` projects every new-spike row
  through the `.pcaD` basis unchanged.  `.pcaD` was trained on
  stderiv-transformed waveforms, so projecting raw-amplitude rows
  through that basis yields features concentrated in an arbitrary
  manifold.  The robust-Mahalanobis classifier then assigns ~all new
  spikes to a single shadow cluster, producing the observed
  "162,358 new spikes → cluster 22, one green mass at the far right
  of cluster 5" symptom in the `jg05-20120316-7` reference session.
- Klusters' waveform view reads `.spkD` bytes directly, so reference
  rows show biphasic stderiv shapes while new rows show raw-ADC
  monophasic shapes — visible side-by-side in the per-cluster
  waveform pane of the same electrode group.

### Fix

`process_reextractspikes_stderiv` Pass-2 write loop now applies the
full stderiv transform before writing to `.spkD`:

1. Spatial derivative per time sample via the already-available
   `computeSDiff` helper (ported unchanged from
   `process_extractspikes_stderiv`, identical for SDIFF_NONE /
   SDIFF_FIRST / SDIFF_LAPLACIAN / SDIFF_ALLPAIRS).
2. Temporal first-difference in-place with `sdiff[-1]=0` boundary.
   `prev[]` stores the unclamped sdiff value so subsequent
   iterations subtract from the pre-clamp value, preserving
   bit-identity with the main extractor's output.

Output layout is sample-major `[s*nChanG + ci]`, matching
`process_extractspikes_stderiv`'s `.spkD` layout exactly.  No change
to the `.res.N` write path or to the detection-stage threshold
computation.

### Rolling back previously-corrupted sessions

Any `.spkD.N` files that have been merged in-place by the buggy
version contain raw waveforms at the new-spike rows.  The script's
`.bak` preservation lets the operator revert:

```bash
mv session.spkD.7.bak  session.spkD.7
mv session.res.7.bak   session.res.7
mv session.clu.7.bak   session.clu.7
mv session.fetD.7.bak  session.fetD.7   # if present
```

Re-run `ndm_reextractspikes_stderiv` with the fixed binary after the
rollback.  Groups that have not yet been merged (no `.bak` sidecars)
are unaffected.

### Files

- `src/ndmanager-plugins/src/process_reextractspikes_stderiv/process_reextractspikes_stderiv.cpp`
  — Pass-2 write now applies the stderiv transform
- `src/ndmanager-plugins/src/process_reextractspikes_stderiv/process_reextractspikes_stderiv.h`
  — docstring corrected
- `src/ndmanager-plugins/scripts/ndm_reextractspikes_stderiv`
  — header and inline comments corrected (no behavioral change in
  bash; the binary carries the fix)

</details>

## 2026-04-22 — fetD completion, klusters focus fixes, Pipeline C nudge correctness

<details>
<summary>Details</summary>

Committed as `f2e7fd4 bugfix(klusters) fix focus issue after spike realignment`.
Despite the commit-message framing, this change set closes out three
long-standing concerns together: incomplete `.fetD`/`.spkD`/`.pcaD`
propagation inside KlustaKwik, focus-chain bugs across the klusters
cluster-operation slots, and a correctness bug where `nudge` and
`realignSpikes` silently corrupted `.fetD` on Pipeline-C sessions
(raw `.spk` + stderiv `.fetD`/`.pcaD`).

### KlustaKwik — `.fetD`/`.spkD`/`.pcaD` fallbacks beyond `LoadData`

Prior work introduced `pickInputPath` (prefer canonical, fall back to
D variant) and wired it into `LoadData`.  This session extends the
same resolution to every other session-file open inside KlustaKwik:

- `RealignChunkWaveforms` — in-place `.spk` rewrite now resolves the
  picked variant.  The `r+b` open path is unchanged; its existing
  graceful "skipping" fallback handles read-only mounts.
- `RefeaturizeFromShifts` — both the `.pca` model load and the `.spk`
  circular-shift fallback (when `.fil` is unavailable) use
  `pickInputPath`.
- Four Phase 1.6 / Phase 2 template-match mean-waveform harvests
  (`KK.cpp` ~2674, ~2750, ~3331, ~3449) — each pass opens the correct
  variant.
- `WritePhase15Checkpoint` — `.spk` and `.fet` originals now come
  from `pickInputPath`, and the `.pending` names are derived from the
  *picked* paths rather than canonical literals.  On success the
  `rename(.pending → original)` now commits back to `.spkD.N` /
  `.fetD.N` when that's what was loaded, instead of creating a ghost
  canonical file alongside the real D variant.
- Startup banner shows `(stderiv variant)` when `.fetD.N` was loaded.
- Build tag bumped to `[build 2026-04-21 fetD]`.

### ndm_klustakwik — accept `.fet.N` OR `.fetD.N`

The input-file check split the parameter-file requirement from the
feature-file requirement.  The feature-file check now accepts either
`.fet.N` or `.fetD.N`, matching what KlustaKwik itself resolves.
Stderiv-only groups no longer emit spurious "input file missing"
warnings.

### Three-tier parameter resolution — latent bug fixed

`ndm_klustakwik` referenced seven variables in its `KlustaKwik`
invocation that were never read via `read_kk_param`:
`subspaceDims`, `subspaceRecluster`, `subspaceReclusterDepth`,
`templateMatchScore`, `templateMatchIters`, `splitRecurseDepth`,
`crossChunkTemplateScore`.  Every session up to this point was
launching KlustaKwik with empty-string values for those flags and
silently relying on `param.c`'s fallback to compile-time defaults —
so per-group YAML overrides for subspace reclustering and template
matching were being ignored by the main sort.

Fixes:
- Added `read_kk_param` calls for all seven with sensible defaults
  (`subspaceRecluster=1`, `subspaceReclusterDepth=2`, `subspaceDims=3`,
  `templateMatchIters=10`, others `0`/`0.0`).
- Hoisted `read_kk_param` from `ndm_klustakwik` into `ndm_functions`
  so wrapper scripts can use identical three-tier resolution.  Passes
  `-s ndm_klustakwik` to `read_script_parameter` so tier-2 always
  reads from the single canonical program block regardless of which
  script is calling.

**User-visible effect:** subsequent main-sort runs will actually
apply subspace reclustering on groups whose YAML configures it;
cluster counts per group will likely shift.  Keep pre-patch `.clu.N`
files as `.clu.N.bak-presubspace` before re-running.

### ndm_subcluster_unmatched — full-parity sandbox KK

The sandbox KlustaKwik invocation passed only five flags
(`-MinClusters`, `-MaxClusters`, `-MaxPossibleClusters`,
`-UseFeatures`, `-MergeThresh`).  With `nStarts=1` and
`SubspaceRecluster=0` (defaults), CEM on a 186k-spike outlier bin
predictably collapsed to a single cluster, yielding
`promoted=0 noise_kept=186981`.

Rewritten invocation now passes all 27 KK flags via the shared
`read_kk_param`, matching `ndm_klustakwik` line-for-line except for
the three throw-away-session values (`-UseFeatures all`,
`-fSaveModel 0`, `-SaveIntermediates 0`).  Parity verified by
flag-set diff.

### klusters — palette focus after destructive / batch operations

Every cluster-list operation that auto-selected the next cluster was
then calling `activeView()->focusClusterView()`, which stole focus
to the 2D scatter and silently broke arrow-key navigation.  Four
slots fixed to restore palette focus instead:

- `slotMoveClustersToNoise` (`Delete` key) — `klusters.cpp:2507`
- `slotMoveClustersToArtefact` (`Shift+Delete`) — `klusters.cpp:2526`
- `slotReclusterFinished` — `klusters.cpp:3262`
- `slotRealignFinished` accept branch — removed the
  `focusClusterView()` call that was overriding the
  immediately-prior `setFocusToList()`
- `slotRealignFinished` reject branch — added symmetric palette
  focus restoration (previously left focus on whatever widget the
  modal review dialog destroyed, silently unpredictable)

Leaves existing `focusClusterView` calls untouched where the user's
next action is plausibly 2D inspection (undo/redo, group, nudge
buttons).

### klusters — nudge/realign Pipeline C correctness

`nudgeClusterTimestamps` and `realignSpikes` used a single flag
(`isStderivSession` / `isStderivRealign`) derived from
`m_origSpkPath.contains(".spkD.")` to gate *both* the `.spk` write
branch and the PCA basis + feature-reprojection branches.  These are
genuinely independent signals that only coincide in Pipelines A (both
raw) and D (both stderiv).

On Pipeline C (raw `.spk` + stderiv `.fetD`/`.pcaD`), the single-flag
check returned `false`, so PCA selected `.pca.N` (wrong basis — the
file doesn't exist, or worse exists with different rank) and feature
reprojection skipped the stderiv transform.  Nudge was silently
writing raw-space projections into `.fetD.N` rows, mixing feature
spaces inside one file.

Split into two flags:

- `spkIsTransformed = m_origSpkPath.contains(".spkD.")` — governs
  the `.spk` write branch (apply `applyStderivTransform` iff on-disk
  `.spk` is in stderiv space, else write raw).
- `fetIsStderiv = m_origFetPath.contains(".fetD.")` — governs PCA
  basis selection (`.pcaD.N` vs `.pca.N`) and whether the raw
  waveform is transformed before projecting onto eigenvectors.

Decision gates after the refactor:

| Site | Flag used | File |
|---|---|---|
| realign `.spk` write | `spkIsTransformed` | klustersdoc.cpp:3230 |
| nudge `.spk` write | `isStderivSpk` (= `spkIsTransformed`) | klustersdoc.cpp:3779 |
| nudge feature reprojection (`makeFetRow`) | `isStderivFet` (= `fetIsStderiv && pca.valid()`) | klustersdoc.cpp:3689, 3712 |
| PCA basis file selection | `fetIsStderiv` | klustersdoc.cpp:2599, 3531 |

Pipeline A and Pipeline D sessions are semantically unchanged (both
flags evaluate identically to the old single flag).  Pipeline C is
now correct.

### Files touched this session

- `src/klustakwik/KlustaKwik.cpp` — banner, build tag
- `src/klustakwik/KK.cpp` — 7 call sites use `pickInputPath`;
  `WritePhase15Checkpoint` preserves D variant through `.pending`
- `src/klusters/src/klusters.cpp` — 5 focus-restoration sites
- `src/klusters/src/klustersdoc.cpp` — `nudge` / `realignSpikes`
  Pipeline-C detection split
- `src/ndmanager-plugins/scripts/ndm_functions` — shared
  `read_kk_param`
- `src/ndmanager-plugins/scripts/ndm_klustakwik` — local
  `read_kk_param` removed; 7 missing param reads added
- `src/ndmanager-plugins/scripts/ndm_subcluster_unmatched` —
  full-parity sandbox KK invocation

</details>

## Earlier changes — KlustaKwik

<details>
<summary>Details</summary>

### nRuns flag (new)

`-nRuns N` sets the number of independent CEM restarts **per chunk** in chunked
mode (`ChunkMinutes > 0`). Each chunk independently runs `CEMTwoPhase` N times
with different random seeds and keeps its best-scoring result before handing off
to Phase 2. Phase 0 preseed, Phase 1.5 realignment, and Phase 2 merge each run
only once per pipeline invocation. This is the per-chunk analogue of `nStarts`
in non-chunked mode.  Setting `nRuns 3`
runs three independent pipeline iterations each seeded differently
(`srand(RandomSeed + run)`).  `MinClusters` and `MaxClusters` revert to their
intended role: per-chunk `TrySplits` bounds only, not an iteration driver.
`nRuns 0` (default) preserves the original loop for backward compatibility.

### Phase 1.5: xcorr alignment + .fil re-extraction (reworked)

Phase 1.5 now runs after all per-chunk `CEMTwoPhase` jobs complete and before
Phase 2 cross-chunk model matching.  It proceeds in two steps:

**Step 1 — `RealignChunkWaveforms`**
For each chunk, builds a per-cluster mean waveform from `.spk`, XcorrDispatch-aligns
each spike against the mean, records the shift in `spikeShifts[]`.
Home-chunk first-write-wins: overlap spikes that appear in both chunk k and k+1 get
their shift from chunk k (the chunk where they naturally live).

**Step 2 — `RefeaturizeFromShifts`**
For each shifted spike, re-extracts the aligned waveform from the `.fil` broadband file
at `(rawTs − shift − PeakSampleIndex)`, selecting only the group's channels via
`GroupChannelIds`.  Projects through the saved PCA eigenvectors (PCAE format,
`.pca.N`), re-normalizes using the per-dimension min/range from `LoadData()`, updates
`Data[]` and corrects the normalized timestamp before Phase 2.
Falls back to circular shift from `.spk` (with wrap-around caveat) when `.fil` is not
available (e.g. after `ndm_stripdat`).

New globals auto-detected from YAML at startup:
- `NbTotalChannels` ← `acquisitionSystem.nChannels`
- `PeakSampleIndex` ← `spikeDetection.channelGroups[g].peakSampleIndex`
- `GroupChannelIds` ← channel list for this electrode group

All three can be overridden on the command line.

### New YAML fields read by KlustaKwikYaml

`KKYamlSpikeParams` now exposes: `nTotalChannels`, `peakSampleIndex`, `channelIds`,
`probeId`, `shankIndex`, `probeFile`, `probeLibraryPath`, `sitePositions`.

### Inline drift estimation (disabled by default)

`_RunInlineDriftEstimation()` is registered but not called unless `probeId` and
`probeFile` are set in the YAML and the `.fil` is present.  Invokes
`process_estimatedrift.py` + `process_applydrift.py` as subprocesses after the final
`.clu` write, producing `SESSION.drift` and `SESSION.chunks.N` for the next run.

### TrySplits — split trial fixes (2026-04-02)

- `CEM(nullptr, 0)` → `CEM(nullptr, 1)` in the split trial: the split candidate
  CEM now runs with `enableSplits=true`, letting `TrySplits` find the best
  multi-cluster solution rather than a single random initialization.
- Split trial `nStartingClusters` raised from 3 → 13 (noise + 12 real), giving
  the split CEM sufficient starting clusters to explore the full structure of each
  candidate cluster.
- Infinite recursion fix: `static thread_local int _trySplitsDepth` counter added.
  Split trials at depth > `SplitRecurseDepth` use `CEM(nullptr, 0)` (no further
  recursion). Safe for OMP parallel regions.
- New `-SplitRecurseDepth N` (default 1): how many levels of recursive splitting
  `TrySplits` explores.  `0` = original behavior; `1` = one level; `2` = two.

### Phase 2.5 SubspaceReclusterPerChunk: 3-phase parallel rewrite

Refactored from a single OMP parallel-for-over-chunks to a three-phase model:

- **Phase A (serial):** For each `(chunk, cluster)` pair, compute cluster mean,
  covariance, top-k eigenvectors, project into whitened k-space, normalize,
  compute null score. Produces a flat `WorkItem` list.
- **Phase B (parallel OMP):** Each work item runs `nRuns` × `startK=2..maxSubK`
  restarts of `CEMTwoPhase` (splits enabled via depth saturation) in a local `KK`
  object. Thread cap = `min(nThreads, nItems)`. `schedule(dynamic)` for load balance.
- **Phase C (serial):** Apply accepted splits to `perChunkClass`/`perChunkModels`.
  Serial because `nextLocalId` is per-chunk shared state.

**Depth saturation:** Before each Phase B `CEMTwoPhase` call,
`_trySplitsDepth = SplitRecurseDepth - SubspaceReclusterDepth` is set, so
`TrySplits` fires once but its split trials are non-recursive.

**ID overflow guard (Phase C):** The packed key `chunkIdx * MaxPossibleClusters +
localClusterId` overflows into adjacent chunk's range if `localClusterId >=
MaxPossibleClusters`. Phase C now counts required new IDs before committing each
split and skips if `nextLocalId + nNewIds >= MaxPossibleClusters`. This was the
root cause of the "random cluster" bug.

New `-SubspaceReclusterDepth N` (default 0): controls `TrySplits` recursion depth
inside SubspaceRecluster sub-CEMs, independently of `-SplitRecurseDepth`.

### MergeChunkModels: iterative cross-chunk passes

The entire chunk-pair loop (votes + Mahal + xcorr) now iterates up to
`-TemplateMatchIters` times, breaking when no new `Union()` calls occur.  Enables
cascade matching: a new xcorr merge in pair (k, k+1) can unlock
previously-unresolved clusters in pair (k+1, k+2).

### Progress banners

Phase 1 OMP loop now emits per-run progress to `stderr` inside a critical section:

```
  [chunk 3/9  run 2/3] score=-1243.6  nclusters=12
```

### Parallelism architecture (2026-04-03)

Phase 1 and Phase 2.5 (`SubspaceReclusterPerChunk`) both flattened to `(chunk×run)`
and `(item×run)` parallel axes respectively. Serial reduction after each selects
best run, restores `Class[]`, runs `MStep`, harvests models. `pointPacked` always
rebuilt downstream from `perChunkClass` unconditionally.

### fet/fetD fallback (1st wave, prior session)

Introduced `pickInputPath` in `KlustaKwik.cpp`: prefers canonical
(`.fet`/`.spk`/`.pca`) and falls back to stderiv D variant
(`.fetD`/`.spkD`/`.pcaD`) when canonical is absent.  Wired into `LoadData` so
reextract-style scripts that produce only the D variant no longer need symlink
shims at the KlustaKwik entry point.

The 2026-04-22 commit completes the propagation to every other session-file open
inside KlustaKwik (see top of this file).

</details>

## Earlier changes — ndmanager-plugins

<details>
<summary>Details</summary>

### process_setupgroups.py

- `parse_probe()` now returns `(shanks, geometries)` — per-site `[x_um, y_um]`
  extracted from `probeFile.sites.geometry` in shank-major order.
- `build_spike_group()` writes `probeId`, `shankIndex`, and `sitePositions_um` onto
  each `spikeDetection.channelGroups` entry so all downstream consumers
  (KlustaKwik, `process_estimatedrift`, `process_localise`) can resolve electrode
  geometry without re-loading the probe file.
- `probes[].id` normalized to `probes[].probeId` on write.

### process_estimatedrift.py

- `read_clu()` handles both binary (`int32` header + `int32` ids, written by
  KlustaKwik and Klusters) and legacy text format.  Previously text-only, causing
  silent failure on binary `.clu` files.
- `n_samp` no longer hardcoded to 52; read from YAML `nSamples` per group via
  `--n-samples-per-group` (comma-separated, one per group).
- `build_group_probe_map()` three-tier resolution: spike group `probeId` first,
  then anatomical group cross-reference, then fallback.
- `build_probe_entry_map()` accepts both `probeId` (canonical) and legacy `id`.
- `get_depths()` reads `sitePositions_um` directly from the YAML group when present,
  returning full-length array with `NaN` for null entries (missing geometry).
- `amplitude_com()` masks `NaN`-depth sites.
- `xcorr_shift()` filters `NaN` from depths before computing inter-site spacing.

### process_localize.py

- `site_positions_from_yaml()` reads inline `sitePositions_um` first; only falls back
  to probe-file lookup when the field is absent or has fewer entries than `n_sites`.

### ndm_estimatedrift

- Builds `--n-samples-per-group` from YAML `nSamples` per group.
- All `xml_read`/`xml_count` → `yaml_read`/`yaml_count`.

### All 15 ndm scripts — YAML migration

`xml_read`/`xml_count` replaced with `yaml_read`/`yaml_count` (69 replacements):
`ndm_concatenate`, `ndm_denoiseuniform`, `ndm_extractspikes`,
`ndm_extractspikes_sdiff`, `ndm_hipass`, `ndm_klustakwik`, `ndm_lfp`,
`ndm_localise`, `ndm_pca`, `ndm_redetectspikes`, `ndm_reorderchannels`,
`ndm_resample`, `ndm_spikegrouper`, `ndm_start`, `ndm_stripdat`.

### ndm_klustakwik (earlier)

- `nRuns` parameter read and passed to KlustaKwik.
- `NbSamplesPerSpike` no longer needs to be passed explicitly (auto-detected from
  YAML), but `nbSamplesPerSpike` is kept in the parameter block for override.

See `doc/design/reextractspikes-v1.md`, `doc/design/reextract-v2.md`,
`doc/design/decomposecollisions.md`, `doc/design/subtractspikes-botm.md`
for the full detail of the recent plugin patch series.

</details>

## Earlier changes — jg05-20120316.yaml (sample session)

<details>
<summary>Details</summary>

- All 13 `spikeDetection.channelGroups` entries annotated with `probeId`,
  `shankIndex`, and `sitePositions_um` (inline electrode site geometry).
- Groups 1–8: Buzsaki64L octrode geometry (0–140 µm depth per shank, 8 sites).
- Groups 9–13: A1x32-6mm-50-177 linear geometry (0–1550 µm, 32 sites total,
  tiled across 5 ndm_spikegrouper subgroups).
- All 9 `anatomicalDescription.channelGroups` annotated with `probeId`/`shankIndex`.
- `probes[].id` → `probes[].probeId` (canonical key).
- `ndm_klustakwik.nbSamplesPerSpike` corrected: 32 → 41 (matches actual `.spk`
  files).
- Probe 1 entry corrected to reference `A1x32-6mm-50-177.probe`.

</details>

## Deep technical references

| Topic | File |
|---|---|
| KlustaKwik canonical-engine history (v1.7 → neurosuite-3 diff) | `src/kiloklustakwik/CHANGES-inherited-from-canonical.md` |
| KiloKlustaKwik-internal changes (DipSplit, time-shift merging) | `src/kiloklustakwik/CHANGES.md` |
| `reextractspikes` + shadow clustering (first spec) | `doc/design/reextractspikes-v1.md` |
| `ndm_reextractspikes{,_stderiv}` — extension handling, symlink shims | `doc/design/reextract-v2.md` |
| `process_decomposecollisions` — collision decomposition bug fixes | `doc/design/decomposecollisions.md` |
| `process_subtractspikes` — including `botm` mode (Proepper 2015) | `doc/design/subtractspikes-botm.md` |
| `neuroscope` — cluster raster / overlay stall fixes | `doc/design/neuroscope-raster.md` |
| `neuroscope` — full subsystem audit | `doc/design/neuroscope-audit.md` |
| `templates/template.yaml` — parameter block refresh | `doc/design/template-yaml.md` |
| Per-probe empirical KK priors (`kk_build_prior` / `kk_resolve_prior`) | `doc/design/kk-prior.md` |
| ndmanager Pipeline tab (editable node graph + YAML-driven `ndm_start` dispatcher) | `doc/design/ndm-start-root.md` |
| Hardware / OS tuning recipe | `doc/design/optimization.md` |
| Modeling comparison (Layer 1/2 vs BOTM) | `doc/design/modeling-l1-vs-botm.md` |
| Apple GPU (Metal) backend plan; Accelerate (Phase 0) | `doc/design/metal-backend.md` |
| KK prior operational workflow | `doc/workflows/empirical-priors.md` |
| Input binding system — the shared engine (Klusters) | `src/klusters/docs/INPUT_SYSTEM.md` |
| Input binding system — NeuroScope adoption | `src/neuroscope/docs/INPUT_SYSTEM.md` |
| Documentation system prototype (MkDocs + pandoc) | `doc/site/README.md` |

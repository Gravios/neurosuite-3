# Documentation reorganisation

This tarball reorganises the neurosuite-3 documentation tree along
three axes:

- **Pass 1.** Splits the 1000-line `doc/ndmanager-plugins/README.md`
  into ~22 per-command and ~11 per-format pages under
  `doc/ndmanager-plugins/commands/` and `doc/ndmanager-plugins/formats/`.
  The README itself becomes a 180-line index. Adds a separate
  `doc/ndmanager-plugins/pipeline.md` for the full pipeline diagrams.

- **Pass 2.** Adds a `doc/workflows/` folder with task-oriented
  walkthroughs that span multiple programs (first-time sort, cluster
  curation, re-extract with lower threshold, iterative refinement,
  empirical priors, drift correction, collision decomposition).

- **Pass 3.** Moves all `CHANGES-<topic>.md` and reference docs
  (`OPTIMIZE.md`, `modeling-recommendations.md`) from the repo root
  into `doc/design/`. Renames `CHANGES.md` to `CHANGELOG.md` to
  reflect that it's now a single pure changelog with all design
  detail living one level down.

## How to apply

```sh
cd /path/to/neurosuite-3
tar -xzf doc-reorg.tar.gz
./apply-doc-reorg.sh        # removes obsolete files now in new locations
```

The `apply-doc-reorg.sh` script uses `git rm` when run inside a git
checkout, falling back to plain `rm` otherwise. It's idempotent and
fail-safe: if any of the new files are missing (e.g. you forgot to
extract the tarball first), it skips removals and exits non-zero.

## Files affected

### New files

```
CHANGELOG.md                                     # was CHANGES.md, updated
apply-doc-reorg.sh                               # one-shot migration script
doc/README.md                                    # updated (workflows + design section)
doc/design/README.md                             # design-folder index
doc/design/decomposecollisions.md                # was CHANGES-decomposecollisions-fixes.md
doc/design/kk-prior.md                           # was CHANGES-kk-prior.md
doc/design/modeling-l1-vs-botm.md                # was modeling-recommendations.md
doc/design/neuroscope-raster.md                  # was CHANGES-neuroscope-raster-fixes.md
doc/design/optimization.md                       # was OPTIMIZE.md
doc/design/reextract-v2.md                       # was CHANGES-reextract-v2.md
doc/design/reextractspikes-v1.md                 # was CHANGES-reextractspikes.md
doc/design/subtractspikes-botm.md                # was CHANGES-subtractspikes-fixes.md
doc/design/template-yaml.md                      # was CHANGES-template-yaml.md
doc/workflows/README.md                          # workflows index
doc/workflows/cluster-curation.md
doc/workflows/collision-decomposition.md
doc/workflows/drift-correction.md
doc/workflows/empirical-priors.md                # was doc/ndmanager-plugins/kk-prior-workflow.md
doc/workflows/first-time-sort.md
doc/workflows/iterative-refinement.md
doc/workflows/re-extract-lower-threshold.md
doc/ndmanager-plugins/README.md                  # rewritten as thin index (180 lines)
doc/ndmanager-plugins/pipeline.md                # full pipeline diagrams
doc/ndmanager-plugins/commands/*.md              # 22 per-command pages
doc/ndmanager-plugins/formats/*.md               # 11 per-format pages
```

### Files removed by the migration script

```
CHANGES.md                                       # renamed to CHANGELOG.md
CHANGES-decomposecollisions-fixes.md             # → doc/design/decomposecollisions.md
CHANGES-kk-prior.md                              # → doc/design/kk-prior.md
CHANGES-neuroscope-raster-fixes.md               # → doc/design/neuroscope-raster.md
CHANGES-reextract-v2.md                          # → doc/design/reextract-v2.md
CHANGES-reextractspikes.md                       # → doc/design/reextractspikes-v1.md
CHANGES-subtractspikes-fixes.md                  # → doc/design/subtractspikes-botm.md
CHANGES-template-yaml.md                         # → doc/design/template-yaml.md
OPTIMIZE.md                                      # → doc/design/optimization.md
modeling-recommendations.md                      # → doc/design/modeling-l1-vs-botm.md
doc/ndmanager-plugins/kk-prior-workflow.md       # → doc/workflows/empirical-priors.md
```

### Files modified (not removed)

```
doc/README.md                                    # workflows section, design index
doc/klusters/README.md                           # workflow link path update
doc/klustakwik/README.md                         # workflow link path update
```

## What this does NOT change

- `src/<prog>/CHANGES.md` files inside the source tree are untouched.
  These track program-internal changes specific to one binary
  (KlustaKwik internals, KlustaKwikExp internals) and live in the
  source tree on purpose.
- All `doc/<prog>/install/*.md` install guides are untouched.
- Per-program READMEs (klusters, klustakwik, ndmanager, neuroscope,
  spikerealign, libklustersshared, gpu) are untouched except for the
  two stale-link fixes mentioned above.
- No source code is touched. This is documentation-only.

## Cross-link integrity

All internal markdown links across the new tree have been validated.
Pre-existing broken links in install docs (cross-references between
spikerealign and klusters install variants for platforms that don't
exist) are unchanged and remain pre-existing issues — the
reorganisation introduces zero new broken links.

## Reverting

If you want to undo the reorganisation:

```sh
git checkout HEAD~1 .
```

…assuming you committed the reorganisation as a single commit.
Otherwise, the original files are recoverable from git history; the
file-content change is purely path-level for the moved-and-renamed
files.

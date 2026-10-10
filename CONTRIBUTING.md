# Contributing to neurosuite-3

How a change lands in this repository: the coding standards it must meet, the
patch-series form it is delivered in, the verification gates it passes before
shipping, and where the change gets recorded. The conventions below are the
operational summary; [`STANDARDIZATION.md`](STANDARDIZATION.md) is the
authority, and where the two differ it wins.

---

## 1. Before you start

- Read the [Developer Guide](DEVELOPER_GUIDE.md) for the map of the codebase,
  and the reference under `src/<component>/docs/` for the subsystem you are
  touching (e.g. [`src/klusters/docs/DOCUMENT_MODEL.md`](src/klusters/docs/DOCUMENT_MODEL.md)
  before a curation change).
- Read [`STANDARDIZATION.md`](STANDARDIZATION.md) §2 (hard conventions) and §3
  (domain invariants) — the two sections a reviewer will hold the change to.
- **The code is the source of truth.** When a reference doc and the code
  disagree, trust the code and fix the doc in the same change.

---

## 2. Coding standards

The full set is [`STANDARDIZATION.md`](STANDARDIZATION.md) §2–§3; the rules that
most often trip up a change:

- **No `m_` member prefix** (§2.1). Members are bare; `this->` disambiguates
  where needed. The direction is *strip* `m_`, never add it (§6.1).
- **Qt-style casts and iteration** (§2.2–2.3). Respect the documented iteration
  order: `QMap` iterates ascending; `Data::Iterator` already negates the
  ordinate (do not re-flip Y downstream); new cluster ids come from
  `nextFreeClusterId()` (§2.3; see
  [`src/klusters/docs/DOCUMENT_MODEL.md`](src/klusters/docs/DOCUMENT_MODEL.md)).
- **Curation goes through the primitives** (§2.5). Build fresh temp tables and
  hand them to `prepareUndo`; **never pre-mutate** `clusterInfoMap` /
  `spikesByCluster` before the snapshot (§6.2) — it makes undo unable to return
  to the real prior state.
- **One multi-cluster signal per commit** (§2.6). A commit that creates >1
  cluster emits `newClustersAdded(QList)` **once**; do not loop the
  single-cluster signal (§6.3).
- **Subproject layout** (§2.7). Per-component sources live under
  `src/<component>/src/`; developer references under `src/<component>/docs/`;
  the user handbook under `src/<component>/doc/`. Do not leave loose or
  duplicate sources at a subproject root (§6.8).
- **Domain invariants** (§3). If the change touches curation, undo, spike
  files, PCA, or the template library, re-check the relevant invariant still
  holds — the spike-file invariant (§3.1), peak-position indexing (§3.2), the
  binary formats (§3.3; see
  [`src/libneurosuite-core/docs/FILE_FORMATS.md`](src/libneurosuite-core/docs/FILE_FORMATS.md)).

Background work (new threads/jobs) must follow the contributor rules in
[`src/klusters/docs/CONCURRENCY.md`](src/klusters/docs/CONCURRENCY.md) §8:
snapshot the epoch at enqueue, honor the cancellation token, and deliver results
only by a queued event under the post fence.

---

## 3. Delivering a change as a patch series

Structural, repo-wide, or multi-commit work is delivered as a numbered
`git format-patch` series (`STANDARDIZATION.md` §4.1):

```sh
# stage your commits in a working clone, then:
git format-patch <base>..HEAD -o patches/

# verify the whole stack applies clean onto a fresh checkout of <base>:
git checkout --detach <base>
git am patches/*.patch
# then build-test (see §5)
```

- **One concern per commit.** Each commit is a single, reviewable change with a
  self-contained message; the series applies in order with `git am`.
- **Verify the stack applies clean** onto a fresh detached checkout of the base
  before shipping — `git am` must consume every patch without a reject.
- The patch files are what gets reviewed and applied; keep them regenerated
  against the current base.

---

## 4. Commit messages

- **Imperative subject**, component-scoped where it helps:
  `klusters: fix undo after a multi-cluster recluster`.
- The body explains **why**, not just what, and references the deep note in
  `doc/design/` or the issue it addresses.
- **End with a verification NOTE** distinguishing what was actually verified
  from what still needs a hardware build (`STANDARDIZATION.md` §4.1). Much of
  the GUI/GPU wiring (Qt, CUDA/HIP/SYCL) cannot be compiled in every
  environment, so state plainly what was checked — e.g.:

  ```
  NOTE: verified by syntax-only compile (-Wall -Wextra -Wshadow) and git am
  onto <base>; full Qt6/CUDA link not built here — needs a hardware build.
  ```

---

## 5. Changelog and documentation

Follow the documentation layout in `STANDARDIZATION.md` §2.8:

- **User-visible changes go in [`CHANGELOG.md`](CHANGELOG.md)** — the single,
  canonical, date-ordered project changelog. There is no root `CHANGES.md`.
- **Deep per-topic technical notes** go in `doc/design/<topic>.md`, indexed from
  `doc/design/README.md` and the reference table at the end of `CHANGELOG.md`.
  Do not create flat `CHANGES-<topic>.md` files at the root.
- **Multi-program walkthroughs** go in `doc/workflows/`.
- **Program-internal histories** (`src/<prog>/CHANGES.md`,
  `CHANGES-inherited-from-canonical.md`) stay in the source tree; the top-level
  changelog references but does not absorb them.
- **Keep the references in step with the code.** If you change a subsystem,
  update its `src/<component>/docs/` reference and the matching entry in
  [`DEVELOPER_GUIDE.md`](DEVELOPER_GUIDE.md) in the same change. The guide has a
  mirror in the project knowledge base for reading inside Claude — keep the two
  in step when you change either (`DEVELOPER_GUIDE.md` "Maintaining this guide").
- Vendored third-party changelogs (e.g. libsamplerate) are never touched.

---

## 6. Verification gates before you ship

Adapted from `STANDARDIZATION.md` §9 — run these on the changed files before
shipping the series:

1. **Build clean.** Syntax-check the changed sources with zero warnings:
   ```sh
   g++ -std=c++20 -fsyntax-only -Wall -Wextra -Wshadow <changed files>
   ```
   (the full build still happens via CMake and CI; this is the fast local gate.)
2. **Convention.** `grep -n 'm_[a-zA-Z]' <changed files>` returns only false
   positives (tool/script names in comments or strings), never a real `m_`
   member.
3. **Domain invariants.** If the change touches curation, undo, spike files, or
   PCA, re-read the change against the relevant §3 invariant — read the change,
   not just the test suite.
4. **Tests.** If a `test/` suite covers the area, build with `-DNS_BUILD_TESTS=ON`
   and run `ctest` (see [`BUILD.md`](BUILD.md) §6).
5. **Docs.** The CHANGELOG entry, `doc/design/` note, and any affected
   `src/<component>/docs/` reference and `DEVELOPER_GUIDE.md` marker are present
   and accurate.

---

## 7. Continuous integration

Two workflows gate `main` and pull requests (see [`BUILD.md`](BUILD.md) §8):

- **`ci.yml`** builds the whole monorepo on `ubuntu-24.04` with the GPU backends
  off and `-Wall -Wextra -Wshadow` surfaced (non-fatal). Reproduce a failure
  locally with the exact configure line in `BUILD.md` §8. klusters and
  libklustersshared are shadow-clean; keep them that way.
- **`docs.yml`** builds the documentation site with `mkdocs build --strict` and
  deploys to GitHub Pages. It watches `src/*/docs/**` and `DEVELOPER_GUIDE.md`,
  so a broken relative link in a reference you touched will fail the deploy —
  keep cross-document links relative and correct.

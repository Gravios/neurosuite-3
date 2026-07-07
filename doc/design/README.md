# Design notes

Deep technical references for changes that span enough surface area
to warrant a dedicated document. Each file describes the design,
trade-offs, and implementation of one substantial topic. New entries
are indexed in [`../../CHANGELOG.md`](../../CHANGELOG.md) by date.

The convention: every `CHANGELOG.md` entry that runs longer than a
paragraph gets a `doc/design/<topic>.md` sibling. The CHANGELOG is
the time-ordered index; the design docs are the durable reference.

> **Note on file naming.** Several documents below predate the
> [variant naming convention](../ndmanager-plugins/formats/naming.md) and
> describe the retired `.spkD` / `.fetD` / `.pcaD` "D-suffix" scheme (and the
> bash shims that bridged it). They are kept as historical design records;
> for the current naming — `<base>.<type>.<method>.<group>` — see the naming
> reference.

## Index

| File | Topic |
|---|---|
| [`reextractspikes-v1.md`](reextractspikes-v1.md) | First spec: masked second-pass detection + shadow clustering |
| [`reextract-v2.md`](reextract-v2.md) | `ndm_reextractspikes{,_stderiv}` — extension handling, cross-pipeline shim |
| [`decomposecollisions.md`](decomposecollisions.md) | Collision decomposition algorithm and bug fixes |
| [`subtractspikes-botm.md`](subtractspikes-botm.md) | BOTM derivation (Proepper 2015) for `ndm_stripdat` |
| [`modeling-l1-vs-botm.md`](modeling-l1-vs-botm.md) | Side-by-side comparison: Layer 1/2 vs BOTM model fits |
| [`neuroscope-raster.md`](neuroscope-raster.md) | NeuroScope cluster raster / overlay stall fixes |
| [`neuroscope-audit.md`](neuroscope-audit.md) | NeuroScope full subsystem audit (2026-04-29) |
| [`template-yaml.md`](template-yaml.md) | Session YAML template parameter-block refresh |
| [`kk-prior.md`](kk-prior.md) | Per-probe empirical KK priors design (probe identity, hashes, friendly names) |
| [`ndm-start-root.md`](ndm-start-root.md) | ndmanager Pipeline tab — editable node graph with `ndm_start` as sticky root, plus YAML-driven dispatcher |
| [`optimization.md`](optimization.md) | Hardware and OS tuning recipe |
| [`substrate-labeling-refactor.md`](substrate-labeling-refactor.md) | Split the spike substrate from cluster labeling; unify flat + hierarchical as an N-layer model |

## Related

- **[CHANGELOG.md](../../CHANGELOG.md)** — date-ordered changelog,
  this index's primary cross-reference.
- **[../workflows/](../workflows/)** — task-oriented walkthroughs.
  Several design topics (priors, drift, BOTM) have a workflow
  counterpart that focuses on operational use rather than design.
- **[../../src/kiloklustakwik/CHANGES-inherited-from-canonical.md](../../src/kiloklustakwik/CHANGES-inherited-from-canonical.md)** —
  inherited canonical-engine history (v1.7 → neurosuite-3 diff). Lives in
  the source tree because it tracks code changes specific to that
  binary.
- **[../../src/kiloklustakwik/CHANGES.md](../../src/kiloklustakwik/CHANGES.md)** —
  KiloKlustaKwik-internal changes (DipSplit, time-shift merging, and other
  features that originated in the former experimental fork).

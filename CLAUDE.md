# CLAUDE.md

Entry point for Claude — and any AI agent — working on `neurosuite-3`. Its only
job is to route you to the real references *before* you design or ship anything,
so you don't cold-start into drift or re-litigate decisions already settled.

## Read first, in order

1. **[`STANDARDIZATION.md`](STANDARDIZATION.md)** — the session-start checklist and
   reference: project map, hard conventions, domain invariants, workflow, the
   5-phase audit playbook, known anti-patterns, and the verification gates that
   must pass before shipping. This is the primary document; treat everything below
   as a pointer into it.
2. **[`doc/klusters/CODING-STYLE-AUDIT.md`](doc/klusters/CODING-STYLE-AUDIT.md)** —
   the naming/clarity audit for `src/klusters/` + `src/libklustersshared/`. Read
   before renaming identifiers in those trees.
3. **[`ROADMAP.md`](ROADMAP.md)** + **[`PLUGINS-ROADMAP.md`](PLUGINS-ROADMAP.md)** —
   active / next / planned / considered / dropped work. Check before starting
   something new so you build the right thing and don't repeat a dropped item.

Architecture and change history live in
[`ARCHITECTURE.md`](ARCHITECTURE.md) and [`CHANGELOG.md`](CHANGELOG.md).

## The conventions agents break most (full set in STANDARDIZATION.md §2, §6)

- **Members are bare — no `m_` prefix, no Hungarian.** klusters uses zero `m_` on
  members across ~30K LOC. When a member name collides with a constructor
  parameter, **rename the parameter** (`lbl`, `stp`, `tot`) to avoid `-Wshadow` —
  never add `m_`. Any PR that (re)introduces `m_` is a regression. Booleans are
  predicate-style (`isChanged`, `valid`, `enabled`).
- **No C-style casts in new code** — `static_cast` / `qobject_cast` /
  `reinterpret_cast` (documented) / `const_cast` (documented).
- **One concern per patch.** One logical change per commit; don't fold unrelated
  fixes together.
- **Validate before claiming.** Verify on the reference session
  (`jg05-20120316` group 7; secondary `eb05-20251118` group 25) before asserting a
  result. Honest negatives are expected and valued.
- **`AUTOUIC` is on** — never commit generated `ui_*.h`.

## Build (detail in [`README.md`](README.md))

```sh
cmake -B build                      # GPU backends (CUDA/HIP/SYCL) auto-detected; a CPU/OpenMP fallback always builds
cmake --build build --parallel
```

Pass `-DNS_INSTALL_DEPS=ON` on the first configure to install system packages, or
install the Qt6 dependencies listed in README.md → Dependencies. No GPU toolkit is
needed for a working CPU build.

## Before shipping (STANDARDIZATION.md §9)

The change must build clean and the relevant reference session must round-trip. If
a Qt6 build is not available in your environment, **say so explicitly** and fall
back to the checks you can run (applies cleanly, brace/scope balance) — never imply
a build passed when it did not.

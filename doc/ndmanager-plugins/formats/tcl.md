# `.tcl.N` — template-class registry (versioned text)

Companion to [`.eap`](eap.md): one entry per `.eap` column, giving that
template class's status and where it came from.

**Naming.** Method-less and stage-independent: `<base>.tcl.<group>`, one
registry per session and group, so a class ID refers to the same `.eap` column
in every curation stage.

**Writers.** [`process_initeap`](../commands/process_initeap.md) (all slots
`free`); [`ndm_decomposecollisions`](../commands/ndm_decomposecollisions.md),
Klusters and fiber-kit when they create, delete or merge classes. All use
`neurofileio::readTcl` / `writeTcl` in `libneurosuite-core`. A writer that finds
no `.tcl` next to an `.eap` starts from all-`free` slots of the `.eap`'s width.

## Format

```
tcl 1
nClasses 128
# col	status	label	provenance_clu	provenance_stage	created
0	active	CA1 pyr a	23	gt	2026-10-02
1	active	-	9	-	2026-10-02
2	merged:0	-	14	gt	2026-10-02
3	tomb	-	41	gt	2026-10-02
4	free	-	-	-	-
```

- Line 1: `tcl <version>`; only version 1 is read. Blank lines and lines
  starting with `#` are ignored.
- `nClasses <T>` (space-separated): number of columns, equal to the `.eap`
  width. If absent, the number of rows is used.
- One row per column, **TAB-separated** (a label may contain spaces); an empty
  field is written `-`. Rows with fewer than two fields are skipped; missing
  trailing fields are empty.

## Fields

| Field | Meaning |
|---|---|
| `col` | `.eap` column index = the class ID |
| `status` | `free` (available) · `active` (live class) · `tomb` (deleted; the column is cleared and the ID is never reused) · `merged:<col>` (folded into column `<col>`, which took over its spikes where it had none of its own). An unknown token reads as `free`. |
| `label` | Free text for the curator |
| `provenance_clu` | Cluster ID the class was created from |
| `provenance_stage` | Curation stage it was created in (`-` for the canonical, untagged stage) |
| `created` | Date stamp, `YYYY-MM-DD` as written by the tools |

Provenance is recorded once at creation and is never used as identity: the
cluster may have changed since. It is, however, how collision decomposition
reuses classes: a collision constituent of unit *u* goes into the lowest
`active` column whose `provenance_clu` is *u*, and a new class with
`provenance_clu = u` is created only when there is none.

New classes take the lowest `free` column. When none is left, the `.eap` and
the registry are widened together and the new slots are `free`.

---

*Part of the [ndmanager-plugins](../README.md) file-format reference.*

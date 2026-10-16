# `.cli.<method>.N` — cluster information of one clustering

What the curator recorded in Klusters about each unit of one clustering —
structure, type, isolation distance, quality, notes — kept beside the
[`.clu`](clu.md) it describes. Klusters writes it with the clustering, so it
always matches that file's cluster ids, and every stage of a group keeps its
own.

The session YAML's `units` table is **not** written by Klusters. It is the
record of finished curation, filled on request in ndManager with
**Actions ▸ Record Curated Units…**, which reads these files.

## Layout

UTF-8 text. Lines starting with `#` are comments; every other line is one unit
with eleven tab-separated fields, in the order of the YAML `units` table:

```
# klusters units v2
# group	cluster	structure	type	isolationDistance	quality	notes	region	division	atlas	regionId
3	2	CA1	pyramidal	14.2	good	stable over the session	hippocampal formation	archicortex	WHS SD rat v4	<atlas or UBERON id>
3	7	DG	interneuron
```

| Field | Example | Meaning |
|---|---|---|
| `structure` | CA1 | the finest anatomical label |
| `type` | pyramidal | the putative cell type |
| `isolationDistance` | 14.2 | |
| `quality` | good | |
| `notes` | | free text |
| `region` | hippocampal formation | the named region |
| `division` | archicortex, cerebral cortex, thalamus, cerebellum | the broad division |
| `atlas` | Waxholm Space rat atlas (WHS SD) v4 | where the region terms come from |
| `regionId` | an atlas structure ID or an `UBERON:` ID | a stable identifier of the finest term |

- `group` and `cluster` are integers; the other fields are free text and may
  be empty. Missing trailing fields read as empty, so a v1 file (seven
  fields, before `region`…`regionId`) reads unchanged.
- Name regions with the terms of one reference atlas and record which in
  `atlas`: for rat the Waxholm Space atlas, for mouse the Allen Common
  Coordinate Framework; `regionId` then lets the hierarchy (structure ⊂
  region ⊂ division) be looked up instead of typed. UBERON IDs work across
  species.
- A tab, newline, carriage return or backslash inside a field is written as
  `\t`, `\n`, `\r` or `\\`.
- Units with every descriptive field empty are not written; a file with no
  units (only the header) means the information was cleared.
- Clusters 0 (artefact) and 1 (noise) are never listed.

## Naming

The name of the `.clu` with `cli` (cluster information) in place of `clu`, with the same method, group and stage suffix:
`rat01.clu.stderiv_C5.3.microfiber` ↔ `rat01.cli.stderiv_C5.3.microfiber`.
It is not in the custody type tables ([naming](naming.md#files-outside-the-type-tables)).

## Writers and readers

- **Written** by Klusters on Save (in the same all-or-nothing commit as the
  `.clu`; left alone when unchanged) and on Save As under the new name. A
  clustering with no cluster information gets no file.
- **Read** by Klusters when the clustering is opened (the file wins over the
  session YAML; without it the YAML's values for the group are shown), and by
  ndManager's Record Curated Units.

## See also

- [`.clu`](clu.md).
- [Klusters cluster information](../../klusters/README.md#cluster-information-panel).

---

*Part of the [ndmanager-plugins](../README.md) reference.*

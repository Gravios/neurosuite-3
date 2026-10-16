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
with seven tab-separated fields, in the order of the YAML `units` table:

```
# klusters units v1
# group	cluster	structure	type	isolationDistance	quality	notes
3	2	CA1	pyr	14.2	good	stable over the session
3	7	DG	int			
```

- `group` and `cluster` are integers; the other fields are free text and may
  be empty (missing trailing fields read as empty).
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

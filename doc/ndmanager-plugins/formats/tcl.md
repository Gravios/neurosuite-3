# `.tcl.N` — template-class registry (version-tagged text)

Companion to [`.eap`](eap.md): one entry per `.eap` column, carrying that template
class's lifecycle and the provenance it was generated from. **Method-less and
stage-INDEPENDENT** — `<base>.tcl.<group>`, one registry per session + group — so a
class id means the same `.eap` column across every curation stage. Written with all
slots `free` by [`process_initeap`](../commands/process_initeap.md) and updated by
the decollide engines and Klusters.

```
tcl 1
nClasses 128
# col <TAB> status <TAB> label <TAB> provenance_clu <TAB> provenance_stage <TAB> created
0	active	CA1 pyr a	23	gt	2026-10-02
1	active	int-1	9	gt	2026-10-02
2	free	-	-	-	-
3	tomb	-	41	gt	2026-10-02
```

The `col` rows are **TAB-separated** (so a label may contain spaces); an empty field
is written `-`.

- **col** — the `.eap` column index = the stable template-class id.
- **status** — `free` (available) · `active` (a live class) · `tomb` (deleted; the
  id is **never reused**) · `merged:<col>` (folded into another column; its spikes
  moved there).
- **provenance_clu / provenance_stage** — the clu id + stage the class was first
  generated from, recorded at creation; pure provenance, never identity (the clu may
  have changed since). The decollide engines set `provenance_clu` to the colliding
  unit id — the key the "same unit → same column" reuse rule matches on.
- **created** — a free-form date stamp.

---

*Part of the [ndmanager-plugins](../README.md) file-format reference.*

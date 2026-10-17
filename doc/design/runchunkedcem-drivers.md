# The two RunChunkedCEM drivers

kiloklustakwik's chunked pipeline has two entry points in
`src/kiloklustakwik/src/KK_chunked.cpp`. They differ only in how the chunks
are cut:

| Driver | Signature | Chunks | Reached by |
|---|---|---|---|
| **A**, external bounds | `RunChunkedCEM(const std::vector<float>& chunkBoundsSec, …)` | read from `-ChunkFile SESSION.chunks.N` (drift-adaptive, from `ndm_applydrift`) | `ndm_applydrift` with `runKlustaKwik`, or a direct `-ChunkFile` call |
| **B**, uniform | `RunChunkedCEM(float chunkMinutes, …, chunkOverlapMinutes, chunkPreseedFraction)` | every `-ChunkMinutes` (default 7), optionally overlapping | `ndm_klustakwik` and every other chunked run |

Driver A's comment says it "delegates to the core implementation"
and that "everything from Phase 1 onward is identical to the uniform
variant". That is not true. Each driver carries its own copy of Phases 1–8.
The two bodies are 1,296 and 1,518 lines and share 1,072 lines. The
differences below were found by diffing them (2026-10-17). Phase 0, which
builds the chunks, differs by design and is not listed.

## Behaviour that differs

| # | Phase | A (external bounds) | B (uniform) | Effect on A runs |
|---|---|---|---|---|
| 1 | 1, per-chunk CEM | starts every chunk at `nStartingClusters`; deletion floor `minClustersAlive = nStartingClusters`; always random init | starts at the preseed's K when `-ChunkPreseedFraction > 0`, else `nStartingClusters`; deletion floor 2 (or preseed K − 4); uses the preseed centres | A never preseeds. A chunk's CEM can never delete below its starting K, where B lets it shrink to 2. |
| 2 | 2.3–2.10 | always runs the stages one after another | `-FusePerChunkStages 1` runs each chunk's Stage 2.3–2.10 block on one thread | `-FusePerChunkStages` is ignored |
| 3 | 2b.5 / 2.9 order | K-template split (`-KnnSplitPerChunkEnable`) and `-FullCemSplitEnable` run **before** the Stage 2.9 DipSplit pass | Stage 2.9 DipSplit first, then the K-template split, then FullCem | the same options run in a different order |
| 4 | 2.12 | runs `FiberStagePerChunk` when `-FiberStageEnable 1` | **no Stage 2.12** | in B (the default chunking) `-FiberStageEnable` is silently ignored |
| 5 | 3.1 votes | no overlap votes (`noOverlapVotes`) | overlap votes, built **after** Phase 4 has settled the cluster ids | by design: drift-adaptive chunks do not overlap |
| 6 | 2.11 / 4 split | no oscillation guard | `-AlternatingSplitCooldownIters` cools down clusters a split/merge pair round-tripped (patch 0053); sets `m_phase4Iter` | `-AlternatingSplitCooldownIters` is ignored (`m_phase4Iter`, which only the cooldown check in `KK_split.cpp` reads, is not advanced either) |
| 7 | 3.2 | logs "Global EM: skipped" when `GlobalMergeIter = 0` | silent | log only |
| 8 | messages | "RunChunkedCEM(ext)" and shorter warnings | longer MergeThresh and table-size warnings | log only |

Rows 1, 2, 3, 4 and 6 are real behaviour differences. Each looks like a
feature or fix that was added to one driver and never ported to the other,
not like a deliberate choice: none is mentioned in the code or the docs.

## Suggested resolution (not done)

Make driver A a thin front end. It would build `chunkPoints` from the
boundary file and call a shared `RunChunkedCEMFromChunks(chunkPoints,
overlapForPair = {}, …)` that holds Phases 1–8 once. Driver B would call the
same function after cutting uniform chunks. This needs decisions first:

- Row 1: should drift-adaptive chunks get the preseed, and the deletion
  floor of 2?
- Row 3: which order of the K-template split and DipSplit is intended?
- Row 4: should Stage 2.12 run in uniform chunking too?

After that, the merged driver must be checked against the reference
sessions (`jg05-20120316` group 7, `eb05-20251118` group 25) for both
`-ChunkFile` and `-ChunkMinutes` runs. Until then, the uniform path's
output can be held byte-identical, while the `-ChunkFile` path's output
will change by design.

For the file layout, `KK_chunked.cpp` was split on 2026-10-17:
`MergeChunkModels` is in `KK_chunk_merge.cpp` and `RunPhase2bMode3Chunk` in
`KK_phase2b_mode3.cpp`.

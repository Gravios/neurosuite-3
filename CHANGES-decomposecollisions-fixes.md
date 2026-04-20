## ndmanager-plugins — process_decomposecollisions fixes (2026-04-19)

Four bugs fixed in the collision-decomposition pipeline.  The `.col.N`
binary format and the on-disk layout are unchanged except for the
documented semantic fix to `shift_frac` in the second component (see
below).  Sessions produced before this commit remain readable; the
shift-semantics note in `process_decomposecollisions.h` explains how to
interpret the older files.

### Fix 1 — `minSnrRms` is no longer inert

Before: `process_group_cpu` and `process_group_gpu` were always invoked
with `min_amp_abs = 0.f`, so the SNR gate fell through to a hardcoded
`rms * 4.f` regardless of what was configured in YAML.  Users setting
`minSnrRms: 6.0` thought they were tightening the gate, but the
parameter was silently ignored.

After: `args.min_snr_rms` is now threaded through to both CPU and GPU
paths, and its semantic is clarified in-place:

  - It is a **multiplier of RMS noise**, not an absolute amplitude.
  - `min_amp = rms × minSnrRms`, with a 4× fallback when the value is
    ≤ 0 (for backward compatibility with older caller sites).

The stderr banner now prints the multiplier and the resulting absolute
`min_amp`, so the effective SNR gate is visible at runtime.

### Fix 2 — `shift_frac` convention unified across comp1 and comp2

Before: comp1 stored `sf1 = pure sub-sample offset` (integer shift is
always 0 for comp1), but comp2 stored `sf2 = best_integer_tau + frac` —
so `sh2 + sf2` double-counted the integer part of the shift.  Downstream
consumers that used only integer shifts (collision_viewer.py) were
unaffected, but any consumer doing `total = sh + sf` for comp2 would
compute the wrong alignment.

After: both components use the uniform convention
`total_shift_samples = shift_samp + shift_frac`, with `shift_frac` a
pure fractional offset in approximately `[-0.5, 0.5]` from parabolic
interpolation around the three scores nearest `shift_samp`.  The header
documents this and flags the older file semantics.

### Fix 3 — `read_script_parameter` calls use the correct signature

Before: every parameter read in `ndm_decomposecollisions` passed the
script name as a positional argument:

  `read_script_parameter "$session" ndm_decomposecollisions overwrite`

Per the function's signature `[-s script] [-a alt] file name1 [name2…]`
that treats "ndm_decomposecollisions" as the first parameter NAME to
resolve, not as the script identifier.  It happened to work today only
because the function iterates over `$@` after `$file`, so the second
name ("overwrite") succeeds after the first silently fails.  This was
load-bearing on an accident and would break any time a YAML session
defined a top-level parameter named `ndm_decomposecollisions`.

After: the redundant positional argument is removed.
`read_script_parameter` defaults to `$program` (the current script's
basename), which is already "ndm_decomposecollisions" at this call
site.  Behaviour on valid sessions is unchanged; behaviour on
adversarial YAML is now correct.

### Fix 4 — Pre-flight and overwrite checks accept `.spkD.N`

Before: both checks only looked for `.spk.N`.  Sessions produced by the
stderiv pipeline (which writes `.spkD.N`) hit "No .spk files found" and
refused to run — even though the C++ binary would have handled them
correctly (main.cpp prefers `.spkD` when both exist).

After: the two loops accept either extension.  The error message is
updated to mention both.

### Not fixed in this patch (tracked for a follow-up)

- The custom line-based YAML reader in `read_group_params` should be
  replaced with `libklustersshared`'s `ParameterYamlReader` — the
  repo-standard yaml-cpp consumer used by every other plugin.  Deferred
  because it would add a libklustersshared link dependency to this
  plugin and warrants a separate review.
- The pass-1 parabolic sub-sample refinement for u1 is computed but
  never applied back to the amplitude fit or residual R1 that pass 2
  operates on.  Worth investigating whether actually shifting u1 by
  the sub-sample offset before computing R1 improves pass-2 residuals
  on real data.
- The amplitude clamp `[pct01 - half_range, pct99 + half_range]` is
  tight by design; consider making the half_range multiplier
  configurable for sessions with many partial-amplitude collisions.
- `collision_viewer.py`'s "suggested threshold" (10th percentile of
  BSC among accepted) is a feedback loop — always ≤ current threshold,
  so will drift permissive over repeated tuning rounds.  A
  between-class separator (Otsu, or midpoint between accept and reject
  means) would be more useful.

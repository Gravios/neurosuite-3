#!/usr/bin/env bash
# =============================================================================
#  run_kkK.fiber.sh  -  KiloKlustaKwik STANDALONE FIBER-CLUSTERING branch
# =============================================================================
#  Runs -FiberStandaloneEnable 1, which BYPASSES the Phase 1-9 CEM/split/merge
#  pipeline entirely.  Per chunk it does:
#    off-spike .fil whitener
#      -> in-band directional mean-shift ridge seeding (random seeds walk to
#         the fiber centers across ALL energy bands, so weak units get a seed)
#      -> trajectory-coherence merge
#      -> whiteness-residual assignment + calibrated posterior
#    -> writes Class[] / .clu.
#
#  Validated on g7 (63,639 spikes, no clu): 147 fibers, median refractory
#  violation 1.16%, 65 clean (<1%).  Per-chunk fibers get chunk-disjoint ids;
#  cross-chunk drift tracking is a separate stage (not yet wired).
#
#  Because the normal pipeline is bypassed, none of the Phase 1-9 flags apply
#  here -- only geometry (auto from YAML), chunking, the fiber params, output.
#  Run SERIAL (no -ParallelK); the fork is on the serial path.
#
#  Validate:  bash -n run_kkK.fiber.sh
# =============================================================================
set -euo pipefail

KKEXP=${KKEXP:-KiloKlustaKwik}
DATASET=${1:?usage: $0 <session-base> <group>}
GROUP=${2:?usage: $0 <session-base> <group>}

ARGS=(

  # ===== ACQUISITION GEOMETRY (auto-filled from <DATASET>.yaml at startup) =====
  # -NbChannels 0  -NbSamplesPerSpike 0  -PeakSampleIndex 0
  # -NbTotalChannels 0  -SamplingRate 0.0  -ElecNo 1

  # ===== CHUNKING (per-chunk stationarity; required) =====
  -ChunkMinutes                         5         # chunk size (min); 0 = single chunk

  # ===== STANDALONE FIBER MODE =====
  -FiberStandaloneEnable                1         # master switch: run the fiber branch

  # ----- in-band mean-shift ridge seeding -----
  -FiberMSKappa                         20        # angular kernel concentration; ↑ = resolve
                                                  #   tightly-packed ridges (crack dense cores)
  -FiberMSDrFrac                        0.15      # in-band radius window = frac*(p99-p1 radius);
                                                  #   ↓ = thinner energy slices, finer ridges
  -FiberMSSeeds                         800       # # random seeds; ↑ = more thorough mode coverage

  # ----- consolidation -----
  -FiberMergeAngleDeg                   20        # trajectory-coherence merge: fuse fragments whose
                                                  #   trajectories stay within this angle over overlap
  -FiberMinGroupSize                    40        # min spikes per provisional group / fiber

  # ===== OUTPUT / LOGGING =====
  -Screen                               1
  -Verbose                              1         # prints "[FiberStandalone] chunk c: N -> M centers -> F fibers"
  -RandomSeed                           42        # seeding is deterministic (RNG-free subsampling)
)

"${KKEXP}" "${DATASET}" "${GROUP}" "${ARGS[@]}"

# -----------------------------------------------------------------------------
# Tuning notes (the giant-basin case):
#   If one fiber swallows a dense high-energy core (many spikes, refractory>3%),
#   tighten the seeder so it resolves close ridges:
#       -FiberMSKappa 35  -FiberMSDrFrac 0.10
#   then re-check per-fiber refractory violations.  These are runtime flags --
#   no recompile needed.
# -----------------------------------------------------------------------------

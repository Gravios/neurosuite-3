#!/usr/bin/env bash
# =============================================================================
#  run_kkK.fiber.sh  -  KiloKlustaKwik STANDALONE FIBER-CLUSTERING branch
# =============================================================================
#  -FiberStandaloneEnable 1 BYPASSES the Phase 1-9 pipeline.  Per chunk:
#    off-spike .fil whitener -> in-band mean-shift ridge seeding -> trajectory-
#    coherence merge -> whiteness-residual assignment + calibrated posterior.
#  Chunks run in PARALLEL (each independent until linking).  With
#  -FiberXChunkEnable 1, per-chunk fibers are linked across chunks for drift:
#  overlap spikes anchor an energy-dependent rotation warp R(r) that the fiber
#  population geometry generalises to every fiber, giving stable global ids.
#
#  Run SERIAL at the KKK level (no -ParallelK); the fork is on the serial path.
#  Validate:  bash -n run_kkK.fiber.sh
# =============================================================================
set -euo pipefail
KKEXP=${KKEXP:-KiloKlustaKwik}
DATASET=${1:?usage: $0 <session-base> <group>}
GROUP=${2:?usage: $0 <session-base> <group>}

ARGS=(
  # ===== GEOMETRY (auto-filled from <DATASET>.yaml) =====
  # -NbChannels 0 -NbSamplesPerSpike 0 -NbTotalChannels 0 -SamplingRate 0.0 -ElecNo 1

  # ===== CHUNKING =====
  -ChunkMinutes                         5      # chunk size (min)
  -ChunkOverlapMinutes                  2      # overlap (min) -> cross-chunk anchors (>0 required for X-chunk)

  # ===== STANDALONE FIBER MODE =====
  -FiberStandaloneEnable                1
  # ----- in-band mean-shift ridge seeding -----
  -FiberMSKappa                         20     # angular kernel; UP resolves packed ridges
  -FiberMSDrFrac                        0.15   # in-band radius window frac; DOWN finer ridges
  -FiberMSSeeds                         800    # # random seeds
  # ----- consolidation -----
  -FiberMergeAngleDeg                   20     # trajectory-coherence merge threshold
  -FiberMinGroupSize                    40     # min spikes per provisional group / fiber

  # ===== CROSS-CHUNK FIBER TRACKING (drift) =====
  -FiberXChunkEnable                    1      # link fibers across chunks (0 = chunk-disjoint ids)
  -FiberXChunkSubspaceDim               10     # L: shared population subspace dim
  -FiberXChunkNKnots                    10     # energy knots for the warp field R(r)
  -FiberXChunkMinAnchors                8      # min overlap anchor pairs to fit R(r); below -> no-warp match
  -FiberXChunkGateRatio                 0.6    # accept link iff best < ratio*second (DOWN stricter)
  -FiberXChunkSmooth                    1.0    # neighbour-knot pooling for R(r) (UP smoother; 0 = per-knot)

  # ===== PARALLELISM / GPU =====
  -FiberThreads                         0      # OpenMP threads for chunk loop (0 = all cores)
  -FiberGPUEnable                       0      # 1 = try GPU kernels; not built yet -> logs + CPU fallback

  # ===== OUTPUT =====
  -Screen                               1
  -Verbose                              1
  -RandomSeed                           42     # seeding is deterministic (RNG-free subsampling)
)
"${KKEXP}" "${DATASET}" "${GROUP}" "${ARGS[@]}"

# -----------------------------------------------------------------------------
# Tuning:
#   Giant high-energy basin (one fiber, refractory>3%):  -FiberMSKappa 35 -FiberMSDrFrac 0.10
#   Over-linking (distinct units merged across chunks):  -FiberXChunkGateRatio 0.45
#   Under-linking (one unit split across chunks):        -ChunkOverlapMinutes 3  -FiberXChunkSmooth 2.0
#   Strong drift (track energy-dependent rotation):      -FiberXChunkNKnots 16
#
# Status: cross-chunk warp validated in a synthetic-drift prototype only (not yet
# on real adjacent chunks); the s(r) amplitude-rescale term is not in yet (knots
# matched by index); GPU kernels are scaffold-only.  All tunable at runtime.
# -----------------------------------------------------------------------------

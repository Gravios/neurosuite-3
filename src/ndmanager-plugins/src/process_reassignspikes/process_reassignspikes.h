/***************************************************************************
                   process_reassignspikes.h
                   ------------------------
    copyright  : (C) 2026  Gravios / NeuroSuite-3 contributors

 Description
 ===========
 History-dependent spike reassignment.  Burst-decremented spikes (each spike
 leaves a fraction of Na+ channels inactivated and engages slow K+/AHP
 currents, so the waveform of spike i depends on the unit's recent firing
 history) drift to lower amplitude and get mis-assigned to smaller neighbours
 during clustering.  This tool re-scores ambiguous spikes against a
 *state-conditioned* waveform model and reassigns those that fit a different
 unit better -- without recomputing the model: the nonlinear fitting and the
 per-unit trust gating are done offline by the companion Python tool
 (adaptmodel.py), which emits the .adapt model artifact this binary consumes.

 Model (per trusted unit, frozen in the .adapt file)
 ---------------------------------------------------
   Three multichannel basis waveforms C0,C1,C2 and four recovery constants.
   Per spike, two causal availabilities a (fast, Na+) and g (slow, AHP) are
   computed from that unit's inter-spike intervals via a depletion+recovery
   recurrence, and the predicted waveform is the linear blend
       w_hat = C0 + (1 - a) * C1 + (1 - g) * C2.
   Reassignment scores the noise-whitened residual of a spike against w_hat
   for each candidate unit, times a prior from the curated label, subject to
   a hard refractory veto.

 Safety
 ------
   * Units NOT present in the .adapt file are NO-TOUCH (the offline gates
     rejected them) and their spikes are never moved.
   * The curated .clu.N is never overwritten.  Output goes to a new stem
     (default suffix "-reassigned"); a text move-log records every change.
   * DRY-RUN by default: scores and logs proposed moves but writes the
     labels unchanged unless --commit is given.

 Pipeline position
 -----------------
   After an initial curated clustering (klusters) AND after adaptmodel.py has
   produced a validated .adapt artifact for the session.

 File layout conventions (neurosuite-3)
 --------------------------------------
   .res.N   : int64 spike-peak timestamps, chronological, no header
   .clu.N   : int32 nClusters header, then nSpikes x int32 cluster ids
   .spk.N   : int16 waveform samples, sample-major:  wav[s * nChanGroup + c]
   .adapt   : little-endian binary model artifact, see ModelUnit below:
                char[4]  "ADPT"
                int32    version (=1)
                int32    nUnits
                per unit:
                  int32   group           (1-based, file suffix)
                  int32   cluster          (curated id)
                  int32   nSamples
                  int32   nChan
                  float64 tau_f_ms, u_f, tau_s_ms, u_s
                  float32 C[3 * nSamples * nChan]   (C0,C1,C2 row-major)
 ***************************************************************************/
#ifndef PROCESS_REASSIGNSPIKES_H
#define PROCESS_REASSIGNSPIKES_H

#include <cstdint>
#include <string>
#include <vector>

#define ADAPT_MAGIC          "ADPT"
#define ADAPT_VERSION        1
#define RES_EXT              "res"
#define CLU_EXT              "clu"
#define SPK_EXT              "spk"
#define DEFAULT_OUT_SUFFIX   "-reassigned"

// One trusted unit's frozen model (loaded from the .adapt artifact).
struct ModelUnit {
    int    group   = 0;
    int    cluster = 0;
    int    nSamples = 0;
    int    nChan    = 0;
    double tauFms = 0.0, uF = 0.0, tauSms = 0.0, uS = 0.0;
    std::vector<float> C;          // 3 * nSamples * nChan, blocks C0,C1,C2
};

struct ReassignArgs {
    // I/O ------------------------------------------------------------------
    std::string basename;                 // session stem (.res.N/.clu.N/.spk.N)
    std::string modelPath;                // .adapt artifact (required)
    std::string parPath;                  // session YAML (geometry); optional
    std::string outSuffix = DEFAULT_OUT_SUFFIX;
    std::vector<int> groups;              // 1-based; empty => all in model

    // Behaviour ------------------------------------------------------------
    double samplingRate   = 0.0;          // Hz (from YAML or -s)
    int    refractorySamp = 0;            // hard refractory veto (samples); 0=off
    double scoreMargin    = 0.0;          // min score gain to move a spike
    double priorWeight    = 1.0;          // weight on the curated-label prior
    double ampPercentile  = 50.0;         // only contest spikes below this
                                          // within-unit amplitude percentile
    bool   commit = false;                // false => dry-run (write log only)
    bool   verbose = false;
};

#endif // PROCESS_REASSIGNSPIKES_H

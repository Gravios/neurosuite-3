/***************************************************************************
 * realign_center.h
 *
 * Shared post-alignment "centering" math for spike realignment.
 *
 * After per-spike cross-correlation alignment (realign_xcorr), a cluster can
 * still sit off-centre as a whole, because the abs-max template pre-alignment
 * anchors on whichever phase is larger and that choice can differ between
 * clusters.  This module computes the single group shift that recentres a
 * cluster onto a chosen sample (peakPos), from a per-sample energy profile.
 *
 * The sample axis of the extracted .spk window is periodic, so the centroid is
 * a CIRCULAR weighted mean, not a linear one.  The circular mean is also
 * exactly invariant to a uniform pedestal in the weights — the Nth roots of
 * unity sum to zero — so an RMS noise floor cancels with no baseline
 * subtraction.  Weighting by energy (RMS^2) concentrates the mass on the
 * spike.  The mean resultant length R guards the degenerate case where the
 * energy splits into two lobes ~N/2 apart and the centroid is meaningless.
 *
 * Callers build the energy profile in their own waveform layout (channel-major
 * or sample-major, rolled by per-spike shifts or by a committed cumulative
 * shift) and pass it here; this module owns only the circular statistics and
 * the shift convention.  The two app copies that used to duplicate this math —
 * Klusters' KlustersDoc::realignSpikes and KiloKlustaKwik's
 * KlustersStyleRealignAllClusters — now share it and stay in lockstep.
 ***************************************************************************/
#pragma once

#include <vector>

namespace realign_center {

struct RecenterResult {
    int    shift    = 0;     ///< minimal signed circular shift, sample units
    double centroid = 0.0;   ///< circular energy centroid, sample units [0,nSamp)
    double R        = 0.0;   ///< mean resultant length, range [0,1]
    bool   applied  = false; ///< false when R < rMin (shift forced to 0)
};

/* Circular weighted-mean recentre.
 *   energy   per-sample weight profile (>= 0), length nSamp
 *   nSamp    samples per waveform (the periodic axis length)
 *   peakPos  target sample the centroid should land on
 *   rMin     minimum mean-resultant-length to trust the centroid; below this
 *            the result is { applied=false, shift=0 } and the caller should
 *            leave the cluster on its per-spike alignment.
 *
 * The returned shift uses the realign roll convention new[t]=old[(t+shift)%N]:
 * adding `shift` to a spike's cumulative shift moves the energy centroid onto
 * peakPos.  R and centroid are always populated (for logging) even when the
 * shift is suppressed. */
RecenterResult circularRecenterShift(const double* energy, int nSamp,
                                     int peakPos, double rMin);

/* Per-spike reference-free centroid de-jitter -- the fiber-kit fiber_realign method='centroid'.
 *   energy    per-spike channel-summed per-sample energy, nSpikes*nSamp row-major (>= 0)
 *   nSpikes   number of spikes
 *   nSamp     samples per waveform (the periodic axis length)
 *   shiftOut  filled (size nSpikes) with each spike's signed circular shift, same roll convention
 *             as circularRecenterShift: new[t]=old[(t+shift)%N].
 *
 * Where circularRecenterShift moves a WHOLE cluster onto a FIXED peakPos, this moves EACH spike so its
 * own circular energy centroid lands on the POPULATION's circular-mean centroid -- a template-free
 * per-spike alignment that works when a cluster's mean is too noisy to anchor an xcorr.  Both the
 * per-spike centroid and the population target are first-DFT-bin phasors (the same circular statistics
 * this module already owns), so it stays in lockstep with the group recenter and with fiber-kit's
 * fiber_lib._centroid_pos / centroid_shift. */
void perSpikeCentroidShifts(const double* energy, int nSpikes, int nSamp,
                            std::vector<int>& shiftOut);

}  // namespace realign_center

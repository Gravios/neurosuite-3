/***************************************************************************
    process_spikegrouper.cpp
    ------------------------
    Discovers optimal spikeDetection channel sub-groups from the high-pass
    filtered .fil file and rewrites spikeDetection.channelGroups in the
    YAML parameter file.

    Algorithm (per anatomicalDescription group)
    -------------------------------------------
    Clustering is restricted to channels that share an anatomical group
    (i.e. the same shank or probe region).  Cross-shank coincidence is
    biologically meaningless and would waste O(N²) computation.

    1.  Load a configurable window (--window-sec) of the .fil file.
    2.  Read anatomicalDescription.channelGroups — these define the hard
        boundaries within which coincidence is computed.
    3.  For each anatomical group, take the intersection with the channels
        listed in spikeDetection.channelGroups; inherit nSamples /
        peakSampleIndex / nFeatures from the matching spike group.
    4.  Compute per-channel noise threshold:
            threshold = thresholdFactor * median(|x|) / 0.6745
        This is identical to process_medianthreshold so results are
        consistent with what ndm_extractspikes will use.
    5.  Detect threshold crossings with a refractory period.
    6.  Build an N×N spike-coincidence similarity matrix C where
            C[i,j] = coincident_events(i,j) / (events(i) + 1)
        then symmetrised as (C + C^T) / 2.
    7.  Agglomerative Ward clustering on (1 - C) as a distance matrix.
        Optimal k in [1, maxSubGroups] chosen by maximising the average
        silhouette score with the constraint that every sub-group has at
        least minChannels channels.
    8.  Write the resulting groups back to spikeDetection.channelGroups
        in the YAML file atomically.

    GPU / CPU dispatch
    ------------------
    Steps 2-4 are the computationally intensive parts.  When compiled with
    USE_CUDA the GPU kernel (process_spikegrouper_cuda.cu) handles them.
    Without CUDA, OpenMP parallelises the per-channel threshold+detection
    loop and the O(N²) coincidence matrix construction.

    copyright  (C) 2025 neurosuite-3 contributors

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 3 of the License, or
    (at your option) any later version.
 ***************************************************************************/

#define _LARGEFILE_SOURCE64
#define _FILE_OFFSET_BITS 64

#include "process_spikegrouper.h"

#include <algorithm>
#include <cassert>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <numeric>
#include <sstream>
#include <string>
#include <vector>

#ifdef _OPENMP
#include <omp.h>
#endif

using namespace std;

// ---------------------------------------------------------------------------
// Forward declarations (YAML helpers defined below main)
// ---------------------------------------------------------------------------
static vector<vector<int>>  readAnatomicalGroups(const string& yamlPath);
static vector<ChannelGroup> readYamlGroups(const string& yamlPath);
static void                 writeYamlGroups(const string& yamlPath,
                                            const vector<ChannelGroup>& groups);

// ---------------------------------------------------------------------------
// CLI
// ---------------------------------------------------------------------------
static void usage(const char* name)
{
    cerr << "usage: " << name << " [options]\n"
         << "  --fil            FILE    .fil input file (required)\n"
         << "  --yaml           FILE    YAML parameter file to update (required)\n"
         << "  --nchannels      N       total channels in .fil (required)\n"
         << "  --nbits          N       bits per sample (default 16)\n"
         << "  --sampling-rate  SR      sampling rate in Hz (required)\n"
         << "  --threshold-factor F     threshold = F * sigma_n (default 3.0)\n"
         << "  --refractory-ms  R       min inter-event interval ms (default 1.0)\n"
         << "  --coincidence-ms W       coincidence half-window ms (default 0.4)\n"
         << "  --window-sec     T       seconds of .fil to analyse (default 60)\n"
         << "  --max-sub-groups K       maximum sub-groups per group (default 6)\n"
         << "  --min-channels   M       minimum channels per sub-group (default 4)\n"
         << "  --max-merged-size S      maximum channels after merging adjacent groups (default 12)\n"
         << "  --channel-overlap N      channels borrowed from each neighbour group (default 0)\n"
         << "  --exclude-channels LIST  comma-separated channel indices to exclude (e.g. 3,7,42)\n"
         << "  --nsamples       S       nSamples for new groups (default 52)\n"
         << "  --peak-sample    P       peakSampleIndex for new groups (default 26)\n"
         << "  --nfeatures      F       nFeatures for new groups (default 3)\n"
#ifdef USE_CUDA
         << "  --cpu                    force CPU path even when CUDA is available\n"
#endif
         << "  -v                       verbose output\n"
         << "  -h                       show this help\n";
    exit(1);
}

static SpikeGrouperArgs parseArgs(int argc, char* argv[])
{
    SpikeGrouperArgs a;
    bool haveChannels = false, haveSR = false;

    for (int i = 1; i < argc; ++i) {
        string arg(argv[i]);
        auto next = [&]() -> const char* {
            if (++i >= argc) { cerr << "missing value for " << arg << "\n"; exit(1); }
            return argv[i];
        };
        if      (arg == "-h" || arg == "--help")          usage(argv[0]);
        else if (arg == "-v" || arg == "--verbose")        a.verbose = true;
        else if (arg == "--cpu")                           a.forceCPU = true;
        else if (arg == "--fil")                           a.filPath          = next();
        else if (arg == "--yaml")                          a.yamlPath         = next();
        else if (arg == "--nchannels")  { a.nChannels       = atoi(next()); haveChannels=true; }
        else if (arg == "--nbits")        a.nBits            = atoi(next());
        else if (arg == "--sampling-rate"){ a.samplingRate   = atof(next()); haveSR=true; }
        else if (arg == "--threshold-factor") a.thresholdFactor = atof(next());
        else if (arg == "--refractory-ms")    a.refractoryMs    = atof(next());
        else if (arg == "--coincidence-ms")   a.coincidenceMs   = atof(next());
        else if (arg == "--window-sec")       a.windowSec       = atof(next());
        else if (arg == "--max-sub-groups")   a.maxSubGroups    = atoi(next());
        else if (arg == "--min-channels")     a.minChannels     = atoi(next());
        else if (arg == "--max-merged-size")  a.maxMergedSize   = atoi(next());
        else if (arg == "--channel-overlap")  a.channelOverlap  = atoi(next());
        else if (arg == "--exclude-channels") {
            // Comma-separated list of channel indices, e.g. "3,7,42"
            string list = next();
            stringstream ss(list);
            string token;
            while (getline(ss, token, ',')) {
                token.erase(remove_if(token.begin(), token.end(), ::isspace), token.end());
                if (!token.empty()) a.excludeChannels.insert(atoi(token.c_str()));
            }
        }
        else if (arg == "--nsamples")         a.nSamples        = atoi(next());
        else if (arg == "--peak-sample")      a.peakSampleIndex = atoi(next());
        else if (arg == "--nfeatures")        a.nFeatures       = atoi(next());
        else { cerr << "unknown option: " << arg << "\n"; usage(argv[0]); }
    }

    if (a.filPath.empty())  { cerr << "--fil is required\n";          exit(11); }
    if (a.yamlPath.empty()) { cerr << "--yaml is required\n";         exit(11); }
    if (!haveChannels)      { cerr << "--nchannels is required\n";    exit(1);  }
    if (!haveSR)            { cerr << "--sampling-rate is required\n"; exit(1); }
    if (a.minChannels < 1)  a.minChannels = 1;
    if (a.maxMergedSize < a.minChannels) {
        cerr << "warning: --max-merged-size (" << a.maxMergedSize
             << ") < --min-channels (" << a.minChannels
             << "); raising max-merged-size to match\n";
        a.maxMergedSize = a.minChannels;
    }

    return a;
}

// ===========================================================================
// CPU: threshold computation  (mirrors process_medianthreshold)
// ===========================================================================

// Returns median(|signal|) / 0.6745 for signal[offset], [offset + stride], ...
static double medianSigma(const short* data, long int nSamplesPerCh,
                           int offset, int stride)
{
    vector<int> absVals(nSamplesPerCh);
    for (long int s = 0; s < nSamplesPerCh; ++s)
        absVals[s] = abs((int)data[offset + s * stride]);
    size_t mid = nSamplesPerCh / 2;
    nth_element(absVals.begin(), absVals.begin() + mid, absVals.end());
    return absVals[mid] / 0.6745;
}

// Detect threshold crossings, honouring a refractory period.
// Returns sorted vector of sample indices.
static vector<long int> detectEvents(const short* data, long int nSamplesPerCh,
                                      int offset, int stride,
                                      double threshold, int refractorySamples)
{
    vector<long int> events;
    bool above = false;
    long int lastEvent = -refractorySamples - 1;
    for (long int s = 0; s < nSamplesPerCh; ++s) {
        short v = data[offset + s * stride];
        bool nowAbove = (v > threshold || v < -threshold);
        if (nowAbove && !above) {                        // rising edge
            if (s - lastEvent >= refractorySamples) {
                events.push_back(s);
                lastEvent = s;
            }
        }
        above = nowAbove;
    }
    return events;
}

// ===========================================================================
// CPU: coincidence matrix
// ===========================================================================
//
// C[i,j] = events on ch-i that have at least one co-event on ch-j within
//           ±coincidenceSamples  /  (events on ch-i + 1)
// Symmetrised: (C + C^T) / 2.
//
// Uses sorted event lists + binary search: O(E_i * log E_j) per pair.
// OpenMP parallelises the outer i-loop.

static vector<double> buildCoincidenceMatrix(
    const vector<vector<long int>>& events,
    int n, int coincidenceSamples)
{
    vector<double> C(n * n, 0.0);

#ifdef _OPENMP
    #pragma omp parallel for schedule(dynamic)
#endif
    for (int i = 0; i < n; ++i) {
        const auto& ei = events[i];
        if (ei.empty()) continue;
        for (int j = 0; j < n; ++j) {
            if (i == j) { C[i*n+j] = 1.0; continue; }
            const auto& ej = events[j];
            if (ej.empty()) continue;
            long int coincident = 0;
            for (long int t : ei) {
                long int lo = t - coincidenceSamples;
                long int hi = t + coincidenceSamples;
                // lower_bound / upper_bound on sorted ej
                auto it = lower_bound(ej.begin(), ej.end(), lo);
                if (it != ej.end() && *it <= hi)
                    ++coincident;
            }
            C[i*n+j] = (double)coincident / (double)(ei.size() + 1);
        }
    }

    // Symmetrise
    for (int i = 0; i < n; ++i)
        for (int j = i+1; j < n; ++j) {
            double s = 0.5 * (C[i*n+j] + C[j*n+i]);
            C[i*n+j] = C[j*n+i] = s;
        }
    return C;
}

// ===========================================================================
// Agglomerative Ward clustering (Lance-Williams update)
// ===========================================================================
//
// Standard O(N³) implementation using the stored distance matrix.
// N is at most a few hundred channels — always fast.

// Ward's merge formula (Lance-Williams):
//   d(k, i∪j) = sqrt( (nk+ni)/(nk+ni+nj) * d(k,i)^2
//                    + (nk+nj)/(nk+ni+nj) * d(k,j)^2
//                    - nk      /(nk+ni+nj) * d(i,j)^2 )

static vector<int> wardCluster(const vector<double>& distMat, int n,
                                int targetK)
{
    // dist is a flat n×n matrix; work with a copy
    vector<double> D(distMat);
    vector<int>    labels(n);
    iota(labels.begin(), labels.end(), 0);  // 0..n-1

    // Cluster sizes
    vector<int> sizes(n, 1);
    // Active cluster ids
    vector<bool> active(n, true);
    int nActive = n;

    while (nActive > targetK) {
        // Find the minimum off-diagonal distance among active clusters
        double minDist = 1e30;
        int    bi = -1, bj = -1;
        for (int i = 0; i < n; ++i) {
            if (!active[i]) continue;
            for (int j = i+1; j < n; ++j) {
                if (!active[j]) continue;
                if (D[i*n+j] < minDist) {
                    minDist = D[i*n+j];
                    bi = i; bj = j;
                }
            }
        }
        if (bi < 0) break;

        // Merge bj into bi
        int ni = sizes[bi], nj = sizes[bj];
        // Update distances from all other active clusters k to the merged cluster
        for (int k = 0; k < n; ++k) {
            if (!active[k] || k == bi || k == bj) continue;
            int nk = sizes[k];
            double dki = D[k*n+bi];
            double dkj = D[k*n+bj];
            double dij = D[bi*n+bj];
            int total = nk + ni + nj;
            double newD = sqrt(
                (double)(nk+ni) / total * dki * dki +
                (double)(nk+nj) / total * dkj * dkj -
                (double)nk      / total * dij * dij
            );
            D[k*n+bi] = D[bi*n+k] = newD;
        }
        sizes[bi] += nj;
        active[bj] = false;
        --nActive;

        // Relabel all samples that had label bj → bi
        for (int s = 0; s < n; ++s)
            if (labels[s] == bj) labels[s] = bi;
    }

    // Re-map labels to 0..k-1
    vector<int> mapping(n, -1);
    int next = 0;
    vector<int> result(n);
    for (int s = 0; s < n; ++s) {
        int lbl = labels[s];
        if (mapping[lbl] < 0) mapping[lbl] = next++;
        result[s] = mapping[lbl];
    }
    return result;
}

// ===========================================================================
// Silhouette score (precomputed distance matrix)
// ===========================================================================

// Average silhouette score given distance matrix D (n×n flat) and labels.
static double silhouetteScore(const vector<double>& D, const vector<int>& labels, int n)
{
    if (n <= 1) return 0.0;

    // Count per-cluster
    int maxLabel = *max_element(labels.begin(), labels.end());
    int K = maxLabel + 1;
    if (K <= 1) return 0.0;

    vector<double> silhouettes(n, 0.0);
    for (int i = 0; i < n; ++i) {
        // a(i) = mean distance to same-cluster members
        double sumA = 0.0; int cntA = 0;
        double bestB = 1e30;
        for (int k = 0; k < K; ++k) {
            double sumK = 0.0; int cntK = 0;
            for (int j = 0; j < n; ++j) {
                if (j == i) continue;
                if (labels[j] == k) { sumK += D[i*n+j]; ++cntK; }
            }
            if (cntK == 0) continue;
            double mean = sumK / cntK;
            if (k == labels[i]) { sumA = sumK; cntA = cntK; }
            else bestB = min(bestB, mean);
        }
        if (cntA == 0) { silhouettes[i] = 0.0; continue; }
        double a = sumA / cntA;
        double b = bestB;
        double denom = max(a, b);
        silhouettes[i] = (denom > 0.0) ? (b - a) / denom : 0.0;
    }
    double total = 0.0;
    for (double s : silhouettes) total += s;
    return total / n;
}

// ===========================================================================
// Post-split adjacency merge
// ===========================================================================
//
// After Ward clustering produces a set of contiguous channel groups, we
// consider merging neighboring groups when doing so would reduce the chance
// of splitting spikes across group boundaries.
//
// Scoring
// -------
// The coincidence matrix C (already symmetrised, values in [0,1]) measures
// how often a spike on channel i fires within the coincidence window of
// channel j.  For two adjacent groups A and B, the inter-group coincidence
// score is:
//
//   score(A,B) = mean of C[i,j] for all i in A, j in B (and vice-versa)
//
// A high score means spikes from these two groups frequently co-fire, so
// separating them will split many spikes — a strong reason to merge.
//
// Constraints
// -----------
//   • merged group size ≤ maxMergedSize  (enforced by groupChannels before
//     this function is called; this pass will not create oversized groups
//     because all input groups already satisfy the cap)
//   • all groups have ≥ minChannels  (also enforced by groupChannels;
//     this pass only merges groups that already satisfy the minimum)
//
// Algorithm
// ---------
// Greedy: repeatedly merge the adjacent pair with the highest inter-group
// coincidence score, subject to the constraints, until no valid merge remains.
// O(K² log K) per iteration where K = number of groups.

// Returns the mean coincidence between the channels of two groups.
static double interGroupCoinc(
    const vector<double>& coinc,   // full n×n coincidence matrix (original group)
    int n,                          // size of coinc matrix
    const vector<int>& idxA,       // indices INTO the n-channel array for group A
    const vector<int>& idxB)       // indices for group B
{
    if (idxA.empty() || idxB.empty()) return 0.0;
    double sum = 0.0;
    long cnt = 0;
    for (int i : idxA)
        for (int j : idxB) {
            sum += coinc[i * n + j];
            sum += coinc[j * n + i];
            cnt += 2;
        }
    return cnt > 0 ? sum / cnt : 0.0;
}

// channelIndices[g] = indices (0..n-1) of the channels in sub-group g,
// in terms of the original n-channel coincidence matrix ordering.
// groupChannels[g] = actual channel IDs.
// coinc = n×n coincidence matrix for the full group.
// Returns updated groupChannels after merging.
static vector<vector<int>> mergeAdjacentGroups(
    const vector<vector<int>>& groupChIds,    // channel IDs per sub-group (sorted)
    const vector<vector<int>>& groupIdxs,     // coinc-matrix indices per sub-group
    const vector<double>&      coinc,
    int                        n,
    int                        minChannels,
    int                        maxMergedSize,
    bool                       verbose)
{
    int K = (int)groupChIds.size();
    if (K <= 1) return groupChIds;

    // Working copies
    vector<vector<int>> chIds  = groupChIds;
    vector<vector<int>> idxs   = groupIdxs;

    bool merged = true;
    while (merged) {
        merged = false;
        K = (int)chIds.size();
        if (K <= 1) break;

        // Find the adjacent pair with the best (highest) inter-group coinc
        // subject to size constraints.
        double bestScore = -1.0;
        int    bestA = -1, bestB = -1;

        for (int a = 0; a < K - 1; ++a) {
            int b = a + 1;   // only adjacent pairs
            int mergedSize = (int)(chIds[a].size() + chIds[b].size());
            if (mergedSize > maxMergedSize) continue;
            // After merge, remaining groups must still be ≥ minChannels.
            // The merged group itself will be mergedSize ≥ each part which
            // was already ≥ minChannels (enforced by clustering), so no
            // issue there. We just enforce the cap.
            double score = interGroupCoinc(coinc, n, idxs[a], idxs[b]);
            if (score > bestScore) {
                bestScore = score;
                bestA = a; bestB = b;
            }
        }

        if (bestA < 0 || bestScore <= 0.0) break;  // nothing to merge

        if (verbose)
            printf("    merge groups %d+%d → %d channels  inter-coinc=%.4f\n",
                   bestA, bestB,
                   (int)(chIds[bestA].size() + chIds[bestB].size()),
                   bestScore);

        // Merge bestB into bestA
        for (int c : chIds[bestB]) chIds[bestA].push_back(c);
        for (int i : idxs[bestB]) idxs[bestA].push_back(i);
        // chIds[bestA] should already be sorted (bestA < bestB and
        // both were contiguous sorted blocks), so just sort to be safe.
        sort(chIds[bestA].begin(), chIds[bestA].end());

        chIds.erase(chIds.begin() + bestB);
        idxs.erase(idxs.begin()  + bestB);
        merged = true;
    }

    return chIds;
}

// ===========================================================================
// Group one block of channels
// ===========================================================================

static vector<vector<int>> groupChannels(
    const vector<double>&       coinc,    // symmetrised, n×n
    const vector<long int>&     /*unused_events_counts*/,
    const vector<int>&          channelIds,
    int                         n,
    int                         maxSubGroups,
    int                         minChannels,
    int                         maxMergedSize,
    bool                        verbose,
    vector<vector<int>>*        outIdxs = nullptr)  // if non-null: per-subgroup coinc indices
{
    // Distance matrix = 1 - coincidence  (clip to [0,1])
    vector<double> D(n * n);
    for (int i = 0; i < n*n; ++i)
        D[i] = max(0.0, 1.0 - coinc[i]);
    for (int i = 0; i < n; ++i) D[i*n+i] = 0.0;

    int kMax = min(maxSubGroups, n / max(1, minChannels));
    if (kMax < 1) kMax = 1;

    int           bestK      = 1;
    double        bestSil   = -1.0;
    vector<int>   bestLabels;

    // wardCluster modifies D in place; take a fresh copy for each k.
    const vector<double> D0(D);   // pristine distance matrix

    for (int k = 1; k <= kMax; ++k) {
        vector<double> Dk(D0);    // copy for this k
        vector<int> lbl = wardCluster(Dk, n, k);
        // Check min-cluster-size constraint
        vector<int> cnt(k, 0);
        for (int x : lbl) ++cnt[x];
        bool valid = true;
        for (int c : cnt) if (c < minChannels) { valid = false; break; }
        if (!valid) continue;

        double sil = (k == 1) ? 0.0 : silhouetteScore(Dk, lbl, n);
        if (sil > bestSil) { bestSil = sil; bestK = k; bestLabels = lbl; }
    }

    if (verbose)
        printf("  optimal k=%d  silhouette=%.4f\n", bestK, bestSil);

    // bestLabels was computed from a clean copy of D — no re-clustering needed.
    vector<int> finalLabels = bestLabels;
    if (finalLabels.empty()) {
        // Fallback: shouldn't happen, but guard against it
        vector<double> Dk(D0);
        finalLabels = wardCluster(Dk, n, bestK);
    }

    // Build contiguous output groups.
    //
    // Arbitrary cluster labels can produce interleaved channel sets (e.g.
    // group 1 = {0..14, 44..95}, group 2 = {15..43}) which look like
    // overlapping groups on a probe display and are meaningless for
    // spike sorting.  Instead, project the cluster labels onto the
    // sorted channel order and find the bestK-1 split points that
    // minimise label disagreement — producing contiguous blocks.
    //
    // Algorithm: channels are already ordered by channelIds (which are
    // sorted by the caller).  For bestK=1 just return all channels.
    // For bestK>1, find the split positions in sorted order that
    // maximise within-block label homogeneity (argmin of a simple
    // dynamic-programme over O(n * bestK) cells).

    // Sort channels by id; keep track of original index for label lookup
    vector<int> sortedIdx(n);
    iota(sortedIdx.begin(), sortedIdx.end(), 0);
    sort(sortedIdx.begin(), sortedIdx.end(),
         [&](int a, int b){ return channelIds[a] < channelIds[b]; });

    vector<int> sortedChs(n), sortedLbls(n);
    for (int i = 0; i < n; ++i) {
        sortedChs[i]  = channelIds[sortedIdx[i]];
        sortedLbls[i] = finalLabels[sortedIdx[i]];
    }

    vector<vector<int>> result;

    if (bestK == 1) {
        result.push_back(sortedChs);
        if (outIdxs) {
            vector<int> allIdxs(n);
            iota(allIdxs.begin(), allIdxs.end(), 0);
            outIdxs->push_back(allIdxs);
        }
        return result;
    }

    // Cost of assigning sorted positions [l..r] to a single group:
    // number of positions that disagree with the majority label in [l..r].
    // Pre-compute label-count prefix sums for each of the bestK labels.
    // Since labels are 0..bestK-1 after re-mapping inside wardCluster we
    // use the actual label values present in sortedLbls.
    int maxLbl = *max_element(sortedLbls.begin(), sortedLbls.end()) + 1;
    // prefix[k][i] = count of label k in sortedLbls[0..i-1]
    vector<vector<int>> prefix(maxLbl, vector<int>(n+1, 0));
    for (int k = 0; k < maxLbl; ++k)
        for (int i = 0; i < n; ++i)
            prefix[k][i+1] = prefix[k][i] + (sortedLbls[i] == k ? 1 : 0);

    auto segCost = [&](int l, int r) -> int {
        // cost = segment length - count of most-frequent label in [l..r]
        int len = r - l + 1;
        int best = 0;
        for (int k = 0; k < maxLbl; ++k)
            best = max(best, prefix[k][r+1] - prefix[k][l]);
        return len - best;
    };

    // DP: dp[k][i] = min cost to split sortedChs[0..i] into k contiguous groups
    // split[k][i] = the split point achieving dp[k][i]
    const int INF = n + 1;
    vector<vector<int>> dp(bestK+1, vector<int>(n, INF));
    vector<vector<int>> sp(bestK+1, vector<int>(n, 0));

    for (int i = 0; i < n; ++i) { dp[1][i] = segCost(0, i); sp[1][i] = 0; }

    for (int k = 2; k <= bestK; ++k) {
        for (int i = k-1; i < n; ++i) {
            for (int j = k-2; j < i; ++j) {
                int cost = dp[k-1][j] + segCost(j+1, i);
                if (cost < dp[k][i]) { dp[k][i] = cost; sp[k][i] = j+1; }
            }
        }
    }

    // Back-track to recover split points
    vector<int> splits;
    int pos = n - 1;
    for (int k = bestK; k >= 2; --k) {
        splits.push_back(sp[k][pos]);
        pos = sp[k][pos] - 1;
    }
    splits.push_back(0);
    reverse(splits.begin(), splits.end());  // now splits[i] = start of block i

    // Collect raw DP blocks (channel ids + coinc-matrix indices)
    struct Block {
        vector<int> chs;
        vector<int> idxs;
    };
    vector<Block> blocks;
    for (int k = 0; k < bestK; ++k) {
        int l = splits[k];
        int r = (k + 1 < bestK) ? splits[k+1] - 1 : n - 1;
        Block b;
        b.chs = vector<int>(sortedChs.begin() + l, sortedChs.begin() + r + 1);
        for (int p = l; p <= r; ++p)
            b.idxs.push_back(sortedIdx[p]);
        blocks.push_back(move(b));
    }

    // ---- Enforce maxMergedSize: re-split any oversized block ----
    // Split at maxMergedSize-aligned boundaries, each piece >= minChannels.
    // (maxMergedSize > 0 is guaranteed by the caller.)
    if (maxMergedSize > 0) {
        vector<Block> sized;
        for (auto& b : blocks) {
            int sz = (int)b.chs.size();
            if (sz <= maxMergedSize) {
                sized.push_back(move(b));
                continue;
            }
            // How many pieces do we need?  Round up to keep each piece <= maxMergedSize
            // but also >= minChannels if possible.
            int pieces = (sz + maxMergedSize - 1) / maxMergedSize;
            int chunkSz = sz / pieces;  // base size; last piece gets the remainder
            int pos2 = 0;
            for (int p = 0; p < pieces; ++p) {
                int len = (p < pieces - 1) ? chunkSz : (sz - pos2);
                Block nb;
                nb.chs  = vector<int>(b.chs.begin()  + pos2, b.chs.begin()  + pos2 + len);
                nb.idxs = vector<int>(b.idxs.begin() + pos2, b.idxs.begin() + pos2 + len);
                sized.push_back(move(nb));
                pos2 += len;
            }
        }
        blocks = move(sized);
    }

    // ---- Enforce minChannels: absorb undersized blocks into best neighbour ----
    // Walk until stable; at most O(n) passes for n blocks.
    bool changed = true;
    while (changed && blocks.size() > 1) {
        changed = false;
        for (int i = 0; i < (int)blocks.size(); ++i) {
            if ((int)blocks[i].chs.size() >= minChannels) continue;
            // Find the neighbour (left or right) whose merged size is smallest
            // (prefer the smaller merge to stay closer to maxMergedSize).
            int best = -1;
            int bestSz = INT_MAX;
            if (i > 0) {
                int sz = (int)(blocks[i-1].chs.size() + blocks[i].chs.size());
                if (sz < bestSz) { bestSz = sz; best = i - 1; }
            }
            if (i + 1 < (int)blocks.size()) {
                int sz = (int)(blocks[i+1].chs.size() + blocks[i].chs.size());
                if (sz < bestSz) { bestSz = sz; best = i + 1; }
            }
            if (best < 0) break;
            // Merge block i into best
            int dst = min(i, best);
            int src = max(i, best);
            for (int c : blocks[src].chs)  blocks[dst].chs.push_back(c);
            for (int x : blocks[src].idxs) blocks[dst].idxs.push_back(x);
            sort(blocks[dst].chs.begin(),  blocks[dst].chs.end());
            blocks.erase(blocks.begin() + src);
            changed = true;
            break;  // restart scan after any merge
        }
    }

    // Emit final result
    for (auto& b : blocks) {
        result.push_back(b.chs);
        if (outIdxs)
            outIdxs->push_back(b.idxs);
    }
    return result;
}

// ===========================================================================
// .fil reader
// ===========================================================================

static vector<short> loadFilWindow(const string& path, int nChannels,
                                    int nBits, double windowSec,
                                    double samplingRate, long int& nSamplesOut)
{
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) { perror(path.c_str()); exit(11); }

    fseeko(f, 0, SEEK_END);
    long long fileSize = ftello(f);
    fseeko(f, 0, SEEK_SET);

    int bytesPerSample = (nBits <= 16) ? 2 : 4;
    long int maxSamples = (long int)(windowSec * samplingRate);
    long int available  = fileSize / (nChannels * bytesPerSample);
    nSamplesOut = min(maxSamples, available);

    if (nSamplesOut < 1) {
        cerr << "process_spikegrouper: .fil file too small\n";
        fclose(f); exit(11);
    }

    size_t nElems = (size_t)nSamplesOut * nChannels;
    vector<short> data(nElems);
    size_t nRead = fread(data.data(), sizeof(short), nElems, f);
    fclose(f);
    if ((long int)(nRead / nChannels) < nSamplesOut)
        nSamplesOut = (long int)(nRead / nChannels);
    return data;
}

// ===========================================================================
// main
// ===========================================================================

int main(int argc, char* argv[])
{
    SpikeGrouperArgs args = parseArgs(argc, argv);

    if (args.verbose) {
        printf("[process_spikegrouper]\n");
        printf("  fil:          %s\n", args.filPath.c_str());
        printf("  yaml:         %s\n", args.yamlPath.c_str());
        printf("  nChannels:    %d  nBits: %d  samplingRate: %.1f Hz\n",
               args.nChannels, args.nBits, args.samplingRate);
        printf("  threshold:    %.1f * sigma_n\n", args.thresholdFactor);
        printf("  refractory:   %.2f ms\n", args.refractoryMs);
        printf("  coincidence:  %.2f ms  window: %.1f s\n",
               args.coincidenceMs, args.windowSec);
        printf("  maxSubGroups: %d  minChannels: %d  maxMergedSize: %d  channelOverlap: %d\n",
               args.maxSubGroups, args.minChannels, args.maxMergedSize, args.channelOverlap);
        if (!args.excludeChannels.empty()) {
            printf("  excludeChannels:");
            for (int ch : args.excludeChannels) printf(" %d", ch);
            printf("\n");
        }
    }

    // ----- Load spikeDetection groups (for metadata and channel lists) ----
    vector<ChannelGroup> spikeGroups = readYamlGroups(args.yamlPath);
    if (spikeGroups.empty()) {
        cerr << "process_spikegrouper: no spikeDetection groups found in "
             << args.yamlPath << "\n";
        return 1;
    }

    // ----- Load anatomical groups (hard shank boundaries) -----------------
    vector<vector<int>> anatGroups = readAnatomicalGroups(args.yamlPath);
    if (anatGroups.empty()) {
        cerr << "process_spikegrouper: no anatomicalDescription groups found in "
             << args.yamlPath << "\n";
        return 1;
    }
    if (args.verbose) {
        printf("  anatomical groups: %d  spikeDetection groups: %d\n",
               (int)anatGroups.size(), (int)spikeGroups.size());
    }

    // Build channel → spike-group-index lookup (for metadata inheritance)
    map<int,int> chToSpikeGroup;
    for (int g = 0; g < (int)spikeGroups.size(); ++g)
        for (int ch : spikeGroups[g].channels)
            chToSpikeGroup[ch] = g;

    // ----- Load .fil window -----------------------------------------------
    long int nSamplesPerCh = 0;
    vector<short> filData = loadFilWindow(args.filPath, args.nChannels,
                                           args.nBits, args.windowSec,
                                           args.samplingRate, nSamplesPerCh);
    if (args.verbose)
        printf("  loaded %ld samples × %d channels\n", nSamplesPerCh, args.nChannels);

    const int refractSamp = max(1, (int)(args.refractoryMs  * args.samplingRate / 1000.0));
    const int coincSamp   = max(1, (int)(args.coincidenceMs * args.samplingRate / 1000.0));

#ifdef USE_CUDA
    bool useCUDA = !args.forceCPU;
    if (useCUDA) {
        if (cudaHasDevice() == 0) { useCUDA = false; }
        if (args.verbose && useCUDA) printf("  using CUDA GPU path\n");
        if (args.verbose && !useCUDA) printf("  no CUDA device — falling back to CPU\n");
    }
#endif

    // ----- Process each anatomical group ----------------------------------
    vector<ChannelGroup> outputGroups;

    for (int ag = 0; ag < (int)anatGroups.size(); ++ag) {

        // Intersect anatomical channels with spikeDetection channels.
        // Channels absent from spikeDetection are silently skipped — they
        // were intentionally excluded (e.g. reference or bad channels).
        // User-supplied excludeChannels are also removed here.
        vector<int> validChs;
        for (int ch : anatGroups[ag])
            if (ch >= 0 && ch < args.nChannels
                && chToSpikeGroup.count(ch)
                && !args.excludeChannels.count(ch))
                validChs.push_back(ch);

        if (validChs.empty()) {
            if (args.verbose)
                printf("  Anatomical group %d/%d: no spikeDetection channels — skipping\n",
                       ag+1, (int)anatGroups.size());
            continue;
        }

        // Inherit metadata from the spike group that contains the first channel.
        // All channels in an anatomical group normally belong to the same spike
        // group; if not, the first channel's group wins.
        int sgIdx = chToSpikeGroup.at(validChs.front());
        const ChannelGroup& meta = spikeGroups[sgIdx];

        if (args.verbose)
            printf("  Anatomical group %d/%d: %d channels [%d..%d]  ""(spike group %d, nSamples=%d)\n",
                   ag+1, (int)anatGroups.size(), (int)validChs.size(),
                   validChs.front(), validChs.back(),
                   sgIdx+1, meta.nSamples);

        if ((int)validChs.size() < 2 * args.minChannels) {
            // Too small to subdivide — pass through unchanged
            if (args.verbose)
                printf("    → %d channels < 2×minChannels=%d; keeping intact\n",
                       (int)validChs.size(), args.minChannels);
            ChannelGroup out;
            out.channels        = validChs;
            out.nSamples        = meta.nSamples;
            out.peakSampleIndex = meta.peakSampleIndex;
            out.nFeatures       = meta.nFeatures;
            outputGroups.push_back(out);
            continue;
        }

        const int nv = (int)validChs.size();
        vector<double> sigmas(nv, 0.0);
        vector<double> coinc(nv * nv, 0.0);

#ifdef USE_CUDA
        if (useCUDA) {
            runCudaSpikeGrouper(
                filData.data(),
                (long int)nSamplesPerCh * args.nChannels,
                nSamplesPerCh,
                args.nChannels,
                validChs.data(),
                nv,
                args.thresholdFactor,
                refractSamp,
                coincSamp,
                sigmas.data(),
                coinc.data(),
                args.verbose
            );
        } else
#endif
        {
            // CPU path: per-channel sigma_n, event detection, coincidence matrix
            vector<vector<long int>> events(nv);

#ifdef _OPENMP
            #pragma omp parallel for schedule(dynamic)
#endif
            for (int i = 0; i < nv; ++i) {
                int ch = validChs[i];
                sigmas[i] = medianSigma(filData.data(), nSamplesPerCh,
                                         ch, args.nChannels);
                double thr = args.thresholdFactor * sigmas[i];
                events[i] = detectEvents(filData.data(), nSamplesPerCh,
                                          ch, args.nChannels, thr, refractSamp);
                if (args.verbose)
                    printf("    ch %4d: sigma_n=%.1f  threshold=%.1f  events=%zu\n",
                           ch, sigmas[i], thr, events[i].size());
            }

            coinc = buildCoincidenceMatrix(events, nv, coincSamp);
        }

        vector<long int> dummy;
        vector<vector<int>> subGroupIdxs;
        vector<vector<int>> subGroups = groupChannels(
            coinc, dummy, validChs, nv,
            args.maxSubGroups, args.minChannels, args.maxMergedSize,
            args.verbose, &subGroupIdxs
        );

        // ---- Post-split merge: re-combine adjacent groups that have high
        //      cross-boundary coincidence, up to maxMergedSize channels. ----
        if ((int)subGroups.size() > 1) {
            if (args.verbose)
                printf("    → merge pass: %d groups, minCh=%d, maxMerged=%d\n",
                       (int)subGroups.size(), args.minChannels, args.maxMergedSize);
            subGroups = mergeAdjacentGroups(
                subGroups, subGroupIdxs, coinc, nv,
                args.minChannels, args.maxMergedSize, args.verbose);
        }

        if (args.verbose) {
            printf("    → %d sub-group(s) after merge\n", (int)subGroups.size());
            for (int s = 0; s < (int)subGroups.size(); ++s) {
                printf("       [%d] %d channels (core):", s, (int)subGroups[s].size());
                for (int c : subGroups[s]) printf(" %d", c);
                printf("\n");
            }
        }

        // ---- Channel overlap: borrow N channels from each neighbour group ----
        // Applied after all splitting/merging so core group sizes are final.
        // Each group gets up to channelOverlap channels prepended from the
        // previous group's tail and appended from the next group's head.
        // subGroups are sorted ascending (contiguous blocks on the same shank)
        // so borrowing from tails/heads preserves anatomical proximity.
        // The core group boundaries are unchanged — overlap channels are
        // purely additive context for spike sorting.
        vector<vector<int>> overlappedGroups(subGroups);
        if (args.channelOverlap > 0 && (int)subGroups.size() > 1) {
            for (int s = 0; s < (int)subGroups.size(); ++s) {
                vector<int> extended = subGroups[s];

                // Borrow from the tail of the previous group
                if (s > 0) {
                    const auto& prev = subGroups[s-1];
                    int n_borrow = min(args.channelOverlap, (int)prev.size());
                    for (int i = (int)prev.size() - n_borrow; i < (int)prev.size(); ++i)
                        extended.push_back(prev[i]);
                }

                // Borrow from the head of the next group
                if (s + 1 < (int)subGroups.size()) {
                    const auto& nxt = subGroups[s+1];
                    int n_borrow = min(args.channelOverlap, (int)nxt.size());
                    for (int i = 0; i < n_borrow; ++i)
                        extended.push_back(nxt[i]);
                }

                // Sort and deduplicate
                sort(extended.begin(), extended.end());
                extended.erase(unique(extended.begin(), extended.end()), extended.end());
                overlappedGroups[s] = move(extended);
            }

            if (args.verbose) {
                for (int s = 0; s < (int)overlappedGroups.size(); ++s) {
                    int core  = (int)subGroups[s].size();
                    int total = (int)overlappedGroups[s].size();
                    if (total != core)
                        printf("       [%d] %d channels after overlap (+%d borrowed):",
                               s, total, total - core);
                    for (int c : overlappedGroups[s]) printf(" %d", c);
                    if (total != core) printf("\n");
                }
            }
        }

        for (const auto& sg : overlappedGroups) {
            ChannelGroup out;
            out.channels        = sg;
            out.nSamples        = meta.nSamples;
            out.peakSampleIndex = meta.peakSampleIndex;
            out.nFeatures       = meta.nFeatures;
            outputGroups.push_back(out);
        }
    }

    if (outputGroups.empty()) {
        cerr << "process_spikegrouper: no output groups produced\n";
        return 1;
    }

    if (args.verbose)
        printf("  Writing %d spikeDetection groups to %s\n",
               (int)outputGroups.size(), args.yamlPath.c_str());

    writeYamlGroups(args.yamlPath, outputGroups);

    if (args.verbose) printf("  Done.\n");
    return 0;
}

// ---------------------------------------------------------------------------
static string ltrim(const string& s) {
    size_t i = s.find_first_not_of(" \t");
    return (i == string::npos) ? "" : s.substr(i);
}

// Indentation of a line (number of leading spaces)

static int indent(const string& s) {
    int n = 0;
    for (char c : s) { if (c == ' ') ++n; else break; }
    return n;
}

// Read an integer value from a YAML line like "  nSamples: 52"

// Parse anatomicalDescription.channelGroups from YAML.
// Returns one vector<int> per group — the channel ids (skip flag ignored).
// Format:
//   anatomicalDescription:
//     channelGroups:
//     - channels:
//       - id: 0
//         skip: 0
//       - id: 1
//         ...
// ---------------------------------------------------------------------------
static vector<vector<int>> readAnatomicalGroups(const string& yamlPath)
{
    ifstream fin(yamlPath);
    if (!fin) { cerr << "cannot open " << yamlPath << "\n"; exit(11); }
    vector<string> lines;
    { string l; while (getline(fin, l)) lines.push_back(l); }
    fin.close();

    vector<vector<int>> groups;

    // Find "anatomicalDescription:"
    int adLine = -1;
    for (int i = 0; i < (int)lines.size(); ++i) {
        if (ltrim(lines[i]).rfind("anatomicalDescription:", 0) == 0) {
            adLine = i; break;
        }
    }
    if (adLine < 0) return groups;

    // Find "channelGroups:" inside anatomicalDescription
    int cgLine = -1;
    for (int i = adLine+1; i < (int)lines.size(); ++i) {
        string t = ltrim(lines[i]);
        if (t.rfind("channelGroups:", 0) == 0) { cgLine = i; break; }
        if (!lines[i].empty() && lines[i][0] != ' ' && lines[i][0] != '\t') break;
    }
    if (cgLine < 0) return groups;

    // Walk the list of groups.  Each group starts with "- channels:".
    // Channel entries look like "- id: N" (with optional "skip: M" sibling).
    vector<int> cur;
    bool inGroup = false;

    for (int i = cgLine+1; i < (int)lines.size(); ++i) {
        const string& line = lines[i];
        if (line.empty()) continue;
        // Stop when we leave the anatomicalDescription block
        if (line[0] != ' ' && line[0] != '\t' && line[0] != '-') break;

        string t = ltrim(line);

        if (t == "- channels:" || t.rfind("- channels:", 0) == 0) {
            if (inGroup && !cur.empty()) groups.push_back(cur);
            cur.clear();
            inGroup = true;
            continue;
        }
        // Channel id line: "- id: N"
        if (inGroup && t.rfind("- id:", 0) == 0) {
            try {
                int id = stoi(t.substr(5));
                cur.push_back(id);
            } catch(...) {}
            continue;
        }
        // skip: line — ignore
        if (inGroup && t.rfind("skip:", 0) == 0) continue;

        // Any other non-empty line at the anatomicalDescription indent level
        // means we've left the channelGroups block.
        if (indent(line) <= indent(lines[cgLine]) && !t.empty() && t[0] != '-')
            break;
    }
    if (inGroup && !cur.empty()) groups.push_back(cur);
    return groups;
}

// ===========================================================================
// Minimal YAML reader/writer — handles the specific structure produced by
// the neurosuite-3 YAML tools (ndm_functions yaml_read, pyyaml dump).
//
// We only read spikeDetection.channelGroups and rewrite that section.
// Everything else in the file is preserved verbatim using a line-based
// approach: we locate the spikeDetection block, replace it, and write
// the rest of the file unchanged.
// ===========================================================================

// Trim leading whitespace
static int yamlIntVal(const string& line, int def) {
    size_t p = line.find(':');
    if (p == string::npos) return def;
    try { return stoi(line.substr(p+1)); } catch(...) { return def; }
}

// ---------------------------------------------------------------------------
// Parse spikeDetection.channelGroups from a flat YAML text.
// Returns the vector of ChannelGroup plus the line range [start,end) in
// the file that should be replaced.
// ---------------------------------------------------------------------------
static vector<ChannelGroup> readYamlGroups(const string& yamlPath)
{
    ifstream fin(yamlPath);
    if (!fin) { cerr << "cannot open " << yamlPath << "\n"; exit(11); }

    vector<string> lines;
    {
        string l;
        while (getline(fin, l)) lines.push_back(l);
    }
    fin.close();

    vector<ChannelGroup> groups;
    // Find "spikeDetection:" line
    int sdLine = -1;
    for (int i = 0; i < (int)lines.size(); ++i) {
        string t = ltrim(lines[i]);
        if (t == "spikeDetection:" || t.rfind("spikeDetection:", 0) == 0) {
            sdLine = i;
            break;
        }
    }
    if (sdLine < 0) return groups;

    // Find "  channelGroups:" inside spikeDetection block
    int cgLine = -1;
    for (int i = sdLine+1; i < (int)lines.size(); ++i) {
        string t = ltrim(lines[i]);
        if (t.rfind("channelGroups:", 0) == 0) { cgLine = i; break; }
        // Stop if we hit another top-level key
        if (!lines[i].empty() && lines[i][0] != ' ' && lines[i][0] != '\t') break;
    }
    if (cgLine < 0) return groups;

    // Parse the groups: each group starts with "  - channels:" and has
    // optional nSamples, peakSampleIndex, nFeatures lines
    ChannelGroup cur;
    bool inGroup = false;
    bool inChannels = false;
    int  groupIndent = -1;

    for (int i = cgLine+1; i < (int)lines.size(); ++i) {
        const string& line = lines[i];
        if (line.empty()) continue;

        // Stop at next top-level key
        if (line[0] != ' ' && line[0] != '\t' && line[0] != '-') break;

        string t = ltrim(line);

        if (t.rfind("- channels:", 0) == 0 || t == "- channels:") {
            // New group
            if (inGroup && !cur.channels.empty()) groups.push_back(cur);
            cur = ChannelGroup();
            inGroup = true;
            inChannels = true;
            groupIndent = indent(line);
            continue;
        }
        if (t.rfind("channels:", 0) == 0) {
            inChannels = true;
            continue;
        }
        if (inChannels && t.rfind("- ", 0) == 0) {
            // Channel id entry like "    - 42"
            try { cur.channels.push_back(stoi(t.substr(2))); } catch(...) {}
            continue;
        }
        // Other group fields
        if (t.rfind("nSamples:", 0) == 0)        { cur.nSamples        = yamlIntVal(t, cur.nSamples);        inChannels = false; continue; }
        if (t.rfind("peakSampleIndex:", 0) == 0)  { cur.peakSampleIndex = yamlIntVal(t, cur.peakSampleIndex); inChannels = false; continue; }
        if (t.rfind("nFeatures:", 0) == 0)        { cur.nFeatures       = yamlIntVal(t, cur.nFeatures);       inChannels = false; continue; }

        // Another top-level key inside spikeDetection or new section
        if (indent(line) <= indent(lines[cgLine])) {
            inChannels = false;
            // Could be another key in spikeDetection — continue parsing
        }
    }
    if (inGroup && !cur.channels.empty()) groups.push_back(cur);
    return groups;
}

// ---------------------------------------------------------------------------
// Rewrite the entire spikeDetection: block in the YAML file with a clean
// version containing only the new channelGroups.  Any keys previously
// present inside spikeDetection (stale groups, leftover metadata) are
// removed.  All other content in the file is preserved verbatim.
// ---------------------------------------------------------------------------
static void writeYamlGroups(const string& yamlPath,
                             const vector<ChannelGroup>& groups)
{
    // Read full file
    ifstream fin(yamlPath);
    if (!fin) { cerr << "cannot open " << yamlPath << "\n"; exit(1); }
    vector<string> lines;
    { string l; while (getline(fin, l)) lines.push_back(l); }
    fin.close();

    // Locate spikeDetection: line
    int sdLine = -1;
    for (int i = 0; i < (int)lines.size(); ++i) {
        if (ltrim(lines[i]).rfind("spikeDetection:", 0) == 0) { sdLine = i; break; }
    }
    if (sdLine < 0) { cerr << "spikeDetection not found in " << yamlPath << "\n"; exit(1); }

    // Determine indentation of spikeDetection (top-level = 0)
    int sdIndent = indent(lines[sdLine]);

    // Find the end of the entire spikeDetection block: first subsequent line
    // at indent <= sdIndent that is not blank and not a list continuation
    int sdEnd = sdLine + 1;
    while (sdEnd < (int)lines.size()) {
        const string& l = lines[sdEnd];
        if (l.empty() || l == "\r") { ++sdEnd; continue; }
        int ind = indent(l);
        if (ind <= sdIndent && !ltrim(l).empty() && ltrim(l)[0] != '-') break;
        ++sdEnd;
    }

    // Build the replacement spikeDetection block from scratch
    string sdInd(sdIndent, ' ');          // "spikeDetection:" indent (usually "")
    string cgInd  = sdInd  + "  ";        // "channelGroups:" indent
    string grpInd = cgInd  + "  ";        // "- channels:" indent
    string chInd  = grpInd + "  ";        // channel list item indent

    vector<string> newLines;
    newLines.push_back(sdInd + "spikeDetection:");
    newLines.push_back(cgInd + "channelGroups:");
    for (const ChannelGroup& g : groups) {
        newLines.push_back(grpInd + "- channels:");
        for (int ch : g.channels)
            newLines.push_back(chInd + "  - " + to_string(ch));
        newLines.push_back(grpInd + "  nSamples: "        + to_string(g.nSamples));
        newLines.push_back(grpInd + "  peakSampleIndex: " + to_string(g.peakSampleIndex));
        newLines.push_back(grpInd + "  nFeatures: "       + to_string(g.nFeatures));
    }

    // Assemble final file: before spikeDetection + replacement + after
    string tmpPath = yamlPath + ".spikegrouper_tmp";
    ofstream fout(tmpPath);
    if (!fout) { cerr << "cannot write " << tmpPath << "\n"; exit(1); }

    for (int i = 0; i < sdLine; ++i)      fout << lines[i] << "\n";
    for (const string& l : newLines)       fout << l        << "\n";
    for (int i = sdEnd; i < (int)lines.size(); ++i) fout << lines[i] << "\n";
    fout.close();

    // Atomic replace
    if (rename(tmpPath.c_str(), yamlPath.c_str()) != 0) {
        perror("rename");
        exit(1);
    }
}

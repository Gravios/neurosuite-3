// process_spikegrouper — Apple GPU (Metal) helpers.  Each returns false (and
// leaves its output alone) when there is no Metal device or the GPU work
// fails; the caller then computes the same thing on the CPU.
#ifndef PROCESS_SPIKEGROUPER_METAL_H
#define PROCESS_SPIKEGROUPER_METAL_H

#include <vector>

// median(|x|) of each channel chs[0..nv) of nChannels-interleaved data
// (nSamples frames), as the CPU's medianSigma takes it: the element of rank
// nSamples / 2 of |x| as an int.  medians[k] for chs[k].
bool metalAbsMedians(const short* data, long nSamples, int nChannels,
                     const int* chs, int nv, std::vector<int>& medians, bool verbose);

// counts[i * nv + j] = events of channel i with at least one event of channel
// j within +-window samples (i != j; 0 on the diagonal or when either list is
// empty).  Each events[c] is sorted.
bool metalCoincidenceCounts(const std::vector<std::vector<long>>& events, int window,
                            std::vector<long>& counts);

#endif

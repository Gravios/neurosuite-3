// realign_xcorr — Apple GPU (Metal) kernel, embedded by ns_metal_embed and
// compiled at run time (realign_xcorr_metal.cpp).
//
// The circular cross-correlation numerators of every (spike, lag), and each
// spike's energy, in exact 64-bit integer arithmetic: an int16 product fits
// in 32 bits and the sums in 64.  Apple GPUs have no double; the host turns
// these exact sums into scores with the OpenMP path's double arithmetic, so
// the shifts and scores are bit-identical to xcorr_omp_compute's.
#include <metal_stdlib>
using namespace metal;

// out[sp * (nLags + 1) + k]: k < nLags the numerator at lag k - maxShift,
// k == nLags the spike's energy sum(x^2).
kernel void xcorrNumerators(device const short* waveforms [[buffer(0)]],   // [nSpikes][nChan][nSamp]
                            device const short* tmpl      [[buffer(1)]],   // [nChan][nSamp]
                            constant int&       nChan     [[buffer(2)]],
                            constant int&       nSamp     [[buffer(3)]],
                            constant int&       maxShift  [[buffer(4)]],
                            constant int&       nSpikes   [[buffer(5)]],
                            device long*        out       [[buffer(6)]],
                            uint2 gid [[thread_position_in_grid]])
{
    const int k = int(gid.x), sp = int(gid.y);
    const int nLags = 2 * maxShift + 1;
    if (k > nLags || sp >= nSpikes) return;
    device const short* spk = waveforms + long(sp) * nChan * nSamp;
    long acc = 0;
    if (k == nLags) {
        for (int i = 0; i < nChan * nSamp; ++i) acc += long(int(spk[i]) * int(spk[i]));
    } else {
        const int lag = k - maxShift;
        for (int ch = 0; ch < nChan; ++ch) {
            device const short* tch = tmpl + ch * nSamp;
            device const short* sch = spk + ch * nSamp;
            for (int s = 0; s < nSamp; ++s) {
                const int sLag = ((s + lag) % nSamp + nSamp) % nSamp;
                acc += long(int(tch[s]) * int(sch[sLag]));
            }
        }
    }
    out[long(sp) * (nLags + 1) + k] = acc;
}

#include "spectralfft.h"

#include <cmath>
#include <complex>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <mutex>

#ifdef HAVE_FFTW
#include <fftw3.h>
#endif
#ifdef HAVE_VDSP
#include <Accelerate/Accelerate.h>
#endif

namespace neuroscope {
namespace spectral {

bool fftwAvailable()
{
#ifdef HAVE_FFTW
    return true;
#else
    return false;
#endif
}

bool vdspAvailable()
{
#ifdef HAVE_VDSP
    return true;
#else
    return false;
#endif
}

const char* fftBackendName(FftBackend backend)
{
    switch (backend) {
    case FftBackend::Auto:   return "auto";
    case FftBackend::Fftw:   return "fftw";
    case FftBackend::Vdsp:   return "vdsp";
    case FftBackend::Radix2: return "radix2";
    }
    return "?";
}

namespace {

// NS_SPECTRAL_FFT, or Auto when unset or not a provider name.
FftBackend environmentBackend()
{
    const char* v = std::getenv("NS_SPECTRAL_FFT");
    if (!v) return FftBackend::Auto;
    if (std::strcmp(v, "fftw") == 0)   return FftBackend::Fftw;
    if (std::strcmp(v, "vdsp") == 0)   return FftBackend::Vdsp;
    if (std::strcmp(v, "radix2") == 0) return FftBackend::Radix2;
    return FftBackend::Auto;
}

#ifdef HAVE_FFTW
// FFTW planner routines are not thread-safe; serialise plan create/destroy.
std::mutex& plannerMutex()
{
    static std::mutex m;
    return m;
}
#endif

#ifdef HAVE_VDSP
// vDSP's real DFT (vDSP_DFT_zrop_CreateSetupD + vDSP_DFT_ExecuteD), as
// Apple's vDSP Programming Guide ("Using DFT Functions") documents it:
//   - the setup Length is the number of real samples N;
//   - input: even samples in Ir, odd samples in Ii (N/2 each);
//   - output: Or[0] = H(0), Oi[0] = H(N/2) (both real), and
//     Or[k] + i*Oi[k] = H(k) for 1 <= k < N/2;
//   - the forward result is the mathematical DFT times 2.
// None of this can be compiled or run where the code was written (Linux), so
// each plan checks it at start-up against a direct DFT of a test signal, and
// takes the scale from that check rather than assuming 2. A plan whose check
// fails does not use vDSP (one message on stderr per process).
double vdspCheck(vDSP_DFT_SetupD setup, int n)
{
    const int half = n / 2;
    std::vector<double> x(static_cast<std::size_t>(n));
    std::uint32_t lcg = 12345u;
    for (double& v : x) {                       // 1 +/- 0.5: X(0) is about n
        lcg = lcg * 1664525u + 1013904223u;
        v = 1.0 + (static_cast<double>(lcg >> 8) / 16777216.0 - 0.5);
    }
    std::vector<double> ir(half), ii(half), outR(half), outI(half);
    for (int j = 0; j < half; ++j) {
        ir[j] = x[2 * j];
        ii[j] = x[2 * j + 1];
    }
    vDSP_DFT_ExecuteD(setup, ir.data(), ii.data(), outR.data(), outI.data());

    auto direct = [&](int k) {
        std::complex<double> s(0.0, 0.0);
        for (int t = 0; t < n; ++t) {
            const double a = -2.0 * kPi * static_cast<double>(k) * t / n;
            s += x[t] * std::complex<double>(std::cos(a), std::sin(a));
        }
        return s;
    };
    double magnitude = 0.0;
    for (double v : x) magnitude += std::abs(v);

    const double scale = outR[0] / direct(0).real();
    if (!std::isfinite(scale) || scale <= 0.0) return 0.0;
    const double tol = 1e-9 * magnitude * scale;
    if (std::abs(outI[0] - scale * direct(half).real()) > tol) return 0.0;
    for (int k : {1, half / 2, half - 1}) {
        if (k < 1) continue;
        const std::complex<double> want = scale * direct(k);
        if (std::abs(std::complex<double>(outR[k], outI[k]) - want) > tol) return 0.0;
    }
    return scale;
}

void reportVdspCheckFailure(int n)
{
    static std::once_flag once;
    std::call_once(once, [n] {
        std::fprintf(stderr,
                     "spectral: vDSP real DFT failed its self-check (nfft %d); "
                     "using another FFT provider\n", n);
    });
}
#endif

} // namespace

struct RealFftPlan::Impl {
#ifdef HAVE_FFTW
    fftw_plan fftw = nullptr;
#endif
#ifdef HAVE_VDSP
    // Apple does not document a DFT setup as safe to execute from several
    // threads at once, so each concurrent power() call leases its own: the
    // first is the plan's own setup, later ones are made from it (sharing its
    // tables) on demand and kept in the pool until the plan is destroyed.
    vDSP_DFT_SetupD vdsp = nullptr;
    double vdspScale = 2.0;
    std::mutex poolMutex;
    std::vector<vDSP_DFT_SetupD> idle;     // free for the next lease
    std::vector<vDSP_DFT_SetupD> extra;    // made from vdsp; destroyed first
    int vdspLength = 0;

    vDSP_DFT_SetupD lease()
    {
        std::lock_guard<std::mutex> lock(poolMutex);
        if (!idle.empty()) {
            vDSP_DFT_SetupD s = idle.back();
            idle.pop_back();
            return s;
        }
        vDSP_DFT_SetupD s = vDSP_DFT_zrop_CreateSetupD(vdsp, static_cast<vDSP_Length>(vdspLength),
                                                       vDSP_DFT_FORWARD);
        if (s) extra.push_back(s);
        return s;
    }

    void giveBack(vDSP_DFT_SetupD s)
    {
        std::lock_guard<std::mutex> lock(poolMutex);
        idle.push_back(s);
    }
#endif

    ~Impl()
    {
#ifdef HAVE_FFTW
        if (fftw) {
            std::lock_guard<std::mutex> lock(plannerMutex());
            fftw_destroy_plan(fftw);
        }
#endif
#ifdef HAVE_VDSP
        for (vDSP_DFT_SetupD s : extra) vDSP_DFT_DestroySetupD(s);
        if (vdsp) vDSP_DFT_DestroySetupD(vdsp);
#endif
    }
};

RealFftPlan::RealFftPlan(int requestedNfft, FftBackend preferred)
    : impl_(std::make_unique<Impl>())
{
    const int n = requestedNfft < 2 ? 2 : requestedNfft;
    const int pow2 = static_cast<int>(nextPowerOfTwo(static_cast<std::size_t>(n)));

    auto useFftw = [&](int size) {
#ifdef HAVE_FFTW
        // Plan with scratch buffers. FFTW_ESTIMATE does not touch the arrays
        // and is deterministic; FFTW_UNALIGNED lets power() use ordinary
        // per-thread std::vector storage with the thread-safe new-array execute.
        std::vector<double> in(static_cast<std::size_t>(size), 0.0);
        std::vector<std::complex<double>> out(static_cast<std::size_t>(size / 2 + 1));
        std::lock_guard<std::mutex> lock(plannerMutex());
        impl_->fftw = fftw_plan_dft_r2c_1d(size, in.data(),
                                           reinterpret_cast<fftw_complex*>(out.data()),
                                           FFTW_ESTIMATE | FFTW_UNALIGNED);
        if (!impl_->fftw) return false;
        nfft_ = size;
        backend_ = FftBackend::Fftw;
        return true;
#else
        (void)size;
        return false;
#endif
    };

    auto useVdsp = [&](int size) {
#ifdef HAVE_VDSP
        if (size % 2 != 0) return false;
        vDSP_DFT_SetupD s = vDSP_DFT_zrop_CreateSetupD(nullptr, static_cast<vDSP_Length>(size),
                                                       vDSP_DFT_FORWARD);
        if (!s) return false;                    // a length vDSP does not implement
        const double scale = vdspCheck(s, size);
        if (scale == 0.0) {
            vDSP_DFT_DestroySetupD(s);
            reportVdspCheckFailure(size);
            return false;
        }
        impl_->vdsp = s;
        impl_->vdspScale = scale;
        impl_->vdspLength = size;
        impl_->idle.push_back(s);
        nfft_ = size;
        backend_ = FftBackend::Vdsp;
        return true;
#else
        (void)size;
        return false;
#endif
    };

    auto useRadix2 = [&] {
        nfft_ = pow2;
        backend_ = FftBackend::Radix2;
    };

    const FftBackend want = preferred == FftBackend::Auto ? environmentBackend() : preferred;
    switch (want) {
    case FftBackend::Fftw:
        if (useFftw(n)) return;
        break;
    case FftBackend::Vdsp:
        if (useVdsp(n) || useVdsp(pow2)) return;
        break;
    case FftBackend::Radix2:
        useRadix2();
        return;
    case FftBackend::Auto:
        break;
    }
    if (useFftw(n) || useVdsp(n) || useVdsp(pow2)) return;
    useRadix2();
}

RealFftPlan::~RealFftPlan() = default;

void RealFftPlan::power(const double* in, std::vector<double>& outPower) const
{
    const int half = nfft_ / 2;
    outPower.resize(static_cast<std::size_t>(half + 1));

    switch (backend_) {
#ifdef HAVE_FFTW
    case FftBackend::Fftw: {
        // Per-thread scratch so concurrent calls on one plan don't collide.
        static thread_local std::vector<double> tin;
        static thread_local std::vector<std::complex<double>> tout;
        tin.assign(in, in + nfft_);
        tout.resize(static_cast<std::size_t>(half + 1));
        fftw_execute_dft_r2c(impl_->fftw, tin.data(),
                             reinterpret_cast<fftw_complex*>(tout.data()));
        for (int k = 0; k <= half; ++k) {
            const double re = tout[k].real();
            const double im = tout[k].imag();
            outPower[k] = re * re + im * im;
        }
        return;
    }
#endif
#ifdef HAVE_VDSP
    case FftBackend::Vdsp: {
        static thread_local std::vector<double> even, odd, re, im;
        even.resize(static_cast<std::size_t>(half));
        odd.resize(static_cast<std::size_t>(half));
        re.resize(static_cast<std::size_t>(half));
        im.resize(static_cast<std::size_t>(half));
        for (int j = 0; j < half; ++j) {
            even[j] = in[2 * j];
            odd[j] = in[2 * j + 1];
        }
        vDSP_DFT_SetupD s = impl_->lease();
        if (!s) {                                // out of memory for a new setup
            std::vector<double> seg(in, in + nfft_);
            rfftPowerOneSided(seg, outPower);
            return;
        }
        vDSP_DFT_ExecuteD(s, even.data(), odd.data(), re.data(), im.data());
        impl_->giveBack(s);
        const double inv = 1.0 / (impl_->vdspScale * impl_->vdspScale);
        outPower[0] = re[0] * re[0] * inv;       // DC
        outPower[half] = im[0] * im[0] * inv;    // Nyquist, packed in Oi[0]
        for (int k = 1; k < half; ++k)
            outPower[k] = (re[k] * re[k] + im[k] * im[k]) * inv;
        return;
    }
#endif
    default: {
        std::vector<double> seg(in, in + nfft_);
        rfftPowerOneSided(seg, outPower);
        return;
    }
    }
}

} // namespace spectral
} // namespace neuroscope

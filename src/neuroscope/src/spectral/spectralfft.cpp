#include "spectralfft.h"

#include <complex>
#include <mutex>

#ifdef HAVE_FFTW
#include <fftw3.h>
#endif

namespace neuroscope {
namespace spectral {

#ifdef HAVE_FFTW

bool fftwAvailable() { return true; }

namespace {
// FFTW planner routines are not thread-safe; serialise plan create/destroy.
std::mutex& plannerMutex()
{
    static std::mutex m;
    return m;
}
} // namespace

RealFftPlan::RealFftPlan(int requestedNfft)
{
    nfft_ = requestedNfft < 2 ? 2 : requestedNfft;

    // Plan with scratch buffers. FFTW_ESTIMATE does not touch the arrays and
    // is deterministic; FFTW_UNALIGNED lets power() use ordinary per-thread
    // std::vector storage with the thread-safe new-array execute.
    std::vector<double> in(nfft_, 0.0);
    std::vector<std::complex<double>> out(static_cast<std::size_t>(nfft_ / 2 + 1));
    std::lock_guard<std::mutex> lock(plannerMutex());
    fftw_plan p = fftw_plan_dft_r2c_1d(nfft_, in.data(),
                                       reinterpret_cast<fftw_complex*>(out.data()),
                                       FFTW_ESTIMATE | FFTW_UNALIGNED);
    impl_ = static_cast<void*>(p);
}

RealFftPlan::~RealFftPlan()
{
    if (impl_) {
        std::lock_guard<std::mutex> lock(plannerMutex());
        fftw_destroy_plan(static_cast<fftw_plan>(impl_));
    }
}

void RealFftPlan::power(const double* in, std::vector<double>& outPower) const
{
    const int half = nfft_ / 2;
    // Per-thread scratch so concurrent calls on one plan don't collide.
    static thread_local std::vector<double> tin;
    static thread_local std::vector<std::complex<double>> tout;
    tin.assign(in, in + nfft_);
    tout.resize(static_cast<std::size_t>(half + 1));

    fftw_execute_dft_r2c(static_cast<fftw_plan>(impl_), tin.data(),
                         reinterpret_cast<fftw_complex*>(tout.data()));

    outPower.resize(half + 1);
    for (int k = 0; k <= half; ++k) {
        const double re = tout[k].real();
        const double im = tout[k].imag();
        outPower[k] = re * re + im * im;
    }
}

#else // ---- no FFTW: radix-2 fallback (power-of-two size) -----------------

bool fftwAvailable() { return false; }

RealFftPlan::RealFftPlan(int requestedNfft)
{
    nfft_ = static_cast<int>(nextPowerOfTwo(static_cast<std::size_t>(requestedNfft < 2 ? 2 : requestedNfft)));
}

RealFftPlan::~RealFftPlan() = default;

void RealFftPlan::power(const double* in, std::vector<double>& outPower) const
{
    std::vector<double> seg(in, in + nfft_);
    rfftPowerOneSided(seg, outPower);
}

#endif

} // namespace spectral
} // namespace neuroscope

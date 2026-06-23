// Offscreen test of the SpectralImage -> QImage renderer. QImage pixel work
// does not require a QApplication. Exits non-zero on failure.

#include "spectralrender.h"
#include "spectralengine.h"

#include <QImage>
#include <cstdio>

using namespace neuroscope::spectral;

static int failures = 0;
static void check(bool ok, const char* name)
{
    std::printf(ok ? "  PASS  %s\n" : "  FAIL  %s\n", name);
    if (!ok) ++failures;
}

int main()
{
    // Build a 3-row (freq) x 4-col (time) mode-B image. Row 2 (top freq) is
    // bright; row 0 (bottom freq) is dim — so we can verify orientation.
    SpectralImage img;
    img.mode = SpectralMode::TimeFrequencySingleChannel;
    img.rows = 3; img.cols = 4;
    img.data.assign(img.rows * img.cols, 0.0f);
    for (int c = 0; c < img.cols; ++c) {
        img.data[0 * img.cols + c] = 1.0f;       // low freq, dim
        img.data[1 * img.cols + c] = 100.0f;
        img.data[2 * img.cols + c] = 10000.0f;   // high freq, bright
    }
    img.valueMin = 1.0; img.valueMax = 10000.0;
    img.freqs = {10.0, 20.0, 30.0};

    QImage out = spectralImageToQImage(img, 60.0, Colormap::Grayscale);
    check(!out.isNull(), "render produces an image");
    check(out.width() == img.cols && out.height() == img.rows, "image dimensions match");

    // Orientation: mode B puts low freq (row 0) at the bottom scanline.
    const int gradTop    = qGray(out.pixel(0, 0));               // high freq
    const int gradBottom = qGray(out.pixel(0, img.rows - 1));    // low freq
    check(gradTop > gradBottom, "mode B: high freq rendered at top (bright)");

    // Brightness tracks power (after dB scaling): brighter than dim row.
    check(qGray(out.pixel(0, 0)) > qGray(out.pixel(0, 1)), "brighter cell maps brighter");

    // Auto scale spans the full [min,max] range, so the middle-power row is
    // mid-gray rather than clipped, even with a narrow dB argument.
    QImage autoImg = spectralImageToQImage(img, 10.0, Colormap::Grayscale, /*autoScale*/true);
    const int autoMid = qGray(autoImg.pixel(0, 1)); // row 1 (power 100) -> scanline 1
    check(autoMid > 40 && autoMid < 215, "auto scale spans full range");
    // The same image under a fixed narrow 10 dB range clips that row darker.
    QImage manImg = spectralImageToQImage(img, 10.0, Colormap::Grayscale, /*autoScale*/false);
    check(qGray(manImg.pixel(0, 1)) < autoMid, "fixed narrow range clips below auto");

    // Mode A keeps row 0 at the top.
    SpectralImage a = img;
    a.mode = SpectralMode::FrequencyAcrossChannels;
    a.rowChannels = {0, 1, 2};
    QImage outA = spectralImageToQImage(a, 60.0, Colormap::Viridis);
    check(qGray(outA.pixel(0, 0)) < qGray(outA.pixel(0, a.rows - 1)),
          "mode A: row 0 (dim) at top");

    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASS",
                failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}

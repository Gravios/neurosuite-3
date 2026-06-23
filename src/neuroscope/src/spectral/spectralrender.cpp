#include "spectralrender.h"

#include <cmath>

namespace neuroscope {
namespace spectral {

QImage spectralImageToQImage(const SpectralImage& img,
                             double dynamicRangeDb,
                             Colormap cm,
                             bool autoScale)
{
    if (!img.valid()) return QImage();

    const double maxP = img.valueMax > 0.0 ? img.valueMax : 1.0;
    const double maxDb = 10.0 * std::log10(maxP);
    double floorDb;
    if (autoScale) {
        // Span the full observed range: floor at the image minimum (guarded
        // against a zero/degenerate minimum).
        const double minP = img.valueMin > 0.0 ? img.valueMin : maxP * 1e-8;
        floorDb = 10.0 * std::log10(minP);
        if (maxDb - floorDb < 1.0) floorDb = maxDb - 1.0;
    } else {
        const double rangeDb = dynamicRangeDb > 0.0 ? dynamicRangeDb : 60.0;
        floorDb = maxDb - rangeDb;
    }
    const double span = maxDb - floorDb; // > 0

    const bool freqUp = (img.mode == SpectralMode::TimeFrequencySingleChannel);

    QImage out(img.cols, img.rows, QImage::Format_RGB888);
    for (int r = 0; r < img.rows; ++r) {
        const int y = freqUp ? (img.rows - 1 - r) : r; // low freq at bottom
        uchar* line = out.scanLine(y);
        for (int c = 0; c < img.cols; ++c) {
            const double p = img.at(r, c);
            const double db = p > 0.0 ? 10.0 * std::log10(p) : floorDb;
            double t = (db - floorDb) / span;
            if (t < 0.0) t = 0.0; else if (t > 1.0) t = 1.0;
            std::uint8_t cr, cg, cb;
            colormapRgb(t, cm, cr, cg, cb);
            line[c * 3 + 0] = cr;
            line[c * 3 + 1] = cg;
            line[c * 3 + 2] = cb;
        }
    }
    return out;
}

} // namespace spectral
} // namespace neuroscope

#ifndef NEUROSCOPE_SPECTRAL_SPECTRALRENDER_H
#define NEUROSCOPE_SPECTRAL_SPECTRALRENDER_H

// Qt rendering of a computed SpectralImage to a QImage.
//
// Power is converted to decibels (10*log10), normalised to a dynamic range
// below the image maximum, and mapped through a colormap. Orientation follows
// the display convention: for the time x frequency mode low frequencies are at
// the bottom (image row 0 -> bottom); for the frequency-across-channels mode
// the first channel is at the top (image row 0 -> top). The returned QImage is
// cols x rows; the view scales it to the plot rectangle.

#include "colormap.h"
#include "spectralengine.h"

#include <QImage>

namespace neuroscope {
namespace spectral {

// dynamicRangeDb : dB below the per-image maximum mapped to the colormap
//                  bottom (e.g. 60). cm : colormap. Empty QImage if invalid.
QImage spectralImageToQImage(const SpectralImage& img,
                             double dynamicRangeDb,
                             Colormap cm);

} // namespace spectral
} // namespace neuroscope

#endif // NEUROSCOPE_SPECTRAL_SPECTRALRENDER_H

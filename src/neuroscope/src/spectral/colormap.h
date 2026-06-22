#ifndef NEUROSCOPE_SPECTRAL_COLORMAP_H
#define NEUROSCOPE_SPECTRAL_COLORMAP_H

// Qt-free colormaps for the spectral view. colormapRgb() maps a normalised
// value t in [0,1] (typically a dB-scaled power) to an 8-bit RGB triple, by
// piecewise-linear interpolation between a handful of anchor colours. Kept
// Qt-free so the mapping is unit-tested directly; the view assembles the
// QImage from it.

#include <algorithm>
#include <cstdint>

namespace neuroscope {
namespace spectral {

enum class Colormap : int {
    Grayscale = 0,
    Viridis   = 1,
    Inferno   = 2
};

namespace detail {

struct Anchor { double t; std::uint8_t r, g, b; };

// Anchor tables (perceptually-ordered approximations; monotone in luminance).
inline const Anchor* viridisAnchors(int& n)
{
    static const Anchor a[] = {
        {0.0,  68,   1,  84}, {0.25,  59,  82, 139}, {0.5,  33, 145, 140},
        {0.75, 94, 201,  98}, {1.0, 253, 231,  37}
    };
    n = 5; return a;
}
inline const Anchor* infernoAnchors(int& n)
{
    static const Anchor a[] = {
        {0.0,   0,   0,   4}, {0.25, 87,  16, 110}, {0.5, 188,  55,  84},
        {0.75, 249, 142,   9}, {1.0, 252, 255, 164}
    };
    n = 5; return a;
}

inline void interp(const Anchor* a, int n, double t,
                   std::uint8_t& r, std::uint8_t& g, std::uint8_t& b)
{
    if (t <= a[0].t)     { r = a[0].r;     g = a[0].g;     b = a[0].b;     return; }
    if (t >= a[n - 1].t) { r = a[n - 1].r; g = a[n - 1].g; b = a[n - 1].b; return; }
    int i = 0;
    while (i < n - 1 && t > a[i + 1].t) ++i;
    const double span = a[i + 1].t - a[i].t;
    const double f = span > 0 ? (t - a[i].t) / span : 0.0;
    r = static_cast<std::uint8_t>(a[i].r + f * (a[i + 1].r - a[i].r) + 0.5);
    g = static_cast<std::uint8_t>(a[i].g + f * (a[i + 1].g - a[i].g) + 0.5);
    b = static_cast<std::uint8_t>(a[i].b + f * (a[i + 1].b - a[i].b) + 0.5);
}

} // namespace detail

// Map t in [0,1] to an RGB triple for the chosen colormap.
inline void colormapRgb(double t, Colormap cm,
                        std::uint8_t& r, std::uint8_t& g, std::uint8_t& b)
{
    t = std::clamp(t, 0.0, 1.0);
    switch (cm) {
    case Colormap::Grayscale: {
        const auto v = static_cast<std::uint8_t>(t * 255.0 + 0.5);
        r = g = b = v;
        return;
    }
    case Colormap::Viridis: { int n; const auto* a = detail::viridisAnchors(n); detail::interp(a, n, t, r, g, b); return; }
    case Colormap::Inferno: { int n; const auto* a = detail::infernoAnchors(n); detail::interp(a, n, t, r, g, b); return; }
    }
    r = g = b = 0;
}

} // namespace spectral
} // namespace neuroscope

#endif // NEUROSCOPE_SPECTRAL_COLORMAP_H

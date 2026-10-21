# Icons

One family: a rounded navy tile, and a cyan recording trace that runs off both
edges of the tile, with one extracellular spike in it (a small bump, a deep
trough, a short low rebound).  Each icon puts its own motif above the trace.

| Icon | Motif | Source |
|---|---|---|
| neurosuite-3 | "NS3" (N cyan, S white, 3 orange) | `doc/images/neurosuite-3.svg` |
| Klusters | three clusters of points, as in its feature-space view | `src/klusters/src/app-klusters.svg` |
| NeuroScope | three stacked channel traces with the spike lined up across them | `src/neuroscope/src/app-neuroscope.svg` |
| NDManager | three processing steps in a row | `src/ndmanager/src/app-ndmanager.svg` |

Palette:

| Role | Colour |
|---|---|
| tile (gradient, top-left → bottom-right) | `#0E2236` → `#173F5C` |
| cyan | `#43C8E2` |
| white | `#EEF4F8` |
| orange | `#FF8F45` |

The SVGs are 512 × 512, drawn with strokes and shapes only (no fonts), so they
look the same everywhere.  They are the sources; everything else is rendered
from them, with a transparent background outside the tile:

- `hi<size>-app-<name>.png` beside each SVG, 16, 22, 32, 48, 64, 128 and
  256 px: installed into the hicolor icon theme (with the SVG as the scalable
  icon) and compiled into the program as its window icon;
- `src/ndmanager/src/icons/klusters-22.png`, `neuroscope-22.png` and
  `neuroscope.png` (32 px): NDManager's icons for the other two programs;
- `doc/images/social-preview.png` (1280 × 640): GitHub's link preview, uploaded
  by hand under the repository's Settings → General → Social preview.

The committed PNGs were rendered with Chromium; any SVG renderer will do.
After changing an SVG, render every size again and look at the 16 px one:
below 32 px only the motif's colours survive.

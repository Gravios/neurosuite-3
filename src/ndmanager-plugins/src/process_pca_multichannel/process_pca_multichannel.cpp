/***************************************************************************
 *  process_pca_multichannel.cpp
 *
 *  See process_pca_multichannel.h for design notes.
 ***************************************************************************/
#define _LARGEFILE_SOURCE64
#define _FILE_OFFSET_BITS 64

#include "process_pca_multichannel.h"
#include "progressbar.h"

#include <gsl/gsl_eigen.h>
#include <gsl/gsl_blas.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#ifdef _OPENMP
#  include <omp.h>
#endif

using std::cerr;
using std::cout;
using std::endl;

static bool verbose = false;

// ════════════════════════════════════════════════════════════════════════════
// ── Probe geometry catalog ──────────────────────────────────────────────────
// ════════════════════════════════════════════════════════════════════════════
//
// A ProbeGeometry is just (nChan, posX[nChan], posY[nChan], name).  All
// positions in μm.  Names match the --probe-template CLI strings exactly.

struct ProbeGeometry {
    int                 nChan = 0;
    std::vector<double> posX, posY;
    std::string         name;
};

// Tetrode: 4 channels in a 2×2 square at 25 μm pitch.  This matches the
// common silicon tetrode layout (NeuroNexus A1x4-tet, etc.).
static ProbeGeometry tetrode_template() {
    ProbeGeometry g;
    g.nChan = 4;
    g.posX  = { 0.0, 25.0,  0.0, 25.0 };
    g.posY  = { 0.0,  0.0, 25.0, 25.0 };
    g.name  = "tetrode";
    return g;
}

// Octrode (linear): 8 channels stacked vertically at 20 μm pitch.  This is
// the standard Buzsaki silicon shank ("8-site") layout — one column of 8.
static ProbeGeometry octrode_linear_template() {
    ProbeGeometry g;
    g.nChan = 8;
    g.posX.assign(8, 0.0);
    g.posY.resize(8);
    for (int i = 0; i < 8; ++i) g.posY[i] = i * 20.0;
    g.name = "octrode-linear";
    return g;
}

// Octrode (2×4): 8 channels in two columns of four, 20 μm horiz × 25 μm vert.
// Some Buzsaki-lab silicon shanks have this layout (the "stereotrode-style"
// octrode).
static ProbeGeometry octrode_2x4_template() {
    ProbeGeometry g;
    g.nChan = 8;
    g.posX.resize(8);
    g.posY.resize(8);
    for (int i = 0; i < 8; ++i) {
        g.posX[i] = (i % 2) * 20.0;
        g.posY[i] = (i / 2) * 25.0;
    }
    g.name = "octrode-2x4";
    return g;
}

// Neuropixels 1.0 channel slice.  The full probe has 384 recording sites
// arranged in a 4-column staggered checkerboard pattern: even rows have
// sites at columns 0 and 2 (x = 11, 43 μm), odd rows at columns 1 and 3
// (x = 27, 59 μm).  Vertical pitch is 20 μm between adjacent rows.
//
// For a spike-group of N channels (typically a contiguous slice along the
// shank), we lay out the channels in the same checkerboard pattern, with
// channel 0 at row 0 column 0.
//
// Reference: Jun et al. 2017, Nature 551:232–236.  Site coordinates from
// the IMRO table.
static ProbeGeometry neuropixels1_template(int nChan) {
    ProbeGeometry g;
    g.nChan = nChan;
    g.posX.resize(nChan);
    g.posY.resize(nChan);

    // Four x-coordinates, two per row.  Site index → column lookup:
    //   even sites (within a 4-site row): cols 0, 2  →  x = 11, 43
    //   odd  sites                       : cols 1, 3  →  x = 27, 59
    // Actually NP1.0 alternates per ROW, not per site.  Even rows: x = 11, 43.
    // Odd rows: x = 27, 59.  Two sites per row.
    static const double col_x[4] = { 11.0, 43.0, 27.0, 59.0 };

    for (int i = 0; i < nChan; ++i) {
        const int row     = i / 2;        // 2 sites per row
        const int siteInRow = i % 2;       // 0 = first site in row, 1 = second
        const int colIdx  = (row % 2 == 0) ? siteInRow            // even row: 0, 1
                                           : siteInRow + 2;       // odd row : 2, 3
        g.posX[i] = col_x[colIdx];
        g.posY[i] = row * 20.0;
    }
    std::ostringstream os; os << "neuropixels1-" << nChan;
    g.name = os.str();
    return g;
}

// Neuropixels 2.0 channel slice.  Two columns, 32 μm apart horizontally,
// 15 μm vertical pitch.  Each row has 2 sites.
//
// Reference: Steinmetz et al. 2021, Science 372:eabf4588.
static ProbeGeometry neuropixels2_template(int nChan) {
    ProbeGeometry g;
    g.nChan = nChan;
    g.posX.resize(nChan);
    g.posY.resize(nChan);
    for (int i = 0; i < nChan; ++i) {
        const int row       = i / 2;
        const int siteInRow = i % 2;
        g.posX[i] = siteInRow * 32.0;
        g.posY[i] = row * 15.0;
    }
    std::ostringstream os; os << "neuropixels2-" << nChan;
    g.name = os.str();
    return g;
}

// Generic linear shank at any pitch.  Single column, vertical stack.
static ProbeGeometry flat_linear_template(int nChan, double pitch_um) {
    ProbeGeometry g;
    g.nChan = nChan;
    g.posX.assign(nChan, 0.0);
    g.posY.resize(nChan);
    for (int i = 0; i < nChan; ++i) g.posY[i] = i * pitch_um;
    std::ostringstream os; os << "flat-linear-" << pitch_um << "um";
    g.name = os.str();
    return g;
}

// Load geometry from a 2-column text file.  Same parser as the drifttracker
// patch51 — comments via '#', trailing tokens are errors, real line numbers
// in diagnostics.
static bool load_geometry_file(const std::string& path, int nChan, ProbeGeometry &g) {
    std::ifstream f(path);
    if (!f) {
        cerr << "ERROR: cannot open --channel-positions '" << path << "'\n";
        return false;
    }
    g.nChan = nChan;
    g.posX.assign(nChan, 0.0);
    g.posY.assign(nChan, 0.0);
    g.name = path;

    int  idx    = 0;
    int  lineno = 0;
    std::string line;
    while (std::getline(f, line)) {
        ++lineno;
        size_t a = 0;
        while (a < line.size() && (line[a] == ' ' || line[a] == '\t')) ++a;
        if (a >= line.size() || line[a] == '#') continue;
        double x, y;
        int    consumed = 0;
        const int n = std::sscanf(line.c_str() + a, "%lf %lf%n", &x, &y, &consumed);
        if (n != 2) {
            cerr << "ERROR: --channel-positions '" << path << "' line " << lineno
                 << ": expected 'x y', got '" << (line.c_str() + a) << "'\n";
            return false;
        }
        size_t tail = a + consumed;
        while (tail < line.size() && (line[tail] == ' ' || line[tail] == '\t')) ++tail;
        if (tail < line.size() && line[tail] != '#') {
            cerr << "ERROR: --channel-positions '" << path << "' line " << lineno
                 << ": trailing token after 'x y' — got '" << (line.c_str() + tail) << "'\n";
            return false;
        }
        if (idx >= nChan) {
            cerr << "ERROR: --channel-positions '" << path << "' line " << lineno
                 << ": more data rows than the group's nChan=" << nChan << "\n";
            return false;
        }
        g.posX[idx] = x;
        g.posY[idx] = y;
        ++idx;
    }
    if (idx != nChan) {
        cerr << "ERROR: --channel-positions '" << path << "' has " << idx
             << " data row(s), expected nChan=" << nChan << "\n";
        return false;
    }
    return true;
}

// Resolve the abstract ProbeTemplate + arguments to a concrete geometry.
// Returns true on success.  Fills `out_g` with the resolved geometry and
// `out_g.name` with the resolved template name (useful for the .pcaM header).
static bool resolve_geometry(const Arguments& args, ProbeGeometry &out_g) {
    // Explicit positions file always wins.
    if (!args.channelPositionsFile.empty())
        return load_geometry_file(args.channelPositionsFile, args.nChannels, out_g);

    // Auto-select if no template specified.
    ProbeTemplate which = args.probe;
    if (which == ProbeTemplate::AUTO) {
        if      (args.nChannels == 4) which = ProbeTemplate::TETRODE;
        else if (args.nChannels == 8) which = ProbeTemplate::OCTRODE_LINEAR;
        else if (args.nChannels <= 16) which = ProbeTemplate::FLAT_LINEAR;
        else                          which = ProbeTemplate::NEUROPIXELS1;
        if (verbose)
            cout << "  Auto-selected probe template for nChan=" << args.nChannels
                 << ": template index " << static_cast<int>(which) << endl;
    }

    switch (which) {
        case ProbeTemplate::TETRODE:
            if (args.nChannels != 4) {
                cerr << "ERROR: tetrode template requires nChan=4, got " << args.nChannels << "\n";
                return false;
            }
            out_g = tetrode_template();
            return true;

        case ProbeTemplate::OCTRODE_LINEAR:
            if (args.nChannels != 8) {
                cerr << "ERROR: octrode-linear template requires nChan=8, got " << args.nChannels << "\n";
                return false;
            }
            out_g = octrode_linear_template();
            return true;

        case ProbeTemplate::OCTRODE_2X4:
            if (args.nChannels != 8) {
                cerr << "ERROR: octrode-2x4 template requires nChan=8, got " << args.nChannels << "\n";
                return false;
            }
            out_g = octrode_2x4_template();
            return true;

        case ProbeTemplate::NEUROPIXELS1:
            if (args.nChannels < 2 || args.nChannels > 384) {
                cerr << "ERROR: neuropixels1 template needs 2 ≤ nChan ≤ 384, got " << args.nChannels << "\n";
                return false;
            }
            out_g = neuropixels1_template(args.nChannels);
            return true;

        case ProbeTemplate::NEUROPIXELS2:
            if (args.nChannels < 2 || args.nChannels > 384) {
                cerr << "ERROR: neuropixels2 template needs 2 ≤ nChan ≤ 384, got " << args.nChannels << "\n";
                return false;
            }
            out_g = neuropixels2_template(args.nChannels);
            return true;

        case ProbeTemplate::FLAT_LINEAR:
            out_g = flat_linear_template(args.nChannels, args.flatLinearPitch);
            return true;

        case ProbeTemplate::USER_FILE:
        case ProbeTemplate::AUTO:
            cerr << "ERROR: internal: unresolved probe template\n";
            return false;
    }
    return false;
}

// ════════════════════════════════════════════════════════════════════════════
// ── Spatial weight matrix ────────────────────────────────────────────────────
// ════════════════════════════════════════════════════════════════════════════
//
// When spatialWeightRadius > 0, build a (nChan × nChan) weight matrix
// W_ij = exp(-||p_i - p_j||² / (2 r²)).  This downweights covariance
// entries between distant channels; for Neuropixels groups with mostly-
// noise channels at the periphery, focuses PCA on the spike-bearing core.
//
// The weight matrix is then "Kronecker-expanded" to (nChan*nSamp ×
// nChan*nSamp) via W_full[ch_i*nSamp+s, ch_j*nSamp+t] = W_ij (i.e., same
// weight for every (s, t) pair within the (ch_i, ch_j) block), and applied
// elementwise to the covariance:  C'_full = W_full ⊙ C_full.
//
// Returns true if weighting is enabled; out_W is nChan × nChan.
static bool build_spatial_weights(double radius_um, const ProbeGeometry &g,
                                  std::vector<double> &out_W) {
    out_W.clear();
    if (!(radius_um > 0.0)) return false;
    const int n = g.nChan;
    out_W.resize((size_t)n * n);
    const double two_r2 = 2.0 * radius_um * radius_um;
    for (int i = 0; i < n; ++i) {
        for (int j = 0; j < n; ++j) {
            const double dx = g.posX[i] - g.posX[j];
            const double dy = g.posY[i] - g.posY[j];
            const double d2 = dx * dx + dy * dy;
            out_W[(size_t)i * n + j] = std::exp(-d2 / two_r2);
        }
    }
    return true;
}

// ════════════════════════════════════════════════════════════════════════════
// ── Varimax rotation (lifted from process_pca patch52) ──────────────────────
// ════════════════════════════════════════════════════════════════════════════
//
// In-place orthogonal rotation of a (p × k) loadings matrix L that
// maximises Σ_j [Σ_i L⁴_ij − (Σ_i L²_ij)²/p].  Returns the number of
// sweeps performed.  See patch52 README for full discussion.

static int applyVarimaxRotation(gsl_matrix *L, int maxIter, double tol) {
    const size_t p = L->size1;
    const size_t k = L->size2;
    if (k < 2) return 0;

    auto criterion = [&]() {
        double V = 0.0;
        for (size_t j = 0; j < k; ++j) {
            double s2 = 0.0, s4 = 0.0;
            for (size_t i = 0; i < p; ++i) {
                const double v  = gsl_matrix_get(L, i, j);
                const double v2 = v * v;
                s2 += v2; s4 += v2 * v2;
            }
            V += s4 - (s2 * s2) / static_cast<double>(p);
        }
        return V;
    };

    double prevV = criterion();
    int    iter  = 0;
    for (iter = 1; iter <= maxIter; ++iter) {
        for (size_t j = 0; j + 1 < k; ++j) {
            for (size_t kk = j + 1; kk < k; ++kk) {
                double sU = 0.0, sV = 0.0, sUU_VV = 0.0, sUV = 0.0;
                for (size_t i = 0; i < p; ++i) {
                    const double a = gsl_matrix_get(L, i, j);
                    const double b = gsl_matrix_get(L, i, kk);
                    const double u = a * a - b * b;
                    const double v = 2.0 * a * b;
                    sU += u; sV += v;
                    sUU_VV += u * u - v * v;
                    sUV    += u * v;
                }
                const double pD  = static_cast<double>(p);
                const double num = 2.0 * (pD * sUV - sU * sV);
                const double den = pD * sUU_VV - (sU * sU - sV * sV);
                if (num == 0.0 && den == 0.0) continue;
                const double theta = 0.25 * std::atan2(num, den);
                if (std::fabs(theta) < 1e-12) continue;
                const double c = std::cos(theta);
                const double s = std::sin(theta);
                for (size_t i = 0; i < p; ++i) {
                    const double a = gsl_matrix_get(L, i, j);
                    const double b = gsl_matrix_get(L, i, kk);
                    gsl_matrix_set(L, i, j,   c * a + s * b);
                    gsl_matrix_set(L, i, kk, -s * a + c * b);
                }
            }
        }
        const double V   = criterion();
        const double rel = (prevV > 0.0) ? std::fabs(V - prevV) / prevV
                                         : std::fabs(V - prevV);
        if (rel < tol) break;
        prevV = V;
    }

    // Re-sort columns by ||column||² descending.
    {
        std::vector<std::pair<double, size_t>> norms(k);
        for (size_t j = 0; j < k; ++j) {
            double s = 0.0;
            for (size_t i = 0; i < p; ++i) {
                const double v = gsl_matrix_get(L, i, j);
                s += v * v;
            }
            norms[j] = {s, j};
        }
        std::sort(norms.begin(), norms.end(),
                  [](const auto& x, const auto& y) { return x.first > y.first; });
        gsl_matrix *tmp = gsl_matrix_alloc(p, k);
        gsl_matrix_memcpy(tmp, L);
        for (size_t j = 0; j < k; ++j) {
            const size_t src = norms[j].second;
            for (size_t i = 0; i < p; ++i)
                gsl_matrix_set(L, i, j, gsl_matrix_get(tmp, i, src));
        }
        gsl_matrix_free(tmp);
    }
    return iter;
}

// ════════════════════════════════════════════════════════════════════════════
// ── CLI parsing ─────────────────────────────────────────────────────────────
// ════════════════════════════════════════════════════════════════════════════

static void error_usage(const char *prog) {
    cerr << "error: invalid arguments.  Try '" << prog << " -h' for usage.\n";
    exit(1);
}

static void help(const char* name) {
    cout
    << name << ": multi-channel PCA for spike feature extraction with probe-geometry awareness.\n"
    << "usage: " << name << " [options] input\n"
    << "       " << name << " [options] -    (read from stdin)\n"
    << "\n"
    << " input             input .spk filename (omit/use '-' for stdin)\n"
    << " -f output         output .fetM filename\n"
    << " -n channels       number of channels in the spike group\n"
    << " -w length         samples per waveform\n"
    << " -d components     number of principal components to keep (single block, NOT per channel)\n"
    << " -p position       peak position within the waveform (optional)\n"
    << " -b length         samples before peak to use (optional; default = full waveform)\n"
    << " -a length         samples after peak to use (optional)\n"
    << " -s size           input size in bytes (required when reading stdin)\n"
    << " -c                use centred data for projection\n"
    << " -g group          electrode-group number for the progress bar label\n"
    << " -t threads        OpenMP thread count (only affects multi-spike accumulation)\n"
    << "\n"
    << "Probe geometry:\n"
    << " --probe-template NAME    one of:\n"
    << "                            tetrode             4 ch square @ 25 μm\n"
    << "                            octrode-linear      8 ch line @ 20 μm  (Buzsaki)\n"
    << "                            octrode-2x4         8 ch in 2×4 grid\n"
    << "                            neuropixels1        N ch slice of NP1.0\n"
    << "                            neuropixels2        N ch slice of NP2.0\n"
    << "                            flat-linear         N ch line @ --flat-linear-pitch μm\n"
    << " --flat-linear-pitch UM   pitch for flat-linear template (default 20)\n"
    << " --channel-positions FILE 2-column 'x y' (μm) per channel; overrides --probe-template\n"
    << "\n"
    << "Spatial covariance weighting (for large groups with mostly-noise periphery):\n"
    << " --spatial-weight RADIUS  Gaussian downweight of channel pairs at distance > 2 R (μm)\n"
    << "                          default 0 (off, pure multi-channel PCA)\n"
    << "\n"
    << "Varimax rotation (mirrors process_pca patch52):\n"
    << " --varimax                rotate kept-K basis for sparser loadings\n"
    << " --varimax-max-iter N     default 30\n"
    << " --varimax-tol T          default 1e-6\n"
    << "\n"
    << " -v                       verbose mode\n"
    << " -h                       display help\n"
    << "\n"
    << "Output: .fetM.<grp> (int32 nFeat + nSpikes×nFeat int64) and .pcaM.<grp>\n"
    << "(see process_pca_multichannel.h for the binary header layout).\n";
    exit(0);
}

void parseArgs(int argc, char **argv, Arguments &args) {
    int nOptions = argc;
    int i;
    if (argc == 2 && (!std::strcmp(argv[1], "-h") || !std::strcmp(argv[1], "--help"))) help(argv[0]);
    if (nOptions < 2) error_usage(argv[0]);

    for (i = 1; i < nOptions; ++i) {
        if (argv[i][0] != '-') break;
        if (std::strlen(argv[i]) < 2) error_usage(argv[0]);

        // Long options first.
        if (!std::strcmp(argv[i], "--probe-template")) {
            if (i + 1 >= nOptions) error_usage(argv[0]);
            const std::string v(argv[++i]);
            args.probeName = v;
            if      (v == "tetrode")         args.probe = ProbeTemplate::TETRODE;
            else if (v == "octrode-linear")  args.probe = ProbeTemplate::OCTRODE_LINEAR;
            else if (v == "octrode-2x4")     args.probe = ProbeTemplate::OCTRODE_2X4;
            else if (v == "neuropixels1")    args.probe = ProbeTemplate::NEUROPIXELS1;
            else if (v == "neuropixels2")    args.probe = ProbeTemplate::NEUROPIXELS2;
            else if (v == "flat-linear")     args.probe = ProbeTemplate::FLAT_LINEAR;
            else {
                cerr << "error: --probe-template: unknown name '" << v << "'.  "
                     << "See -h for valid choices.\n";
                exit(1);
            }
            continue;
        }
        if (!std::strcmp(argv[i], "--flat-linear-pitch")) {
            if (i + 1 >= nOptions) error_usage(argv[0]);
            args.flatLinearPitch = std::atof(argv[++i]);
            if (!(args.flatLinearPitch > 0.0)) {
                cerr << "error: --flat-linear-pitch must be > 0\n"; exit(1);
            }
            continue;
        }
        if (!std::strcmp(argv[i], "--channel-positions")) {
            if (i + 1 >= nOptions) error_usage(argv[0]);
            args.channelPositionsFile = argv[++i];
            args.probe = ProbeTemplate::USER_FILE;
            continue;
        }
        if (!std::strcmp(argv[i], "--spatial-weight")) {
            if (i + 1 >= nOptions) error_usage(argv[0]);
            args.spatialWeightRadius = std::atof(argv[++i]);
            if (args.spatialWeightRadius < 0.0) {
                cerr << "error: --spatial-weight must be >= 0\n"; exit(1);
            }
            continue;
        }
        if (!std::strcmp(argv[i], "--varimax")) {
            args.varimax = true;
            continue;
        }
        if (!std::strcmp(argv[i], "--varimax-max-iter")) {
            if (i + 1 >= nOptions) error_usage(argv[0]);
            args.varimaxMaxIter = std::atoi(argv[++i]);
            if (args.varimaxMaxIter < 1) { cerr << "error: --varimax-max-iter must be >= 1\n"; exit(1); }
            continue;
        }
        if (!std::strcmp(argv[i], "--varimax-tol")) {
            if (i + 1 >= nOptions) error_usage(argv[0]);
            args.varimaxTol = std::atof(argv[++i]);
            if (!(args.varimaxTol > 0.0)) { cerr << "error: --varimax-tol must be > 0\n"; exit(1); }
            continue;
        }

        // Short options.
        switch (argv[i][1]) {
            case 's': if (i + 1 >= nOptions) error_usage(argv[0]);
                      args.inputSize = std::atoll(argv[++i]); args.isInputSizeProvided = true; break;
            case 'n': if (i + 1 >= nOptions) error_usage(argv[0]);
                      args.nChannels = std::atoi(argv[++i]); args.isNChannelsProvided = true; break;
            case 'b': if (i + 1 >= nOptions) error_usage(argv[0]);
                      args.beforeSpike = std::atoi(argv[++i]); args.isBeforeSpikeProvided = true; break;
            case 'p': if (i + 1 >= nOptions) error_usage(argv[0]);
                      args.peakPosition = std::atoi(argv[++i]); args.isPeakPositionProvided = true; break;
            case 'a': if (i + 1 >= nOptions) error_usage(argv[0]);
                      args.afterSpike = std::atoi(argv[++i]); args.isAfterSpikeProvided = true; break;
            case 'w': if (i + 1 >= nOptions) error_usage(argv[0]);
                      args.spikeLength = std::atoi(argv[++i]); args.isSpikeLengthProvided = true; break;
            case 'd': if (i + 1 >= nOptions) error_usage(argv[0]);
                      args.nComponents = std::atoi(argv[++i]); args.isNComponentsProvided = true; break;
            case 'f': if (i + 1 >= nOptions) error_usage(argv[0]);
                      args.outputFileName = argv[++i]; args.isOutputFileProvided = true; break;
            case 'c': args.isCenteredData = true; break;
            case 'g': if (i + 1 >= nOptions) error_usage(argv[0]);
                      args.electrodeGroup = std::atoi(argv[++i]); break;
            case 't':
#ifdef _OPENMP
                      if (i + 1 >= nOptions) error_usage(argv[0]);
                      omp_set_num_threads(std::atoi(argv[++i]));
#else
                      ++i;
                      cerr << "warning: -t ignored (not compiled with OpenMP)\n";
#endif
                      break;
            case 'v': verbose = true; break;
            case 'h': help(argv[0]); break;
            default:
                cerr << "error: unknown option '" << argv[i] << "'.\n"; exit(1);
        }
    }

    // Input file.
    if (i >= argc) { cerr << "error: missing input file (use '-' for stdin)\n"; exit(1); }
    if (!std::strcmp(argv[i], "-")) args.isInputFileProvided = false;
    else {
        args.inputFileName = argv[i];
        args.isInputFileProvided = true;
    }
}

bool checkInputs(const Arguments &args) {
    if (!args.isNChannelsProvided   || args.nChannels   <= 0) { cerr << "error: -n required and > 0\n"; return false; }
    if (!args.isSpikeLengthProvided || args.spikeLength <= 0) { cerr << "error: -w required and > 0\n"; return false; }
    if (!args.isNComponentsProvided || args.nComponents <= 0) { cerr << "error: -d required and > 0\n"; return false; }
    if (!args.isOutputFileProvided)                           { cerr << "error: -f required\n"; return false; }
    if (!args.isInputFileProvided && !args.isInputSizeProvided) {
        cerr << "error: -s required when reading from stdin\n"; return false;
    }
    return true;
}

// ════════════════════════════════════════════════════════════════════════════
// ── Multi-channel PCA driver ────────────────────────────────────────────────
// ════════════════════════════════════════════════════════════════════════════

int main(int argc, char *argv[]) {
    Arguments args;
    parseArgs(argc, argv, args);
    if (!checkInputs(args)) return 1;

    // ── Determine effective sample window ──
    int recShift = 0;
    int data2use = args.spikeLength;
    if (args.isPeakPositionProvided && args.isBeforeSpikeProvided && args.isAfterSpikeProvided) {
        recShift = args.peakPosition - args.beforeSpike;
        data2use = args.beforeSpike + args.afterSpike + 1;
        if (recShift < 0 || recShift + data2use > args.spikeLength) {
            cerr << "error: -b/-a/-p define a window outside [0, spikeLength)\n";
            return 1;
        }
    }

    // ── Open input ──
    FILE *fin = args.isInputFileProvided ? std::fopen(args.inputFileName, "rb") : stdin;
    if (!fin) {
        cerr << "error: cannot open input '" << args.inputFileName << "'\n";
        return 1;
    }
    long long nBytes;
    if (args.isInputFileProvided) {
        std::fseek(fin, 0, SEEK_END);
        nBytes = std::ftell(fin);
        std::fseek(fin, 0, SEEK_SET);
    } else {
        nBytes = args.inputSize;
    }
    const long long nRecords  = nBytes / RECORD_BYTE_SIZE;
    const long long perSpike  = (long long)args.nChannels * args.spikeLength;
    if (nRecords % perSpike != 0) {
        cerr << "error: input size " << nBytes << " bytes is not a multiple of "
             << "nChan*nSamp*2 = " << (perSpike * RECORD_BYTE_SIZE) << "\n";
        return 1;
    }
    const long long nSpikes = nRecords / perSpike;
    if (nSpikes == 0) { cerr << "error: no complete spikes in input\n"; return 1; }

    // ── Resolve probe geometry ──
    ProbeGeometry geom;
    if (!resolve_geometry(args, geom)) return 1;
    if (verbose)
        cout << "Probe geometry: " << geom.name << " (" << geom.nChan << " channels)" << endl;

    // ── Allocate and read raw spike data ──
    if (verbose) {
        cout << "Reading " << nSpikes << " spikes "
             << "(" << args.nChannels << " ch × " << args.spikeLength << " samp)" << endl;
        if (data2use != args.spikeLength)
            cout << "  using window: [" << recShift << ", " << (recShift + data2use) << ")"
                 << " (" << data2use << " samples per channel)" << endl;
    }
    std::vector<short> rawData((size_t)nRecords);
    const size_t got = std::fread(rawData.data(), sizeof(short), nRecords, fin);
    if ((long long)got != nRecords) {
        cerr << "error: read " << got << " of " << nRecords << " records\n";
        return 1;
    }
    if (args.isInputFileProvided) std::fclose(fin);

    ProgressBar pb("", "PCAm", 6);
    if (args.electrodeGroup >= 0) {
        std::ostringstream os; os << "PCAm-" << args.electrodeGroup;
        pb = ProgressBar("", os.str(), 6);
    }
    pb.start();

    // ── Step 1: build the data matrix X of size (nSpikes × dim) where ──
    // ── dim = nChan * data2use.  Indexing: within a spike, layout is   ──
    // ── time-major channel-minor: x[t*nChan + ch].  We flatten to       ──
    // ── X[spike, ch*data2use + sampleIdx]  — channel-major in the       ──
    // ── feature axis so per-channel sub-blocks are contiguous, useful   ──
    // ── for spatial weighting.                                          ──
    const int dim = args.nChannels * data2use;
    if (verbose) cout << "Feature dimensionality: " << dim
                      << " (= " << args.nChannels << " ch × " << data2use << " samp)" << endl;

    gsl_matrix *X = gsl_matrix_alloc((size_t)nSpikes, (size_t)dim);
    for (long long k = 0; k < nSpikes; ++k) {
        const short *spike = rawData.data() + k * perSpike;
        for (int ch = 0; ch < args.nChannels; ++ch) {
            for (int s = 0; s < data2use; ++s) {
                const int srcIdx = (s + recShift) * args.nChannels + ch;
                const int dstIdx = ch * data2use + s;
                gsl_matrix_set(X, (size_t)k, (size_t)dstIdx,
                               static_cast<double>(spike[srcIdx]));
            }
        }
    }
    rawData.clear(); rawData.shrink_to_fit();
    pb.advance();

    // ── Step 2: compute and subtract the column mean ──
    std::vector<double> mean(dim, 0.0);
    for (long long k = 0; k < nSpikes; ++k)
        for (int d = 0; d < dim; ++d)
            mean[d] += gsl_matrix_get(X, (size_t)k, (size_t)d);
    for (int d = 0; d < dim; ++d) mean[d] /= static_cast<double>(nSpikes);
    for (long long k = 0; k < nSpikes; ++k)
        for (int d = 0; d < dim; ++d) {
            const double v = gsl_matrix_get(X, (size_t)k, (size_t)d) - mean[d];
            gsl_matrix_set(X, (size_t)k, (size_t)d, v);
        }
    pb.advance();

    // ── Step 3: build covariance C = (X^T X) / (nSpikes - 1), dim × dim ──
    gsl_matrix *C = gsl_matrix_alloc((size_t)dim, (size_t)dim);
    gsl_blas_dgemm(CblasTrans, CblasNoTrans,
                   1.0 / static_cast<double>(nSpikes - 1), X, X,
                   0.0, C);
    pb.advance();

    // ── Step 3b: optional spatial weighting ──
    std::vector<double> spatialW;
    const bool useSpatialWeight = build_spatial_weights(args.spatialWeightRadius, geom, spatialW);
    if (useSpatialWeight) {
        if (verbose) cout << "Applying spatial weighting (radius = "
                          << args.spatialWeightRadius << " μm)" << endl;
        // For each (channel_i, channel_j) block of size data2use × data2use,
        // multiply by W_ij.  Block layout: C[ch_i*data2use + s, ch_j*data2use + t].
        for (int ci = 0; ci < args.nChannels; ++ci) {
            for (int cj = 0; cj < args.nChannels; ++cj) {
                const double w = spatialW[(size_t)ci * args.nChannels + cj];
                for (int s = 0; s < data2use; ++s) {
                    for (int t = 0; t < data2use; ++t) {
                        const size_t r = (size_t)ci * data2use + s;
                        const size_t c = (size_t)cj * data2use + t;
                        const double v = gsl_matrix_get(C, r, c);
                        gsl_matrix_set(C, r, c, v * w);
                    }
                }
            }
        }
    }

    // ── Step 4: eigendecomp ──
    if (verbose) cout << "Eigendecomposing " << dim << " × " << dim
                      << " covariance" << endl;
    gsl_vector *evals = gsl_vector_alloc((size_t)dim);
    gsl_matrix *evecs = gsl_matrix_alloc((size_t)dim, (size_t)dim);
    gsl_eigen_symmv_workspace *ws = gsl_eigen_symmv_alloc((size_t)dim);
    gsl_eigen_symmv(C, evals, evecs, ws);
    gsl_eigen_symmv_sort(evals, evecs, GSL_EIGEN_SORT_VAL_DESC);
    gsl_eigen_symmv_free(ws);
    gsl_matrix_free(C);
    pb.advance();

    // ── Step 5: keep top K, optional Varimax ──
    gsl_matrix_view topEvecsView = gsl_matrix_submatrix(evecs, 0, 0, dim, args.nComponents);
    if (args.varimax && args.nComponents >= 2) {
        int sw = applyVarimaxRotation(&topEvecsView.matrix, args.varimaxMaxIter, args.varimaxTol);
        if (verbose)
            cout << "Varimax: " << sw << " sweep(s)"
                 << (sw >= args.varimaxMaxIter ? " (hit max-iter)" : "") << endl;
    }

    // ── Step 6: project X onto top K to get features (nSpikes × K) ──
    gsl_matrix *features = gsl_matrix_alloc((size_t)nSpikes, (size_t)args.nComponents);
    gsl_blas_dgemm(CblasNoTrans, CblasNoTrans, 1.0, X, &topEvecsView.matrix, 0.0, features);
    gsl_matrix_free(X);
    pb.advance();

    // Report total/explained variance.
    double totalVar = 0.0;
    for (int d = 0; d < dim; ++d) totalVar += gsl_vector_get(evals, d);
    double keptVar = 0.0;
    for (int d = 0; d < args.nComponents; ++d) keptVar += gsl_vector_get(evals, d);
    if (verbose)
        cout << "Explained variance with " << args.nComponents << " components: "
             << (100.0 * keptVar / totalVar) << "% of total"
             << " (" << keptVar << " / " << totalVar << ")" << endl;

    // ── Step 7: write .fetM (int32 nFeat + int64 features × nSpikes) ──
    FILE *fout = std::fopen(args.outputFileName, "wb");
    if (!fout) { cerr << "error: cannot open output '" << args.outputFileName << "'\n"; return 1; }
    {
        int32_t nf = (int32_t)args.nComponents;
        std::fwrite(&nf, sizeof(int32_t), 1, fout);
    }
    {
        std::vector<int64_t> buf((size_t)nSpikes * args.nComponents);
        for (long long k = 0; k < nSpikes; ++k)
            for (int j = 0; j < args.nComponents; ++j)
                buf[(size_t)k * args.nComponents + j] =
                    static_cast<int64_t>(std::llround(gsl_matrix_get(features, (size_t)k, (size_t)j)));
        std::fwrite(buf.data(), sizeof(int64_t), buf.size(), fout);
    }
    std::fclose(fout);

    // ── Step 8: write .pcaM (geometry + mean + eigenvectors + eigenvalues) ──
    {
        std::string pcaPath(args.outputFileName);
        const std::string tmp(".tmp");
        if (pcaPath.size() >= tmp.size() &&
            pcaPath.compare(pcaPath.size() - tmp.size(), tmp.size(), tmp) == 0)
            pcaPath.erase(pcaPath.size() - tmp.size());
        const std::string fetMStr(".fetM."), pcaMStr(".pcaM.");
        size_t pos = pcaPath.rfind(fetMStr);
        if (pos != std::string::npos)
            pcaPath.replace(pos, fetMStr.size(), pcaMStr);

        FILE *pf = std::fopen(pcaPath.c_str(), "wb");
        if (!pf) { cerr << "error: cannot open .pcaM '" << pcaPath << "'\n"; return 1; }
        int32_t magic = PCAM_MAGIC, ver = PCAM_VERSION;
        std::fwrite(&magic, sizeof(int32_t), 1, pf);
        std::fwrite(&ver,   sizeof(int32_t), 1, pf);
        int32_t hdr[5] = {
            (int32_t)args.nChannels, (int32_t)data2use, (int32_t)args.nComponents,
            (int32_t)(args.isCenteredData ? 1 : 0), (int32_t)recShift
        };
        std::fwrite(hdr, sizeof(int32_t), 5, pf);
        // Geometry
        std::fwrite(geom.posX.data(), sizeof(double), args.nChannels, pf);
        std::fwrite(geom.posY.data(), sizeof(double), args.nChannels, pf);
        // Mean vector (length dim)
        std::fwrite(mean.data(), sizeof(double), dim, pf);
        // Eigenvectors (col-major: dim rows × nComp cols)
        {
            std::vector<double> e((size_t)dim * args.nComponents);
            for (int j = 0; j < args.nComponents; ++j)
                for (int r = 0; r < dim; ++r)
                    e[(size_t)j * dim + r] = gsl_matrix_get(&topEvecsView.matrix, r, j);
            std::fwrite(e.data(), sizeof(double), e.size(), pf);
        }
        // Top-K eigenvalues
        {
            std::vector<double> ev((size_t)args.nComponents);
            for (int j = 0; j < args.nComponents; ++j) ev[j] = gsl_vector_get(evals, j);
            std::fwrite(ev.data(), sizeof(double), ev.size(), pf);
        }
        std::fclose(pf);
        if (verbose) cout << "Wrote " << pcaPath << endl;
    }

    // Cleanup
    gsl_matrix_free(features);
    gsl_matrix_free(evecs);
    gsl_vector_free(evals);
    pb.advance();
    return 0;
}

/***************************************************************************
 * process_refeaturize.cpp
 *
 * Re-project spike waveforms onto previously saved PCA eigenvectors.
 *
 * Usage:
 *   process_refeaturize -p pca-file -w waveformLength -n nChannels
 *                       [-i spike-indices-file] [-x] spk-file
 *
 * Options:
 *   -p pca-file       Path to the .pca.N file written by process_pca -e
 *   -w waveformLength Number of samples per waveform (same as in original PCA)
 *   -n nChannels      Number of channels in the spike file
 *   -i indices-file   Text file listing 0-based spike indices to re-featurize
 *                     (one per line). If absent, all spikes are processed.
 *   -x                Append per-channel peak values as extra features
 *                     (mirrors the -x flag of process_pca)
 *   -v                Verbose output
 *   -h                Show this help
 *
 * Output:
 *   Writes new feature rows to stdout (same format as process_pca output but
 *   WITHOUT the leading count line and WITHOUT a timestamp column — the caller
 *   is responsible for patching the updated rows back into the .fet.N file).
 *
 *   Each output line: nComponents*nChannels integers, one per feature, space-
 *   separated, followed by a newline.  If -x is specified, nChannels extra
 *   peak values are appended (same order as process_pca).
 *
 * PCA file format (written by process_pca -e):
 *   int32  magic       0x50434145 ("PCAE")
 *   int32  version     1
 *   int32  nChannels
 *   int32  data2use    samples per channel used for PCA
 *   int32  nComponents principal components kept
 *   int32  recShift    first sample offset within waveform
 *   int32  isCentered  1=centered projection, 0=raw
 *   for each channel:
 *     double[data2use]             per-channel mean
 *     double[data2use*nComponents] eigenvectors (col-major: col=component)
 ***************************************************************************/

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <cerrno>
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <stdint.h>

using namespace std;

static bool verbose = false;

static void usage(const char* name)
{
    cerr << "usage: " << name
         << " -p pca-file -w waveformLength -n nChannels"
            " [-i indices-file] [-x] [-v] spk-file" << endl;
    exit(1);
}

static void help(const char* name)
{
    cout << name << ": re-project spike waveforms onto saved PCA eigenvectors." << endl;
    cout << "usage: " << name
         << " -p pca-file -w waveformLength -n nChannels"
            " [-i indices-file] [-x] [-v] spk-file" << endl;
    cout << "  -p pca-file        .pca.N file from process_pca -e" << endl;
    cout << "  -w waveformLength  samples per waveform" << endl;
    cout << "  -n nChannels       channels in spike file" << endl;
    cout << "  -i indices-file    0-based spike indices to process (default: all)" << endl;
    cout << "  -x                 append peak values as extra features" << endl;
    cout << "  -v                 verbose" << endl;
    exit(0);
}

// --------------------------------------------------------------------------
// PCA model loaded from file
// --------------------------------------------------------------------------
struct PcaModel {
    int nChannels;
    int data2use;
    int nComponents;
    int recShift;
    bool isCentered;
    // mean[channel][sample]
    vector<vector<double>> mean;
    // eigvec[channel][sample * nComponents + component]  (col-major)
    vector<vector<double>> eigvec;
};

static bool loadPcaModel(const char* path, PcaModel& m)
{
    FILE* f = fopen(path, "rb");
    if (!f) { cerr << "error: cannot open pca file '" << path << "'" << endl; return false; }

    int32_t magic, version, nc, d2u, ncomp, rshift, iscentered;
    if (fread(&magic,      sizeof(int32_t), 1, f) != 1 ||
        fread(&version,    sizeof(int32_t), 1, f) != 1 ||
        fread(&nc,         sizeof(int32_t), 1, f) != 1 ||
        fread(&d2u,        sizeof(int32_t), 1, f) != 1 ||
        fread(&ncomp,      sizeof(int32_t), 1, f) != 1 ||
        fread(&rshift,     sizeof(int32_t), 1, f) != 1 ||
        fread(&iscentered, sizeof(int32_t), 1, f) != 1) {
        cerr << "error: truncated pca file header" << endl; fclose(f); return false;
    }
    if (magic != static_cast<int32_t>(0x50434145)) {
        cerr << "error: bad magic in pca file (not a PCAE file)" << endl; fclose(f); return false;
    }
    if (version != 1) {
        cerr << "error: unsupported pca file version " << version << endl; fclose(f); return false;
    }

    m.nChannels   = static_cast<int>(nc);
    m.data2use    = static_cast<int>(d2u);
    m.nComponents = static_cast<int>(ncomp);
    m.recShift    = static_cast<int>(rshift);
    m.isCentered  = (iscentered != 0);

    m.mean.resize(static_cast<size_t>(m.nChannels));
    m.eigvec.resize(static_cast<size_t>(m.nChannels));

    for (int ch = 0; ch < m.nChannels; ++ch) {
        m.mean[static_cast<size_t>(ch)].resize(static_cast<size_t>(m.data2use));
        if (fread(m.mean[static_cast<size_t>(ch)].data(), sizeof(double),
                  static_cast<size_t>(m.data2use), f) != static_cast<size_t>(m.data2use)) {
            cerr << "error: truncated pca file (means, channel " << ch << ")" << endl;
            fclose(f); return false;
        }
        size_t evSz = static_cast<size_t>(m.data2use * m.nComponents);
        m.eigvec[static_cast<size_t>(ch)].resize(evSz);
        if (fread(m.eigvec[static_cast<size_t>(ch)].data(), sizeof(double), evSz, f) != evSz) {
            cerr << "error: truncated pca file (eigenvectors, channel " << ch << ")" << endl;
            fclose(f); return false;
        }
    }
    fclose(f);
    return true;
}

// --------------------------------------------------------------------------
// Project a single spike onto the PCA model.
// waveform: raw int16 samples, interleaved channels, waveformLength samples each.
// out:      nChannels * nComponents output values (written in place).
// peakOut:  if non-null, receives nChannels peak values.
// --------------------------------------------------------------------------
static void projectSpike(const short* waveform, int waveformLength,
                         const PcaModel& m,
                         double* out, short* peakOut)
{
    for (int ch = 0; ch < m.nChannels; ++ch) {
        const vector<double>& mu  = m.mean[static_cast<size_t>(ch)];
        const vector<double>& ev  = m.eigvec[static_cast<size_t>(ch)];

        // Collect the data2use samples for this channel with optional shift
        vector<double> x(static_cast<size_t>(m.data2use));
        for (int s = 0; s < m.data2use; ++s) {
            int sampleIdx = m.recShift + s;     // 0-based position in waveform
            // interleaved: sample i, channel ch -> index i*nChannels + ch
            double raw = static_cast<double>(
                waveform[sampleIdx * m.nChannels + ch]);
            x[static_cast<size_t>(s)] = m.isCentered ? (raw - mu[static_cast<size_t>(s)]) : raw;
        }

        // Project: comp k = sum_s  eigvec[ch][s * nComponents + k] * x[s]
        for (int k = 0; k < m.nComponents; ++k) {
            double val = 0.0;
            for (int s = 0; s < m.data2use; ++s)
                val += ev[static_cast<size_t>(s * m.nComponents + k)]
                     * x[static_cast<size_t>(s)];
            out[static_cast<size_t>(ch * m.nComponents + k)] = val;
        }

        // Peak value (sample at recShift + peakOffset; peakOffset = beforeSpike offset
        // not stored in PcaModel; use max-abs within the window instead)
        if (peakOut) {
            short peak = 0;
            for (int s = 0; s < m.data2use; ++s) {
                int sampleIdx = m.recShift + s;
                short v = waveform[sampleIdx * m.nChannels + ch];
                if (abs(v) > abs(peak)) peak = v;
            }
            peakOut[static_cast<size_t>(ch)] = peak;
        }
    }
}

int main(int argc, char** argv)
{
    const char* pcaPath    = nullptr;
    const char* spkPath    = nullptr;
    const char* idxPath    = nullptr;
    int waveformLength     = -1;
    int nChannels          = -1;
    bool extraFeatures     = false;

    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) help(argv[0]);
        else if (!strcmp(argv[i], "-p") && i+1 < argc) pcaPath = argv[++i];
        else if (!strcmp(argv[i], "-w") && i+1 < argc) waveformLength = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-n") && i+1 < argc) nChannels      = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-i") && i+1 < argc) idxPath        = argv[++i];
        else if (!strcmp(argv[i], "-x"))               extraFeatures  = true;
        else if (!strcmp(argv[i], "-v"))               verbose        = true;
        else if (argv[i][0] != '-')                    spkPath        = argv[i];
        else { cerr << "error: unknown option '" << argv[i] << "'" << endl; usage(argv[0]); }
    }

    if (!pcaPath || !spkPath || waveformLength < 1 || nChannels < 1) {
        cerr << "error: missing required arguments" << endl; usage(argv[0]);
    }

    // Load PCA model
    PcaModel model;
    if (!loadPcaModel(pcaPath, model)) return 1;
    if (model.nChannels != nChannels) {
        cerr << "error: pca file has " << model.nChannels
             << " channels, -n says " << nChannels << endl;
        return 1;
    }

    // Open spike file
    FILE* spkFile = fopen(spkPath, "rb");
    if (!spkFile) {
        cerr << "error: cannot open spike file '" << spkPath << "'" << endl; return 1;
    }
    fseeko(spkFile, 0, SEEK_END);
    long long spkSize = static_cast<long long>(ftello(spkFile));
    rewind(spkFile);

    long long bytesPerSpike = static_cast<long long>(nChannels) * waveformLength * 2; // int16
    if (spkSize % bytesPerSpike != 0) {
        cerr << "error: spike file size (" << spkSize
             << ") not a multiple of bytesPerSpike (" << bytesPerSpike << ")" << endl;
        fclose(spkFile); return 1;
    }
    long long nSpikes = spkSize / bytesPerSpike;

    // Build index list
    vector<long long> indices;
    if (idxPath) {
        ifstream idxFile(idxPath);
        if (!idxFile) {
            cerr << "error: cannot open indices file '" << idxPath << "'" << endl;
            fclose(spkFile); return 1;
        }
        long long idx;
        while (idxFile >> idx) indices.push_back(idx);
    } else {
        indices.resize(static_cast<size_t>(nSpikes));
        for (long long i = 0; i < nSpikes; ++i) indices[static_cast<size_t>(i)] = i;
    }

    if (verbose) {
        cerr << "PCA model: " << model.nChannels << " channels, "
             << model.data2use << " samples, " << model.nComponents << " components, "
             << "recShift=" << model.recShift << ", centered=" << model.isCentered << endl;
        cerr << "Spike file: " << nSpikes << " spikes, processing " << indices.size() << endl;
    }

    // Allocate buffers
    vector<short> waveform(static_cast<size_t>(nChannels * waveformLength));
    vector<double> proj(static_cast<size_t>(model.nChannels * model.nComponents));
    vector<short> peaks(static_cast<size_t>(model.nChannels));

    // Process each requested spike
    for (long long idx : indices) {
        if (idx < 0 || idx >= nSpikes) {
            cerr << "warning: spike index " << idx << " out of range [0," << nSpikes-1
                 << "], skipping" << endl;
            continue;
        }
        // Seek and read
        off_t offset = static_cast<off_t>(idx) * static_cast<off_t>(bytesPerSpike);
        if (fseeko(spkFile, offset, SEEK_SET) != 0) {
            cerr << "error: seek failed for spike " << idx << endl; continue;
        }
        size_t samplesPerSpike = static_cast<size_t>(nChannels * waveformLength);
        if (fread(waveform.data(), sizeof(short), samplesPerSpike, spkFile) != samplesPerSpike) {
            cerr << "error: read failed for spike " << idx << endl; continue;
        }

        projectSpike(waveform.data(), waveformLength, model, proj.data(),
                     extraFeatures ? peaks.data() : nullptr);

        // Write feature row (no timestamp — caller handles that)
        for (int ch = 0; ch < model.nChannels; ++ch)
            for (int k = 0; k < model.nComponents; ++k)
                printf("%d ", static_cast<int>(proj[static_cast<size_t>(ch * model.nComponents + k)]));

        if (extraFeatures)
            for (int ch = 0; ch < model.nChannels; ++ch)
                printf("%d ", static_cast<int>(peaks[static_cast<size_t>(ch)]));

        printf("\n");
    }

    fclose(spkFile);
    return 0;
}

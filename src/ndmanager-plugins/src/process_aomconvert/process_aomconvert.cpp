/***************************************************************************
 *   Copyright (C) 2025 neurosuite-3 contributors                         *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program. If not, see <http://www.gnu.org/licenses/>.  *
 ***************************************************************************/

/*
 * process_aomconvert – AlphaOmega .mat (HDF5 v7.3) → neurosuite .dat + .yaml
 *
 * Usage:
 *   process_aomconvert [options] input.mat output_basename
 *
 * Options:
 *   -t, --topology SPEC    Mixed probe topology, e.g. "1-16:16,17-32:4"
 *                          FIRST-LAST:SIZE tokens (1-based, inclusive).
 *                          SIZE==4 → tetrode, SIZE==1 → single, else → linear.
 *   -g, --groups N         Uniform group size when --topology absent (default: 8)
 *   -c, --chunk N          Samples per streaming read chunk (default: 10000000)
 *   -n, --no-yaml          Skip YAML generation
 *   -d, --dry-run          Print metadata and exit without writing
 *   -h, --help             Display help
 *
 * Outputs:
 *   output_basename.dat    Little-endian int16, sample-major interleaved
 *   output_basename.yaml   neurosuite-3 session YAML (template.yaml schema)
 */

#ifndef _LARGEFILE_SOURCE
#  define _LARGEFILE_SOURCE
#endif
#define _FILE_OFFSET_BITS 64

#include <hdf5.h>
#include <utility>

#include <algorithm>
#include <cassert>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace std;

// ── constants ────────────────────────────────────────────────────────────────

static const int    N_PCS_PER_CHAN  = 3;
static const double KK_CHUNK_MIN   = 10.0;
static const int    WAVEFORM_SAMPS = 32;    // nSamples / peakSampleIndex default
static const long   DEFAULT_CHUNK  = 10'000'000L;
static const int    DEFAULT_GROUPS = 8;

// ── chi-squared ppf (Wilson-Hilferty approximation, accurate to <0.1% for df≥3) ──
// Returns x such that P(chi2(df) ≤ x) = p.
static double chi2ppf(double p, int df)
{
    // For p=0.9999 the Wilson-Hilferty approximation gives <0.1% error for df≥3.
    // Reference: Abramowitz & Stegun 26.4.17
    const double z = 3.7190;   // qnorm(0.9999) ≈ 3.7190
    double h = 2.0 / (9.0 * df);
    double x = df * pow(1.0 - h + z * sqrt(h), 3.0);
    return x;
}

// ── probe type from group size ────────────────────────────────────────────────
static string probeType(int sz)
{
    if (sz == 4) return "tetrode";
    if (sz == 1) return "single";
    return "linear";
}

// ── Group: one spike detection group ─────────────────────────────────────────
struct Group {
    vector<int> channels;   // 0-based
    string      type;       // "linear" | "tetrode" | "single"
};

// ── Topology parsing ──────────────────────────────────────────────────────────
// Spec: "FIRST-LAST:SIZE,..." (1-based, inclusive)
static vector<Group> parseTopology(const string& spec, int nChannels)
{
    vector<Group> groups;
    istringstream ss(spec);
    string token;
    while (getline(ss, token, ',')) {
        if (token.empty()) continue;
        auto colon = token.rfind(':');
        if (colon == string::npos)
            throw runtime_error("invalid topology token '" + token + "': missing ':'");
        int size = stoi(token.substr(colon + 1));
        string rng = token.substr(0, colon);
        int first, last;
        auto dash = rng.find('-');
        if (dash == string::npos) {
            first = last = stoi(rng);
        } else {
            first = stoi(rng.substr(0, dash));
            last  = stoi(rng.substr(dash + 1));
        }
        for (int ch = first - 1; ch < last; ch += size) {
            if (ch >= nChannels)
                throw runtime_error("topology channel " + to_string(ch + 1) +
                                    " exceeds nChannels=" + to_string(nChannels));
            Group g;
            for (int k = ch; k < min(ch + size, nChannels); ++k)
                g.channels.push_back(k);
            g.type = probeType(size);
            groups.push_back(std::move(g));
        }
    }
    if (groups.empty())
        throw runtime_error("topology spec produced no groups");
    return groups;
}

static vector<Group> uniformGroups(int nChannels, int groupSize)
{
    vector<Group> groups;
    string ptype = probeType(groupSize);
    for (int i = 0; i < nChannels; i += groupSize) {
        Group g;
        for (int k = i; k < min(i + groupSize, nChannels); ++k)
            g.channels.push_back(k);
        g.type = ptype;
        groups.push_back(std::move(g));
    }
    return groups;
}

// ── HDF5 helpers ──────────────────────────────────────────────────────────────

// Prefix for all dataset path lookups.
// Empty string = datasets live at HDF5 root (Layout A: direct export).
// "groupname/" = datasets live inside a top-level group (Layout B: MATLAB -v7.3 struct).
// Set once by discoverChannels(); all helpers below use it.
static string g_channelPrefix;

// Suppress HDF5 error stack printing during probes
static herr_t silentHdf5Error(hid_t, void*) { return 0; }

static double readHdf5Scalar(hid_t file, const string& name)
{
    string fullName = g_channelPrefix + name;
    hid_t ds = H5Dopen2(file, fullName.c_str(), H5P_DEFAULT);
    if (ds < 0)
        throw runtime_error("HDF5 dataset not found: " + fullName);
    double val = 0.0;
    hid_t memType = H5T_NATIVE_DOUBLE;
    // AlphaOmega stores scalars as 1-element arrays; flatten to scalar
    hid_t space = H5Dget_space(ds);
    hsize_t nElems = H5Sget_simple_extent_npoints(space);
    H5Sclose(space);
    if (nElems == 1) {
        H5Dread(ds, memType, H5S_ALL, H5S_ALL, H5P_DEFAULT, &val);
    } else {
        vector<double> buf(nElems);
        H5Dread(ds, memType, H5S_ALL, H5S_ALL, H5P_DEFAULT, buf.data());
        val = buf[0];
    }
    H5Dclose(ds);
    return val;
}

// Return sorted list of CRAW_NNN dataset names present in the file.
//
// Handles two layouts produced by AlphaOmega + MATLAB:
//
//   Layout A (direct HDF5 export):
//     /CRAW_001  /CRAW_001_KHz  ... (top-level datasets)
//
//   Layout B (MATLAB struct saved with -v7.3):
//     MATLAB serialises the struct variable (e.g. 'ds') as an HDF5 group:
//     /ds/CRAW_001  /ds/CRAW_001_KHz  ...
//     The struct variable name is arbitrary and unknown in advance.
//
// Strategy: collect CRAW_NNN from the root; if none found, open every
// root-level GROUP and search one level deeper; use the first group
// that contains CRAW_NNN datasets.  Record the prefix so that all
// subsequent readHdf5Scalar / readChannelSlice calls use the same path.

static bool isCrawChannel(const string& n)
{
    if (n.size() < 8 || n.substr(0, 5) != "CRAW_") return false;
    for (size_t i = 5; i < n.size(); ++i)
        if (!isdigit(n[i])) return false;
    return true;
}

static vector<string> collectCraw(hid_t group)
{
    struct Col {
        vector<string> names;
        static herr_t cb(hid_t, const char* name, const H5L_info_t*, void* data) {
            string n(name);
            if (isCrawChannel(n))
                static_cast<Col*>(data)->names.push_back(n);
            return 0;
        }
    } col;
    H5Literate(group, H5_INDEX_NAME, H5_ITER_NATIVE, nullptr, &Col::cb, &col);
    return col.names;
}

static vector<string> discoverChannels(hid_t file)
{
    g_channelPrefix.clear();

    // Try root first (Layout A)
    auto names = collectCraw(file);
    if (!names.empty()) {
        sort(names.begin(), names.end(),
             [](const string& a, const string& b){
                 return stoi(a.substr(5)) < stoi(b.substr(5)); });
        return names;
    }

    // Root has no CRAW_* — look one level deep in every root group (Layout B)
    struct GroupFinder {
        hid_t       file;
        vector<string> names;
        string      prefix;
        static herr_t cb(hid_t loc, const char* name,
                         const H5L_info_t*, void* data)
        {
            auto* gf = static_cast<GroupFinder*>(data);
            if (!gf->names.empty()) return 0;  // already found

            H5O_info_t info;
            H5Oget_info_by_name(loc, name, &info, H5O_INFO_BASIC, H5P_DEFAULT);
            if (info.type != H5O_TYPE_GROUP) return 0;

            hid_t grp = H5Gopen2(loc, name, H5P_DEFAULT);
            if (grp < 0) return 0;
            auto found = collectCraw(grp);
            H5Gclose(grp);

            if (!found.empty()) {
                gf->names  = found;
                gf->prefix = string(name) + "/";
            }
            return 0;
        }
    } gf;
    gf.file = file;
    H5Literate(file, H5_INDEX_NAME, H5_ITER_NATIVE, nullptr,
               &GroupFinder::cb, &gf);

    if (!gf.names.empty()) {
        g_channelPrefix = gf.prefix;
        sort(gf.names.begin(), gf.names.end(),
             [](const string& a, const string& b){
                 return stoi(a.substr(5)) < stoi(b.substr(5)); });
        return gf.names;
    }

    return {};  // no CRAW channels found
}

// Get number of samples in a dataset (last dimension)
static hsize_t datasetNSamples(hid_t file, const string& name)
{
    string fullName = g_channelPrefix + name;
    hid_t ds = H5Dopen2(file, fullName.c_str(), H5P_DEFAULT);
    if (ds < 0) throw runtime_error("cannot open dataset " + fullName);
    hid_t sp = H5Dget_space(ds);
    int ndims = H5Sget_simple_extent_ndims(sp);
    vector<hsize_t> dims(ndims);
    H5Sget_simple_extent_dims(sp, dims.data(), nullptr);
    H5Sclose(sp);
    H5Dclose(ds);
    return dims.back();
}

// Read a slice of one channel into int16 buffer (offset, count in samples)
static void readChannelSlice(hid_t file, const string& name,
                              hsize_t offset, hsize_t count,
                              int16_t* dst)
{
    string fullName = g_channelPrefix + name;
    hid_t ds = H5Dopen2(file, fullName.c_str(), H5P_DEFAULT);
    if (ds < 0) throw runtime_error("cannot open dataset " + fullName);
    hid_t filespace = H5Dget_space(ds);
    int ndims = H5Sget_simple_extent_ndims(filespace);
    vector<hsize_t> dims(ndims);
    H5Sget_simple_extent_dims(filespace, dims.data(), nullptr);

    // Hyperslab selection: last dimension is the sample axis
    vector<hsize_t> start(ndims, 0);
    vector<hsize_t> cnt(ndims, 1);
    start.back() = offset;
    cnt.back()   = count;
    H5Sselect_hyperslab(filespace, H5S_SELECT_SET,
                        start.data(), nullptr, cnt.data(), nullptr);

    hsize_t memDim = count;
    hid_t memspace = H5Screate_simple(1, &memDim, nullptr);
    H5Dread(ds, H5T_NATIVE_INT16, memspace, filespace, H5P_DEFAULT, dst);
    H5Sclose(memspace);
    H5Sclose(filespace);
    H5Dclose(ds);
}

// ── YAML writer ───────────────────────────────────────────────────────────────

static void writeYaml(const string& path,
                      const string& basename,
                      int nChannels,
                      int srHz,
                      double bitResUv,
                      int amplification,
                      hsize_t nSamples,
                      double durationS,
                      double tBegin,
                      double tEnd,
                      const vector<Group>& groups)
{
    ofstream f(path);
    if (!f) throw runtime_error("cannot create " + path);

    auto joinInts = [](const vector<int>& v) {
        string s;
        for (size_t i = 0; i < v.size(); ++i) {
            if (i) s += ", ";
            s += to_string(v[i]);
        }
        return s;
    };

    // ── session header ────────────────────────────────────────────────────
    f << "# neurosuite-3 session YAML — generated by process_aomconvert\n"
         "parameters:\n"
         "  version: '1.0'\n"
         "  creator: ndm_aom2dat\n"
         "\n"
         "generalInfo:\n"
         "  date: null\n"
         "  experimenters: null\n"
         "  description: null\n"
         "  notes: null\n"
         "\n";

    // ── acquisitionSystem ────────────────────────────────────────────────
    f << "# Read by ndm_hipass, ndm_extractspikes, ndm_pca, ndm_klustakwik, etc.\n"
         "acquisitionSystem:\n"
      << "  nBits: 16\n"
      << "  nChannels: " << nChannels << "\n"
      << "  samplingRate: " << srHz << "\n"
         "  voltageRange: null\n"
      << "  amplification: " << amplification << "\n"
         "  offset: 0\n"
      << "  bitResolution_uV: " << fixed << setprecision(6) << bitResUv << "\n"
      << "  nSamples: " << nSamples << "\n"
      << "  duration_s: " << fixed << setprecision(4) << durationS << "\n"
      << "  recordingBegin_s: " << tBegin << "\n"
      << "  recordingEnd_s: " << tEnd << "\n"
         "\n";

    // ── fieldPotentials ───────────────────────────────────────────────────
    f << "fieldPotentials:\n"
         "  lfpSamplingRate: 1250\n"
         "\n"
         "files:\n"
         "- samplingRate: 1250\n"
         "  extension: lfp\n"
         "\n";

    // ── anatomicalDescription ─────────────────────────────────────────────
    f << "# Read by NeuroScope, ndm_reorderchannels, ndm_spikegrouper\n"
         "anatomicalDescription:\n"
         "  channelGroups:\n";
    for (auto& g : groups) {
        f << "  - channels:\n";
        for (int ch : g.channels)
            f << "    - {id: " << ch << ", skip: 0}\n";
        f << "    probeType: " << g.type << "\n";
    }
    f << "\n";

    // ── spikeDetection ────────────────────────────────────────────────────
    f << "# Read by ndm_extractspikes, ndm_pca, ndm_klustakwik\n"
         "spikeDetection:\n"
         "  channelGroups:\n";
    for (auto& g : groups) {
        int nCh = (int)g.channels.size();
        int nSp = nCh * N_PCS_PER_CHAN;
        double mt = round(chi2ppf(0.9999, nSp) * 100.0) / 100.0;
        int maxC = (g.type == "tetrode") ? 20 : (g.type == "single") ? 5 : 40;
        int nChunks = (int)ceil(durationS / (KK_CHUNK_MIN * 60.0));
        int maxP = nChunks * maxC + 50;
        double scale = sqrt(max(nSp, 24) / 24.0);
        int mergeIter = max(40, (int)(round(50.0 * scale / 10.0) * 10));

        f << "  - channels: [" << joinInts(g.channels) << "]\n"
          << "    nSamples: " << WAVEFORM_SAMPS << "\n"
          << "    peakSampleIndex: " << WAVEFORM_SAMPS / 2 << "\n"
          << "    nFeatures: " << N_PCS_PER_CHAN << "\n"
          << "    probeType: " << g.type << "\n"
          << "    # Tier-1 KlustaKwik: probe-calibrated, override global block for this group\n"
          << "    # nSpatialDims=" << nSp << "  mergeThresh=chi2(" << nSp << ",0.9999)\n"
          << "    klustakwik:\n"
          << "      maxClusters: " << maxC << "\n"
          << "      maxPossibleClusters: " << maxP << "\n"
          << "      mergeThresh: " << fixed << setprecision(2) << mt << "\n"
          << "      globalMergeIter: " << mergeIter << "\n"
          << "      timeMergeIter: " << mergeIter << "\n";
    }
    f << "\n";

    // ── programs: ndm_aom2dat ─────────────────────────────────────────────
    f << "programs:\n"
         "\n"
         "- name: ndm_aom2dat\n"
         "  parameters:\n"
         "  - {name: matFile,      value: null,          status: Mandatory}\n"
         "  - {name: topology,     value: null,          status: Optional}\n"
         "  - {name: groups,       value: " << DEFAULT_GROUPS << ",          status: Optional}\n"
         "  - {name: chunkSamples, value: " << DEFAULT_CHUNK << ", status: Optional}\n"
         "  help: |\n"
         "    Convert AlphaOmega .mat (HDF5 v7.3) recording to .dat + session YAML.\n"
         "    matFile: path to input .mat file (relative to session directory).\n"
         "    topology: mixed probe spec e.g. '1-16:16,17-32:4' (overrides groups).\n"
         "    groups: uniform channels per spike group (default " << DEFAULT_GROUPS << ").\n"
         "    chunkSamples: streaming read chunk size (default " << DEFAULT_CHUNK << ").\n"
         "\n"
         "# ── Global KlustaKwik parameters (tier 2) ───────────────────────────────────\n"
         "# Session-level constants shared across all spike groups.\n"
         "# Per-group spikeDetection.channelGroups[g].klustakwik entries override these.\n"
         "# Edit here to change a parameter for every group at once.\n"
         "- name: ndm_klustakwik\n"
         "  parameters:\n"
         "  - {name: minClusters,          value: 2,        status: Optional}\n"
         "  - {name: useFeatures,          value: all,      status: Optional}\n"
         "  - {name: nStarts,              value: 3,        status: Optional}\n"
         "  - {name: penaltyMix,           value: 0,        status: Optional}\n"
         "  - {name: initMethod,           value: farthest, status: Optional}\n"
         "  - {name: chunkMinutes,         value: 10.0,     status: Optional}\n"
         "  - {name: chunkOverlapMinutes,  value: 2.0,      status: Optional}\n"
         "  - {name: chunkPreseedFraction, value: 0.05,     status: Optional}\n"
         "  - {name: maxIter,              value: 500,      status: Optional}\n"
         "  - {name: splitEvery,           value: 25,       status: Optional}\n"
         "  - {name: distThresh,           value: 6.9,      status: Optional}\n"
         "  - {name: fullStepEvery,        value: 10,       status: Optional}\n"
         "  - {name: changedThresh,        value: 0.05,     status: Optional}\n"
         "  - {name: log,                  value: 1,        status: Optional}\n"
         "  - {name: screen,               value: 0,        status: Optional}\n"
         "  - {name: fSaveModel,           value: 1,        status: Optional}\n"
         "  - {name: saveIntermediates,    value: 0,        status: Optional}\n";

    if (!f.good()) throw runtime_error("write error on " + path);
}

// ── main ──────────────────────────────────────────────────────────────────────

static void usage(const char* prog, int code = EXIT_SUCCESS)
{
    ostream& out = (code == EXIT_SUCCESS) ? cout : cerr;
    out << "\nusage: " << prog << " [options] input.mat output_basename\n\n"
        << "  -t, --topology SPEC    mixed probe topology, e.g. \"1-16:16,17-32:4\"\n"
        << "                         FIRST-LAST:SIZE (1-based, inclusive)\n"
        << "                         SIZE==4 → tetrode, SIZE==1 → single, else → linear\n"
        << "  -g, --groups N         uniform channels per group (default: " << DEFAULT_GROUPS << ")\n"
        << "  -c, --chunk N          samples per streaming chunk (default: " << DEFAULT_CHUNK << ")\n"
        << "  -n, --no-yaml          skip YAML generation\n"
        << "  -d, --dry-run          print metadata and exit without writing\n"
        << "  -h, --help             display help\n\n";
    exit(code);
}

int main(int argc, char* argv[])
{
    // ── Disable HDF5 POSIX file locking FIRST ────────────────────────────
    // Must happen before ANY HDF5 call. H5Eset_auto2 below triggers library
    // initialisation, which reads HDF5_USE_FILE_LOCKING at that point.
    // NTFS/FUSE mounts fail POSIX fcntl locking silently, so H5Fopen returns
    // -1 on valid files if locking is still active when the lib initialises.
#if defined(_WIN32)
    _putenv_s("HDF5_USE_FILE_LOCKING", "FALSE");
#else
    setenv("HDF5_USE_FILE_LOCKING", "FALSE", 1);
#endif

    // Silence HDF5 automatic error printing
    H5Eset_auto2(H5E_DEFAULT, &silentHdf5Error, nullptr);

    string   topologySpec;
    int      groupSize  = DEFAULT_GROUPS;
    long     chunkSamps = DEFAULT_CHUNK;
    bool     noYaml     = false;
    bool     dryRun     = false;

    int i = 1;
    for (; i < argc; ++i) {
        string arg(argv[i]);
        if (arg == "-h" || arg == "--help")    usage(argv[0]);
        else if (arg == "-n" || arg == "--no-yaml")  noYaml  = true;
        else if (arg == "-d" || arg == "--dry-run")  dryRun  = true;
        else if ((arg == "-t" || arg == "--topology") && i + 1 < argc)
            topologySpec = argv[++i];
        else if ((arg == "-g" || arg == "--groups") && i + 1 < argc)
            groupSize = stoi(argv[++i]);
        else if ((arg == "-c" || arg == "--chunk") && i + 1 < argc)
            chunkSamps = stol(argv[++i]);
        else if (arg[0] == '-') {
            cerr << "error: unknown option " << arg
                 << " (type " << argv[0] << " -h for help)\n";
            return EXIT_FAILURE;
        } else break;
    }

    if (argc - i < 2) {
        cerr << "error: expected input.mat and output_basename\n";
        usage(argv[0], EXIT_FAILURE);
    }
    string matPath(argv[i]);
    string outBase(argv[i + 1]);

    // ── open HDF5 file ────────────────────────────────────────────────────
    hid_t file = H5Fopen(matPath.c_str(), H5F_ACC_RDONLY, H5P_DEFAULT);
    if (file < 0) {
        cerr << "error: cannot open " << matPath
             << " (not an HDF5/v7.3 .mat file, or file not found)\n";
        return EXIT_FAILURE;
    }

    // ── discover CRAW_NNN channels ────────────────────────────────────────
    vector<string> crawNames = discoverChannels(file);
    if (crawNames.empty()) {
        cerr << "error: no CRAW_NNN datasets found in " << matPath << "\n";
        H5Fclose(file);
        return EXIT_FAILURE;
    }
    int nChannels = (int)crawNames.size();

    // ── read metadata from first channel ─────────────────────────────────
    string ch0 = crawNames[0];
    double srKhz, bitResUv, gain, tBegin, tEnd;
    try {
        srKhz    = readHdf5Scalar(file, ch0 + "_KHz");
        bitResUv = readHdf5Scalar(file, ch0 + "_BitResolution");
        gain     = readHdf5Scalar(file, ch0 + "_Gain");
        tBegin   = readHdf5Scalar(file, ch0 + "_TimeBegin");
        tEnd     = readHdf5Scalar(file, ch0 + "_TimeEnd");
    } catch (const exception& e) {
        cerr << "error reading metadata: " << e.what() << "\n";
        H5Fclose(file);
        return EXIT_FAILURE;
    }
    int    srHz       = (int)round(srKhz * 1000.0);
    double durationS  = tEnd - tBegin;

    // ── validate sample counts ────────────────────────────────────────────
    hsize_t nSamples = 0;
    for (auto& name : crawNames) {
        hsize_t n = datasetNSamples(file, name);
        nSamples = (nSamples == 0) ? n : min(nSamples, n);
    }

    // ── build topology ────────────────────────────────────────────────────
    vector<Group> groups;
    try {
        groups = topologySpec.empty()
                 ? uniformGroups(nChannels, groupSize)
                 : parseTopology(topologySpec, nChannels);
    } catch (const exception& e) {
        cerr << "error: " << e.what() << "\n";
        H5Fclose(file);
        return EXIT_FAILURE;
    }

    // ── print metadata ────────────────────────────────────────────────────
    cout << "Input             : " << matPath << "\n"
         << "Channels          : " << nChannels
         << "  (" << crawNames.front() << " … " << crawNames.back() << ")\n"
         << "Samples           : " << nSamples << "\n"
         << "Sampling rate     : " << srHz << " Hz\n"
         << "Duration          : " << fixed << setprecision(2)
                                   << durationS / 60.0 << " min  ("
                                   << fixed << setprecision(1) << durationS << " s)\n"
         << "BitResolution     : " << fixed << setprecision(4) << bitResUv << " µV/count\n"
         << "Gain              : " << (int)gain << "\n"
         << "Output .dat       : " << outBase << ".dat\n"
         << "Expected size     : " << fixed << setprecision(2)
                                   << (double)nSamples * nChannels * 2 / 1e9 << " GB\n"
         << "Spike groups      : " << groups.size() << "  (";
    for (size_t gi = 0; gi < groups.size(); ++gi) {
        if (gi) cout << ", ";
        cout << groups[gi].channels.size() << "ch " << groups[gi].type;
    }
    cout << ")\n";

    if (dryRun) {
        cout << "\n[dry-run] Exiting before write.\n";
        H5Fclose(file);
        return EXIT_SUCCESS;
    }

    // ── open .dat output ──────────────────────────────────────────────────
    string datPath = outBase + ".dat";
    FILE* datFp = fopen(datPath.c_str(), "wb");
    if (!datFp) {
        cerr << "error: cannot create " << datPath
             << ": " << strerror(errno) << "\n";
        H5Fclose(file);
        return EXIT_FAILURE;
    }

    // ── streaming write ───────────────────────────────────────────────────
    // Each chunk: read all channels for [offset, offset+chunkSamps), then
    // interleave into (chunkSamps × nChannels) int16 and fwrite in one call.
    //
    // Layout: [ch0_s0, ch1_s0, ..., chN_s0, ch0_s1, ch1_s1, ...]
    // (sample-major, matching every other ndmanager-plugins .dat producer)

    long nChunks = (long)((nSamples + chunkSamps - 1) / chunkSamps);
    cout << "\nWriting " << outBase << ".dat in " << nChunks
         << " chunks of " << chunkSamps << " samples …\n";

    // Channel-major staging buffer: chBuf[ch][sample]
    // Interleaved output buffer:    outBuf[sample * nChannels + ch]
    vector<vector<int16_t>> chBuf(nChannels);
    vector<int16_t>         outBuf;

    hsize_t bytesWritten = 0;
    clock_t t0 = clock();

    for (long ci = 0; ci < nChunks; ++ci) {
        hsize_t offset = (hsize_t)ci * (hsize_t)chunkSamps;
        hsize_t n      = min((hsize_t)chunkSamps, nSamples - offset);

        // Read all channels for this chunk
        for (int ch = 0; ch < nChannels; ++ch) {
            chBuf[ch].resize(n);
            readChannelSlice(file, crawNames[ch], offset, n, chBuf[ch].data());
        }

        // Interleave: outBuf[sample * nChannels + ch] = chBuf[ch][sample]
        outBuf.resize(n * nChannels);
        for (hsize_t s = 0; s < n; ++s)
            for (int ch = 0; ch < nChannels; ++ch)
                outBuf[s * nChannels + ch] = chBuf[ch][s];

        size_t written = fwrite(outBuf.data(), sizeof(int16_t),
                                n * nChannels, datFp);
        if (written != (size_t)(n * nChannels)) {
            cerr << "\nerror: write failed at chunk " << ci + 1 << "\n";
            fclose(datFp);
            H5Fclose(file);
            return EXIT_FAILURE;
        }
        bytesWritten += n * nChannels * sizeof(int16_t);

        double elapsed = (double)(clock() - t0) / CLOCKS_PER_SEC;
        double rateMBs = elapsed > 0 ? bytesWritten / elapsed / 1e6 : 0.0;
        printf("  chunk %4ld/%-4ld  %5.1f%%  %5.2f GB  %4.0f MB/s\r",
               ci + 1, nChunks,
               100.0 * (ci + 1) / nChunks,
               (double)bytesWritten / 1e9,
               rateMBs);
        fflush(stdout);
    }
    fclose(datFp);

    double elapsed = (double)(clock() - t0) / CLOCKS_PER_SEC;
    printf("\nDone. %.3f GB written in %.1f s\n",
           (double)bytesWritten / 1e9, elapsed);

    // ── write YAML ────────────────────────────────────────────────────────
    if (!noYaml) {
        string yamlPath = outBase + ".yaml";
        cout << "\nWriting " << yamlPath << " …\n";
        try {
            writeYaml(yamlPath, outBase, nChannels, srHz, bitResUv,
                      (int)gain, nSamples, durationS, tBegin, tEnd, groups);
        } catch (const exception& e) {
            cerr << "error writing YAML: " << e.what() << "\n";
            H5Fclose(file);
            return EXIT_FAILURE;
        }
        cout << "YAML written  : " << yamlPath << "\n";
    }

    H5Fclose(file);
    cout << "\nConversion complete.\n";
    return EXIT_SUCCESS;
}

/***************************************************************************
                   process_reassignspikes.cpp
                   --------------------------
    copyright  : (C) 2026  Gravios / NeuroSuite-3 contributors
    See process_reassignspikes.h for the model, file conventions and safety
    contract.  This binary APPLIES a frozen, externally-validated adaptation
    model; it does not fit one.  Fitting + per-unit trust gating live in the
    companion Python tool adaptmodel.py, which emits the .adapt artifact.
 ***************************************************************************/

#include "process_reassignspikes.h"
#include "progressbar.h"            // BlockProgress -- defrag-style stage bar

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include <yaml-cpp/yaml.h>

using std::string;
using std::vector;

// ===========================================================================
// Usage / argument parsing  (hand-rolled argv loop, matching the siblings)
// ===========================================================================
static void usage(const char *name, bool full = false)
{
    fprintf(stderr,
        "usage: %s -m model.adapt [options] basename\n", name);
    if (!full) return;
    fprintf(stderr,
        "\n"
        "  Reassign history-dependent (burst-decremented) spikes to the unit\n"
        "  whose state-conditioned waveform model fits best.  Reads the frozen\n"
        "  .adapt model from adaptmodel.py; only units present there are\n"
        "  eligible (all others are NO-TOUCH).  Dry-run unless --commit.\n\n"
        "  -m FILE   model artifact (.adapt)                      [required]\n"
        "  -p FILE   session YAML parameter file (geometry, rate)\n"
        "  -s RATE   sampling rate (Hz) if no YAML\n"
        "  -g LIST   comma-separated 1-based groups (default: all in model)\n"
        "  -r N      refractory veto in samples (0 = off)         [0]\n"
        "  -a PCT    only contest spikes below this within-unit\n"
        "            amplitude percentile                          [50]\n"
        "  -k W      curated-label prior weight (>0 favours staying)[1.0]\n"
        "  -M G      minimum score gain required to move a spike   [0.0]\n"
        "  -o SUF    output stem suffix                            [%s]\n"
        "  --commit  write the reassigned .clu.N (else dry-run log only)\n"
        "  -v        verbose\n"
        "  -h        this help\n",
        DEFAULT_OUT_SUFFIX);
}

static bool parseArgs(int argc, char **argv, ReassignArgs &a)
{
    bool haveBase = false;
    for (int i = 1; i < argc; ++i) {
        const string arg = argv[i];
        if      (arg == "-h" || arg == "--help") { usage(argv[0], true); exit(0); }
        else if (arg == "--commit")              a.commit = true;
        else if (arg == "-v")                    a.verbose = true;
        else if (arg == "-m" && i+1 < argc)      a.modelPath  = argv[++i];
        else if (arg == "-p" && i+1 < argc)      a.parPath    = argv[++i];
        else if (arg == "-o" && i+1 < argc)      a.outSuffix  = argv[++i];
        else if (arg == "-s" && i+1 < argc)      a.samplingRate  = atof(argv[++i]);
        else if (arg == "-r" && i+1 < argc)      a.refractorySamp = atoi(argv[++i]);
        else if (arg == "-a" && i+1 < argc)      a.ampPercentile = atof(argv[++i]);
        else if (arg == "-k" && i+1 < argc)      a.priorWeight   = atof(argv[++i]);
        else if (arg == "-M" && i+1 < argc)      a.scoreMargin   = atof(argv[++i]);
        else if (arg == "-g" && i+1 < argc) {
            string s = argv[++i], tok;
            for (size_t p = 0; p <= s.size(); ++p) {
                if (p == s.size() || s[p] == ',') {
                    if (!tok.empty()) a.groups.push_back(atoi(tok.c_str()));
                    tok.clear();
                } else tok += s[p];
            }
        }
        else if (arg[0] != '-') { a.basename = arg; haveBase = true; }
        else { fprintf(stderr, "unknown option: %s\n", arg.c_str()); return false; }
    }
    if (a.modelPath.empty()) { fprintf(stderr, "error: -m model.adapt required\n"); return false; }
    return haveBase;
}

// ===========================================================================
// YAML geometry  (mirrors process_drifttracker's loader; tolerant)
// ===========================================================================
static void loadSamplingRateFromYaml(const string &path, double &samplingRate)
{
    if (path.empty() || samplingRate > 0.0) return;
    YAML::Node root;
    try { root = YAML::LoadFile(path); }
    catch (const YAML::Exception &e) {
        fprintf(stderr, "[reassignspikes] warning: cannot parse '%s': %s\n",
                path.c_str(), e.what());
        return;
    }
    try {
        const auto &acq = root["acquisitionSystem"];
        if (acq && acq.IsMap() && acq["samplingRate"])
            samplingRate = acq["samplingRate"].as<double>(0.0);
    } catch (...) {}
}

// ===========================================================================
// Binary file I/O  (neurosuite-3 conventions, see header)
// ===========================================================================
static bool readRes(const string &path, vector<int64_t> &out)
{
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return false;
    const std::streamsize n = f.tellg();
    f.seekg(0);
    out.resize(n / (std::streamsize)sizeof(int64_t));
    return bool(f.read(reinterpret_cast<char*>(out.data()), n));
}

static bool readClu(const string &path, vector<int32_t> &ids, int32_t &nClusters)
{
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return false;
    const std::streamsize n = f.tellg();
    f.seekg(0);
    vector<int32_t> raw(n / (std::streamsize)sizeof(int32_t));
    if (!f.read(reinterpret_cast<char*>(raw.data()), n)) return false;
    if (raw.empty()) { nClusters = 0; ids.clear(); return true; }
    nClusters = raw[0];                       // int32 header
    ids.assign(raw.begin() + 1, raw.end());
    return true;
}

static bool writeClu(const string &path, const vector<int32_t> &ids, int32_t nClusters)
{
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(reinterpret_cast<const char*>(&nClusters), sizeof(int32_t));
    f.write(reinterpret_cast<const char*>(ids.data()),
            (std::streamsize)ids.size() * sizeof(int32_t));
    return bool(f);
}

static bool readSpk(const string &path, int nSamples, int nChan,
                    vector<int16_t> &out, size_t &nSpk)
{
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return false;
    const std::streamsize n = f.tellg();
    f.seekg(0);
    out.resize(n / (std::streamsize)sizeof(int16_t));
    if (!f.read(reinterpret_cast<char*>(out.data()), n)) return false;
    const size_t per = (size_t)nSamples * (size_t)nChan;
    nSpk = per ? out.size() / per : 0;
    return true;
}

// ===========================================================================
// Model artifact (.adapt) loader
// ===========================================================================
static bool readModel(const string &path, vector<ModelUnit> &units)
{
    std::ifstream f(path, std::ios::binary);
    if (!f) { fprintf(stderr, "error: cannot open model '%s'\n", path.c_str()); return false; }
    char magic[4];
    f.read(magic, 4);
    if (std::strncmp(magic, ADAPT_MAGIC, 4) != 0) {
        fprintf(stderr, "error: '%s' is not an .adapt model\n", path.c_str());
        return false;
    }
    int32_t version = 0, nUnits = 0;
    f.read(reinterpret_cast<char*>(&version), sizeof(int32_t));
    f.read(reinterpret_cast<char*>(&nUnits),  sizeof(int32_t));
    if (version != ADAPT_VERSION)
        fprintf(stderr, "[reassignspikes] warning: model version %d (expected %d)\n",
                version, ADAPT_VERSION);
    units.clear();
    for (int u = 0; u < nUnits; ++u) {
        ModelUnit m;
        int32_t g, c, ns, nc;
        f.read(reinterpret_cast<char*>(&g),  sizeof(int32_t));
        f.read(reinterpret_cast<char*>(&c),  sizeof(int32_t));
        f.read(reinterpret_cast<char*>(&ns), sizeof(int32_t));
        f.read(reinterpret_cast<char*>(&nc), sizeof(int32_t));
        f.read(reinterpret_cast<char*>(&m.tauFms), sizeof(double));
        f.read(reinterpret_cast<char*>(&m.uF),     sizeof(double));
        f.read(reinterpret_cast<char*>(&m.tauSms), sizeof(double));
        f.read(reinterpret_cast<char*>(&m.uS),     sizeof(double));
        m.group = g; m.cluster = c; m.nSamples = ns; m.nChan = nc;
        m.C.resize((size_t)3 * ns * nc);
        f.read(reinterpret_cast<char*>(m.C.data()),
               (std::streamsize)m.C.size() * sizeof(float));
        if (!f) { fprintf(stderr, "error: truncated model at unit %d\n", u); return false; }
        units.push_back(std::move(m));
    }
    return true;
}

// ===========================================================================
// Recovery state  (causal; mirrors adaptmodel.recovery_states exactly)
// ===========================================================================
// Precomputed per-unit timeline for O(log n) availability queries: stores the
// post-spike availability after each spike so a query time recovers from the
// nearest preceding spike.
struct UnitTimeline {
    vector<int64_t> t;          // spike times (samples), ascending
    vector<double>  postF, postS;
};

static void buildTimeline(const vector<int64_t> &times, double sr,
                          double tauFms, double uF, double tauSms, double uS,
                          UnitTimeline &tl)
{
    const double tauF = tauFms / 1000.0, tauS = tauSms / 1000.0;
    tl.t = times;
    const size_t n = times.size();
    tl.postF.resize(n); tl.postS.resize(n);
    double aF = 1.0, aS = 1.0;
    double pF = aF * (1.0 - uF), pS = aS * (1.0 - uS);
    if (n) { tl.postF[0] = pF; tl.postS[0] = pS; }
    for (size_t i = 1; i < n; ++i) {
        const double isi = (double)(times[i] - times[i-1]) / sr;
        aF = 1.0 - (1.0 - pF) * std::exp(-isi / tauF); pF = aF * (1.0 - uF);
        aS = 1.0 - (1.0 - pS) * std::exp(-isi / tauS); pS = aS * (1.0 - uS);
        tl.postF[i] = pF; tl.postS[i] = pS;
    }
}

// Availability at query time tq, recovering from the spike strictly before it.
// excludeSelf: ignore a spike exactly at tq (the spike being scored against its
// own current unit), so it doesn't deplete itself.
static void availabilityAt(const UnitTimeline &tl, int64_t tq, double sr,
                           double tauFms, double tauSms,
                           double &a, double &g)
{
    const double tauF = tauFms / 1000.0, tauS = tauSms / 1000.0;
    // last index with t < tq
    long lo = 0, hi = (long)tl.t.size() - 1, idx = -1;
    while (lo <= hi) {
        long mid = (lo + hi) / 2;
        if (tl.t[mid] < tq) { idx = mid; lo = mid + 1; }
        else hi = mid - 1;
    }
    if (idx < 0) { a = g = 1.0; return; }
    const double isi = (double)(tq - tl.t[idx]) / sr;
    a = 1.0 - (1.0 - tl.postF[idx]) * std::exp(-isi / tauF);
    g = 1.0 - (1.0 - tl.postS[idx]) * std::exp(-isi / tauS);
}

// ===========================================================================
// Scoring  (v0: single pass, state-conditioned residual + refractory veto)
// ===========================================================================
static double residualToModel(const int16_t *wave, const ModelUnit &m,
                              double a, double g)
{
    const size_t P = (size_t)m.nSamples * m.nChan;
    const float *C0 = m.C.data();
    const float *C1 = C0 + P;
    const float *C2 = C1 + P;
    double sse = 0.0;
    for (size_t p = 0; p < P; ++p) {
        const double pred = C0[p] + (1.0 - a) * C1[p] + (1.0 - g) * C2[p];
        const double d = (double)wave[p] - pred;
        sse += d * d;
    }
    return sse;
}

// Nearest-neighbour ISI to tq within a timeline (samples); LLONG_MAX if none.
static int64_t nearestISI(const UnitTimeline &tl, int64_t tq)
{
    if (tl.t.empty()) return INT64_MAX;
    long lo = 0, hi = (long)tl.t.size() - 1, idx = -1;
    while (lo <= hi) { long mid=(lo+hi)/2; if (tl.t[mid] < tq){idx=mid;lo=mid+1;} else hi=mid-1; }
    int64_t best = INT64_MAX;
    if (idx >= 0)                         best = std::min(best, tq - tl.t[idx]);
    if (idx + 1 < (long)tl.t.size())      best = std::min(best, tl.t[idx+1] - tq);
    return best;
}

// ===========================================================================
// Per-group reassignment
// ===========================================================================
struct Move { size_t spikeIndex; int from; int to; double gain; int64_t t; };

static int reassignGroup(int grp, const ReassignArgs &args,
                         const vector<ModelUnit> &model,
                         std::ofstream &logf, BlockProgress &prog)
{
    const string base = args.basename;
    const string sfx  = "." + std::to_string(grp);
    vector<int64_t> res;
    vector<int16_t> spk; size_t nSpk = 0;
    vector<int32_t> clu; int32_t nClusters = 0;

    // model units for this group
    vector<const ModelUnit*> mu;
    for (const auto &m : model) if (m.group == grp) mu.push_back(&m);
    if (mu.empty()) return 0;                       // nothing trusted here
    const int nSamples = mu[0]->nSamples, nChan = mu[0]->nChan;

    if (!readRes(base + "." RES_EXT + sfx, res) ||
        !readClu(base + "." CLU_EXT + sfx, clu, nClusters) ||
        !readSpk(base + "." SPK_EXT + sfx, nSamples, nChan, spk, nSpk)) {
        fprintf(stderr, "[reassignspikes] group %d: missing/short files, skipping\n", grp);
        return 0;
    }
    const size_t n = std::min(res.size(), std::min(clu.size(), nSpk));
    const size_t P = (size_t)nSamples * nChan;

    // index trusted units by cluster id; build their timelines + noise var
    auto findModel = [&](int cid) -> const ModelUnit* {
        for (auto *m : mu) if (m->cluster == cid) return m;
        return nullptr;
    };
    vector<UnitTimeline> tl(mu.size());
    vector<double> sigma2(mu.size(), 1.0);
    vector<int>    domChan(mu.size(), 0);
    for (size_t k = 0; k < mu.size(); ++k) {
        vector<int64_t> times;
        for (size_t i = 0; i < n; ++i) if (clu[i] == mu[k]->cluster) times.push_back(res[i]);
        buildTimeline(times, args.samplingRate,
                      mu[k]->tauFms, mu[k]->uF, mu[k]->tauSms, mu[k]->uS, tl[k]);
        // dominant channel from C0 peak-to-peak
        const float *C0 = mu[k]->C.data();
        double bestptp = -1; int bestc = 0;
        for (int c = 0; c < nChan; ++c) {
            double mn = 1e30, mx = -1e30;
            for (int s = 0; s < nSamples; ++s) { double v = C0[(size_t)s*nChan + c]; mn=std::min(mn,v); mx=std::max(mx,v); }
            if (mx - mn > bestptp) { bestptp = mx - mn; bestc = c; }
        }
        domChan[k] = bestc;
        // noise variance from this unit's own residuals
        double acc = 0; size_t cnt = 0;
        for (size_t i = 0; i < n; ++i) if (clu[i] == mu[k]->cluster) {
            double a, g; availabilityAt(tl[k], res[i], args.samplingRate,
                                        mu[k]->tauFms, mu[k]->tauSms, a, g);
            acc += residualToModel(&spk[i*P], *mu[k], a, g); cnt++;
        }
        if (cnt) sigma2[k] = std::max(1.0, acc / (double)(cnt * P));
    }

    // per-unit amplitude percentile threshold => which spikes are "contested"
    // (only the low-amplitude tail of each trusted unit is eligible to move)
    auto ampThreshold = [&](int kidx) -> double {
        vector<double> amps;
        for (size_t i = 0; i < n; ++i) if (clu[i] == mu[kidx]->cluster) {
            const int16_t *w = &spk[i*P];
            double mn=1e30,mx=-1e30;
            for (int s=0;s<nSamples;++s){ double v=w[(size_t)s*nChan+domChan[kidx]]; mn=std::min(mn,v); mx=std::max(mx,v); }
            amps.push_back(mx-mn);
        }
        if (amps.empty()) return 1e30;
        std::sort(amps.begin(), amps.end());
        size_t q = (size_t)std::floor(args.ampPercentile/100.0 * (amps.size()-1));
        return amps[q];
    };
    vector<double> ampThr(mu.size());
    for (size_t k=0;k<mu.size();++k) ampThr[k]=ampThreshold((int)k);

    prog.beginStage("G" + std::to_string(grp), (long long)n);

    vector<Move> moves;
    for (size_t i = 0; i < n; ++i) {
        prog.setPosition((long long)i);
        const int cur = clu[i];
        const ModelUnit *mc = findModel(cur);
        if (!mc) continue;                          // current unit NO-TOUCH
        // contest only the low-amplitude tail of the current unit
        size_t kcur = 0; for (size_t k=0;k<mu.size();++k) if (mu[k]->cluster==cur) kcur=k;
        const int16_t *w = &spk[i*P];
        double mn=1e30,mx=-1e30;
        for (int s=0;s<nSamples;++s){ double v=w[(size_t)s*nChan+domChan[kcur]]; mn=std::min(mn,v); mx=std::max(mx,v); }
        if (mx - mn > ampThr[kcur]) continue;       // not in the contested tail

        // score every trusted candidate (including the current unit)
        double bestScore = -1e300; int bestK = -1;
        double curScore = 0;
        for (size_t k = 0; k < mu.size(); ++k) {
            double a, g;
            availabilityAt(tl[k], res[i], args.samplingRate,
                           mu[k]->tauFms, mu[k]->tauSms, a, g);
            // refractory veto for a genuine move into a different unit
            if ((int)mu[k]->cluster != cur && args.refractorySamp > 0 &&
                nearestISI(tl[k], res[i]) < (int64_t)args.refractorySamp)
                continue;
            const double resid = residualToModel(w, *mu[k], a, g);
            double sc = -0.5 * resid / sigma2[k]
                        - 0.5 * (double)P * std::log(sigma2[k]);
            if ((int)mu[k]->cluster != cur) sc -= args.priorWeight;   // stay-prior
            if ((int)mu[k]->cluster == cur) curScore = sc;
            if (sc > bestScore) { bestScore = sc; bestK = (int)k; }
        }
        if (bestK < 0) continue;
        const int to = mu[bestK]->cluster;
        if (to != cur && (bestScore - curScore) > args.scoreMargin)
            moves.push_back({i, cur, to, bestScore - curScore, res[i]});
    }
    prog.endStage();

    // log + (optionally) apply
    for (const auto &mv : moves)
        logf << grp << '\t' << mv.t << '\t' << mv.from << " -> " << mv.to
             << "\tgain=" << mv.gain << '\n';

    if (args.commit && !moves.empty()) {
        vector<int32_t> out = clu;
        for (const auto &mv : moves) out[mv.spikeIndex] = mv.to;
        const string outClu = base + args.outSuffix + "." CLU_EXT + sfx;
        if (!writeClu(outClu, out, nClusters))
            fprintf(stderr, "[reassignspikes] group %d: failed to write %s\n",
                    grp, outClu.c_str());
    }
    if (args.verbose)
        fprintf(stderr, "[reassignspikes] group %d: %zu contested move(s)%s\n",
                grp, moves.size(), args.commit ? " (committed)" : " (dry-run)");
    return (int)moves.size();
}

// ===========================================================================
int main(int argc, char **argv)
{
    ReassignArgs args;
    if (!parseArgs(argc, argv, args)) { usage(argv[0]); return 1; }

    vector<ModelUnit> model;
    if (!readModel(args.modelPath, model)) return 1;
    if (model.empty()) { fprintf(stderr, "error: model has no trusted units\n"); return 1; }

    loadSamplingRateFromYaml(args.parPath, args.samplingRate);
    if (args.samplingRate <= 0.0) {
        fprintf(stderr, "error: sampling rate unknown (give -p YAML or -s RATE)\n");
        return 1;
    }

    // groups: requested, else every group present in the model
    vector<int> groups = args.groups;
    if (groups.empty()) {
        for (const auto &m : model)
            if (std::find(groups.begin(), groups.end(), m.group) == groups.end())
                groups.push_back(m.group);
        std::sort(groups.begin(), groups.end());
    }

    const string logPath = args.basename + args.outSuffix + ".clu.log";
    std::ofstream logf(logPath);
    logf << "# process_reassignspikes " << (args.commit ? "COMMIT" : "DRY-RUN")
         << "  model=" << args.modelPath << "\n# grp\ttime\tfrom -> to\tgain\n";

    BlockProgress prog(args.basename);
    int total = 0;
    for (int g : groups) total += reassignGroup(g, args, model, logf, prog);
    prog.finish();

    fprintf(stderr, "[reassignspikes] %d proposed move(s) across %zu group(s); log: %s%s\n",
            total, groups.size(), logPath.c_str(),
            args.commit ? "" : "  (dry-run: no .clu written, pass --commit to apply)");
    return 0;
}

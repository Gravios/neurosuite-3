// custody.hpp — single source of truth for neurosuite-3 chain-of-custody file
// relationships.  Header-only and DEPENDENCY-FREE (std + <fstream> only; no Qt,
// no yaml, no libneurosuite-core linkage) so every program can use it by
// #include alone — the Qt apps (Klusters, Neuroscope) and the standalone
// ndmanager-plugins binaries that do not link the core library alike.
//
// The value here is the POLICY, not the string juggling: which artifact types
// are method-specific, which are shared across methods, and which are
// session-wide.  Centralising it means no call site ever again decides
// strict-vs-fallback resolution by hand.
//
//   Naming model:  <base>.<type>.<method>.<group>      (per-group, method-tagged)
//                  <base>.<type>.<group>               (per-group, untagged/legacy)
//                  <base>.<type>                        (session-wide: fil/dat/...)
//
//   Artifact classes:
//     MethodSpecific (clu, fet, pca, col, model, klg, …) — strict: the file is
//         exactly <base>.<type>.<method>.<group>, no fallback.
//     Shared (res, spk) — one physical copy across methods: prefer the
//         method-tagged path, then .standard, then untagged.  (Spike times are
//         method-independent; the raw .spk is shared, the stderiv transform
//         being applied downstream at PCA time rather than stored separately.)
//     SessionWide (fil, dat, xml, yaml, nrs, par) — <base>.<type>, no method,
//         no group.
//
// Keep this file in lock-step with the bash (ndm_custody) and Python
// (ndm_resolve_io) mirrors via the shared conformance vectors.

#ifndef NEUROSUITE_CUSTODY_HPP
#define NEUROSUITE_CUSTODY_HPP

#include <filesystem>
#include <string>
#include <vector>
#include <fstream>

namespace neurosuite {
namespace custody {

enum class Klass { MethodSpecific, Shared, SessionWide };

inline const char* kDefaultMethod() { return "standard"; }

// ── method tokens ───────────────────────────────────────────────────────────
// A method token is <family>[_<kind><order>], e.g. "stderiv_C4":
//   family  the engine — standard | sdiff | stderiv
//   kind    'S' the plain methodOrder spatial derivative, 'C' the session's
//           custom difference pattern (sdiffPairs); absent on a bare token
//   order   the spatial-derivative order actually applied; -1 when absent
// Recording kind+order in the NAME is what lets a consumer know how a file was
// produced without re-reading session parameters that may have been edited
// since.  A token that does not match the suffix grammar is opaque: the whole
// string is the family, so unknown tokens never masquerade as a known one.
struct MethodSpec {
    std::string family;
    char        kind;    // 'S', 'C', or '\0' when absent
    int         order;   // -1 when absent
};

inline MethodSpec parseMethodToken(const std::string& m)
{
    MethodSpec spec;
    spec.family = m;
    spec.kind   = '\0';
    spec.order  = -1;
    const std::string::size_type u = m.rfind('_');
    if (u == std::string::npos) return spec;
    const std::string suffix = m.substr(u + 1);
    if (suffix.size() < 2) return spec;
    if (suffix[0] != 'S' && suffix[0] != 'C') return spec;
    int val = 0;
    for (std::string::size_type i = 1; i < suffix.size(); ++i) {
        if (suffix[i] < '0' || suffix[i] > '9') return spec;
        val = val * 10 + (suffix[i] - '0');
    }
    spec.family = m.substr(0, u);
    spec.kind   = suffix[0];
    spec.order  = val;
    return spec;
}

// True for the stderiv family: the bare legacy token and any stderiv_<kind><order>.
// Deliberately not a plain "stderiv" prefix test — "stderivfoo" is a different
// family and must not be treated as the transformed domain.
inline bool isStderivMethod(const std::string& m)
{ return parseMethodToken(m).family == "stderiv"; }

// ── type classification ─────────────────────────────────────────────────────

// Per-group types that participate in chain-of-custody naming.
inline bool isPerGroupType(const std::string& type)
{
    return type == "res" || type == "spk" || type == "clu" ||
           type == "clc" ||                                  // microfiber / pure-shape children (writeClu format)
           type == "fet" || type == "pca" || type == "col" ||
           type == "model" || type == "klg";
}

// Session-wide types (no method, no group).
inline bool isSessionWideType(const std::string& type)
{
    return type == "fil" || type == "dat" || type == "xml" ||
           type == "yaml" || type == "nrs" || type == "par" ||
           type == "eeg" || type == "lfp";
}

// Any token the parser may legitimately see in the type slot.
inline bool isKnownType(const std::string& token)
{
    return isPerGroupType(token) || isSessionWideType(token);
}

// Positional cluster-id files: .clu (fibers / general units) and .clc (their
// microfiber pure-shape children) share the writeClu binary format (int32
// nClusters header + per-spike int32 ids, aligned to .res) and are both loaded
// as the cluster source.  The policy of which types ARE cluster-id files lives
// here so no call site decides it by hand.
inline bool isClusterIdType(const std::string& type)
{
    return type == "clu" || type == "clc";
}

inline Klass classify(const std::string& type)
{
    if (isSessionWideType(type))           return Klass::SessionWide;
    if (type == "res" || type == "spk")    return Klass::Shared;
    return Klass::MethodSpecific;          // clu/fet/pca/col/model/klg/…
}

// ── path composition ────────────────────────────────────────────────────────

inline std::string methodPath(const std::string& base, const std::string& type,
                              const std::string& method, int group)
{
    return base + "." + type + "." + method + "." + std::to_string(group);
}

inline std::string untaggedPath(const std::string& base, const std::string& type,
                                int group)
{
    return base + "." + type + "." + std::to_string(group);
}

inline std::string sessionPath(const std::string& base, const std::string& type)
{
    return base + "." + type;
}

// ── small utilities ─────────────────────────────────────────────────────────

inline bool fileExists(const std::string& path)
{
    return std::ifstream(path).good();
}

inline std::vector<std::string> splitDots(const std::string& s)
{
    std::vector<std::string> out;
    std::string::size_type start = 0, dot;
    while ((dot = s.find('.', start)) != std::string::npos) {
        out.push_back(s.substr(start, dot - start));
        start = dot + 1;
    }
    out.push_back(s.substr(start));
    return out;
}

inline std::string baseName(const std::string& path)
{
    const std::string::size_type slash = path.find_last_of("/\\");
    return (slash == std::string::npos) ? path : path.substr(slash + 1);
}

inline bool isAllDigits(const std::string& s)
{
    return !s.empty() &&
           s.find_first_not_of("0123456789") == std::string::npos;
}

// ── method parsing ──────────────────────────────────────────────────────────

// Extract the method token from a per-group path <base>.<type>.<method>.<group>.
// Returns "" for an untagged name <base>.<type>.<group> (robust against a base
// that itself contains dots: the slot before the group is only the method if it
// is NOT a known type token).
inline std::string methodOf(const std::string& path)
{
    const std::vector<std::string> p = splitDots(baseName(path));
    if (p.size() < 3) return "";              // need at least base.type.group
    if (!isAllDigits(p.back())) return "";    // last field must be the group
    const std::string& cand = p[p.size() - 2];
    if (isKnownType(cand)) return "";         // <base>.<type>.<group> (untagged)
    return cand;                              // <base>.<type>.<method>.<group>
}

// ── full anchor parse ───────────────────────────────────────────────────────

struct Anchor {
    std::string base;     // session base (may contain dots)
    std::string type;     // clu / spk / …
    std::string method;   // "" if untagged
    int         group = -1;
    std::string suffix;   // trailing post-group tokens, e.g. ".drift" (no leading dot)
    bool        ok = false;
};

// Parse <base>.<type>.<method>.<grp>[.<suffix>] (or untagged / with suffix).
// `path` is typically a .clu file the user opened; the parser finds the LAST
// known type token, then [.<method>].<grp>[.<suffix>].
inline Anchor parseAnchor(const std::string& path)
{
    Anchor a;
    const std::string name = baseName(path);
    const std::vector<std::string> p = splitDots(name);

    // Find the last known type token (scan from the right, but require at least
    // one base field before it).
    long typeIdx = -1;
    for (long i = static_cast<long>(p.size()) - 2; i >= 1; --i) {
        if (isKnownType(p[i])) { typeIdx = i; break; }
    }
    if (typeIdx < 0) return a;                  // ok stays false

    a.type = p[typeIdx];
    a.base = p[0];
    for (long i = 1; i < typeIdx; ++i) a.base += "." + p[i];

    // After the type token: [.<method>].<grp>[.<suffix>].
    long idx = typeIdx + 1;
    if (idx >= static_cast<long>(p.size())) return a;
    if (!isAllDigits(p[idx])) {                 // method token present
        a.method = p[idx];
        ++idx;
    }
    if (idx >= static_cast<long>(p.size()) || !isAllDigits(p[idx])) return a;
    a.group = std::stoi(p[idx]);
    ++idx;
    for (long i = idx; i < static_cast<long>(p.size()); ++i) {
        if (i > idx) a.suffix += ".";
        a.suffix += p[i];
    }
    a.ok = true;
    return a;
}

// ── resolution ──────────────────────────────────────────────────────────────

struct Resolved {
    std::string path;     // composed path (first existing, or the canonical one)
    std::string method;   // method of the resolved file ("" if untagged/session)
    bool        found = false;
};

// Resolve an input by class:
//   SessionWide    -> <base>.<type>            (no method/group)
//   MethodSpecific -> <base>.<type>.<method>.<grp>   (strict; no fallback)
//   Shared         -> prefer <method>, then standard, then untagged; first that
//                     exists (else the method-tagged path, for diagnostics).
inline Resolved resolve(const std::string& base, const std::string& type,
                        int group, const std::string& method)
{
    Resolved r;
    switch (classify(type)) {
    case Klass::SessionWide: {
        r.path  = sessionPath(base, type);
        r.found = fileExists(r.path);
        return r;
    }
    case Klass::MethodSpecific: {
        r.path   = methodPath(base, type, method, group);
        r.method = method;
        r.found  = fileExists(r.path);
        return r;
    }
    case Klass::Shared:
    default: {
        std::vector<std::string> cands;
        cands.push_back(methodPath(base, type, method, group));
        if (method != kDefaultMethod())
            cands.push_back(methodPath(base, type, kDefaultMethod(), group));
        cands.push_back(untaggedPath(base, type, group));
        for (const std::string& c : cands) {
            if (fileExists(c)) {
                r.path   = c;
                r.method = methodOf(c);
                r.found  = true;
                return r;
            }
        }
        r.path   = cands.front();   // method-tagged, for the error message
        r.method = method;
        r.found  = false;
        return r;
    }
    }
}

// Convenience: true if the resolved file is in the stderiv (transformed) domain.
// Reads the method off the file actually resolved — so a Shared .spk that fell
// back to the raw copy is NOT stderiv even on a stderiv session.
// Resolve a SHARED artifact across EVERY method.  Spike times (.res) and the raw
// .spk are one physical copy whatever produced them, so which token wrote the file
// is not knowable from the caller's own method: detection may run at stderiv while
// extraction and alignment run at stderiv_C5.  resolve() only walks
// method -> standard -> untagged and therefore misses another method's copy --
// including any suffixed token, which no fixed list can enumerate.
//
// Order: preferred method, then standard / stderiv / sdiff, then any other
// method-tagged copy present in the directory, then the untagged legacy name.
// Mirrors ndm_resolve_any in ndm_custody; keep the two in step.
inline Resolved resolveAny(const std::string& base, const std::string& type,
                           int group, const std::string& preferred = std::string())
{
    Resolved r;
    std::vector<std::string> order;
    if (!preferred.empty()) order.push_back(preferred);
    for (const char* m : { "standard", "stderiv", "sdiff" })
        if (preferred != m) order.push_back(m);
    for (const std::string& m : order) {
        const std::string cand = methodPath(base, type, m, group);
        if (fileExists(cand)) { r.path = cand; r.method = m; r.found = true; return r; }
    }

    // Any other method-tagged copy: <base>.<type>.<anything>.<group>.  Scanning the
    // directory is what makes suffixed tokens work without hard-coding them.
    namespace fs = std::filesystem;
    const fs::path bp(base);
    const std::string stem   = bp.filename().string() + "." + type + ".";
    const std::string suffix = "." + std::to_string(group);
    const fs::path dir = bp.has_parent_path() ? bp.parent_path() : fs::path(".");
    std::error_code ec;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        const std::string name = it->path().filename().string();
        if (name.size() <= stem.size() + suffix.size())               continue;
        if (name.compare(0, stem.size(), stem) != 0)                  continue;
        if (name.compare(name.size() - suffix.size(), suffix.size(), suffix) != 0) continue;
        const std::string m = name.substr(stem.size(),
                                          name.size() - stem.size() - suffix.size());
        if (m.empty() || m.find('.') != std::string::npos) continue;  // not a bare token
        r.path = it->path().string(); r.method = m; r.found = true;
        return r;
    }

    const std::string untagged = untaggedPath(base, type, group);
    if (fileExists(untagged)) { r.path = untagged; r.found = true; return r; }

    r.path = methodPath(base, type, preferred.empty() ? kDefaultMethod() : preferred.c_str(), group);
    r.method = preferred;
    r.found = false;
    return r;
}

inline bool resolvedIsStderiv(const Resolved& r) { return isStderivMethod(r.method); }

// ── derivation relationships ────────────────────────────────────────────────

// Per-group artifacts invalidated when the waveforms are realigned (their old
// position/feature values become stale).  Wrappers use this to archive
// downstream files after a realign instead of hand-rolling the list.
inline std::vector<std::string> staleAfterRealign()
{
    return { "fet", "pca" };
}

} // namespace custody
} // namespace neurosuite

#endif // NEUROSUITE_CUSTODY_HPP

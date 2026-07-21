// sdiff_pairs.h — shared parser for SDIFF_CUSTOM custom difference patterns.
//
// A custom spatial-derivative pattern is given as "a-b,c-d,..." where each token
// is a pair of 0-based within-group channel positions (matching the 0-based
// channel indexing of the .dat frame and .fet columns).  It is parsed into a
// per-channel partner map: partner[a] = b, so output channel a becomes
// x[a] - x[b].  The pattern must form a spanning tree with exactly one root (a
// position that is never a source), and the root must be the last position so
// that its (redundant) output is the last channel — the one process_pca_stderiv
// drops via SDIFF_PASS, exactly as for orders 1 and 3.
//
// Patterns are per spike group.  parseSdiffPairsPerGroup() parses a colon-
// separated spec "g1pat:g2pat:..." — commas separate pairs within a group,
// colons separate groups, aligned one-to-one with the colon-separated channel
// groups of -c — into one partner map per group.  An empty segment means that
// group carries no custom pattern and keeps the base spatial-derivative order.
//
// Header-only (inline) so process_extractspikes_stderiv and
// process_reextractspikes_stderiv share a single implementation without a
// separate translation unit.  On malformed input it prints to stderr and exits.
#ifndef KLUSTERSSHARED_SDIFF_PAIRS_H
#define KLUSTERSSHARED_SDIFF_PAIRS_H

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

// When `ok` is non-null the parser never exits: any malformed input sets *ok=false
// and returns an empty map (for GUI callers such as klusters, which must not abort
// on a bad session string).  With ok==nullptr (the extractor's use) a malformed
// pattern prints to stderr and exits, exactly as before.
inline std::vector<int> parseSdiffPairs(const char *spec, bool *ok = nullptr)
{
    std::vector<int> src, dst, partner;
    int maxPos = 0;
    std::string s(spec ? spec : "");
    size_t p = 0;
    while(true) {
        size_t c   = s.find(',', p);
        std::string tok = (c == std::string::npos) ? s.substr(p) : s.substr(p, c - p);
        if(!tok.empty()) {
            size_t dash = tok.find('-');
            if(dash == std::string::npos) {
                if(ok) { *ok = false; return std::vector<int>(); }
                std::cerr << "error: bad sdiffPairs token '" << tok << "' (want a-b).\n";
                exit(1);
            }
            int a = atoi(tok.substr(0, dash).c_str());
            int b = atoi(tok.substr(dash + 1).c_str());
            if(a < 0 || b < 0 || a == b) {
                if(ok) { *ok = false; return std::vector<int>(); }
                std::cerr << "error: bad sdiffPairs token '" << tok << "'.\n";
                exit(1);
            }
            src.push_back(a);
            dst.push_back(b);
            if(a + 1 > maxPos) maxPos = a + 1;
            if(b + 1 > maxPos) maxPos = b + 1;
        }
        if(c == std::string::npos) break;
        p = c + 1;
    }
    if(src.empty()) {
        if(ok) { *ok = false; return std::vector<int>(); }
        std::cerr << "error: empty sdiffPairs.\n"; exit(1);
    }

    partner.assign(maxPos, -1);
    for(size_t k = 0; k < src.size(); k++) {
        if(partner[src[k]] != -1) {
            if(ok) { *ok = false; return std::vector<int>(); }
            std::cerr << "error: sdiffPairs channel " << src[k]
                      << " has two difference targets.\n";
            exit(1);
        }
        partner[src[k]] = dst[k];
    }

    // Exactly one root, and it must be the last position.
    std::vector<int> roots;
    for(int idx = 0; idx < maxPos; idx++)
        if(partner[idx] == -1) roots.push_back(idx);
    if(roots.size() != 1) {
        if(ok) { *ok = false; return std::vector<int>(); }
        std::cerr << "error: sdiffPairs must form a spanning tree with exactly one "
                     "root; got " << roots.size() << " channels with no target.\n";
        exit(1);
    }
    if(roots[0] != maxPos - 1) {
        if(ok) { *ok = false; return std::vector<int>(); }
        std::cerr << "error: sdiffPairs root channel " << roots[0]
                  << " must be the last position (" << maxPos - 1 << ").\n";
        exit(1);
    }
    int pred = -1;                                   // wire the root to a predecessor
    for(int j = 0; j < maxPos; j++)
        if(partner[j] == roots[0]) { pred = j; break; }
    if(pred < 0) {
        if(ok) { *ok = false; return std::vector<int>(); }
        std::cerr << "error: sdiffPairs root is disconnected.\n"; exit(1);
    }
    partner[roots[0]] = pred;

    if(ok) *ok = true;
    return partner;
}

// Split "g1pat:g2pat:..." into one partner map per group (empty map = no custom
// pattern for that group).  Aligned with the colon-separated groups of -c.
inline std::vector<std::vector<int> > parseSdiffPairsPerGroup(const char *spec)
{
    std::vector<std::vector<int> > perGroup;
    std::string s(spec ? spec : "");
    size_t p = 0;
    while(true) {
        size_t c = s.find(':', p);
        std::string seg = (c == std::string::npos) ? s.substr(p) : s.substr(p, c - p);
        size_t a = seg.find_first_not_of(" \t");         // trim surrounding blanks
        size_t b = seg.find_last_not_of(" \t");
        std::string tok = (a == std::string::npos) ? std::string()
                                                   : seg.substr(a, b - a + 1);
        if(tok.empty()) perGroup.push_back(std::vector<int>());
        else            perGroup.push_back(parseSdiffPairs(tok.c_str()));
        if(c == std::string::npos) break;
        p = c + 1;
    }
    return perGroup;
}


// ── Order 5 (SDIFF_CUSTOM_CAR): per-channel reference SET ─────────────────────
// Pattern "a-b+c+d,e-f+g,..." of 0-based within-group positions: setMap[a] =
// {b,c,d}, so output channel a becomes x[a] - mean({x[b],x[c],x[d]}).  A single
// target "a-b" gives the singleton {b} (== order-4 bipolar on that channel), so
// order 5 generalises order 4; with the full channel set it reduces to order-3
// all-pairs.  Every channel must carry a (non-empty) set and no channel may
// reference itself.  There is NO root/last-position requirement (x - mean(set) is
// generally full rank); the downstream SDIFF_PASS still drops the last channel,
// so place the least-informative channel last.
inline std::vector<std::vector<int> > parseSdiffSets(const char *spec, bool *ok = nullptr)
{
    std::vector<std::vector<int> > setMap;
    int maxPos = 0;
    std::string s(spec ? spec : "");
    // Two passes: first learn maxPos so setMap can be sized and completeness checked.
    std::vector<std::pair<int, std::vector<int> > > toks;
    size_t p = 0;
    auto fail = [&](const std::string& msg) {
        if(ok) { *ok = false; }
        else { std::cerr << "error: " << msg << "\n"; exit(1); }
    };
    while(true) {
        size_t comma = s.find(',', p);
        std::string tok = (comma == std::string::npos) ? s.substr(p) : s.substr(p, comma - p);
        if(!tok.empty()) {
            size_t dash = tok.find('-');
            if(dash == std::string::npos) { fail("bad sdiffSets token '" + tok + "' (want a-b[+c...])"); return std::vector<std::vector<int> >(); }
            int a = atoi(tok.substr(0, dash).c_str());
            if(a < 0) { fail("bad sdiffSets source '" + tok + "'"); return std::vector<std::vector<int> >(); }
            std::vector<int> set;
            std::string rhs = tok.substr(dash + 1);
            size_t q = 0;
            while(true) {
                size_t plus = rhs.find('+', q);
                std::string m = (plus == std::string::npos) ? rhs.substr(q) : rhs.substr(q, plus - q);
                int b = atoi(m.c_str());
                if(m.empty() || b < 0 || b == a) { fail("bad sdiffSets target in '" + tok + "'"); return std::vector<std::vector<int> >(); }
                set.push_back(b);
                if(b + 1 > maxPos) maxPos = b + 1;
                if(plus == std::string::npos) break;
                q = plus + 1;
            }
            if(a + 1 > maxPos) maxPos = a + 1;
            toks.push_back(std::make_pair(a, set));
        }
        if(comma == std::string::npos) break;
        p = comma + 1;
    }
    if(toks.empty()) { fail("empty sdiffSets"); return std::vector<std::vector<int> >(); }
    setMap.assign(maxPos, std::vector<int>());
    for(size_t k = 0; k < toks.size(); k++) {
        if(!setMap[toks[k].first].empty()) { fail("sdiffSets channel specified twice"); return std::vector<std::vector<int> >(); }
        setMap[toks[k].first] = toks[k].second;
    }
    for(int i = 0; i < maxPos; i++)
        if(setMap[i].empty()) { fail("sdiffSets channel has no reference set (all channels must be specified)"); return std::vector<std::vector<int> >(); }
    if(ok) *ok = true;
    return setMap;
}

// True iff the spec uses order-5 SET syntax (any '+' target list) rather than the
// order-4 single-partner "a-b" spanning-tree syntax.  Lets a caller pick the order.
inline bool sdiffSpecUsesSets(const char *spec)
{
    std::string s(spec ? spec : "");
    return s.find('+') != std::string::npos;
}

// Per-group version of parseSdiffSets ("g1sets:g2sets:...").
inline std::vector<std::vector<std::vector<int> > > parseSdiffSetsPerGroup(const char *spec)
{
    std::vector<std::vector<std::vector<int> > > perGroup;
    std::string s(spec ? spec : "");
    size_t p = 0;
    while(true) {
        size_t c = s.find(':', p);
        std::string seg = (c == std::string::npos) ? s.substr(p) : s.substr(p, c - p);
        size_t a = seg.find_first_not_of(" \t");
        size_t b = seg.find_last_not_of(" \t");
        std::string tok = (a == std::string::npos) ? std::string() : seg.substr(a, b - a + 1);
        if(tok.empty()) perGroup.push_back(std::vector<std::vector<int> >());
        else            perGroup.push_back(parseSdiffSets(tok.c_str()));
        if(c == std::string::npos) break;
        p = c + 1;
    }
    return perGroup;
}

#endif // KLUSTERSSHARED_SDIFF_PAIRS_H

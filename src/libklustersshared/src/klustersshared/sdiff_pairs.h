// sdiff_pairs.h — shared parser for SDIFF_CUSTOM custom difference patterns.
//
// A custom spatial-derivative pattern is given as "a-b,c-d,..." where each token
// is a pair of 1-based within-group channel positions.  It is parsed into a
// per-channel partner map: partner[a-1] = b-1, so output channel a becomes
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
            if(a < 1 || b < 1 || a == b) {
                if(ok) { *ok = false; return std::vector<int>(); }
                std::cerr << "error: bad sdiffPairs token '" << tok << "'.\n";
                exit(1);
            }
            src.push_back(a - 1);
            dst.push_back(b - 1);
            if(a > maxPos) maxPos = a;
            if(b > maxPos) maxPos = b;
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
            std::cerr << "error: sdiffPairs channel " << src[k] + 1
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
        std::cerr << "error: sdiffPairs root channel " << roots[0] + 1
                  << " must be the last position (" << maxPos << ").\n";
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

#endif // KLUSTERSSHARED_SDIFF_PAIRS_H

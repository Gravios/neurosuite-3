// sdiff_pairs_test.cpp — the sdiffPairs grammar (sdiff_pairs.h).
//
// Pair patterns "a-b,…" must be a tree rooted at the last position; set patterns
// "a-b+c,…" must give every channel a set without repeats; positions are plain
// non-negative integers.  Valid patterns parse to the documented maps; malformed
// ones — cycles, junk such as "x-1", an order-5 token in a pair pattern, repeated
// set members — are rejected rather than read as something else.  Uses the
// non-exiting `ok` form throughout.  Self-contained (own main), run via ctest.

#include "sdiff_pairs.h"

#include <cstdio>
#include <string>
#include <vector>

static int g_fail = 0, g_ran = 0;
static void check(bool cond, const std::string& what) {
    ++g_ran;
    if (!cond) { std::printf("FAIL: %s\n", what.c_str()); ++g_fail; }
}

static void pairsOk(const char* spec, const std::vector<int>& want) {
    bool ok = false;
    const std::vector<int> got = parseSdiffPairs(spec, &ok);
    check(ok && got == want, std::string("pairs accepted: ") + spec);
}
static void pairsBad(const char* spec) {
    bool ok = true;
    const std::vector<int> got = parseSdiffPairs(spec, &ok);
    check(!ok && got.empty(), std::string("pairs rejected: ") + spec);
}
static void setsOk(const char* spec, const std::vector<std::vector<int>>& want) {
    bool ok = false;
    const auto got = parseSdiffSets(spec, &ok);
    check(ok && got == want, std::string("sets accepted: ") + spec);
}
static void setsBad(const char* spec) {
    bool ok = true;
    const auto got = parseSdiffSets(spec, &ok);
    check(!ok && got.empty(), std::string("sets rejected: ") + spec);
}

int main() {
    // order 4: chain and star trees rooted at the last position; the root is
    // wired to its first predecessor
    pairsOk("0-1,1-2,2-3", {1, 2, 3, 2});
    pairsOk("0-3,1-3,2-3", {3, 3, 3, 0});
    pairsOk("2-3,0-2,1-2", {2, 2, 3, 2});
    pairsOk(" 0 - 1 , 1-2", {1, 2, 1});          // blanks around numbers

    pairsBad("0-1,1-0,2-3");    // one root, but 0 and 1 form a cycle
    pairsBad("0-1,1-2,2-3,3-0");// no root
    pairsBad("0-1,1-2,2-0,3-4");// cycle among 0..2
    pairsBad("x-1,1-2");        // junk position (was read as 0-1)
    pairsBad("0-1+2");          // order-5 token in a pair pattern (was read as 0-1)
    pairsBad("0-1,2-1");        // root 1 is not the last position
    pairsBad("0-1,0-2");        // channel 0 has two targets
    pairsBad("0-0");            // self-reference
    pairsBad("-1-2");           // negative
    pairsBad("0-");             // missing target
    pairsBad("");               // empty

    // order 5
    setsOk("0-1+2,1-0+2,2-0+1", {{1, 2}, {0, 2}, {0, 1}});
    setsOk("0-1,1-0", {{1}, {0}});
    setsBad("0-1+1,1-0");       // repeated member
    setsBad("0-1+x,1-0");       // junk member
    setsBad("0-1+2");           // channels 1 and 2 have no set
    setsBad("0-0+1,1-0");       // self in set
    setsBad("0-1,0-1,1-0");     // channel 0 specified twice

    // per-group splitting keeps empty groups empty
    {
        const auto g = parseSdiffPairsPerGroup("0-1,1-2::2-3,0-3,1-3");
        check(g.size() == 3 && g[1].empty() && g[0] == std::vector<int>({1, 2, 1})
              && g[2] == std::vector<int>({3, 3, 3, 0}), "per-group split");
    }

    // order 6: the expression form, and specs mixing it with the older forms
    {
        check(sdiffSpecOrder("0-1,1-2,2-3") == 4 && sdiffSpecOrder("0-1+2,1-0,2-0") == 5
              && sdiffSpecOrder("c0-c1,c1-c0") == 6 && sdiffSpecOrder("0-1,1-0::c0-(c1+c2)/2") == 6,
              "the order a spec selects");
        check(!sdiffSpecUsesSets("c0-(c1+c2)/2,c1,c2") && sdiffSpecIsExpression("C0-c1"),
              "an expression's '+' is not a reference set");
        check(sdiffPairsAsExpr({1, 2, 3, 2}) == "c0-c1,c1-c2,c2-c3,c3-c2", "pairs as expressions");
        check(sdiffSetsAsExpr({{1, 2}, {0}}) == "c0-(c1+c2)/2,c1-(c0)/1", "sets as expressions");

        bool ok = false;
        std::string err;
        const auto g = parseSdiffExprsPerGroup("0-1,1-2,2-3::c0-(c1+c2)/2,c1-c0,c2-c0:0-1+2,1-0,2-0",
                                               &ok, &err);
        check(ok && g.size() == 4 && g[1].empty(), "a mixed spec parses, empty groups stay empty");
        const double x[4] = {3, 5, 11, -2};
        // an order-4 group converted computes exactly what order 4 does
        const std::vector<int> partner = parseSdiffPairs("0-1,1-2,2-3");
        bool same4 = g.size() == 4 && g[0].size() == 4;
        for (int k = 0; same4 && k < 4; ++k) same4 = g[0].eval(k, x) == x[k] - x[partner[k]];
        check(same4, "converted order-4 group: the same values");
        // an order-5 group converted computes exactly what order 5 does
        const auto sets = parseSdiffSets("0-1+2,1-0,2-0");
        bool same5 = g.size() == 4 && g[3].size() == 3;
        for (int k = 0; same5 && k < 3; ++k) {
            double sum = 0.0;
            for (int m : sets[k]) sum += x[m];
            same5 = g[3].eval(k, x) == x[k] - sum / (double)sets[k].size();
        }
        check(same5, "converted order-5 group: the same values, to the bit");
        check(g.size() == 4 && g[2].size() == 3 && g[2].eval(0, x) == 3 - (5 + 11) / 2.0, "an expression group");

        parseSdiffExprsPerGroup("c0-c1:0-1,1-0,2-3", &ok, &err);
        check(!ok && err.find("spike group 2") != std::string::npos, "a bad older-form group is named");
        parseSdiffExprsPerGroup("c0-c1:c0-(c1", &ok, &err);
        check(!ok && err.find("spike group 2") != std::string::npos && err.find("')'") != std::string::npos,
              "a bad expression is named with the reason");
    }

    std::printf("%d/%d checks passed\n", g_ran - g_fail, g_ran);
    return g_fail ? 1 : 0;
}

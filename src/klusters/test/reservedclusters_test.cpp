// reservedclusters_test.cpp -- what an edit may do to a reserved cluster.
//
// Reserved clusters (types.h: kReservedClusterLo .. kReservedClusterHi) hold
// the spikes of the chunks a fiber-kit chunk run left unsorted.  Checked:
// withoutReserved / reservedOf split a list and keep its order, at both ends
// of the block and just outside it; firstReservedGain finds no gain when a
// labelling keeps, reorders, renumbers around or takes spikes out of a
// reserved cluster, and names the reserved cluster when one gains a spike:
// moved in from another cluster, swapped in for one taken out, moved from one
// reserved cluster to another, created out of nothing, or a row out of range.
// chunkOf / label / idFromLabel show a reserved id as R<chunk> and read a typed
// label back.  Qt-free, own main, run via ctest.

#include "reservedclusters.h"

#include <cstdio>
#include <string>
#include <utility>
#include <vector>

static int g_fail = 0, g_ran = 0;
static void check(bool ok, const std::string& what)
{
    ++g_ran;
    if (!ok) { std::printf("FAIL: %s\n", what.c_str()); ++g_fail; }
}

using Labels = std::vector<std::pair<dataType, dataType>>;   // (feature row, cluster id) by position

static dataType gain(const Labels& before, const Labels& after, dataType nbRows = 20)
{
    std::vector<unsigned char> scratch;
    return reservedclusters::firstReservedGain(
        before.size(), [&](std::size_t i) { return before[i].first; },
                       [&](std::size_t i) { return before[i].second; },
        after.size(),  [&](std::size_t i) { return after[i].first; },
                       [&](std::size_t i) { return after[i].second; },
        nbRows, scratch);
}

int main()
{
    using reservedclusters::withoutReserved;
    using reservedclusters::reservedOf;
    const int lo = static_cast<int>(kReservedClusterLo), hi = static_cast<int>(kReservedClusterHi);
    const dataType R = kReservedClusterLo, R2 = kReservedClusterLo + 2;

    check(isReservedClusterId(lo) && isReservedClusterId(hi) && !isReservedClusterId(lo - 1)
              && !isReservedClusterId(hi + 1) && !isReservedClusterId(0) && !isReservedClusterId(1),
          "the block is 2^24-100 .. 2^24-1, and 0 and 1 are not in it");
    {
        const std::vector<int> ids{ 0, 1, 7, lo + 3, 2, hi, lo - 1, lo, hi + 1 };
        check(withoutReserved(ids) == std::vector<int>{ 0, 1, 7, 2, lo - 1, hi + 1 },
              "withoutReserved drops the block and keeps the order");
        check(reservedOf(ids) == std::vector<int>{ lo + 3, hi, lo },
              "reservedOf keeps only the block, in order");
        check(withoutReserved(std::vector<int>{}).empty() && reservedOf(std::vector<int>{}).empty(),
              "an empty list stays empty");
        const std::vector<long> wide{ 5, kReservedClusterLo + 99 };
        check(withoutReserved(wide) == std::vector<long>{ 5 }, "works for any integer element type");
    }

    // rows 1-3 in cluster 2, rows 4-6 in R, rows 7-8 in R2
    const Labels base{ { 1, 2 }, { 2, 2 }, { 3, 2 }, { 4, R }, { 5, R }, { 6, R }, { 7, R2 }, { 8, R2 } };
    check(gain(base, base) == -1, "unchanged: no gain");
    check(gain(base, { { 7, R2 }, { 4, R }, { 1, 2 }, { 5, R }, { 2, 2 }, { 6, R }, { 8, R2 }, { 3, 2 } }) == -1,
          "the same labels at other positions: no gain");
    check(gain(base, { { 1, 9 }, { 2, 9 }, { 3, 9 }, { 4, R }, { 5, R }, { 6, R }, { 7, R2 }, { 8, R2 } }) == -1,
          "another cluster renamed: no gain");
    check(gain(base, { { 1, 2 }, { 2, 2 }, { 3, 2 }, { 4, 10 }, { 5, R }, { 6, 0 }, { 7, R2 }, { 8, R2 } }) == -1,
          "spikes taken out of R (to a new cluster and to 0): no gain");
    check(gain(base, { { 1, 2 }, { 2, 2 }, { 3, 2 }, { 4, 11 }, { 5, 11 }, { 6, 11 }, { 7, R2 }, { 8, R2 } }) == -1,
          "R emptied: no gain");
    check(gain(base, { { 1, 2 }, { 2, 2 }, { 3, R }, { 4, R }, { 5, R }, { 6, R }, { 7, R2 }, { 8, R2 } }) == R,
          "a spike of cluster 2 moved into R: R gains");
    check(gain(base, { { 1, 2 }, { 2, 2 }, { 3, R }, { 4, 2 }, { 5, R }, { 6, R }, { 7, R2 }, { 8, R2 } }) == R,
          "one spike swapped in for one taken out: R gains (the count alone would miss it)");
    check(gain(base, { { 1, 2 }, { 2, 2 }, { 3, 2 }, { 4, R }, { 5, R }, { 6, R2 }, { 7, R2 }, { 8, R2 } }) == R2,
          "a spike moved from R to R2: R2 gains");
    check(gain(base, { { 1, R + 50 }, { 2, 2 }, { 3, 2 }, { 4, R }, { 5, R }, { 6, R }, { 7, R2 }, { 8, R2 } })
              == R + 50,
          "a reserved cluster that did not exist before: it gains");
    check(gain(base, { { 1, 2 }, { 2, 2 }, { 3, 2 }, { 0, R }, { 5, R }, { 6, R }, { 7, R2 }, { 8, R2 } }) == R,
          "row 0 (no spike) under R: a gain");
    check(gain(base, { { 1, 2 }, { 2, 2 }, { 3, 2 }, { 21, R }, { 5, R }, { 6, R }, { 7, R2 }, { 8, R2 } }, 20) == R,
          "a row past the last one under R: a gain");
    check(gain({ { 1, 2 }, { 2, 3 } }, { { 1, 3 }, { 2, 2 } }) == -1, "no reserved cluster at all: no gain");
    {
        // The scratch buffer of one call must not leak into the next.
        std::vector<unsigned char> scratch;
        const Labels a{ { 1, R } }, b{ { 1, 2 } }, c{ { 1, R } };
        auto run = [&](const Labels& x, const Labels& y) {
            return reservedclusters::firstReservedGain(
                x.size(), [&](std::size_t i) { return x[i].first; }, [&](std::size_t i) { return x[i].second; },
                y.size(), [&](std::size_t i) { return y[i].first; }, [&](std::size_t i) { return y[i].second; },
                5, scratch);
        };
        const dataType first = run(a, a);    // marks row 1 as R's
        const dataType second = run(b, c);   // row 1 was cluster 2: R gains it
        check(first == -1 && second == R, "the scratch buffer carries nothing from one call to the next");
    }

    // Labels: R<chunk> for a reserved id, the number for any other; a typed label reads back.
    {
        using reservedclusters::chunkOf;
        using reservedclusters::idFromLabel;
        using reservedclusters::label;
        check(chunkOf(kReservedClusterLo) == 0 && chunkOf(kReservedClusterLo + 14) == 14
                  && chunkOf(kReservedClusterHi) == 99,
              "chunkOf: the block holds chunks 0 .. 99");
        check(chunkOf(kReservedClusterLo - 1) == -1 && chunkOf(kReservedClusterHi + 1) == -1
                  && chunkOf(0) == -1 && chunkOf(1) == -1 && chunkOf(-5) == -1,
              "chunkOf: -1 just outside the block, for 0, 1 and negatives");
        check(label(kReservedClusterLo) == "R0" && label(kReservedClusterLo + 14) == "R14"
                  && label(kReservedClusterHi) == "R99",
              "label: a reserved id is R<chunk>");
        check(label(0) == "0" && label(1) == "1" && label(57) == "57"
                  && label(kReservedClusterLo - 1) == "16777115" && label(kReservedClusterHi + 1) == "16777216",
              "label: any other id is its number, also just outside the block");
        bool roundTrip = true;
        const long long samples[] = { 0, 1, 2, 4321, kReservedClusterLo - 1, kReservedClusterLo,
                                      kReservedClusterLo + 50, kReservedClusterHi };
        for (const long long id : samples)
            roundTrip = roundTrip && idFromLabel(label(id)) == id;
        check(roundTrip, "idFromLabel(label(id)) == id, in and outside the block");
        check(idFromLabel("r14") == kReservedClusterLo + 14 && idFromLabel("R007") == kReservedClusterLo + 7,
              "idFromLabel: either case, leading zeros");
        check(idFromLabel("16777130") == kReservedClusterLo + 14, "idFromLabel: the number of a reserved id reads as itself");
        check(idFromLabel("R100") == -1 && idFromLabel("R") == -1 && idFromLabel("") == -1 && idFromLabel("R-1") == -1
                  && idFromLabel("14a") == -1 && idFromLabel(" 14") == -1 && idFromLabel("RR1") == -1
                  && idFromLabel("1234567890123") == -1,
              "idFromLabel: -1 for R100, a bare R, empty text, signs, other characters, 13+ digits");
    }

    std::printf("%d/%d checks passed\n", g_ran - g_fail, g_ran);
    return g_fail ? 1 : 0;
}

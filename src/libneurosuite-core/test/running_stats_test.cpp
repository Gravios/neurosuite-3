// running_stats_test.cpp — the lineage node payload math (running_stats.hpp):
// meanStdOfRecords (mean + population std over a record stack) and the EXACT
// weighted combine combineMeanStd (parallel-variance formula).
//
// The headline invariant: folding selection B into A's summary gives exactly the
// summary of A∪B — this is why a node can keep only {mean,std,count} and never the
// spikes.  Self-contained (own main), run via ctest.

#include "neurosuite/core/running_stats.hpp"

#include <cstdio>
#include <cstdint>
#include <cmath>
#include <string>
#include <vector>

using neurosuite::stats::MeanStd;
using neurosuite::stats::meanStdOfRecords;
using neurosuite::stats::combineMeanStd;

static int g_fail = 0, g_ran = 0;
static void check(bool ok, const std::string& what) {
    ++g_ran;
    if (!ok) { std::printf("FAIL: %s\n", what.c_str()); ++g_fail; }
}
static bool close(float a, float b, float tol = 1e-2f) { return std::fabs(a - b) <= tol; }
static bool vecClose(const std::vector<float>& a, const std::vector<float>& b, float tol = 1e-2f) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) if (!close(a[i], b[i], tol)) return false;
    return true;
}

int main()
{
    const std::size_t recLen = 3;

    // ── 1. meanStdOfRecords on a known stack ────────────────────────────────
    // 4 records, element 0 = {2,4,4,4,... } classic std example on the first column.
    // Column 0: {2,4,4,6} -> mean 4, pop var ((4+0+0+4)/4)=2 -> std sqrt2≈1.4142.
    std::vector<int16_t> recs = {
        2, 10, -5,
        4, 10, -5,
        4, 10, -5,
        6, 10, -5,
    };
    MeanStd s = meanStdOfRecords(recs, recLen);
    check(s.count == 4, "count = 4 records");
    check(close(s.mean[0], 4.f) && close(s.mean[1], 10.f) && close(s.mean[2], -5.f), "means correct");
    check(close(s.std[0], std::sqrt(2.f)) && close(s.std[1], 0.f) && close(s.std[2], 0.f),
          "population std correct (col0 sqrt2, constant cols 0)");

    // ── 2. The combine invariant: combine(A,B) == summary(A∪B) ──────────────
    std::vector<int16_t> all;
    for (int16_t v = 0; v < 21; ++v) {          // 21 records, varied values
        all.push_back(static_cast<int16_t>(v));
        all.push_back(static_cast<int16_t>(100 - 2 * v));
        all.push_back(static_cast<int16_t>(v * v - 30));
    }
    // Split A = first 8 records, B = remaining 13.
    std::vector<int16_t> A(all.begin(), all.begin() + 8 * recLen);
    std::vector<int16_t> B(all.begin() + 8 * recLen, all.end());
    MeanStd sa = meanStdOfRecords(A, recLen);
    MeanStd sb = meanStdOfRecords(B, recLen);
    MeanStd whole = meanStdOfRecords(all, recLen);
    MeanStd comb = combineMeanStd(sa.mean, sa.std, sa.count, sb.mean, sb.std, sb.count);
    check(comb.count == whole.count, "combined count == whole count (8+13=21)");
    check(vecClose(comb.mean, whole.mean), "combined MEAN == summary of the union (exact)");
    check(vecClose(comb.std,  whole.std),  "combined STD  == summary of the union (exact)");

    // ── 3. Associativity / order independence ────────────────────────────────
    MeanStd combBA = combineMeanStd(sb.mean, sb.std, sb.count, sa.mean, sa.std, sa.count);
    check(vecClose(combBA.mean, comb.mean) && vecClose(combBA.std, comb.std),
          "combine is symmetric in its operands");

    // ── 4. Empty operands (placeholder folds) ────────────────────────────────
    MeanStd withEmptyA = combineMeanStd({}, {}, 0, sa.mean, sa.std, sa.count);
    check(withEmptyA.count == sa.count && vecClose(withEmptyA.mean, sa.mean),
          "folding into an empty summary yields the selection (first populate)");
    MeanStd withEmptyB = combineMeanStd(sa.mean, sa.std, sa.count, {}, {}, 0);
    check(withEmptyB.count == sa.count && vecClose(withEmptyB.mean, sa.mean),
          "folding an empty selection is a no-op");

    // ── 5. Three-way fold matches the whole (repeated updates accumulate) ────
    std::vector<int16_t> C1(all.begin(),                   all.begin() + 5 * recLen);
    std::vector<int16_t> C2(all.begin() + 5 * recLen,      all.begin() + 13 * recLen);
    std::vector<int16_t> C3(all.begin() + 13 * recLen,     all.end());
    MeanStd s1 = meanStdOfRecords(C1, recLen), s2 = meanStdOfRecords(C2, recLen), s3 = meanStdOfRecords(C3, recLen);
    MeanStd acc = combineMeanStd({}, {}, 0, s1.mean, s1.std, s1.count);
    acc = combineMeanStd(acc.mean, acc.std, acc.count, s2.mean, s2.std, s2.count);
    acc = combineMeanStd(acc.mean, acc.std, acc.count, s3.mean, s3.std, s3.count);
    check(acc.count == whole.count && vecClose(acc.mean, whole.mean) && vecClose(acc.std, whole.std),
          "three successive folds reproduce the whole summary");

    // ── Legacy stubs (count > 0, EMPTY mean: a .wtl v1 node after readWtl) ──
    // A stub contributes nothing to the summary; the result's count is the
    // count behind its mean.  Formerly combine(stub, B) returned the stub and
    // silently dropped B (the geometry guard saw mean sizes 0 != L).
    {
        const std::vector<float> m = {1.f, 2.f, 3.f}, sd = {0.5f, 0.5f, 0.5f};
        const MeanStd ab = combineMeanStd({}, {}, 50, m, sd, 10);
        check(ab.count == 10 && ab.mean == m && ab.std == sd, "combine(stub, B) = B (stub count dropped)");
        const MeanStd ba = combineMeanStd(m, sd, 10, {}, {}, 50);
        check(ba.count == 10 && ba.mean == m && ba.std == sd, "combine(B, stub) = B");
        const MeanStd ss = combineMeanStd({}, {}, 50, {}, {}, 7);
        check(ss.count == 57 && ss.mean.empty() && ss.std.empty(), "combine(stub, stub) = stub with summed count");
        const MeanStd se = combineMeanStd({}, {}, 50, {}, {}, 0);
        check(se.count == 50 && se.mean.empty(), "combine(stub, empty) keeps the stub's count");

        // Order independence over {stub, S1, S2}: every permutation agrees.
        const MeanStd s1 = meanStdOfRecords({2, 4, 6, 4, 6, 8}, recLen);        // 2 records
        const MeanStd s2 = meanStdOfRecords({10, 0, -3, 12, 1, 5, 9, 2, 0}, recLen);  // 3 records
        const MeanStd stub{{}, {}, 99};
        const MeanStd parts[3] = {stub, s1, s2};
        const int perms[6][3] = {{0,1,2},{0,2,1},{1,0,2},{1,2,0},{2,0,1},{2,1,0}};
        MeanStd ref;
        bool same = true;
        for (int k = 0; k < 6; ++k) {
            MeanStd acc;
            for (int j = 0; j < 3; ++j) {
                const MeanStd& x = parts[perms[k][j]];
                acc = combineMeanStd(acc.mean, acc.std, acc.count, x.mean, x.std, x.count);
            }
            if (k == 0) ref = acc;
            else same = same && acc.count == ref.count && vecClose(acc.mean, ref.mean, 1e-4f)
                             && vecClose(acc.std, ref.std, 1e-4f);
        }
        check(ref.count == 5 && same, "fold over {stub, S1, S2} is order-independent; count = 5 (summarised only)");
    }

    std::printf("running_stats_test: %d checks, %d failures%s\n",
                g_ran, g_fail, g_fail ? " — FAILURES" : "");
    std::printf(g_fail ? "RESULT: FAIL\n" : "RESULT: PASS\n");
    return g_fail ? 1 : 0;
}

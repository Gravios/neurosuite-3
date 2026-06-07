/***************************************************************************
 *                      spikeassignment_test.cpp                           *
 *                                                                         *
 *  Standalone test for SpikeAssignment. No framework: exits 0 on success, *
 *  non-zero on the first failure.  Build:                                 *
 *                                                                         *
 *    g++ -std=c++20 -O2 -I src/klusters/src \                             *
 *        -I src/libklustersshared/src/klustersshared \                    *
 *        src/klusters/tests/spikeassignment_test.cpp -o sa_test && ./sa_test
 *                                                                         *
 *  Validates against an independent reference label model over randomised *
 *  edit / undo / redo sequences, checks internal invariants after every   *
 *  step, and demonstrates that an edit is O(spikes moved), not O(total).  *
 ***************************************************************************/
#include "spikeassignment.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <random>
#include <vector>

using Cluster = SpikeAssignment::Cluster;
using Spike   = SpikeAssignment::Spike;

// Stand-in for the current model's per-edit O(N) full-table rebuild (defined
// after main()).
std::vector<Cluster> sa_dummy_rebuild(const std::vector<Cluster> &labels);

static int g_fail = 0;
#define CHECK(cond, msg)                                                       \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::printf("FAIL: %s  (%s:%d)\n", (msg), __FILE__, __LINE__);     \
            ++g_fail;                                                          \
        }                                                                      \
    } while (0)

// Independent ground truth: just the per-spike label vector. members(c) is
// recomputed by a full scan, deliberately the dumb O(N) way.
struct Reference {
    std::vector<Cluster> label;
    std::map<Cluster, std::vector<Spike>> membersOf() const
    {
        std::map<Cluster, std::vector<Spike>> m;
        for (std::size_t s = 0; s < label.size(); ++s)
            m[label[s]].push_back(static_cast<Spike>(s));
        return m;
    }
};

// Assert that the sparse-set store matches the reference exactly.
static void verifyEqual(const SpikeAssignment &sa, const Reference &ref, const char *where)
{
    CHECK(sa.checkInvariants(), where);
    CHECK(sa.nbSpikes() == (Spike)ref.label.size(), where);

    for (std::size_t s = 0; s < ref.label.size(); ++s)
        if (sa.clusterOf((Spike)s) != ref.label[s]) {
            std::printf("FAIL: clusterOf mismatch spike %zu (%s)\n", s, where);
            ++g_fail;
            return;
        }

    auto refMembers = ref.membersOf();
    // Same set of non-empty clusters.
    auto live = sa.clusters();
    CHECK(live.size() == refMembers.size(), where);
    for (Cluster c : live) {
        auto it = refMembers.find(c);
        CHECK(it != refMembers.end(), where);
        if (it == refMembers.end())
            continue;
        CHECK(sa.count(c) == (Spike)it->second.size(), where);
        // members(c) as a set must equal the reference set (order is an
        // internal detail of the packed vector, so compare sorted).
        std::vector<Spike> got = sa.members(c);
        std::sort(got.begin(), got.end());
        CHECK(got == it->second, where);
    }
}

int main()
{
    std::mt19937_64 rng(12345);

    // ---- randomised equivalence: edits + undo/redo --------------------------
    {
        const std::size_t N = 50000;
        const Cluster initialK = 40;
        std::uniform_int_distribution<Cluster> clu0(0, initialK - 1);

        Reference ref;
        ref.label.resize(N);
        for (std::size_t s = 0; s < N; ++s)
            ref.label[s] = clu0(rng);

        SpikeAssignment sa(ref.label);
        sa.setUndoDepth(100000); // no trimming during the equivalence run
        verifyEqual(sa, ref, "initial");

        // Mirror SpikeAssignment's undo/redo stacks with full ref snapshots so
        // we can check round-trips exactly.
        std::vector<std::vector<Cluster>> refUndo, refRedo;

        const int OPS = 4000;
        std::uniform_int_distribution<int> opPick(0, 99);
        for (int op = 0; op < OPS; ++op) {
            const int r = opPick(rng);

            if (r < 55) {
                // move a random subset of spikes to a random destination
                std::vector<Spike> picks;
                std::uniform_int_distribution<std::size_t> spikeD(0, N - 1);
                const int k = 1 + (int)(rng() % 200);
                for (int i = 0; i < k; ++i)
                    picks.push_back((Spike)spikeD(rng));
                // destination: existing or a brand-new id
                Cluster dest;
                if (rng() % 4 == 0)
                    dest = sa.maxClusterId() + 1 + (Cluster)(rng() % 3);
                else
                    dest = (Cluster)(rng() % (sa.maxClusterId() + 1));

                std::vector<Cluster> snap = ref.label;
                sa.beginEdit();
                sa.moveSpikes(picks, dest);
                sa.endEdit();
                for (Spike s : picks)
                    ref.label[(std::size_t)s] = dest;

                if (ref.label != snap) { // a real change occurred
                    refUndo.push_back(std::move(snap));
                    refRedo.clear();
                }
                verifyEqual(sa, ref, "after move");

            } else if (r < 70) {
                // merge two existing clusters
                auto live = sa.clusters();
                if (live.size() >= 2) {
                    Cluster from = live[rng() % live.size()];
                    Cluster to   = live[rng() % live.size()];
                    if (from != to) {
                        std::vector<Cluster> snap = ref.label;
                        sa.beginEdit();
                        sa.mergeCluster(from, to);
                        sa.endEdit();
                        for (auto &c : ref.label)
                            if (c == from)
                                c = to;
                        if (ref.label != snap) {
                            refUndo.push_back(std::move(snap));
                            refRedo.clear();
                        }
                        verifyEqual(sa, ref, "after merge");
                    }
                }

            } else if (r < 85) {
                // undo
                if (sa.canUndo()) {
                    CHECK(!refUndo.empty(), "ref undo stack desync");
                    refRedo.push_back(ref.label);
                    ref.label = refUndo.back();
                    refUndo.pop_back();
                    sa.undo();
                    verifyEqual(sa, ref, "after undo");
                } else {
                    CHECK(refUndo.empty(), "undo availability desync");
                }

            } else {
                // redo
                if (sa.canRedo()) {
                    CHECK(!refRedo.empty(), "ref redo stack desync");
                    refUndo.push_back(ref.label);
                    ref.label = refRedo.back();
                    refRedo.pop_back();
                    sa.redo();
                    verifyEqual(sa, ref, "after redo");
                } else {
                    CHECK(refRedo.empty(), "redo availability desync");
                }
            }
        }
        std::printf("equivalence: %d ops over %zu spikes -> %s\n", OPS, N,
                    g_fail == 0 ? "OK" : "FAILED");
    }

    // ---- undo-depth trimming ----------------------------------------------
    {
        Reference ref;
        ref.label.assign(1000, 0);
        SpikeAssignment sa(ref.label);
        sa.setUndoDepth(3);
        for (int i = 0; i < 10; ++i) {
            sa.beginEdit();
            sa.moveSpike(i, 1); // move spike i to cluster 1
            sa.endEdit();
        }
        int undos = 0;
        while (sa.canUndo()) { sa.undo(); ++undos; }
        CHECK(undos == 3, "undo depth not trimmed to 3");
        std::printf("undo-depth trim: kept %d levels -> %s\n", undos,
                    undos == 3 ? "OK" : "FAILED");
    }

    // ---- performance: edit cost is O(moved), not O(total) ------------------
    {
        const std::size_t N = 5'000'000;
        std::vector<Cluster> labels(N);
        std::mt19937_64 r2(7);
        for (std::size_t s = 0; s < N; ++s)
            labels[s] = (Cluster)(r2() % 200);

        SpikeAssignment sa(labels);

        // Move 500 spikes — should be ~microseconds regardless of N.
        std::vector<Spike> picks;
        for (int i = 0; i < 500; ++i)
            picks.push_back((Spike)(r2() % N));

        auto t0 = std::chrono::high_resolution_clock::now();
        sa.beginEdit();
        sa.moveSpikes(picks, 12345);
        sa.endEdit();
        auto t1 = std::chrono::high_resolution_clock::now();
        const double moveUs = std::chrono::duration<double, std::micro>(t1 - t0).count();

        // Baseline analogue of the current model: rebuild a full label copy.
        auto t2 = std::chrono::high_resolution_clock::now();
        std::vector<Cluster> rebuilt = sa_dummy_rebuild(labels);
        auto t3 = std::chrono::high_resolution_clock::now();
        const double rebuildUs = std::chrono::duration<double, std::micro>(t3 - t2).count();
        if (rebuilt.size() != N) ++g_fail; // keep the copy from being optimised away

        auto t4 = std::chrono::high_resolution_clock::now();
        sa.undo();
        auto t5 = std::chrono::high_resolution_clock::now();
        const double undoUs = std::chrono::duration<double, std::micro>(t5 - t4).count();

        std::printf("perf @ %zu spikes: move 500 = %.1f us, undo = %.1f us, "
                    "full O(N) rebuild = %.1f us  (%.0fx)\n",
                    N, moveUs, undoUs, rebuildUs, rebuildUs / (moveUs > 0 ? moveUs : 1));
        CHECK(sa.checkInvariants(), "perf invariants");
    }

    std::printf("%s\n", g_fail == 0 ? "ALL PASS" : "SOME FAILED");
    return g_fail == 0 ? 0 : 1;
}

// Stand-in for the current model's per-edit O(N) full-table rebuild.
std::vector<Cluster> sa_dummy_rebuild(const std::vector<Cluster> &labels)
{
    std::vector<Cluster> out(labels.size());
    for (std::size_t i = 0; i < labels.size(); ++i)
        out[i] = labels[i];
    return out;
}

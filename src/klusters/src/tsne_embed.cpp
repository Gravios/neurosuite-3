// Barnes-Hut t-SNE (2-D) — see tsne_embed.h for the contract.
//
// The structure follows van der Maaten's reference algorithm: exact kNN on a
// VP-tree, per-point bandwidth by bisection, symmetrised sparse affinities,
// and a quadtree-approximated repulsive term.  Written dependency-free so the
// whole file compiles and runs standalone for verification.

#include "tsne_embed.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numeric>
#include <random>

#ifdef _OPENMP
#include <omp.h>
#endif

namespace {

// ── VP-tree for exact kNN in the (z-scored) input space ─────────────────────
struct VpTree {
    struct Node { int idx = -1; double mu = 0.0; int lo = -1, hi = -1; };
    const std::vector<double>* pts = nullptr;   // N*D row-major
    int D = 0;
    std::vector<Node> nodes;
    std::vector<int>  items;
    std::mt19937      rng;

    double dist(int a, int b) const {
        const double* pa = pts->data() + static_cast<size_t>(a) * D;
        const double* pb = pts->data() + static_cast<size_t>(b) * D;
        double s = 0.0;
        for (int d = 0; d < D; ++d) { const double t = pa[d] - pb[d]; s += t * t; }
        return std::sqrt(s);
    }

    int build(int lo, int hi) {                 // half-open range lo..hi over items
        if (lo >= hi) return -1;
        const int me = static_cast<int>(nodes.size());
        nodes.push_back(Node{});
        // Vantage point: random within the range (deterministic rng).
        std::uniform_int_distribution<int> pick(lo, hi - 1);
        std::swap(items[lo], items[pick(rng)]);
        nodes[me].idx = items[lo];
        if (hi - lo == 1) return me;
        const int mid = (lo + 1 + hi) / 2;
        std::nth_element(items.begin() + lo + 1, items.begin() + mid,
                         items.begin() + hi,
                         [&](int a, int b) {
                             return dist(nodes[me].idx, a) < dist(nodes[me].idx, b);
                         });
        nodes[me].mu = dist(nodes[me].idx, items[mid]);
        const int lo2 = build(lo + 1, mid);
        const int hi2 = build(mid, hi);
        nodes[me].lo = lo2;
        nodes[me].hi = hi2;
        return me;
    }

    struct Heap {                                // max-heap of (d, idx), size k
        std::vector<std::pair<double, int>> v;
        size_t k;
        explicit Heap(size_t k_) : k(k_) { v.reserve(k_ + 1); }
        double worst() const {
            return v.size() < k ? std::numeric_limits<double>::max() : v.front().first;
        }
        void push(double d, int i) {
            if (v.size() < k) {
                v.emplace_back(d, i); std::push_heap(v.begin(), v.end());
            } else if (d < v.front().first) {
                std::pop_heap(v.begin(), v.end()); v.back() = {d, i};
                std::push_heap(v.begin(), v.end());
            }
        }
    };

    void search(int node, int q, Heap& heap) const {
        if (node < 0) return;
        const Node& n = nodes[node];
        if (n.idx != q) heap.push(dist(n.idx, q), n.idx);
        const double d = dist(n.idx, q);
        if (d < n.mu) {
            search(n.lo, q, heap);
            if (d + heap.worst() >= n.mu) search(n.hi, q, heap);
        } else {
            search(n.hi, q, heap);
            if (d - heap.worst() <= n.mu) search(n.lo, q, heap);
        }
    }
};

// ── Quadtree for the Barnes-Hut repulsive term on the 2-D embedding ─────────
struct QuadTree {
    struct Cell {
        double cx, cy, half;            // square cell
        double comX = 0, comY = 0;      // centre of mass
        int    n = 0;
        int    pointIdx = -1;           // leaf payload (-1 = none)
        int    child[4] = {-1, -1, -1, -1};
        bool   leaf = true;
    };
    std::vector<Cell> cells;
    const double* Y = nullptr;          // N*2

    void init(const double* y, int N) {
        Y = y;
        cells.clear();
        cells.reserve(static_cast<size_t>(N) * 2 + 16);
        double minX = y[0], maxX = y[0], minY = y[1], maxY = y[1];
        for (int i = 1; i < N; ++i) {
            minX = std::min(minX, y[2 * i]);     maxX = std::max(maxX, y[2 * i]);
            minY = std::min(minY, y[2 * i + 1]); maxY = std::max(maxY, y[2 * i + 1]);
        }
        const double half =
            0.5 * std::max(maxX - minX, maxY - minY) * 1.0001 + 1e-9;
        cells.push_back(Cell{0.5 * (minX + maxX), 0.5 * (minY + maxY), half});
        for (int i = 0; i < N; ++i) insert(0, i);
    }

    int quadrant(const Cell& c, double x, double y) const {
        return (x >= c.cx ? 1 : 0) + (y >= c.cy ? 2 : 0);
    }

    void insert(int ci, int p) {
        const double x = Y[2 * p], yv = Y[2 * p + 1];
        // Update aggregates on the way down.
        {
            Cell& c = cells[ci];
            c.comX = (c.comX * c.n + x) / (c.n + 1);
            c.comY = (c.comY * c.n + yv) / (c.n + 1);
            c.n += 1;
        }
        for (;;) {
            if (cells[ci].leaf) {
                if (cells[ci].pointIdx < 0) { cells[ci].pointIdx = p; return; }
                // Duplicate positions: keep as an aggregated leaf to avoid
                // infinite subdivision (mass already counted above).
                const int q = cells[ci].pointIdx;
                if (std::abs(Y[2 * q] - x) < 1e-12 &&
                    std::abs(Y[2 * q + 1] - yv) < 1e-12)
                    return;
                subdivide(ci, q);
            }
            const int qd = quadrant(cells[ci], x, yv);
            int child = cells[ci].child[qd];
            if (child < 0) {
                child = newChild(ci, qd);
                cells[ci].child[qd] = child;
            }
            {   // aggregate into the child on the way down
                Cell& cc = cells[child];
                cc.comX = (cc.comX * cc.n + x) / (cc.n + 1);
                cc.comY = (cc.comY * cc.n + yv) / (cc.n + 1);
                cc.n += 1;
            }
            ci = child;
        }
    }

    int newChild(int ci, int qd) {
        const Cell& c = cells[ci];
        const double h = c.half * 0.5;
        Cell nc;
        nc.cx = c.cx + (qd & 1 ? h : -h);
        nc.cy = c.cy + (qd & 2 ? h : -h);
        nc.half = h;
        cells.push_back(nc);
        return static_cast<int>(cells.size()) - 1;
    }

    void subdivide(int ci, int existing) {
        cells[ci].leaf = false;
        const double x = Y[2 * existing], yv = Y[2 * existing + 1];
        const int qd = quadrant(cells[ci], x, yv);
        const int child = newChild(ci, qd);
        cells[ci].child[qd] = child;
        Cell& cc = cells[child];
        cc.pointIdx = existing;
        cc.comX = x; cc.comY = yv; cc.n = 1;
        cells[ci].pointIdx = -1;
    }

    // Accumulate the (unnormalised) repulsive force on point p and its share
    // of Z.  theta2 = theta squared.
    void forces(int p, double theta2, double& fx, double& fy, double& Z) const {
        const double x = Y[2 * p], yv = Y[2 * p + 1];
        // Explicit stack; roots first.
        int stack[128]; int top = 0; stack[top++] = 0;
        while (top) {
            const Cell& c = cells[stack[--top]];
            if (c.n == 0) continue;
            const double dx = x - c.comX, dy = yv - c.comY;
            const double d2 = dx * dx + dy * dy;
            const bool isSelfOnly = (c.leaf && c.pointIdx == p && c.n == 1);
            if (isSelfOnly) continue;
            const double cellSize = 2.0 * c.half;
            if (c.leaf || cellSize * cellSize < theta2 * d2) {
                // Treat as a single body of mass n at the centre of mass;
                // subtract self if p is inside this aggregated cell.
                const double mass = c.n;
                if (!c.leaf) {
                }
                const double q = 1.0 / (1.0 + d2);
                Z  += mass * q;
                const double f = mass * q * q;
                fx += f * dx;
                fy += f * dy;
            } else {
                for (int k = 0; k < 4; ++k)
                    if (c.child[k] >= 0) {
                        if (top < 127) stack[top++] = c.child[k];
                    }
            }
        }
    }
};

}   // namespace

bool tsneEmbed2D(const std::vector<double>& data, int N, int D,
                 std::vector<double>& outXY, const TsneParams& params,
                 const std::function<void(int, int)>& progress,
                 const std::atomic<bool>* cancel, std::string* err)
{
    auto fail = [&](const char* m) { if (err) *err = m; return false; };
    if (N < 8)  return fail("need at least 8 points");
    if (D < 1)  return fail("need at least 1 dimension");
    if (data.size() != static_cast<size_t>(N) * D) return fail("data size mismatch");

    // ── z-score per dimension (constant dimensions become zeros) ────────────
    std::vector<double> X(static_cast<size_t>(N) * D);
    for (int d = 0; d < D; ++d) {
        double mean = 0;
        for (int i = 0; i < N; ++i) mean += data[static_cast<size_t>(i) * D + d];
        mean /= N;
        double var = 0;
        for (int i = 0; i < N; ++i) {
            const double t = data[static_cast<size_t>(i) * D + d] - mean;
            var += t * t;
        }
        const double sd = std::sqrt(var / std::max(1, N - 1));
        const double inv = sd > 1e-12 ? 1.0 / sd : 0.0;
        for (int i = 0; i < N; ++i)
            X[static_cast<size_t>(i) * D + d] =
                (data[static_cast<size_t>(i) * D + d] - mean) * inv;
    }

    const double perp = std::max(2.0, std::min(params.perplexity, (N - 1) / 3.0));
    const int    k    = std::min(N - 1, static_cast<int>(3 * perp));

    // ── exact kNN via VP-tree ───────────────────────────────────────────────
    VpTree tree;
    tree.pts = &X; tree.D = D; tree.rng.seed(params.seed);
    tree.items.resize(N);
    std::iota(tree.items.begin(), tree.items.end(), 0);
    tree.build(0, N);

    std::vector<int>    nnIdx(static_cast<size_t>(N) * k);
    std::vector<double> nnD2(static_cast<size_t>(N) * k);
#ifdef _OPENMP
#pragma omp parallel for schedule(dynamic, 64)
#endif
    for (int i = 0; i < N; ++i) {
        VpTree::Heap heap(static_cast<size_t>(k));
        tree.search(0, i, heap);
        std::sort(heap.v.begin(), heap.v.end());
        for (int j = 0; j < k; ++j) {
            nnIdx[static_cast<size_t>(i) * k + j] = heap.v[static_cast<size_t>(j)].second;
            const double dd = heap.v[static_cast<size_t>(j)].first;
            nnD2[static_cast<size_t>(i) * k + j] = dd * dd;
        }
    }
    if (cancel && cancel->load()) return fail("cancelled");

    // ── per-point bandwidth by bisection on entropy = log(perplexity) ──────
    const double logPerp = std::log(perp);
    std::vector<double> Pcond(static_cast<size_t>(N) * k);
#ifdef _OPENMP
#pragma omp parallel for schedule(static)
#endif
    for (int i = 0; i < N; ++i) {
        const double* d2 = &nnD2[static_cast<size_t>(i) * k];
        double beta = 1.0, betaMin = -1, betaMax = -1;
        double* row = &Pcond[static_cast<size_t>(i) * k];
        for (int iter = 0; iter < 64; ++iter) {
            double sum = 0;
            for (int j = 0; j < k; ++j) { row[j] = std::exp(-beta * d2[j]); sum += row[j]; }
            if (sum < 1e-300) sum = 1e-300;
            double H = 0;
            for (int j = 0; j < k; ++j) {
                const double p = row[j] / sum;
                if (p > 1e-300) H -= p * std::log(p);
            }
            const double diff = H - logPerp;
            if (std::abs(diff) < 1e-5) break;
            if (diff > 0) { betaMin = beta; beta = (betaMax < 0) ? beta * 2 : 0.5 * (beta + betaMax); }
            else          { betaMax = beta; beta = (betaMin < 0) ? beta * 0.5 : 0.5 * (beta + betaMin); }
        }
        double sum = 0;
        for (int j = 0; j < k; ++j) sum += row[j];
        const double inv = 1.0 / std::max(sum, 1e-300);
        for (int j = 0; j < k; ++j) row[j] *= inv;
    }

    // ── symmetrise into CSR (p_ij = (p_j|i + p_i|j) / 2N over the kNN union) ─
    std::vector<int> rowCnt(static_cast<size_t>(N) + 1, 0);
    // Count union degrees: every (i -> nn j) contributes to rows i and j.
    for (int i = 0; i < N; ++i)
        for (int j = 0; j < k; ++j) {
            rowCnt[static_cast<size_t>(i) + 1]++;
            rowCnt[static_cast<size_t>(nnIdx[static_cast<size_t>(i) * k + j]) + 1]++;
        }
    std::vector<int> rowPtr(rowCnt);
    for (int i = 0; i < N; ++i) rowPtr[static_cast<size_t>(i) + 1] += rowPtr[static_cast<size_t>(i)];
    const int nnz = rowPtr[static_cast<size_t>(N)];
    std::vector<int>    colIdx(static_cast<size_t>(nnz));
    std::vector<double> valP(static_cast<size_t>(nnz));
    {
        std::vector<int> cursor(rowPtr.begin(), rowPtr.end() - 1);
        for (int i = 0; i < N; ++i)
            for (int j = 0; j < k; ++j) {
                const int    jj = nnIdx[static_cast<size_t>(i) * k + j];
                const double p  = Pcond[static_cast<size_t>(i) * k + j];
                colIdx[static_cast<size_t>(cursor[static_cast<size_t>(i)])] = jj;
                valP[static_cast<size_t>(cursor[static_cast<size_t>(i)]++)] = p;
                colIdx[static_cast<size_t>(cursor[static_cast<size_t>(jj)])] = i;
                valP[static_cast<size_t>(cursor[static_cast<size_t>(jj)]++)] = p;
            }
    }
    // Duplicate (i,j) entries — j in both kNN lists — simply sum in the
    // gradient, which is exactly the symmetrisation p_j|i + p_i|j.
    {
        double sumP = 0;
        for (int e = 0; e < nnz; ++e) sumP += valP[static_cast<size_t>(e)];
        const double inv = 1.0 / std::max(sumP, 1e-300);
        for (int e = 0; e < nnz; ++e) valP[static_cast<size_t>(e)] *= inv;
    }

    // ── gradient descent ────────────────────────────────────────────────────
    std::vector<double>& Y = outXY;
    Y.assign(static_cast<size_t>(N) * 2, 0.0);
    {
        std::mt19937 g(params.seed);
        std::normal_distribution<double> nd(0.0, 1e-4);
        for (double& v : Y) v = nd(g);
    }
    std::vector<double> uY(static_cast<size_t>(N) * 2, 0.0);
    std::vector<double> gains(static_cast<size_t>(N) * 2, 1.0);
    std::vector<double> grad(static_cast<size_t>(N) * 2, 0.0);
    const double theta2 = params.theta * params.theta;

    QuadTree qt;
    for (int iter = 0; iter < params.nIter; ++iter) {
        if (cancel && cancel->load()) return fail("cancelled");
        const double exag = (iter < params.exagIter) ? params.exag : 1.0;

        qt.init(Y.data(), N);

        // Repulsive pass (Barnes-Hut) — thread-local Z, reduced after.
        double Z = 0.0;
#ifdef _OPENMP
#pragma omp parallel for schedule(dynamic, 128) reduction(+:Z)
#endif
        for (int i = 0; i < N; ++i) {
            double fx = 0, fy = 0, zi = 0;
            qt.forces(i, theta2, fx, fy, zi);
            grad[static_cast<size_t>(2 * i)]     = -fx;   // repulsion stored negative;
            grad[static_cast<size_t>(2 * i) + 1] = -fy;   // normalised and combined below
            Z += zi;
        }
        Z = std::max(Z, 1e-300);

        // Attractive pass over the sparse P.
#ifdef _OPENMP
#pragma omp parallel for schedule(static)
#endif
        for (int i = 0; i < N; ++i) {
            double ax = 0, ay = 0;
            const double yx = Y[static_cast<size_t>(2 * i)];
            const double yy = Y[static_cast<size_t>(2 * i) + 1];
            for (int e = rowPtr[static_cast<size_t>(i)]; e < rowPtr[static_cast<size_t>(i) + 1]; ++e) {
                const int j = colIdx[static_cast<size_t>(e)];
                const double dx = yx - Y[static_cast<size_t>(2 * j)];
                const double dy = yy - Y[static_cast<size_t>(2 * j) + 1];
                const double q = 1.0 / (1.0 + dx * dx + dy * dy);
                const double f = exag * valP[static_cast<size_t>(e)] * q;
                ax += f * dx; ay += f * dy;
            }
            // total gradient = 4 * (attractive - repulsive/Z)
            grad[static_cast<size_t>(2 * i)]     = 4.0 * (ax + grad[static_cast<size_t>(2 * i)] / Z);
            grad[static_cast<size_t>(2 * i) + 1] = 4.0 * (ay + grad[static_cast<size_t>(2 * i) + 1] / Z);
        }

        const double momentum = iter < 250 ? 0.5 : 0.8;
        double cx = 0, cy = 0;
        for (int i = 0; i < 2 * N; ++i) {
            gains[static_cast<size_t>(i)] =
                ((grad[static_cast<size_t>(i)] > 0) != (uY[static_cast<size_t>(i)] > 0))
                    ? gains[static_cast<size_t>(i)] + 0.2
                    : std::max(0.01, gains[static_cast<size_t>(i)] * 0.8);
            uY[static_cast<size_t>(i)] = momentum * uY[static_cast<size_t>(i)]
                - params.eta * gains[static_cast<size_t>(i)] * grad[static_cast<size_t>(i)];
            Y[static_cast<size_t>(i)] += uY[static_cast<size_t>(i)];
            if (i & 1) cy += Y[static_cast<size_t>(i)]; else cx += Y[static_cast<size_t>(i)];
        }
        cx /= N; cy /= N;
        for (int i = 0; i < N; ++i) { Y[static_cast<size_t>(2 * i)] -= cx; Y[static_cast<size_t>(2 * i) + 1] -= cy; }

        if (progress) progress(iter + 1, params.nIter);
    }
    return true;
}

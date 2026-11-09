// matrixdisplayorder_test.cpp -- the display permutation the four curation matrices
// derive from the document's held child-scope sort order (matrixdisplayorder.h).
//
//   * derive(): specials (0/1) keep the front in row order, the held ids follow in
//     the held order, unnamed rows append naturally; the result is a complete
//     permutation; held ids absent from the rows (dead atoms) do not place; an empty
//     held order is the identity (an empty permutation);
//   * set(): a wrong-sized or non-permutation order clears to identity;
//   * toMatrix / toDisplay round-trip, and fall back to identity when unset or out
//     of range; displayList() reads the cluster list in display order;
//   * apply(): derive + set in one call, reports whether a non-identity order is on;
//   * two matrices with the SAME clusters in DIFFERENT row orders agree on the
//     displayed sequence of cluster ids (the point of holding ids, not rows).
#include "matrixdisplayorder.h"

#include <QList>
#include <cstdio>

static int fails = 0;
static void check(bool ok, const char* what)
{
    std::printf("  %s %s\n", ok ? "ok:  " : "FAIL:", what);
    if (!ok) ++fails;
}

static QList<int> shown(const MatrixDisplayOrder& o, const QList<int>& clusterList)
{
    QList<int> out;
    for (int d = 0; d < clusterList.size(); ++d) out.append(clusterList[o.toMatrix(d)]);
    return out;
}

int main()
{
    // rows as a thread computed them: 0, 1, then atoms 7, 3, 9, 5
    const QList<int> rows{0, 1, 7, 3, 9, 5};

    // derive: held 5, 9, 3, 7 -> specials, then held, complete
    {
        const QList<int> perm = MatrixDisplayOrder::derive(rows, {5, 9, 3, 7});
        check(perm == QList<int>({0, 1, 5, 4, 3, 2}), "derive: specials first, then the held ids at their rows");
        MatrixDisplayOrder o; check(o.set(perm, rows.size()), "set accepts the complete permutation");
        check(shown(o, rows) == QList<int>({0, 1, 5, 9, 3, 7}), "the displayed id sequence is the held order");
        check(o.displayList(rows) == QList<int>({0, 1, 5, 9, 3, 7}), "displayList reads the list in display order");
        check(o.toDisplay(5) == 2 && o.toMatrix(2) == 5, "toDisplay/toMatrix round-trip");
    }
    // partial held order: unnamed rows append naturally
    {
        const QList<int> perm = MatrixDisplayOrder::derive(rows, {9, 7});
        check(perm == QList<int>({0, 1, 4, 2, 3, 5}), "unnamed rows append in natural order after the held ones");
    }
    // dead atom in the held order: does not place, survivors keep their order
    {
        const QList<int> perm = MatrixDisplayOrder::derive(rows, {42, 5, 3});
        check(perm == QList<int>({0, 1, 5, 3, 2, 4}), "a held id absent from the rows is skipped");
        check(perm.size() == rows.size(), "the permutation is still complete");
    }
    // empty held order -> identity
    {
        check(MatrixDisplayOrder::derive(rows, {}).isEmpty(), "empty held order derives the identity");
        MatrixDisplayOrder o; check(!o.apply(rows, {}) && !o.active(), "apply with nothing held is identity, inactive");
        check(o.toMatrix(3) == 3 && o.toDisplay(3) == 3, "identity maps every index to itself");
        check(o.displayList(rows) == rows, "identity displayList is the list itself");
    }
    // set: invalid orders clear
    {
        MatrixDisplayOrder o;
        check(!o.set({0, 1, 2}, rows.size()) && !o.active(), "wrong-sized order clears to identity");
        check(!o.set({0, 1, 2, 2, 4, 5}, rows.size()) && !o.active(), "a repeated index clears to identity");
        check(!o.set({0, 1, 2, 3, 4, 9}, rows.size()) && !o.active(), "an out-of-range index clears to identity");
        o.set({0, 1, 5, 4, 3, 2}, rows.size());
        check(o.toMatrix(7) == 7 && o.toMatrix(-1) == -1, "out-of-range display index falls through unchanged");
        check(o.toDisplay(99) == 99, "a matrix row outside the permutation falls through unchanged");
        o.clear(); check(!o.active(), "clear() turns the order off");
    }
    // apply: a non-identity order is on
    {
        MatrixDisplayOrder o;
        check(o.apply(rows, {3, 5, 7, 9}) && o.active(), "apply with a held order is active");
        check(shown(o, rows) == QList<int>({0, 1, 3, 5, 7, 9}), "and shows the held order");
    }
    // two matrices over the same clusters in different row orders agree
    {
        const QList<int> rowsA{0, 1, 7, 3, 9, 5}, rowsB{1, 0, 3, 5, 7, 9};
        MatrixDisplayOrder a, b;
        a.apply(rowsA, {9, 5, 7, 3});
        b.apply(rowsB, {9, 5, 7, 3});
        check(shown(a, rowsA).mid(2) == shown(b, rowsB).mid(2),
              "error- and template-like row orders show the same atom sequence");
        check(shown(b, rowsB).mid(0, 2) == QList<int>({1, 0}), "specials keep their own row order at the front");
    }
    // specials placed from the held order are not duplicated
    {
        const QList<int> perm = MatrixDisplayOrder::derive(rows, {1, 5, 0, 3});
        check(perm == QList<int>({0, 1, 5, 3, 2, 4}), "specials named in the held order are not placed twice");
    }

    std::printf("matrixdisplayorder_test: %s (%d failed)\n", fails ? "FAIL" : "PASS", fails);
    return fails ? 1 : 0;
}

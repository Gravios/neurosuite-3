#!/usr/bin/env python3
"""A move of spikes between clusters keeps every cluster's information.

Data::moveSpikeSubset built the new cluster table from position and count alone: one move --
Promote Children, Drop to Noise, a child dragged to another parent -- wiped the structure, type,
quality and notes of EVERY cluster of the parent layer, the untouched ones included.
Data::restoreClusterLabels (the undo of the child layer) did the same to the children.
"""
import os
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import run as R                    # noqa: E402
import test_curation_log as T      # noqa: E402
import test_undo_pairing as U      # noqa: E402

fails = 0
H = lambda **k: {"hierarchy": k}   # noqa: E731
A = lambda **k: {"annotate": k}    # noqa: E731


def check(ok, what, extra=""):
    global fails
    print(("  ok:   " if ok else "  FAIL: ") + what + (("\n        " + str(extra)) if (not ok and extra) else ""))
    fails += not ok


def main():
    klusters = R.skip_without_binary()
    for name, op in (("Drop to Noise", H(dropChildren=[20000])), ("Promote Children", H(promoteChildren=[20000])),
                     ("Merge Children", H(mergeChildren=[20000, 20001]))):
        with tempfile.TemporaryDirectory() as d:
            base = U.hier(d, "p")
            rep = R.run(klusters, f"{base}.clu.{U.M}.1",
                        [H(), A(cluster=2, quality="good"), A(cluster=3, quality="ok"), op, A(cluster=2), A(cluster=3)],
                        d, timeout=600, autoRenumber=False, autoRealign=False)
            an = [r.get("quality") for r in rep.get("results", []) if "annotate" in r.get("step", {})]
            check(an[2:] == ["good", "ok"], f"{name} (a child of parent 2): parents 2 and 3 keep their quality", an)
    with tempfile.TemporaryDirectory() as d:        # the child layer, across an undo that restores it
        base = U.hier(d, "c")
        rep = R.run(klusters, f"{base}.clu.{U.M}.1",
                    [H(), A(cluster=20004, quality="mua", child=True), H(mergeChildren=[20000, 20001]), H(undo=True),
                     A(cluster=20004, child=True)], d, timeout=600, autoRenumber=False, autoRealign=False)
        an = [r.get("quality") for r in rep.get("results", []) if "annotate" in r.get("step", {})]
        check(an == ["mua", "mua"], "a child keeps its quality when another child edit is undone", an)
    with tempfile.TemporaryDirectory() as d:
        base, _ = T.session(d, "f")
        rep = R.run(klusters, f"{base}.clu.{T.METHOD}.1", [A(cluster=2, quality="good"), {"group": [3, 4]}, A(cluster=2)],
                    d, timeout=600, autoRenumber=False, autoRealign=False)
        an = [r.get("quality") for r in rep.get("results", []) if "annotate" in r.get("step", {})]
        check(an[1:] == ["good"], "a merge of two other clusters: cluster 2 keeps its quality", an)
    print(f"test_annotation_survives_moves: {'PASS' if not fails else 'FAIL'} ({fails} failed)")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())

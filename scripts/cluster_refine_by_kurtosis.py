#!/usr/bin/env python3
"""
cluster_refine_by_kurtosis.py — split bimodal/high-variance clusters using
the (sample, channel) coordinates of maximum kurtosis suspicion identified
by cluster_waveform_stats.py.

Strategy (post-CEM refinement, NOT a CEM replacement):

  For each candidate cluster:
    1.  Locate the (t*, c*) coordinate within the peak window [peak−3,
        peak+3] on the dominant channel where excess kurtosis is most
        negative — this is the empirical location where the bimodality
        lives, derived directly from per-spike statistics rather than
        from a parametric Gaussian fit.

    2.  Extract the per-spike waveform value at (t*, c*) — a 1-D feature
        in which the bimodality is concentrated by construction.

    3.  Fit 1-Gaussian (K=1) and 2-Gaussian (K=2) mixtures in that 1-D
        feature and compare via BIC.

    4.  Accept the split only if ALL of:
          * BIC(K=1) − BIC(K=2) ≥ --bic-thresh    (decisive evidence)
          * Both children have ≥ --min-subclust spikes
          * Ashman's D = √2·|μ_1 − μ_2| / √(σ_1²+σ_2²) ≥ --ashman-thresh
            (D ≥ 2 ⇔ histograms cleanly separated)

This is fundamentally different from CEM-based splitting:
  * Operates on RAW WAVEFORM amplitudes at the discriminating sample/
    channel, not on PCA features that smear bimodality across dimensions.
  * Uses kurtosis (4th moment) of the cluster's own per-spike
    distribution to locate the split direction, instead of testing a
    parametric Gaussian fit's likelihood improvement.
  * Single-shot per cluster — no iteration / no global state.

OUTPUT:
    <session>.clu.<group>.rec  (binary .clu format, suitable as a Klusters
                                 drop-in replacement for .clu.<group> if
                                 the refinement is accepted on review)
    stdout: per-candidate decision log + summary statistics.

USAGE:
    python3 cluster_refine_by_kurtosis.py SESSION GROUP \\
        --npz <session>.cluster_waveforms.g<group>.npz

Defaults are CONSERVATIVE — better to under-split than create false splits.
Tune via --kurt-thresh / --bic-thresh / --ashman-thresh.
"""

import argparse
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
try:
    from footprint_drift_diagnostic import (
        parse_session_params, read_clu,
    )
except ImportError:
    sys.stderr.write("ERROR: place next to footprint_drift_diagnostic.py\n")
    sys.exit(1)


# ─── waveform memmap (same convention as cluster_waveform_stats.py) ──────


def memmap_spk(session: Path, group: int, n_chan: int, n_samples: int,
               use_spk: bool):
    ext = "spk" if use_spk else "spkD"
    path = Path(f"{session}.{ext}.{group}")
    if not path.is_file():
        raise FileNotFoundError(
            f"{path} not found.  Pass --use-spk for plain .spk; default reads .spkD"
        )
    bytes_per_spike = n_samples * n_chan * 2
    file_bytes = path.stat().st_size
    if file_bytes % bytes_per_spike != 0:
        raise RuntimeError(f"{path} size {file_bytes} not divisible by "
                           f"{bytes_per_spike}")
    n_spikes = file_bytes // bytes_per_spike
    mm = np.memmap(path, dtype=np.int16, mode="r",
                   shape=(n_spikes, n_samples, n_chan))
    return mm, str(path)


# ─── 1-D 2-Gaussian mixture fit + BIC ─────────────────────────────────


def _gaussian_log_pdf(x, mu, sd):
    """log N(x; mu, sd) elementwise, vectorised over the last axis of mu/sd."""
    return -0.5 * np.log(2 * np.pi) - np.log(sd) - 0.5 * ((x - mu) / sd) ** 2


def fit_1d_2gmm(x, max_iter=100, tol=1e-5):
    """Manual EM for 1-D 2-Gaussian mixture.  No sklearn dependency.
    Returns (means[2], sds[2], weights[2], responsibilities[N, 2])."""
    x = np.asarray(x, dtype=np.float64)
    n = len(x)
    if n < 4:
        return None, None, None, None

    # Init: split by median
    med = np.median(x)
    left = x[x <= med]; right = x[x > med]
    if len(left) < 2 or len(right) < 2:
        # Degenerate (all identical) — split arbitrarily
        return None, None, None, None
    mu = np.array([float(np.mean(left)), float(np.mean(right))])
    sd = np.array([max(float(np.std(left)), 1e-6),
                   max(float(np.std(right)), 1e-6)])
    pi = np.array([len(left) / n, len(right) / n])

    prev_ll = -np.inf
    for _ in range(max_iter):
        # E step (in log space for numerical stability)
        log_pdf = _gaussian_log_pdf(x[:, None], mu, sd)
        log_joint = log_pdf + np.log(np.maximum(pi, 1e-300))
        log_norm = np.logaddexp(log_joint[:, 0], log_joint[:, 1])
        log_resp = log_joint - log_norm[:, None]
        resp = np.exp(log_resp)
        # M step
        Nk = resp.sum(axis=0)
        if Nk.min() < 1.0:
            break
        mu_new = (resp * x[:, None]).sum(axis=0) / Nk
        var_new = (resp * (x[:, None] - mu_new) ** 2).sum(axis=0) / Nk
        sd_new = np.sqrt(np.maximum(var_new, 1e-12))
        pi_new = Nk / n

        ll = float(log_norm.sum())
        if abs(ll - prev_ll) < tol:
            mu, sd, pi = mu_new, sd_new, pi_new
            break
        mu, sd, pi = mu_new, sd_new, pi_new
        prev_ll = ll

    return mu, sd, pi, resp


def bic_K1(x):
    x = np.asarray(x, dtype=np.float64)
    n = len(x)
    mu = float(x.mean())
    sd = max(float(x.std()), 1e-6)
    ll = _gaussian_log_pdf(x, mu, sd).sum()
    n_params = 2
    return -2 * ll + n_params * np.log(n)


def bic_K2(x, mu, sd, pi):
    x = np.asarray(x, dtype=np.float64)
    n = len(x)
    log_pdf = _gaussian_log_pdf(x[:, None], mu, sd)
    log_joint = log_pdf + np.log(np.maximum(pi, 1e-300))
    ll = float(np.logaddexp(log_joint[:, 0], log_joint[:, 1]).sum())
    n_params = 5
    return -2 * ll + n_params * np.log(n)


def ashman_D(mu, sd):
    """Bimodality coefficient.  D ≥ 2 ⇒ histograms clearly separable."""
    return float(np.sqrt(2) * abs(mu[0] - mu[1])
                  / np.sqrt(sd[0] ** 2 + sd[1] ** 2))


# ─── candidate selection ─────────────────────────────────────────────


def find_candidates(npz, kurt_thresh, min_spikes, peak_sample, half_window=3):
    """Return list of (cid, nspikes, dom_ch, t_star, min_kurt) tuples."""
    ids = npz["clusters"]
    nspikes = npz["nspikes"]
    ptp_mean = npz["ptp_mean"]            # (C, K)
    kurts = npz["kurts"]                  # (T, C, K)
    T = kurts.shape[0]
    t_lo = max(0, peak_sample - half_window)
    t_hi = min(T, peak_sample + half_window + 1)

    candidates = []
    for i, cid in enumerate(ids):
        if cid <= 1:
            continue
        if int(nspikes[i]) < min_spikes:
            continue
        ch_dom = int(np.argmax(ptp_mean[:, i]))
        kurt_window = kurts[t_lo:t_hi, ch_dom, i]
        if kurt_window.size == 0:
            continue
        local_min = float(kurt_window.min())
        if local_min > kurt_thresh:
            continue
        t_star = int(np.argmin(kurt_window)) + t_lo
        candidates.append((
            int(cid), int(nspikes[i]), ch_dom, t_star, local_min
        ))
    candidates.sort(key=lambda c: c[4])    # most-negative kurt first
    return candidates


# ─── main ────────────────────────────────────────────────────────────


def main():
    ap = argparse.ArgumentParser(description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("session", type=Path)
    ap.add_argument("group", type=int)
    ap.add_argument("--npz", type=Path, required=True,
                    help="cluster_waveforms NPZ from cluster_waveform_stats.py")
    ap.add_argument("--output-clu", type=Path, default=None,
                    help="Output path; default <session>.clu.<group>.rec next to session")
    ap.add_argument("--use-spk", action="store_true",
                    help="Read .spk (default reads .spkD)")
    ap.add_argument("--kurt-thresh", type=float, default=-0.7,
                    help="Min kurtosis (most-negative) to consider; default -0.7")
    ap.add_argument("--min-spikes", type=int, default=100,
                    help="Min parent spike count for refinement; default 100")
    ap.add_argument("--min-subclust", type=int, default=30,
                    help="Min spikes in each child cluster after split; default 30")
    ap.add_argument("--bic-thresh", type=float, default=30.0,
                    help="Required BIC improvement (K=1 − K=2); default 30 (decisive)")
    ap.add_argument("--ashman-thresh", type=float, default=2.0,
                    help="Required Ashman D for bimodality; default 2.0")
    ap.add_argument("--dry-run", action="store_true",
                    help="Don't write the .rec file, just print decisions")
    args = ap.parse_args()

    print(f"cluster_refine_by_kurtosis — {args.session.name} group {args.group}")

    # Geometry + clu + spk
    geom = parse_session_params(args.session, args.group)
    n_chan = geom["nChanGroup"]
    n_samples = geom["nSamples"]
    peak_sample = int(geom.get("peakIdx", n_samples // 2))
    print(f"  geometry: {n_chan} chan × {n_samples} samples, "
          f"peak at sample {peak_sample}")

    clu_orig = read_clu(args.session, args.group)
    spk, spk_path = memmap_spk(args.session, args.group, n_chan, n_samples,
                                args.use_spk)
    n = min(len(clu_orig), len(spk))
    if len(clu_orig) != len(spk):
        print(f"  WARNING: clu ({len(clu_orig)}) and spk ({len(spk)}) "
              f"length mismatch; truncating to {n}")
    clu = clu_orig[:n].astype(np.int32).copy()
    print(f"  waveforms: {n} spikes from {spk_path}")
    print(f"  starting clusters: {np.unique(clu).size} unique IDs, "
          f"range [{int(clu.min())}, {int(clu.max())}]")

    # Candidate selection
    npz = np.load(args.npz, allow_pickle=True)
    candidates = find_candidates(npz, args.kurt_thresh, args.min_spikes,
                                  peak_sample)
    print(f"\nCandidates: {len(candidates)} clusters meet "
          f"(min_kurt < {args.kurt_thresh}, nspk ≥ {args.min_spikes})")
    if not candidates:
        print("  Nothing to refine; exiting.")
        return

    # Process each candidate
    next_id = int(clu.max()) + 1
    accepted = []
    rejected = []
    print(f"\n{'parent':>6s} {'nspk':>6s} {'domCh':>5s} {'tstar':>5s} "
          f"{'minKurt':>8s}  {'bicΔ':>8s} {'AshD':>6s} {'decision'}")
    print("-" * 80)
    for cid, nspk, c_dom, t_star, mk in candidates:
        spike_idx = np.flatnonzero(clu == cid)
        if len(spike_idx) != nspk:
            # cluster might have been changed by an earlier split if cluster
            # IDs collide — but candidates come from the ORIGINAL NPZ which
            # was computed on the original .clu, so this should match.
            print(f"  WARNING: cluster {cid}: NPZ says {nspk} spikes, "
                  f".clu has {len(spike_idx)}.")
            if len(spike_idx) < args.min_spikes:
                rejected.append((cid, nspk, mk, "size-changed"))
                continue
        amps = spk[spike_idx, t_star, c_dom].astype(np.float64)

        mu, sd, pi, resp = fit_1d_2gmm(amps)
        if mu is None:
            print(f"{cid:>6d} {nspk:>6d} {c_dom:>5d} {t_star:>5d} "
                  f"{mk:>+8.3f}  {'--':>8s} {'--':>6s}  rejected: GMM-degenerate")
            rejected.append((cid, nspk, mk, "gmm-degenerate"))
            continue

        labels = np.argmax(resp, axis=1)
        n0 = int((labels == 0).sum())
        n1 = int((labels == 1).sum())
        b1 = bic_K1(amps)
        b2 = bic_K2(amps, mu, sd, pi)
        bic_diff = b1 - b2
        D = ashman_D(mu, sd)

        # Accept criteria
        reasons = []
        if min(n0, n1) < args.min_subclust:
            reasons.append(f"child-too-small({n0}/{n1})")
        if bic_diff < args.bic_thresh:
            reasons.append(f"bic({bic_diff:.1f})")
        if D < args.ashman_thresh:
            reasons.append(f"ashman({D:.2f})")

        if reasons:
            print(f"{cid:>6d} {nspk:>6d} {c_dom:>5d} {t_star:>5d} "
                  f"{mk:>+8.3f}  {bic_diff:>8.1f} {D:>6.2f}  rejected: "
                  f"{', '.join(reasons)}")
            rejected.append((cid, nspk, mk, ', '.join(reasons)))
            continue

        # Assign the SECOND mode (smaller cluster by convention) to new ID;
        # keeps parent cluster's identity stable for the larger sub-cluster.
        if n0 >= n1:
            move_mask = (labels == 1)
            keep_n, move_n = n0, n1
        else:
            move_mask = (labels == 0)
            keep_n, move_n = n1, n0
        new_cid = next_id
        next_id += 1
        clu[spike_idx[move_mask]] = new_cid

        print(f"{cid:>6d} {nspk:>6d} {c_dom:>5d} {t_star:>5d} "
              f"{mk:>+8.3f}  {bic_diff:>8.1f} {D:>6.2f}  "
              f"ACCEPT → {cid}({keep_n}) + {new_cid}({move_n}) "
              f"μ=({mu[0]:.0f},{mu[1]:.0f})")
        accepted.append((cid, nspk, mk, new_cid, keep_n, move_n,
                         bic_diff, D, mu, sd))

    # Summary
    print()
    print(f"Summary: {len(accepted)} accepted, {len(rejected)} rejected")
    if accepted:
        bics = np.array([a[6] for a in accepted])
        Ds = np.array([a[7] for a in accepted])
        print(f"  accepted bic delta: median {np.median(bics):.1f}, "
              f"range [{bics.min():.1f}, {bics.max():.1f}]")
        print(f"  accepted Ashman D : median {np.median(Ds):.2f}, "
              f"range [{Ds.min():.2f}, {Ds.max():.2f}]")
        new_count = int(np.unique(clu).size)
        old_count = int(np.unique(clu_orig[:n]).size)
        print(f"  cluster count: {old_count} → {new_count} ({new_count-old_count:+d})")

    # Write .clu.<group>.rec
    if not args.dry_run:
        if args.output_clu is None:
            out_path = Path(f"{args.session}.clu.{args.group}.rec")
        else:
            out_path = args.output_clu
        n_clusters = int(np.unique(clu).size)
        with open(out_path, "wb") as f:
            np.array([n_clusters], dtype=np.int32).tofile(f)
            clu.astype(np.int32).tofile(f)
        print(f"\nWrote {out_path}")
        print(f"  {n_clusters} distinct cluster IDs, {len(clu)} spike labels")
    else:
        print("\n(dry-run: no file written)")

    print("Done.")


if __name__ == "__main__":
    main()

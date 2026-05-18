#!/usr/bin/env python3
"""
drift_fit_single_cluster.py — stage 3 of the holistic drift model design.

Fits the spatial drift forward model to ONE cluster's observed per-chunk
per-channel amplitude footprint:

    A_pred(c, t) = α · exp(-|q_c − (s + d_t)| / λ)

where:
    α      — intrinsic source strength (scalar, fitted)
    s      — source position at t=0 in channel-pitch units (fitted)
    λ      — spatial attenuation length in channel pitches (fitted)
    d_t    — drift trajectory, drift at chunk t (fitted, t=1..T-1; d_0=0 by anchor)
    q_c    — channel position (currently 1D = channel index 0..nChan-1)

This is a single-cluster fit.  If it gives a good fit (residual RMSE small
relative to observed scale, d_t monotonic-ish, s in a sane position),
then stage 4 (joint population fit) is justified.  If residuals are large
or d_t looks like a random walk, the forward model needs revision before
going population-wide.

OUTPUTS (in --output):
  observed.png       — observed (n_chunks × n_chan) amplitude heatmap
  predicted.png      — predicted heatmap from fitted parameters
  residuals.png      — observed − predicted, same colour scale
  drift_trajectory.png — fitted d_t over time, plus s_u as a horizontal line
  fit_summary.txt    — fitted parameters, RMSE, R², per-channel residual stats

ASSUMPTIONS / LIMITATIONS:
- Channel geometry treated as 1D with channels at integer positions 0..nChan-1.
  For a V-probe with two columns or staggered layouts, this is an
  approximation; expect higher residuals for channels in the "other" column.
  Add probe geometry via --channel-positions CSV (idx,x,y) for stage 4+.
- Exponential attenuation g(r) = exp(-r/λ) is phenomenological; real spike
  amplitudes follow ~1/r² for monopole-like sources.  λ absorbs the shape
  mismatch in this approximation.
- d_t is unconstrained between chunks (no smoothness prior).  For this
  stage-3 demonstration we want to see whether the *unregularised* fit
  produces a smooth trajectory naturally.  If it doesn't, that's a
  diagnostic — we'd add a regulariser in stage 4.
"""

import argparse
import sys
from pathlib import Path

import numpy as np
from scipy.optimize import least_squares

# Reuse the readers + footprint computation from the diagnostic.
sys.path.insert(0, str(Path(__file__).resolve().parent))
try:
    from footprint_drift_diagnostic import (
        parse_session_params,
        read_spkD,
        read_res,
        read_clu,
        compute_footprints,
    )
except ImportError:
    sys.stderr.write(
        "ERROR: cannot import from footprint_drift_diagnostic.py — "
        "make sure both files are in the same directory.\n"
    )
    sys.exit(1)

try:
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    HAVE_MPL = True
except ImportError:
    HAVE_MPL = False


# ─── forward model ───────────────────────────────────────────────────────


def unpack_params(params, n_chunks):
    """params = [α, s, λ, d_1, d_2, ..., d_{n_chunks-1}].
    d_0 is anchored to 0 (the source position at chunk 0 IS s)."""
    alpha = params[0]
    s_u = params[1]
    lam = params[2]
    d_rest = params[3:]               # length n_chunks - 1
    d_t = np.concatenate([[0.0], d_rest])  # length n_chunks, d_0 = 0
    return alpha, s_u, lam, d_t


def pack_params(alpha, s_u, lam, d_t):
    return np.concatenate([[alpha, s_u, lam], d_t[1:]])


def forward(params, q_c, n_chunks):
    """Predicted amplitude matrix (n_chunks, n_chan) under current params."""
    alpha, s_u, lam, d_t = unpack_params(params, n_chunks)
    n_chan = len(q_c)
    A = np.zeros((n_chunks, n_chan))
    for t in range(n_chunks):
        dist = np.abs(q_c - (s_u + d_t[t]))
        A[t, :] = alpha * np.exp(-dist / lam)
    return A


def residuals(params, A_obs, mask, q_c, n_chunks):
    """Flat residual vector for scipy.optimize.least_squares.
    mask: boolean (n_chunks, n_chan) — True where A_obs is valid."""
    A_pred = forward(params, q_c, n_chunks)
    diff = A_pred - A_obs
    return diff[mask].ravel()


def initial_guess(A_obs, mask, q_c, n_chunks):
    """Reasonable starting point: α = max observed, s = arg-max channel at
    chunk 0, λ = 1.0 channel pitch, d_t = 0."""
    # Find chunk with most data + use it for s_u initial guess
    chunk_validity = mask.sum(axis=1)
    if chunk_validity.max() == 0:
        return np.zeros(2 + n_chunks)
    seed_chunk = int(np.argmax(chunk_validity))
    seed_row = A_obs[seed_chunk]
    # Channel-of-maximum-amplitude as the initial source position
    weights = np.where(mask[seed_chunk], seed_row, 0.0)
    s_init = float(q_c[int(np.argmax(weights))])
    alpha_init = float(np.nanmax(A_obs[mask])) if mask.any() else 1.0
    lam_init = 1.0
    d_rest = np.zeros(n_chunks - 1)
    return np.concatenate([[alpha_init, s_init, lam_init], d_rest])


# ─── fit driver ──────────────────────────────────────────────────────────


def fit_drift(A_obs, q_c):
    """Run least_squares on the forward model.  A_obs may contain NaN
    (no data for that chunk).  Returns (params, success, n_obs)."""
    n_chunks, n_chan = A_obs.shape
    mask = np.isfinite(A_obs) & (A_obs > 0)
    n_obs = int(mask.sum())

    x0 = initial_guess(A_obs, mask, q_c, n_chunks)

    # Bounds:  α > 0,  s ∈ [-2, nChan+1],  λ ∈ [0.2, 5.0],  d ∈ [-nChan, nChan]
    lo = np.concatenate([
        [1.0, -2.0, 0.2],
        np.full(n_chunks - 1, -float(n_chan)),
    ])
    hi = np.concatenate([
        [1e8, float(n_chan) + 1.0, 5.0],
        np.full(n_chunks - 1, float(n_chan)),
    ])

    res = least_squares(
        residuals, x0, args=(A_obs, mask, q_c, n_chunks),
        bounds=(lo, hi),
        method="trf",
        max_nfev=20000,
        verbose=0,
    )
    return res, mask, n_obs


# ─── plots ───────────────────────────────────────────────────────────────


def plot_heatmap(A, channel_list, chunk_minutes, out_path, title, vmin=None, vmax=None):
    if not HAVE_MPL:
        return
    n_chunks, n_chan = A.shape
    fig, ax = plt.subplots(figsize=(8, max(4, n_chunks * 0.15)))
    im = ax.imshow(
        A, aspect="auto", origin="lower", cmap="viridis",
        extent=[-0.5, n_chan - 0.5, 0, n_chunks * chunk_minutes],
        vmin=vmin, vmax=vmax,
    )
    ax.set_xlabel("channel (group-local idx)")
    ax.set_ylabel("time (minutes)")
    ax.set_xticks(range(n_chan))
    ax.set_xticklabels([str(c) for c in channel_list], fontsize=8)
    ax.set_title(title)
    plt.colorbar(im, ax=ax, label="median ptp (raw int16)")
    fig.tight_layout()
    fig.savefig(out_path, dpi=110)
    plt.close(fig)


def plot_residual_heatmap(R, channel_list, chunk_minutes, out_path):
    if not HAVE_MPL:
        return
    n_chunks, n_chan = R.shape
    vmax = float(np.nanmax(np.abs(R)))
    fig, ax = plt.subplots(figsize=(8, max(4, n_chunks * 0.15)))
    im = ax.imshow(
        R, aspect="auto", origin="lower", cmap="RdBu_r",
        extent=[-0.5, n_chan - 0.5, 0, n_chunks * chunk_minutes],
        vmin=-vmax, vmax=vmax,
    )
    ax.set_xlabel("channel (group-local idx)")
    ax.set_ylabel("time (minutes)")
    ax.set_xticks(range(n_chan))
    ax.set_xticklabels([str(c) for c in channel_list], fontsize=8)
    ax.set_title("residuals (observed − predicted)\n(zero = perfect fit; "
                 "structure = model mis-specified)")
    plt.colorbar(im, ax=ax, label="residual (raw int16)")
    fig.tight_layout()
    fig.savefig(out_path, dpi=110)
    plt.close(fig)


def plot_trajectory(d_t, s_u, lam, chunk_minutes, out_path):
    if not HAVE_MPL:
        return
    t = np.arange(len(d_t)) * chunk_minutes + chunk_minutes / 2
    fig, ax = plt.subplots(figsize=(10, 5))
    ax.plot(t, d_t, "o-", linewidth=1.5, markersize=5, color="C0",
            label=f"fitted d(t)")
    ax.axhline(0, color="black", linewidth=0.5)
    ax.set_xlabel("time (minutes)")
    ax.set_ylabel("d(t) — drift in channel-pitch units")
    ax.set_title(
        f"Fitted drift trajectory\n"
        f"source position s_u = {s_u:.3f} channels  •  "
        f"attenuation length λ = {lam:.3f} channels  •  "
        f"d_0 ≡ 0 (anchor)"
    )
    ax.grid(alpha=0.3)
    ax.legend()
    fig.tight_layout()
    fig.savefig(out_path, dpi=110)
    plt.close(fig)


# ─── main ────────────────────────────────────────────────────────────────


def main():
    ap = argparse.ArgumentParser(description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("session", type=Path, help="session base path")
    ap.add_argument("group", type=int, help="electrode group (1-based)")
    ap.add_argument("cluster", type=int, help="cluster ID to fit")
    ap.add_argument("--chunk-minutes", type=float, default=10.0)
    ap.add_argument("--min-spikes-per-chunk", type=int, default=10)
    ap.add_argument("--output", type=Path, default=Path("drift_fit"))
    args = ap.parse_args()

    out = args.output
    out.mkdir(parents=True, exist_ok=True)

    print(f"Stage 3 — drift fit for {args.session} group {args.group} "
          f"cluster {args.cluster}")

    geom = parse_session_params(args.session, args.group)
    print(f"  geom: {geom['nChanGroup']} chan × {geom['nSamples']} samples, "
          f"sr={geom['samplingRate']:.1f} Hz")

    spk = read_spkD(args.session, args.group,
                    geom["nChanGroup"], geom["nSamples"])
    res = read_res(args.session, args.group)
    clu = read_clu(args.session, args.group)

    n = min(len(spk), len(res), len(clu))
    spk, res, clu = spk[:n], res[:n], clu[:n]
    if not (clu == args.cluster).any():
        print(f"  ERROR: no spikes assigned to cluster {args.cluster}",
              file=sys.stderr)
        sys.exit(1)

    n_in_cluster = int((clu == args.cluster).sum())
    print(f"  cluster {args.cluster}: {n_in_cluster} spikes")

    footprints, chunk_edges, n_chunks = compute_footprints(
        spk, res, clu,
        sampling_rate=geom["samplingRate"],
        chunk_minutes=args.chunk_minutes,
        min_spikes_per_chunk=args.min_spikes_per_chunk,
        cluster_ids=[args.cluster],
    )
    A_obs = footprints[args.cluster]   # (n_chunks, n_chan)
    n_chan = A_obs.shape[1]
    print(f"  observation matrix: {n_chunks} chunks × {n_chan} channels")
    print(f"  valid (chunk, channel) cells: "
          f"{int((np.isfinite(A_obs) & (A_obs > 0)).sum())} / "
          f"{n_chunks * n_chan}")

    # 1D channel geometry — channel index as position.
    q_c = np.arange(n_chan, dtype=np.float64)

    # ── Fit ──────────────────────────────────────────────────────────
    print("  fitting...")
    result, mask, n_obs = fit_drift(A_obs, q_c)
    alpha, s_u, lam, d_t = unpack_params(result.x, n_chunks)
    A_pred = forward(result.x, q_c, n_chunks)
    R = A_obs - A_pred

    rmse = float(np.sqrt(np.mean(R[mask] ** 2)))
    rss = float(np.sum(R[mask] ** 2))
    tss = float(np.sum((A_obs[mask] - A_obs[mask].mean()) ** 2))
    r2 = 1.0 - rss / tss if tss > 0 else float("nan")
    print(f"  fit: α={alpha:.0f}  s_u={s_u:.3f}  λ={lam:.3f}  "
          f"d_t range=[{d_t.min():.3f}, {d_t.max():.3f}]")
    print(f"  RMSE={rmse:.0f}  R²={r2:.4f}  "
          f"(observed mean={A_obs[mask].mean():.0f}, n_obs={n_obs})")
    print(f"  converged: {result.success}, "
          f"nfev={result.nfev}, status={result.status}")

    # ── Plots ────────────────────────────────────────────────────────
    if HAVE_MPL:
        vmin = float(np.nanmin(A_obs[mask]))
        vmax = float(np.nanmax(A_obs[mask]))
        plot_heatmap(A_obs, geom["channelList"], args.chunk_minutes,
                     out / "observed.png",
                     f"Cluster {args.cluster} — OBSERVED footprint",
                     vmin=vmin, vmax=vmax)
        plot_heatmap(A_pred, geom["channelList"], args.chunk_minutes,
                     out / "predicted.png",
                     f"Cluster {args.cluster} — PREDICTED footprint "
                     f"(α={alpha:.0f}, s_u={s_u:.2f}, λ={lam:.2f})",
                     vmin=vmin, vmax=vmax)
        plot_residual_heatmap(R, geom["channelList"], args.chunk_minutes,
                              out / "residuals.png")
        plot_trajectory(d_t, s_u, lam, args.chunk_minutes,
                        out / "drift_trajectory.png")
        print(f"  plots written to {out}/")

    # ── Summary ──────────────────────────────────────────────────────
    with open(out / "fit_summary.txt", "w") as f:
        f.write("drift_fit_single_cluster — stage 3 result\n")
        f.write("=" * 70 + "\n")
        f.write(f"session         : {args.session}\n")
        f.write(f"group           : {args.group}\n")
        f.write(f"cluster         : {args.cluster}\n")
        f.write(f"spikes used     : {n_in_cluster}\n")
        f.write(f"chunks          : {n_chunks} @ {args.chunk_minutes} min\n")
        f.write(f"observation cells: {n_obs} valid out of {n_chunks * n_chan}\n")
        f.write("\nFitted parameters\n")
        f.write("-" * 40 + "\n")
        f.write(f"  α (source strength)       : {alpha:.2f}\n")
        f.write(f"  s_u (initial position)    : {s_u:.4f} channel pitches\n")
        f.write(f"  λ (attenuation length)    : {lam:.4f} channel pitches\n")
        f.write(f"  d_t range                 : "
                f"[{d_t.min():.4f}, {d_t.max():.4f}]\n")
        f.write(f"  d_t monotonic?            : "
                f"{'yes' if np.all(np.diff(d_t) >= -1e-3) or np.all(np.diff(d_t) <= 1e-3) else 'no'}\n")
        f.write("\nFit quality\n")
        f.write("-" * 40 + "\n")
        f.write(f"  RMSE                      : {rmse:.2f}\n")
        f.write(f"  R²                        : {r2:.4f}\n")
        f.write(f"  obs mean                  : {A_obs[mask].mean():.2f}\n")
        f.write(f"  obs std                   : {A_obs[mask].std():.2f}\n")
        f.write(f"  RMSE / obs mean           : {rmse / A_obs[mask].mean() * 100:.2f}%\n")
        f.write(f"  converged                 : {result.success}\n")
        f.write(f"  nfev                      : {result.nfev}\n")

        f.write("\nPer-channel residual stats\n")
        f.write("-" * 40 + "\n")
        for c in range(n_chan):
            col = R[:, c][mask[:, c]]
            if col.size == 0:
                f.write(f"  ch {geom['channelList'][c]:3d}: (no data)\n")
                continue
            f.write(f"  ch {geom['channelList'][c]:3d} (idx {c}): "
                    f"mean={col.mean():+7.0f}  rms={np.sqrt(np.mean(col**2)):6.0f}  "
                    f"max|r|={np.abs(col).max():6.0f}\n")

        f.write("\nFitted d_t trajectory\n")
        f.write("-" * 40 + "\n")
        for t in range(n_chunks):
            t_min = (chunk_edges[t] + chunk_edges[t + 1]) / 2 / 60
            f.write(f"  chunk {t:2d} (t={t_min:6.1f} min): d_t = {d_t[t]:+.4f}\n")

    print(f"  summary: {out / 'fit_summary.txt'}")
    print("Done.")


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""
drift_fit_single_cluster_2d.py — stage 3 (v2), 2D drift fit.

Replaces the 1D channel-index model in drift_fit_single_cluster.py with
a 2D model that uses real probe geometry:

    A_pred(c, t) = α · exp(-‖q_c − (s + d_t)‖ / λ)

where:
    α       — intrinsic source strength (scalar, fitted)
    s       — source position at t=0 as 2D (s_x, s_y) in µm (fitted)
    λ       — spatial attenuation length in µm (fitted)
    d_t     — 2D drift trajectory, (d_x, d_y) per chunk (fitted, t=1..T-1;
              d_0 ≡ (0, 0) by anchor)
    q_c     — channel position (x_c, y_c) in µm (from probe geometry)

PROBE GEOMETRY
--------------
Three ways to supply it, in order of precedence:
  1. --probe-geometry <csv>      Custom: rows of "channel_local_idx,x_um,y_um"
                                  (one row per channel in the group)
  2. --probe buzsaki64l          Built-in: NeuroNexus Buzsaki64L shank,
                                  staggered 8-site layout (Sirota lab default)
  3. (no flag)                   ERROR — geometry required.

The Buzsaki64L built-in is offered because that's the canonical probe for
sirotaA-jg sessions per the nphys-data probe library.  If your data is
from a different probe (A1x32-Poly2, etc.), supply --probe-geometry.

OUTPUTS (in --output):
  observed.png         — observed (n_chunks × n_chan) amplitude heatmap
  predicted.png        — predicted heatmap from fitted parameters
  residuals.png        — observed − predicted
  drift_trajectory.png — fitted d_t(x, y) over time; 2D path + components
  geometry.png         — probe geometry + fitted source-position trajectory
  fit_summary.txt      — fitted parameters, RMSE, R², per-channel stats
"""

import argparse
import csv
import sys
from pathlib import Path

import numpy as np
from scipy.optimize import least_squares

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
        "place this script next to it.\n"
    )
    sys.exit(1)

try:
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    HAVE_MPL = True
except ImportError:
    HAVE_MPL = False


# ─── built-in probe geometries ───────────────────────────────────────────


# NeuroNexus Buzsaki64L — single shank, sites ordered tip-to-base.
# Source: src/nphys-data/src/probes/neuronexus/Buzsaki64L.probe in this fork.
# Per shank: 8 sites; (x, y) µm relative to shank tip.
BUZSAKI64L_SHANK_GEOMETRY = np.array([
    [   0,   0],   # site 0
    [ -11,  20],   # site 1
    [  11,  40],   # site 2
    [ -11,  60],   # site 3
    [  11,  80],   # site 4
    [ -11, 100],   # site 5
    [  11, 120],   # site 6
    [ -11, 140],   # site 7
], dtype=np.float64)


def get_geometry(args, n_chan: int) -> np.ndarray:
    """Resolve the (n_chan, 2) geometry array from CLI args."""
    if args.probe_geometry:
        rows = []
        with open(args.probe_geometry, "r") as f:
            reader = csv.reader(f)
            for row in reader:
                if not row or row[0].lstrip().startswith("#"):
                    continue
                # Accept header row if present
                try:
                    idx, x, y = int(row[0]), float(row[1]), float(row[2])
                except ValueError:
                    continue
                rows.append((idx, x, y))
        rows.sort()
        if len(rows) != n_chan:
            raise ValueError(
                f"--probe-geometry: expected {n_chan} rows for this group, "
                f"got {len(rows)}"
            )
        return np.array([[r[1], r[2]] for r in rows], dtype=np.float64)

    if args.probe == "buzsaki64l":
        if n_chan != 8:
            raise ValueError(
                f"--probe buzsaki64l: expected 8 channels per group, "
                f"this group has {n_chan}.  Supply --probe-geometry instead."
            )
        return BUZSAKI64L_SHANK_GEOMETRY.copy()

    raise ValueError(
        "Probe geometry required.  Pass --probe buzsaki64l for sirotaA-jg "
        "data, or --probe-geometry <csv> with rows 'idx,x_um,y_um'."
    )


# ─── forward model (2D) ──────────────────────────────────────────────────


def unpack_params(params, n_chunks):
    """params = [α, s_x, s_y, λ, d_x_1, d_y_1, ..., d_x_{T-1}, d_y_{T-1}].
    d_0 = (0, 0) by anchor."""
    alpha = params[0]
    s_x = params[1]
    s_y = params[2]
    lam = params[3]
    d_rest = params[4:].reshape(n_chunks - 1, 2)
    d_t = np.vstack([[0.0, 0.0], d_rest])  # (n_chunks, 2)
    return alpha, s_x, s_y, lam, d_t


def pack_params(alpha, s_x, s_y, lam, d_t):
    return np.concatenate([[alpha, s_x, s_y, lam], d_t[1:].ravel()])


def forward(params, q_c, n_chunks):
    """Predicted (n_chunks, n_chan) amplitude matrix."""
    alpha, s_x, s_y, lam, d_t = unpack_params(params, n_chunks)
    s = np.array([s_x, s_y])
    n_chan = q_c.shape[0]
    A = np.zeros((n_chunks, n_chan))
    for t in range(n_chunks):
        src = s + d_t[t]
        dist = np.linalg.norm(q_c - src, axis=1)
        A[t, :] = alpha * np.exp(-dist / lam)
    return A


def residuals(params, A_obs, mask, q_c, n_chunks):
    A_pred = forward(params, q_c, n_chunks)
    diff = A_pred - A_obs
    return diff[mask].ravel()


def initial_guess(A_obs, mask, q_c, n_chunks):
    """Seed:  α = max obs,  s = position of channel with max amplitude at the
    chunk with most data,  λ = inter-site spacing,  d_t = 0."""
    chunk_validity = mask.sum(axis=1)
    if chunk_validity.max() == 0:
        return np.zeros(4 + 2 * (n_chunks - 1))
    seed_chunk = int(np.argmax(chunk_validity))
    seed_row = np.where(mask[seed_chunk], A_obs[seed_chunk], 0.0)
    seed_ch = int(np.argmax(seed_row))
    s_x_init, s_y_init = q_c[seed_ch]
    alpha_init = float(np.nanmax(A_obs[mask])) if mask.any() else 1.0

    # λ: median pairwise neighbour distance in geometry
    diffs = np.linalg.norm(q_c[1:] - q_c[:-1], axis=1)
    lam_init = float(np.median(diffs)) if len(diffs) else 25.0

    d_rest = np.zeros(2 * (n_chunks - 1))
    return np.concatenate([[alpha_init, s_x_init, s_y_init, lam_init], d_rest])


def fit_drift_2d(A_obs, q_c):
    n_chunks, n_chan = A_obs.shape
    mask = np.isfinite(A_obs) & (A_obs > 0)
    n_obs = int(mask.sum())

    x0 = initial_guess(A_obs, mask, q_c, n_chunks)

    # Bounds — geometry-aware
    x_min, x_max = q_c[:, 0].min(), q_c[:, 0].max()
    y_min, y_max = q_c[:, 1].min(), q_c[:, 1].max()
    x_span = max(50.0, x_max - x_min)
    y_span = max(50.0, y_max - y_min)

    lo = np.concatenate([
        [1.0, x_min - x_span, y_min - y_span, 5.0],
        np.tile([-x_span, -y_span], n_chunks - 1),
    ])
    hi = np.concatenate([
        [1e8, x_max + x_span, y_max + y_span, 500.0],
        np.tile([+x_span, +y_span], n_chunks - 1),
    ])

    res = least_squares(
        residuals, x0, args=(A_obs, mask, q_c, n_chunks),
        bounds=(lo, hi),
        method="trf",
        max_nfev=50000,
        verbose=0,
    )
    return res, mask, n_obs


# ─── plots ───────────────────────────────────────────────────────────────


def plot_heatmap(A, channel_list, chunk_minutes, out_path, title,
                 vmin=None, vmax=None):
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
    ax.set_title("residuals (observed − predicted)\n"
                 "(structureless = model right; stripes = model wrong)")
    plt.colorbar(im, ax=ax, label="residual (raw int16)")
    fig.tight_layout()
    fig.savefig(out_path, dpi=110)
    plt.close(fig)


def plot_drift_trajectory(d_t, s_x, s_y, lam, chunk_minutes, out_path):
    if not HAVE_MPL:
        return
    t = np.arange(len(d_t)) * chunk_minutes + chunk_minutes / 2
    fig, (ax_xy, ax_t) = plt.subplots(1, 2, figsize=(14, 5))

    # 2D path
    ax_xy.plot(d_t[:, 0], d_t[:, 1], "o-", linewidth=1.0, markersize=4,
               color="C0")
    ax_xy.scatter([d_t[0, 0]], [d_t[0, 1]], c="green", s=80, zorder=5,
                  label="start (t=0)")
    ax_xy.scatter([d_t[-1, 0]], [d_t[-1, 1]], c="red", s=80, zorder=5,
                  label="end")
    ax_xy.axhline(0, color="gray", linewidth=0.4)
    ax_xy.axvline(0, color="gray", linewidth=0.4)
    ax_xy.set_xlabel("d_x (µm)")
    ax_xy.set_ylabel("d_y (µm)")
    ax_xy.set_title(f"Fitted 2D drift trajectory\n"
                    f"s_0 = ({s_x:.2f}, {s_y:.2f}) µm  •  λ = {lam:.2f} µm")
    ax_xy.legend()
    ax_xy.grid(alpha=0.3)
    ax_xy.set_aspect("equal", adjustable="datalim")

    # Components vs time
    ax_t.plot(t, d_t[:, 0], "o-", linewidth=1.5, markersize=4,
              color="C0", label="d_x (lateral)")
    ax_t.plot(t, d_t[:, 1], "o-", linewidth=1.5, markersize=4,
              color="C1", label="d_y (depth)")
    ax_t.axhline(0, color="black", linewidth=0.5)
    ax_t.set_xlabel("time (minutes)")
    ax_t.set_ylabel("drift component (µm)")
    ax_t.set_title("Drift components over time")
    ax_t.grid(alpha=0.3)
    ax_t.legend()

    fig.tight_layout()
    fig.savefig(out_path, dpi=110)
    plt.close(fig)


def plot_geometry(q_c, channel_list, s_x, s_y, d_t, lam, out_path):
    if not HAVE_MPL:
        return
    fig, ax = plt.subplots(figsize=(6, 9))
    # Channels
    ax.scatter(q_c[:, 0], q_c[:, 1], c="black", s=80, zorder=3,
               marker="s", label="channel")
    for c, (x, y) in enumerate(q_c):
        ax.annotate(f"ch{channel_list[c]}", (x, y), xytext=(5, 0),
                    textcoords="offset points", fontsize=8)

    # Source trajectory: s + d_t for each t
    src_x = s_x + d_t[:, 0]
    src_y = s_y + d_t[:, 1]
    ax.plot(src_x, src_y, "o-", color="C3", linewidth=1.0, markersize=4,
            alpha=0.7, label="fitted source position over time")
    ax.scatter([src_x[0]], [src_y[0]], c="green", s=80, zorder=5,
               label="t=0")
    ax.scatter([src_x[-1]], [src_y[-1]], c="red", s=80, zorder=5,
               label="t=end")

    # Attenuation length scale visualisation: circle of radius λ around start
    circle = plt.Circle((src_x[0], src_y[0]), lam, fill=False, color="C3",
                        linestyle="--", alpha=0.6, label=f"λ = {lam:.1f} µm")
    ax.add_patch(circle)

    ax.set_xlabel("x (µm, lateral)")
    ax.set_ylabel("y (µm, depth from tip)")
    ax.set_title("Channel geometry + fitted source trajectory")
    ax.legend(fontsize=8)
    ax.grid(alpha=0.3)
    ax.set_aspect("equal", adjustable="datalim")
    fig.tight_layout()
    fig.savefig(out_path, dpi=110)
    plt.close(fig)


# ─── main ────────────────────────────────────────────────────────────────


def main():
    ap = argparse.ArgumentParser(description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("session", type=Path)
    ap.add_argument("group", type=int)
    ap.add_argument("cluster", type=int)
    ap.add_argument("--chunk-minutes", type=float, default=10.0)
    ap.add_argument("--min-spikes-per-chunk", type=int, default=10)
    ap.add_argument("--output", type=Path, default=Path("drift_fit_2d"))
    ap.add_argument("--probe", choices=["buzsaki64l"], default=None,
                    help="Built-in probe geometry to use.")
    ap.add_argument("--probe-geometry", type=Path, default=None,
                    help="CSV with rows 'channel_local_idx,x_um,y_um' "
                         "(overrides --probe).")
    args = ap.parse_args()

    out = args.output
    out.mkdir(parents=True, exist_ok=True)

    print(f"Stage 3 (2D) — drift fit for {args.session} group {args.group} "
          f"cluster {args.cluster}")

    geom = parse_session_params(args.session, args.group)
    print(f"  yaml: {geom['nChanGroup']} chan × {geom['nSamples']} samples, "
          f"sr={geom['samplingRate']:.1f} Hz")

    n_chan = geom["nChanGroup"]
    q_c = get_geometry(args, n_chan)
    print(f"  probe geometry: {n_chan} channels")
    for c in range(n_chan):
        print(f"    ch {geom['channelList'][c]:3d} (idx {c}): "
              f"({q_c[c, 0]:+7.2f}, {q_c[c, 1]:+7.2f}) µm")

    spk = read_spkD(args.session, args.group, n_chan, geom["nSamples"])
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
    A_obs = footprints[args.cluster]
    print(f"  observation matrix: {n_chunks} chunks × {n_chan} channels")
    n_valid = int((np.isfinite(A_obs) & (A_obs > 0)).sum())
    print(f"  valid cells: {n_valid} / {n_chunks * n_chan}")

    print("  fitting...")
    result, mask, n_obs = fit_drift_2d(A_obs, q_c)
    alpha, s_x, s_y, lam, d_t = unpack_params(result.x, n_chunks)
    A_pred = forward(result.x, q_c, n_chunks)
    R = A_obs - A_pred

    rmse = float(np.sqrt(np.mean(R[mask] ** 2)))
    rss = float(np.sum(R[mask] ** 2))
    tss = float(np.sum((A_obs[mask] - A_obs[mask].mean()) ** 2))
    r2 = 1.0 - rss / tss if tss > 0 else float("nan")
    print(f"  fit: α={alpha:.0f}  s=({s_x:.2f}, {s_y:.2f}) µm  λ={lam:.2f} µm")
    print(f"       d_x range=[{d_t[:, 0].min():.2f}, {d_t[:, 0].max():.2f}] µm")
    print(f"       d_y range=[{d_t[:, 1].min():.2f}, {d_t[:, 1].max():.2f}] µm")
    print(f"  RMSE={rmse:.0f}  R²={r2:.4f}  "
          f"(obs mean={A_obs[mask].mean():.0f}, n_obs={n_obs})")
    print(f"  converged: {result.success}  nfev={result.nfev}  "
          f"status={result.status}")

    if HAVE_MPL:
        vmin = float(np.nanmin(A_obs[mask]))
        vmax = float(np.nanmax(A_obs[mask]))
        plot_heatmap(A_obs, geom["channelList"], args.chunk_minutes,
                     out / "observed.png",
                     f"Cluster {args.cluster} — OBSERVED",
                     vmin=vmin, vmax=vmax)
        plot_heatmap(A_pred, geom["channelList"], args.chunk_minutes,
                     out / "predicted.png",
                     f"Cluster {args.cluster} — PREDICTED 2D "
                     f"(s=({s_x:.1f},{s_y:.1f}) λ={lam:.1f})",
                     vmin=vmin, vmax=vmax)
        plot_residual_heatmap(R, geom["channelList"], args.chunk_minutes,
                              out / "residuals.png")
        plot_drift_trajectory(d_t, s_x, s_y, lam, args.chunk_minutes,
                              out / "drift_trajectory.png")
        plot_geometry(q_c, geom["channelList"], s_x, s_y, d_t, lam,
                      out / "geometry.png")
        print(f"  plots written to {out}/")

    with open(out / "fit_summary.txt", "w") as f:
        f.write("drift_fit_single_cluster_2d — stage 3 (v2) result\n")
        f.write("=" * 70 + "\n")
        f.write(f"session         : {args.session}\n")
        f.write(f"group           : {args.group}\n")
        f.write(f"cluster         : {args.cluster}\n")
        f.write(f"spikes used     : {n_in_cluster}\n")
        f.write(f"chunks          : {n_chunks} @ {args.chunk_minutes} min\n")
        f.write(f"valid obs cells : {n_obs} of {n_chunks * n_chan}\n")
        f.write(f"probe           : {args.probe or 'custom CSV'}\n")
        f.write("\nFitted parameters (units = µm)\n")
        f.write("-" * 40 + "\n")
        f.write(f"  α                          : {alpha:.2f}\n")
        f.write(f"  s_0  (source at t=0)       : ({s_x:.4f}, {s_y:.4f}) µm\n")
        f.write(f"  λ    (attenuation length)  : {lam:.4f} µm\n")
        f.write(f"  d_x  range                 : [{d_t[:,0].min():+.3f}, "
                f"{d_t[:,0].max():+.3f}] µm\n")
        f.write(f"  d_y  range                 : [{d_t[:,1].min():+.3f}, "
                f"{d_t[:,1].max():+.3f}] µm\n")
        # Monotonicity per axis
        for axis, name in enumerate(["d_x", "d_y"]):
            diffs = np.diff(d_t[:, axis])
            mono = (np.all(diffs >= -1e-3) or np.all(diffs <= 1e-3))
            f.write(f"  {name} monotonic              : {'yes' if mono else 'no'}\n")
        f.write("\nFit quality\n")
        f.write("-" * 40 + "\n")
        f.write(f"  RMSE                       : {rmse:.2f}\n")
        f.write(f"  R²                         : {r2:.4f}\n")
        f.write(f"  obs mean                   : {A_obs[mask].mean():.2f}\n")
        f.write(f"  obs std                    : {A_obs[mask].std():.2f}\n")
        f.write(f"  RMSE / obs mean            : {rmse/A_obs[mask].mean()*100:.2f}%\n")
        f.write(f"  converged                  : {result.success}\n")
        f.write(f"  nfev                       : {result.nfev}\n")

        f.write("\nPer-channel residual stats\n")
        f.write("-" * 40 + "\n")
        for c in range(n_chan):
            col = R[:, c][mask[:, c]]
            if col.size == 0:
                f.write(f"  ch {geom['channelList'][c]:3d} (no data)\n")
                continue
            f.write(f"  ch {geom['channelList'][c]:3d} (idx {c}) "
                    f"@({q_c[c,0]:+5.1f},{q_c[c,1]:+5.1f}): "
                    f"mean={col.mean():+7.0f}  rms={np.sqrt(np.mean(col**2)):6.0f}  "
                    f"max|r|={np.abs(col).max():6.0f}\n")

        f.write("\nFitted source trajectory s_0 + d_t\n")
        f.write("-" * 40 + "\n")
        for t in range(n_chunks):
            t_min = (chunk_edges[t] + chunk_edges[t + 1]) / 2 / 60
            src_x = s_x + d_t[t, 0]
            src_y = s_y + d_t[t, 1]
            f.write(f"  chunk {t:2d} (t={t_min:6.1f} min): "
                    f"source=({src_x:+7.2f}, {src_y:+7.2f}) µm  "
                    f"d=({d_t[t,0]:+6.2f}, {d_t[t,1]:+6.2f})\n")

    print(f"  summary: {out / 'fit_summary.txt'}")
    print("Done.")


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""
drift_fit_single_cluster_2d.py — stage 3 (v5), 2D drift fit.

Probe geometry is loaded automatically from the session YAML's
`probes:` section + the referenced .probe file (canonical schema; see
src/libklustersshared/src/klustersshared/parameteryamlreader_probes.cpp).
No hardcoded probe geometry.

Forward model — K-source multi-pole:

    A_pred(c, t) = Σ_k α_k · exp(-‖q_c − (s_k + d_t)‖ / λ) + ε_c

Real spikes are NOT point sources — the extracellular waveform spans
soma + apical dendrite + axon, producing a multipolar spatial template
that a single exp(-r/λ) cannot fit.  K=2 captures dipole structure
(soma + descending axon trunk → moderate amplitude on deeper channels).
K=3 adds room for tripolar structure (apical dendrite contribution).

All sources translate rigidly under a single drift trajectory d_t —
they belong to one neuron, so probe motion shifts them together.
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
        parse_session_params, read_spkD, read_res, read_clu,
        compute_footprints,
    )
except ImportError:
    sys.stderr.write("ERROR: place this next to footprint_drift_diagnostic.py\n")
    sys.exit(1)

try:
    import yaml
    HAVE_YAML = True
except ImportError:
    HAVE_YAML = False

try:
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    HAVE_MPL = True
except ImportError:
    HAVE_MPL = False


# ─── probe geometry loader ───────────────────────────────────────────────


def load_probe_geometry(session_path: Path, group: int):
    """Return (q_c [n_chan, 2] in µm, probe_path str).

    Mirrors readProbesSection at src/libklustersshared/src/klustersshared/
    parameteryamlreader_probes.cpp:56 — session YAML has a `probes:`
    sequence; each entry has probeFile + channelOffset + anatomicalGroups
    /spikeGroups.  The referenced .probe file has totalChannels, sites
    (count_per_shank, geometry as flat [x_um, y_um] list ordered tip-
    to-base per shank then next shank), and optional channelMap.
    """
    if not HAVE_YAML:
        raise RuntimeError("pyyaml required: pip install pyyaml")

    yaml_path = session_path.with_suffix(".yaml")
    with open(yaml_path) as f:
        sess = yaml.safe_load(f) or {}

    sd_groups = (sess.get("spikeDetection") or {}).get("channelGroups") or []
    if group < 1 or group > len(sd_groups):
        raise ValueError(f"group {group} out of range "
                         f"({len(sd_groups)} spikeDetection groups)")
    group_channels = list(sd_groups[group - 1].get("channels") or [])

    probes = sess.get("probes")
    if not probes:
        raise RuntimeError(f"{yaml_path}: no `probes:` section. "
                           f"Either add it or use --probe-geometry-csv.")
    library_path = sess.get("probeLibraryPath")

    # Find probe entry covering this group
    chosen = None
    for entry in probes:
        groups = entry.get("spikeGroups") or entry.get("anatomicalGroups") or []
        if group in groups:
            chosen = entry
            break
    if chosen is None and len(probes) == 1:
        chosen = probes[0]
    if chosen is None:
        raise RuntimeError(
            f"no probe entry lists group {group} in spike/anatomicalGroups; "
            f"available: {[e.get('label') or e.get('probeFile') for e in probes]}"
        )

    probe_file_ref = chosen.get("probeFile")
    channel_offset = int(chosen.get("channelOffset", 0))
    if not probe_file_ref:
        raise RuntimeError(f"probe entry missing probeFile: {chosen}")

    # Resolve .probe file path: absolute, then library, then session dir
    candidates = [Path(probe_file_ref)]
    if library_path:
        candidates.append(Path(library_path) / probe_file_ref)
    candidates.append(session_path.parent / probe_file_ref)
    probe_path = next((c for c in candidates if c.is_file()), None)
    if probe_path is None:
        raise FileNotFoundError(
            f"probe file {probe_file_ref!r} not found in any of: "
            f"{[str(c) for c in candidates]}"
        )

    with open(probe_path) as f:
        probe = (yaml.safe_load(f) or {}).get("probeFile") or {}
    geometry = (probe.get("sites") or {}).get("geometry") or []
    channel_map = probe.get("channelMap") or []   # empty = sequential

    if not geometry:
        raise RuntimeError(f"{probe_path}: empty sites.geometry")

    n_chan = len(group_channels)
    q_c = np.zeros((n_chan, 2), dtype=np.float64)
    for c_idx, hw_id in enumerate(group_channels):
        local_hw = int(hw_id) - channel_offset
        site_idx = (int(channel_map[local_hw]) if channel_map else local_hw)
        if not (0 <= site_idx < len(geometry)):
            raise RuntimeError(
                f"site index {site_idx} (hw {hw_id}, offset {channel_offset}) "
                f"out of geometry range [0, {len(geometry)})"
            )
        x, y = geometry[site_idx]
        q_c[c_idx] = [float(x), float(y)]
    return q_c, str(probe_path)


def load_geometry_csv(path: Path, n_chan: int) -> np.ndarray:
    rows = []
    with open(path) as f:
        for row in csv.reader(f):
            if not row or row[0].lstrip().startswith("#"):
                continue
            try:
                rows.append((int(row[0]), float(row[1]), float(row[2])))
            except ValueError:
                continue
    rows.sort()
    if len(rows) != n_chan:
        raise ValueError(f"expected {n_chan} rows, got {len(rows)}")
    return np.array([[r[1], r[2]] for r in rows], dtype=np.float64)


# ─── forward model ───────────────────────────────────────────────────────


def unpack_params(params, n_chunks, n_chan, n_sources):
    """params layout:
       [α_1, s1_x, s1_y, ..., α_K, sK_x, sK_y,
        λ,
        ε_0, ..., ε_{n_chan-1},
        d_x_1, d_y_1, ..., d_x_{T-1}, d_y_{T-1}]"""
    K = n_sources
    src = params[:3 * K].reshape(K, 3)
    alphas = src[:, 0]
    s_xy = src[:, 1:3]
    lam = params[3 * K]
    eps_c = params[3 * K + 1: 3 * K + 1 + n_chan]
    d_rest = params[3 * K + 1 + n_chan:].reshape(n_chunks - 1, 2)
    d_t = np.vstack([[0.0, 0.0], d_rest])
    return alphas, s_xy, lam, eps_c, d_t


def forward(params, q_c, n_chunks, n_sources):
    n_chan = q_c.shape[0]
    alphas, s_xy, lam, eps_c, d_t = unpack_params(
        params, n_chunks, n_chan, n_sources)
    A = np.broadcast_to(eps_c, (n_chunks, n_chan)).copy()
    for t in range(n_chunks):
        for k in range(n_sources):
            src = s_xy[k] + d_t[t]
            dist = np.linalg.norm(q_c - src, axis=1)
            A[t, :] += alphas[k] * np.exp(-dist / lam)
    return A


def residuals(params, A_obs, mask, q_c, n_chunks, n_sources):
    return (forward(params, q_c, n_chunks, n_sources) - A_obs)[mask].ravel()


def initial_guess(A_obs, mask, q_c, n_chunks, n_sources):
    n_chan = q_c.shape[0]
    chunk_validity = mask.sum(axis=1)

    eps_init = np.zeros(n_chan)
    for c in range(n_chan):
        col = A_obs[:, c][mask[:, c]]
        if col.size:
            cut = max(1, int(col.size * 0.25))
            eps_init[c] = float(np.partition(col, cut - 1)[:cut].mean())

    seed_chunk = int(np.argmax(chunk_validity)) if chunk_validity.max() else 0
    available = np.where(mask[seed_chunk], A_obs[seed_chunk] - eps_init, 0.0)

    src_params = []
    for k in range(n_sources):
        c_max = int(np.argmax(available))
        amp = max(1.0, float(available[c_max]))
        src_params.extend([amp, q_c[c_max, 0], q_c[c_max, 1]])
        # Suppress 40 µm neighborhood of this seed
        dists = np.linalg.norm(q_c - q_c[c_max], axis=1)
        available = available * (dists > 40)

    # λ: median nearest-neighbour distance
    nn_dists = []
    for i in range(n_chan):
        d = np.linalg.norm(q_c - q_c[i], axis=1)
        d[i] = np.inf
        nn_dists.append(d.min())
    lam_init = float(np.median(nn_dists)) if nn_dists else 25.0

    d_rest = np.zeros(2 * (n_chunks - 1))
    return np.concatenate([src_params, [lam_init], eps_init, d_rest])


def fit_drift(A_obs, q_c, n_sources):
    n_chunks, n_chan = A_obs.shape
    mask = np.isfinite(A_obs) & (A_obs > 0)
    n_obs = int(mask.sum())

    x0 = initial_guess(A_obs, mask, q_c, n_chunks, n_sources)

    x_min, x_max = q_c[:, 0].min(), q_c[:, 0].max()
    y_min, y_max = q_c[:, 1].min(), q_c[:, 1].max()
    x_span = max(100.0, x_max - x_min)
    y_span = max(100.0, y_max - y_min)
    obs_max = float(np.nanmax(A_obs[mask])) if mask.any() else 1e8

    per_src_lo = [1.0, x_min - x_span, y_min - y_span]
    per_src_hi = [1e8, x_max + x_span, y_max + y_span]
    lo = np.concatenate([
        np.tile(per_src_lo, n_sources),
        [2.0],
        np.zeros(n_chan),
        np.tile([-x_span, -y_span], n_chunks - 1),
    ])
    hi = np.concatenate([
        np.tile(per_src_hi, n_sources),
        [500.0],
        np.full(n_chan, obs_max),
        np.tile([+x_span, +y_span], n_chunks - 1),
    ])

    res = least_squares(
        residuals, x0, args=(A_obs, mask, q_c, n_chunks, n_sources),
        bounds=(lo, hi), method="trf", max_nfev=200000, verbose=0,
    )
    return res, mask, n_obs


# ─── plots ───────────────────────────────────────────────────────────────


def plot_heatmap(A, channel_list, chunk_minutes, out_path, title,
                 vmin=None, vmax=None):
    if not HAVE_MPL: return
    n_chunks, n_chan = A.shape
    fig, ax = plt.subplots(figsize=(8, max(4, n_chunks * 0.15)))
    im = ax.imshow(A, aspect="auto", origin="lower", cmap="viridis",
                   extent=[-0.5, n_chan - 0.5, 0, n_chunks * chunk_minutes],
                   vmin=vmin, vmax=vmax)
    ax.set_xlabel("channel (group-local idx)")
    ax.set_ylabel("time (minutes)")
    ax.set_xticks(range(n_chan))
    ax.set_xticklabels([str(c) for c in channel_list], fontsize=8)
    ax.set_title(title)
    plt.colorbar(im, ax=ax, label="median ptp")
    fig.tight_layout()
    fig.savefig(out_path, dpi=110)
    plt.close(fig)


def plot_residual_heatmap(R, channel_list, chunk_minutes, out_path):
    if not HAVE_MPL: return
    n_chunks, n_chan = R.shape
    vmax = float(np.nanmax(np.abs(R)))
    fig, ax = plt.subplots(figsize=(8, max(4, n_chunks * 0.15)))
    im = ax.imshow(R, aspect="auto", origin="lower", cmap="RdBu_r",
                   extent=[-0.5, n_chan - 0.5, 0, n_chunks * chunk_minutes],
                   vmin=-vmax, vmax=vmax)
    ax.set_xlabel("channel (group-local idx)")
    ax.set_ylabel("time (minutes)")
    ax.set_xticks(range(n_chan))
    ax.set_xticklabels([str(c) for c in channel_list], fontsize=8)
    ax.set_title("residuals (obs − pred); structureless = model right")
    plt.colorbar(im, ax=ax, label="residual")
    fig.tight_layout()
    fig.savefig(out_path, dpi=110)
    plt.close(fig)


def plot_drift_trajectory(d_t, s_xy, lam, chunk_minutes, out_path):
    if not HAVE_MPL: return
    t = np.arange(len(d_t)) * chunk_minutes + chunk_minutes / 2
    fig, (ax_xy, ax_t) = plt.subplots(1, 2, figsize=(14, 5))
    ax_xy.plot(d_t[:, 0], d_t[:, 1], "o-", linewidth=1.0,
               markersize=4, color="C0")
    ax_xy.scatter([d_t[0, 0]], [d_t[0, 1]], c="green", s=80, zorder=5, label="t=0")
    ax_xy.scatter([d_t[-1, 0]], [d_t[-1, 1]], c="red", s=80, zorder=5, label="t=end")
    ax_xy.axhline(0, color="gray", linewidth=0.4)
    ax_xy.axvline(0, color="gray", linewidth=0.4)
    ax_xy.set_xlabel("d_x (µm)")
    ax_xy.set_ylabel("d_y (µm)")
    src_str = "  •  ".join(f"s{k+1}=({s[0]:+.1f},{s[1]:+.1f})"
                            for k, s in enumerate(s_xy))
    ax_xy.set_title(f"2D drift trajectory\n{src_str}  •  λ={lam:.1f} µm")
    ax_xy.legend(); ax_xy.grid(alpha=0.3)
    ax_xy.set_aspect("equal", adjustable="datalim")

    ax_t.plot(t, d_t[:, 0], "o-", linewidth=1.5, markersize=4,
              color="C0", label="d_x (lateral)")
    ax_t.plot(t, d_t[:, 1], "o-", linewidth=1.5, markersize=4,
              color="C1", label="d_y (depth)")
    ax_t.axhline(0, color="black", linewidth=0.5)
    ax_t.set_xlabel("time (minutes)")
    ax_t.set_ylabel("drift (µm)")
    ax_t.set_title("Drift components over time")
    ax_t.grid(alpha=0.3); ax_t.legend()
    fig.tight_layout()
    fig.savefig(out_path, dpi=110)
    plt.close(fig)


def plot_geometry(q_c, channel_list, s_xy, alphas, d_t, lam, out_path):
    if not HAVE_MPL: return
    fig, ax = plt.subplots(figsize=(7, 10))
    ax.scatter(q_c[:, 0], q_c[:, 1], c="black", s=120, zorder=3,
               marker="s", label="channels")
    for c, (x, y) in enumerate(q_c):
        ax.annotate(f"ch{channel_list[c]}", (x, y), xytext=(8, 0),
                    textcoords="offset points", fontsize=8)

    colors = ["C3", "C2", "C4", "C5", "C6"]
    for k, s in enumerate(s_xy):
        traj_x = s[0] + d_t[:, 0]
        traj_y = s[1] + d_t[:, 1]
        c = colors[k % len(colors)]
        ax.plot(traj_x, traj_y, "o-", color=c, alpha=0.7, linewidth=1.0,
                markersize=4, label=f"src {k+1} (α={alphas[k]:.0f})")
        ax.scatter([traj_x[0]], [traj_y[0]], c=c, s=80, marker="^",
                   zorder=5, edgecolor="black", linewidth=0.5)
        ax.scatter([traj_x[-1]], [traj_y[-1]], c=c, s=80, marker="v",
                   zorder=5, edgecolor="black", linewidth=0.5)
    ax.set_xlabel("x (µm, lateral)")
    ax.set_ylabel("y (µm, depth from tip)")
    ax.set_title(f"Probe + fitted source trajectories\n(▲=start, ▼=end; λ={lam:.1f} µm)")
    ax.legend(fontsize=9, loc="best"); ax.grid(alpha=0.3)
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
    ap.add_argument("--n-sources", type=int, default=2,
                    help="K, number of point-source components.  K=2 dipole "
                         "(default), K=3 tripolar, K=1 legacy point-source.")
    ap.add_argument("--probe-geometry-csv", type=Path, default=None,
                    help="Override probe geometry with a CSV (idx,x_um,y_um). "
                         "Default: auto-load from session YAML probes section.")
    args = ap.parse_args()

    out = args.output
    out.mkdir(parents=True, exist_ok=True)
    K = args.n_sources

    print(f"Stage 3 (v5) — drift fit for {args.session} group {args.group} "
          f"cluster {args.cluster}, K={K} source(s)")

    geom = parse_session_params(args.session, args.group)
    n_chan = geom["nChanGroup"]
    print(f"  yaml: {n_chan} chan × {geom['nSamples']} samples, "
          f"sr={geom['samplingRate']:.1f} Hz")

    if args.probe_geometry_csv:
        q_c = load_geometry_csv(args.probe_geometry_csv, n_chan)
        probe_src = str(args.probe_geometry_csv)
    else:
        q_c, probe_src = load_probe_geometry(args.session, args.group)
    print(f"  probe geometry: {probe_src}")
    for c in range(n_chan):
        print(f"    ch {geom['channelList'][c]:3d} (idx {c}): "
              f"({q_c[c, 0]:+8.2f}, {q_c[c, 1]:+8.2f}) µm")

    spk = read_spkD(args.session, args.group, n_chan, geom["nSamples"])
    res = read_res(args.session, args.group)
    clu = read_clu(args.session, args.group)
    n = min(len(spk), len(res), len(clu))
    spk, res, clu = spk[:n], res[:n], clu[:n]

    if not (clu == args.cluster).any():
        print(f"  ERROR: no spikes in cluster {args.cluster}", file=sys.stderr)
        sys.exit(1)
    n_in_cluster = int((clu == args.cluster).sum())
    print(f"  cluster {args.cluster}: {n_in_cluster} spikes")

    footprints, chunk_edges, n_chunks = compute_footprints(
        spk, res, clu, sampling_rate=geom["samplingRate"],
        chunk_minutes=args.chunk_minutes,
        min_spikes_per_chunk=args.min_spikes_per_chunk,
        cluster_ids=[args.cluster])
    A_obs = footprints[args.cluster]
    n_valid = int((np.isfinite(A_obs) & (A_obs > 0)).sum())
    print(f"  observation matrix: {n_chunks}×{n_chan}, "
          f"{n_valid}/{n_chunks * n_chan} valid")

    print("  fitting...")
    result, mask, n_obs = fit_drift(A_obs, q_c, K)
    alphas, s_xy, lam, eps_c, d_t = unpack_params(
        result.x, n_chunks, n_chan, K)
    A_pred = forward(result.x, q_c, n_chunks, K)
    R = A_obs - A_pred

    rmse = float(np.sqrt(np.mean(R[mask] ** 2)))
    rss = float(np.sum(R[mask] ** 2))
    tss = float(np.sum((A_obs[mask] - A_obs[mask].mean()) ** 2))
    r2 = 1.0 - rss / tss if tss > 0 else float("nan")
    for k in range(K):
        print(f"  src {k+1}: α={alphas[k]:.0f}  "
              f"s=({s_xy[k][0]:+.2f}, {s_xy[k][1]:+.2f}) µm")
    print(f"  λ={lam:.2f} µm  ε_c: min={eps_c.min():.0f}, "
          f"max={eps_c.max():.0f}, mean={eps_c.mean():.0f}")
    print(f"  d_x range=[{d_t[:, 0].min():.2f}, {d_t[:, 0].max():.2f}] µm")
    print(f"  d_y range=[{d_t[:, 1].min():.2f}, {d_t[:, 1].max():.2f}] µm")
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
                     f"Cluster {args.cluster} — PREDICTED (K={K})",
                     vmin=vmin, vmax=vmax)
        plot_residual_heatmap(R, geom["channelList"], args.chunk_minutes,
                              out / "residuals.png")
        plot_drift_trajectory(d_t, s_xy, lam, args.chunk_minutes,
                              out / "drift_trajectory.png")
        plot_geometry(q_c, geom["channelList"], s_xy, alphas, d_t, lam,
                      out / "geometry.png")
        print(f"  plots written to {out}/")

    with open(out / "fit_summary.txt", "w") as f:
        f.write("drift_fit_single_cluster_2d — stage 3 (v5)\n")
        f.write("=" * 70 + "\n")
        f.write(f"session         : {args.session}\n")
        f.write(f"group           : {args.group}\n")
        f.write(f"cluster         : {args.cluster}\n")
        f.write(f"spikes used     : {n_in_cluster}\n")
        f.write(f"chunks          : {n_chunks} @ {args.chunk_minutes} min\n")
        f.write(f"valid obs cells : {n_obs}/{n_chunks * n_chan}\n")
        f.write(f"probe geometry  : {probe_src}\n")
        f.write(f"n_sources (K)   : {K}\n")

        f.write("\nProbe geometry (channel local idx → global, x, y µm)\n")
        f.write("-" * 40 + "\n")
        for c in range(n_chan):
            f.write(f"  idx {c} → ch{geom['channelList'][c]:3d}  "
                    f"({q_c[c,0]:+8.2f}, {q_c[c,1]:+8.2f})\n")

        f.write("\nFitted source parameters (µm)\n")
        f.write("-" * 40 + "\n")
        for k in range(K):
            f.write(f"  source {k+1}: α = {alphas[k]:10.2f}   "
                    f"s = ({s_xy[k][0]:+8.3f}, {s_xy[k][1]:+8.3f})\n")
        f.write(f"  λ                  : {lam:.4f} µm\n")
        f.write(f"  d_x range          : [{d_t[:,0].min():+.3f}, "
                f"{d_t[:,0].max():+.3f}]\n")
        f.write(f"  d_y range          : [{d_t[:,1].min():+.3f}, "
                f"{d_t[:,1].max():+.3f}]\n")

        f.write("\nPer-channel ε_c (baseline)\n")
        f.write("-" * 40 + "\n")
        for c in range(n_chan):
            f.write(f"  ch{geom['channelList'][c]:3d} (idx {c}) "
                    f"@({q_c[c,0]:+6.1f},{q_c[c,1]:+6.1f}): {eps_c[c]:8.1f}\n")

        f.write("\nFit quality\n")
        f.write("-" * 40 + "\n")
        f.write(f"  RMSE        : {rmse:.2f}\n")
        f.write(f"  R²          : {r2:.4f}\n")
        f.write(f"  obs mean    : {A_obs[mask].mean():.2f}\n")
        f.write(f"  RMSE/mean   : {rmse/A_obs[mask].mean()*100:.2f}%\n")
        f.write(f"  converged   : {result.success}  (nfev={result.nfev})\n")

        f.write("\nPer-channel residual stats\n")
        f.write("-" * 40 + "\n")
        for c in range(n_chan):
            col = R[:, c][mask[:, c]]
            if col.size == 0:
                f.write(f"  ch{geom['channelList'][c]:3d}: no data\n")
                continue
            f.write(f"  ch{geom['channelList'][c]:3d} (idx {c}) "
                    f"@({q_c[c,0]:+6.1f},{q_c[c,1]:+6.1f}): "
                    f"mean={col.mean():+7.0f}  rms={np.sqrt(np.mean(col**2)):6.0f}  "
                    f"max|r|={np.abs(col).max():6.0f}\n")

        f.write("\nFitted drift trajectory\n")
        f.write("-" * 40 + "\n")
        for t in range(n_chunks):
            t_min = (chunk_edges[t] + chunk_edges[t + 1]) / 2 / 60
            f.write(f"  chunk {t:2d} (t={t_min:6.1f} min): "
                    f"d=({d_t[t,0]:+7.2f}, {d_t[t,1]:+7.2f})\n")

    print(f"  summary: {out / 'fit_summary.txt'}")
    print("Done.")


if __name__ == "__main__":
    main()

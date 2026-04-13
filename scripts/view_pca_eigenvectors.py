#!/usr/bin/env python3
"""
view_pca_eigenvectors.py — visualise eigenvectors from .pca.N files produced
by ndm_pca and ndm_pca_stderiv.

Usage
-----
  view_pca_eigenvectors.py session [group [group ...]] [options]

  session       session name (e.g. jg05-20120316)
  group         electrode group number(s); default: all found .pca.N files

Options
  --pca-dir DIR   directory containing .pca.N files (default: current dir)
  --sampling-rate HZ  for x-axis labelling in ms (default: auto from .yaml/.xml)
  --save PREFIX   save figures as PREFIX_groupN.pdf instead of displaying
  --stderiv       label plots as stderiv-space eigenvectors

Description
-----------
Reads the binary .pca.N file produced by process_pca (the same format read by
process_refeaturize) and plots:
  - The per-channel mean waveforms used during PCA
  - The top principal components (eigenvectors) per channel, coloured by
    component rank (PC1 = darkest)

The .pca.N binary layout:
  int32  magic=0x50434145  version  nChannels  data2use  nComponents
  int32  recShift  isCentered
  per channel:
    double[data2use]             mean
    double[data2use*nComponents] eigenvectors  (col-major: col = PC index)
"""

import struct, sys, os, glob, argparse
import numpy as np

try:
    import matplotlib
    import matplotlib.pyplot as plt
    import matplotlib.gridspec as gridspec
except ImportError:
    sys.exit("error: matplotlib is required — pip install matplotlib")

MAGIC = 0x50434145  # "PCAE"

def read_pca_file(path):
    """Read a .pca.N binary file and return a dict."""
    with open(path, "rb") as f:
        magic, version, nch, d2u, ncomp, recshift, iscentered = \
            struct.unpack("7i", f.read(28))
    if magic != MAGIC:
        raise ValueError(f"{path}: not a PCAE file (magic={magic:#010x})")
    if version != 1:
        raise ValueError(f"{path}: unsupported version {version}")

    means   = []
    eigvecs = []
    with open(path, "rb") as f:
        f.seek(28)
        for ch in range(nch):
            mu = np.frombuffer(f.read(d2u * 8), dtype=np.float64).copy()
            ev_flat = np.frombuffer(f.read(d2u * ncomp * 8), dtype=np.float64).copy()
            # col-major: shape (d2u, ncomp)
            ev = ev_flat.reshape(ncomp, d2u).T
            means.append(mu)
            eigvecs.append(ev)

    return dict(nChannels=nch, data2use=d2u, nComponents=ncomp,
                recShift=recshift, isCentered=bool(iscentered),
                means=means, eigvecs=eigvecs)

def detect_sampling_rate(session_name, pca_dir):
    """Try to read samplingRate from a .yaml or .xml session file."""
    for ext in (".yaml", ".yml", ".xml"):
        path = os.path.join(pca_dir, session_name + ext)
        if not os.path.exists(path):
            path = session_name + ext
        if os.path.exists(path):
            with open(path) as f:
                for line in f:
                    if "samplingRate" in line:
                        import re
                        m = re.search(r"(\d+)", line)
                        if m:
                            return int(m.group(1))
    return None

def plot_group(pca, group_num, sampling_rate, title_prefix, save_path=None):
    nch    = pca["nChannels"]
    ncomp  = pca["nComponents"]
    d2u    = pca["data2use"]
    shift  = pca["recShift"]

    dt = 1.0 / sampling_rate * 1000.0 if sampling_rate else 1.0
    unit = "ms" if sampling_rate else "samples"
    xs = (np.arange(d2u) + shift) * dt

    # Layout: rows = channels, cols = mean + ncomp eigenvectors
    ncols = 1 + ncomp
    fig = plt.figure(figsize=(3.0 * ncols, 2.2 * nch + 0.8))
    gs  = gridspec.GridSpec(nch, ncols, figure=fig,
                            hspace=0.45, wspace=0.35)

    cmap = plt.cm.plasma
    pc_colors = [cmap(0.1 + 0.7 * k / max(ncomp - 1, 1)) for k in range(ncomp)]

    for ch in range(nch):
        mu = pca["means"][ch]
        ev = pca["eigvecs"][ch]  # shape (d2u, ncomp)

        # Mean waveform
        ax0 = fig.add_subplot(gs[ch, 0])
        ax0.plot(xs, mu, color="steelblue", linewidth=1.2)
        ax0.axhline(0, color="gray", linewidth=0.4, linestyle="--")
        ax0.set_ylabel(f"ch {ch}", fontsize=7, labelpad=2)
        if ch == 0:
            ax0.set_title("Mean", fontsize=8)
        if ch == nch - 1:
            ax0.set_xlabel(unit, fontsize=7)
        ax0.tick_params(labelsize=6)
        ax0.spines[["top", "right"]].set_visible(False)

        # Eigenvectors
        for k in range(ncomp):
            ax = fig.add_subplot(gs[ch, k + 1])
            ax.plot(xs, ev[:, k], color=pc_colors[k], linewidth=1.2)
            ax.axhline(0, color="gray", linewidth=0.4, linestyle="--")
            if ch == 0:
                ax.set_title(f"PC{k+1}", fontsize=8, color=pc_colors[k])
            if ch == nch - 1:
                ax.set_xlabel(unit, fontsize=7)
            ax.tick_params(labelsize=6)
            ax.spines[["top", "right"]].set_visible(False)

    centered_str = "centered" if pca["isCentered"] else "uncentered"
    fig.suptitle(
        f"{title_prefix}  group {group_num}  "
        f"({nch} ch × {ncomp} PCs, {d2u} samples, {centered_str})",
        fontsize=9, y=1.01
    )

    if save_path:
        fig.savefig(save_path, bbox_inches="tight", dpi=150)
        print(f"  saved: {save_path}")
        plt.close(fig)
    else:
        plt.show()

def main():
    ap = argparse.ArgumentParser(
        description="Visualise .pca.N eigenvectors from ndm_pca / ndm_pca_stderiv")
    ap.add_argument("session",  help="session name")
    ap.add_argument("groups",   nargs="*", type=int,
                    help="electrode group numbers (default: all found)")
    ap.add_argument("--pca-dir",      default=".",
                    help="directory containing .pca.N files (default: .)")
    ap.add_argument("--sampling-rate", type=int, default=None,
                    help="sampling rate Hz for ms x-axis")
    ap.add_argument("--save",  default=None,
                    help="save as PREFIX_groupN.pdf instead of displaying")
    ap.add_argument("--stderiv", action="store_true",
                    help="label as stderiv-space PCA")
    args = ap.parse_args()

    session    = args.session
    pca_dir    = args.pca_dir
    sr         = args.sampling_rate

    if sr is None:
        sr = detect_sampling_rate(session, pca_dir)
        if sr:
            print(f"  detected sampling rate: {sr} Hz")

    # Find all .pca.N files for this session
    pattern = os.path.join(pca_dir, f"{session}.pca.*")
    found   = sorted(glob.glob(pattern))
    if not found:
        sys.exit(f"error: no .pca.* files matching '{pattern}'")

    groups = args.groups
    if not groups:
        groups = []
        for fp in found:
            ext = os.path.splitext(fp)[1]
            try:
                groups.append(int(ext.lstrip(".")))
            except ValueError:
                pass
        groups = sorted(groups)
        if not groups:
            sys.exit(f"error: could not determine group numbers from files: {found}")

    label = "stderiv-PCA" if args.stderiv else "PCA"
    title_prefix = f"{session}  [{label}]"

    for g in groups:
        pca_path = os.path.join(pca_dir, f"{session}.pca.{g}")
        if not os.path.exists(pca_path):
            print(f"  warning: {pca_path} not found, skipping group {g}")
            continue
        print(f"  reading {pca_path}")
        try:
            pca = read_pca_file(pca_path)
        except Exception as e:
            print(f"  error: {e}")
            continue

        save_path = f"{args.save}_group{g}.pdf" if args.save else None
        plot_group(pca, g, sr, title_prefix, save_path)

if __name__ == "__main__":
    main()

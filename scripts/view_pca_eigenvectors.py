#!/usr/bin/env python3
"""
view_pca_eigenvectors.py — visualise eigenvectors from .pca.N (raw) and
.pcaD.N (spatial+temporal derivative) files written by process_pca.

Usage
-----
  view_pca_eigenvectors.py session [group [group ...]] [options]

  session       session name (e.g. jg05-20120316)
  group         electrode group number(s); default: all groups for which a
                .pca.N or .pcaD.N file exists

Options
  --pca-dir DIR       directory containing .pca.N / .pcaD.N files (default: cwd)
  --sampling-rate HZ  for x-axis labelling in ms (default: auto from .yaml/.xml)
  --save PREFIX       save figures as PREFIX_<variant>_groupN.pdf instead of displaying
  --variant {raw,stderiv,both}
                      which variant(s) to display (default: both)
  --stderiv           [legacy alias] equivalent to --variant stderiv

Description
-----------
Reads the binary file produced by process_pca and plots:
  - The per-electrode mean waveform used during PCA fitting
  - The PCA basis vectors (eigenvectors) for each electrode, coloured by
    component rank (PC1 = darkest)

When the stderiv pipeline (ndm_pca_stderiv) is used, process_pca derives
the output filename from .fetD.N → .pcaD.N, so a session may carry both a
.pca.N (raw) and .pcaD.N (derivative) file per group.  Both are plotted by
default.

Semantics
---------
The header carries three count fields whose roles must not be confused:

  nElectrodes          number of independent electrodes in the spike group
                       (file field: nChannels; CLI: process_pca -n)

  nPCsPerElectrode     number of PCA basis vectors per electrode — i.e.
                       eigenvectors retained from each electrode's local
                       (samples × samples) covariance matrix
                       (file field: nComponents; CLI: process_pca -d)

  nSamplesPerWindow    window length in samples used for PCA fitting
                       (file field: data2use; derived from before/after)

Per-electrode payload:
  - 1 mean vector   of length nSamplesPerWindow
  - nPCsPerElectrode eigenvectors, each of length nSamplesPerWindow
    (column-major: each eigenvector is one column of a
     (nSamplesPerWindow × nPCsPerElectrode) matrix)

For an 8-electrode group with 3 PCs/electrode and a 24-sample window, the
.pca.N file contains 8 × (24 + 24*3) = 768 doubles after the header, and
the .fet.N file produced from it has 8 × 3 = 24 PCA features per spike.

For sdiffOrder 1 or 3, ndm_pca_stderiv drops one linearly-dependent
electrode before PCA, so .pcaD.N stores nElectrodes-1 entries.

File format
-----------
process_pca currently writes a 5-int32 legacy header followed by per-electrode
data.  The 7-int32 PCAE header (magic 0x50434145 + version) is also read
when present, matching the dual-format reader in process_shadowcluster.

  Legacy (5 ints):
    int32  nElectrodes  nSamplesPerWindow  nPCsPerElectrode  isCentered  recShift
  PCAE (7 ints):
    int32  magic=0x50434145  version=1  nElectrodes  nSamplesPerWindow
    int32  nPCsPerElectrode  recShift  isCentered

Then, for both layouts:
  for each electrode:  double[nSamplesPerWindow]                       mean
  for each electrode:  double[nSamplesPerWindow*nPCsPerElectrode]      eigenvectors
                                                                       (col-major)
"""

import struct, sys, os, glob, argparse
import numpy as np

try:
    import matplotlib
    import matplotlib.pyplot as plt
    import matplotlib.gridspec as gridspec
except ImportError:
    sys.exit("error: matplotlib is required — pip install matplotlib")

PCAE_MAGIC = 0x50434145  # "PCAE"

# ---------------------------------------------------------------------------
# File reader — handles both legacy and PCAE headers
# ---------------------------------------------------------------------------
def read_pca_file(path):
    """Read a .pca.N or .pcaD.N file. Auto-detects PCAE (7-int) vs legacy
    (5-int) headers. Returns a dict with arrays per electrode.

    Returned keys:
      nElectrodes        int   number of electrodes
      nPCsPerElectrode   int   PCA vectors per electrode
      nSamplesPerWindow  int   window length used for PCA
      recShift           int   first-sample offset within the spike
      isCentered         bool  did process_pca subtract the per-electrode mean
      means              list of np.ndarray, one per electrode, each shape
                                (nSamplesPerWindow,)
      eigvecs            list of np.ndarray, one per electrode, each shape
                                (nSamplesPerWindow, nPCsPerElectrode)
                                — column k is PCk for that electrode
      headerKind         str   'legacy' or 'PCAE'
    """
    with open(path, "rb") as f:
        # Peek the first int32 to decide header layout
        first_word_bytes = f.read(4)
        if len(first_word_bytes) != 4:
            raise ValueError(f"{path}: file is empty or truncated")
        (w0,) = struct.unpack("i", first_word_bytes)

        if w0 == PCAE_MAGIC:
            # PCAE: magic, version, nElectrodes, nSamplesPerWindow,
            #       nPCsPerElectrode, recShift, isCentered
            hdr = f.read(24)
            if len(hdr) != 24:
                raise ValueError(f"{path}: truncated PCAE header")
            (version, nElectrodes, nSamplesPerWindow,
             nPCsPerElectrode, recShift, isCentered) = struct.unpack("6i", hdr)
            if version != 1:
                raise ValueError(f"{path}: unsupported PCAE version {version}")
            header_kind = "PCAE"
        else:
            # Legacy: nElectrodes=w0, nSamplesPerWindow, nPCsPerElectrode,
            #         isCentered, recShift
            hdr = f.read(16)
            if len(hdr) != 16:
                raise ValueError(f"{path}: truncated legacy header "
                                 f"(only {len(hdr)+4} bytes)")
            (nSamplesPerWindow, nPCsPerElectrode,
             isCentered, recShift) = struct.unpack("4i", hdr)
            nElectrodes = w0
            header_kind = "legacy"

        if nElectrodes <= 0 or nSamplesPerWindow <= 0 or nPCsPerElectrode <= 0:
            raise ValueError(f"{path}: invalid dimensions "
                             f"(nElectrodes={nElectrodes}, "
                             f"nSamplesPerWindow={nSamplesPerWindow}, "
                             f"nPCsPerElectrode={nPCsPerElectrode})")

        # On-disk layout matches process_pca writer and KK::RefeaturizeFromShifts:
        # ALL per-electrode means first, THEN ALL per-electrode eigenvectors.
        means = []
        for elec in range(nElectrodes):
            buf = f.read(nSamplesPerWindow * 8)
            if len(buf) != nSamplesPerWindow * 8:
                raise ValueError(f"{path}: truncated mean for electrode {elec}")
            means.append(np.frombuffer(buf, dtype=np.float64).copy())

        eigvecs = []
        for elec in range(nElectrodes):
            n_doubles = nSamplesPerWindow * nPCsPerElectrode
            buf = f.read(n_doubles * 8)
            if len(buf) != n_doubles * 8:
                raise ValueError(f"{path}: truncated eigvec for electrode {elec}")
            ev_flat = np.frombuffer(buf, dtype=np.float64).copy()
            # On-disk layout for one electrode is column-major in
            # (nSamplesPerWindow rows × nPCsPerElectrode cols), with the PC
            # index varying slowest.  Reshape (nPCs, nSamples) and transpose
            # to get (nSamples, nPCs) so ev[:, k] is PC k as a function of
            # sample index.
            eigvecs.append(
                ev_flat.reshape(nPCsPerElectrode, nSamplesPerWindow).T)

        # Trailing bytes? Warn but don't fail.
        trailing = f.read()
        if trailing:
            print(f"  warning: {path} has {len(trailing)} extra trailing bytes")

    return dict(nElectrodes=nElectrodes,
                nPCsPerElectrode=nPCsPerElectrode,
                nSamplesPerWindow=nSamplesPerWindow,
                recShift=recShift,
                isCentered=bool(isCentered),
                means=means, eigvecs=eigvecs,
                headerKind=header_kind)


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


# ---------------------------------------------------------------------------
# File discovery — handles both .pca.N and .pcaD.N
# ---------------------------------------------------------------------------
def discover_files(session, pca_dir, variants, requested_groups):
    """Return list of (variant, group, path) tuples for the requested
    variants. variants is a subset of {'raw', 'stderiv'}.

    If requested_groups is empty, every group with a matching file is included.
    """
    # Map variant → file extension used by process_pca
    ext_for = {"raw": "pca", "stderiv": "pcaD"}
    found_by_variant = {}
    for v in variants:
        ext = ext_for[v]
        pattern = os.path.join(pca_dir, f"{session}.{ext}.*")
        # Filter to integer-suffixed group ids only
        for fp in sorted(glob.glob(pattern)):
            suffix = os.path.splitext(fp)[1].lstrip(".")
            try:
                g = int(suffix)
            except ValueError:
                continue
            found_by_variant.setdefault(v, {})[g] = fp

    if not found_by_variant:
        return []

    if requested_groups:
        groups = list(requested_groups)
    else:
        groups = sorted({g for d in found_by_variant.values() for g in d})

    out = []
    for g in groups:
        for v in variants:
            d = found_by_variant.get(v, {})
            if g in d:
                out.append((v, g, d[g]))
    return out


# ---------------------------------------------------------------------------
# Plotting
# ---------------------------------------------------------------------------
def plot_group(pca, group_num, sampling_rate, title_prefix, save_path=None):
    nElectrodes       = pca["nElectrodes"]
    nPCsPerElectrode  = pca["nPCsPerElectrode"]
    nSamplesPerWindow = pca["nSamplesPerWindow"]
    recShift          = pca["recShift"]

    dt = 1.0 / sampling_rate * 1000.0 if sampling_rate else 1.0
    unit = "ms" if sampling_rate else "samples"
    xs = (np.arange(nSamplesPerWindow) + recShift) * dt

    # Layout: rows = electrodes, cols = mean + nPCsPerElectrode eigenvectors
    ncols = 1 + nPCsPerElectrode
    fig = plt.figure(figsize=(3.0 * ncols, 2.2 * nElectrodes + 0.8))
    gs  = gridspec.GridSpec(nElectrodes, ncols, figure=fig,
                            hspace=0.45, wspace=0.35)

    cmap = plt.cm.plasma
    pc_colors = [cmap(0.1 + 0.7 * k / max(nPCsPerElectrode - 1, 1))
                 for k in range(nPCsPerElectrode)]

    for elec in range(nElectrodes):
        mu = pca["means"][elec]
        ev = pca["eigvecs"][elec]  # shape (nSamplesPerWindow, nPCsPerElectrode)

        # Mean waveform
        ax0 = fig.add_subplot(gs[elec, 0])
        ax0.plot(xs, mu, color="steelblue", linewidth=1.2)
        ax0.axhline(0, color="gray", linewidth=0.4, linestyle="--")
        ax0.set_ylabel(f"elec {elec}", fontsize=7, labelpad=2)
        if elec == 0:
            ax0.set_title("Mean", fontsize=8)
        if elec == nElectrodes - 1:
            ax0.set_xlabel(unit, fontsize=7)
        ax0.tick_params(labelsize=6)
        ax0.spines[["top", "right"]].set_visible(False)

        # Per-electrode eigenvectors (one subplot per PC)
        for k in range(nPCsPerElectrode):
            ax = fig.add_subplot(gs[elec, k + 1])
            ax.plot(xs, ev[:, k], color=pc_colors[k], linewidth=1.2)
            ax.axhline(0, color="gray", linewidth=0.4, linestyle="--")
            if elec == 0:
                ax.set_title(f"PC{k+1}", fontsize=8, color=pc_colors[k])
            if elec == nElectrodes - 1:
                ax.set_xlabel(unit, fontsize=7)
            ax.tick_params(labelsize=6)
            ax.spines[["top", "right"]].set_visible(False)

    centered_str = "centered" if pca["isCentered"] else "uncentered"
    fig.suptitle(
        f"{title_prefix}  group {group_num}  "
        f"({nElectrodes} electrodes × {nPCsPerElectrode} PCs/electrode, "
        f"{nSamplesPerWindow}-sample window, {centered_str}, "
        f"{pca['headerKind']} header)",
        fontsize=9, y=1.01
    )

    if save_path:
        fig.savefig(save_path, bbox_inches="tight", dpi=150)
        print(f"  saved: {save_path}")
        plt.close(fig)
    else:
        plt.show()


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
def main():
    ap = argparse.ArgumentParser(
        description="Visualise .pca.N / .pcaD.N eigenvectors from "
                    "ndm_pca / ndm_pca_stderiv")
    ap.add_argument("session",  help="session name")
    ap.add_argument("groups",   nargs="*", type=int,
                    help="electrode group numbers (default: all found)")
    ap.add_argument("--pca-dir", default=".",
                    help="directory containing .pca.N / .pcaD.N files (default: .)")
    ap.add_argument("--sampling-rate", type=int, default=None,
                    help="sampling rate Hz for ms x-axis")
    ap.add_argument("--save", default=None,
                    help="save as PREFIX_<variant>_groupN.pdf instead of displaying")
    ap.add_argument("--variant", choices=("raw", "stderiv", "both"),
                    default="both",
                    help="which file variant(s) to plot (default: both)")
    ap.add_argument("--stderiv", action="store_true",
                    help="[legacy] equivalent to --variant stderiv")
    args = ap.parse_args()

    session = args.session
    pca_dir = args.pca_dir
    sr      = args.sampling_rate

    # Resolve variant set: --stderiv is a legacy alias and overrides --variant
    # only if the user didn't explicitly set --variant on the command line.
    if args.stderiv and args.variant == "both":
        variants = ("stderiv",)
    elif args.variant == "both":
        variants = ("raw", "stderiv")
    else:
        variants = (args.variant,)

    if sr is None:
        sr = detect_sampling_rate(session, pca_dir)
        if sr:
            print(f"  detected sampling rate: {sr} Hz")

    targets = discover_files(session, pca_dir, variants, args.groups)
    if not targets:
        ext_for = {"raw": "pca", "stderiv": "pcaD"}
        kinds = ", ".join(f".{ext_for[v]}.*" for v in variants)
        sys.exit(f"error: no files matching '{session}{{{kinds}}}' "
                 f"in '{pca_dir}'")

    label_for = {"raw": "PCA", "stderiv": "stderiv-PCA"}
    saved_any = False

    for variant, g, path in targets:
        print(f"  reading {path}")
        try:
            pca = read_pca_file(path)
        except Exception as e:
            print(f"  error: {e}")
            continue

        title_prefix = f"{session}  [{label_for[variant]}]"

        if args.save:
            save_path = f"{args.save}_{variant}_group{g}.pdf"
        else:
            save_path = None

        plot_group(pca, g, sr, title_prefix, save_path)
        if save_path:
            saved_any = True

    if args.save and not saved_any:
        sys.exit("error: nothing was saved (all reads failed)")


if __name__ == "__main__":
    main()

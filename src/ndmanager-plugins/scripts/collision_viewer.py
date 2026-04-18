#!/usr/bin/env python3
"""
collision_viewer.py  —  Diagnostic viewer for ndm_decomposecollisions output.

Panels
------
  Left:   Collision browser — filterable table of all candidate records.
          Columns: group, spike#, timestamp, units (k1+k2), residual, accepted.
  Centre: Waveform panel — 2×2 grid of subplots per tetrode channel:
            • Raw spike (grey)
            • Component 1 fit  (blue)
            • Component 2 fit  (orange)
            • Two-component sum (dashed green)
            • Residual after subtraction (red)
            • Mean waveform of primary unit (thick blue)
  Right:  Threshold panel — scatter of residual_norm vs best_single_corr for
          all candidates in the current pair (k1, k2), with current record
          highlighted and threshold lines draggable.

Usage
-----
  python3 collision_viewer.py SESSION [GROUP]
  python3 collision_viewer.py /path/to/jg05-20120316 [7]

The viewer locates all SESSION.col.N files automatically.
It reads SESSION.res.N, SESSION.clu.N, SESSION.spk.N (or .spkD.N),
SESSION.fet.N for feature-space diagnostics.
"""

from __future__ import annotations
import argparse
import math
import os
import csv
import struct
import sys
from typing import Optional

import numpy as np

from PyQt6.QtWidgets import (
    QApplication, QMainWindow, QWidget, QSplitter, QVBoxLayout, QHBoxLayout,
    QTableWidget, QTableWidgetItem, QLabel, QComboBox, QCheckBox, QPushButton,
    QSlider, QGroupBox, QHeaderView, QAbstractItemView, QStatusBar, QSizePolicy,
    QToolBar, QDoubleSpinBox, QSpinBox, QFileDialog, QTabWidget,
)
from PyQt6.QtCore import Qt, QEvent, pyqtSignal, QObject, QTimer
from PyQt6.QtGui import QColor, QBrush, QFont

import matplotlib
matplotlib.use("QtAgg")
from matplotlib.backends.backend_qtagg import FigureCanvasQTAgg as FigureCanvas
from matplotlib.figure import Figure
import matplotlib.pyplot as plt
import matplotlib.patches as mpatches

# ─────────────────────────────────────────────────────────────────────────────
# Binary .col.N reader
# ─────────────────────────────────────────────────────────────────────────────

COL_MAGIC       = b"COL\x01"
HEADER_FMT      = "<4sIIIII8s"
PARAM_FMT       = "<ffifI12s"
TEMPLATE_FMT    = "<iiifff"
RECORD_FMT      = "<qiifIfiiffiiff"
RECORD_DTYPE    = np.dtype([
    ("ts",      "<i8"), ("idx",  "<i4"), ("bsu",    "<i4"),
    ("bsc",     "<f4"), ("flags","<u4"), ("resnorm","<f4"),
    ("u1",      "<i4"), ("sh1",  "<i4"), ("sf1",    "<f4"), ("a1", "<f4"),
    ("u2",      "<i4"), ("sh2",  "<i4"), ("sf2",    "<f4"), ("a2", "<f4"),
])
TEMPLATE_DTYPE  = np.dtype([
    ("uid","<i4"),("nspk","<i4"),("dom_ch","<i4"),
    ("mean_ptp","<f4"),("pct01","<f4"),("pct99","<f4"),
])


def load_col(path: str) -> Optional[dict]:
    """Load a binary .col.N file. Returns a dict or None on error."""
    try:
        with open(path, "rb") as f:
            first4 = f.read(4)
            if first4 == b"coll" or first4[:1] in (b"{", b"-"):
                print(f"Cannot load {path}: file is in legacy YAML format.\n"
                      f"  Re-run ndm_decomposecollisions to generate "
                      f"binary .col files.", file=sys.stderr)
                return None
            if first4 != COL_MAGIC:
                print(f"Cannot load {path}: bad magic {first4!r}", file=sys.stderr)
                return None
            hdr = struct.unpack(HEADER_FMT, first4 + f.read(28))
            prm = struct.unpack(PARAM_FMT, f.read(32))
            n_spikes, n_records, n_templates, group_idx, flags = hdr[1:6]
            is_stderiv   = bool(flags & 2)
            exclude_noise = bool(flags & 1)
            tmpls = np.frombuffer(f.read(n_templates * 24), dtype=TEMPLATE_DTYPE).copy()
            recs  = np.frombuffer(f.read(n_records  * 60), dtype=RECORD_DTYPE).copy()
    except Exception as e:
        print(f"Cannot load {path}: {e}", file=sys.stderr)
        return None

    return {
        "path":          path,
        "group":         group_idx,
        "n_spikes":      n_spikes,
        "n_records":     n_records,
        "is_stderiv":    is_stderiv,
        "exclude_noise": exclude_noise,
        "corr_thresh":   prm[0],
        "resid_thresh":  prm[1],
        "max_shift":     prm[2],
        "min_snr":       prm[3],
        "min_spk_tmpl":  prm[4],
        "templates":     tmpls,
        "records":       recs,
    }


# ─────────────────────────────────────────────────────────────────────────────
# Data loaders (.res, .clu, .spk/.spkD, .fet)
# ─────────────────────────────────────────────────────────────────────────────

def load_res(session: str, g: int) -> Optional[np.ndarray]:
    """Read .res.N — binary little-endian int64, no header."""
    p = f"{session}.res.{g}"
    if not os.path.isfile(p):
        return None
    return np.fromfile(p, dtype="<i8")


def load_clu(session: str, g: int) -> Optional[np.ndarray]:
    """Read .clu.N — binary: int32 nClusters header + int32[] ids."""
    p = f"{session}.clu.{g}"
    if not os.path.isfile(p):
        return None
    raw = np.fromfile(p, dtype="<i4")
    if len(raw) < 2:
        return None
    return raw[1:]  # raw[0] = nClusters header


def load_spk(session: str, g: int, n_sites: int, n_samp: int) -> tuple[Optional[np.ndarray], bool]:
    """Returns (waveforms float32 [n_spk, n_samp, n_sites], is_stderiv)."""
    for ext, stderiv in [(f".spkD.{g}", True), (f".spk.{g}", False)]:
        p = session + ext
        if os.path.isfile(p):
            raw    = np.fromfile(p, dtype="<i2")
            stride = n_samp * n_sites
            n_spk  = raw.size // stride
            if n_spk > 0:
                wf = raw[:n_spk * stride].reshape(n_spk, n_samp, n_sites).astype(np.float32)
                return wf, stderiv
    return None, False


def load_fet(session: str, g: int) -> Optional[np.ndarray]:
    """Load .fetD.N or .fet.N  →  (n_spikes, n_dims) float64."""
    for ext in (f".fetD.{g}", f".fet.{g}"):
        p = session + ext
        if os.path.isfile(p):
            with open(p, "rb") as f:
                ndim = struct.unpack("<i", f.read(4))[0]
            raw = np.fromfile(p, dtype="<i8", offset=4)
            n   = raw.size // ndim
            return raw[:n * ndim].reshape(n, ndim).astype(np.float64)
    return None


def read_group_params(yaml_path: str, g: int) -> dict:
    """Minimal YAML reader for spikeDetection group params (no pyyaml dependency)."""
    defaults = {"n_samples": 52, "peak_sample_idx": 26, "channels": []}
    if not os.path.isfile(yaml_path):
        return defaults
    try:
        import yaml
        with open(yaml_path) as f:
            p = yaml.safe_load(f) or {}
        groups = (p.get("spikeDetection") or {}).get("channelGroups", [])
        if 0 <= g - 1 < len(groups):
            grp = groups[g - 1]
            return {
                "n_samples":       int(grp.get("nSamples",       52)),
                "peak_sample_idx": int(grp.get("peakSampleIndex", 26)),
                "channels":        [int(c) for c in grp.get("channels", [])],
            }
    except Exception:
        pass
    return defaults


# ─────────────────────────────────────────────────────────────────────────────
# Waveform reconstruction helpers
# ─────────────────────────────────────────────────────────────────────────────

def apply_shift(tmpl: np.ndarray, tau: int, n_samp: int) -> np.ndarray:
    """Linearly shift template by tau samples with zero-padding at the edges.
    Mirrors exactly the slice convention used in fit_amplitude:
      tau >= 0: wf[tau:] aligned with tmpl[0:n_samp-tau]  → out[tau:] = tmpl[:n_samp-tau]
      tau <  0: wf[0:n_samp+tau] aligned with tmpl[-tau:] → out[:n_samp+tau] = tmpl[-tau:]
    """
    out = np.zeros((n_samp, tmpl.shape[1]), dtype=np.float64)
    if tau >= 0:
        length = min(n_samp - tau, tmpl.shape[0])
        if length > 0:
            out[tau:tau + length, :] = tmpl[:length, :]
    else:
        length = min(n_samp + tau, tmpl.shape[0] + tau)
        if length > 0:
            out[:length, :] = tmpl[-tau:-tau + length, :]
    return out


def compute_residual(
    wf:    np.ndarray,   # (n_samp, n_sites) raw spike
    tmpl1: np.ndarray,   # (n_samp, n_sites) mean template unit 1
    a1:    float,
    tau1:  int,
    tmpl2: np.ndarray,   # (n_samp, n_sites) mean template unit 2
    a2:    float,
    tau2:  int,
) -> tuple[np.ndarray, np.ndarray, np.ndarray, np.ndarray]:
    """
    Returns (comp1_wf, comp2_wf, twocomp_sum, residual).
    Each is (n_samp, n_sites).
    """
    n_samp = wf.shape[0]
    c1  = a1 * apply_shift(tmpl1, tau1, n_samp)
    c2  = a2 * apply_shift(tmpl2, tau2, n_samp)
    s12 = c1 + c2
    res = wf.astype(np.float64) - s12
    return c1, c2, s12, res


# ─────────────────────────────────────────────────────────────────────────────
# Session state
# ─────────────────────────────────────────────────────────────────────────────

class SessionData:
    """Holds all loaded data for one session."""

    def __init__(self, session_path: str):
        self.session = session_path
        self.yaml    = session_path + ".yaml"
        self.groups: dict[int, dict] = {}   # group_idx → data dict
        self._col_data:  dict[int, dict]          = {}
        self._wf_cache:  dict[int, np.ndarray]    = {}
        self._res_cache: dict[int, np.ndarray]    = {}
        self._clu_cache: dict[int, np.ndarray]    = {}
        self._fet_cache: dict[int, np.ndarray]    = {}
        self._tmpl_cache: dict[tuple, np.ndarray] = {}   # (g, uid) → mean wf

    def load_group(self, g: int) -> bool:
        col_path = f"{self.session}.col.{g}"
        col = load_col(col_path)
        if col is None:
            return False
        gp = read_group_params(self.yaml, g)
        col["n_samp"]  = gp["n_samples"]
        col["peak"]    = gp["peak_sample_idx"]
        col["channels"] = gp["channels"]
        n_sites = len(gp["channels"])
        # .spkD stores the same nCG channels as .spk — channel count is unchanged
        # by the stderiv transform.  Only .fetD has nChan-1 features.
        if n_sites == 0:
            # Infer directly from .spk/.spkD file size — most reliable fallback
            spk_p = f"{self.session}.spkD.{g}" if col.get("is_stderiv") \
                    else f"{self.session}.spk.{g}"
            if os.path.isfile(spk_p) and gp["n_samples"] > 0:
                raw_sz = os.path.getsize(spk_p) // 2   # int16 samples
                n_spk  = col["n_spikes"]
                if n_spk > 0:
                    n_sites = (raw_sz // n_spk) // gp["n_samples"]
        col["n_sites"] = max(n_sites, 1)
        self._col_data[g] = col

        # Load supporting files
        res = load_res(self.session, g)
        clu = load_clu(self.session, g)
        wf, _ = load_spk(self.session, g, col["n_sites"], col["n_samp"])
        fet = load_fet(self.session, g)

        self._res_cache[g] = res
        self._clu_cache[g] = clu
        self._wf_cache[g]  = wf
        self._fet_cache[g] = fet

        # Build per-unit mean templates from raw waveforms + clu
        self._build_templates(g)
        self.groups[g] = col
        return True

    def _build_templates(self, g: int) -> None:
        wf  = self._wf_cache.get(g)
        clu = self._clu_cache.get(g)
        if wf is None or clu is None:
            return
        n = min(len(wf), len(clu))
        for uid in np.unique(clu):
            idx = np.where(clu[:n] == uid)[0]
            if len(idx) < 2:
                continue
            self._tmpl_cache[(g, int(uid))] = wf[idx].astype(np.float64).mean(axis=0)

    def col(self, g: int) -> Optional[dict]:
        return self._col_data.get(g)

    def waveform(self, g: int, spike_idx: int) -> Optional[np.ndarray]:
        wf = self._wf_cache.get(g)
        if wf is None or spike_idx >= len(wf):
            return None
        return wf[spike_idx].astype(np.float64)   # (n_samp, n_sites)

    def template(self, g: int, uid: int) -> Optional[np.ndarray]:
        return self._tmpl_cache.get((g, uid))

    def features(self, g: int) -> Optional[np.ndarray]:
        return self._fet_cache.get(g)

    def clu(self, g: int) -> Optional[np.ndarray]:
        return self._clu_cache.get(g)

    def group_ids(self) -> list[int]:
        return sorted(self.groups.keys())

    @classmethod
    def discover(cls, session_path: str) -> "SessionData":
        sd = cls(session_path)
        d  = os.path.dirname(session_path) or "."
        bn = os.path.basename(session_path)
        for fname in sorted(os.listdir(d)):
            if fname.startswith(bn + ".col."):
                try:
                    g = int(fname.rsplit(".", 1)[1])
                    sd.load_group(g)
                except Exception as e:
                    print(f"Skipping {fname}: {e}", file=sys.stderr)
        return sd


# ─────────────────────────────────────────────────────────────────────────────
# Waveform canvas
# ─────────────────────────────────────────────────────────────────────────────

COLOURS = {
    "raw":   "#999999",
    "tmpl1": "#4488ff",
    "tmpl2": "#ff8844",
    "sum":   "#44cc88",
}


def allchan_corr(
    wf: np.ndarray,
    tmpl1: np.ndarray, a1: float, tau1: int,
    tmpl2: np.ndarray, a2: float, tau2: int,
) -> float:
    """Pearson r between raw spike and (a1·T1(τ1) + a2·T2(τ2)) across ALL
    channels simultaneously (flatten to 1-D before computing).
    Returns the all-channel correlation coefficient in [-1, 1].
    """
    n_samp = wf.shape[0]
    _, _, fit, _ = compute_residual(wf, tmpl1, a1, tau1, tmpl2, a2, tau2)
    a = wf.ravel().astype(np.float64)
    b = fit.ravel()
    ac, bc = a - a.mean(), b - b.mean()
    denom = float(np.sqrt((ac**2).sum() * (bc**2).sum()))
    return float(np.dot(ac, bc) / denom) if denom > 1e-12 else 0.0


class WaveformCanvas(FigureCanvas):
    """Single stacked waveform view — Klusters style.
    Channels are offset vertically on one axis.  Only raw spike and the
    two mean templates are drawn; no per-component fits or residuals.
    """

    def __init__(self, parent=None):
        self.fig = Figure(figsize=(8, 6), tight_layout=True)
        super().__init__(self.fig)
        self.setParent(parent)
        self.ax = self.fig.add_subplot(111)
        self.fig.patch.set_facecolor("#111")
        self._scale_factor = 1.0   # multiplicative scale applied to all traces
        self._last_args: Optional[tuple] = None  # for redraw on scale change
        self._setup_ax()

    def _setup_ax(self) -> None:
        self.ax.set_facecolor("#0d0d0d")
        self.ax.tick_params(left=False, labelleft=False,
                            bottom=True, colors="#555", labelsize=7)
        for spine in self.ax.spines.values():
            spine.set_color("#222")
        self.ax.set_xlabel("sample", color="#555", fontsize=8)
        self.draw_idle()

    def plot_collision(
        self,
        wf:    np.ndarray,          # (n_samp, n_sites)
        tmpl1: Optional[np.ndarray],
        a1:    float, tau1: int,
        tmpl2: Optional[np.ndarray],
        a2:    float, tau2: int,
        unit1: int, unit2: int,
        channel_labels: list[int],
    ) -> None:
        n_samp, n_sites = wf.shape
        self.ax.clear()
        self._setup_ax()

        # Store args so scale changes can redraw without needing a new record
        self._last_args = (wf, tmpl1, a1, tau1, tmpl2, a2, tau2,
                           unit1, unit2, channel_labels)

        # Channel spacing: 3× the global peak-to-peak of the raw spike,
        # scaled by _scale_factor (I/D keys increase/decrease amplitude).
        global_ptp = float(np.abs(wf).max()) or 1.0
        scale      = self._scale_factor
        spacing    = global_ptp * 3.0 / scale

        t = np.arange(n_samp)

        # All-channel correlation of raw vs fitted two-component model
        fit_corr: Optional[float] = None
        if tmpl1 is not None and tmpl2 is not None:
            try:
                fit_corr = allchan_corr(wf, tmpl1, a1, tau1, tmpl2, a2, tau2)
            except Exception:
                pass

        # Pre-compute shifted templates once for all channels
        t1_shifted = apply_shift(tmpl1, tau1, n_samp) if tmpl1 is not None else None
        t2_shifted = apply_shift(tmpl2, tau2, n_samp) if tmpl2 is not None else None
        if t1_shifted is not None and t2_shifted is not None:
            combo = t1_shifted * a1 + t2_shifted * a2  # (n_samp, n_sites)
        else:
            combo = None


        for ch in range(n_sites):
            # Invert so ch 0 is at top, ch n_sites-1 at bottom
            offset = (n_sites - 1 - ch) * spacing
            lbl = channel_labels[ch] if ch < len(channel_labels) else ch

            # Channel label on the left margin
            self.ax.text(-1.5, offset, f"ch{lbl}",
                         color="#555", fontsize=6,
                         ha="right", va="center")

            # Raw spike — grey
            self.ax.plot(t, wf[:, ch] * scale + offset,
                         color=COLOURS["raw"], lw=1.2, alpha=0.9,
                         label="raw" if ch == 0 else "_")

            # Linear combination a1·T1(τ1) + a2·T2(τ2) — green solid
            if combo is not None:
                self.ax.plot(t, combo[:, ch] * scale + offset,
                             color=COLOURS["sum"], lw=1.8, alpha=0.9,
                             label=f"a1·u{unit1}+a2·u{unit2}" if ch == 0 else "_")

            # Individual scaled+shifted templates — dashed
            if t1_shifted is not None:
                self.ax.plot(t, t1_shifted[:, ch] * a1 * scale + offset,
                             color=COLOURS["tmpl1"], lw=1.2,
                             ls="--", alpha=0.7,
                             label=f"u{unit1}" if ch == 0 else "_")

            if t2_shifted is not None:
                self.ax.plot(t, t2_shifted[:, ch] * a2 * scale + offset,
                             color=COLOURS["tmpl2"], lw=1.2,
                             ls="--", alpha=0.7,
                             label=f"u{unit2}" if ch == 0 else "_")

            # Baseline per channel
            self.ax.axhline(offset, color="#222", lw=0.5, zorder=0)

        title = f"u{unit1} + u{unit2}"
        if fit_corr is not None:
            title += f"   all-ch corr = {fit_corr:.3f}"
        self.ax.set_title(title, color="#999", fontsize=9, pad=4)
        self.ax.set_xlim(-2, n_samp)
        self.ax.legend(
            fontsize=7, loc="upper right",
            facecolor="#1a1a1a", edgecolor="#444",
            labelcolor="white", framealpha=0.8)
        self.draw_idle()

    def scale_up(self) -> None:
        """Increase waveform amplitude (I key)."""
        self._scale_factor = min(self._scale_factor * 1.4, 200.0)
        self._redraw()

    def scale_down(self) -> None:
        """Decrease waveform amplitude (D key)."""
        self._scale_factor = max(self._scale_factor / 1.4, 0.01)
        self._redraw()

    def _redraw(self) -> None:
        if self._last_args is not None:
            self.plot_collision(*self._last_args)

    def clear_plot(self) -> None:
        self._last_args = None
        self.ax.clear()
        self._setup_ax()
        self.draw_idle()


# ─────────────────────────────────────────────────────────────────────────────
# Threshold / scatter canvas
# ─────────────────────────────────────────────────────────────────────────────

class ThresholdCanvas(FigureCanvas):
    thresholds_changed = pyqtSignal(float, float)   # (corr_thresh, resid_thresh)

    def __init__(self, parent=None):
        self.fig = Figure(figsize=(5, 4), tight_layout=True)
        super().__init__(self.fig)
        self.setParent(parent)
        self.ax = self.fig.add_subplot(111)
        self.fig.patch.set_facecolor("#111")
        self._corr_thresh  = 0.85
        self._resid_thresh = 0.25
        self._h_line = None
        self._v_line = None
        self._drag   = None   # "h" or "v"
        self.mpl_connect("button_press_event",   self._on_press)
        self.mpl_connect("motion_notify_event",  self._on_motion)
        self.mpl_connect("button_release_event", self._on_release)

    def update_scatter(
        self,
        all_bsc:       np.ndarray,   # best single corr for all records in pair
        all_resid:     np.ndarray,   # residual norms
        all_accepted:  np.ndarray,   # bool
        current_idx:   int,          # index into the arrays for highlighted point
        unit1: int, unit2: int,
        pair_feat_stats: Optional[dict],
    ) -> None:
        ax = self.ax
        ax.clear()
        ax.set_facecolor("#0d0d0d")
        ax.set_xlabel("Best single-template corr", color="#888", fontsize=8)
        ax.set_ylabel("Residual norm  ||R|| / ||W||", color="#888", fontsize=8)
        ax.set_title(f"Pair u{unit1} + u{unit2}  —  {all_accepted.sum()} accepted",
                     color="#aaa", fontsize=9)
        ax.tick_params(colors="#666", labelsize=7)
        for sp in ax.spines.values():
            sp.set_color("#333")

        # Scatter: rejected (dim), accepted (bright)
        rej = ~all_accepted
        if rej.any():
            ax.scatter(all_bsc[rej], all_resid[rej], s=12, c="#444",
                       alpha=0.6, zorder=2, label="rejected")
        if all_accepted.any():
            ax.scatter(all_bsc[all_accepted], all_resid[all_accepted],
                       s=14, c="#44cc66", alpha=0.8, zorder=3, label="accepted")

        # Highlight current record
        if 0 <= current_idx < len(all_bsc):
            ax.scatter([all_bsc[current_idx]], [all_resid[current_idx]],
                       s=80, c="#ffcc00", zorder=5, marker="*", label="current")

        # Threshold lines (draggable)
        self._v_line = ax.axvline(self._corr_thresh,  color="#4488ff",
                                  lw=1.5, ls="--", label=f"corr={self._corr_thresh:.2f}")
        self._h_line = ax.axhline(self._resid_thresh, color="#ff8844",
                                  lw=1.5, ls="--", label=f"resid={self._resid_thresh:.2f}")

        # Optimal thresholds derived from pair feature stats
        if pair_feat_stats:
            opt_c = pair_feat_stats.get("opt_corr")
            opt_r = pair_feat_stats.get("opt_resid")
            if opt_c is not None:
                ax.axvline(opt_c, color="#4488ff", lw=1.0, ls=":", alpha=0.5,
                           label=f"opt corr={opt_c:.2f}")
            if opt_r is not None:
                ax.axhline(opt_r, color="#ff8844", lw=1.0, ls=":", alpha=0.5,
                           label=f"opt resid={opt_r:.2f}")

        ax.legend(fontsize=7, facecolor="#222", edgecolor="#555",
                  labelcolor="white", framealpha=0.7)
        ax.set_xlim(-0.1, 1.05)
        ax.set_ylim(-0.02, 1.05)
        self.draw_idle()

    def set_thresholds(self, corr: float, resid: float) -> None:
        self._corr_thresh  = corr
        self._resid_thresh = resid

    def _on_press(self, ev):
        if ev.inaxes != self.ax or ev.button != 1:
            return
        # Decide which line we're dragging (within 0.05 data units)
        dy = abs(ev.ydata - self._resid_thresh) if ev.ydata is not None else 999
        dx = abs(ev.xdata - self._corr_thresh)  if ev.xdata is not None else 999
        if dy < 0.05:
            self._drag = "h"
        elif dx < 0.05:
            self._drag = "v"

    def _on_motion(self, ev):
        if self._drag is None or ev.inaxes != self.ax:
            return
        if self._drag == "h" and ev.ydata is not None:
            self._resid_thresh = max(0.0, min(1.0, ev.ydata))
            if self._h_line:
                self._h_line.set_ydata([self._resid_thresh, self._resid_thresh])
                self.draw_idle()
        elif self._drag == "v" and ev.xdata is not None:
            self._corr_thresh = max(0.0, min(1.0, ev.xdata))
            if self._v_line:
                self._v_line.set_xdata([self._corr_thresh, self._corr_thresh])
                self.draw_idle()

    def _on_release(self, ev):
        if self._drag:
            self._drag = None
            self.thresholds_changed.emit(self._corr_thresh, self._resid_thresh)


# ─────────────────────────────────────────────────────────────────────────────
# Feature scatter canvas (PCA dim1 vs dim2)
# ─────────────────────────────────────────────────────────────────────────────

class FeatureCanvas(FigureCanvas):
    def __init__(self, parent=None):
        self.fig = Figure(figsize=(4, 4), tight_layout=True)
        super().__init__(self.fig)
        self.setParent(parent)
        self.ax = self.fig.add_subplot(111)
        self.fig.patch.set_facecolor("#111")

    def update_features(
        self,
        fet:          np.ndarray,   # (n_spk, n_dims)
        clu:          np.ndarray,   # (n_spk,)
        unit1:        int,
        unit2:        int,
        spike_idx:    int,
        dim_x:        int = 0,
        dim_y:        int = 1,
    ) -> None:
        ax = self.ax
        ax.clear()
        ax.set_facecolor("#0d0d0d")
        ax.set_xlabel(f"dim {dim_x + 1}", color="#888", fontsize=8)
        ax.set_ylabel(f"dim {dim_y + 1}", color="#888", fontsize=8)
        ax.set_title("Feature space", color="#aaa", fontsize=9)
        ax.tick_params(colors="#666", labelsize=7)
        for sp in ax.spines.values():
            sp.set_color("#333")

        n = min(len(fet), len(clu))
        for uid, col, zorder in [(unit1, COLOURS["tmpl1"], 3),
                                  (unit2, COLOURS["tmpl2"], 3)]:
            mask = clu[:n] == uid
            if mask.any():
                ax.scatter(fet[:n][mask, dim_x], fet[:n][mask, dim_y],
                           s=6, c=col, alpha=0.5, zorder=zorder, label=f"u{uid}")

        # All other units dimly
        other = (clu[:n] != unit1) & (clu[:n] != unit2)
        if other.any():
            ax.scatter(fet[:n][other, dim_x], fet[:n][other, dim_y],
                       s=3, c="#333", alpha=0.3, zorder=1)

        # Highlight current spike
        if 0 <= spike_idx < n:
            ax.scatter([fet[spike_idx, dim_x]], [fet[spike_idx, dim_y]],
                       s=100, c="#ffcc00", zorder=6, marker="*", label="current")

        ax.legend(fontsize=7, facecolor="#222", edgecolor="#555",
                  labelcolor="white", framealpha=0.7)
        self.draw_idle()


# ─────────────────────────────────────────────────────────────────────────────
# Pair statistics (optimal thresholds from feature-space separation)
# ─────────────────────────────────────────────────────────────────────────────

def compute_pair_stats(
    records: np.ndarray,
    unit1: int,
    unit2: int,
) -> dict:
    """
    For all records in the current pair, compute:
    - Distribution of residualNorm values
    - Distribution of bestSingleCorr values
    - Suggested thresholds: opt_corr  = 5th percentile of bsc for accepted spikes
                             opt_resid = 95th percentile of residual for accepted spikes
    """
    mask = (records["u1"] == unit1) & (records["u2"] == unit2)
    if not mask.any():
        mask = (records["u1"] == unit2) & (records["u2"] == unit1)
    sub = records[mask]
    if len(sub) == 0:
        return {}

    accepted = (sub["flags"] & 1).astype(bool)
    stats = {
        "n_total":    len(sub),
        "n_accepted": int(accepted.sum()),
        "bsc_mean":   float(sub["bsc"].mean()),
        "bsc_std":    float(sub["bsc"].std()),
        "resnorm_mean": float(sub["resnorm"].mean()),
        "resnorm_std":  float(sub["resnorm"].std()),
    }
    if accepted.any():
        # A slightly conservative threshold is the 10th pct of corr and
        # 90th pct of residual among actually-accepted collisions.
        stats["opt_corr"]  = float(np.percentile(sub["bsc"][accepted],    10))
        stats["opt_resid"] = float(np.percentile(sub["resnorm"][accepted], 90))
    return stats


# ─────────────────────────────────────────────────────────────────────────────
# Collision browser table
# ─────────────────────────────────────────────────────────────────────────────

COLS = ["Grp", "Spike#", "Timestamp", "U1", "U2", "BestCorr", "Residual", "Accepted"]

class CollisionTable(QTableWidget):
    record_selected = pyqtSignal(int, int)   # (group, record_row)

    def __init__(self, parent=None):
        super().__init__(0, len(COLS), parent)
        self.setHorizontalHeaderLabels(COLS)
        self.setSelectionBehavior(QAbstractItemView.SelectionBehavior.SelectRows)
        self.setSelectionMode(QAbstractItemView.SelectionMode.SingleSelection)
        self.setEditTriggers(QAbstractItemView.EditTrigger.NoEditTriggers)
        self.horizontalHeader().setSectionResizeMode(QHeaderView.ResizeMode.ResizeToContents)
        self.setAlternatingRowColors(True)
        self.setStyleSheet("""
            QTableWidget {background:#111; color:#ccc; gridline-color:#222;
                          alternate-background-color:#161616;}
            QHeaderView::section {background:#1a1a1a; color:#999;
                                   border:1px solid #333; padding:3px;}
        """)
        self.currentCellChanged.connect(self._on_row_change)
        self._row_to_rec: list[tuple[int, int]] = []   # (group, record_idx)

    def populate(self, session: SessionData, show_accepted_only: bool = False,
                 filter_group: int = 0, filter_pair: tuple = (0, 0)) -> None:
        self.setRowCount(0)
        self._row_to_rec.clear()
        rows = []
        for g in session.group_ids():
            col = session.col(g)
            if col is None:
                continue
            if filter_group and g != filter_group:
                continue
            recs = col["records"]
            for ri, rec in enumerate(recs):
                if show_accepted_only and not (rec["flags"] & 1):
                    continue
                u1, u2 = int(rec["u1"]), int(rec["u2"])
                if filter_pair != (0, 0) and (u1, u2) != filter_pair \
                        and (u2, u1) != filter_pair:
                    continue
                rows.append((g, ri, rec))

        # Sort descending by best-single-template correlation
        rows.sort(key=lambda x: float(x[2]["bsc"]), reverse=True)

        self.setRowCount(len(rows))
        for row, (g, ri, rec) in enumerate(rows):
            accepted = bool(rec["flags"] & 1)
            vals = [
                str(g),
                str(int(rec["idx"])),
                str(int(rec["ts"])),
                str(int(rec["u1"])),
                str(int(rec["u2"])),
                f"{rec['bsc']:.3f}",
                f"{rec['resnorm']:.3f}",
                "✓" if accepted else "",
            ]
            for col_i, v in enumerate(vals):
                item = QTableWidgetItem(v)
                item.setTextAlignment(Qt.AlignmentFlag.AlignCenter)
                if accepted:
                    item.setForeground(QBrush(QColor("#44cc66")))
                self.setItem(row, col_i, item)
            self._row_to_rec.append((g, ri))

    def _on_row_change(self, row, *_):
        if 0 <= row < len(self._row_to_rec):
            g, ri = self._row_to_rec[row]
            self.record_selected.emit(g, ri)

    def select_row_for(self, g: int, ri: int) -> None:
        try:
            row = self._row_to_rec.index((g, ri))
            self.selectRow(row)
        except ValueError:
            pass


# ─────────────────────────────────────────────────────────────────────────────
# Main window
# ─────────────────────────────────────────────────────────────────────────────

class CollisionViewer(QMainWindow):
    def __init__(self, session_path: str):
        super().__init__()
        self.setWindowTitle(f"Collision Viewer — {os.path.basename(session_path)}")
        self.resize(1600, 900)
        self.setStyleSheet("QMainWindow, QWidget { background:#111; color:#ccc; }"
                           "QSplitter::handle { background:#222; }"
                           "QLabel { color:#999; }"
                           "QPushButton { background:#222; color:#ccc; border:1px solid #444; "
                           "  padding:3px 8px; border-radius:3px; }"
                           "QPushButton:hover { background:#333; }"
                           "QComboBox { background:#1a1a1a; color:#ccc; border:1px solid #444; }"
                           "QCheckBox { color:#ccc; }"
                           "QDoubleSpinBox, QSpinBox { background:#1a1a1a; color:#ccc; "
                           "  border:1px solid #444; }")

        self.session = SessionData.discover(session_path)
        if not self.session.group_ids():
            self._no_data()
            return

        self._cur_group   = self.session.group_ids()[0]
        self._cur_rec     = 0
        self._session_path = session_path
        # Decisions: (group, rec_idx) → True (accept) / False (reject)
        # Overrides the algorithm's collision_accepted flag.
        self._decisions: dict[tuple[int,int], bool] = {}
        self._build_ui()
        self._populate_table()
        self._show_record(self._cur_group, self._cur_rec)
        # Install on QApplication so key events arrive here even when
        # the table, spinboxes, or other child widgets hold focus.
        QApplication.instance().installEventFilter(self)

    # ── no data fallback ──────────────────────────────────────────────────

    def _no_data(self):
        lbl = QLabel("No .col.N files found.\nRun ndm_decomposecollisions first.")
        lbl.setAlignment(Qt.AlignmentFlag.AlignCenter)
        lbl.setStyleSheet("font-size:18px; color:#666;")
        self.setCentralWidget(lbl)

    # ── UI construction ───────────────────────────────────────────────────

    def _build_ui(self):
        # ── Toolbar ──
        tb = QToolBar("Controls", self)
        tb.setStyleSheet("QToolBar { border:none; background:#111; spacing:6px; padding:4px; }")
        self.addToolBar(tb)

        tb.addWidget(QLabel("Group:"))
        self._grp_combo = QComboBox()
        self._grp_combo.addItem("All", 0)
        for g in self.session.group_ids():
            self._grp_combo.addItem(str(g), g)
        self._grp_combo.currentIndexChanged.connect(self._on_filter_change)
        tb.addWidget(self._grp_combo)

        tb.addSeparator()
        self._accepted_only = QCheckBox("Accepted only")
        self._accepted_only.stateChanged.connect(self._on_filter_change)
        tb.addWidget(self._accepted_only)

        tb.addSeparator()
        tb.addWidget(QLabel("Pair U1:"))
        self._u1_spin = QSpinBox(); self._u1_spin.setRange(0, 999); self._u1_spin.setValue(0)
        self._u1_spin.setSpecialValueText("any")
        tb.addWidget(self._u1_spin)
        tb.addWidget(QLabel("U2:"))
        self._u2_spin = QSpinBox(); self._u2_spin.setRange(0, 999); self._u2_spin.setValue(0)
        self._u2_spin.setSpecialValueText("any")
        tb.addWidget(self._u2_spin)
        self._u1_spin.valueChanged.connect(self._on_filter_change)
        self._u2_spin.valueChanged.connect(self._on_filter_change)

        tb.addSeparator()
        self._accept_btn = QPushButton("✓ Accept  [A]")
        self._reject_btn = QPushButton("✗ Reject  [R]")
        self._accept_btn.setStyleSheet(
            "QPushButton{background:#1a3a1a;color:#44cc66;border:1px solid #2a6a2a;}"
            "QPushButton:hover{background:#234a23;}")
        self._reject_btn.setStyleSheet(
            "QPushButton{background:#3a1a1a;color:#ff6644;border:1px solid #6a2a2a;}"
            "QPushButton:hover{background:#4a2323;}")
        self._accept_btn.clicked.connect(self._accept_current)
        self._reject_btn.clicked.connect(self._reject_current)
        tb.addWidget(self._accept_btn)
        tb.addWidget(self._reject_btn)
        tb.addSeparator()
        prev_btn = QPushButton("◀ Prev  [P]")
        next_btn = QPushButton("Next  [N] ▶")
        prev_btn.clicked.connect(self._prev_record)
        next_btn.clicked.connect(self._next_record)
        tb.addWidget(prev_btn)
        tb.addWidget(next_btn)
        tb.addSeparator()
        save_btn = QPushButton("💾 Save decisions")
        save_btn.setToolTip("Save accept/reject decisions to SESSION_decisions.csv")
        save_btn.clicked.connect(self._save_decisions)
        tb.addWidget(save_btn)

        # ── Main splitter ──
        splitter = QSplitter(Qt.Orientation.Horizontal)
        self.setCentralWidget(splitter)

        # Left: table
        left = QWidget()
        lv   = QVBoxLayout(left)
        lv.setContentsMargins(4, 4, 4, 4)
        lv.addWidget(QLabel("Collision records"))
        self._table = CollisionTable()
        self._table.record_selected.connect(self._show_record)
        lv.addWidget(self._table)
        self._stats_lbl = QLabel("")
        self._stats_lbl.setWordWrap(True)
        self._stats_lbl.setStyleSheet("font-size:11px; color:#777;")
        lv.addWidget(self._stats_lbl)
        left.setMinimumWidth(360)
        splitter.addWidget(left)

        # Centre: waveform tabs
        centre = QTabWidget()
        centre.setStyleSheet("QTabWidget::pane { border:1px solid #333; }"
                             "QTabBar::tab { background:#1a1a1a; color:#999; padding:4px 12px; }"
                             "QTabBar::tab:selected { background:#222; color:#ccc; }")
        self._wf_canvas  = WaveformCanvas()
        self._fet_canvas = FeatureCanvas()
        centre.addTab(self._wf_canvas,  "Waveforms & Residual")
        centre.addTab(self._fet_canvas, "Feature Space")
        splitter.addWidget(centre)

        # Right: threshold scatter + stats
        right = QWidget()
        rv    = QVBoxLayout(right)
        rv.setContentsMargins(4, 4, 4, 4)
        rv.addWidget(QLabel("Threshold diagnostics  (drag dashed lines)"))
        self._thresh_canvas = ThresholdCanvas()
        self._thresh_canvas.thresholds_changed.connect(self._on_thresholds_dragged)
        rv.addWidget(self._thresh_canvas)

        stats_box = QGroupBox("Pair statistics")
        stats_box.setStyleSheet("QGroupBox { color:#888; border:1px solid #333; margin-top:8px; }"
                                "QGroupBox::title { subcontrol-origin:margin; left:8px; }")
        sv = QVBoxLayout(stats_box)
        self._pair_stats_lbl = QLabel("")
        self._pair_stats_lbl.setWordWrap(True)
        self._pair_stats_lbl.setStyleSheet("font-size:11px; color:#aaa;")
        sv.addWidget(self._pair_stats_lbl)
        rv.addWidget(stats_box)
        right.setMinimumWidth(320)
        splitter.addWidget(right)

        splitter.setSizes([360, 820, 360])

        # Status bar
        self._status = QStatusBar()
        self.setStatusBar(self._status)
        self._status.setStyleSheet("QStatusBar { color:#666; background:#0d0d0d; }")

    # ── Filter / populate ──────────────────────────────────────────────────

    def _on_filter_change(self, *_):
        self._populate_table()

    def _populate_table(self):
        g        = self._grp_combo.currentData() or 0
        accepted = self._accepted_only.isChecked()
        u1       = self._u1_spin.value()
        u2       = self._u2_spin.value()
        pair     = (u1, u2) if u1 and u2 else (0, 0)
        self._table.populate(self.session, accepted, g, pair)
        n = self._table.rowCount()
        total = sum(len(c["records"]) for c in
                    [self.session.col(gg) for gg in self.session.group_ids()]
                    if c is not None)
        self._stats_lbl.setText(f"Showing {n} / {total} records")

    # ── Navigation ────────────────────────────────────────────────────────

    def _prev_record(self):
        row = self._table.currentRow()
        if row > 0:
            self._table.selectRow(row - 1)

    def _next_record(self):
        row = self._table.currentRow()
        if row < self._table.rowCount() - 1:
            self._table.selectRow(row + 1)

    # ── Core display ──────────────────────────────────────────────────────

    def _show_record(self, g: int, ri: int):
        self._cur_group = g
        self._cur_rec   = ri
        col = self.session.col(g)
        if col is None:
            return
        recs = col["records"]
        if ri >= len(recs):
            return
        rec = recs[ri]

        u1      = int(rec["u1"])
        u2      = int(rec["u2"])
        sh1     = int(rec["sh1"])
        sh2     = int(rec["sh2"])
        a1      = float(rec["a1"])
        a2      = float(rec["a2"])
        s_idx   = int(rec["idx"])
        accepted= bool(rec["flags"] & 1)

        wf     = self.session.waveform(g, s_idx)
        tmpl1  = self.session.template(g, u1)
        tmpl2  = self.session.template(g, u2)
        chan_lbl= col.get("channels", list(range(col.get("n_sites", 4))))

        # ── Waveform panel ──
        if wf is not None:
            self._wf_canvas.plot_collision(
                wf, tmpl1, a1, sh1, tmpl2, a2, sh2, u1, u2, chan_lbl)
        else:
            self._wf_canvas.clear_plot()

        # ── Feature panel ──
        fet = self.session.features(g)
        clu = self.session.clu(g)
        if fet is not None and clu is not None:
            self._fet_canvas.update_features(fet, clu, u1, u2, s_idx)

        # ── Threshold scatter for this pair ──
        mask = ((recs["u1"] == u1) & (recs["u2"] == u2)) | \
               ((recs["u1"] == u2) & (recs["u2"] == u1))
        pair_recs = recs[mask]
        within_mask = np.where(mask)[0]
        cur_in_pair = int(np.searchsorted(within_mask, ri)) \
            if ri in within_mask else -1

        pair_stats = compute_pair_stats(recs, u1, u2)
        self._thresh_canvas.set_thresholds(col["corr_thresh"], col["resid_thresh"])
        self._thresh_canvas.update_scatter(
            pair_recs["bsc"].astype(float),
            pair_recs["resnorm"].astype(float),
            (pair_recs["flags"] & 1).astype(bool),
            cur_in_pair,
            u1, u2,
            pair_stats,
        )

        # ── Pair stats label ──
        if pair_stats:
            lns = [
                f"Pair u{u1} + u{u2}",
                f"Candidates: {pair_stats['n_total']}  "
                f"  Accepted: {pair_stats['n_accepted']}",
                f"BSC  mean={pair_stats['bsc_mean']:.3f}  "
                f"std={pair_stats['bsc_std']:.3f}",
                f"Resid mean={pair_stats['resnorm_mean']:.3f}  "
                f"std={pair_stats['resnorm_std']:.3f}",
            ]
            if "opt_corr" in pair_stats:
                lns += [
                    f"Suggested corr_threshold  ≥ {pair_stats['opt_corr']:.3f}",
                    f"Suggested resid_threshold ≤ {pair_stats['opt_resid']:.3f}",
                ]
            self._pair_stats_lbl.setText("\n".join(lns))

        # ── Status bar ──
        flag_str = "✓ ACCEPTED" if accepted else "✗ rejected"
        self._status.showMessage(
            f"Group {g}  |  spike #{s_idx}  ts={int(rec['ts'])}  "
            f"u{u1}+u{u2}  sh={sh1},{sh2}  a={a1:.2f},{a2:.2f}  "
            f"resid={rec['resnorm']:.3f}  bsc={rec['bsc']:.3f}  {flag_str}")

    def _on_thresholds_dragged(self, corr: float, resid: float):
        self._status.showMessage(
            f"Thresholds updated: corr_threshold={corr:.3f}  "
            f"residual_threshold={resid:.3f}  "
            f"(update .yaml and re-run ndm_decomposecollisions to apply)")

    # ── Keyboard shortcuts ────────────────────────────────────────────
    # eventFilter is installed on QApplication so shortcuts work
    # regardless of which child widget currently holds focus.

    def eventFilter(self, obj, event):
        if event.type() == QEvent.Type.KeyPress:
            k = event.key()
            if k == Qt.Key.Key_A:
                self._accept_current(); return True
            elif k == Qt.Key.Key_R:
                self._reject_current(); return True
            elif k == Qt.Key.Key_N:
                self._next_record();    return True
            elif k == Qt.Key.Key_P:
                self._prev_record();    return True
            elif k == Qt.Key.Key_I:
                self._wf_canvas.scale_up();   return True
            elif k == Qt.Key.Key_D:
                self._wf_canvas.scale_down(); return True
        return super().eventFilter(obj, event)

    def keyPressEvent(self, event):
        # Fallback — normally handled by eventFilter above
        self.eventFilter(self, event)

    # ── Accept / Reject ───────────────────────────────────────────────

    def _accept_current(self):
        self._set_decision(True)

    def _reject_current(self):
        self._set_decision(False)

    def _set_decision(self, accepted: bool):
        key = (self._cur_group, self._cur_rec)
        col = self.session.col(self._cur_group)
        if col is None:
            return
        recs = col["records"]
        if self._cur_rec >= len(recs):
            return
        self._decisions[key] = accepted
        rec = recs[self._cur_rec]
        u1, u2 = int(rec["u1"]), int(rec["u2"])
        bsc   = float(rec["bsc"])
        resid = float(rec["resnorm"])
        verb  = "Accepted" if accepted else "Rejected"
        self._status.showMessage(
            f"{verb}  group {self._cur_group}  spike #{int(rec["idx"])}"  
            f"  u{u1}+u{u2}  corr={bsc:.3f}  resid={resid:.3f}"  
            f"  ({len(self._decisions)} decisions stored)")
        self._refresh_table_row()
        self._update_decision_stats()
        # Auto-advance to next record
        self._next_record()

    def _refresh_table_row(self):
        """Recolour the current table row to reflect the manual decision."""
        row = self._table.currentRow()
        if row < 0:
            return
        key = (self._cur_group, self._cur_rec)
        if key not in self._decisions:
            return
        is_acc = self._decisions[key]
        colour = QColor("#22441a") if is_acc else QColor("#441a1a")
        for col_i in range(self._table.columnCount()):
            item = self._table.item(row, col_i)
            if item:
                item.setBackground(QBrush(colour))
                item.setForeground(QBrush(QColor("#66ff44" if is_acc else "#ff6644")))

    def _update_decision_stats(self):
        """Recompute and display per-pair threshold suggestions from decisions."""
        if not self._decisions:
            return
        col = self.session.col(self._cur_group)
        if col is None:
            return
        recs = col["records"]
        # Gather BSC values for manually-accepted records in the current pair
        rec = recs[self._cur_rec] if self._cur_rec < len(recs) else None
        if rec is None:
            return
        u1, u2 = int(rec["u1"]), int(rec["u2"])
        pair_acc_bsc   = []
        pair_acc_resid = []
        pair_rej_bsc   = []
        pair_rej_resid = []
        for ri, r in enumerate(recs):
            ru1, ru2 = int(r["u1"]), int(r["u2"])
            if not ((ru1==u1 and ru2==u2) or (ru1==u2 and ru2==u1)):
                continue
            dec = self._decisions.get((self._cur_group, ri))
            if dec is True:
                pair_acc_bsc.append(float(r["bsc"]))
                pair_acc_resid.append(float(r["resnorm"]))
            elif dec is False:
                pair_rej_bsc.append(float(r["bsc"]))
                pair_rej_resid.append(float(r["resnorm"]))
        lns = [f"Manual decisions  —  pair u{u1}+u{u2}"]
        lns.append(f"  Accepted: {len(pair_acc_bsc)}   Rejected: {len(pair_rej_bsc)}")
        if pair_acc_bsc:
            suggested_corr  = float(np.percentile(pair_acc_bsc,   10))
            suggested_resid = float(np.percentile(pair_acc_resid, 90))
            lns.append(f"  BSC  [acc] min={min(pair_acc_bsc):.3f}  ")
            lns.append(f"  Suggested corr_threshold  ≥ {suggested_corr:.3f}")
            lns.append(f"  Suggested resid_threshold ≤ {suggested_resid:.3f}")
        if pair_rej_bsc:
            lns.append(f"  BSC  [rej] max={max(pair_rej_bsc):.3f}")
        self._pair_stats_lbl.setText("\n".join(lns))

    # ── Save decisions ────────────────────────────────────────────────

    def _save_decisions(self):
        """Save manual accept/reject decisions to SESSION_decisions.csv."""
        if not self._decisions:
            self._status.showMessage("No decisions to save.")
            return
        out_path = self._session_path + "_decisions.csv"
        rows = []
        for (g, ri), accepted in sorted(self._decisions.items()):
            col = self.session.col(g)
            if col is None:
                continue
            recs = col["records"]
            if ri >= len(recs):
                continue
            rec = recs[ri]
            rows.append({
                "group":        g,
                "rec_idx":      ri,
                "spike_idx":    int(rec["idx"]),
                "timestamp":    int(rec["ts"]),
                "unit1":        int(rec["u1"]),
                "unit2":        int(rec["u2"]),
                "best_corr":    round(float(rec["bsc"]),    4),
                "residual":     round(float(rec["resnorm"]),4),
                "shift1":       int(rec["sh1"]),
                "shift2":       int(rec["sh2"]),
                "amplitude1":   round(float(rec["a1"]),     4),
                "amplitude2":   round(float(rec["a2"]),     4),
                "accepted":     1 if accepted else 0,
            })
        if not rows:
            return
        with open(out_path, "w", newline="") as f:
            writer = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
            writer.writeheader()
            writer.writerows(rows)
        self._status.showMessage(
            f"Saved {len(rows)} decisions → {out_path}")
        # Also print per-pair threshold summary to stdout
        pairs: dict[tuple, list] = {}
        for row in rows:
            key = (row["group"], row["unit1"], row["unit2"])
            pairs.setdefault(key, []).append(row)
        print("\n=== Per-pair threshold summary (from manual decisions) ===")
        for (g, u1, u2), precs in sorted(pairs.items()):
            acc  = [r for r in precs if r["accepted"]]
            rej  = [r for r in precs if not r["accepted"]]
            print(f"  Group {g}  u{u1}+u{u2}:  {len(acc)} accepted, {len(rej)} rejected")
            if acc:
                bsc_vals   = [r["best_corr"] for r in acc]
                resid_vals = [r["residual"]  for r in acc]
                print(f"    corr_threshold  ≥ {np.percentile(bsc_vals,   10):.3f}"  
                      f"  (min accepted corr = {min(bsc_vals):.3f})")
                print(f"    resid_threshold ≤ {np.percentile(resid_vals, 90):.3f}"  
                      f"  (max accepted resid = {max(resid_vals):.3f})")
            if rej:
                bsc_rej = [r["best_corr"] for r in rej]
                print(f"    rejected corr range: [{min(bsc_rej):.3f}, {max(bsc_rej):.3f}]")
        print("===")


# ─────────────────────────────────────────────────────────────────────────────
# Entry point
# ─────────────────────────────────────────────────────────────────────────────

def main():
    ap = argparse.ArgumentParser(description="Collision decomposition viewer")
    ap.add_argument("session", help="Session base path (e.g. /data/jg05-20120316)")
    ap.add_argument("group", nargs="?", type=int, default=0,
                    help="Pre-select a spike group (optional)")
    args = ap.parse_args()

    session_path = args.session
    # Strip .yaml / .clu.N etc if passed accidentally
    for ext in (".yaml", ".yml"):
        if session_path.endswith(ext):
            session_path = session_path[:-len(ext)]

    app = QApplication(sys.argv)
    app.setStyle("Fusion")
    win = CollisionViewer(session_path)
    win.show()

    if args.group and hasattr(win, "_grp_combo"):
        idx = win._grp_combo.findData(args.group)
        if idx >= 0:
            win._grp_combo.setCurrentIndex(idx)

    sys.exit(app.exec())


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""
process_estimategeometry.py
===========================
Estimate the site layout of a wire bundle (tetrode, octrode, …) whose
geometry is unknown, from how similar its channels' signals are.

Electrodes that sit close together record more of the same activity: the
same spikes at similar amplitudes, and the same high-frequency background.
The similarity of every channel pair, measured several independent ways, is
turned into an ordering of the pairs from "close" to "far", and the sites
are placed in the bundle's cross-section so that their distances follow that
order while no two wires overlap (centre-to-centre distance >= the wire's
outer diameter) and the bundle stays packed.

PAIR FEATURES (similarity of channels i and j)
  dat:<lo>-<hi>   magnitude-squared coherence of the raw .dat in a band,
                  averaged over the band, Fisher-transformed.  Bands from
                  300 Hz up are computed on spike-free windows only.
  fil:<lo>-<hi>   the same on the filtered .fil (spike-free windows): the
                  high-frequency background (unresolved, distant spikes).
  spike:amp       correlation, across detected spikes, of the two channels'
                  log peak-to-peak amplitudes after removing each spike's
                  mean (its spatial amplitude profile).
  spike:shape     mean correlation of the two channels' waveforms within
                  a spike.

WEIGHTS
  A feature counts only as far as it reflects distance.  When the session
  also has groups with KNOWN geometry (silicon shanks with a probe file),
  each feature's weight is its Spearman correlation with the true site
  distances on those groups (squared; <= 0 -> weight 0).  Without them, its
  split-half reliability is used (the same pair ordering from two disjoint
  halves of the recording).  The combined dissimilarity is the weighted mean
  of the per-feature pair ranks.

LAYOUT
  bundle  sites in the cross-section of a packed bundle: ordinal (non-metric)
          stress on the combined dissimilarity + hard core at the wire
          diameter + a weak pull to the centre; the scale is set by the
          packing, not by the data.
  free    plain non-metric embedding (no packing); scaled to the calibrated
          distances when calibration groups exist.
  The orientation and handedness of the result are arbitrary (the data do
  not determine them); the centroid is at (0, 0) and the long axis along x.

UNCERTAINTY
  The recording segments are resampled with replacement (--n-bootstrap);
  every replicate is embedded and aligned to the estimate (rotation and
  reflection).  Per-site SD (µm) and how often each pair are nearest
  neighbours are reported.

OUTPUT: THE PROBE FILE (per estimated group g)
  The estimate is written into the probe file bound to the group (probeId/shankIndex), at the
  group's sites (bound by channel number), centred where those sites were.  The first layout found
  there is kept as sites.geometry_backup (never overwritten by a later run).  For a bundle,
  substrate.material becomes wire-bundle and sites.plane cross-section: x and y are the two axes of
  the bundle cross-section.  probeFile.estimate.<shank> records the run: weights, stress, per-site
  SD, nearest neighbours, low-coherence wires, the comparison with the previous layout.
  - no probe file: a new SESSION.probe.<id>.probe is written, to attach in ndmanager;
  - a probe file outside the session folder (the shared library) is not changed;
  - the session YAML is not changed (a stale sitePositions_um is reported).

WHICH GROUPS
  By default: groups without a geometry, or with a wire-bundle template (drawn, not measured).
  Already estimated (probeFile.estimate): only with --overwrite.  Measured: calibration only;
  --estimate-known checks them (nothing written).  A group named with --groups is estimated
  whatever it has, and never calibrates itself.

DEPENDENCIES  python >= 3.9, numpy, scipy, pyyaml

Copyright (C) 2026 neurosuite-3 contributors
SPDX-License-Identifier: GPL-3.0-or-later
"""

from __future__ import annotations

import argparse
import datetime
import math
import os
import sys
from typing import Optional

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import probe_geometry  # noqa: E402

import numpy as np  # noqa: E402
import yaml  # noqa: E402
from scipy.optimize import minimize  # noqa: E402
from scipy.signal import butter, find_peaks, sosfiltfilt  # noqa: E402
from scipy.stats import spearmanr  # noqa: E402


def log(msg: str) -> None:
    print(msg, file=sys.stderr, flush=True)


# ─────────────────────────────────────────────────────────────────────────────
# Known geometry (probe file or inline sitePositions_um)
# ─────────────────────────────────────────────────────────────────────────────

def probe_library_paths(override: str = "", session_dir: str = "") -> list[str]:
    paths: list[str] = []
    if override:
        paths.append(override)
    if session_dir:
        paths.append(session_dir)
    env = os.environ.get("NEUROSUITE_PROBE_PATH", "")
    if env:
        paths.append(env)
    paths += [os.path.expanduser("~/.local/share/neurosuite/probes"),
              "/usr/local/share/neurosuite/probes", "/usr/share/neurosuite/probes", "probes"]
    return paths


def find_probe(probe_file: str, library_paths: list[str]) -> tuple[Optional[dict], Optional[str]]:
    """(the file's probeFile mapping, its path), or (None, None)."""
    for lib in ["", *library_paths]:
        cand = os.path.join(lib, probe_file) if lib else probe_file
        if os.path.isfile(cand):
            with open(cand) as fh:
                doc = yaml.safe_load(fh)
            return (doc or {}).get("probeFile"), cand
    return None, None


def load_probe(probe_file: str, library_paths: list[str]) -> Optional[dict]:
    return find_probe(probe_file, library_paths)[0]


def _nominal(probe: Optional[dict]) -> bool:
    """A wire-bundle probe file not written by this step (the library's general/tetrode.probe,
    an octrode template): its layout was drawn, not measured."""
    probe = probe or {}
    return (((probe.get("substrate") or {}).get("material")) == "wire-bundle"
            and str(probe.get("vendor") or "") != "estimated" and not probe.get("estimate"))


def _estimated(probe: Optional[dict], shank: int) -> bool:
    est = (probe or {}).get("estimate") or {}
    return isinstance(est, dict) and (shank in est or str(shank) in est)


def group_probe(params: dict, g: int, libs: list[str]) -> Optional[dict]:
    """The probe file bound to group g: dict(entry, probe, path, shank, offset), or None (no probeId),
    or dict(error=...) when the binding is broken."""
    groups = ((params.get("spikeDetection") or {}).get("channelGroups")) or []
    grp = groups[g - 1] if 0 < g <= len(groups) else {}
    pid = (grp or {}).get("probeId")
    if pid is None:
        return None
    entry = next((pe for pe in (params.get("probes") or []) if pe.get("probeId", pe.get("id")) == pid), None)
    if entry is None or not entry.get("probeFile"):
        return dict(error=f"probeId {pid} not in probes:")
    probe, path = find_probe(str(entry["probeFile"]), libs)
    if probe is None:
        return dict(error=f"probe file {entry['probeFile']} not found")
    return dict(entry=entry, probe=probe, path=path, pid=pid, shank=int(grp.get("shankIndex", 0) or 0),
                offset=int(entry.get("channelOffset", 0) or 0))


def known_positions(params: dict, g: int, libs: list[str]) -> tuple[Optional[np.ndarray], str, str]:
    """Site positions of 1-based group g in the group's channel order, their source, and their kind:
    "given" (a layout from a probe file or sitePositions_um: measured, or a drawn placeholder -- the
    step cannot tell), "template" (a wire-bundle template, see _nominal) or "estimated" (written by
    this step) -- or (None, reason, "").  Inline sitePositions_um first, then probeId/shankIndex ->
    probes: entry -> probe file, looked up by channel number (probe_geometry)."""
    groups = ((params.get("spikeDetection") or {}).get("channelGroups")) or []
    if not 0 < g <= len(groups):
        return None, "no such group", ""
    grp = groups[g - 1] or {}
    chans = probe_geometry.group_channels(params, g)
    gp = group_probe(params, g, libs)
    probe = gp.get("probe") if gp and "error" not in gp else None
    kind = ("estimated" if probe is not None and _estimated(probe, gp["shank"]) else
            "template" if _nominal(probe) else "given")
    inline = grp.get("sitePositions_um")
    if inline and len(inline) == len(chans) and all(p is not None for p in inline):
        P = np.array([[float(p[0]), float(p[1])] for p in inline])
        src = "sitePositions_um" + (f" (from {gp['entry']['probeFile']})" if probe is not None else "")
    elif probe is not None:
        pos = probe_geometry.positions_for_channels(probe, gp["offset"], chans)
        if any(p is None for p in pos):
            return None, f"{gp['entry']['probeFile']} has no geometry for every channel", ""
        P, src = np.array(pos, float), str(gp["entry"]["probeFile"])
    else:
        return None, (gp or {}).get("error", "no probeId"), ""
    if len({tuple(p) for p in P.tolist()}) < len(P):
        return None, f"{src}: several channels share a position (a point-sensor convention)", ""
    return P, src, kind


# ─────────────────────────────────────────────────────────────────────────────
# Signals
# ─────────────────────────────────────────────────────────────────────────────

class Signal:
    """int16 interleaved recording, read in blocks of rows (float32)."""

    def __init__(self, path: str, n_channels: int):
        self.path = path
        self.mm = np.memmap(path, np.int16, "r")
        if self.mm.size % n_channels:
            raise ValueError(f"{path}: size is not a multiple of {n_channels} channels")
        self.mm = self.mm.reshape(-1, n_channels)
        self.n = self.mm.shape[0]

    def read(self, start: int, n: int, chans: list[int]) -> np.ndarray:
        return np.asarray(self.mm[start:start + n][:, chans], np.float32)


def parse_bands(s: str, nyq: float) -> list[tuple[float, float]]:
    out = []
    for tok in (s or "").replace(" ", "").split(","):
        if not tok:
            continue
        lo, hi = (float(v) for v in tok.split("-"))
        hi = min(hi, 0.95 * nyq)
        if hi > lo:
            out.append((lo, hi))
    return out


def _pow2(x: float) -> int:
    return int(2 ** max(4, int(round(math.log2(max(x, 16))))))


def cross_spectra(X: np.ndarray, L: int, fsel: np.ndarray, ok: Optional[np.ndarray] = None,
                  step: Optional[int] = None) -> tuple[np.ndarray, int]:
    """Sum over Hann windows of X_i(f) X_j(f)* for the selected rfft bins.  Windows that are
    not entirely `ok` (spike-free) are skipped.  Returns (S (n, n, F) complex, n_windows)."""
    T, n = X.shape
    step = step or L
    starts = np.arange(0, T - L + 1, step)
    if ok is not None and starts.size:
        cs = np.r_[0, np.cumsum(~ok)]
        starts = starts[cs[starts + L] - cs[starts] == 0]
    if starts.size == 0:
        return np.zeros((n, n, fsel.size), complex), 0
    win = np.hanning(L).astype(np.float32)
    S = np.zeros((n, n, fsel.size), np.complex128)
    for b in range(0, starts.size, 256):
        st = starts[b:b + 256]
        W = X[st[:, None] + np.arange(L)[None, :]] * win[None, :, None]        # (k, L, n)
        W = W - W.mean(1, keepdims=True)
        F = np.fft.rfft(W, axis=1)[:, fsel, :]                                  # (k, F, n)
        S += np.einsum("kfi,kfj->ijf", F, np.conj(F))
    return S.astype(np.complex64), int(starts.size)


def band_coherence(S: np.ndarray, freqs: np.ndarray, band: tuple[float, float]) -> np.ndarray:
    m = (freqs >= band[0]) & (freqs < band[1])
    if not m.any():
        return np.full(S.shape[:2], np.nan)
    Sb = S[:, :, m]
    d = np.real(np.einsum("iif->if", Sb))
    with np.errstate(invalid="ignore", divide="ignore"):
        coh = np.abs(Sb) ** 2 / (d[:, None, :] * d[None, :, :])
    return np.nanmean(coh, 2)


def detect_events(F: np.ndarray, sd: np.ndarray, thr: float, dead: int) -> np.ndarray:
    """Negative peaks of the most negative normalised channel below -thr, >= dead apart."""
    z = (F / sd[None, :]).min(1)
    pk, _ = find_peaks(-z, height=thr, distance=max(1, dead))
    return pk


# ─────────────────────────────────────────────────────────────────────────────
# Per-segment accumulation (sums, so that halves and bootstrap replicates are re-sums)
# ─────────────────────────────────────────────────────────────────────────────

class GroupAccumulator:
    def __init__(self, n: int, sr: float, dat_bands, fil_bands, spike_half: int, guard: int):
        self.n, self.sr = n, sr
        self.dat_bands, self.fil_bands = dat_bands, fil_bands
        self.half, self.guard = spike_half, guard
        lo_bands = [b for b in dat_bands if b[0] < 300]
        hi_bands = [b for b in dat_bands if b[0] >= 300]
        self.L_long = _pow2(0.5 * sr)                  # 2 Hz bins for the bands below 300 Hz
        self.L_short = _pow2(0.008 * sr)
        f_long = np.fft.rfftfreq(self.L_long, 1 / sr)
        f_short = np.fft.rfftfreq(self.L_short, 1 / sr)
        self.sel_long = np.flatnonzero(np.any([(f_long >= a) & (f_long < b) for a, b in lo_bands], 0)) \
            if lo_bands else np.zeros(0, int)
        self.sel_short_dat = np.flatnonzero(np.any([(f_short >= a) & (f_short < b) for a, b in hi_bands], 0)) \
            if hi_bands else np.zeros(0, int)
        self.sel_short_fil = np.flatnonzero(np.any([(f_short >= a) & (f_short < b) for a, b in fil_bands], 0)) \
            if fil_bands else np.zeros(0, int)
        self.f_long, self.f_short = f_long, f_short
        self.lo_bands, self.hi_bands = lo_bands, hi_bands
        self.segs: list[dict] = []

    def add(self, D: Optional[np.ndarray], F: np.ndarray, sd: np.ndarray, thr: float, dead: int) -> None:
        n = self.n
        rec: dict = {}
        ev = detect_events(F, sd, thr, dead)
        ok = np.ones(len(F), bool)
        if ev.size:
            lo = np.clip(ev - self.guard, 0, len(F)); hi = np.clip(ev + self.guard + 1, 0, len(F))
            d = np.zeros(len(F) + 1, int); np.add.at(d, lo, 1); np.add.at(d, hi, -1)
            ok = np.cumsum(d)[:-1] == 0
        if D is not None and self.sel_long.size:
            rec["S_long"], rec["n_long"] = cross_spectra(D, self.L_long, self.sel_long, step=self.L_long // 2)
        if D is not None and self.sel_short_dat.size:
            rec["S_sdat"], rec["n_sdat"] = cross_spectra(D, self.L_short, self.sel_short_dat, ok=ok)
        if self.sel_short_fil.size:
            rec["S_sfil"], rec["n_sfil"] = cross_spectra(F, self.L_short, self.sel_short_fil, ok=ok)
        h = self.half
        ev = ev[(ev >= h) & (ev < len(F) - h)]
        if ev.size:
            Wv = F[ev[:, None] + np.arange(-h, h + 1)[None, :]]                 # (k, 2h+1, n)
            ptp = Wv.max(1) - Wv.min(1)
            lp = np.log(np.maximum(ptp, 1e-3))
            lp = lp - lp.mean(1, keepdims=True)
            rec["amp_s"] = lp.sum(0); rec["amp_ss"] = lp.T @ lp; rec["amp_k"] = int(ev.size)
            Wc = Wv - Wv.mean(1, keepdims=True)
            Wc = Wc / np.maximum(np.linalg.norm(Wc, axis=1, keepdims=True), 1e-6)
            rec["shape"] = np.einsum("kti,ktj->ij", Wc, Wc); rec["shape_k"] = int(ev.size)
        else:
            rec["amp_s"] = np.zeros(n); rec["amp_ss"] = np.zeros((n, n)); rec["amp_k"] = 0
            rec["shape"] = np.zeros((n, n)); rec["shape_k"] = 0
        rec["n_events"] = int(ev.size); rec["ok_frac"] = float(ok.mean())
        self.segs.append(rec)

    def feature_names(self) -> list[str]:
        names = [f"dat:{a:g}-{b:g}" for a, b in self.lo_bands] + [f"dat:{a:g}-{b:g}" for a, b in self.hi_bands]
        names += [f"fil:{a:g}-{b:g}" for a, b in self.fil_bands]
        return names + ["spike:amp", "spike:shape"]

    def features(self, idx) -> dict[str, np.ndarray]:
        """Similarity matrices (Fisher z) from the segments `idx` (with repeats for bootstrap)."""
        out: dict[str, np.ndarray] = {}
        segs = [self.segs[i] for i in idx]

        def tot(key):
            vals = [s[key] for s in segs if key in s]
            return sum(vals) if vals else None
        for key, bands, freqs, sel in (("S_long", self.lo_bands, self.f_long, self.sel_long),
                                       ("S_sdat", self.hi_bands, self.f_short, self.sel_short_dat),
                                       ("S_sfil", self.fil_bands, self.f_short, self.sel_short_fil)):
            S = tot(key)
            pre = "fil" if key == "S_sfil" else "dat"
            for b in bands:
                name = f"{pre}:{b[0]:g}-{b[1]:g}"
                if S is None or not np.any(S):
                    continue
                c = band_coherence(S, freqs[sel], b)
                out[name] = np.arctanh(np.sqrt(np.clip(c, 0, 0.999999)))
        k = tot("amp_k")
        if k and k > 10:
            s, ss = tot("amp_s"), tot("amp_ss")
            C = ss / k - np.outer(s / k, s / k)
            dd = np.sqrt(np.maximum(np.diag(C), 1e-12))
            out["spike:amp"] = np.arctanh(np.clip(C / np.outer(dd, dd), -0.999999, 0.999999))
            out["spike:shape"] = np.arctanh(np.clip(tot("shape") / tot("shape_k"), -0.999999, 0.999999))
        return out


# ─────────────────────────────────────────────────────────────────────────────
# Pair vectors, weights, combination
# ─────────────────────────────────────────────────────────────────────────────

def triu(M: np.ndarray) -> np.ndarray:
    i, j = np.triu_indices(M.shape[0], 1)
    return M[i, j]


def untriu(v: np.ndarray, n: int) -> np.ndarray:
    M = np.zeros((n, n)); i, j = np.triu_indices(n, 1)
    M[i, j] = v; M[j, i] = v
    return M


def rank01(v: np.ndarray) -> np.ndarray:
    r = np.argsort(np.argsort(v, kind="stable"), kind="stable").astype(float)
    # average ties
    for val in np.unique(v):
        m = v == val
        if m.sum() > 1:
            r[m] = r[m].mean()
    return r / max(len(v) - 1, 1)


def _spearman(a, b) -> float:
    if len(a) < 3 or np.std(a) == 0 or np.std(b) == 0:
        return float("nan")
    return float(spearmanr(a, b)[0])


def split_half_reliability(acc: GroupAccumulator, names: list[str], rng, n_splits: int = 20) -> dict[str, float]:
    nseg = len(acc.segs)
    rel = {nm: [] for nm in names}
    for _ in range(n_splits):
        p = rng.permutation(nseg); A, B = p[: nseg // 2], p[nseg // 2:]
        fa, fb = acc.features(A), acc.features(B)
        for nm in names:
            if nm in fa and nm in fb:
                rel[nm].append(_spearman(triu(fa[nm]), triu(fb[nm])))
    return {nm: float(np.nanmean(v)) if v else float("nan") for nm, v in rel.items()}


def combine(feats: dict[str, np.ndarray], weights: dict[str, float]) -> np.ndarray:
    """Weighted mean pair rank of DISSIMILARITY (0 = most similar pair, 1 = least)."""
    tot = 0.0; acc = None
    for nm, w in weights.items():
        if w <= 0 or nm not in feats:
            continue
        d = 1.0 - rank01(triu(feats[nm]))
        acc = d * w if acc is None else acc + d * w
        tot += w
    if acc is None:
        raise ValueError("no feature has a positive weight")
    return acc / tot


# ─────────────────────────────────────────────────────────────────────────────
# Embedding
# ─────────────────────────────────────────────────────────────────────────────

def pava(y: np.ndarray, w: Optional[np.ndarray] = None) -> np.ndarray:
    """Isotonic (non-decreasing) least-squares fit of y."""
    y = np.asarray(y, float); w = np.ones_like(y) if w is None else np.asarray(w, float)
    vals, wts, cnt = [], [], []
    for yi, wi in zip(y, w):
        vals.append(yi); wts.append(wi); cnt.append(1)
        while len(vals) > 1 and vals[-2] > vals[-1]:
            v = (vals[-2] * wts[-2] + vals[-1] * wts[-1]) / (wts[-2] + wts[-1])
            vals[-2:] = [v]; wts[-2:] = [wts[-2] + wts[-1]]; cnt[-2:] = [cnt[-2] + cnt[-1]]
    return np.repeat(vals, cnt)


def pair_dist(P: np.ndarray) -> np.ndarray:
    i, j = np.triu_indices(len(P), 1)
    return np.linalg.norm(P[i] - P[j], axis=1)


def _objective(x, n, dhat, wire, layout, compact):
    P = x.reshape(n, 2)
    i, j = np.triu_indices(n, 1)
    diff = P[i] - P[j]
    d = np.sqrt((diff ** 2).sum(1) + 1e-12)
    ss = (d ** 2).sum()
    r = d - dhat
    stress = (r ** 2).sum() / ss
    g_d = 2 * r / ss - 2 * d * (r ** 2).sum() / ss ** 2                      # d stress / d d
    f = stress
    if layout == "bundle":
        v = np.maximum(wire - d, 0.0) / wire
        f += 10.0 * (v ** 2).sum()
        g_d += 10.0 * (-2 * v / wire)
        C = P - P.mean(0)
        f += compact * (C ** 2).sum() / (n * wire ** 2)
    G = np.zeros_like(P)
    gd = (g_d / d)[:, None] * diff
    np.add.at(G, i, gd); np.add.at(G, j, -gd)
    if layout == "bundle":
        G += compact * 2 * (P - P.mean(0)) / (n * wire ** 2)
    return f, G.ravel()


def _fit_from(P0, order, n, wire, layout, compact, iters=40):
    P = P0.copy()
    last = np.inf
    for _ in range(iters):
        d = pair_dist(P)
        dh = np.empty_like(d); dh[order] = pava(d[order])
        if layout == "free":                                     # keep scale fixed: normalise
            dh *= math.sqrt((d ** 2).sum() / max((dh ** 2).sum(), 1e-12))
        r = minimize(_objective, P.ravel(), args=(n, dh, wire, layout, compact), jac=True,
                     method="L-BFGS-B", options=dict(maxiter=300))
        P = r.x.reshape(n, 2)
        if abs(last - r.fun) < 1e-7:
            break
        last = r.fun
    d = pair_dist(P)
    dh = np.empty_like(d); dh[order] = pava(d[order])
    stress = float(np.sqrt(((d - dh) ** 2).sum() / (d ** 2).sum()))
    return P, stress, float(last)


def classical_mds(D: np.ndarray) -> np.ndarray:
    n = len(D); J = np.eye(n) - 1.0 / n
    B = -0.5 * J @ (D ** 2) @ J
    w, V = np.linalg.eigh(B)
    o = np.argsort(w)[::-1][:2]
    return V[:, o] * np.sqrt(np.maximum(w[o], 1e-9))


def embed(diss: np.ndarray, n: int, wire: float, layout: str, rng, restarts: int = 30,
          init: Optional[np.ndarray] = None, compact: float = 0.05) -> tuple[np.ndarray, float]:
    """Sites (n, 2) whose distances follow the order of `diss` (pair vector, small = close)."""
    order = np.argsort(diss, kind="stable")
    starts = []
    if init is not None:
        starts.append(init)
    M = classical_mds(untriu(diss + 0.1, n))
    starts.append(M / max(np.median(pair_dist(M)), 1e-9) * wire * 1.5)
    while len(starts) < restarts:
        starts.append(rng.standard_normal((n, 2)) * wire)
    best = None
    for P0 in starts:
        P, stress, obj = _fit_from(P0, order, n, wire, layout, compact)
        if best is None or obj < best[2]:
            best = (P, stress, obj)
    P = best[0]
    if layout == "free":            # ordinal only: scale so the median nearest-neighbour distance = wire
        Dm = untriu(pair_dist(P), n) + np.eye(n) * 1e18
        P = P * wire / max(np.median(Dm.min(1)), 1e-12)
    return gauge(P), best[1]


def gauge(P: np.ndarray) -> np.ndarray:
    """Centroid at 0, long axis along x, site 0 at x >= 0, site 1 at y >= 0 (or the first site off
    the x axis)."""
    P = P - P.mean(0)
    _, _, Vt = np.linalg.svd(P, full_matrices=False)
    P = P @ Vt.T
    if P[0, 0] < 0:
        P[:, 0] *= -1
    k = next((i for i in range(1, len(P)) if abs(P[i, 1]) > 1e-6), None)
    if k is not None and P[k, 1] < 0:
        P[:, 1] *= -1
    return P


def procrustes(P: np.ndarray, Q: np.ndarray, scale: bool = False) -> tuple[np.ndarray, float]:
    """Align P to Q (rotation + reflection [+ scale]); returns (aligned P, RMS distance)."""
    Pc, Qc = P - P.mean(0), Q - Q.mean(0)
    U, s, Vt = np.linalg.svd(Pc.T @ Qc)
    R = U @ Vt
    c = s.sum() / max((Pc ** 2).sum(), 1e-12) if scale else 1.0
    A = c * Pc @ R + Q.mean(0)
    return A, float(np.sqrt(((A - Q) ** 2).sum(1).mean()))


# ─────────────────────────────────────────────────────────────────────────────
# Driver
# ─────────────────────────────────────────────────────────────────────────────

def parse_args(argv=None):
    p = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    p.add_argument("--param-file", required=True)
    p.add_argument("--session", required=True)
    p.add_argument("--groups", default="", help="groups to estimate, e.g. '3' or '1,4' (default: every group "
                   "without a known geometry)")
    p.add_argument("--dat", default=None, help="raw recording (default <session>.dat)")
    p.add_argument("--fil", default=None, help="high-passed recording (default <session>.fil; computed from "
                   "the .dat with --high-pass-hz when absent)")
    p.add_argument("--wire-diameter-um", type=float, default=15.0,
                   help="outer diameter of one wire incl. insulation = the closest centre-to-centre distance")
    p.add_argument("--layout", choices=["bundle", "free"], default="bundle")
    p.add_argument("--dat-bands", default="1-12,12-40,40-100,100-300,300-1000")
    p.add_argument("--fil-bands", default="1000-3000,3000-6000")
    p.add_argument("--n-segments", type=int, default=120)
    p.add_argument("--segment-sec", type=float, default=2.0)
    p.add_argument("--detect-threshold", type=float, default=5.0, help="spike detection, in robust SDs")
    p.add_argument("--spike-window-ms", type=float, default=0.5, help="half window for spike amplitudes")
    p.add_argument("--spike-guard-ms", type=float, default=1.0, help="spike-free margin for the background")
    p.add_argument("--high-pass-hz", type=float, default=800.0)
    p.add_argument("--calibrate", type=int, default=1, help="1 = weight features on groups of known geometry")
    p.add_argument("--calibration-groups", default="",
                   help="restrict calibration to these known-geometry groups (default: all of them)")
    p.add_argument("--estimate-known", type=int, default=0,
                   help="1 = also estimate groups that have a geometry, as a check (nothing written)")
    p.add_argument("--n-bootstrap", type=int, default=50)
    p.add_argument("--restarts", type=int, default=30)
    p.add_argument("--probe-library", default="")
    p.add_argument("--overwrite", type=int, default=0, help="1 = redo groups already estimated")
    p.add_argument("--seed", type=int, default=0)
    return p.parse_args(argv)


def _parse_groups(s: str) -> list[int]:
    out = []
    for tok in (s or "").replace(" ", ",").split(","):
        if not tok:
            continue
        if "-" in tok:
            a, b = tok.split("-"); out += list(range(int(a), int(b) + 1))
        else:
            out.append(int(tok))
    return out


def run(a) -> int:
    with open(a.param_file) as fh:
        params = yaml.safe_load(fh) or {}
    acq = params.get("acquisitionSystem") or {}
    nch, sr = int(acq["nChannels"]), float(acq["samplingRate"])
    session = a.session
    sdir = os.path.dirname(os.path.abspath(a.param_file))
    libs = probe_library_paths(a.probe_library, sdir)
    rng = np.random.default_rng(a.seed)
    ngroups = len(((params.get("spikeDetection") or {}).get("channelGroups")) or [])
    known = {g: known_positions(params, g, libs) for g in range(1, ngroups + 1)}
    big = {g for g in range(1, ngroups + 1) if len(probe_geometry.group_channels(params, g)) >= 3}
    kind = {g: known[g][2] for g in big if known[g][0] is not None}
    template = {g for g, k in kind.items() if k == "template"}
    estimated = {g for g, k in kind.items() if k == "estimated"}
    measured = {g for g, k in kind.items() if k == "given"}
    pending = {g: f for g in big - set(kind)
               for f in [unattached_estimate(session, probe_geometry.group_channels(params, g))] if f}
    asked = [g for g in _parse_groups(a.groups) if g in big]
    for g in _parse_groups(a.groups):
        if g not in big:
            log(f"  group {g}: no such group or fewer than 3 channels; skipped")
    if asked:                          # a group asked for by name is estimated, whatever geometry it has;
        targets = list(asked)          # one this step already estimated only with --force, as always
        if not a.overwrite:
            for g in [g for g in targets if g in estimated or g in pending]:
                log(f"  group {g}: already estimated ({pending.get(g) or known[g][1]}); --force to redo")
            targets = [g for g in targets if g not in estimated and g not in pending]
    else:
        targets = (big - set(kind) - set(pending)) | template
        if a.overwrite:
            targets |= set(pending)
        else:
            for g in sorted(pending):
                log(f"  group {g}: already estimated into {pending[g]} (not attached yet); --force to redo")
        for g in sorted(template):
            log(f"  group {g}: {known[g][1]} is a wire-bundle template (drawn, not measured): estimated, "
                "and not used for calibration")
        if a.overwrite:
            targets |= estimated
        else:
            for g in sorted(estimated):
                log(f"  group {g}: already estimated ({known[g][1]}); --force to redo")
        if a.estimate_known:
            targets |= measured
        else:
            for g in sorted(measured):
                log(f"  group {g}: geometry known ({known[g][1]}); not estimated (name the group, or "
                    "--estimateKnown to check it)")
        targets = sorted(targets)
    calib = sorted(measured - set(asked)) if a.calibrate else []
    if a.calibration_groups:
        want = set(_parse_groups(a.calibration_groups))
        calib = [g for g in calib if g in want]
    # --estimateKnown on a measured shank (not named) is a check: nothing is written
    check_only = {g for g in targets if g in measured and g not in asked}
    if not targets:
        log("nothing to estimate: every group has a geometry (or fewer than 3 channels); name a group "
            "to estimate it anyway")
        return 0
    process = sorted(set(targets) | set(calib))
    log(f"estimating group(s) {targets}" + (f"; calibration from known geometry: {calib} (never a group's own)"
                                             if calib else "; no calibration groups (split-half weights)"))

    dat_path = a.dat or f"{session}.dat"
    fil_path = a.fil or f"{session}.fil"
    D = Signal(dat_path, nch) if os.path.exists(dat_path) else None
    Fz = Signal(fil_path, nch) if os.path.exists(fil_path) else None
    if D is None and Fz is None:
        log(f"neither {dat_path} nor {fil_path} exists"); return 2
    if D is None:
        log(f"  no {dat_path}: dat: bands skipped")
    if Fz is None:
        log(f"  no {fil_path}: high-passing the .dat at {a.high_pass_hz:g} Hz")
    T = (D or Fz).n
    nyq = sr / 2
    dat_bands = parse_bands(a.dat_bands, nyq) if D is not None else []
    fil_bands = parse_bands(a.fil_bands, nyq)
    L = int(round(a.segment_sec * sr))
    nseg = max(4, min(a.n_segments, T // max(L, 1)))
    edges = np.linspace(0, T - L, nseg + 1).astype(np.int64)
    starts = np.array([rng.integers(edges[k], max(edges[k] + 1, edges[k + 1])) for k in range(nseg)])
    pad = int(0.05 * sr)
    sos = butter(3, a.high_pass_hz / nyq, btype="high", output="sos") if Fz is None else None
    chans = {g: probe_geometry.group_channels(params, g) for g in process}
    allc = sorted({c for g in process for c in chans[g]})
    col = {c: i for i, c in enumerate(allc)}
    half = int(round(a.spike_window_ms * 1e-3 * sr)); guard = int(round(a.spike_guard_ms * 1e-3 * sr))
    dead = guard

    def filt_block(s0):
        if Fz is not None:
            return Fz.read(s0, L, allc)
        lo = max(0, s0 - pad)
        X = D.read(lo, L + (s0 - lo) + pad, allc)
        return sosfiltfilt(sos, X, axis=0).astype(np.float32)[s0 - lo: s0 - lo + L]

    # robust SD per channel from a few segments
    probe_seg = starts[np.linspace(0, nseg - 1, min(8, nseg)).astype(int)]
    sd = np.median(np.vstack([np.median(np.abs(filt_block(s)), 0) for s in probe_seg]), 0) / 0.6745
    # flat channels (median |x| = 0: unconnected, blanked or a channel subset file) carry nothing;
    # they are left out of their group, and a group with fewer than 3 live channels is dropped
    flat = {c for c in allc if sd[col[c]] < 0.5}
    sd = np.maximum(sd, 1e-3)
    keep = {g: [i for i, c in enumerate(chans[g]) if c not in flat] for g in process}
    flat_in = {g: [int(c) for c in chans[g] if c in flat] for g in process}
    for g in list(process):
        if flat_in[g]:
            log(f"  group {g}: flat channel(s) {flat_in[g]} left out")
        if len(keep[g]) < 3:
            log(f"  group {g}: fewer than 3 live channels; dropped")
            process.remove(g)
            targets = [t for t in targets if t != g]; calib = [c for c in calib if c != g]
        else:
            chans[g] = [chans[g][i] for i in keep[g]]
    known = {g: ((v[0][keep[g]] if v[0] is not None and g in keep else v[0]), v[1], v[2])
             for g, v in known.items()}
    if not targets:
        log("nothing left to estimate"); return 0
    accs = {g: GroupAccumulator(len(chans[g]), sr, dat_bands, fil_bands, half, guard) for g in process}
    for k, s0 in enumerate(starts):
        Fb = filt_block(int(s0))
        Db = D.read(int(s0), L, allc) if D is not None else None
        for g in process:
            ci = [col[c] for c in chans[g]]
            accs[g].add(Db[:, ci] if Db is not None else None, Fb[:, ci], sd[ci], a.detect_threshold, dead)
        if (k + 1) % 20 == 0 or k + 1 == nseg:
            log(f"  segment {k + 1}/{nseg}")

    # ── weights: calibration (known geometry) or split-half reliability ──
    names = accs[process[0]].feature_names()
    full = {g: accs[g].features(range(nseg)) for g in process}
    # each calibration group's Spearman per measure; a target is never part of its own
    # calibration (with --estimateKnown it would pick the measures that match the very layout it
    # is then compared with)
    group_rho = {c: {nm: _spearman(triu(full[c][nm]), -pair_dist(known[c][0])) for nm in names if nm in full[c]}
                 for c in calib}

    def calibration_for(t):
        use = [c for c in calib if c != t]
        out = {}
        for nm in names:
            rs = [(group_rho[c][nm], len(triu(full[c][nm]))) for c in use
                  if nm in group_rho[c] and np.isfinite(group_rho[c][nm])]
            out[nm] = float(np.average([r for r, _ in rs], weights=[w for _, w in rs])) if rs else float("nan")
        return use, out
    for g in targets:
        n = len(chans[g])
        rel = split_half_reliability(accs[g], names, rng)
        cal_used, calib_rho = calibration_for(g)
        if cal_used:
            src = {nm: calib_rho.get(nm, float("nan")) for nm in names}
        else:
            src = rel
            if g in calib:
                log(f"  group {g}: no other group to calibrate on; split-half weights")
        weights = {nm: (max(v, 0.0) ** 2 if np.isfinite(v) else 0.0) for nm, v in src.items()}
        if not any(w > 0 for w in weights.values()):
            log(f"  group {g}: no feature carries distance information (all weights 0); skipped")
            continue
        diss = combine(full[g], weights)
        P, stress = embed(diss, n, a.wire_diameter_um, a.layout, rng, restarts=a.restarts)
        # ── bootstrap ──
        reps, nn_count = [], np.zeros((n, n))
        for b in range(a.n_bootstrap):
            idx = rng.integers(0, nseg, nseg)
            fb = accs[g].features(idx)
            try:
                db = combine(fb, weights)
            except ValueError:
                continue
            Pb, _ = embed(db, n, a.wire_diameter_um, a.layout, rng, restarts=max(5, a.restarts // 3), init=P)
            Pb, _ = procrustes(Pb, P)
            reps.append(Pb)
            Db_ = untriu(pair_dist(Pb), n) + np.eye(n) * 1e9
            nn = Db_.argmin(1)
            nn_count[np.arange(n), nn] += 1
        reps = np.array(reps)
        site_sd = np.sqrt(((reps - P[None]) ** 2).sum(2).mean(0)) if len(reps) else np.full(n, np.nan)
        # ── suspect wires: low low-band coherence with every other wire ──
        suspect = []
        lowname = next((nm for nm in names if nm.startswith("dat:") and nm in full[g]), None)
        if lowname is not None:
            C = np.tanh(full[g][lowname]) ** 2; np.fill_diagonal(C, np.nan)      # back to coherence
            # each wire's BEST partner: an end site of a row still has a coherent neighbour, a broken
            # or high-impedance wire has none
            m = np.nanmax(C, 1)
            med = np.median(m); mad = 1.4826 * np.median(np.abs(m - med)) + 1e-9
            # an outlier AND clearly less coherent (not a 0.01 wobble)
            suspect = [int(chans[g][i]) for i in np.flatnonzero(((m - med) / mad < -3.5) & (m < med - 0.1))]
        ev = sum(s["n_events"] for s in accs[g].segs)
        live = [int(c) for c in chans[g]]
        info = dict(
            date=datetime.date.today().isoformat(), channels=live, flatChannelsLeftOut=flat_in.get(g, []),
            layout=a.layout, wireDiameter_um=a.wire_diameter_um, segments=int(nseg), spikesDetected=int(ev),
            weightsFrom=("calibration groups " + ",".join(map(str, cal_used))) if cal_used else "split-half reliability",
            weights={nm: _r(w) for nm, w in weights.items() if w > 0},
            ordinalStress=round(stress, 3),
            siteSD_um={c: round(float(v), 1) for c, v in zip(live, site_sd)},
            nearestNeighbour={live[i]: {live[j]: round(float(nn_count[i, j]) / max(len(reps), 1), 2)}
                              for i in range(n) for j in [int(np.argmax(nn_count[i]))] if nn_count[i, j] > 0},
            lowCoherenceWires=suspect)
        log(f"  group {g}: weights ({info['weightsFrom']}) "
            + ", ".join(f"{nm} {w}" for nm, w in info["weights"].items()))
        log(f"  group {g}: sites (µm, ±SD) "
            + "; ".join(f"{c}: {P[i, 0]:.1f},{P[i, 1]:.1f} ±{site_sd[i]:.1f}" for i, c in enumerate(live)))
        if known[g][0] is not None:
            Q = known[g][0]
            rho = _spearman(diss, pair_dist(Q))
            _, rms = procrustes(P, Q, scale=True)
            info["previousLayout"] = dict(source=known[g][1], kind=known[g][2], spearman=_r(rho),
                                          procrustesRMS_um_scaled=round(rms, 1))
            log(f"  group {g}: vs the previous layout ({known[g][1]}, {known[g][2]}): Spearman {rho:.2f}, "
                f"scaled Procrustes RMS {rms:.1f} µm (a check only if that layout was measured)")
        log(f"  group {g}: stress {stress:.3f}; site SD median {np.nanmedian(site_sd):.1f} µm"
            + (f"; low-coherence wires {suspect} (broken or high-impedance, or in another layer)" if suspect else ""))
        if g in check_only:
            log(f"  group {g}: --estimateKnown check; {known[g][1]} not changed")
            continue
        update_probe(params, g, live, P, info, a, session, sdir, libs)
    return 0


def _r(v, k=3):
    return None if v is None or not np.isfinite(v) else round(float(v), k)


def _dump(path: str, doc: dict, header: str = "") -> None:
    with open(path, "w") as fh:
        fh.write(header)
        yaml.safe_dump(doc, fh, sort_keys=False, default_flow_style=None, width=140, allow_unicode=True)


def unattached_estimate(session: str, chans: list[int]) -> Optional[str]:
    """A SESSION.probe.<id>.probe written by this step for exactly these channels and not attached
    yet (a group without a probeId), so a rerun finds it instead of writing another."""
    import glob
    for f in sorted(glob.glob(f"{glob.escape(session)}.probe.*.probe")):
        try:
            with open(f) as fh:
                pr = (yaml.safe_load(fh) or {}).get("probeFile") or {}
        except (OSError, yaml.YAMLError):
            continue
        if str(pr.get("vendor")) != "estimated":
            continue
        for info in (pr.get("estimate") or {}).values():
            if set((info or {}).get("channels") or []) | set((info or {}).get("flatChannelsLeftOut") or []) \
                    == set(int(c) for c in chans):
                return f
    return None


def _next_probe_id(params: dict, session: str) -> int:
    used = {pe.get("probeId", pe.get("id")) for pe in (params.get("probes") or [])}
    k = 0
    while k in used or os.path.exists(f"{session}.probe.{k}.probe"):
        k += 1
    return k


def update_probe(params: dict, g: int, chans: list[int], P: np.ndarray, info: dict, a, session: str,
                 sdir: str, libs: list[str]) -> None:
    """Write the estimate into the probe file bound to group g (its sites, by channel number), keeping
    the first geometry as sites.geometry_backup.  A group without a probe file gets a new
    SESSION.probe.<id>.probe to attach; a probe file outside the session folder (the shared library)
    is not changed."""
    bundle = a.layout == "bundle"
    gp = group_probe(params, g, libs)
    if gp is None or "error" in gp:
        if gp is not None:
            log(f"  group {g}: {gp['error']}; writing a new probe file")
        path = unattached_estimate(session, probe_geometry.group_channels(params, g))
        if path is None:
            pid = _next_probe_id(params, session)
            path = f"{session}.probe.{pid}.probe"
        else:
            pid = path[:-len(".probe")].rsplit(".", 1)[-1]          # SESSION.probe.<id>.probe
        off = int(min(chans))
        rel = [int(c) - off for c in chans]
        doc = {"probeFile": {
            "version": "1.0", "vendor": "estimated", "model": f"{os.path.basename(session)} group {g}",
            "totalChannels": int(max(rel)) + 1,
            "substrate": {"material": "wire-bundle" if bundle else None, "thickness_um": None},
            "shanks": {"count": 1, "spacing_um": None, "length_mm": None},
            "sites": {"count_per_shank": len(chans), "layout": {4: "tetrode", 8: "octrode"}.get(len(chans), "bundle"),
                      "area_um2": None, "spacing_um": None,
                      "plane": "cross-section" if bundle else "unknown",
                      "geometry": [[round(float(x), 1), round(float(y), 1)] for x, y in P]},
            "channelMap": {"description": f"site j = channel {off} + map[j]", "map": None if rel == list(range(len(chans))) else rel},
            "notes": "Estimated by ndm_estimategeometry; see estimate.",
            "estimate": {0: info}}}
        _dump(path, doc)
        log(f"  group {g}: wrote {path}; attach it in ndmanager's Probes tab (probeId {pid}, channelOffset {off}) "
            "and set the group's probeId/shankIndex")
        return
    path = gp["path"]
    if os.path.dirname(os.path.abspath(path)) != os.path.abspath(sdir):
        log(f"  group {g}: {path} is outside the session folder (a shared library file) and is not changed; "
            "copy it into the session (ndmanager's Probes tab does) and run again")
        return
    with open(path) as fh:
        text = fh.read()
    header = "".join(ln for ln in text.splitlines(True)[:50] if ln.startswith("#"))
    header = header if text.startswith("#") else ""
    doc = yaml.safe_load(text) or {}
    probe = doc.setdefault("probeFile", {})
    sites = probe.setdefault("sites", {})
    site_ch = [gp["offset"] + c for c in probe_geometry.site_channels(probe)]
    missing = [c for c in chans if c not in site_ch]
    if missing:
        log(f"  group {g}: {path} has no site for channel(s) {missing}; not changed")
        return
    idx = [site_ch.index(c) for c in chans]
    geom = sites.get("geometry")
    if geom and "geometry_backup" not in sites:
        sites["geometry_backup"] = [list(p) if p is not None else None for p in geom]
    geom = list(geom or [])
    geom += [None] * (len(site_ch) - len(geom))
    old = [geom[j] for j in idx]
    if all(p is not None for p in old):
        cx, cy = np.mean(np.array(old, float), 0)
    else:
        cx, cy = gp["shank"] * float(((probe.get("shanks") or {}).get("spacing_um")) or 0.0), 0.0
    for k, j in enumerate(idx):
        geom[j] = [round(float(P[k, 0] + cx), 1), round(float(P[k, 1] + cy), 1)]
    sites["geometry"] = geom
    if bundle:
        probe.setdefault("substrate", {})
        probe["substrate"]["material"] = "wire-bundle"
        sites["plane"] = "cross-section"
    est = probe.get("estimate") if isinstance(probe.get("estimate"), dict) else {}
    est = {int(k) if str(k).isdigit() else k: v for k, v in est.items()}
    est[gp["shank"]] = info
    probe["estimate"] = est
    _dump(path, doc, header)
    log(f"  group {g}: updated {path} (shank {gp['shank']}, sites {idx[0]}..{idx[-1]})"
        + ("; the first layout is kept as sites.geometry_backup" if "geometry_backup" in sites else ""))
    grp = (((params.get("spikeDetection") or {}).get("channelGroups")) or [])[g - 1] or {}
    if grp.get("sitePositions_um"):
        log(f"  group {g}: the session YAML's sitePositions_um still holds the old layout and is read before "
            "the probe file; rerun ndm_setupgroups or remove it")


if __name__ == "__main__":
    sys.exit(run(parse_args()))

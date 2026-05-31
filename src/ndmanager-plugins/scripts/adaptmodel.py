#!/usr/bin/env python3
"""
adaptmodel.py -- history-dependent spike-waveform adaptation: fit + diagnose.

PURPOSE
-------
Curated clusters whose low-amplitude (burst-decremented) spikes smear toward
the origin in feature space get mis-assigned to smaller neighbours.  The
decrement is physiological: each spike leaves a fraction of Na+ channels
inactivated and engages slow K+/AHP currents, so the waveform of spike i
depends on the *recent firing history* of its own unit.  This module models
that dependence in WAVEFORM space and -- critically -- only DIAGNOSES it.
It never edits a .clu.  Reassignment is a separate, downstream step that must
not run on a unit whose adaptation model has not earned trust here.

MODEL (per cluster, per spike i)
--------------------------------
Two causal recovery states from the unit's own inter-spike intervals:
    fast Na+ de-inactivation availability  a_i in [0,1]   (tau_f ~ 1-10 ms)
    slow AHP / slow inactivation           g_i in [0,1]   (tau_s ~ 30-300 ms)
each a depletion-and-exponential-recovery recurrence (Tsodyks-Markram form):
    recover:  x_i   = 1 - (1 - x_{i-1}^+) * exp(-ISI_{i-1}/tau)
    deplete:  x_i^+ = x_i * (1 - u)
Three multichannel basis waveforms (rested + two difference templates) give a
linear prediction:
    w_hat_i = T_rest - (1 - a_i) * D_fast - (1 - g_i) * D_slow
Given the states this is closed-form least squares in the templates; the only
nonlinear unknowns are (tau_f, u_f, tau_s, u_s), profiled against multichannel
reconstruction error.

TRUST GATES (why this is diagnose-only)
---------------------------------------
  * cross-validated held-out waveform prediction must beat a static template
  * tau_f / tau_s must land in physiological ranges (else: drift or 2nd unit)
  * D_fast must not be collinear with dT_rest/dt (else: alignment jitter
    masquerading as a morph -- the dominant failure mode in waveform space)
A cluster failing any gate is flagged NO-TOUCH and reassignment must skip it.

I/O matches the neurosuite-3 pipeline: YAML parameter file
(acquisitionSystem.samplingRate, spikeDetection.channelGroups[].{channels,
nSamples,peakSampleIndex}); per-group .res.N (text, one sample-time per line),
.clu.N (text, header = nClusters then one label per spike), .spk.N (int16,
sample-major: w[s*nChanInGroup + c], reshaping to (nSpk, nSamples, nChan)).

Dependencies: numpy, scipy, pyyaml.
"""

from __future__ import annotations

import argparse
import os
import sys
from dataclasses import dataclass, field, asdict

import numpy as np

# adapt_format.py (the single .adapt format authority) sits beside this script;
# ensure it is importable regardless of cwd or install layout.
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import adapt_format

# ----------------------------------------------------------------------------
# Physiological priors / gate ranges (milliseconds).  Defaults are broad;
# cell-type specialisation only narrows them.
TAU_F_RANGE_MS = (0.3, 15.0)     # Na+ recovery from inactivation
TAU_S_RANGE_MS = (10.0, 500.0)   # AHP / slow inactivation
MIN_CV_IMPROVEMENT = 0.02        # held-out R^2 gain over static template
MAX_DERIV_COLLINEARITY = 0.80    # |cos(D_fast, dT_rest/dt)| above this = suspect

# klusters label convention: 0 = artifact, 1 = MUA/noise; real units >= 2.
DEFAULT_MIN_CLUSTER = 2


# ============================================================================
# Parameter file
# ============================================================================
@dataclass
class GroupParams:
    index1: int                 # 1-based electrode-group index (file suffix)
    channels: list              # absolute channel indices in this group
    n_samples: int              # spike window length
    peak_sample: int            # alignment / peak sample index

    @property
    def n_chan(self) -> int:
        return len(self.channels)


@dataclass
class SessionParams:
    sampling_rate: float
    n_channels: int
    groups: list                # list[GroupParams]

    def group(self, index1: int) -> GroupParams:
        for g in self.groups:
            if g.index1 == index1:
                return g
        raise KeyError(f"no spike group {index1} in parameter file")


def load_params(yaml_path: str) -> SessionParams:
    import yaml
    with open(yaml_path) as fh:
        doc = yaml.safe_load(fh)
    acq = doc["acquisitionSystem"]
    sr = float(acq["samplingRate"])
    nch = int(acq["nChannels"])
    groups = []
    cg = doc["spikeDetection"]["channelGroups"]
    for i, g in enumerate(cg, start=1):
        groups.append(GroupParams(
            index1=i,
            channels=[int(c) for c in g["channels"]],
            n_samples=int(g["nSamples"]),
            peak_sample=int(g.get("peakSampleIndex", g["nSamples"] // 2)),
        ))
    return SessionParams(sampling_rate=sr, n_channels=nch, groups=groups)


# ============================================================================
# Per-group data
# ============================================================================
@dataclass
class GroupData:
    times: np.ndarray           # (nSpk,) spike times in samples
    labels: np.ndarray          # (nSpk,) cluster ids
    waveforms: np.ndarray       # (nSpk, nSamples, nChan) float32
    gp: GroupParams


def _looks_text(path: str) -> bool:
    with open(path, "rb") as fh:
        head = fh.read(64)
    if not head:
        return True
    return all(c in b"0123456789+- \t\r\n.eE" for c in head)


def _read_res(path: str) -> np.ndarray:
    # neurosuite-3 fork: int64 binary, no header.  Classic klusters: ASCII.
    if _looks_text(path):
        return np.loadtxt(path, dtype=np.int64, ndmin=1)
    return np.fromfile(path, dtype="<i8")


def _read_clu(path: str) -> np.ndarray:
    # neurosuite-3 fork: int32 binary, first value = nClusters header.
    # Classic klusters: ASCII, first line = nClusters header.
    if _looks_text(path):
        raw = np.loadtxt(path, dtype=np.int64, ndmin=1)
    else:
        raw = np.fromfile(path, dtype="<i4").astype(np.int64)
    if raw.size == 0:
        return raw
    return raw[1:]  # drop the cluster-count header


def _read_spk(path: str, n_samples: int, n_chan: int) -> np.ndarray:
    flat = np.fromfile(path, dtype=np.int16)
    per = n_samples * n_chan
    if flat.size % per != 0:
        raise ValueError(f"{path}: {flat.size} int16 not divisible by "
                         f"nSamples*nChan={per}")
    nspk = flat.size // per
    # sample-major: w[spike, sample, channel]
    return flat.reshape(nspk, n_samples, n_chan).astype(np.float32)


def load_group(basename: str, gp: GroupParams) -> GroupData:
    times = _read_res(f"{basename}.res.{gp.index1}")
    labels = _read_clu(f"{basename}.clu.{gp.index1}")
    waves = _read_spk(f"{basename}.spk.{gp.index1}", gp.n_samples, gp.n_chan)
    n = min(len(times), len(labels), len(waves))
    if not (len(times) == len(labels) == len(waves)):
        sys.stderr.write(
            f"warn: group {gp.index1} length mismatch "
            f"res={len(times)} clu={len(labels)} spk={len(waves)}; "
            f"truncating to {n}\n")
    return GroupData(times[:n], labels[:n], waves[:n], gp)


# ============================================================================
# Alignment  (the make-or-break step in waveform space)
# ============================================================================
def _fourier_shift(x: np.ndarray, shift: float) -> np.ndarray:
    """Sub-sample shift along axis 0 (samples) via Fourier phase ramp."""
    n = x.shape[0]
    f = np.fft.rfft(x, axis=0)
    k = np.fft.rfftfreq(n) * 2.0 * np.pi
    phase = np.exp(-1j * k * shift)[:, None]
    return np.fft.irfft(f * phase, n=n, axis=0)


def align_waveforms(waves: np.ndarray, peak_sample: int,
                    max_lag: int = 3, iters: int = 2):
    """
    Align spikes to an iterated reference template by upsampled (parabolic)
    cross-correlation on the channel of largest template excursion, then apply
    the sub-sample shift to all channels.  Returns (aligned, shifts).

    Sub-sample misalignment is, to first order, T_rest plus a multiple of
    dT_rest/dt -- so leaving it in would let the morph absorb jitter.  We
    remove it here and separately test for residual collinearity downstream.
    """
    nspk, nsamp, nchan = waves.shape
    aligned = waves.copy()
    shifts = np.zeros(nspk, dtype=np.float64)

    for _ in range(iters):
        ref = np.median(aligned, axis=0)                     # (nsamp, nchan)
        dom = int(np.argmax(np.ptp(ref, axis=0)))                # dominant channel
        r = ref[:, dom]
        r = r - r.mean()
        rn = np.linalg.norm(r) + 1e-12
        new = np.empty_like(aligned)
        for i in range(nspk):
            s = aligned[i, :, dom]
            s = s - s.mean()
            # cross-correlation restricted to +/- max_lag
            cc = np.correlate(s, r, mode="full") / (rn * (np.linalg.norm(s) + 1e-12))
            mid = nsamp - 1
            lo, hi = mid - max_lag, mid + max_lag + 1
            window = cc[lo:hi]
            j = int(np.argmax(window))
            lag = j - max_lag
            # parabolic sub-sample refinement
            sub = 0.0
            if 0 < j < len(window) - 1:
                ym1, y0, yp1 = window[j - 1], window[j], window[j + 1]
                denom = (ym1 - 2 * y0 + yp1)
                if abs(denom) > 1e-12:
                    sub = 0.5 * (ym1 - yp1) / denom
            total = lag + sub
            shifts[i] += total
            new[i] = _fourier_shift(aligned[i], total)
        aligned = new
    return aligned, shifts


# ============================================================================
# Recovery states
# ============================================================================
def recovery_states(times_s: np.ndarray, tau_s: float, u: float) -> np.ndarray:
    """Causal availability in [0,1] just before each spike (depletion+recovery).
    times_s: spike times in SECONDS, ascending.  tau_s: time constant (s)."""
    n = len(times_s)
    out = np.ones(n, dtype=np.float64)
    if n == 0:
        return out
    avail = 1.0
    out[0] = 1.0
    post = avail * (1.0 - u)
    for i in range(1, n):
        isi = times_s[i] - times_s[i - 1]
        avail = 1.0 - (1.0 - post) * np.exp(-isi / tau_s)
        out[i] = avail
        post = avail * (1.0 - u)
    return out


def dual_states(times_s, tau_f, u_f, tau_s, u_s):
    a = recovery_states(times_s, tau_f, u_f)
    g = recovery_states(times_s, tau_s, u_s)
    return a, g


# ============================================================================
# Template profile fit + recovery-parameter optimisation
# ============================================================================
def _design(a, g):
    return np.column_stack([np.ones_like(a), 1.0 - a, 1.0 - g])  # (n,3)


def profile_templates(W, a, g):
    """Closed-form LSQ for [base, c_fast, c_slow] per pixel.
    W: (n, P).  Returns C (3,P), residual SSE, total SS."""
    X = _design(a, g)
    C, _, _, _ = np.linalg.lstsq(X, W, rcond=None)
    resid = W - X @ C
    sse = float(np.sum(resid * resid))
    tss = float(np.sum((W - W.mean(axis=0)) ** 2))
    return C, sse, tss


def fit_recovery(W, times_s, x0_ms=(3.0, 0.3, 80.0, 0.2)):
    """Optimise (tau_f,u_f,tau_s,u_s) minimising reconstruction SSE.
    Templates profiled out at each evaluation.  Returns dict."""
    from scipy.optimize import minimize

    lo = np.array([TAU_F_RANGE_MS[0], 0.01, TAU_S_RANGE_MS[0], 0.01])
    hi = np.array([TAU_F_RANGE_MS[1], 0.95, TAU_S_RANGE_MS[1], 0.95])

    def unpack(theta):
        return (theta[0] / 1000.0, theta[1], theta[2] / 1000.0, theta[3])

    def obj(theta):
        tf, uf, ts, us = unpack(theta)
        a, g = dual_states(times_s, tf, uf, ts, us)
        _, sse, _ = profile_templates(W, a, g)
        return sse

    res = minimize(obj, np.asarray(x0_ms, float), method="L-BFGS-B",
                   bounds=list(zip(lo, hi)))
    tf, uf, ts, us = unpack(res.x)
    a, g = dual_states(times_s, tf, uf, ts, us)
    C, sse, tss = profile_templates(W, a, g)
    return dict(tau_f_ms=tf * 1000.0, u_f=uf, tau_s_ms=ts * 1000.0, u_s=us,
                C=C, sse=sse, tss=tss, a=a, g=g, success=bool(res.success))


# ============================================================================
# Cross-validation + alignment-collinearity gate
# ============================================================================
def stratified_split(a):
    """Interleave by availability so train and test both span the comet."""
    order = np.argsort(a)
    test = order[::2]
    train = order[1::2]
    return np.sort(train), np.sort(test)


def cross_validate(W, times_s, theta_ms):
    tf, uf, ts, us = (theta_ms[0] / 1000.0, theta_ms[1],
                      theta_ms[2] / 1000.0, theta_ms[3])
    a, g = dual_states(times_s, tf, uf, ts, us)       # full causal history
    tr, te = stratified_split(a)
    X = _design(a, g)
    # adaptive: fit templates on train, predict test
    C, _, _, _ = np.linalg.lstsq(X[tr], W[tr], rcond=None)
    pred = X[te] @ C
    sse_ad = np.sum((W[te] - pred) ** 2)
    # static: single mean template from train
    mean_tpl = W[tr].mean(axis=0)
    sse_st = np.sum((W[te] - mean_tpl) ** 2)
    tss = np.sum((W[te] - W[te].mean(axis=0)) ** 2) + 1e-12
    return dict(cv_r2_adaptive=float(1.0 - sse_ad / tss),
                cv_r2_static=float(1.0 - sse_st / tss))


def deriv_collinearity(C, n_samples, n_chan):
    """|cos| between the fast difference template and dT_rest/dt.
    High value => the 'morph' is mostly sub-sample alignment jitter."""
    base = C[0].reshape(n_samples, n_chan)
    dfast = (-C[1]).reshape(n_samples, n_chan)
    dref = np.gradient(base, axis=0)
    x, y = dref.ravel(), dfast.ravel()
    denom = (np.linalg.norm(x) * np.linalg.norm(y)) + 1e-12
    return float(abs(np.dot(x, y) / denom))


# ============================================================================
# Per-cluster diagnosis
# ============================================================================
@dataclass
class ClusterDiagnosis:
    group: int
    cluster: int
    n_spikes: int
    tau_f_ms: float = 0.0
    u_f: float = 0.0
    tau_s_ms: float = 0.0
    u_s: float = 0.0
    cv_r2_adaptive: float = 0.0
    cv_r2_static: float = 0.0
    cv_improvement: float = 0.0
    deriv_collinearity: float = 0.0
    amp_range_frac: float = 0.0     # (max-min)/max peak amplitude across spikes
    plausible: bool = False
    flags: list = field(default_factory=list)


def diagnose_cluster(group_idx, cluster_id, times_s, W) -> ClusterDiagnosis:
    nspk, nsamp, nchan = W.shape
    d = ClusterDiagnosis(group=group_idx, cluster=cluster_id, n_spikes=nspk)
    if nspk < 50:
        d.flags.append("too-few-spikes")
        return d

    Wf = W.reshape(nspk, -1)
    fit = fit_recovery(Wf, times_s)
    cv = cross_validate(Wf, times_s, (fit["tau_f_ms"], fit["u_f"],
                                      fit["tau_s_ms"], fit["u_s"]))
    coll = deriv_collinearity(fit["C"], nsamp, nchan)

    # comet extent: peak-to-peak amplitude per spike on the dominant channel
    dom = int(np.argmax(np.ptp(W.mean(axis=0), axis=0)))
    amps = np.ptp(W[:, :, dom], axis=1)
    amp_range = float((amps.max() - amps.min()) / (amps.max() + 1e-9))

    d.tau_f_ms = fit["tau_f_ms"]; d.u_f = fit["u_f"]
    d.tau_s_ms = fit["tau_s_ms"]; d.u_s = fit["u_s"]
    d.cv_r2_adaptive = cv["cv_r2_adaptive"]
    d.cv_r2_static = cv["cv_r2_static"]
    d.cv_improvement = cv["cv_r2_adaptive"] - cv["cv_r2_static"]
    d.deriv_collinearity = coll
    d.amp_range_frac = amp_range

    # ---- trust gates ----
    if not (TAU_F_RANGE_MS[0] <= d.tau_f_ms <= TAU_F_RANGE_MS[1]):
        d.flags.append("tau_f-out-of-range")
    if not (TAU_S_RANGE_MS[0] <= d.tau_s_ms <= TAU_S_RANGE_MS[1]):
        d.flags.append("tau_s-out-of-range")
    if d.cv_improvement < MIN_CV_IMPROVEMENT:
        d.flags.append("no-cv-gain")
    if coll > MAX_DERIV_COLLINEARITY:
        d.flags.append("alignment-collinear")
    d.plausible = (len(d.flags) == 0)
    return d


# ============================================================================
# Session driver
# ============================================================================
def diagnose(basename, par, group_indices=None, min_cluster=DEFAULT_MIN_CLUSTER,
             out_path=None, verbose=True):
    params = load_params(par)
    gi = group_indices or [g.index1 for g in params.groups]
    results, artifact = [], {}
    for idx in gi:
        gp = params.group(idx)
        gd = load_group(basename, gp)
        aligned, _ = align_waveforms(gd.waveforms, gp.peak_sample)
        for cl in sorted(set(gd.labels.tolist())):
            if cl < min_cluster:
                continue
            mask = gd.labels == cl
            t = gd.times[mask].astype(np.float64) / params.sampling_rate
            order = np.argsort(t)
            t = t[order]
            W = aligned[mask][order]
            d = diagnose_cluster(idx, cl, t, W)
            results.append(d)
            if d.plausible:
                fit = fit_recovery(W.reshape(len(W), -1), t)
                artifact[f"g{idx}_c{cl}"] = dict(
                    group=idx, cluster=cl,
                    tau_f_ms=d.tau_f_ms, u_f=d.u_f,
                    tau_s_ms=d.tau_s_ms, u_s=d.u_s,
                    n_samples=gp.n_samples, n_chan=gp.n_chan,
                    templates=fit["C"].astype(np.float32))
    if verbose:
        _print_report(results)
    if out_path:
        written = _save_artifact(out_path, artifact)
        if verbose:
            n = len(artifact)
            print(f"\nmodel artifact: {written}  ({n} trusted unit(s))")
            print("NOTE: no .clu was modified.  process_reassignspikes consumes "
                  "this artifact and skips every NO-TOUCH unit.")
    return results, artifact


def _print_report(results):
    hdr = (f"{'grp':>3} {'clu':>4} {'nspk':>7} {'tauF':>6} {'uF':>5} "
           f"{'tauS':>6} {'uS':>5} {'cvAd':>6} {'cvSt':>6} {'gain':>6} "
           f"{'coll':>5} {'amp%':>5}  verdict")
    print(hdr)
    print("-" * len(hdr))
    for d in results:
        verdict = "OK" if d.plausible else "NO-TOUCH:" + ",".join(d.flags)
        print(f"{d.group:>3} {d.cluster:>4} {d.n_spikes:>7} "
              f"{d.tau_f_ms:>6.2f} {d.u_f:>5.2f} {d.tau_s_ms:>6.1f} "
              f"{d.u_s:>5.2f} {d.cv_r2_adaptive:>6.3f} {d.cv_r2_static:>6.3f} "
              f"{d.cv_improvement:>6.3f} {d.deriv_collinearity:>5.2f} "
              f"{100*d.amp_range_frac:>5.1f}  {verdict}")


def _save_artifact(path, artifact):
    """Write the .adapt binary consumed by process_reassignspikes.

    Delegates to adapt_format.write_adapt, the single Python-side definition of
    the format (kept in sync with the C++ reader in process_reassignspikes).
    """
    return adapt_format.write_adapt(path, artifact.values())


# ============================================================================
# Self-test: synthesise adapting clusters and confirm recovery
# ============================================================================
def _selftest():
    rng = np.random.default_rng(0)
    sr = 20000.0
    nsamp, nchan, peak = 32, 4, 16
    t = np.arange(nsamp)
    base = np.zeros((nsamp, nchan), np.float32)
    for c in range(nchan):
        amp = 200 - 40 * c
        base[:, c] = -amp * np.exp(-((t - peak) ** 2) / 8.0) \
                     + 0.4 * amp * np.exp(-((t - peak - 5) ** 2) / 20.0)
    dfast = 0.6 * base
    dslow = np.roll(base, 1, axis=0) * 0.3 + 0.2 * base

    tau_f_true, u_f_true, tau_s_true, u_s_true = 4.0, 0.35, 90.0, 0.25

    # Poisson-ish train with bursts
    times = np.sort(rng.exponential(0.02, 4000).cumsum())
    times = (times * sr).astype(np.int64)
    a = recovery_states(times / sr, tau_f_true / 1000, u_f_true)
    g = recovery_states(times / sr, tau_s_true / 1000, u_s_true)
    W = np.empty((len(times), nsamp, nchan), np.float32)
    for i in range(len(times)):
        w = base - (1 - a[i]) * dfast - (1 - g[i]) * dslow
        W[i] = w + rng.normal(0, 6, (nsamp, nchan))

    d = diagnose_cluster(1, 2, times / sr, W)
    print("self-test (true: tauF=4.0 uF=0.35 tauS=90 uS=0.25):")
    _print_report([d])
    ok = (d.plausible
          and abs(d.tau_f_ms - tau_f_true) < 3.0
          and d.cv_improvement > 0.05
          and d.deriv_collinearity < MAX_DERIV_COLLINEARITY)
    print("\nSELFTEST", "PASS" if ok else "FAIL")
    return 0 if ok else 1


# ============================================================================
def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    dp = sub.add_parser("diagnose", help="fit + report; writes no .clu")
    dp.add_argument("--par", required=True, help="session YAML parameter file")
    dp.add_argument("--basename", required=True,
                    help="session path stem (expects .res.N/.clu.N/.spk.N)")
    dp.add_argument("--groups", default=None,
                    help="comma-separated 1-based group indices (default all)")
    dp.add_argument("--min-cluster", type=int, default=DEFAULT_MIN_CLUSTER)
    dp.add_argument("--out", default=None, help="model artifact .npz")
    sub.add_parser("selftest", help="synthetic recovery check")
    args = ap.parse_args()

    if args.cmd == "selftest":
        return _selftest()
    groups = ([int(x) for x in args.groups.split(",")]
              if args.groups else None)
    diagnose(args.basename, args.par, groups, args.min_cluster, args.out)
    return 0


if __name__ == "__main__":
    sys.exit(main())

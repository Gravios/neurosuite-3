#!/usr/bin/env python3
"""A synthetic session for the Klusters self-test harness (klusters_selftest.cpp).

make_session(dir, name) writes <base>.yaml, .fil (int16, NTOT channels), and for group 1
under the `standard` token: .res (int64), .clu (int32 header + ids), .spk (int16, sample-major
[s * nChan + ch]), .pca (PCAE v2, fitted here) and .fet (int32 nDims + int64 rows: PCs + time).

Two units (clusters 2 and 3) of distinct shapes on 4 channels, each spike placed with a
SUB-SAMPLE offset (the template is rendered at 16x and decimated at a random phase), and the
recorded .res DELIBERATELY JITTERED by -2..+2 samples around the true peak, so a realign has
whole-sample work to do and a super-sampled realign sub-sample work.

Binary formats mirror neurofileio (neurosuite-3 core): no byte order games, little-endian host.
"""
import os
import numpy as np

SR = 20000
NTOT = 8                 # channels in the .fil
CHANS = [0, 1, 2, 3]     # the group's channels
NSAMP = 32
PEAK = 16                # 1-based peakSampleIndex
NCOMP = 3
PCAE_MAGIC = 0x50434145
PCAE_VERSION = 2


def template(kind, n, up):
    """A unit's waveform at `up` x the sampling, n * up ticks long, 4 channels, channel-major."""
    t = (np.arange(n * up) - (PEAK - 1) * up) / up      # samples relative to the peak
    if kind == 0:
        w = -500 * np.exp(-0.5 * (t / 1.6) ** 2) + 150 * np.exp(-0.5 * ((t - 4) / 3.0) ** 2)
        gains = np.array([1.0, 0.7, 0.3, 0.1])
    else:
        w = -350 * np.exp(-0.5 * (t / 2.4) ** 2) + 90 * np.exp(-0.5 * ((t - 7) / 4.0) ** 2)
        gains = np.array([0.2, 0.5, 1.0, 0.6])
    return gains[:, None] * w[None, :]


def write_pca(path, spk, nch, nsamp, ncomp):
    """Fit a PCAE basis (per channel: mean, leading eigenvectors of the window covariance)."""
    X = spk.reshape(len(spk), nsamp, nch)
    means = []
    evecs = []
    for ch in range(nch):
        x = X[:, :, ch].astype(np.float64)
        mu = x.mean(0)
        c = np.cov(x - mu, rowvar=False)
        w, v = np.linalg.eigh(c)
        order = np.argsort(w)[::-1][:ncomp]
        ev = v[:, order].T.copy()                        # [k][u]
        for k in range(ncomp):                            # sign: largest entry positive (fitPca)
            big = np.argmax(np.abs(ev[k]))
            if ev[k][big] < 0:
                ev[k] *= -1
        means.append(mu)
        evecs.append(ev.reshape(-1))                      # col-major [k * data2use + u]
    with open(path, "wb") as fh:
        np.array([PCAE_MAGIC, PCAE_VERSION, nch, nsamp, ncomp, 0, 1, 0, nch], np.int32).tofile(fh)
        for mu in means:
            mu.astype(np.float64).tofile(fh)
        for ev in evecs:
            ev.astype(np.float64).tofile(fh)
    return means, evecs


def project(spk, means, evecs, nch, nsamp, ncomp):
    X = spk.reshape(len(spk), nsamp, nch).astype(np.float64)
    out = np.zeros((len(spk), nch * ncomp), np.int64)
    for ch in range(nch):
        x = X[:, :, ch] - means[ch]
        ev = evecs[ch].reshape(ncomp, nsamp)
        out[:, ch * ncomp:(ch + 1) * ncomp] = np.rint(x @ ev.T).astype(np.int64)
    return out


def cut(fil, res, chans, nsamp, peak):
    """Windows [res - (peak - 1), + nsamp) of the group's channels, sample-major."""
    n = len(res)
    out = np.zeros((n, nsamp, len(chans)), np.int16)
    for i, r in enumerate(res):
        s = int(r) - (peak - 1)
        out[i] = fil[s:s + nsamp][:, chans]
    return out.reshape(n, -1)


def make_session(d, name, n_per_unit=300, dur=20.0, seed=7, group=1, method="standard"):
    rng = np.random.default_rng(seed)
    T = int(dur * SR)
    up = 16
    fil = rng.normal(0, 12.0, (T, NTOT))
    # the units, at sub-sample offsets, their .res jittered by whole samples
    times, labels, truth = [], [], []
    for kind, cid in ((0, 2), (1, 3)):
        tpl = template(kind, NSAMP + 8, up)              # a little longer than the window
        at = np.sort(rng.choice(np.arange(SR, T - SR, 40), n_per_unit, replace=False))
        for a in at:
            phase = rng.integers(0, up)                  # sub-sample offset: decimate at this phase
            w = tpl[:, phase::up][:, :NSAMP + 4]          # 4 channels x (NSAMP+4) samples
            s = int(a) - (PEAK - 1)                      # the template's peak lands at sample a (minus phase/up)
            fil[s:s + w.shape[1], CHANS] += w.T
            jitter = int(rng.integers(-2, 3))
            times.append(int(a) + jitter)
            labels.append(cid)
            truth.append(int(a))
    order = np.argsort(times, kind="stable")
    res = np.asarray(times, np.int64)[order]
    clu = np.asarray(labels, np.int32)[order]
    truth = np.asarray(truth, np.int64)[order]
    fil = np.clip(np.rint(fil), -32768, 32767).astype(np.int16)

    base = os.path.join(d, name)
    fil.tofile(base + ".fil")
    fil.tofile(base + ".dat")           # the trace view opens the .dat; the same bytes serve
    res.tofile(f"{base}.res.{method}.{group}")
    with open(f"{base}.clu.{method}.{group}", "wb") as fh:
        np.array([int(clu.max()) + 1], np.int32).tofile(fh)
        clu.tofile(fh)
    spk = cut(fil, res, CHANS, NSAMP, PEAK)
    spk.tofile(f"{base}.spk.{method}.{group}")
    means, evecs = write_pca(f"{base}.pca.{method}.{group}", spk, len(CHANS), NSAMP, NCOMP)
    pcs = project(spk, means, evecs, len(CHANS), NSAMP, NCOMP)
    fet = np.concatenate([pcs, res[:, None]], axis=1).astype(np.int64)
    with open(f"{base}.fet.{method}.{group}", "wb") as fh:
        np.array([fet.shape[1]], np.int32).tofile(fh)
        fet.tofile(fh)
    chans = ", ".join(str(c) for c in CHANS)
    with open(base + ".yaml", "w") as fh:
        fh.write(f"acquisitionSystem: {{nBits: 16, nChannels: {NTOT}, samplingRate: {SR}, voltageRange: 20, amplification: 1000, offset: 0}}\n"
                 "fieldPotentials: {lfpSamplingRate: 1250}\n"
                 "spikeDetection:\n  channelGroups:\n"
                 f"    - channels: [{chans}]\n      nSamples: {NSAMP}\n      peakSampleIndex: {PEAK}\n      nFeatures: {NCOMP}\n")
    return base, dict(res=res, clu=clu, truth=truth, spk=spk, fet=fet, fil=fil)


# ── readers for the checks ───────────────────────────────────────────────────

def read_res(path):
    return np.fromfile(path, np.int64)


def read_clu(path):
    return np.fromfile(path, np.int32)[1:]


def read_spk(path, nsamp, nch):
    return np.fromfile(path, np.int16).reshape(-1, nsamp * nch)


def read_fet(path):
    nd = int(np.fromfile(path, np.int32, 1)[0])
    return np.fromfile(path, np.int64, offset=4).reshape(-1, nd)

#!/usr/bin/env python3
"""
ao_mat2dat.py  –  AlphaOmega .mat (v7.3 / HDF5) → neurosuite .dat + session YAML
================================================================================
Usage:
    python3 ao_mat2dat.py  INPUT.mat  OUTPUT_BASENAME  [options]

Options:
    --channels  CHAN [CHAN ...]   Channels to export (1-based, e.g. 1-32).
                                 Default: all CRAW_* channels found in the file,
                                 sorted numerically.

    --topology  SPEC             Mixed probe topology.  SPEC is a comma-separated
                                 list of  FIRST-LAST:SIZE  tokens (1-based channel
                                 numbers, inclusive).  Each token defines a channel
                                 range and the spike-group size within that range.
                                 Group type is inferred from SIZE:
                                   SIZE == 4   → tetrode
                                   SIZE == 1   → single electrode
                                   anything else → linear/silicon shank
                                 Example (your recording):
                                   --topology "1-16:16,17-32:4"
                                   → one linear group [ch 0-15]
                                   → four tetrode groups [16-19],[20-23],[24-27],[28-31]

    --groups    N                Uniform group size for ALL channels (ignored when
                                 --topology is given).  Default: 8.

    --chunk-samps  N             Samples per read chunk (default: 10000000 = ~10s
                                 @ 30 kHz; tuned for 128 GB RAM).
    --out-dir   DIR              Directory for output files (default: same as INPUT)
    --no-yaml                    Skip YAML generation
    --dry-run                    Print metadata and exit without writing

Outputs:
    OUTPUT_BASENAME.dat          Interleaved int16 binary, channel-major within sample
    OUTPUT_BASENAME.yaml         neurosuite-3 session YAML

Channel ordering in .dat follows the order given by --channels (or sorted CRAW_*).
Channel indices in YAML are 0-based.

Notes:
    • AlphaOmega v7.3 .mat files are HDF5; scipy.io cannot read them — h5py is used.
    • int16 values are written as-is (no scaling); physical voltage = value × BitResolution µV.
    • For very large files (>10 GB) the script streams in chunks; peak RAM ≈ chunk_samps × nChan × 2 bytes.
"""

import argparse
import os
import sys
import time
from pathlib import Path

import h5py
import numpy as np
import yaml


# ── helpers ───────────────────────────────────────────────────────────────────

def _hdf5_scalar(val):
    """Unwrap HDF5 scalar dataset or numpy scalar to plain Python."""
    if hasattr(val, '__len__') and len(val) == 1:
        val = val[0]
    return float(val)


def discover_channels(f: h5py.File) -> list[str]:
    """Return sorted list of CRAW_* dataset names present in the file."""
    keys = [k for k in f.keys() if k.startswith('CRAW_') and not k.endswith(('_KHz', '_KHz_Orig',
             '_BitResolution', '_Gain', '_TimeBegin', '_TimeEnd'))]
    return sorted(keys, key=lambda s: int(s.split('_')[1]))


def read_channel_meta(f: h5py.File, cname: str) -> dict:
    meta = {}
    for suffix, key in [('_KHz', 'sampling_rate_khz'),
                        ('_BitResolution', 'bit_resolution_uv'),
                        ('_Gain', 'gain'),
                        ('_TimeBegin', 'time_begin_s'),
                        ('_TimeEnd', 'time_end_s')]:
        ds_name = cname + suffix
        if ds_name in f:
            meta[key] = _hdf5_scalar(f[ds_name][()])
    return meta


def _probe_type(group_size: int) -> str:
    """Infer probe type label from spike-group size."""
    if group_size == 4:
        return 'tetrode'
    if group_size == 1:
        return 'single'
    return 'linear'


def parse_topology(spec: str, n_channels: int) -> list[tuple[list[int], str]]:
    """
    Parse a topology spec string and return a list of (ch_indices_0based, probe_type).

    Spec format:  "FIRST-LAST:SIZE,FIRST-LAST:SIZE,..."
      FIRST, LAST  – 1-based channel numbers, inclusive
      SIZE         – spike-group size within that range

    Example:  "1-16:16,17-32:4"
      → ([ 0..15], 'linear')  ← one group of 16
      → ([16..19], 'tetrode') ← group 1 of 4
      → ([20..23], 'tetrode') ← group 2 of 4
      → ([24..27], 'tetrode') ← group 3 of 4
      → ([28..31], 'tetrode') ← group 4 of 4
    """
    groups = []
    for token in spec.split(','):
        token = token.strip()
        if not token:
            continue
        try:
            rng_part, size_str = token.split(':')
            size = int(size_str)
            if '-' in rng_part:
                first, last = (int(x) for x in rng_part.split('-'))
            else:
                first = last = int(rng_part)
        except ValueError:
            raise ValueError(
                f'Invalid topology token "{token}". '
                'Expected format FIRST-LAST:SIZE, e.g. "1-16:16" or "17-32:4".'
            )
        ch_range = list(range(first - 1, last))  # convert to 0-based
        if any(c >= n_channels or c < 0 for c in ch_range):
            raise ValueError(
                f'Topology token "{token}" references channels outside '
                f'the exported range (1–{n_channels}).'
            )
        ptype = _probe_type(size)
        for i in range(0, len(ch_range), size):
            groups.append((ch_range[i:i + size], ptype))
    return groups


def make_uniform_groups(n_channels: int, group_size: int) -> list[tuple[list[int], str]]:
    """Uniform split: every group_size channels → one group."""
    ptype = _probe_type(group_size)
    groups = []
    for i in range(0, n_channels, group_size):
        groups.append((list(range(i, min(i + group_size, n_channels))), ptype))
    return groups


def build_yaml(basename: str, ch_names: list[str], meta: dict,
               n_samples: int,
               groups: list[tuple[list[int], str]]) -> dict:
    sr_hz = int(round(meta['sampling_rate_khz'] * 1000))
    duration_s = meta['time_end_s'] - meta['time_begin_s']

    doc = {
        'session': {
            'name': basename,
            'nChannels': len(ch_names),
            'samplingRate': sr_hz,
            'nBits': 16,
            'voltageRange': None,           # not directly available; set manually
            'amplification': int(meta['gain']),
            'bitResolution_uV': round(meta['bit_resolution_uv'], 6),
            'nSamples': n_samples,
            'duration_s': round(duration_s, 4),
            'recordingBegin_s': round(meta['time_begin_s'], 4),
            'recordingEnd_s': round(meta['time_end_s'], 4),
        },
        'anatomicalGroups': [],
        'spikeGroups': [],
    }

    for g_idx, (ch_list, ptype) in enumerate(groups):
        doc['anatomicalGroups'].append({
            'group': g_idx,
            'channels': ch_list,
            'probeType': ptype,
        })
        doc['spikeGroups'].append({
            'group': g_idx,
            'channels': ch_list,
            'probeType': ptype,
            'nSamples': 32,
            'peakSampleIndex': 16,
        })

    return doc


# ── main ──────────────────────────────────────────────────────────────────────

def parse_args():
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('input_mat', help='AlphaOmega v7.3 .mat file')
    p.add_argument('output_basename', help='Stem for output files (no extension)')
    p.add_argument('--channels', nargs='+', type=int, metavar='N',
                   help='1-based channel numbers to export (default: all CRAW_*)')
    p.add_argument('--topology', default=None, metavar='SPEC',
                   help='Mixed probe topology, e.g. "1-16:16,17-32:4" '
                        '(overrides --groups)')
    p.add_argument('--groups', type=int, default=8, metavar='N',
                   help='Uniform channels per group, used when --topology is absent '
                        '(default: 8)')
    p.add_argument('--chunk-samps', type=int, default=10_000_000, metavar='N',
                   help='Samples per read chunk (default: 10000000 ≈ 610 MB for 32ch)')
    p.add_argument('--out-dir', default=None, metavar='DIR',
                   help='Output directory (default: directory of INPUT)')
    p.add_argument('--no-yaml', action='store_true', help='Skip YAML generation')
    p.add_argument('--dry-run', action='store_true',
                   help='Print metadata and exit without writing files')
    return p.parse_args()


def main():
    args = parse_args()

    mat_path = Path(args.input_mat).resolve()
    if not mat_path.exists():
        sys.exit(f'ERROR: {mat_path} not found')

    out_dir = Path(args.out_dir) if args.out_dir else mat_path.parent
    out_dir.mkdir(parents=True, exist_ok=True)
    basename = args.output_basename

    dat_path  = out_dir / f'{basename}.dat'
    yaml_path = out_dir / f'{basename}.yaml'

    print(f'Opening {mat_path} …')
    with h5py.File(mat_path, 'r') as f:

        # ── discover channels ──────────────────────────────────────────────
        all_craw = discover_channels(f)
        if not all_craw:
            sys.exit('ERROR: No CRAW_* datasets found in the file.')

        if args.channels:
            sel_names = [f'CRAW_{n:03d}' for n in sorted(args.channels)]
            missing = [n for n in sel_names if n not in all_craw]
            if missing:
                sys.exit(f'ERROR: Requested channels not in file: {missing}')
        else:
            sel_names = all_craw

        n_channels = len(sel_names)
        print(f'Channels selected: {n_channels}  ({sel_names[0]} … {sel_names[-1]})')

        # ── validate shapes & collect metadata ───────────────────────────
        n_samples_list = []
        for cname in sel_names:
            ds = f[cname]
            if ds.dtype != np.int16:
                print(f'  WARNING: {cname} dtype={ds.dtype}, expected int16 — will cast')
            shape = ds.shape
            # HDF5 may store as (N,) or (1,N) — normalise
            n = shape[-1] if len(shape) > 1 else shape[0]
            n_samples_list.append(n)

        if len(set(n_samples_list)) != 1:
            print('WARNING: channels have different lengths:',
                  {n: c for n, c in zip(sel_names, n_samples_list)})
            n_samples = min(n_samples_list)
            print(f'  → using min = {n_samples}')
        else:
            n_samples = n_samples_list[0]

        # Use first selected channel for global metadata
        meta = read_channel_meta(f, sel_names[0])
        sr_hz  = int(round(meta.get('sampling_rate_khz', 30) * 1000))
        dur_s  = n_samples / sr_hz

        print(f'Samples        : {n_samples:,}')
        print(f'Sampling rate  : {sr_hz:,} Hz')
        print(f'Duration       : {dur_s/60:.2f} min  ({dur_s:.1f} s)')
        print(f'BitResolution  : {meta.get("bit_resolution_uv", "?")} µV/count')
        print(f'Gain           : {meta.get("gain", "?")}')
        print(f'Output .dat    : {dat_path}')
        print(f'Expected size  : {n_samples * n_channels * 2 / 1e9:.2f} GB')

        # ── resolve electrode groups ──────────────────────────────────────
        if args.topology:
            groups = parse_topology(args.topology, n_channels)
        else:
            groups = make_uniform_groups(n_channels, args.groups)

        print(f'Spike groups   : {len(groups)}  '
              f'({", ".join(f"{len(g)}ch {t}" for g, t in groups)})')

        if args.dry_run:
            print('\n[dry-run] exiting before write.')
            return

        # ── write .dat ────────────────────────────────────────────────────
        print(f'\nWriting {dat_path.name} in chunks of {args.chunk_samps:,} samples …')
        chunk = args.chunk_samps
        t0 = time.time()
        bytes_written = 0

        # Open all datasets once
        datasets = [f[cname] for cname in sel_names]

        with open(dat_path, 'wb') as out:
            n_chunks = (n_samples + chunk - 1) // chunk
            for ci in range(n_chunks):
                s = ci * chunk
                e = min(s + chunk, n_samples)
                n = e - s

                # Read each channel slice and stack into (n_channels, n_samps)
                buf = np.empty((n_channels, n), dtype=np.int16)
                for ci2, ds in enumerate(datasets):
                    raw = ds[s:e] if ds.ndim == 1 else ds[0, s:e]
                    buf[ci2] = raw.astype(np.int16)

                # Interleave: transpose to (n_samps, n_channels), then flatten
                out.write(buf.T.astype('<i2').tobytes())
                bytes_written += n * n_channels * 2

                elapsed = time.time() - t0
                pct = (ci + 1) / n_chunks * 100
                rate = bytes_written / elapsed / 1e6 if elapsed > 0 else 0
                print(f'  chunk {ci+1:4d}/{n_chunks}  {pct:5.1f}%  '
                      f'{bytes_written/1e9:.2f} GB  {rate:.0f} MB/s', end='\r')

        print(f'\nDone. {bytes_written/1e9:.3f} GB written in {time.time()-t0:.1f} s')

        # ── write YAML ───────────────────────────────────────────────────
        if not args.no_yaml:
            doc = build_yaml(basename, sel_names, meta, n_samples, groups)

            print(f'\nBuilding YAML: {n_channels} channels → '
                  f'{len(groups)} spike groups')

            with open(yaml_path, 'w') as yf:
                yaml.dump(doc, yf, default_flow_style=False, sort_keys=False,
                          allow_unicode=True)
            print(f'YAML written : {yaml_path}')

    print('\nConversion complete.')


if __name__ == '__main__':
    main()

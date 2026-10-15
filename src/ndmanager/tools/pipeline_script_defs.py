#!/usr/bin/env python3
"""
pipeline_script_defs.py — regenerate src/ndmanager/src/pipelinescriptdefs.inc

The Pipeline designer (pipelinedesignerpage.cpp) lists every ndm_* plugin it
can place, with the parameters a newly placed node starts with.  Those
parameters come from templates/template.yaml — its parameter entries, in
template order, with the template's value and status — so the designer and
the template cannot disagree.  This file adds what the template does not
say: the palette label, tooltip and category of each plugin, in palette
order, and for a few plugins template parameters that are commented out
there but are worth showing in the designer.

Plugins that are aliases of others (ndm_extractspikes_sdiff /
_stderiv, ndm_pca_stderiv) stay placeable so existing pipelines load, but
carry no parameters of their own: they read ndm_detectspikes /
ndm_extractspikes / ndm_pca.

Usage:
    pipeline_script_defs.py            write the .inc
    pipeline_script_defs.py --check    exit 1 if the .inc is out of date

copyright (C) 2026 neurosuite-3 contributors  GPL-3.0-or-later
"""

from __future__ import annotations

import argparse
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
TEMPLATE = os.path.join(ROOT, "templates", "template.yaml")
OUT = os.path.join(ROOT, "src", "ndmanager", "src", "pipelinescriptdefs.inc")

# Plugins deliberately not in the palette, with the reason (checked by
# src/ndmanager-plugins/test/test_pipeline_designer_defs.py).
NOT_IN_PALETTE = {
    "ndm_xml2yaml": "converts a legacy XML parameter file; not a processing step",
    "ndm_recolorchannels": "edits display colours in the parameter file",
}

# (type, label, tooltip, category, startFlag, extra commented-out template
#  parameters to show — a name, or (name, value) when the commented value is
#  an example rather than the default — alias?)  — palette order within each
#  category.
META = [
    # ── Orchestrator (root) ──
    ("ndm_start", "ndm_start ★", "Pipeline orchestrator (root node)", "orchestrator", "",
     ["wideband", "events", "video", "concatenation", "spikes", "lfp", "clean"], False),
    # ── Conversion ──
    ("ndm_ncs2dat", "NCS → DAT", "Neuralynx .ncs to .dat", "conversion", "wideband", [], False),
    ("ndm_smr2dat", "SMR → DAT", "CED .smr to .dat", "conversion", "wideband", [], False),
    ("ndm_aom2dat", "AOM → DAT", "AlphaOmega .mat to .dat", "conversion", "wideband", [], False),
    ("ndm_plx2dat", "PLX → DAT", "Plexon .plx/.pl2 to .dat + .evt + header metadata",
     "conversion", "wideband", [], False),
    ("ndm_nev2evt", "NEV → EVT", "Neuralynx .nev events to .evt", "conversion", "events", [], False),
    ("ndm_smr2evt", "SMR → EVT", "CED .smr events to .evt", "conversion", "events", [], False),
    ("ndm_nvt2spots", "NVT → Spots", "Neuralynx .nvt tracking to spots", "conversion", "video", [], False),
    ("ndm_tsp2sts", "TSP → STS", "Amplipex .tsp/.meta spot timestamps", "conversion", "video", [], False),
    ("ndm_smi2sts", "SMI → STS", "Neuralynx .smi spot timestamps", "conversion", "video", [], False),
    ("ndm_transcodevideo", "Transcode Video", "Convert a video to another codec", "conversion", "video", [], False),
    ("ndm_extractleds", "Extract LEDs", "Bright-spot (LED) tracking from video", "conversion", "video", [], False),
    # ── Preparation ──
    ("ndm_extractchannels", "Extract Channels", "Strip channels from .dat", "preparation", "wideband", [], False),
    ("ndm_reorderchannels", "Reorder Channels", "Reorder the channels of .dat", "preparation", "wideband", [], False),
    ("ndm_mergedat", "Merge DATs", "Merge multiple .dat files", "preparation", "wideband", [], False),
    ("ndm_resample", "Resample", "Resample to common rate", "preparation", "wideband", [], False),
    ("ndm_concatenate", "Concatenate", "Concatenate sessions", "preparation", "concatenation", [], False),
    ("ndm_extractemg", "Extract EMG", "FastICA EMG removal → -emgclean.dat + EMG events",
     "preparation", "", [], False),
    ("ndm_hipass", "High-pass Filter", ".dat → .fil (Butterworth or median)", "preparation", "spikes",
     ["windowHalfLength", "inputExtension"], False),
    # Alternative to ndm_hipass, not an addition to it: both write .fil, and
    # the second to run refuses because the output already exists.
    ("ndm_bandpass", "Band-pass Filter", "Median + FIR → .fil (instead of High-pass)", "preparation", "spikes",
     ["inputExtension"], False),
    ("ndm_denoiseuniform", "Denoise Uniform", "Remove uniform-noise events", "preparation", "spikes", [], False),
    ("ndm_spikecleaner", "Spike Cleaner", "Remove dropout/flat waveforms", "preparation", "spikes", [], False),
    # ── Grouping ──
    ("ndm_setupgroups", "Setup Groups", "Build groups from probe file", "grouping", "spikes", [], False),
    ("ndm_spikegrouper", "Spike Grouper", "Auto-discover channel groups", "grouping", "spikes", [], False),
    # ── Detection ──
    ("ndm_detectspikes", "Detect Spikes", ".fil → .res (threshold detection)", "detection", "spikes",
     ["inputExtension"], False),
    # Extraction is a second pass: it reads the .res written by
    # ndm_detectspikes, so it carries no threshold/window parameters.
    ("ndm_extractspikes", "Extract Spikes", ".res + .fil → .spk (after Detect Spikes)", "detection", "spikes",
     ["inputExtension"], False),
    ("ndm_extractspikes_sdiff", "Extract Spikes (SDiff)",
     "Detect + Extract with method sdiff (alias; set parameters on Detect / Extract Spikes)",
     "detection", "spikes", [], True),
    ("ndm_extractspikes_stderiv", "Extract Spikes (StDeriv)",
     "Detect + Extract with method stderiv (alias; set parameters on Detect / Extract Spikes)",
     "detection", "spikes", [], True),
    # ── Alignment ──
    ("ndm_alignspikes", "Align Spikes ★", "Peak-align snippets before PCA", "alignment", "spikes", [], False),
    ("ndm_alignspikes_pca", "Align Spikes (PCA)", "Per-cluster realignment after clustering",
     "alignment", "spikes", [], False),
    # ── Features ──
    ("ndm_pca", "PCA", ".spk → .fet", "features", "spikes", [], False),
    ("ndm_refeaturize", "Re-featurize", ".spk + .pca → .fet (no PCA refit)", "features", "spikes", [], False),
    ("ndm_pca_stderiv", "PCA (StDeriv)", "PCA with method stderiv (alias; set parameters on PCA)",
     "features", "spikes", [], True),
    # ── Sorting ──
    ("ndm_klustakwik", "KiloKlustaKwik", ".fet → .clu (CEM clustering)", "sorting", "spikes", [], False),
    ("ndm_autoklusta", "Auto-curate", "Learned merge/split curation of .clu", "sorting", "spikes", [], False),
    ("ndm_fiber-kit", "fiber-kit", "External fiber-kit sorter (ndm_fiber-kit must be on PATH)",
     "sorting", "spikes", [], False),
    # ── Post-sort ──
    ("ndm_reextractspikes", "Reextract Spikes", "2nd-pass detection + shadow clustering", "postprocess", "spikes", [], False),
    ("ndm_subcluster_unmatched", "Subcluster Unmatched", "Re-cluster the unmatched bin", "postprocess", "spikes", [], False),
    ("ndm_stripdat", "Strip DAT", "Remove sorted spikes from .dat", "postprocess", "spikes", [], False),
    ("ndm_redetectspikes", "Redetect Spikes", "2nd-round detection on the stripped .dat",
     "postprocess", "spikes", [], False),
    # ── Analysis ──
    ("ndm_estimatedrift", "Estimate Drift", "Compute probe drift from sorted units", "analysis", "", [], False),
    ("ndm_driftcorrect", "Drift Correct", ".spk → .spkC (waveforms at the reference position)",
     "analysis", "", [], False),
    ("ndm_applydrift", "Apply Drift", "Generate drift-adaptive chunk files", "analysis", "", [], False),
    ("ndm_drifttracker", "Drift Tracker", "Merge clusters along drift trajectories → .clu.N.drift",
     "analysis", "", [], False),
    ("ndm_decomposecollisions", "Decompose Collisions", "Separate collision waveforms", "analysis", "", [], False),
    ("ndm_localise", "Localise", "Spike source positions relative to the shank", "analysis", "", [], False),
    # ── LFP ──
    ("ndm_lfp", "LFP", ".dat → .lfp (downsampled)", "lfp", "lfp", ["inputExtension"], False),
    # Channel-subset excerpt of an existing binary.  Filed under lfp because
    # that is the usual source, but inputExtension points it at any of them
    # and the output extension follows the input.
    ("ndm_slicebinary", "Slice Binary", ".lfp/.dat → .<ext>.slice.<tag> (channel subset)", "lfp", "lfp",
     [("timeRange", ""), "inputExtension"], False),   # template value is an example
    # ── Output ──
    ("ndm_clean", "Clean", "Remove intermediate files", "output", "",
     ["hipass", "wideband", "xml", "pos", "spots"], False),
]


def template_entries() -> dict[str, list[tuple[str, str, str, bool]]]:
    """{program: [(name, value, status, commented)]} in template order, values
    as written (quotes stripped)."""
    out: dict = {}
    cur = None
    with open(TEMPLATE, encoding="utf-8") as fh:
        lines = fh.read().split("\n")
    i = 0
    while i < len(lines):
        ln = lines[i]
        m = re.match(r"^  - name: (\S+)\s*$", ln)
        if m:
            cur = m.group(1)
            out.setdefault(cur, [])
            i += 1
            continue
        if ln.startswith("    help:"):
            cur = None
        m = re.match(r"^      (#\s*)?- name: (\S+)\s*$", ln)
        if cur and m:
            commented = bool(m.group(1))
            name, value, status = m.group(2), "", "Optional"
            j = i + 1
            while j < len(lines):
                f = re.match(r"^      (?:#\s*)?  (value|status):\s*(.*?)\s*$", lines[j])
                if not f:
                    break
                if f.group(1) == "value":
                    value = f.group(2)
                else:
                    status = f.group(2)
                j += 1
            if len(value) >= 2 and value[0] == value[-1] and value[0] in "'\"":
                value = value[1:-1]
            out[cur].append((name, value, status, commented))
            i = j
            continue
        i += 1
    return out


def _c(s: str) -> str:
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def render() -> str:
    tpl = template_entries()
    rows = []
    rows.append("// Generated by src/ndmanager/tools/pipeline_script_defs.py from")
    rows.append("// templates/template.yaml — edit those, then regenerate.  Included by")
    rows.append("// pipelinedesignerpage.cpp inside the ndmScriptDefs() initializer.")
    cat = None
    for typ, label, brief, category, flag, extra, alias in META:
        if typ not in tpl and not alias:
            raise SystemExit(f"{typ}: no program block in templates/template.yaml")
        if category != cat:
            rows.append(f"// ── {category} ──")
            cat = category
        params = []
        shown = {(e if isinstance(e, str) else e[0]): (None if isinstance(e, str) else e[1])
                 for e in extra}
        if not alias:
            for name, value, status, commented in tpl[typ]:
                if commented:
                    if name not in shown:
                        continue
                    if shown[name] is not None:
                        value = shown[name]
                params.append(f"{{ {_c(name)}, {_c(value)}, {_c(status)} }}")
            missing = set(shown) - {n for n, *_ in tpl[typ]}
            if missing:
                raise SystemExit(f"{typ}: {sorted(missing)} not in the template")
        head = f"{{ {_c(typ)}, {_c(label)}, {_c(brief)}, {_c(category)}, {_c(flag)},"
        if not params:
            rows.append(head + " {} },")
        else:
            rows.append(head)
            for k, p in enumerate(params):
                pre = "  {" if k == 0 else "   "
                post = "} }," if k == len(params) - 1 else ","
                rows.append(f"  {pre}{p}{post}")
    return "\n".join(rows) + "\n"


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--check", action="store_true")
    a = ap.parse_args(argv)
    text = render()
    if a.check:
        try:
            same = open(OUT, encoding="utf-8").read() == text
        except OSError:
            same = False
        if not same:
            print(f"out of date: {OUT} (run {os.path.relpath(__file__, ROOT)})")
        return 0 if same else 1
    with open(OUT, "w", encoding="utf-8") as fh:
        fh.write(text)
    print(f"wrote {OUT}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

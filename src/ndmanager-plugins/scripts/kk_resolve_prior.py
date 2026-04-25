#!/usr/bin/env python3
"""
kk_resolve_prior.py — Locate the right .prior.yaml for a session+group.

Computes a probe_id from the session YAML's channel-group structure, then
searches a list of directories for a file named  <probe_id>.<group>.prior.yaml
that *also* declares a matching probe_id internally.  Prints the resolved
absolute path on stdout, or exits non-zero if no match is found.

Search order
────────────
  1.  $NDM_PRIOR_DIR             (user/site override)
  2.  ~/.ndm/priors              (per-user store)
  3.  /etc/ndm/priors            (system-wide store)
  4.  Same directory as the session yaml (session-local override)

Usage
─────
  PRIOR=$(kk_resolve_prior.py --session /path/session.yaml --group 7)
  if [ -n "$PRIOR" ]; then
      echo "Using prior $PRIOR"
  fi

Exit codes
──────────
  0   match found, path printed on stdout
  1   no matching prior found
  2   bad arguments / unreadable session yaml
  3   match found by filename but probe_id inside disagrees
"""

import argparse
import hashlib
import json
import os
import sys
from pathlib import Path

import yaml


# Re-implements probe_signature_from_session / probe_id_from_signature /
# template-matching from kk_build_prior.py — kept duplicated to avoid an
# import dependency.  Both copies MUST stay byte-identical; if you change
# the canonical signature shape, change both places.

def probe_signature_from_session(session_dict):
    acq = session_dict.get("acquisitionSystem", {}) or {}
    spk = session_dict.get("spikeDetection", {}) or {}

    n_channels = int(acq.get("nChannels", 0))

    groups_raw = spk.get("channelGroups", []) or []
    groups = []
    for g in groups_raw:
        if not isinstance(g, dict):
            continue
        ch = g.get("channels", []) or []
        ch_sorted = sorted(int(c) for c in ch)
        groups.append({
            "channels":          ch_sorted,
            "n_samples":         int(g.get("nSamples", 0)),
            "peak_sample_index": int(g.get("peakSampleIndex", 0)),
            "n_features":        int(g.get("nFeatures", 0)),
        })

    return {
        "n_channels":     n_channels,
        "channel_groups": groups,
    }


def probe_id_from_signature(sig):
    canonical = json.dumps(sig, sort_keys=True, separators=(",", ":"))
    return hashlib.sha256(canonical.encode("utf-8")).hexdigest()[:16]


def _default_template_dirs():
    env = os.environ.get("NDM_TEMPLATE_DIR")
    if env:
        for entry in env.split(":"):
            entry = entry.strip()
            if entry:
                yield Path(entry)
    yield Path("/usr/share/ndmanager/templates")
    yield Path("/usr/local/share/ndmanager/templates")
    here = Path(__file__).resolve().parent
    yield here / ".." / ".." / "ndmanager" / "src"
    yield here.parent / "ndmanager" / "src"


def find_matching_template(target_sig, template_dirs=None):
    if template_dirs is None:
        template_dirs = _default_template_dirs()
    seen = set()
    for d in template_dirs:
        if not d.is_dir():
            continue
        d_resolved = d.resolve()
        if d_resolved in seen:
            continue
        seen.add(d_resolved)
        for tpl in sorted(d_resolved.glob("Template-*.yaml")):
            try:
                with open(tpl) as f:
                    tpl_dict = yaml.safe_load(f) or {}
            except Exception:
                continue
            tpl_sig = probe_signature_from_session(tpl_dict)
            if tpl_sig == target_sig:
                stem = tpl.stem
                if stem.startswith("Template-"):
                    stem = stem[len("Template-"):]
                return stem
    return None


def probe_id_for_session(session_dict, template_dirs=None):
    sig      = probe_signature_from_session(session_dict)
    sig_hash = probe_id_from_signature(sig)
    friendly = find_matching_template(sig, template_dirs)
    return (friendly or sig_hash), sig, sig_hash


def search_dirs(session_path):
    """Yield candidate directories in priority order."""
    env = os.environ.get("NDM_PRIOR_DIR")
    if env:
        for entry in env.split(":"):
            entry = entry.strip()
            if entry:
                yield Path(entry)
    yield Path.home() / ".ndm" / "priors"
    yield Path("/etc/ndm/priors")
    yield Path(session_path).resolve().parent


def resolve(session_path, group, verbose=False, template_dirs=None):
    """Return an absolute Path to the resolved prior, or None if no match."""
    try:
        with open(session_path) as f:
            session = yaml.safe_load(f)
    except Exception as e:
        print(f"ERROR: failed to read session yaml: {e}", file=sys.stderr)
        sys.exit(2)

    probe_id, sig, sig_hash = probe_id_for_session(session, template_dirs)
    fname = f"{probe_id}.{group}.prior.yaml"

    if verbose:
        print(f"# probe_id   = {probe_id}", file=sys.stderr)
        print(f"# sig_hash   = {sig_hash}", file=sys.stderr)
        print(f"# looking for {fname}", file=sys.stderr)

    for d in search_dirs(session_path):
        cand = d / fname
        if not cand.is_file():
            if verbose:
                print(f"#   miss: {cand}", file=sys.stderr)
            continue
        # Validate against the file's contents.  The authoritative cross-
        # check is probe_signature_hash — friendly probe_id strings can be
        # renamed or hand-edited, but the hash is computed from the actual
        # signature.  When a hash is present in the prior, it MUST match
        # the session's signature hash.
        try:
            with open(cand) as f:
                prior = yaml.safe_load(f) or {}
        except Exception as e:
            if verbose:
                print(f"#   unreadable: {cand} ({e})", file=sys.stderr)
            continue
        inner_hash = prior.get("probe_signature_hash")
        inner_id   = prior.get("probe_id")
        inner_grp  = prior.get("electrode_group")
        if inner_hash is not None and inner_hash != sig_hash:
            print(f"ERROR: filename matches but probe_signature_hash inside "
                  f"{cand} ({inner_hash!r}) != session hash ({sig_hash!r})",
                  file=sys.stderr)
            sys.exit(3)
        # If no hash present (legacy file), fall back to friendly-id check.
        if inner_hash is None and inner_id != probe_id:
            print(f"ERROR: legacy prior {cand} has probe_id {inner_id!r}, "
                  f"session resolves to {probe_id!r}", file=sys.stderr)
            sys.exit(3)
        if inner_grp is not None and int(inner_grp) != group:
            print(f"ERROR: filename matches but electrode_group inside "
                  f"{cand} ({inner_grp}) != requested group ({group})",
                  file=sys.stderr)
            sys.exit(3)
        return cand.resolve()

    return None


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--session", required=True,
                    help="Path to session .yaml")
    ap.add_argument("--group", type=int, required=True,
                    help="Electrode group (1-based)")
    ap.add_argument("--templates-dir", action="append", default=None,
                    help="Override search path for Template-*.yaml. "
                         "May be specified multiple times.")
    ap.add_argument("--print-id", action="store_true",
                    help="Print probe_id to stderr alongside the resolved path")
    ap.add_argument("-v", "--verbose", action="store_true",
                    help="Trace search to stderr")
    args = ap.parse_args()

    template_dirs = ([Path(p) for p in args.templates_dir]
                     if args.templates_dir else None)
    path = resolve(args.session, args.group,
                   verbose=args.verbose or args.print_id,
                   template_dirs=template_dirs)
    if path is None:
        sys.exit(1)
    print(path)
    sys.exit(0)


if __name__ == "__main__":
    main()

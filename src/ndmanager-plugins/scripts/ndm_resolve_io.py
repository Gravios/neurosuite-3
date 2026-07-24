"""Chain-of-custody file relationships for neurosuite-3 — Python mirror.

This is the Python counterpart of ``neurosuite::custody`` (custody.hpp, the C++
single source of truth) and the bash ``ndm_*`` helpers.  It owns the POLICY:
which artifact types are method-specific, which are shared across methods, and
which are session-wide -- so no ``process_*.py`` / viewer / ``neurosuite_compare``
caller hand-rolls ``.spk``/``.spkD`` checks again.

Naming model::

    <base>.<type>.<method>.<group>     per-group, method-tagged
    <base>.<type>.<group>              per-group, untagged/legacy
    <base>.<type>                       session-wide (fil/dat/...)

Classes::

    MethodSpecific  clu fet pca col model klg   strict, no fallback
    Shared          res spk                     method -> standard -> untagged
    SessionWide     fil dat xml yaml par ...     <base>.<type>

Kept in lock-step with custody.hpp via the shared conformance vectors
(custody_vectors.tsv); see test_custody_conformance.py.
"""

import os
from collections import namedtuple

__all__ = [
    "classify", "is_per_group_type", "is_session_wide_type", "is_known_type",
    "method_path", "untagged_path", "session_path", "file_exists",
    "method_of", "parse_anchor", "resolve", "resolve_any", "resolved_is_stderiv",
    "parse_method_token", "is_stderiv_method",
    "stale_after_realign",
    # backward-compatible Stage-1 names
    "resolve_input", "method_from_path",
]

DEFAULT_METHOD = "standard"

_PER_GROUP = {"res", "spk", "clu", "clc", "fet", "pca", "col", "model", "klg"}
_SESSION_WIDE = {"fil", "dat", "xml", "yaml", "nrs", "par", "eeg", "lfp"}


def is_per_group_type(type_):
    return type_ in _PER_GROUP


def is_session_wide_type(type_):
    return type_ in _SESSION_WIDE


def is_known_type(token):
    return token in _PER_GROUP or token in _SESSION_WIDE


def classify(type_):
    """Return 'MethodSpecific' | 'Shared' | 'SessionWide'."""
    if is_session_wide_type(type_):
        return "SessionWide"
    if type_ in ("res", "spk"):
        return "Shared"
    return "MethodSpecific"


# ── path composition ─────────────────────────────────────────────────────────

def method_path(base, type_, method, group):
    return "{}.{}.{}.{}".format(base, type_, method, group)


def untagged_path(base, type_, group):
    return "{}.{}.{}".format(base, type_, group)


def session_path(base, type_):
    return "{}.{}".format(base, type_)


def file_exists(path):
    return os.path.isfile(path)


# ── method parsing ───────────────────────────────────────────────────────────

def method_of(path):
    """Method token of ``<base>.<type>.<method>.<group>``; '' if untagged.

    Robust against a dotted base: the slot before the group is only the method
    if it is NOT a known type token.
    """
    p = os.path.basename(path).split(".")
    if len(p) < 3:
        return ""
    if not p[-1].isdigit():
        return ""
    cand = p[-2]
    if is_known_type(cand):
        return ""
    return cand


Anchor = namedtuple("Anchor", "base type method group suffix ok")


def parse_anchor(path):
    """Parse ``<base>.<type>.<method>.<grp>[.<suffix>]`` (or untagged)."""
    p = os.path.basename(path).split(".")
    type_idx = -1
    for i in range(len(p) - 2, 0, -1):
        if is_known_type(p[i]):
            type_idx = i
            break
    if type_idx < 0:
        return Anchor("", "", "", -1, "", False)

    type_ = p[type_idx]
    base = ".".join(p[:type_idx])
    idx = type_idx + 1
    method = ""
    if idx < len(p) and not p[idx].isdigit():
        method = p[idx]
        idx += 1
    if idx >= len(p) or not p[idx].isdigit():
        return Anchor(base, type_, method, -1, "", False)
    group = int(p[idx])
    idx += 1
    suffix = ".".join(p[idx:])
    return Anchor(base, type_, method, group, suffix, True)


# ── resolution ───────────────────────────────────────────────────────────────

Resolved = namedtuple("Resolved", "path method found")


def resolve(base, type_, group, method):
    """Resolve an input by class (see module docstring)."""
    k = classify(type_)
    if k == "SessionWide":
        path = session_path(base, type_)
        return Resolved(path, "", file_exists(path))
    if k == "MethodSpecific":
        path = method_path(base, type_, method, group)
        return Resolved(path, method, file_exists(path))
    # Shared: method -> standard -> untagged.
    cands = [method_path(base, type_, method, group)]
    if method != DEFAULT_METHOD:
        cands.append(method_path(base, type_, DEFAULT_METHOD, group))
    cands.append(untagged_path(base, type_, group))
    for c in cands:
        if file_exists(c):
            return Resolved(c, method_of(c), True)
    return Resolved(cands[0], method, False)


MethodSpec = namedtuple("MethodSpec", "family kind order")


def parse_method_token(method):
    """Decompose a method token <family>[_<kind><order>], e.g. "stderiv_C4".

    kind is "S" (plain methodOrder derivative) or "C" (the session's custom
    sdiffPairs pattern); order is the spatial-derivative order actually applied.
    A token that does not match the suffix grammar is opaque: the whole string
    becomes the family, so an unknown token never masquerades as a known one.
    """
    cut = method.rfind("_")
    if cut != -1:
        suffix = method[cut + 1:]
        if len(suffix) >= 2 and suffix[0] in ("S", "C") and suffix[1:].isdigit():
            return MethodSpec(method[:cut], suffix[0], int(suffix[1:]))
    return MethodSpec(method, None, None)


def is_stderiv_method(method):
    """True for the stderiv family: bare "stderiv" and any stderiv_<kind><order>.

    Not a prefix test - "stderivfoo" is a different family.
    """
    return parse_method_token(method).family == "stderiv"


def resolve_any(base, type_, group, preferred=""):
    """Resolve a SHARED artifact across EVERY method.

    Spike times (.res) and the raw .spk are one physical copy whatever produced
    them, so the writing token need not match the caller's method.  resolve() only
    walks method -> standard -> untagged; this also finds any other method-tagged
    copy, including suffixed tokens no fixed list can enumerate.  Mirrors
    ndm_resolve_any (bash) and custody::resolveAny (C++).
    """
    order = ([preferred] if preferred else []) + \
            [m for m in ("standard", "stderiv", "sdiff") if m != preferred]
    for m in order:
        cand = method_path(base, type_, m, group)
        if file_exists(cand):
            return Resolved(cand, m, True)
    directory = os.path.dirname(base) or "."
    stem, suffix = os.path.basename(base) + f".{type_}.", f".{group}"
    try:
        names = sorted(os.listdir(directory))
    except OSError:
        names = []
    for name in names:
        if not (name.startswith(stem) and name.endswith(suffix)):
            continue
        m = name[len(stem):len(name) - len(suffix)]
        if not m or "." in m:
            continue
        return Resolved(os.path.join(directory, name), m, True)
    untagged = untagged_path(base, type_, group)
    if file_exists(untagged):
        return Resolved(untagged, "", True)
    return Resolved(method_path(base, type_, preferred or DEFAULT_METHOD, group), preferred, False)


def resolved_is_stderiv(resolved):
    """True iff the file actually resolved is in the stderiv domain."""
    return is_stderiv_method(resolved.method)


def stale_after_realign():
    """Per-group artifacts invalidated when waveforms are realigned."""
    return ["fet", "pca"]


# ── backward-compatible Stage-1 names ────────────────────────────────────────

def resolve_input(base, type_, group, method):
    """Stage-1 shim: returns ``(path, found)`` using the class-aware resolver."""
    r = resolve(base, type_, group, method)
    return r.path, r.found


def method_from_path(path):
    """Stage-1 alias for :func:`method_of`."""
    return method_of(path)

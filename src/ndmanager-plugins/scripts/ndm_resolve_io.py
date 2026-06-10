"""Chain-of-custody method-pinned path resolution for neurosuite-3.

Every per-group artifact is named ``<base>.<type>.<method>.<group>`` for
``type`` in {res, spk, clu, fet, pca} and ``method`` in
{standard, stderiv, sdiff, ...} (the method set is open). The method is always
known -- read off the ``.clu`` anchor or supplied by the caller -- so
resolution composes the path directly: it exists or it is an error. There is
no untagged, canonical, or legacy-glued form.

Mirrors ``neurofileio::{methodPath, resolveInputForMethod, methodFromPath}``
(C++) and ``ndm_method_path`` / ``ndm_resolve_method`` / ``ndm_method_from_path``
(bash). This is the single Python home for the rule; the ``process_*.py``
plugins and the external ``neurosuite_compare`` package should import it rather
than hand-rolling ``.spk``/``.spkD`` checks.
"""

import os

__all__ = ["method_path", "resolve_input", "method_from_path"]


def method_path(base, type_, method, group):
    """Compose ``<base>.<type>.<method>.<group>``."""
    return "{}.{}.{}.{}".format(base, type_, method, group)


def resolve_input(base, type_, group, method):
    """Resolve a method-pinned input.

    Returns ``(path, found)``. ``path`` is always the composed method path, so
    callers can emit a precise missing-input error when ``found`` is False.
    """
    path = method_path(base, type_, method, group)
    return path, os.path.isfile(path)


def method_from_path(path):
    """Extract the method token from ``<base>.<type>.<method>.<group>``.

    The base may itself contain dots, so the name is parsed from the right: the
    last field must be an all-digit group and the method is the second-to-last
    field. Returns ``""`` if the name is not a tagged per-group file.
    """
    name = os.path.basename(path)
    parts = name.split(".")
    if len(parts) < 4:
        return ""
    if not parts[-1].isdigit():
        return ""
    return parts[-2]

# `kk_resolve_prior.py` — locate the matching prior for a session and group

## Synopsis

```sh
kk_resolve_prior.py --session <session.yaml> --group <N> \
    [--templates-dir <dir>]... [--print-id] [-v]
```

**Input:** the session YAML; candidate `<probe_id>.<N>.prior.yaml` files

**Output:** absolute path of the matching prior on stdout (nothing when
there is no match); trace on stderr

## Description

Recomputes the probe identity of the session exactly as
[`kk_build_prior.py`](kk_build_prior.md#probe-identity) does (friendly
template name when an installed `Template-*.yaml` has the same
signature, else the 16-hex-character signature hash) and looks for
`<probe_id>.<N>.prior.yaml`. It reads files only and never writes.

[`ndm_subcluster_unmatched`](ndm_subcluster_unmatched.md) calls it for
each group, discarding stderr and treating any non-zero exit as "no
prior".

## Options

| Option | Meaning |
|---|---|
| `--session` | Path to the session YAML file (required). |
| `--group` | 1-based electrode group (required). |
| `--templates-dir` | Directory searched for `Template-*.yaml` (repeatable); default search path as for `kk_build_prior.py`. |
| `--print-id` | Print the probe id and hash to stderr (same trace as `-v`). |
| `-v`, `--verbose` | Trace the search to stderr. |

## Search order

1. each directory in `$NDM_PRIOR_DIR` (colon-separated)
2. `~/.ndm/priors`
3. `/etc/ndm/priors`
4. the directory containing the session YAML

The first existing `<probe_id>.<N>.prior.yaml` is validated and
returned; unreadable files are skipped.

## Validation

The candidate's contents must agree with the request, otherwise the
resolver stops with exit status 3 instead of trying later directories:

- `probe_signature_hash`, when present, must equal the session's hash;
- a file without a hash (older priors) must have a `probe_id` equal to
  the session's;
- `electrode_group`, when present, must equal `--group`.

## Exit status

| Code | Meaning |
|---|---|
| 0 | Match found; path on stdout |
| 1 | No matching prior found |
| 2 | Bad arguments or unreadable session YAML |
| 3 | File name matched but its hash, `probe_id` or `electrode_group` disagrees |

## See also

- [`kk_build_prior.py`](kk_build_prior.md) — the training counterpart
- [`ndm_subcluster_unmatched`](ndm_subcluster_unmatched.md) — the caller
- [Empirical priors workflow](../../workflows/empirical-priors.md) —
  bootstrap, refresh, troubleshooting

---

*Part of the [ndmanager-plugins](../README.md) reference.*

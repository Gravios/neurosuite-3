# `kk_resolve_prior.py` — locate the matching prior for a session+group

Given a session YAML and electrode group, recomputes the probe
identity and returns the path to the matching prior. Best-effort and
side-effect-free; never silently uses a mismatched file.

## Usage

```sh
kk_resolve_prior.py --session <session.yaml> --group <N> [-v]
```

The resolved path is printed on stdout when found. Any verbose trace
goes to stderr.

## Search order

1. `$NDM_PRIOR_DIR` (colon-separated, like `$PATH`)
2. `~/.ndm/priors`
3. `/etc/ndm/priors`
4. The session YAML's directory

The first file matching `<probe_id>.<group>.prior.yaml` whose
`probe_signature_hash` agrees with the session is returned.

## Validation

The resolver re-reads each candidate and validates
`probe_signature_hash` against the session signature before returning a
path. A filename collision or hand-edited mismatch is fail-loud — the
resolver exits non-zero rather than silently using the wrong prior.

## Exit codes

| Code | Meaning |
|---|---|
| 0 | Resolved (path on stdout) |
| 1 | No matching prior found |
| 2 | Bad arguments / unreadable session yaml |
| 3 | Filename matched but `probe_signature_hash` inside disagrees |

## See also

- [`kk_build_prior.py`](kk_build_prior.md) — the training counterpart.
- [Empirical priors workflow](../../workflows/empirical-priors.md) —
  bootstrap, refresh, and troubleshooting.
- [`ndm_subcluster_unmatched`](ndm_subcluster_unmatched.md) — calls
  this resolver internally; falls through to script defaults on miss.

---

*Part of the [ndmanager-plugins](../README.md) reference.*

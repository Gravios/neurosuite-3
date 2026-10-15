# `ndm_clean` — remove intermediate files after preprocessing

## Synopsis

```bash
ndm_clean [session.yaml]
```

**Input:** the files in the current directory

**Output:** none; deletes files

## Description

Deletes the per-session intermediates that remain in a directory after
[`ndm_start`](ndm_start.md) has merged and concatenated the sessions of a day.
It runs in the current directory and always exits 0 once the parameters are
read. In legacy `ndm_start` mode it runs last when `clean: true`.

Each category is selected by a parameter; `SESSION` is the session (directory)
name:

| Parameter | Deletes | Keeps |
|---|---|---|
| `wideband` | every file ending in `dat` whose name does not contain `SESSION.dat` | `SESSION.dat` |
| `xml` | every `*.xml`, `*.yaml`, `*.yml` | `SESSION.xml`, `SESSION.yaml`, `SESSION.yml` |
| `hipass` | `SESSION.fil` | — |
| `spots` | every `*spots` file | — |
| `pos` | every file ending in `pos` whose name does not contain `SESSION.pos` | `SESSION.pos` |

The patterns are broad. With the defaults, `wideband` also deletes
`SESSION-spkclean.dat` ([`ndm_stripdat`](ndm_stripdat.md)) and
`SESSION-emgclean.dat` (`ndm_extractemg`), and `xml` also deletes YAML outputs
such as `SESSION.plexon.yaml` ([`ndm_plx2dat`](ndm_plx2dat.md)). Set those
flags to `false` when you need such files, and run `ndm_clean` only after the
`.fil` is no longer needed (it is the input of spike detection and
re-extraction).

`xpathReader` must be installed, even for YAML sessions.

## Parameters

None of these is in the template by default; add them to the `ndm_clean`
block to change a default (see [parameters](../parameters.md)).

| Name | Default | Meaning |
|---|---|---|
| `wideband` | `true` | Delete the single-session wide-band `.dat` files |
| `xml` | `true` | Delete the single-session parameter files (`.xml`, `.yaml`, `.yml`) |
| `hipass` | `true` | Delete `SESSION.fil` |
| `spots` | `false` | Delete `.spots` files |
| `pos` | `false` | Delete single-session `.pos` files |

```yaml
- name: ndm_clean
  parameters:
    - name: hipass
      value: 'false'
      status: Optional
```

## See also

- [`ndm_start`](ndm_start.md) — the `clean` flag
- [Pipeline overview](../pipeline.md)

---

*Part of the [ndmanager-plugins](../README.md) reference.*

# `ndm_spikegrouper` — automatic spike group refinement (`.fil` → updated YAML)

Discovers optimal spike detection channel groups from the high-pass
filtered data. Must be run after `ndm_hipass` and before
`ndm_extractspikes`. The `anatomicalDescription` section is left untouched.

```yaml
- name: ndm_spikegrouper
  parameters:
  - {name: maxMergedSize, value: 12, status: Mandatory}
  - {name: minChannels,   value: 4,  status: Mandatory}
```


---

*Part of the [ndmanager-plugins](../README.md) reference.
See [pipeline overview](../pipeline.md) for how this fits the full workflow.*

# Channel and data manipulation

Short utilities for channel and data manipulation.  Each is invoked with the session YAML or basename as its single argument.

---

## `ndm_resample`

Resamples per-subsession `.dat` files from their original hardware rates to
the common target rate. The underlying `process_resample` binary is
OpenMP-parallelised across channels.

```yaml
- name: ndm_resample
  parameters:
  - {name: suffixes,      value: "nlx smr"}
  - {name: samplingRates, value: "32000 30000"}
  - {name: nChannels,     value: "64 32"}
```


---

## `ndm_mergedat`

Interleaves channels from multiple `.dat` files into a single multiplexed file.


---

## `ndm_extractchannels`

Removes unwanted channels, writing only the listed channels in listed order.


---

## `ndm_reorderchannels`

Permutes channel order to match the physical probe layout. Must be run before
`ndm_setupgroups` if the ADC channel numbering does not already match probe
order.


---

## `ndm_recolorchannels`

Legacy cosmetic tool — updates channel colour attributes in the YAML. Has no
effect on signal processing; provided for compatibility with older
NeuroScope-driven workflows.


---

## `ndm_concatenate`

Concatenates multi-session `.dat`, `.pos`, and `.evt` files. Creates a
`.cat.evt` file with subsession boundary timestamps.



---

*Part of the [ndmanager-plugins](../README.md) reference.
See [pipeline overview](../pipeline.md) for how this fits the full workflow.*

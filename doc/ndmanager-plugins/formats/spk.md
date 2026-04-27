# `.spk.N` / `.spkD.N` — spike waveforms

Binary. No header. `nSpikes × nSamples × nChannels` int16 samples,
**sample-major** layout (all channels for sample 0 first, then all
for sample 1, etc.). File size = `nSpikes × nSamples × nChannels × 2`
bytes.

- `.spk.N`: raw waveform snippets from `.fil` (`ndm_extractspikes`,
  `ndm_extractspikes_sdiff`, `ndm_reextractspikes`).
- `.spkD.N`: stderiv-transformed waveforms — spatial derivative across
  group channels + temporal first-difference with `sdiff[-1] = 0`
  boundary. Written by `ndm_extractspikes_stderiv` and
  `ndm_reextractspikes_stderiv`; bit-identical in layout and value
  space between the two.


---

*Part of the [ndmanager-plugins](../README.md) file-format reference.*

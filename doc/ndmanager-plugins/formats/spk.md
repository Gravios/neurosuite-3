# `.spk.<method>.N` — spike waveforms

Binary. No header. `nSpikes × nSamples × nChannels` int16 samples,
**sample-major** layout (all channels for sample 0 first, then all
for sample 1, etc.). File size = `nSpikes × nSamples × nChannels × 2`
bytes.

`.spk` is a **Shared** artifact under the
[variant naming convention](naming.md): the raw waveform snippets are
method-independent, so one physical copy is used across variants. It
resolves by preferring `<base>.spk.<method>.N`, then `.spk.standard.N`,
then the untagged legacy `.spk.N` (first that exists). Written by
`ndm_extractspikes` / `ndm_reextractspikes` from `.fil`.

**There is no separate stderiv `.spk`.** The old `.spkD.N` is retired: the
stderiv transform (spatial derivative across group channels + temporal
first-difference) is applied **downstream at PCA time**
(`process_pca_stderiv`) to produce `.fet.stderiv.N`, rather than being
stored as a second waveform file. A `.spk` that resolves to the raw/
untagged copy is therefore *not* in the stderiv domain even in a stderiv
session.


---

*Part of the [ndmanager-plugins](../README.md) file-format reference.*

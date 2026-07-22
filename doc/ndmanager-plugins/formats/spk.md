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

**A stderiv session has its own `.spk`.** The old flat `.spkD.N` is retired,
but only the *name* changed: `process_extractspikes_stderiv` writes the
**transformed** waveform (spatial derivative across group channels +
temporal first-difference) to `.spk.<method>.N` at **full group width**.
The transform is applied at extraction, not deferred to PCA time; what
`process_pca_stderiv` does downstream is the `SDIFF_PASS` channel
*reduction* on that already-transformed file, not the transform itself.

So `.spk.standard.N` and `.spk.stderiv.N` hold different data and are not
interchangeable. Because `spk` is classed Shared, a request that misses can
fall back to `standard` and hand back raw waveforms for a stderiv request —
check the resolved method (`resolvedIsStderiv`) rather than trusting the
fallback. A `.spk` that resolved to the raw/untagged copy is *not* in the
stderiv domain even in a stderiv session.


---

*Part of the [ndmanager-plugins](../README.md) file-format reference.*

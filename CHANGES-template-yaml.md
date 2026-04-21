## templates/template.yaml — parameter block refresh (2026-04-20)

Default session-template updated so three plugins whose parameter
surface grew over the recent patch series (`ndm_reextractspikes{,_stderiv}`,
`ndm_decomposecollisions`, `ndm_stripdat`) expose all their YAML-readable
parameters with sensible defaults and inline help.

New session YAMLs created from this template will open in Klusters/ndmanager
with every current parameter pre-populated, so operators don't need to hand-edit
the YAML to enable features like `subtractionMode=botm` or `autoStrip=true`.

This patch touches only `templates/template.yaml`.  No plugin code or
installed binary changes.

### ndm_stripdat (rewritten)

Before: one parameter (`redetectSpikes`).

After: seven parameters covering the BOTM era:

```yaml
- name: ndm_stripdat
  parameters:
  - {name: redetectSpikes,   value: 'no',                    status: Optional}
  - {name: stripClusters,    value: 'default',               status: Optional}
  - {name: overwrite,        value: 'false',                 status: Optional}
  - {name: subtractionMode,  value: 'raw',                   status: Optional}
  - {name: botmRidge,        value: '1e-3',                  status: Optional}
  - {name: autoStrip,        value: 'true',                  status: Optional}
  - {name: qualityTags,      value: 'good,great,excellent',  status: Optional}
```

Help text expanded to document:
  - The three-state strip-list resolution (sidecar → autostrip → global)
  - All three subtraction modes (raw / model / botm) with guidance on when
    to use each
  - BOTM-specific notes (ridge regularization, when to raise it)
  - autoStrip mechanics (YAML units: block with quality: labels, regeneration
    every run so the list tracks current curation)
  - Typical iterative workflow

### ndm_decomposecollisions (three params added)

Before: 7 parameters.  After: 10.  New parameters (all exposed by the
bash wrapper but previously absent from the template):

```yaml
  - {name: corrWindow,        value: 0,       status: Optional}
  - {name: nWorkers,          value: 0,       status: Optional}
  - {name: useGpu,            value: 'false', status: Optional}
```

Help entries added for each:
  - corrWindow: narrower window (e.g. 16 at 32 kHz) focuses correlation
    on the peak and reduces drift sensitivity in tails; 0 = auto uses the
    full waveform.
  - nWorkers: 0 = auto (all CPU cores); positive integer caps the pool;
    1 disables parallelism.
  - useGpu: opt-in CuPy-backed correlation; falls back to CPU when no GPU
    is present.  Mainly useful for sessions > 10M spikes on dense probes.

### ndm_reextractspikes / ndm_reextractspikes_stderiv (no change)

Already populated with the right defaults by the 2026-04-18 patch.
Confirmed correct against the shipped bash wrapper.

### ndm_redetectspikes (no change)

Intentionally empty — the script inherits all its parameters from
ndm_hipass and ndm_extractspikes and has no own parameters.

### Not touched

- All non-recent programs (ndm_klustakwik, ndm_pca, ndm_extractspikes,
  ndm_lfp, etc.) keep their existing defaults.
- No schema changes; every new parameter follows the established
  `{name: ..., value: ..., status: Optional}` shape.

### Compatibility

Existing session YAMLs continue to work — unspecified parameters fall
back to the plugin's hard-coded defaults, which match the defaults
declared in this template.  Re-exporting an old YAML from
Klusters/ndmanager will add the new parameter entries.

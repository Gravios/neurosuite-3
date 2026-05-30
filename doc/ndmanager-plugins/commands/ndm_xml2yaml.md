# `ndm_xml2yaml` — convert XML parameter file to YAML

```bash
ndm_xml2yaml session.xml             # writes session.yaml in the same directory
ndm_xml2yaml session.xml output.yaml # explicit output path
```

After conversion, populate the `probes:` section and run
`ndm_setupgroups` to rebuild the channel groups in the YAML format.
The rest of the pipeline works from YAML only — there is no XML-reading
path in the current extraction, PCA, or KiloKlustaKwik stages.


---

*Part of the [ndmanager-plugins](../README.md) reference.
See [pipeline overview](../pipeline.md) for how this fits the full workflow.*

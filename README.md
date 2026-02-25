# neurosuite-3 YAML support — delivery package

## Contents

### patches/
27 git patches (minus 2 debug-only commits) in logical application order.
Apply with: `git am patches/*.patch`

### binaries/
Built Linux x86-64 binaries:
- `ndmanager`        — opens and saves .xml and .yaml parameter files
- `neuroscope`       — opens .yaml parameter files (video, channel colours, spikes)
- `klusters`         — saves cluster user information to .yaml parameter files
- `libklustersshared.so.2.0.0` — shared library with the YAML layer

### src/
Key source files for review.  The full diff is in the patch series.

**libklustersshared/** — single implementation of the YAML format, shared by all apps:
- `ParameterYamlReader`  — parse any neurosuite YAML file; two overloads of
  `getAnatomicalDescription` and `getSpikeDescription` cover both the ndmanager
  and neuroscope/klusters caller signatures
- `ParameterYamlWriter`  — write-from-scratch (used by ndmanager)
- `ParameterYamlModifier` — read-modify-write (used by neuroscope and klusters)
- Shared data types: `ChannelColorEntry`, `GeneralInformation`, `FileInformation`,
  `ProgramInformation`, `NeuroscopeVideoInfo`

**ndmanager/** — `NdManagerYamlReader` and `NdManagerYamlWriter` are now thin
wrappers / typedefs; all logic is in libklustersshared.

**neuroscope/** — `NeuroscopeYamlReader` delegates fully to `ParameterYamlReader`.
`ChannelDescription` is a typedef for `ChannelColorEntry`.

**klusters/** — `KlustersYamlReader` delegates fully to `ParameterYamlReader`.

### scripts/
- `ndm_xml2yaml` — converts legacy .xml parameter files to .yaml

## Bugs fixed in this delivery

| # | Severity | Description |
|---|----------|-------------|
| A | **Critical** | `getUnits()` used cluster id as map key → silent data loss for multi-group datasets where every group has cluster 1, 2, 3… Fixed: sequential document-order index (matches `XmlReader` contract) |
| B | **Data loss** | `NeuroscopeYamlReader::getRotation/getFlip/getTrajectory/getBackgroundImage` returned stubs (0/empty) so video orientation was lost on every YAML save/reopen cycle |
| C | Code quality | Unnecessary `const_cast` in `KlustersYamlReader` to call a `const` method |
| D | Code quality | Unnecessary `const_cast` in `ParameterYamlWriter::setFilesInformation` for a `const` getter |
| E | Missing feature | `NdManagerYamlReader::getVideoInfo` was a no-op stub; ndmanager's VideoPage fields (width/height/samplingRate) were blank when opening YAML files. Added `ParameterYamlReader::getTopLevelVideoInfo()` |

## Earlier fixes (also in patch series)

- 7 neuroscope segfault fixes (BUGs 1–7 from session open/load path)
- 3 process_spikegrouper fixes (distance matrix corruption, stale YAML keys, non-contiguous groups)
- process_medianfilter tiling fix
- ndm_xml2yaml converter with man page and ndManager plugin description

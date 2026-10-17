# `ndm_functions` — shared bash helpers for `ndm_*` scripts

## Synopsis

```bash
source ndm_functions          # also sources ndm_custody
```

**Input:** — (a library, not a command)

**Output:** shell functions and variables in the calling script

## Description

Every bash `ndm_*` script sources `ndm_functions`. It defines the exit codes,
messages, input/output checks, parameter-file readers, session-name
resolution and the shared spike-processing driver, and it sources
[`ndm_custody`](#ndm_custody--file-resolution-chain-of-custody), the bash
copy of the file-naming policy. It needs `python3` with PyYAML for YAML
parameter files and `xpathReader` for XML ones.

This page lists the helpers a plugin author uses. The parameter resolution
rules themselves are described in [parameters](../parameters.md).

### Script skeleton

```bash
source ndm_functions
program=$(basename $0)                      # selects this script's programs: block
[ $# -gt 1 ] && { echo_error "usage: $program [parameter-file]"; exit $GENERAL_ERROR; }
ndm_resolve_session "$@"
[ "$NDM_SHIFTED" = 0 ] && set -- "$session" "$@"
param_ext=$(param_file_ext "$session")

print_header "$session - Doing the thing"
ndm_input_extension dat                     # $inputExtension, default dat
outputs_exist $session.out && exit $OUTPUTS_EXIST
ndm_input_missing && exit $INPUTS_MISSING
check_commands_installed process_thing

nChannels=$(yaml_read $session "//acquisitionSystem/nChannels")
check_mandatory_variable $nChannels "missing number of channels"
width=$(read_script_parameter $session width); width=${width:-16}

process_thing -n $nChannels -w $width $session.$inputExtension $session.out
check_command_status process_thing
```

The order matters for batch runs: check outputs first (exit 10), then inputs
(exit 11), then read parameters and process.

## Exit codes

| Variable | Value | Meaning |
|---|---|---|
| `NO_ERROR` | 0 | Success |
| `GENERAL_ERROR` | 1 | Error — including a missing mandatory parameter (`check_mandatory_variable`) |
| `OUTPUTS_EXIST` | 10 | Outputs already present; step skipped |
| `INPUTS_MISSING` | 11 | Inputs missing; step skipped |
| `VAR_MISSING` | 20 | Defined for missing parameters; no helper uses it |

`step_completed` treats 0, 10 and 11 as success; [`ndm_start`](ndm_start.md)
uses it to decide whether a step failed.

## Messages and checks

| Function | Use |
|---|---|
| `echo_error` / `echo_warning` | Coloured `<program> - error: …` / `warning: …` on stderr |
| `echo_info`, `echo_message` | Plain message on stdout (`echo_message` is an alias) |
| `print_header text [char [style]]` | 80-column banner for a processing step |
| `print_group_failed step group` | One-line failure marker for a group, `✗` on UTF-8 terminals |
| `check_commands_installed cmd…` | Exit 1 if a command is not on `PATH` |
| `check_command_status name [code]` | After a sub-program: exit 1 on failure. Ignores 10, 11 and 141 (SIGPIPE); 126/127 report "not executable"/"not found" |
| `step_completed name [code]` | After an `ndm_*` step: returns 0 for 0/10/11, else 1. Does not exit |
| `outputs_exist file…` | Returns 0 (and warns "skipping") if any file exists |
| `inputs_missing file…` | Returns 0 (and warns "skipping") if any file is missing |

## Parameter files

The YAML readers take a session base name (no extension) and use
`SESSION.yaml`, then `SESSION.yml`. When neither exists, the same calls fall
back to `xpathReader` on `SESSION.xml`. A YAML parse error (e.g. a tab
character) is printed on stderr; a key that is not there gives an empty
string. Booleans come back as `true` / `false`; a stored `~` reads as unset.

| Function | Use |
|---|---|
| `read_script_parameter [-s script] [-a alt] SESSION name…` | Value of a parameter of the program block `$program` (or `-s script`); tries each `name` in turn, then the legacy block `-a alt` (default `$alt_program`), then the `general` block. Empty if unset |
| `yaml_read SESSION xpath` | One value by XPath-style path: `//acquisitionSystem/nChannels`, `//spikeDetection/channelGroups/group[2]/channels/channel` (1-based index; a list comes back space-separated), or the program-parameter form `//program/name[.='P']/../parameters/parameter/name[.='X']/../value` |
| `yaml_count SESSION xpath` | Length of a list, e.g. `//spikeDetection/channelGroups/group` |
| `yaml_read_key file dot.path` | One value by dotted key path, list items by 0-based index |
| `find_yaml_file SESSION` | `SESSION.yaml`, `SESSION.yml` or empty |
| `param_file_ext SESSION` | `yaml`, `yml` or `xml` |
| `check_mandatory_variable $v "description"` | Exit 1 with "description (check parameter file)" if `$v` is empty |
| `check_optional_variable $v` | Returns 0 if `$v` is set |
| `xml_read`, `xml_count` | Older names of `yaml_read` / `yaml_count`, same dispatch |

### `read_kk_param` — KiloKlustaKwik parameters

```bash
read_kk_param [-b caller_block] GROUP NAME DEFAULT
```

Returns the first value found, needs `$session`:

1. `spikeDetection.channelGroups[GROUP].klustakwik.NAME`
2. the `caller_block` program block, when `-b` is given and is not
   `ndm_klustakwik`
3. the `ndm_klustakwik` program block (then `general`)
4. `DEFAULT`

Used by [`ndm_klustakwik`](ndm_klustakwik.md) and wrappers such as
[`ndm_subcluster_unmatched`](ndm_subcluster_unmatched.md), so a sandbox sort
reads the same settings as the main sort.

## Session and input

| Function | Sets / returns |
|---|---|
| `ndm_resolve_session [arg]` | `$session`: `arg` (extension stripped) if `arg.yaml`/`.yml`/`.xml` exists in the current directory, else the directory's name. Also `$directory` (directory name) and `$NDM_SHIFTED` (1 if `arg` was used) |
| `ndm_input_extension default [override]` | `$inputExtension` from `override`, else the `inputExtension` parameter, else `default`; leading dots removed. `$NDM_INPUT_EXPLICIT=1` when the value was given rather than defaulted |
| `ndm_input_missing` | Like `inputs_missing "$session.$param_ext" "$session.$inputExtension"`, except that an explicitly given input that does not exist **exits 1** with an error instead of returning 0 for a skip |
| `ndm_parse_method "$@"` | `$NDM_METHOD` from `--method M` / `--method=M`; the other arguments in `$NDM_ARGS` |
| `ndm_process_spikes detect\|extract [file] [--method M] [--methodOrder N] [--inputExtension EXT] [--force]` | The driver behind [`ndm_detectspikes` / `ndm_extractspikes`](spike-detection.md) |

Directory and file-name helpers used by `ndm_start` (layout
`prefix-YYYYMMDD[-description]-suffix`, see [`ndm_start`](ndm_start.md#directory-layout)):
`dirname_part`, `filename_part`, `filename_date`, `format_date` (to
`YYYY-MM-DD`), `get_suffix n suffixes…` (drops a leading `-`),
`get_n_suffixes`, `list_suffixes`, `list_sessions directory suffixes…`,
`extension_exists`, `common_prefix`, `prefix_has_date`. Also `round` and
`test_time_delta`.

## `ndm_custody` — file resolution (chain of custody)

`ndm_custody` holds the bash copy of the per-group file-naming policy. The
same rules exist in C++ (`custody.hpp`) and Python
(`scripts/ndm_resolve_io.py`); the three are checked against shared vectors
(`libneurosuite-core/test/custody_vectors.tsv`) by
`test/test_custody_conformance.sh`. The naming scheme and the reasons behind
it are in [file naming](../formats/naming.md).

Every type belongs to one class:

| Class | Types | Path | Resolution |
|---|---|---|---|
| MethodSpecific | `clu`, `clc`, `fet`, `pca`, `col`, `model`, `klg` (and any unknown type) | `base.type.method.group` | Exactly that path, no fallback |
| Shared | `res`, `spk` | `base.type.method.group` | `method`, then its waveform token (`stderiv_C5_D34` → `stderiv_C5`), then `standard`, then untagged `base.type.group` |
| SessionWide | `fil`, `dat`, `xml`, `yaml`, `nrs`, `par`, `eeg`, `lfp` | `base.type` | No method, no group |

| Function | Use |
|---|---|
| `ndm_classify type` | Prints the class |
| `ndm_method_path base type group method` | `base.type.method.group` (note the argument order) |
| `ndm_untagged_path`, `ndm_session_path` | `base.type.group`, `base.type` |
| `ndm_resolve base type group method` | Prints the resolved path by class; sets `NDM_R_FOUND`, `NDM_R_METHOD`; returns 0 if found. When nothing exists, prints the path a writer should use |
| `ndm_resolve_any base type group [method]` | For `.res`: preferred method, `standard`, `stderiv`, `sdiff`, any other method-tagged copy, untagged. Prints the preferred path and returns 1 if none exists |
| `ndm_method_of path` | Method token of a per-group file name, or empty |
| `ndm_parse_anchor path` | Splits a name into `NDM_A_BASE`, `_TYPE`, `_METHOD`, `_GROUP`, `_SUFFIX`; `NDM_A_OK=1` on success |
| `ndm_parse_method_token token` | Splits `family[_<S\|C><order>][_D<lag><dims>]` into `NDM_M_FAMILY`, `_KIND`, `_ORDER`, `_LAG`, `_DIMS`; an unmatched token is all family |
| `ndm_check_method_token token` | Returns 1 with `NDM_M_ERROR` for an unknown family, `_S`/`_C` on a family other than `stderiv`, `_S` order outside 1–3, or `_C` order other than 4/5 |
| `ndm_waveform_token token` | The token without its `_D` suffix |
| `ndm_is_stderiv_method token`, `ndm_resolved_is_stderiv` | True for `stderiv` and any `stderiv_*` token (not a prefix test) |
| `ndm_sdiff_drops_last order` | True for orders 1, 3, 4, 5 (PCA sees nChannels − 1) |
| `ndm_resolve_method`, `ndm_resolve_shared`, `ndm_method_from_path` | Older names: strict path, Shared resolve, `ndm_method_of` |

## See also

- [Parameters](../parameters.md) — resolution order, `general`, `inputExtension`, KiloKlustaKwik tiers
- [File naming](../formats/naming.md)
- [`ndm_start`](ndm_start.md)

---

*Part of the [ndmanager-plugins](../README.md) reference.*

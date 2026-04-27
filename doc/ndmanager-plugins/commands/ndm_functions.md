# `ndm_functions`

Sourced by every `ndm_*` script. Provides:

- `yaml_read`, `yaml_count` — pyyaml-based querying of the session YAML.
- `read_script_parameter` — three-tier parameter resolution for pipeline
  scripts (per-group YAML → global YAML → bash default).
- `read_kk_param` — same three-tier resolution scoped to
  `programs[ndm_klustakwik].parameters`, used by both `ndm_klustakwik`
  and `ndm_subcluster_unmatched` so the sandbox sort is parameter-
  identical to the main sort.
- `check_command_status`, `outputs_exist`, `check_commands_installed`,
  `print_header`, `echo_info`, `echo_warning`, `echo_error` — logging
  and exit-code conventions.

Not a user-facing tool, but the file to edit when adding a new plugin
or a new three-tier parameter.


---

*Part of the [ndmanager-plugins](../README.md) reference.
See [pipeline overview](../pipeline.md) for how this fits the full workflow.*

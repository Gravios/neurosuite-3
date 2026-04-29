# progressbar dedupe + modernisation

## Files in this drop

Replacing in place:
- `src/libklustersshared/src/klustersshared/progressbar.h`        (rewritten)
- `src/libklustersshared/src/klustersshared/progressbar.cpp`      (rewritten)
- `src/libklustersshared/src/klustersshared/customtypes.h`        (PROGRESS_MAX_N_CHARS removed)
- `src/ndmanager-plugins/src/process_pca/CMakeLists.txt`          (links canonical sources)
- `src/ndmanager-plugins/src/process_nlxconvert/CMakeLists.txt`   (links canonical sources)
- `src/ndmanager-plugins/scripts/ndm_functions`                   (banner width 150 → 80)

## Files to delete after extraction

The plugin-local duplicates of progressbar and customtypes are no longer
compiled — the plugin CMakeLists now reference the canonical sources in
libklustersshared.  Remove the duplicates:

```bash
git rm \
    src/ndmanager-plugins/src/process_pca/progressbar.h \
    src/ndmanager-plugins/src/process_pca/progressbar.cpp \
    src/ndmanager-plugins/src/process_pca/customtypes.h \
    src/ndmanager-plugins/src/process_nlxconvert/progressbar.h \
    src/ndmanager-plugins/src/process_nlxconvert/progressbar.cpp \
    src/ndmanager-plugins/src/process_nlxconvert/customtypes.h
```

## What the new bar does

Layout, ≤ 80 columns during run, ≤ 82 columns at completion (` ✓` appended):

```
session.dat [NEV] ━━━━━━━━━━━━━━━━━━━━━━━━━━━━─────────────────────────────────
session.dat [NEV] ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━ ✓
```

- Bar width computed at start() from `min(terminalWidth, 80) - chrome - len(label) - len(step)`.
- Long labels are ellipsised with "…" so the bar never collapses below 10 chars.
- Minimalist horizontal-line style: filled `━` (U+2501 box-drawing heavy
  horizontal), unfilled `─` (U+2500 light).  No closing bracket — the
  bar runs to the line edge, with a green ✓ floating off the end at
  completion.  ASCII fallback uses `=`/`-` (no checkmark).
- Each redraw is prefixed with `\x1b[2K\r` (erase-line + carriage
  return), so any concurrent text intrusion from another process's
  stdout/stderr writes (e.g. ndm_pca's logfile-cat-back from finished
  groups while in-flight groups are still drawing bars) gets wiped on
  the next redraw cycle.

## Output channel: writes to /dev/tty

The bar opens `/dev/tty` in the constructor and writes there, bypassing
shell stdout/stderr redirections.  This makes the bar **visible even
when the wrapper script captures stdout/stderr to a logfile** — e.g.
ndm_pca's per-group parallel mode:

```bash
(once $i $PCA_THREAD_OPT) > /tmp/ndm_pca_${session}_$i.log 2>&1 &
```

The plugin's diagnostic prints (`process_pca: transformed N spikes ...`)
still go through stdout/stderr normally and are captured to the logfile,
then replayed when the wrapper cats the logfile after the parallel wait.
The progress bar shows live during execution; the logfile preserves the
diagnostic record.

Channel selection order:
1. `/dev/tty` (transient, fopen+fclose) — bypasses shell redirections.
2. stderr — if it's a TTY.
3. stdout — if it's a TTY.
4. None of the above: non-TTY mode emits one milestone line per 5%
   on stdout (logfile-clean).

## Parallel bars share the same TTY

When ndm_pca spawns N parallel groups, all N ProgressBar instances draw
to the same `/dev/tty`.  Their `\r`-based redraws will overwrite each
other on a single line — the visual is a single line that flickers
between groups.  Readable but not pretty.  If multi-line cursor-
positioning becomes a priority, the bar could be extended to take a
"row offset" parameter and use ANSI cursor-position escapes; for now
the simple single-line approach matches what apt and brew do.

## API compatibility

The constructor signature is now `ProgressBar(label, step, total)`.  The
old optional fourth `length` parameter is removed (it was always
default-50 in practice; the new layout computes its own width).  All
existing call sites in `process_pca` and `process_nlxconvert` continue
to compile unchanged.

The public methods `start()`, `advance()`, `message(s)` keep their
signatures and semantics.  A new `finish()` method is added (called by
the destructor automatically).

## Why CMake reference instead of library link

The plugins are pure CLI tools with no Qt dependency.  libklustersshared
is a Qt shared library — linking it would drag Qt into the plugin
runtime.  Instead the plugin CMakeLists pull the canonical
`progressbar.cpp` source file directly and add the canonical headers
directory to the include path.  Each plugin still gets its own object
file at build time, but the source of truth is one file.

## Banner width

`ndm_functions::print_header` was hardcoded to N=150 (legacy desktop
terminal width).  Changed to N=80 to match the bar.  Title centering
math gracefully handles titles wider than 80 chars (no negative loop
counts).


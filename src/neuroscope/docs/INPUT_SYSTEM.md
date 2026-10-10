# The NeuroScope input system

NeuroScope drives its keyboard and mouse through the **same** binding engine as
Klusters. This document is the NeuroScope-*specific* reference: where the app
registers its bindings, how the TraceView mouse monolith was decomposed onto the
registry, the frame-zoom seam, how the shared Preferences page persists through
NeuroScope's own `Configuration`, and the derived UI (cheat-sheet, active-tool
indicator).

> **Read the engine reference first.** The architecture — `Chord`, `Command`,
> `InputScope`, `BindingRegistry`, the tiered scope stack, `Passive` vs
> `Exclusive` capture, the `Focus` / `Repeat` policies, the resolver, and **the
> seam** (registry owns the trigger chord; the drag/commit body stays in the
> view) — is documented once, in
> **[The Klusters input system](../../klusters/docs/INPUT_SYSTEM.md)**. That
> engine is shared verbatim; this document does not repeat it.

---

## 1. One core, two apps

The input core was promoted out of Klusters into **`libklustersshared`** so both
GUIs link the same code and share one process-wide `input::registry()`:

| Lives in | What |
|----------|------|
| `src/libklustersshared/src/input/` | the whole engine — `chord.*`, `command.h`, `inputscope.h`, `ctx.h`, `bindingregistry.*`, `inputdispatcher.*`, `keymapprofile.*` |
| `src/libklustersshared/src/input/` | the Preferences ▸ Input UI too — `prefinput.*`, `buttonchordedit.*`, `wheelchordedit.*` (promoted so both apps get the identical page) |
| `src/libklustersshared/src/input/inputprefsstore.h` | `input::InputPrefsStore` — the abstraction that lets the shared page persist through each app's own `Configuration` (see [§5](#5-preferences-and-persistence)) |

`src/klusters/src/input/` **no longer exists** — if an older note or the Klusters
reference still points there, the code is the source of truth: the path is
`src/libklustersshared/src/input/`. Both apps build and link the
`neurosuite-qt` shared library that carries it.

---

## 2. Where NeuroScope registers its bindings

Everything is registered **once at startup** from
`NeuroscopeApp::registerInputBindings()` (`neuroscope.cpp`), which fans out to
three sites:

| Registration site | File | Registers |
|---|---|---|
| `NeuroscopeApp::registerInputBindings()` | `neuroscope.cpp` | calls `BaseFrame::registerInput(reg)`, registers the **App-layer QAction mirrors** (46 of them, all `external`), then applies the persisted overrides |
| `registerTraceInputOnce()` | `tracewidget.cpp` | the `view.trace` scope, the `+` / `-` duration commands, the two `external` scroll mirrors, the **seven `mode.*` ToolMode scopes**, and the **seven trace Gesture commands** |
| `BaseFrame::registerInput()` | `baseframe.cpp` | the `view.frame` scope and the one `frame.zoomRubberBand` Gesture |

`registerTraceInputOnce()` is guarded by a `static bool done` — scopes and
commands are registered **once, process-wide**, not per-`TraceWidget`.

### Scopes

| Scope id | Layer | Active when | Carries |
|----------|-------|-------------|---------|
| `view.frame` | `ViewType` | the view is a `BaseFrame` **and** `!managesOwnPrimaryPress()` | base rubber-band zoom (Position / Spectral) |
| `view.trace` | `ViewType` | the trace widget is focused | `+` / `-` duration, the scroll mirrors |
| `mode.zoom` | `ToolMode` | `currentMode() == BaseFrame::ZOOM` | the trace zoom gesture |
| `mode.selectChannels` | `ToolMode` | `currentMode() == TraceView::SELECT` | select-channels press |
| `mode.measure` | `ToolMode` | `currentMode() == TraceView::MEASURE` | measure press |
| `mode.selectTime` | `ToolMode` | `currentMode() == TraceView::SELECT_TIME` | select-time press |
| `mode.selectEvent` | `ToolMode` | `currentMode() == TraceView::SELECT_EVENT` | select-event press |
| `mode.addEvent` | `ToolMode` | `currentMode() == TraceView::ADD_EVENT` | add-event press |
| `mode.drawLine` | `ToolMode` | `currentMode() == TraceView::DRAW_LINE` | draw-time-line press |

### Commands

| Command id | Scope | Kind | Default chord | Effect |
|------------|-------|------|---------------|--------|
| `trace.durationDouble` | `view.trace` | Action | `+` (AtLeast) | double the time window |
| `trace.durationHalve` | `view.trace` | Action | `-` (AtLeast) | halve the time window |
| `trace.scrollLeft` | `view.trace` | Action · `external` | `Left` | scroll left a quarter window |
| `trace.scrollRight` | `view.trace` | Action · `external` | `Right` | scroll right a quarter window |
| `frame.zoomRubberBand` | `view.frame` | **Gesture** | Left + any mod | begin the base rubber-band zoom |
| `trace.zoomRubberBand` | `mode.zoom` | **Gesture** | Left + any mod | begin the trace rubber-band zoom |
| `trace.selectChannelsPress` | `mode.selectChannels` | **Gesture** | Left + any mod | begin channel selection |
| `trace.measurePress` | `mode.measure` | **Gesture** | Left + any mod | begin a measurement |
| `trace.selectTimePress` | `mode.selectTime` | **Gesture** | Left + any mod | begin a time selection |
| `trace.selectEventPress` | `mode.selectEvent` | **Gesture** | Left + any mod | begin event selection |
| `trace.addEventPress` | `mode.addEvent` | **Gesture** | Left + any mod | add an event at the click |
| `trace.drawLinePress` | `mode.drawLine` | **Gesture** | Left + any mod | begin the time line |

Every Gesture's chord is `Left press, any modifiers` (`ModMatch::AtLeast`). They
never collide, because each lives in a `mode.*` scope that is only active in its
own mode — exactly one binds `Left` at any instant. `view.frame` also binds
`Left`, and it stands down for the trace view through the `managesOwnPrimaryPress`
guard ([§4](#4-the-primary-press-seam)).

The 46 `external` QAction mirrors (File / Edit / Tools / Channels / … menu
actions, registered via `registerActionCommand()`) exist only so those actions
appear in Preferences ▸ Input and the cheat-sheet and are rebindable; the
resolver **skips** `external` commands so Qt keeps dispatching them without a
double-fire.

---

## 3. The TraceView mouse decomposition

`TraceView::mousePressEvent` used to be a ~470-line switch over the active mode.
It is now the whole of it:

```cpp
void TraceView::mousePressEvent(QMouseEvent* event){
    // All TraceView press interactions are now registry Gesture commands.
    if(dispatchInput(event)) return;
}
```

Each tool's press became a **Gesture** whose `invoke()` only *begins* the
interaction; the drag preview (`mouseMoveEvent`) and the commit
(`mouseReleaseEvent`) stay inline in `TraceView`, which is the seam the engine
reference describes.

Two **pure helpers** feed the gesture bodies (extracted so several modes reuse
the identical hit-test; each extraction was verified behavior-preserving by a
normalized token diff against the old inline code):

- `resolveClickGeometry(viewportPos)` → `TraceClickGeometry { current, x, groupIndex, sampleIndex, labelSelected }`
- `nearestChannelAt(geometry)` → channel index, or `-1` for an empty single-column click

The seven begin-bodies are **public** on `TraceView`:

```
beginSelectChannelsPress(viewportPos, modifiers)   // reads the live modifiers
beginMeasurePress(viewportPos)
beginSelectTimePress(viewportPos)
beginSelectEventPress(viewportPos)
beginAddEventPress(viewportPos)
beginDrawLinePress(viewportPos)
```

plus the zoom, which runs through `BaseFrame::beginBaseZoom()` under
`mode.zoom`'s `trace.zoomRubberBand`. They are public by necessity: the
registration lambdas live in an **anonymous namespace** in `tracewidget.cpp` and
have to call them. The two geometry helpers stay `protected`.

---

## 4. The primary-press seam: `managesOwnPrimaryPress`

`BaseFrame` funnels **its own** `mousePressEvent` through `dispatchInput()` as
well, carrying one gesture — `frame.zoomRubberBand` on `view.frame` — so the
Position and Spectral views' zoom goes through the registry too, mirroring the
trace `mode.zoom` gesture.

But `TraceView` *is* a `BaseFrame`, and it also binds `Left` (seven ways, by
mode). If `view.frame` and the trace `mode.*` scopes both bound `Left` for the
trace view they would collide. The guard:

```cpp
// baseframe.h  (in class BaseFrame)
virtual bool managesOwnPrimaryPress() const { return false; }
// traceview.h  (in class TraceView)
bool managesOwnPrimaryPress() const override { return true; }
```

`view.frame`'s `active()` predicate is `bf && !bf->managesOwnPrimaryPress()`, so
the frame zoom is live for Position / Spectral but **stands down for TraceView**,
whose own `mode.*` gestures own the `Left` press. `TraceView` overrides
`mousePressEvent` and never reaches `BaseFrame::mousePressEvent` anyway; the guard
is what keeps the *scope* from claiming the press in the resolver.

> Palette (channel-group) navigation was deliberately **left at the inline seam**
> — it is not a registry gesture.

---

## 5. Preferences and persistence

The shared `PrefInput` page cannot know about `neuroscope::Configuration`. The
seam is **`input::InputPrefsStore`**, an abstract interface with four accessors:

```cpp
virtual const QMap<QString,QString>& getInputBindingOverrides() const = 0;   // commandId -> chord string
virtual void                          setInputBindingOverrides(const QMap<QString,QString>&) = 0;
virtual const QMap<QString,QString>& getInputProfiles() const = 0;           // saved keymap layouts
virtual void                          setInputProfiles(const QMap<QString,QString>&) = 0;
```

`neuroscope::Configuration` (like `klusters::Configuration`) **inherits**
`InputPrefsStore` and stores the two string maps; `read()`/`write()` persist them
through `QSettings` under `"inputBindings"` / `"inputProfiles"`. The
serialization of a chord map ↔ string map lives in the shared `keymapprofile.h`,
so `Configuration` stays a dumb string store.

At startup, `registerInputBindings()`:

1. reads the persisted overrides from `Configuration`,
2. calls `reg.setOverride(id, chord)` for each one that is still valid (the
   command still exists and the chord parses), then
3. calls `applyInputOverridesToActions()`, which pushes any override targeting an
   `external` QAction mirror back onto the live `QAction`'s shortcut.

### The bundled Default preset

`:/keymaps/Default.keymap` (declared in `neuroscope-keymaps.qrc`) ships one
built-in layout. It carries **no binding lines**, so *applying* it clears every
override and restores NeuroScope's shipped defaults. The file also doubles as the
authoring template for a new bundled layout — its header explains the
`name = … / <commandId> = <chord>` format and the qrc/CMake step to add one.

---

## 6. Derived UI

Everything below is **derived from the registry** by iteration, so a new command
or scope surfaces with no edit here:

- **Help ▸ Keyboard Shortcuts…** (`showKeyboardShortcuts()`, `neuroscope.cpp`) —
  a read-only cheat-sheet dialog titled *Keyboard Shortcuts*, built by walking
  the registry: split into a **Keyboard** section and a **Mouse** section and,
  within each, grouped by category (the menu or tool a command belongs to). Each
  row shows the command's **effective** chord.
- **Preferences ▸ Input** — the shared `PrefInput`, the editable mirror of the
  same iteration (the keymap-layout bar + one rebind row per command).
- **The active-tool indicator** — the seven tool `QAction`s are checkable members
  of an exclusive `QActionGroup` (`mToolGroup`). `syncToolChecks()` moves the
  check to the **active display's** `currentMode()` (tools are per-display), so
  switching displays makes the toolbar reflect the mode of the one you land on.

---

## 7. Recipes (NeuroScope-specific)

- **A new rebindable key on the trace:** add an `Action` command to `view.trace`
  in `registerTraceInputOnce()` with a `Chord::key()` default and an `invoke()`.
- **A new trace tool:** register a `mode.<x>` `ToolMode` scope (active when
  `currentMode() == X`) plus a `Gesture` command whose `invoke()` *begins* it;
  keep the move/release inline in `TraceView`; add the tool `QAction` to
  `mToolGroup` so the active-tool indicator tracks it, and give it a
  `TraceView` mode constant.
- **A new frame-wide gesture (all non-trace frames):** add it to
  `BaseFrame::registerInput()` on `view.frame`.
- **Mirror a new menu `QAction` so it's rebindable:**
  `registerActionCommand(id, category, action)` — registered `external`, so the
  resolver skips it and `applyInputOverridesToActions()` carries any override
  back onto the live shortcut.
- **Anything structural** (the engine, capture, the scope stack, the resolver,
  adding a modal/Exclusive mode, testing) — see the
  [Klusters reference](../../klusters/docs/INPUT_SYSTEM.md).

---

## 8. Gotchas (NeuroScope-specific)

- **`TraceView::managesOwnPrimaryPress()` must stay `true`.** Flip it and both
  `view.frame`'s zoom and the trace `mode.*` tools grab `Left` for the trace view.
- **The `begin*` press bodies are `public` by necessity** — the registration
  lambdas live in an anonymous namespace in `tracewidget.cpp`. The geometry
  helpers (`resolveClickGeometry`, `nearestChannelAt`) stay `protected`.
- **`registerTraceInputOnce()` is process-wide** (the `static bool done` guard) —
  it is not per-view registration.
- **Persisted overrides outlive code.** A binding that "breaks" after an update
  is usually a stale override, not a regression — Preferences ▸ Input ▸ *Reset
  all to defaults*.

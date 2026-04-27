# Probe Maker — interactive probe design and editing

The Probes tab today (`ProbePage` over `ProbeLayout` / `probelayout.ui`) is a
table-based form for *referencing* probes from a library: it stores
`probeFile` paths, channel offsets, and group-id mappings (see
`libklustersshared/parameteryamlreader_probes.{h,cpp}` for `ProbeEntry`).
This is fine for the canonical workflow — pick a `.probe` file from the
library, set a channel offset, attach to anatomical groups — but it has
two gaps:

1. **No way to edit a probe in-app.**  When a `.probe` file in the
   library doesn't quite match the actual hardware (custom builds,
   tetrode-array variants, soldering corrections), the user has to drop
   to a text editor and hand-edit YAML.  The existing canonical files
   (`src/nphys-data/src/probes/...`) are well-written but the format is
   subtle enough that text editing is error-prone.

2. **No visual feedback.**  Site geometry is the part of a probe most
   likely to be wrong, and the table form doesn't show whether two pads
   collide, whether a shank is inverted, or whether channel ordering
   matches what the amplifier actually delivers.

The Probe Maker adds an interactive editor for the same `.probe` schema,
sharing the file format with the canonical library.  Saving a session-
local probe writes to `<session>.ndm.<n>.probe` (using the same `.ndm.`
namespace prefix as `.ndm.<n>.pipeline` files); loading accepts both
session-local files and library files.

## File format

The Probe Maker reads and writes the existing `.probe` YAML schema
(`probeFile:` top level, `version`/`vendor`/`model`/`totalChannels`/
`substrate`/`shanks`/`sites`/`channelMap`/`notes`).  This is the format
already used by every file under `src/nphys-data/src/probes/` and parsed
by `ndm_setupgroups`; nothing new needs to be defined.

For session-local probes (designed in-app rather than picked from the
library) the file lives alongside the session YAML:

```
session.yaml                            ← session schema
session.ndm.default.pipeline            ← Pipeline Designer state
session.ndm.default.probe               ← Probe Maker state, default
session.ndm.32-tetrodes.probe           ← named variant via Save As
```

The `.ndm.` infix namespaces our files so they don't collide with
`.probe` files in shared libraries that might have the same basename
(e.g. `tetrode.probe`).

`ProbePage`'s existing `ProbeEntry::probeFile` field accepts either form
— a library-relative path or a session-local `<session>.ndm.<n>.probe`
path — without changes.

## Framework: QGraphicsScene, not custom-paint

The Pipeline Designer uses a hand-rolled `PipelineCanvas : QWidget` with
custom `paintEvent`, hit-testing, and drag-drop in ~1700 lines.  That
choice was reasonable there because nodes are uniform rectangles, item
counts stay below ~50, and there's no hierarchy or geometric meaning to
respect.

The Probe Maker has different constraints:

| | Pipeline Designer | Probe Maker |
|---|---|---|
| Item count | ~10–50 | up to 1536 (Neuropixels 2.0) |
| Hierarchy | Flat | Connector → shank → channel — essential |
| Layout | Arbitrary | Real micron coordinates |
| Selection | One level | Connector / shank / channel |
| Per-item shape | Uniform rounded rect | Polygon (shank) + circles (pads) |

`QGraphicsView` + `QGraphicsScene` are part of Qt6 (no new dependency)
and give us, for free, the features we'd otherwise hand-roll:

- **Item parenting is the hierarchy.**  `channelItem->setParentItem(shankItem)`
  means moving the shank moves all its channels with no separate
  bookkeeping.  Selecting a shank and pressing Delete cascades to its
  channels naturally.
- **Built-in selection and dragging** via `QGraphicsItem::ItemIsSelectable`
  and `ItemIsMovable`.  Shift-click for multi-select, rubber-band
  selection, all stock.
- **Spatial indexing for hit-testing.**  A custom canvas hit-tests in
  O(n); `QGraphicsScene` uses a BSP tree, so 1500 channels stay
  responsive.
- **Z-ordering and level-of-detail painting**, so we can fade individual
  pads when zoomed out and show shank silhouettes only.
- **`QGraphicsView` handles zoom/pan natively** via `QTransform`
  (Ctrl+wheel zoom, middle-drag pan), important when µm-scale features
  meet whole-shank views.

The cost is modest: about the same line count as the custom canvas to
do dramatically more.

## Two-panel layout: logical graph + physical layout

The same data model drives two views:

```
  ┌──────────────────────────────────────────────┐
  │  Toolbar: Save · Save As · Load · Add shank  │
  ├────────────────────┬─────────────────────────┤
  │  Logical graph     │  Physical layout        │
  │  (hierarchy)       │  (geometry)             │
  │                    │                         │
  │  Connector ★ ROOT  │     ╔══════╗            │
  │       │            │     ║      ║            │
  │   ┌───┴────┐       │     ║ ●  ● ║            │
  │   │        │       │     ║ ●  ● ║            │
  │ Shank A  Shank B   │     ║ ●  ● ║            │
  │   │        │       │     ║      ║            │
  │ ch 0…15  ch 16…31  │     ╚══╤═══╝            │
  │                    │       \V/               │
  └────────────────────┴─────────────────────────┘
  Inspector (selection-driven, identical to Pipeline tab)
```

- **Logical view** — uses the same visual idiom as the Pipeline
  Designer.  Gold "★ ROOT" for the connector, teal nodes for shanks,
  blue pills for channels.  This is where the user edits *structure*:
  add/remove shanks, reassign channel-id ranges, group channels into
  bundles.
- **Physical view** — the same data drawn to scale in micron
  coordinates.  Shanks are polygons (`QGraphicsPolygonItem`) with a tip
  wedge; pads are circles (`QGraphicsEllipseItem`) parented to the
  shank.  This is where the user edits *geometry*: drag a pad to
  reposition, set tip angle, set shank length/width.

Both views observe the same `ProbeConnector` data and emit
`selectionChanged(itemId)` signals.  Clicking a shank in the logical
view highlights its silhouette in the physical view and vice versa.

## Data model

The C++ types live alongside the existing `ProbeEntry`/`ProbeGroupMeta`
in `parameteryamlreader_probes.h`, extending the model rather than
replacing it:

```cpp
struct ProbeChannel {
    int     hardwareId;     ///< 0-based hardware channel index
    QPointF posUm;          ///< (x, y) on the shank, in µm
    int     siteIndex = -1; ///< probe site number for display; -1 = unset
    qreal   areaUm2  = 177; ///< pad area
    bool    enabled  = true;
};

struct ProbeShank {
    QString id;                 ///< "shankA", "shankB" — local identifier
    QString label;              ///< "Shank A" — user-facing
    QPointF originUm;           ///< shank origin in connector coords
    qreal   lengthUm  = 1500.0;
    qreal   widthUm   =   70.0;
    qreal   tipAngle  =   90.0; ///< degrees, 90° = blunt
    QString layout    = "linear"; ///< or "tetrode", "poly2", "poly3", custom
    QList<ProbeChannel> channels;
};

struct ProbeConnector {
    QString version       = "1.0";
    QString vendor;
    QString model;
    QString catalogPage;
    int     totalChannels = 0;
    QString substrateMaterial = "silicon";
    qreal   substrateThicknessUm = 0.0;
    QList<ProbeShank> shanks;
    QString channelMapDescription;
    QList<int> channelMap;     ///< empty = sequential; else hardware → site
    QString notes;
};
```

The serialiser in `parameteryamlreader_probes.cpp` walks this structure
into the canonical `probeFile:` schema.  Round-trip is loss-free for the
fields the schema defines; unknown fields in a loaded file are preserved
in a `QMap<QString, YAML::Node> extras` member (TODO: add when needed —
none of the canonical library files use unknown fields today).

## Save / Save As / Load actions

Same idiom as the Pipeline tab:

| Action | Where | Shortcut | Writes |
|---|---|---|---|
| Save Probe | File menu, Probe toolbar | Ctrl+Alt+R | `<session>.ndm.default.probe` |
| Save Probe As… | File menu, Probe toolbar | Ctrl+Alt+Shift+R | `<session>.ndm.<n>.probe` |
| Load Probe… | File menu | — | replaces graph from selected `*.probe` |

(R for "rrobe" is admittedly stretching it; I picked it because the
obvious P is taken by the Pipeline action.  Open to a different binding.)

The same name-sanitisation rules apply (`[a-z0-9_-]`, lowercased,
whitespace → underscore, default warns on overwrite).

## Open questions left for implementation

1. **Auto-load default probe on session open?**  Pipeline files
   auto-load.  For probes the answer is probably *no* — the existing
   `ProbeEntry::probeFile` already references a library probe, and an
   auto-loaded `.ndm.default.probe` would silently override it without
   the user understanding why.  Better: surface a "Use session probe"
   toggle in the existing ProbePage form when a `.ndm.default.probe`
   exists.
2. **Channel-map editing.**  The data model carries `channelMap`
   (hardware → site permutation) but the UI in the prototype just shows
   it as a JSON-ish text box.  A drag-to-reorder list view would be
   nicer but isn't a v1 requirement.
3. **Probe library palette.**  Drag from `src/nphys-data/src/probes/`
   directly into the canvas?  Future work.

For the v1 the page edits a single in-memory `ProbeConnector`, with
Save/Save As/Load to round-trip it through `.probe` YAML.

## File inventory

```
src/ndmanager/src/probemakerpage.{h,cpp}                    NEW — main page widget
src/ndmanager/src/probelogicalview.{h,cpp}                  NEW — QGraphicsView for hierarchy
src/ndmanager/src/probephysicalview.{h,cpp}                 NEW — QGraphicsView for µm coords
src/ndmanager/src/probemakeritems.{h,cpp}                   NEW — QGraphicsItem subclasses
src/libklustersshared/.../parameteryamlreader_probes.{h,cpp} EXTEND — connector/shank/channel types
src/ndmanager/src/CMakeLists.txt                            EDIT — add new sources
src/ndmanager/src/parameterview.{h,cpp}                     EDIT — wire ProbeMakerPage as new tab
src/ndmanager/src/ndmanager.{h,cpp}                         EDIT — File menu actions
doc/ndmanager/README.md                                     EDIT — Probe Maker tab section
doc/design/probe-maker.md                                   NEW — this file
CHANGELOG.md                                                EDIT — entry for the feature
```

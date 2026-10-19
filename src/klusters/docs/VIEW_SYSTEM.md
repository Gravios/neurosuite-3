# The Klusters view system

*Part of the [Developer Guide](../../../doc/dev/DEVELOPER_GUIDE.md), the index of internals references.*

A developer reference for the on-screen view layer of Klusters — the frame/zoom base chain, the per-document container, and the four curation matrices. Every structural claim below cites the file and names the symbol it is about (grep for the symbol to find the line; line numbers are not given because they drift); files under `src/klusters/src/` are named bare, and the shared `DockArea` base is cited by its path under `src/libklustersshared/`. Anything not read directly out of the code is marked **(unverified)**.

<details>
<summary>Sources this reference was written against</summary>

**Source note.** This reference was written against the base chain (`baseframe.h`/`.cpp`, `bufferedview.h`/`.cpp`, `viewwidget.h`/`.cpp`), the concrete views (`clusterview.h`, `waveformview.h`, `correlationview.h`, `errormatrixview.h`/`.cpp`, `traceview.h`, `tracewidget.h`, `templateview.h`, `templatematrixview.h`/`.cpp`, `residualmatrixview.h`/`.cpp`, `driftmatrixview.h`/`.cpp`, `mergerecommendview.h`), the container (`klustersview.h`/`.cpp`) and its creation site in `klusters.cpp`, the matrix helpers (`matrixviewport.h`, `matrixnavigator.h`, `matrixtemplatestrip.h`/`.cpp`, `matrixtemplatecols.h`, `matrixgrid.h`, `matrixbadge.h`, `matrixclick.h`), and the shared `src/libklustersshared/src/gui/dockarea.h`/`.cpp`. Cross-references to `STANDARDIZATION.md` use its section numbers; the document layer is covered by [DOCUMENT_MODEL.md](DOCUMENT_MODEL.md) and the input engine by [INPUT_SYSTEM.md](INPUT_SYSTEM.md).

</details>

**Contents:** [1. The view class hierarchy](#1-the-view-class-hierarchy) · [2. What each base layer provides](#2-what-each-base-layer-provides) · [3. `KlustersView` — the per-document container — and `DockArea`](#3-klustersview--the-per-document-container--and-dockarea) · [4. `setConnections` — wiring document signals to each view](#4-setconnections--wiring-document-signals-to-each-view) · [5. The matrix-view support classes](#5-the-matrix-view-support-classes) · [6. The input seam](#6-the-input-seam) · [7. Adding a new view — checklist](#7-adding-a-new-view--checklist) · [8. Cross-check against `STANDARDIZATION.md`](#8-cross-check-against-standardizationmd)

---

## 1. The view class hierarchy

Klusters is a document/view application: one `KlustersDoc` (see [DOCUMENT_MODEL.md](DOCUMENT_MODEL.md)) is presented by one or more `KlustersView` containers, each of which docks a set of concrete view widgets. The drawing views descend from a three-layer frame chain rooted in `QFrame`; the newer curation matrices and the template/trace readers are plain `QWidget`s outside that chain.

The frame chain:

```
QFrame → BaseFrame → BufferedView → ViewWidget → { ClusterView, WaveformView, CorrelationView, ErrorMatrixView }
                                  └→ TraceView   (sibling of ViewWidget)
```

| Class | Base class | Decl (file) | Role |
|---|---|---|---|
| `BaseFrame` | `QFrame` | `baseframe.h` | zoom/rubber-band frame, world↔viewport coordinate math, input seam |
| `BufferedView` | `BaseFrame` | `bufferedview.h` | off-screen double-buffer paint lifecycle (template method) |
| `ViewWidget` | `BufferedView` | `viewwidget.h` | document/view references + the shared cluster-edit slot vocabulary + worker-thread seam |
| `ClusterView` | `ViewWidget` | `clusterview.h` | the feature scatter / cluster view (primary) |
| `WaveformView` | `ViewWidget` | `waveformview.h` | mean/sample waveforms per cluster |
| `CorrelationView` | `ViewWidget` | `correlationview.h` | auto/cross-correlograms |
| `ErrorMatrixView` | `ViewWidget` | `errormatrixview.h` | confusion/error probability matrix |
| `TraceView` | `BufferedView` | `traceview.h` | raw trace drawing surface (sibling of `ViewWidget`) |

Views that are **plain `QWidget`** — outside the `BaseFrame` chain entirely:

| Class | Base class | Decl (file) | Role |
|---|---|---|---|
| `TemplateMatrixView` | `QWidget` | `templatematrixview.h` | template (xcorr) similarity matrix |
| `ResidualMatrixView` | `QWidget` | `residualmatrixview.h` | residual similarity matrix |
| `DriftMatrixView` | `QWidget` | `driftmatrixview.h` | drift (cross-corr) matrix |
| `TemplateView` | `QWidget` | `templateview.h` | template-library reader; composes `TemplateWavePanel` (`templateview.h`) + `PartitionStrip` (`templateview.h`) |
| `TraceWidget` | `QWidget` | `tracewidget.h` | dock wrapper that **owns the inner** `TraceView view;` (`tracewidget.h`) plus the position/scroll controls |
| `MergeRecommendView` | `QWidget` | `mergerecommendview.h` | read-only merge-candidate panel in the palette stack |

The container and its base:

| Class | Base class | Decl (file) |
|---|---|---|
| `KlustersView` | `DockArea` | `klustersview.h` |
| `DockArea` | `QScrollArea` | `src/libklustersshared/src/gui/dockarea.h` |

> **Being a `ViewWidget` does not mean a view uses the buffered skeleton or the spike-view wiring.** `ErrorMatrixView` sits in the `ViewWidget` chain for its document/view references and thread seam, but it **overrides `paintEvent` directly** (`errormatrixview.h`) and is wired like a matrix (§4), not like the other `ViewWidget`s. Conversely, `TraceView` (the `BufferedView` drawing surface) is not docked directly — the dock holds a `TraceWidget` (a `QWidget`) that wraps it (`tracewidget.h`; construction at `klustersview.cpp`).

---

## 2. What each base layer provides

### 2.1 `BaseFrame` — the zoom/coordinate frame and the input seam

`BaseFrame` owns the mapping between the "world" (a `ZoomWindow window;`, `baseframe.h`) and the on-screen `QRect viewport;` (`baseframe.h`), the rubber-band zoom, and the repaint-level state.

| Provides | Decl (file) |
|---|---|
| Mode base + retired `ZOOM` value (`enum {NONE=0, ZOOM=1}`) | `baseframe.h` |
| World↔viewport conversions (`viewportToWorld` / `worldToViewport` + width/height variants) | `baseframe.h` |
| Repaint-level enum `DrawContentsMode{REFRESH=1, UPDATE=2, REDRAW=3}` | `baseframe.h` |
| Monotonic level raise `invalidate(level)` | `baseframe.h` |
| Rubber band + first-click state (`mRubberBand`, `firstClick`, `rubber`) | `baseframe.h` |
| `setMode` / `updateDrawing` / `changeBackgroundColor` slots | `baseframe.h` |
| Input seam: `dispatchInput`, `managesOwnPrimaryPress`, `beginBaseZoom`, `static registerInput` | `baseframe.h` |

The repaint-level discipline is the one load-bearing detail here. The three modes coalesce, so a lower level must never silently cancel a higher one that another method queued the same turn:

```cpp
enum DrawContentsMode{REFRESH=1, UPDATE=2,REDRAW=3};               // baseframe.h
// Raise the pending redraw level, never lower it ...
void invalidate(DrawContentsMode level){ if(level > drawContentsMode) drawContentsMode = level; } // baseframe.h
```

### 2.2 `BufferedView` — the off-screen double buffer

`BufferedView` owns the double-buffer lifecycle once, as a template-method `paintEvent`, and exposes hooks the view overrides instead of re-implementing the whole paint:

| Member | Decl (file) |
|---|---|
| `QPixmap doublebuffer;` (the cached layer) | `bufferedview.h` |
| `bool renderReady() const` — gate the whole buffered repaint | `bufferedview.h` |
| `bool paintDirect(QPainter&)` — paint wholesale, skip the buffer | `bufferedview.h` |
| `void beforeRedraw()` — runs before a REDRAW samples the window | `bufferedview.h` |
| `QRect computeViewport()` — device rect the world maps into | `bufferedview.h` |
| `int bufferPad()` — extra pixels around the buffer pixmap | `bufferedview.h` |
| `void setupWorldTransform(QPainter&)` — install world→device transform | `bufferedview.h` |
| `void paintBuffer(QPainter&, DrawContentsMode)` — the cached content | `bufferedview.h` |
| `void paintBufferDeviceLayer(QPainter&)` — device-space content baked in | `bufferedview.h` |
| `void paintWidgetOverlays(QPainter&)` — live overlays on the widget | `bufferedview.h` |
| `void afterPaint(QPainter&)` — end-of-paint hook | `bufferedview.h` |
| `void ensureDoubleBuffer()` / `void paintEvent(QPaintEvent*) override` | `bufferedview.h` |

The skeleton (`bufferedview.cpp`) is the one place the modes are consumed:

```cpp
if ((drawContentsMode == UPDATE || drawContentsMode == REDRAW) && renderReady()) {
    ensureDoubleBuffer();
    if (drawContentsMode == REDRAW) beforeRedraw();     // bufferedview.cpp
    QPainter buf(&doublebuffer);
    setupWorldTransform(buf);
    paintBuffer(buf, drawContentsMode);                 // cached layer
    buf.resetTransform();
    paintBufferDeviceLayer(buf);                        // device-space content baked in
    buf.end();
    drawContentsMode = REFRESH;                         // the one legitimate lowering  // bufferedview.cpp
}
widget.drawPixmap(0, 0, doublebuffer);                  // blit
paintWidgetOverlays(widget);                            // live overlays, every paint   // bufferedview.cpp
afterPaint(widget);
```

Only three views ride this skeleton (they implement the hooks and declare no `paintEvent`): `ClusterView` (`clusterview.h`), `WaveformView` (`waveformview.h`), `CorrelationView` (`correlationview.h`).

> **The header's five-user claim is intent, not current state.** `bufferedview.h` says the cluster, waveform, error-matrix, correlation and trace views all drive this skeleton. In the current tree `ErrorMatrixView` and `TraceView` override `paintEvent` directly (`errormatrixview.h`, `traceview.h`) rather than implementing the hooks. `ErrorMatrixView` still reuses the inherited `BufferedView::doublebuffer` (`bufferedview.h`; `errormatrixview.cpp`); `TraceView` keeps its own buffering behind its own `paintEvent`.

### 2.3 `ViewWidget` — document/view references, the cluster-edit slot vocabulary, and the thread seam

`ViewWidget` adds the document binding and the virtual slots every scatter/selection view shares (`viewwidget.h` ctor takes `KlustersDoc& doc, KlustersView& view`). Members: `KlustersView& view` (`viewwidget.h`), `KlustersDoc& doc`, `QList<int> clusterUpdateList`, `QStatusBar* statusBar`.

| Virtual slot | Decl (file) | Virtual slot | Decl (file) |
|---|---|---|---|
| `updatedDimensions(int,int)` | `viewwidget.h` | `spikesAddedToCluster(int,bool)` | `viewwidget.h` |
| `singleColorUpdate(int,bool)` | `viewwidget.h` | `emptySelection()` | `viewwidget.h` |
| `addClusterToView(int,bool)` | `viewwidget.h` | `updateClusters(QList<int>&,bool,bool)` | `viewwidget.h` |
| `removeClusterFromView(int,bool)` | `viewwidget.h` | `undoUpdateClusters(QList<int>&,bool)` | `viewwidget.h` |
| `addNewClusterToView(QList<int>&,int,bool)` | `viewwidget.h` | `isThreadsRunning() const` | `viewwidget.h` |
| `addNewClusterToView(int,bool)` | `viewwidget.h` | `stopRunningThreads()` | `viewwidget.h` |
| `spikesRemovedFromClusters(QList<int>&,bool)` | `viewwidget.h` | `supersedeRunningThreads()` | `viewwidget.h` |

The last three are the concurrency seam: a membership edit calls `supersedeRunningThreads()` (non-blocking; in-flight jobs fail their generation guard), while writers of shared mutable data use the blocking `stopRunningThreads()`. See [CONCURRENCY.md](CONCURRENCY.md).

---

## 3. `KlustersView` — the per-document container — and `DockArea`

`KlustersView` is the per-document view container (`klustersview.h`). One is created per display in `KlustersApp::createDisplay(DisplayType)` (`klusters.cpp`) with `new KlustersView(...)` (`klusters.cpp`) and registered with the document via `doc->addView(view)` (`klusters.cpp`) — so it joins the document's notify set (`KlustersDoc::viewList`, see [DOCUMENT_MODEL.md](DOCUMENT_MODEL.md) §1).

The kind of display is a `DisplayType` enum (`klustersview.h`): `CLUSTERS, WAVEFORMS, CORRELATIONS, OVERVIEW, GROUPING_ASSISTANT_VIEW, ERROR_MATRIX, TRACES, TEMPLATE_MATRIX, RESIDUAL_MATRIX, DRIFT_MATRIX, TEMPLATE_LIBRARY`.

### 3.1 How views are docked and scrolled

`DockArea` is a `QScrollArea` that scrolls an inner `QMainWindow mMainWindow;` (`dockarea.h`) — the main window is what actually hosts the `QDockWidget`s, and the surrounding scroll area supplies a scrollbar when too many docks are packed in (`dockarea.h`):

```cpp
DockArea::DockArea (QWidget * parent) : QScrollArea ( parent ) {   // dockarea.cpp
    mMainWindow.setDockOptions(QMainWindow::AllowNestedDocks | ... | QMainWindow::AllowTabbedDocks);
    mMainWindow.setWindowFlags ( Qt::Widget );
    QScrollArea::setWidgetResizable(true);
    QScrollArea::setWidget(&mMainWindow);                           // dockarea.cpp
}
void DockArea::addDockWidget ( Qt::DockWidgetArea pArea, QDockWidget * pDockwidget ) {
    pDockwidget->setObjectName ( pDockwidget->windowTitle() );
    ... // register in mDockWidgetByNameMap
    mMainWindow.addDockWidget ( pArea, pDockwidget );              // dockarea.cpp — forwards to the inner main window
}
```

`splitDockWidget` / `tabifyDockWidget` / `resizeDocks` are the same thin forwards to `mMainWindow` (`dockarea.h`).

In the `KlustersView` constructor each base display builds a `mainDock = new QDockWidget(...)` (`klustersview.cpp`), sets its widget to the concrete view, and calls `addDockWidget(Qt::TopDockWidgetArea, mainDock)` (`klustersview.cpp`). Composite displays (`OVERVIEW`, `GROUPING_ASSISTANT_VIEW`) stack/tabify their docks via `applyOverviewLayout()` (`klustersview.cpp`). Additional sub-views are added to an existing container with `KlustersView::addView(DisplayType, ...)` (`klustersview.cpp`, decl `klustersview.h`), which likewise builds a `QDockWidget` and calls `addDockWidget` (e.g. `klustersview.cpp`).

Container-owned handles:

| Member | Decl (file) |
|---|---|
| `QList<ViewWidget*> viewList;` (the per-`ViewWidget` notify/iteration set) | `klustersview.h` |
| `TraceWidget* traceWidget;` | `klustersview.h` |
| `currentViewWidget` (active `ViewWidget`) | `klustersview.cpp` |
| `QPointer<QDockWidget>` matrix/trace docks for the overview layout | `klustersview.h` |

> **Only `ViewWidget`s live in `viewList`.** The three plain-`QWidget` matrices, `TemplateView` and `MergeRecommendView` are never appended to `viewList` (`klustersview.h`). The document reaches those purely through Qt signals (§4); the `for v : *viewList` method-call loop the curation code uses (see [DOCUMENT_MODEL.md](DOCUMENT_MODEL.md) §6) only ever touches `ViewWidget`s — which is why `ErrorMatrixView`, a `ViewWidget`, *is* in the list while the other three matrices are not.

### 3.2 Matrix zoom sync

When several matrices are docked together, `connectMatrixZoomSync()` (`klustersview.cpp`) wires every ordered pair so panning/zooming one drives the rest, via a one-way `viewChanged → setViewState` link made with `Qt::UniqueConnection` (`linkMatrixZoom`, `klustersview.cpp`). `setViewState` never re-emits `viewChanged`, so the cross-links cannot loop.

---

## 4. `setConnections` — wiring document signals to each view

Every view a `KlustersView` creates is wired through one dispatcher, `setConnections(DisplayType, QWidget* view, QDockWidget*)` (decl `klustersview.h`, def `klustersview.cpp`). It makes the one connection common to all views and then dispatches to a per-type helper that takes the already-`qobject_cast` concrete receiver (so a wrong receiver type is a build error, not a silent null-connect):

```cpp
connect(this, &KlustersView::updateContents, view, [view](){ view->update(); });  // klustersview.cpp
if(displayType == CLUSTERS){
    ClusterView* clusterView = qobject_cast<ClusterView*>(view);
    connectSpikeViewCommon(clusterView);
    connectClusterView(clusterView);                                              // klustersview.cpp
} else if(displayType == ERROR_MATRIX){
    connectErrorMatrixView(qobject_cast<ErrorMatrixView*>(view));                 // klustersview.cpp
} ...
```

| Helper | Receiver | Wires from | Def (file) |
|---|---|---|---|
| `connectSpikeViewCommon` | `ViewWidget*` | `this` (KlustersView relay signals) | `klustersview.cpp` |
| `connectClusterView` | `ClusterView*` | `this` **and** `&doc` | `klustersview.cpp` |
| `connectWaveformView` | `WaveformView*` | `this` | `klustersview.cpp` |
| `connectCorrelationView` | `CorrelationView*` | `this` | `klustersview.cpp` |
| `connectErrorMatrixView` | `ErrorMatrixView*` | `&doc` | `klustersview.cpp` |
| `connectTemplateMatrixView` | `TemplateMatrixView*` | `&doc` | `klustersview.cpp` |
| `connectResidualMatrixView` | `ResidualMatrixView*` | `&doc` | `klustersview.cpp` |
| `connectDriftMatrixView` | `DriftMatrixView*` | `&doc` | `klustersview.cpp` |
| `connectTraceWidget` | `TraceWidget*` | `this` (+ `ClusterView::moveToTime`) | `klustersview.cpp` |

**Two delivery mechanisms, confirming [DOCUMENT_MODEL.md](DOCUMENT_MODEL.md) §6 with one refinement:**

- The three spike views (`ClusterView` / `WaveformView` / `CorrelationView`) are driven by `KlustersView`'s own relay signals — `connectSpikeViewCommon` connects `this`'s `singleColorUpdated`, `clusterAddedToView`, `clusterRemovedFromView`, `newClusterAddedToView` (both overloads), `spikesRemovedFromClusters`, `modeToSet`, `spikesAddedToCluster`, `modifiedClusters`, `modifiedClustersUndo`, `updateDrawing`, `changeBackgroundColor` to the `ViewWidget`/`BaseFrame` slots (`klustersview.cpp`). The container relays because the document also calls `KlustersView` methods directly in its `for v : *viewList` loop — `addNewClustersToView` (`klustersview.cpp`) and `renumberClusters` (`klustersview.cpp`).
- The four matrices and `TraceWidget` connect **straight to `&doc`** signals (`clustersGrouped`, `clustersDeleted`, `newClusterAdded`, both `newClustersAdded` overloads, `renumber`, the `undo*`/`redo*` family, `clusterFeaturesReprojected`).

> **Refinement to §6: the split is not clean "method calls vs. signals."** `ClusterView` also takes several **direct `&doc` signal** connects — `clusterFeaturesReprojected` → full REDRAW (`klustersview.cpp`), `dimensionExtremaChanged`, and `renumber`/`undoRenumbering`/`redoRenumbering` → `tsneInvalidate`. And `ErrorMatrixView`, although a `ViewWidget`, is wired by `connectErrorMatrixView` to `&doc`, **not** by `connectSpikeViewCommon`.

Because both `KlustersDoc::newClustersAdded` and each matrix's `newClustersAdded` are **overloaded** (`QMap<int,int>&,QList<int>&` and `QList<int>&`), every such `connect` disambiguates with an explicit member-function-pointer cast (e.g. `klustersview.cpp`, template matrix ) — matching §2.6.

`connectTraceWidget` is a special case: the `TraceWidget` wrapper forwards `updateDrawing` and `changeBackgroundColor` to its inner `TraceView` (`klustersview.cpp`), because the wrapper is a `QWidget`, not a `BaseFrame` — connecting the signal to it as a `BaseFrame` previously produced null-receiver connects.

---

## 5. The matrix-view support classes

The four curation matrices (`ErrorMatrixView`, `TemplateMatrixView`, `ResidualMatrixView`, `DriftMatrixView`) share **no render base** — the error matrix is a `ViewWidget`, the other three are plain `QWidget`s (`matrixgrid.h`). To avoid four diverging copies, the behavior they have in common is factored into header-only helpers that each view holds **by composition** (or calls as free functions):

| Helper | What it is | Decl (file) |
|---|---|---|
| `struct MatrixViewport` | persistent pan/zoom state + the zoom-around-pivot math (`zoomAround`) and per-scope save/restore (`swapForScope`) | `matrixviewport.h` |
| `struct MatrixNavigator` | transient Ctrl+Left-drag pan state machine over a `MatrixViewport` (`begin`/`drag`/`end`) | `matrixnavigator.h` |
| `struct MatrixTemplateStrip` | the marked-node "template column" region: storage, per-cell time-overlap shade, and the click `hitTest`; `computeShade` needs the doc and is defined in `matrixtemplatestrip.cpp` | `matrixtemplatestrip.h` |
| `struct MatrixTemplateCol` + strip renderer | one marked lineage-node template (channel-major `mean`/`std`, `[a,b]` window) and the free function that lays out/paints the strip | `matrixtemplatecols.h` |
| `drawMatrixGrid` / `drawMatrixParentBands` | full-span dashed cell grid; per-parent identity bands + block boundaries for joint scope | `matrixgrid.h` |
| `mbBadgeRect` / `mbDrawComputingBadge` | the small translucent "Computing…" badge drawn over the stale matrix while a recompute runs | `matrixbadge.h` |
| `matrixResolveClick` → `MatrixPairSelection` | pure select-on-release decision (strip cell else cluster pair, Ctrl extends) | `matrixclick.h` |

How the four views share them:

| Helper | ErrorMatrixView | TemplateMatrixView | ResidualMatrixView | DriftMatrixView |
|---|---|---|---|---|
| `MatrixViewport vp_` | `errormatrixview.h` | `templatematrixview.h` | `residualmatrixview.h` | `driftmatrixview.h` |
| `MatrixNavigator nav_` | `errormatrixview.h` | `templatematrixview.h` | `residualmatrixview.h` | `driftmatrixview.h` |
| `MatrixTemplateStrip strip_` | `errormatrixview.h` | `templatematrixview.h` | `residualmatrixview.h` | `driftmatrixview.h` |
| `drawMatrixGrid` | `errormatrixview.cpp` | `templatematrixview.cpp` | `residualmatrixview.cpp` | `driftmatrixview.cpp` |
| `drawMatrixParentBands` | `errormatrixview.cpp` | `templatematrixview.cpp` | `residualmatrixview.cpp` | `driftmatrixview.cpp` |
| `mbDrawComputingBadge` | `errormatrixview.cpp` | `templatematrixview.cpp` | `residualmatrixview.cpp` | `driftmatrixview.cpp` |
| `matrixResolveClick` | — (own richer release) | — (own richer release) | `residualmatrixview.cpp` | `driftmatrixview.cpp` |
| back buffer | inherited `BufferedView::doublebuffer` | own `QPixmap doublebuffer` | own `QPixmap doublebuffer` | own `QPixmap doublebuffer` |

Only the residual and drift matrices use `matrixResolveClick`; the error and template matrices keep their own, richer release (selected-pair accumulation for G-group merge, pair boxes, the error matrix's display-order remap) and so deliberately do not (`matrixclick.h`).

---

## 6. The input seam

A view receives pointer input through one function on the base frame, so that gestures become rebindable commands. The engine itself is documented in [INPUT_SYSTEM.md](INPUT_SYSTEM.md); only the view-side seam is here.

`BaseFrame::dispatchInput(QEvent*)` (decl `baseframe.h`) is the single event→command entry point; it defers to the app-wide resolver:

```cpp
bool BaseFrame::dispatchInput(QEvent* e){ return input::dispatch(this, e); }   // baseframe.cpp
```

`mousePressEvent` funnels through it first and only then runs its hardcoded fall-through (`baseframe.cpp`). `BaseFrame::registerInput` (`baseframe.cpp`) registers the shared rubber-band zoom as a `view.frame`-scoped Gesture command whose scope predicate **excludes any view that manages its own primary press** (`baseframe.cpp`).

`managesOwnPrimaryPress()` (`baseframe.h`, default `false`) is how a view keeps the shared zoom out of its early dispatch so its own Left-button gestures (pan, boundary, lasso, spike-pick) win; such a view still gets the base zoom via the inline fall-through:

| View | `managesOwnPrimaryPress` | Decl (file) |
|---|---|---|
| `BaseFrame` (default) | `false` | `baseframe.h` |
| `ClusterView` | `true` | `clusterview.h` |
| `WaveformView` | `true` | `waveformview.h` |
| `ErrorMatrixView` | `true` | `errormatrixview.h` |
| `TraceView` | `true` | `traceview.h` |
| `CorrelationView` | inherits `false` (routes zoom through the resolver) | — |

---

## 7. Adding a new view — checklist

Grounded in what the base classes actually require:

1. **Pick a base.** A scatter/selection view that shares the cluster-edit vocabulary subclasses `ViewWidget` (`viewwidget.h`); a drawing surface that only needs the buffer subclasses `BufferedView` (`bufferedview.h`); a reader panel that is neither (a matrix, a library view) is a plain `QWidget`.
2. **Decide the paint path.** For a buffered view, implement the hooks — at minimum `paintBuffer(QPainter&, DrawContentsMode)` (`bufferedview.h`), plus `renderReady()` if it can be dataless, `computeViewport()`/`bufferPad()` if it needs legend insets, and `paintWidgetOverlays()`/`afterPaint()` for live overlays — and **do not** override `paintEvent`. Override `setupWorldTransform` only if your content is not drawn in world coordinates (as `ClusterView` does, `clusterview.h`). A non-buffered view implements its own `paintEvent`.
3. **Schedule repaints correctly.** Raise the level with `invalidate(UPDATE|REDRAW)` (`baseframe.h`) — never assign `drawContentsMode` downward — then call `update()`. The skeleton resets to `REFRESH` after painting (`bufferedview.cpp`).
4. **Honor the input seam.** Route pointer handlers through `dispatchInput` (`baseframe.h`); if the view has competing Left-button gestures, override `managesOwnPrimaryPress()` to return `true` (`baseframe.h`) so the shared zoom stays out of its scope.
5. **Add a `DisplayType`** (`klustersview.h`) and a case in the `KlustersView` constructor switch (`klustersview.cpp`) and/or `KlustersView::addView` (`klustersview.cpp`): build a `QDockWidget`, `setWidget(new YourView(...))`, `addDockWidget(area, dock)`, and (for `ViewWidget`s) `viewList.append(currentViewWidget)`.
6. **Register the container with the document** via `KlustersApp::createDisplay` → `doc->addView(view)` (`klusters.cpp`) so it joins `KlustersDoc::viewList`.
7. **Wire it in `setConnections`.** Add a `connectYourView` helper and a dispatch arm (`klustersview.cpp`): a `ViewWidget` that shares the edit vocabulary goes through `connectSpikeViewCommon` + its own helper; a reader/matrix connects directly to `&doc` signals. Always `qobject_cast` the receiver once in the helper, and disambiguate the overloaded `newClustersAdded` with an explicit member-function-pointer cast.
8. **Get repainted.** The common `updateContents → view->update()` connection (`klustersview.cpp`) covers the generic refresh; connect the specific document signals the view must react to inside your helper.

---

## 8. Cross-check against `STANDARDIZATION.md`

<details>
<summary>Audit trail</summary>

| Claim | Verdict | Evidence |
|---|---|---|
| §1 `DockArea` is a shared `libklustersshared` widget | Holds | `DockArea : QScrollArea`, `src/libklustersshared/src/gui/dockarea.h` |
| §2.1 bare members, no `m_` prefix | Holds for the views (`viewList`, `doublebuffer`, `traceWidget` are bare); the newer matrix helper structs use trailing-underscore privates (`vp_`/`nav_`/`strip_`), which is not an `m_` prefix | `klustersview.h`; `errormatrixview.h` |
| §2.5 `addNewClustersToView` is a rename-primitive sibling | Holds — it is a per-view `KlustersView` method | `klustersview.cpp` |
| §2.6 emit `newClustersAdded(QList)` once; views wire both overloads with explicit casts | Holds | disambiguating casts `klustersview.cpp` |
| [DOCUMENT_MODEL.md](DOCUMENT_MODEL.md) §6 "direct method calls vs. Qt signals" split | Holds, refined | spike views via `connectSpikeViewCommon` relay; matrices via `&doc`; but `ClusterView` also takes direct `&doc` connects |

### Cautions for future editors

1. **`ErrorMatrixView` is the exception that breaks the tidy mental model.** It is a `ViewWidget` (`errormatrixview.h`) — so it is in `viewList` and has the thread seam — yet it overrides `paintEvent`, embeds the matrix helpers, and is wired like a matrix (`connectErrorMatrixView`, `klustersview.cpp`), **not** through `connectSpikeViewCommon`.
2. **The `BufferedView` template method is used by only three views today** (cluster, waveform, correlation). `ErrorMatrixView` and `TraceView` override `paintEvent`; the `bufferedview.h` header comment listing five users describes the intended end-state of the paint refactor, not the current tree.
3. **`traceview.h` declares `void paintEvent ( QPaintEvent*ainter);`** — missing `override` and with a garbled parameter name. It still overrides because the signature matches `BufferedView::paintEvent`, but nothing would catch a signature drift. Add `override` when you next touch it.
4. **The three plain-`QWidget` matrices are not in `viewList`** (`QList<ViewWidget*>`, `klustersview.h`); the document reaches them only through Qt signals, never the `for v : *viewList` method-call loop.
5. **Both `newClustersAdded` overloads exist on `KlustersDoc` and on every matrix view** — always disambiguate the member-function pointer in `connect` (`klustersview.cpp`).
6. **`matrixResolveClick` (`matrixclick.h`) is used only by residual + drift.** Error and template keep their own richer release on purpose; do not "unify" them onto it.

</details>

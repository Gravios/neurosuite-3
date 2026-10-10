# The Klusters view system

A developer reference for the on-screen view layer of Klusters — the frame/zoom base chain, the per-document container, and the four curation matrices. Every structural claim below cites `file:line`; files under `src/klusters/src/` are named bare, and the shared `DockArea` base is cited by its path under `src/libklustersshared/`. Anything not read directly out of the code is marked **(unverified)**.

> **Source note.** This reference was written against the base chain (`baseframe.h`/`.cpp`, `bufferedview.h`/`.cpp`, `viewwidget.h`/`.cpp`), the concrete views (`clusterview.h`, `waveformview.h`, `correlationview.h`, `errormatrixview.h`/`.cpp`, `traceview.h`, `tracewidget.h`, `templateview.h`, `templatematrixview.h`/`.cpp`, `residualmatrixview.h`/`.cpp`, `driftmatrixview.h`/`.cpp`, `mergerecommendview.h`), the container (`klustersview.h`/`.cpp`) and its creation site in `klusters.cpp`, the matrix helpers (`matrixviewport.h`, `matrixnavigator.h`, `matrixtemplatestrip.h`/`.cpp`, `matrixtemplatecols.h`, `matrixgrid.h`, `matrixbadge.h`, `matrixclick.h`), and the shared `src/libklustersshared/src/gui/dockarea.h`/`.cpp`. Cross-references to `STANDARDIZATION.md` use its section numbers; the document layer is covered by [DOCUMENT_MODEL.md](DOCUMENT_MODEL.md) and the input engine by [INPUT_SYSTEM.md](INPUT_SYSTEM.md).

---

## 1. The view class hierarchy

Klusters is a document/view application: one `KlustersDoc` (see [DOCUMENT_MODEL.md](DOCUMENT_MODEL.md)) is presented by one or more `KlustersView` containers, each of which docks a set of concrete view widgets. The drawing views descend from a three-layer frame chain rooted in `QFrame`; the newer curation matrices and the template/trace readers are plain `QWidget`s outside that chain.

The frame chain:

```
QFrame → BaseFrame → BufferedView → ViewWidget → { ClusterView, WaveformView, CorrelationView, ErrorMatrixView }
                                  └→ TraceView   (sibling of ViewWidget)
```

| Class | Base class | Decl (`file:line`) | Role |
|---|---|---|---|
| `BaseFrame` | `QFrame` | `baseframe.h:51` | zoom/rubber-band frame, world↔viewport coordinate math, input seam |
| `BufferedView` | `BaseFrame` | `bufferedview.h:39` | off-screen double-buffer paint lifecycle (template method) |
| `ViewWidget` | `BufferedView` | `viewwidget.h:45` | document/view references + the shared cluster-edit slot vocabulary + worker-thread seam |
| `ClusterView` | `ViewWidget` | `clusterview.h:64` | the feature scatter / cluster view (primary) |
| `WaveformView` | `ViewWidget` | `waveformview.h:56` | mean/sample waveforms per cluster |
| `CorrelationView` | `ViewWidget` | `correlationview.h:58` | auto/cross-correlograms |
| `ErrorMatrixView` | `ViewWidget` | `errormatrixview.h:65` | confusion/error probability matrix |
| `TraceView` | `BufferedView` | `traceview.h:54` | raw trace drawing surface (sibling of `ViewWidget`) |

Views that are **plain `QWidget`** — outside the `BaseFrame` chain entirely:

| Class | Base class | Decl (`file:line`) | Role |
|---|---|---|---|
| `TemplateMatrixView` | `QWidget` | `templatematrixview.h:54` | template (xcorr) similarity matrix |
| `ResidualMatrixView` | `QWidget` | `residualmatrixview.h:42` | residual similarity matrix |
| `DriftMatrixView` | `QWidget` | `driftmatrixview.h:54` | drift (cross-corr) matrix |
| `TemplateView` | `QWidget` | `templateview.h:112` | template-library reader; composes `TemplateWavePanel` (`templateview.h:50`) + `PartitionStrip` (`templateview.h:80`) |
| `TraceWidget` | `QWidget` | `tracewidget.h:52` | dock wrapper that **owns the inner** `TraceView view;` (`tracewidget.h:572`) plus the position/scroll controls |
| `MergeRecommendView` | `QWidget` | `mergerecommendview.h:38` | read-only merge-candidate panel in the palette stack |

The container and its base:

| Class | Base class | Decl (`file:line`) |
|---|---|---|
| `KlustersView` | `DockArea` | `klustersview.h:63` |
| `DockArea` | `QScrollArea` | `src/libklustersshared/src/gui/dockarea.h:50` |

> **Being a `ViewWidget` does not mean a view uses the buffered skeleton or the spike-view wiring.** `ErrorMatrixView` sits in the `ViewWidget` chain for its document/view references and thread seam, but it **overrides `paintEvent` directly** (`errormatrixview.h:334`) and is wired like a matrix (§4), not like the other `ViewWidget`s. Conversely, `TraceView` (the `BufferedView` drawing surface) is not docked directly — the dock holds a `TraceWidget` (a `QWidget`) that wraps it (`tracewidget.h:572`; construction at `klustersview.cpp:214`).

---

## 2. What each base layer provides

### 2.1 `BaseFrame` — the zoom/coordinate frame and the input seam

`BaseFrame` owns the mapping between the "world" (a `ZoomWindow window;`, `baseframe.h:291`) and the on-screen `QRect viewport;` (`baseframe.h:286`), the rubber-band zoom, and the repaint-level state.

| Provides | Decl (`file:line`) |
|---|---|
| Mode base + retired `ZOOM` value (`enum {NONE=0, ZOOM=1}`) | `baseframe.h:65` |
| World↔viewport conversions (`viewportToWorld` / `worldToViewport` + width/height variants) | `baseframe.h:178,196,227-260` |
| Repaint-level enum `DrawContentsMode{REFRESH=1, UPDATE=2, REDRAW=3}` | `baseframe.h:277` |
| Monotonic level raise `invalidate(level)` | `baseframe.h:316` |
| Rubber band + first-click state (`mRubberBand`, `firstClick`, `rubber`) | `baseframe.h:344,295,303` |
| `setMode` / `updateDrawing` / `changeBackgroundColor` slots | `baseframe.h:121,113,116` |
| Input seam: `dispatchInput`, `managesOwnPrimaryPress`, `beginBaseZoom`, `static registerInput` | `baseframe.h:156,165,170,108` |

The repaint-level discipline is the one load-bearing detail here. The three modes coalesce, so a lower level must never silently cancel a higher one that another method queued the same turn:

```cpp
enum DrawContentsMode{REFRESH=1, UPDATE=2,REDRAW=3};               // baseframe.h:277
// Raise the pending redraw level, never lower it ...
void invalidate(DrawContentsMode level){ if(level > drawContentsMode) drawContentsMode = level; } // baseframe.h:316
```

### 2.2 `BufferedView` — the off-screen double buffer

`BufferedView` owns the double-buffer lifecycle once, as a template-method `paintEvent`, and exposes hooks the view overrides instead of re-implementing the whole paint:

| Member | Decl (`file:line`) |
|---|---|
| `QPixmap doublebuffer;` (the cached layer) | `bufferedview.h:54` |
| `bool renderReady() const` — gate the whole buffered repaint | `bufferedview.h:58` |
| `bool paintDirect(QPainter&)` — paint wholesale, skip the buffer | `bufferedview.h:61` |
| `void beforeRedraw()` — runs before a REDRAW samples the window | `bufferedview.h:63` |
| `QRect computeViewport()` — device rect the world maps into | `bufferedview.h:66` |
| `int bufferPad()` — extra pixels around the buffer pixmap | `bufferedview.h:69` |
| `void setupWorldTransform(QPainter&)` — install world→device transform | `bufferedview.h:71` |
| `void paintBuffer(QPainter&, DrawContentsMode)` — the cached content | `bufferedview.h:74` |
| `void paintBufferDeviceLayer(QPainter&)` — device-space content baked in | `bufferedview.h:76` |
| `void paintWidgetOverlays(QPainter&)` — live overlays on the widget | `bufferedview.h:78` |
| `void afterPaint(QPainter&)` — end-of-paint hook | `bufferedview.h:80` |
| `void ensureDoubleBuffer()` / `void paintEvent(QPaintEvent*) override` | `bufferedview.h:83,86` |

The skeleton (`bufferedview.cpp:41`) is the one place the modes are consumed:

```cpp
if ((drawContentsMode == UPDATE || drawContentsMode == REDRAW) && renderReady()) {
    ensureDoubleBuffer();
    if (drawContentsMode == REDRAW) beforeRedraw();     // bufferedview.cpp:53-54
    QPainter buf(&doublebuffer);
    setupWorldTransform(buf);
    paintBuffer(buf, drawContentsMode);                 // cached layer
    buf.resetTransform();
    paintBufferDeviceLayer(buf);                        // device-space content baked in
    buf.end();
    drawContentsMode = REFRESH;                         // the one legitimate lowering  // bufferedview.cpp:63
}
widget.drawPixmap(0, 0, doublebuffer);                  // blit
paintWidgetOverlays(widget);                            // live overlays, every paint   // bufferedview.cpp:67
afterPaint(widget);
```

Only three views ride this skeleton (they implement the hooks and declare no `paintEvent`): `ClusterView` (`clusterview.h:481-492`), `WaveformView` (`waveformview.h:327-333`), `CorrelationView` (`correlationview.h:252-263`).

> **The header's five-user claim is intent, not current state.** `bufferedview.h:6-11` says the cluster, waveform, error-matrix, correlation and trace views all drive this skeleton. In the current tree `ErrorMatrixView` and `TraceView` override `paintEvent` directly (`errormatrixview.h:334`, `traceview.h:562`) rather than implementing the hooks. `ErrorMatrixView` still reuses the inherited `BufferedView::doublebuffer` (`bufferedview.h:54`; `errormatrixview.cpp:714,721`); `TraceView` keeps its own buffering behind its own `paintEvent`.

### 2.3 `ViewWidget` — document/view references, the cluster-edit slot vocabulary, and the thread seam

`ViewWidget` adds the document binding and the virtual slots every scatter/selection view shares (`viewwidget.h:71` ctor takes `KlustersDoc& doc, KlustersView& view`). Members: `KlustersView& view` (`viewwidget.h:206`), `KlustersDoc& doc` (`:209`), `QList<int> clusterUpdateList` (`:212`), `QStatusBar* statusBar` (`:215`).

| Virtual slot | Decl (`file:line`) | Virtual slot | Decl (`file:line`) |
|---|---|---|---|
| `updatedDimensions(int,int)` | `viewwidget.h:95` | `spikesAddedToCluster(int,bool)` | `viewwidget.h:149` |
| `singleColorUpdate(int,bool)` | `viewwidget.h:101` | `emptySelection()` | `viewwidget.h:153` |
| `addClusterToView(int,bool)` | `viewwidget.h:109` | `updateClusters(QList<int>&,bool,bool)` | `viewwidget.h:164` |
| `removeClusterFromView(int,bool)` | `viewwidget.h:116` | `undoUpdateClusters(QList<int>&,bool)` | `viewwidget.h:174` |
| `addNewClusterToView(QList<int>&,int,bool)` | `viewwidget.h:125` | `isThreadsRunning() const` | `viewwidget.h:176` |
| `addNewClusterToView(int,bool)` | `viewwidget.h:133` | `stopRunningThreads()` | `viewwidget.h:180` |
| `spikesRemovedFromClusters(QList<int>&,bool)` | `viewwidget.h:141` | `supersedeRunningThreads()` | `viewwidget.h:189` |

The last three are the concurrency seam: a membership edit calls `supersedeRunningThreads()` (non-blocking; in-flight jobs fail their generation guard), while writers of shared mutable data use the blocking `stopRunningThreads()`. See [CONCURRENCY.md](CONCURRENCY.md).

---

## 3. `KlustersView` — the per-document container — and `DockArea`

`KlustersView` is the per-document view container (`klustersview.h:63`). One is created per display in `KlustersApp::createDisplay(DisplayType)` (`klusters.cpp:3348`) with `new KlustersView(...)` (`klusters.cpp:3409,3414`) and registered with the document via `doc->addView(view)` (`klusters.cpp:3200,3423`) — so it joins the document's notify set (`KlustersDoc::viewList`, see [DOCUMENT_MODEL.md](DOCUMENT_MODEL.md) §1).

The kind of display is a `DisplayType` enum (`klustersview.h:74`): `CLUSTERS, WAVEFORMS, CORRELATIONS, OVERVIEW, GROUPING_ASSISTANT_VIEW, ERROR_MATRIX, TRACES, TEMPLATE_MATRIX, RESIDUAL_MATRIX, DRIFT_MATRIX, TEMPLATE_LIBRARY`.

### 3.1 How views are docked and scrolled

`DockArea` is a `QScrollArea` that scrolls an inner `QMainWindow mMainWindow;` (`dockarea.h:151`) — the main window is what actually hosts the `QDockWidget`s, and the surrounding scroll area supplies a scrollbar when too many docks are packed in (`dockarea.h:46-47`):

```cpp
DockArea::DockArea (QWidget * parent) : QScrollArea ( parent ) {   // dockarea.cpp:41
    mMainWindow.setDockOptions(QMainWindow::AllowNestedDocks | ... | QMainWindow::AllowTabbedDocks);
    mMainWindow.setWindowFlags ( Qt::Widget );
    QScrollArea::setWidgetResizable(true);
    QScrollArea::setWidget(&mMainWindow);                           // dockarea.cpp:50
}
void DockArea::addDockWidget ( Qt::DockWidgetArea pArea, QDockWidget * pDockwidget ) {
    pDockwidget->setObjectName ( pDockwidget->windowTitle() );
    ... // register in mDockWidgetByNameMap
    mMainWindow.addDockWidget ( pArea, pDockwidget );              // dockarea.cpp:90 — forwards to the inner main window
}
```

`splitDockWidget` / `tabifyDockWidget` / `resizeDocks` are the same thin forwards to `mMainWindow` (`dockarea.h:76-100`).

In the `KlustersView` constructor each base display builds a `mainDock = new QDockWidget(...)` (`klustersview.cpp:109`), sets its widget to the concrete view, and calls `addDockWidget(Qt::TopDockWidgetArea, mainDock)` (`klustersview.cpp:254`). Composite displays (`OVERVIEW`, `GROUPING_ASSISTANT_VIEW`) stack/tabify their docks via `applyOverviewLayout()` (`klustersview.cpp:260-262`). Additional sub-views are added to an existing container with `KlustersView::addView(DisplayType, ...)` (`klustersview.cpp:1070`, decl `klustersview.h:152`), which likewise builds a `QDockWidget` and calls `addDockWidget` (e.g. `klustersview.cpp:1205,1225,1248,1273,1323`).

Container-owned handles:

| Member | Decl (`file:line`) |
|---|---|
| `QList<ViewWidget*> viewList;` (the per-`ViewWidget` notify/iteration set) | `klustersview.h:1008` |
| `TraceWidget* traceWidget;` | `klustersview.h:1035` |
| `currentViewWidget` (active `ViewWidget`) | `klustersview.cpp:79,126` |
| `QPointer<QDockWidget>` matrix/trace docks for the overview layout | `klustersview.h:1053-1062` |

> **Only `ViewWidget`s live in `viewList`.** The three plain-`QWidget` matrices, `TemplateView` and `MergeRecommendView` are never appended to `viewList` (`klustersview.h:1008`). The document reaches those purely through Qt signals (§4); the `for v : *viewList` method-call loop the curation code uses (see [DOCUMENT_MODEL.md](DOCUMENT_MODEL.md) §6) only ever touches `ViewWidget`s — which is why `ErrorMatrixView`, a `ViewWidget`, *is* in the list while the other three matrices are not.

### 3.2 Matrix zoom sync

When several matrices are docked together, `connectMatrixZoomSync()` (`klustersview.cpp:2274`) wires every ordered pair so panning/zooming one drives the rest, via a one-way `viewChanged → setViewState` link made with `Qt::UniqueConnection` (`linkMatrixZoom`, `klustersview.cpp:2265-2271`). `setViewState` never re-emits `viewChanged`, so the cross-links cannot loop.

---

## 4. `setConnections` — wiring document signals to each view

Every view a `KlustersView` creates is wired through one dispatcher, `setConnections(DisplayType, QWidget* view, QDockWidget*)` (decl `klustersview.h:1166`, def `klustersview.cpp:1982`). It makes the one connection common to all views and then dispatches to a per-type helper that takes the already-`qobject_cast` concrete receiver (so a wrong receiver type is a build error, not a silent null-connect):

```cpp
connect(this, &KlustersView::updateContents, view, [view](){ view->update(); });  // klustersview.cpp:1984
if(displayType == CLUSTERS){
    ClusterView* clusterView = qobject_cast<ClusterView*>(view);
    connectSpikeViewCommon(clusterView);
    connectClusterView(clusterView);                                              // klustersview.cpp:1990-1993
} else if(displayType == ERROR_MATRIX){
    connectErrorMatrixView(qobject_cast<ErrorMatrixView*>(view));                 // klustersview.cpp:2002-2003
} ...
```

| Helper | Receiver | Wires from | Def (`file:line`) |
|---|---|---|---|
| `connectSpikeViewCommon` | `ViewWidget*` | `this` (KlustersView relay signals) | `klustersview.cpp:2016` |
| `connectClusterView` | `ClusterView*` | `this` **and** `&doc` | `klustersview.cpp:2033` |
| `connectWaveformView` | `WaveformView*` | `this` | `klustersview.cpp:2064` |
| `connectCorrelationView` | `CorrelationView*` | `this` | `klustersview.cpp:2084` |
| `connectErrorMatrixView` | `ErrorMatrixView*` | `&doc` | `klustersview.cpp:2098` |
| `connectTemplateMatrixView` | `TemplateMatrixView*` | `&doc` | `klustersview.cpp:2125` |
| `connectResidualMatrixView` | `ResidualMatrixView*` | `&doc` | `klustersview.cpp:2162` |
| `connectDriftMatrixView` | `DriftMatrixView*` | `&doc` | `klustersview.cpp:2194` |
| `connectTraceWidget` | `TraceWidget*` | `this` (+ `ClusterView::moveToTime`) | `klustersview.cpp:2227` |

**Two delivery mechanisms, confirming [DOCUMENT_MODEL.md](DOCUMENT_MODEL.md) §6 with one refinement:**

- The three spike views (`ClusterView` / `WaveformView` / `CorrelationView`) are driven by `KlustersView`'s own relay signals — `connectSpikeViewCommon` connects `this`'s `singleColorUpdated`, `clusterAddedToView`, `clusterRemovedFromView`, `newClusterAddedToView` (both overloads), `spikesRemovedFromClusters`, `modeToSet`, `spikesAddedToCluster`, `modifiedClusters`, `modifiedClustersUndo`, `updateDrawing`, `changeBackgroundColor` to the `ViewWidget`/`BaseFrame` slots (`klustersview.cpp:2018-2029`). The container relays because the document also calls `KlustersView` methods directly in its `for v : *viewList` loop — `addNewClustersToView` (`klustersview.cpp:1520,1557`) and `renumberClusters` (`klustersview.cpp:1903`).
- The four matrices and `TraceWidget` connect **straight to `&doc`** signals (`clustersGrouped`, `clustersDeleted`, `newClusterAdded`, both `newClustersAdded` overloads, `renumber`, the `undo*`/`redo*` family, `clusterFeaturesReprojected`).

> **Refinement to §6: the split is not clean "method calls vs. signals."** `ClusterView` also takes several **direct `&doc` signal** connects — `clusterFeaturesReprojected` → full REDRAW (`klustersview.cpp:2043`), `dimensionExtremaChanged` (`:2049`), and `renumber`/`undoRenumbering`/`redoRenumbering` → `tsneInvalidate` (`:2052-2054`). And `ErrorMatrixView`, although a `ViewWidget`, is wired by `connectErrorMatrixView` to `&doc` (`:2098`), **not** by `connectSpikeViewCommon`.

Because both `KlustersDoc::newClustersAdded` and each matrix's `newClustersAdded` are **overloaded** (`QMap<int,int>&,QList<int>&` and `QList<int>&`), every such `connect` disambiguates with an explicit member-function-pointer cast (e.g. `klustersview.cpp:2107,2120`, template matrix `:2133-2136`) — matching §2.6.

`connectTraceWidget` is a special case: the `TraceWidget` wrapper forwards `updateDrawing` and `changeBackgroundColor` to its inner `TraceView` (`klustersview.cpp:2237-2238`), because the wrapper is a `QWidget`, not a `BaseFrame` — connecting the signal to it as a `BaseFrame` previously produced null-receiver connects.

---

## 5. The matrix-view support classes

The four curation matrices (`ErrorMatrixView`, `TemplateMatrixView`, `ResidualMatrixView`, `DriftMatrixView`) share **no render base** — the error matrix is a `ViewWidget`, the other three are plain `QWidget`s (`matrixgrid.h:28-33`). To avoid four diverging copies, the behavior they have in common is factored into header-only helpers that each view holds **by composition** (or calls as free functions):

| Helper | What it is | Decl (`file:line`) |
|---|---|---|
| `struct MatrixViewport` | persistent pan/zoom state + the zoom-around-pivot math (`zoomAround`) and per-scope save/restore (`swapForScope`) | `matrixviewport.h:29` (`:44`, `:77`) |
| `struct MatrixNavigator` | transient Ctrl+Left-drag pan state machine over a `MatrixViewport` (`begin`/`drag`/`end`) | `matrixnavigator.h:32` (`:42,55,74`) |
| `struct MatrixTemplateStrip` | the marked-node "template column" region: storage, per-cell time-overlap shade, and the click `hitTest`; `computeShade` needs the doc and is defined in `matrixtemplatestrip.cpp` | `matrixtemplatestrip.h:41` |
| `struct MatrixTemplateCol` + strip renderer | one marked lineage-node template (channel-major `mean`/`std`, `[a,b]` window) and the free function that lays out/paints the strip | `matrixtemplatecols.h:43` |
| `drawMatrixGrid` / `drawMatrixParentBands` | full-span dashed cell grid; per-parent identity bands + block boundaries for joint scope | `matrixgrid.h:46` / `matrixgrid.h:103` |
| `mbBadgeRect` / `mbDrawComputingBadge` | the small translucent "Computing…" badge drawn over the stale matrix while a recompute runs | `matrixbadge.h:38` / `matrixbadge.h:57` |
| `matrixResolveClick` → `MatrixPairSelection` | pure select-on-release decision (strip cell else cluster pair, Ctrl extends) | `matrixclick.h:44` (`:31`) |

How the four views share them:

| Helper | ErrorMatrixView | TemplateMatrixView | ResidualMatrixView | DriftMatrixView |
|---|---|---|---|---|
| `MatrixViewport vp_` | `errormatrixview.h:465` | `templatematrixview.h:222` | `residualmatrixview.h:151` | `driftmatrixview.h:179` |
| `MatrixNavigator nav_` | `errormatrixview.h:467` | `templatematrixview.h:224` | `residualmatrixview.h:153` | `driftmatrixview.h:181` |
| `MatrixTemplateStrip strip_` | `errormatrixview.h:430` | `templatematrixview.h:288` | `residualmatrixview.h:221` | `driftmatrixview.h:291` |
| `drawMatrixGrid` | `errormatrixview.cpp:891` | `templatematrixview.cpp:794` | `residualmatrixview.cpp:432` | `driftmatrixview.cpp:658` |
| `drawMatrixParentBands` | `errormatrixview.cpp:907` | `templatematrixview.cpp:810` | `residualmatrixview.cpp:446` | `driftmatrixview.cpp:671` |
| `mbDrawComputingBadge` | `errormatrixview.cpp:731` | `templatematrixview.cpp:628` | `residualmatrixview.cpp:382` | `driftmatrixview.cpp:610` |
| `matrixResolveClick` | — (own richer release) | — (own richer release) | `residualmatrixview.cpp:549` | `driftmatrixview.cpp:891` |
| back buffer | inherited `BufferedView::doublebuffer` | own `QPixmap doublebuffer` (`:338`) | own `QPixmap doublebuffer` (`:208`) | own `QPixmap doublebuffer` (`:276`) |

Only the residual and drift matrices use `matrixResolveClick`; the error and template matrices keep their own, richer release (selected-pair accumulation for G-group merge, pair boxes, the error matrix's display-order remap) and so deliberately do not (`matrixclick.h:9-13`).

---

## 6. The input seam

A view receives pointer input through one function on the base frame, so that gestures become rebindable commands. The engine itself is documented in [INPUT_SYSTEM.md](INPUT_SYSTEM.md); only the view-side seam is here.

`BaseFrame::dispatchInput(QEvent*)` (decl `baseframe.h:156`) is the single event→command entry point; it defers to the app-wide resolver:

```cpp
bool BaseFrame::dispatchInput(QEvent* e){ return input::dispatch(this, e); }   // baseframe.cpp:99-103
```

`mousePressEvent` funnels through it first and only then runs its hardcoded fall-through (`baseframe.cpp:157-170`). `BaseFrame::registerInput` (`baseframe.cpp:121`) registers the shared rubber-band zoom as a `view.frame`-scoped Gesture command whose scope predicate **excludes any view that manages its own primary press** (`baseframe.cpp:126-130`).

`managesOwnPrimaryPress()` (`baseframe.h:165`, default `false`) is how a view keeps the shared zoom out of its early dispatch so its own Left-button gestures (pan, boundary, lasso, spike-pick) win; such a view still gets the base zoom via the inline fall-through:

| View | `managesOwnPrimaryPress` | Decl (`file:line`) |
|---|---|---|
| `BaseFrame` (default) | `false` | `baseframe.h:165` |
| `ClusterView` | `true` | `clusterview.h:504` |
| `WaveformView` | `true` | `waveformview.h:356` |
| `ErrorMatrixView` | `true` | `errormatrixview.h:355` |
| `TraceView` | `true` | `traceview.h:589` |
| `CorrelationView` | inherits `false` (routes zoom through the resolver) | — |

---

## 7. Adding a new view — checklist

Grounded in what the base classes actually require:

1. **Pick a base.** A scatter/selection view that shares the cluster-edit vocabulary subclasses `ViewWidget` (`viewwidget.h:45`); a drawing surface that only needs the buffer subclasses `BufferedView` (`bufferedview.h:39`); a reader panel that is neither (a matrix, a library view) is a plain `QWidget`.
2. **Decide the paint path.** For a buffered view, implement the hooks — at minimum `paintBuffer(QPainter&, DrawContentsMode)` (`bufferedview.h:74`), plus `renderReady()` (`:58`) if it can be dataless, `computeViewport()`/`bufferPad()` (`:66,69`) if it needs legend insets, and `paintWidgetOverlays()`/`afterPaint()` (`:78,80`) for live overlays — and **do not** override `paintEvent`. Override `setupWorldTransform` only if your content is not drawn in world coordinates (as `ClusterView` does, `clusterview.h:485`). A non-buffered view implements its own `paintEvent`.
3. **Schedule repaints correctly.** Raise the level with `invalidate(UPDATE|REDRAW)` (`baseframe.h:316`) — never assign `drawContentsMode` downward — then call `update()`. The skeleton resets to `REFRESH` after painting (`bufferedview.cpp:63`).
4. **Honor the input seam.** Route pointer handlers through `dispatchInput` (`baseframe.h:156`); if the view has competing Left-button gestures, override `managesOwnPrimaryPress()` to return `true` (`baseframe.h:165`) so the shared zoom stays out of its scope.
5. **Add a `DisplayType`** (`klustersview.h:74`) and a case in the `KlustersView` constructor switch (`klustersview.cpp:115`) and/or `KlustersView::addView` (`klustersview.cpp:1070`): build a `QDockWidget`, `setWidget(new YourView(...))`, `addDockWidget(area, dock)`, and (for `ViewWidget`s) `viewList.append(currentViewWidget)`.
6. **Register the container with the document** via `KlustersApp::createDisplay` → `doc->addView(view)` (`klusters.cpp:3348,3423`) so it joins `KlustersDoc::viewList`.
7. **Wire it in `setConnections`.** Add a `connectYourView` helper and a dispatch arm (`klustersview.cpp:1982-2012`): a `ViewWidget` that shares the edit vocabulary goes through `connectSpikeViewCommon` + its own helper; a reader/matrix connects directly to `&doc` signals. Always `qobject_cast` the receiver once in the helper, and disambiguate the overloaded `newClustersAdded` with an explicit member-function-pointer cast.
8. **Get repainted.** The common `updateContents → view->update()` connection (`klustersview.cpp:1984`) covers the generic refresh; connect the specific document signals the view must react to inside your helper.

---

## 8. Cross-check against `STANDARDIZATION.md`

| Claim | Verdict | Evidence |
|---|---|---|
| §1 `DockArea` is a shared `libklustersshared` widget | Holds | `DockArea : QScrollArea`, `src/libklustersshared/src/gui/dockarea.h:50` |
| §2.1 bare members, no `m_` prefix | Holds for the views (`viewList`, `doublebuffer`, `traceWidget` are bare); the newer matrix helper structs use trailing-underscore privates (`vp_`/`nav_`/`strip_`), which is not an `m_` prefix | `klustersview.h:1008,1035`; `errormatrixview.h:465-467` |
| §2.5 `addNewClustersToView` is a rename-primitive sibling | Holds — it is a per-view `KlustersView` method | `klustersview.cpp:1520,1557` |
| §2.6 emit `newClustersAdded(QList)` once; views wire both overloads with explicit casts | Holds | disambiguating casts `klustersview.cpp:2120,2135-2136` |
| [DOCUMENT_MODEL.md](DOCUMENT_MODEL.md) §6 "direct method calls vs. Qt signals" split | Holds, refined | spike views via `connectSpikeViewCommon` relay (`:2016`); matrices via `&doc` (`:2098-2224`); but `ClusterView` also takes direct `&doc` connects (`:2043-2054`) |

### Cautions for future editors

1. **`ErrorMatrixView` is the exception that breaks the tidy mental model.** It is a `ViewWidget` (`errormatrixview.h:65`) — so it is in `viewList` and has the thread seam — yet it overrides `paintEvent` (`:334`), embeds the matrix helpers, and is wired like a matrix (`connectErrorMatrixView`, `klustersview.cpp:2098`), **not** through `connectSpikeViewCommon`.
2. **The `BufferedView` template method is used by only three views today** (cluster, waveform, correlation). `ErrorMatrixView` and `TraceView` override `paintEvent`; the `bufferedview.h:6-11` header comment listing five users describes the intended end-state of the paint refactor, not the current tree.
3. **`traceview.h:562` declares `void paintEvent ( QPaintEvent*ainter);`** — missing `override` and with a garbled parameter name. It still overrides because the signature matches `BufferedView::paintEvent`, but nothing would catch a signature drift. Add `override` when you next touch it.
4. **The three plain-`QWidget` matrices are not in `viewList`** (`QList<ViewWidget*>`, `klustersview.h:1008`); the document reaches them only through Qt signals, never the `for v : *viewList` method-call loop.
5. **Both `newClustersAdded` overloads exist on `KlustersDoc` and on every matrix view** — always disambiguate the member-function pointer in `connect` (`klustersview.cpp:2107,2120,2133-2136`).
6. **`matrixResolveClick` (`matrixclick.h`) is used only by residual + drift.** Error and template keep their own richer release on purpose; do not "unify" them onto it.

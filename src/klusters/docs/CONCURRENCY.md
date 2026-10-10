# Klusters background-job and threading architecture

A developer reference for how Klusters runs curation-adjacent compute off the GUI thread: the shared worker pools, the per-view cancellation token, the snapshot-and-retire worker pattern, the serial realign lane, and the legacy save threads. Every structural claim below cites `file:line`. Paths are under `src/klusters/src/` unless noted (the `CMakeLists.txt` cites are at the app root `src/klusters/` and `src/klusters/src/`). Anything not read directly out of the code is marked **(unverified)**.

> **Source note.** Written against `klustersjobpool.h`/`.cpp`, `klustersjobtoken` (defined in `klustersjobpool.h`), `requestticket.h`, `serialjobqueue.h`, `realignjob.h`, `realignworker.h`/`.cpp`, `autosavethread.h`/`.cpp`, `savethread.h`/`.cpp`, the `*thread.h`/`.cpp` worker family (`waveformthread`, `correlationthread`, `errormatrixthread`, `minmaxthread`, `pairxcorrthread`, `templatematrixthread`, `residualmatrixthread`, `driftmatrixthread`, `driftshiftthread`, `mergerecommendthread`), the view-side stop/supersede methods in `klustersview.cpp`/`.h`, `waveformview.cpp`, `residualmatrixview.cpp`, `errormatrixview.cpp`, the quiesce call sites in `klustersdoc_undo.cpp` and `klustersdoc_realign.cpp`, the pool-drain sites in `klustersdoc.cpp`/`klustersdoc_io.cpp`, `klusters_realign.cpp`, and the OpenMP wiring in `../CMakeLists.txt` and `CMakeLists.txt`. Cross-references to `STANDARDIZATION.md` use its section numbers. See also [DOCUMENT_MODEL.md](DOCUMENT_MODEL.md) (the `Data` epoch/snapshot substrate the workers pin) and [VIEW_SYSTEM.md](VIEW_SYSTEM.md) (the views that own the tokens and receive completion events).

---

## 1. The two worker pools — `KlustersJobPool`

The per-request compute jobs run on **two process-wide `QThreadPool` lanes**, exposed as free functions in `namespace KlustersJobPool` (`klustersjobpool.h:36`). They replaced a thread-per-request design whose live `QThread`s each cost two file descriptors for the event-dispatcher wakeup pipe, so loadable cluster count was bounded by the descriptor budget (`klustersjobpool.h:1-26`). Pool workers run no event loop.

| API | Decl | Def | Role |
|---|---|---|---|
| `QThreadPool* pool()` | `klustersjobpool.h:59` | `klustersjobpool.cpp:7-20` | the bulk lane (Batch + Background) |
| `QThreadPool* interactivePool()` | `klustersjobpool.h:63` | `klustersjobpool.cpp:22-35` | the interactive lane |
| `void start(QRunnable*, Priority)` | `klustersjobpool.h:69` | `klustersjobpool.cpp:37-43` | the one enqueue spelling |
| `void drain()` | `klustersjobpool.h:76` | `klustersjobpool.cpp:45-51` | block until both lanes are empty |

**Both lanes are full-size**, sized to the machine by the same expression:

```cpp
pool->setMaxThreadCount(qMax(4, QThread::idealThreadCount()));   // klustersjobpool.cpp:16  (pool)
pool->setMaxThreadCount(qMax(4, QThread::idealThreadCount()));   // klustersjobpool.cpp:31  (interactivePool)
```

The floor of 4 keeps small machines from serializing every loader behind one or two workers, and also cushions jobs that occupy a worker while parked (`klustersjobpool.cpp:11-15`). Two full-size lanes rather than a static N−2 / 2 split is a deliberate choice: queue priority orders who runs *next* but never preempts a running job, so a lane split that caps the interactive case would starve it when the batch lane is idle; instead each lane alone uses every core, and when they overlap the OS timeslices the ~2× oversubscription — which *is* the yield the priorities could not provide (`klustersjobpool.h:11-20`).

### 1.1 The three priority bands

```cpp
enum Priority {
    BackgroundPriority  = -1,   // klustersjobpool.h:52
    BatchPriority       =  0,   // klustersjobpool.h:53
    InteractivePriority =  1    // klustersjobpool.h:54
};
```

The meaning of the three bands is split across the two lanes by `start()`:

```cpp
void KlustersJobPool::start(QRunnable* job, Priority priority) {
    if (priority == InteractivePriority)
        interactivePool()->start(job);          // klustersjobpool.cpp:40  — own lane, FIFO
    else
        pool()->start(job, priority);            // klustersjobpool.cpp:42  — bulk lane, value is the queue priority
}
```

So **a caller picks a lane by picking a `Priority`**: `InteractivePriority` routes to `interactivePool()` where all jobs are equal FIFO peers; `Batch`/`Background` route to `pool()`, where the enum value is handed straight to `QThreadPool::start` as the queue priority (higher runs first, equal priorities keep FIFO) (`klustersjobpool.h:66-69`, `.cpp:37-43`). Submission is always the single line `KlustersJobPool::start(this, <Priority>)` from the job's launch path.

| Worker | Lane / priority | Site |
|---|---|---|
| `WaveformThread` | Interactive | `waveformthread.cpp:68` |
| `CorrelationThread` | Interactive | `correlationthread.cpp:39` |
| `PairXCorrThread` | Interactive | `pairxcorrthread.cpp:29` |
| `DriftShiftThread` | Interactive (slider step) | `driftshiftthread.cpp:32` |
| `TemplateMatrixThread` | Batch | `templatematrixthread.cpp:269` |
| `ResidualMatrixThread` | Batch | `residualmatrixthread.cpp:30` |
| `DriftMatrixThread` | Batch | `driftmatrixthread.cpp:45` |
| `ErrorMatrixThread` | **Batch or Background** (runtime) | `errormatrixthread.cpp:64-65` |
| `MergeRecommendThread` | Background | `mergerecommendthread.cpp:39` |

> **`ErrorMatrixThread` chooses its lane at runtime.** A display compute is batch work the user sees land; the `seedOnly` cache-warmer is background work nobody is watching, so it enqueues with `seedOnly ? BackgroundPriority : BatchPriority` (`errormatrixthread.cpp:62-65`). This is why a plain grep for `start(this, KlustersJobPool::<Const>)` misses it.

### 1.2 Lifetime and the drain choke point

Both pools are created on first use and never destroyed — a static-destruction-order race between pool workers and other globals is worse than one idle pool at exit (`klustersjobpool.h:22-26`). Because every job holds references into the document's `Data`, `KlustersDoc` **drains the pool before deleting `Data`**:

```cpp
KlustersJobPool::drain();   // klustersdoc.cpp:135  (destructor, immediately before `delete clusteringData`)
KlustersJobPool::drain();   // klustersdoc_io.cpp:155 (document close, before clusteringData is freed)
```

`drain()` is `pool()->waitForDone(); interactivePool()->waitForDone();` (`klustersjobpool.cpp:49-50`). It normally returns at once because the views already superseded their jobs on teardown and `canCloseDocument()` refuses the close while any job is active; it is the backstop for a job orphaned by a view that died just before close (`klustersdoc.cpp:129-135`, `klustersdoc_io.cpp:150-155`).

---

## 2. The cancellation/completion token — `KlustersJobToken`

`struct KlustersJobToken` (`klustersjobpool.h:84-102`) is the state a view shares with every job it enqueues. The view owns it through a `std::shared_ptr` and hands each job a copy, so it outlives whichever side dies first.

| Field | Decl | Role |
|---|---|---|
| `QMutex postMutex` | `klustersjobpool.h:88` | fences completion posts against view destruction |
| `bool viewDead = false` | `klustersjobpool.h:89` | set by the view destructor under `postMutex`; a job posts only while it is false |
| `std::atomic_int generation{0}` | `klustersjobpool.h:95` | the request generation — bumped to supersede every in-flight job at once |
| `std::atomic_int active{0}` | `klustersjobpool.h:101` | count of jobs enqueued and not yet retired |

> **`active` is an atomic count, not a boolean.** A job increments it at enqueue and decrements it as the very last act of `run()`, so `active == 0` is the synchronous-quiesce contract and what `isThreadsRunning()` reports (`klustersjobpool.h:96-101`). The death fence is the pair `postMutex` + `viewDead` (`klustersjobpool.h:88-89`): the generation is the *cancellation* mechanism, `postMutex`/`viewDead` the *post* fence — two separate guards, do not conflate them.

### 2.1 Lifecycle

1. **Enqueue (GUI thread).** The job snapshots the live generation into its own `jobGeneration` and increments `active`:
   ```cpp
   jobGeneration = token->generation.load(std::memory_order_acquire);   // waveformthread.cpp:66
   token->active.fetch_add(1, std::memory_order_acq_rel);               // waveformthread.cpp:67
   ```
2. **Run (pool worker).** The job polls cancellation wherever it used to poll a per-thread stop flag:
   ```cpp
   bool cancelled() const {
       return token->generation.load(std::memory_order_acquire) != jobGeneration;   // waveformthread.h:181
   }
   ```
3. **Supersede (GUI thread).** The view bumps the generation; every in-flight job's `cancelled()` now returns true at its next check:
   ```cpp
   jobToken->generation.fetch_add(1, std::memory_order_acq_rel);        // waveformview.cpp:1388
   ```
4. **Retire (pool worker).** `run()` decrements `active` as its last statement, so `active == 0` means no job of this view is inside a `Data` call anymore:
   ```cpp
   token->active.fetch_sub(1, std::memory_order_acq_rel);               // waveformthread.cpp:77
   ```
5. **View death (GUI thread).** The destructor sets `viewDead` under `postMutex`, then flushes posted events:
   ```cpp
   { QMutexLocker lock(&jobToken->postMutex); jobToken->viewDead = true; }   // waveformview.cpp:131-133
   QApplication::removePostedEvents(this);                                   // waveformview.cpp:138
   ```

A completion **event carries the captured generation** (`generation()` accessor, `waveformthread.h:88`; `correlationthread.h:81`); the view's `customEvent` compares it against the live generation and drops a stale result, so a late result from a superseded request is discarded, not applied. **(unverified: the view-side comparison is documented in the event headers (`waveformthread.h:47-57`, `correlationthread.h:72-73`) but the `customEvent` body was not read for this reference.)**

> **`ErrorMatrixView` carries two tokens.** Display computes and the background cache-warmer run on **separate** tokens (`displayToken`, `warmerToken`), bumped together on supersede (`errormatrixview.cpp:160-161`, `:175-176`), so the view can still tell the two streams apart while superseding both.

---

## 3. The pool-worker pattern

Every pool worker (`WaveformThread`, `CorrelationThread`, the matrix threads, `PairXCorrThread`, `MergeRecommendThread`) is a `QRunnable` with `setAutoDelete(true)` and the same four-beat shape: **snapshot the inputs at enqueue on the GUI thread → process off-thread against the snapshot → post the result back via a queued event → let the token/epoch guards make a late result safe to drop.**

### 3.1 Two flavors of "snapshot"

- **Data-snapshot workers** pin the membership epoch at creation and read `Data` only through it. Each captures `d.currentSnapshot()` in its ctor:
  | Worker | snapshot capture |
  |---|---|
  | `WaveformThread` | `waveformthread.h:157,205` (`snapshot(d.currentSnapshot())`) |
  | `CorrelationThread` | `correlationthread.cpp:31` |
  | `ErrorMatrixThread` | `errormatrixthread.cpp:39` (handed in by the launch site) |
  | `PairXCorrThread` | `pairxcorrthread.cpp:21` |
  | `ResidualMatrixThread` | `residualmatrixthread.cpp:21` |
  | `DriftMatrixThread` | `driftmatrixthread.cpp:36` |
  | `TemplateMatrixThread` | `templatematrixthread.cpp:260` |

  The snapshot pins that epoch's tables, its pinned `.spk` reader, and its per-epoch waveform/correlogram store, so a concurrent edit swapping `Data`'s live tables cannot tear under a running job (`waveformthread.h:201-205`; see [DOCUMENT_MODEL.md](DOCUMENT_MODEL.md) §3). Scalar view fields that are *not* in the snapshot are copied separately on the GUI thread, e.g. `WaveformThread::snapshotViewParams()` captures presentation mode, spike count, and the time window (`waveformthread.h:164-170`), and `CorrelationThread` snapshots bin size / time window (`correlationthread.h:139-140`).

- **Value-copy workers** never touch `Data` at all; the view copies everything they need on the GUI thread. `MergeRecommendThread` takes value copies of the gated pair list and per-cluster templates (`mergerecommendthread.h:38-44,88-107`), and `DriftShiftThread` takes copied mean waveforms and channel depths for a slider step (`driftshiftthread.cpp:16-33`). These carry the token (generation/active/viewDead) but no `ClusteringSnapshot`.

### 3.2 Worked example A — `WaveformThread` (interactive, subscribe-don't-wait)

Enqueue is the last act of each `get*` method: snapshot params, then `enqueue()` (`waveformthread.cpp:31-69`). `run()` is split so retirement happens on every path:

```cpp
void WaveformThread::run(){
    process();
    token->active.fetch_sub(1, std::memory_order_acq_rel);   // waveformthread.cpp:77  — last touch of shared state
}
```

`process()` installs a completion closure on a `RequestTicket` (§5), then sweeps the requested clusters against the **snapshot** — never the live `Data`:

```cpp
auto fetchOnce = [&](int id) -> Data::Status {
    return (mode == Data::SAMPLE)
        ? data.getSampleWaveformPoints(snapshot, id, snapNbSpkToDisplay)       // waveformthread.cpp:118
        : data.getTimeFrameWaveformPoints(snapshot, id, snapStartTime, snapEndTime);
};
```

Where another job already owns an overlapping computation, the sweep **subscribes instead of sleep-polling** (`data.subscribeWaveform(..., ticket, ...)`, `waveformthread.cpp:148`), and `ticket->completeOne()` at the end posts the event (`waveformthread.cpp:205`). Posting is fenced by the token:

```cpp
ticket->post = [tok, viewPtr, gen, ...](bool failed){
    QMutexLocker lock(&tok->postMutex);                 // waveformthread.cpp:98
    if(tok->viewDead) return;                           // waveformthread.cpp:99
    QApplication::postEvent(viewPtr, failed ? (QEvent*)new NoWaveformDataEvent(gen)
                                            : new GetWaveformsEvent(gen, ...));   // waveformthread.cpp:101-104
};
```

### 3.3 Worked example B — `ErrorMatrixThread` (batch, interruptible compute)

The error matrix's interruptible work lives inside `GroupingAssistant`, not the thread, so the cancellation check is routed *into* the assistant through an external-stop predicate set in the ctor:

```cpp
assistant.setExternalStop([this]{ return cancelled(); });   // errormatrixthread.cpp:57
```

The job owns its result arrays and **transfers ownership into the event** as it posts (the job never touches them again); the event frees whatever no handler takes — the mechanism that lets a stale-generation result be dropped without a leak (`errormatrixthread.h:79,108-120`). Posting goes through `post()`, the same `postMutex`/`viewDead` fence:

```cpp
void ErrorMatrixThread::post(QEvent* event){
    QMutexLocker lock(&token->postMutex);
    if(token->viewDead){ delete event; return; }        // errormatrixthread.cpp:74-77
    QApplication::postEvent(&errorMatrixView, event);
}
```

### 3.4 Completion event type codes

Each worker posts a distinct `QEvent::User + N` subclass, letting the view's `customEvent` dispatch by type:

| Event | Code | Decl |
|---|---|---|
| `WaveformThread::GetWaveformsEvent` | `User + 200` | `waveformthread.h:101` |
| `WaveformThread::NoWaveformDataEvent` | `User + 250` | `waveformthread.h:139` |
| `CorrelationThread::CorrelationsEvent` | `User + 300` | `correlationthread.h:85` |
| `ErrorMatrixThread::ErrorMatrixEvent` | `User + 600` | `errormatrixthread.h:108` |
| `MergeRecommendThread::MergeRecommendEvent` | `User + 605` | `mergerecommendthread.h:73` |
| `SaveThread::SaveDoneEvent` | `User + 100` | `savethread.h:95` |
| `AutoSaveThread::AutoSaveEvent` | `User + 500` | `autosavethread.h:73` |

> **`MinMaxThread` is NOT a pool worker.** Despite the `*thread.cpp` name, `MinMaxThread : public QThread` (`minmaxthread.h:35`) is a plain, persistent `QThread` owned by `Data` (`Data::minMaxThread`, `data.h:1352`; constructed once at `data.cpp:65` via the factory `minMaxCalculator()`, `data.cpp:381-382`). Its `run()` is a one-liner, `data.minMaxDimensionCalculation(modifiedClusters)` (`minmaxthread.cpp:22-25`), and it is stopped through `Data`'s own internal booleans ("inform the `MinMaxThread` that an undo or a redo is in process", `data.h:1838`; "cluster 0 has changed", `data.h:1841`) — **outside the pool/token/snapshot model entirely.** Do not assume every `*thread` follows the `QRunnable`+token pattern.

---

## 4. Retiring an in-flight job — supersede vs stop

There are two ways to retire running work, exposed per view-widget as `supersedeRunningThreads()` and the blocking `stopRunningThreadsSync()`, and aggregated at the display by `KlustersView`.

| Primitive | Blocking? | Decl / def | What it does |
|---|---|---|---|
| `KlustersView::supersedeAllViewThreads()` | **no** | `klustersview.h:685`, def `klustersview.cpp:560-579` | bumps every child widget's generation, returns immediately |
| `KlustersView::stopAllViewThreads()` | **yes** | `klustersview.h:676`, def `klustersview.cpp:522` | supersede, then spin until each job retires |

`supersedeAllViewThreads()` just forwards `supersedeRunningThreads()` to every `ViewWidget`, `TemplateMatrixView`, `ResidualMatrixView`, and `DriftMatrixView` child (`klustersview.cpp:564-578`); each forward is a single `generation.fetch_add` plus a `removePostedEvents` (e.g. `waveformview.cpp:1382-1391`). **Supersede is correct precisely because the jobs read their captured snapshot** — a membership-only edit's table swap cannot tear under them, so the only services a blocking wait still owed were stopping doomed work early and fencing stale events, both of which the generation bump provides (`klustersview.h:677-685`, `waveformview.cpp:1382-1391`).

### 4.1 Why undo/redo quiesces before swapping the `Data` epoch

`undo()` supersedes every view's workers **before** reverting the `Data` epoch, so a late worker result can't land against the new epoch:

```cpp
viewList->at(i)->supersedeAllViewThreads();   // klustersdoc_undo.cpp:294   (redo mirror at :458)
...
clusteringData->undo();                        // klustersdoc_undo.cpp:299
```

This is the concurrency half of the undo rule in [DOCUMENT_MODEL.md](DOCUMENT_MODEL.md) §5.2: the doc-layer snapshot stacks are swapped only after the workers reading the outgoing epoch have been told to stop and their pending events discarded. It ties to `STANDARDIZATION.md` §2.5 / §6.2 "read current → write temp → commit, never pre-mutate" — the swap is atomic *and* unobserved by any live job.

### 4.2 Why long computes poll per row (and why stop went non-blocking)

A blocking `stop` is only safe if the compute actually checks for cancellation. `DriftMatrixThread` learned this the hard way:

```cpp
// Both branches poll the cancellation test per ROW.  Neither used to, so
// a stop request set a flag nothing read and the whole O(clusters^2) ran
// to completion regardless -- which is what made the next edit's
// stopAllViewThreads() block the GUI thread in wait() for minutes.   // driftmatrixthread.cpp:210-213
```

> **Contradiction to flag: `stopAllViewThreads()` is now zero-caller.** Several in-code comments still say the blocking quiesce "remains for the `.spk` byte writers (the realign paths)" (`waveformview.cpp:1387`, `klustersview.h:683-684`). In the current tree it has **no callers** — `waveformview.cpp:1366` and `waveformview.h:82` both describe `KlustersView::stopAllViewThreads()` as "zero-caller", and the realign write paths use the **non-blocking** `supersedeAllViewThreads()` (`klustersdoc_realign.cpp:2519`, `:2553`). The SpkOverlay work (byte writers publish their records into the epoch snapshots) removed the need to have the views' jobs *gone* before a write; `stopAllViewThreads()` and its per-widget `…Sync()` twins are retained only as the documented blocking primitive should a future writer of shared mutable state need one (`klustersview.h:669-675`). **Do not resurrect a blocking wait on the strength of the stale comments.**

---

## 5. The result handle — `RequestTicket`

`struct RequestTicket` (`requestticket.h:28-48`) is the "subscribe, don't block" aggregation point. When a job needs a computation another job already owns, it **registers a waiter** with that computation's store and retires, instead of occupying a pool worker in a `sleep(1)` poll (`requestticket.h:1-20`).

```cpp
struct RequestTicket {
    std::atomic_int  remaining{1};   // requestticket.h:30  — 1 for the job's own sweep; +1 per parked waiter
    std::atomic_bool failed{false};  // requestticket.h:34
    std::atomic_bool posted{false};  // requestticket.h:35
    std::function<void(bool failed)> post;   // requestticket.h:39  — installed before any share can complete
    void completeOne() {
        if (remaining.fetch_sub(1, std::memory_order_acq_rel) == 1) {   // requestticket.h:42
            bool expected = false;
            if (posted.compare_exchange_strong(expected, true) && post)
                post(failed.load(std::memory_order_acquire));           // requestticket.h:45
        }
    }
};
```

**How a view uses it.** The view launches a worker; the worker makes a ticket (`std::make_shared<RequestTicket>()`), installs the completion closure that posts the view's event under the token fence, runs its sweep calling `data.subscribe*(…, ticket, …)` for any in-flight computation it overlaps, and finally calls `ticket->completeOne()` for its own share (`waveformthread.cpp:86-205`, `correlationthread.cpp:58-109`). The **last** share to complete — this sweep, or a parked waiter completed from the owning job's terminal on another worker — fires the single completion event. Shared ownership (`shared_ptr`) means the closure can run long after the originating job object is deleted (`requestticket.h:11-14`). Failure semantics are the requester's: a single-cluster waveform request sets `failed` and posts the no-data event; multi-cluster and correlogram requests treat a failed share as a skip (`requestticket.h:16-20`, `waveformthread.cpp:129-205`).

---

## 6. OpenMP is optional — skip, don't break

The CPU matrix kernels use OpenMP where available and fall back to single-threaded otherwise; the build and run both succeed without it.

- **Detection is non-fatal.** `find_package(OpenMP)` with no `REQUIRED`, under the comment "OpenMP — always attempted, silently skipped if unavailable" (`../CMakeLists.txt:22-24`, i.e. `src/klusters/CMakeLists.txt`).
- **Linking is guarded.**
  ```cmake
  if(OpenMP_CXX_FOUND)
      target_link_libraries(klusters PRIVATE OpenMP::OpenMP_CXX)          # CMakeLists.txt:153-154
      message(STATUS "OpenMP: enabled (${OpenMP_CXX_VERSION})")           # CMakeLists.txt:155
  else()
      message(STATUS "OpenMP: not found — single-threaded CPU fallback")  # CMakeLists.txt:157
  endif()
  ```
  (`src/klusters/src/CMakeLists.txt:153-158`.)
- **The headers are guarded and the pragmas degrade silently.**
  ```cpp
  #ifdef _OPENMP
  #include <omp.h>          // templatematrixthread.cpp:13-15
  #endif
  ...
  #pragma omp parallel for schedule(dynamic,1) default(none) \         // templatematrixthread.cpp:382
      shared(...) firstprivate(...)
  ```
  An unknown `#pragma omp` is ignored by a compiler built without OpenMP, so the same loop runs serially — the "skip, don't break" guarantee. The guard pattern recurs across the matrix threads and `groupingassistant.cpp`.

---

## 7. The serial lane and the save threads

### 7.1 `SerialJobQueue` — one job at a time (`serialjobqueue.h`)

`SerialJobQueue : public QObject` (`serialjobqueue.h:93`) is an Active-Object executor: jobs run strictly one at a time, FIFO, the next started only after the current one signals completion (`serialjobqueue.h:61-92`). This makes "only one job touches `Data` at a time" a structural invariant instead of a thing each curation path re-establishes with ad-hoc busy flags.

- A `Job` is `virtual void run(std::function<void()> done)` plus `name()` (`serialjobqueue.h:26-33`); it must call `done()` **exactly once**, synchronously or later, or the queue stalls (`serialjobqueue.h:18-24`). `LambdaJob` adapts a synchronous callable (`serialjobqueue.h:41-58`).
- The queue lives on and is driven from the **GUI thread** (`serialjobqueue.h:71-74`). After a completion the next job starts from the event loop via `QTimer::singleShot(0, …)` so a chain of synchronous jobs can't recurse and the UI repaints between jobs (`scheduleStart`, `serialjobqueue.h:134-139`; `startNext`, `:141-148`; `onJobDone`, `:150-159`).

### 7.2 `RealignWorker` + `RealignJob` — the serialized realign pipeline

`RealignJob : public Job` (`realignjob.h:40`) wraps the asynchronous `RealignWorker` so one realignment is one queue item. Its `run()` creates the worker, moves it to a fresh `QThread`, wires every worker signal to the GUI thread (the app is the `connect` receiver, so delivery is queued), and makes `done()` the **last** statement after the result is applied and the thread torn down:

```cpp
QObject::connect(worker, &RealignWorker::finished, context,
    [ff, thread, done](bool ok, ...){
        if (ff) ff(ok, ...);        // apply the result on the GUI thread
        thread->quit(); thread->wait(2000); thread->deleteLater();
        done();                      // realignjob.h:113  — advance the queue LAST
    });
```

Because `done()` fires last, the curation lane stays occupied for the whole realign, so a renumber/matrix job queued behind it cannot touch `Data` until the realignment has settled (`realignjob.h:28-31`, `serialjobqueue.h:76-91`). `RealignWorker` (`realignworker.h:16`, a `QObject` moved onto a `QThread`) does the work in `run()` (`realignworker.cpp:29-154`): single-cluster or a batch loop over `clusterIds` in one thread (collapsing the per-cluster thread-spawn + GUI round-trip), with a `cancel()` flag checked between clusters for clean shutdown (`realignworker.cpp:24-27,64-65`).

**Why realign is serialized:** realign/nudge rewrite the per-session pending `.spk`/`.res`/`.fet` scratch files in place and must preserve the spike-file invariant `.spk[i]`↔`.res[i]` (`STANDARDIZATION.md` §3.1; [DOCUMENT_MODEL.md](DOCUMENT_MODEL.md) §7.4). The classic failure was a merge → auto-align → renumber → matrix sequence racing through hand-rolled busy flags; as four ordered queue items the race cannot occur by construction (`serialjobqueue.h:76-91`). Wiring: `KlustersApp::enqueueRealignJob` lazily creates `realignQueue = new SerialJobQueue(this)` (`klusters_realign.cpp:218-219`), builds the `RealignJob` with the apply-result closure (`:224-234`), and `realignQueue->enqueue(job)` (`:236`); the post-merge renumber rides behind it as a `LambdaJob` (`klusters.cpp:5379-5380`). `realignQueue` is a member (`klusters.h:1558`). The batch "Align All" path is separate and still uses a dedicated `realignThread`/`realignWorker` (`klusters_realign.cpp:209-210`).

### 7.3 The legacy save threads — plain `QThread`s

Both pre-date the pool and remain simple one-shot `QThread`s that call into `Data`/`KlustersDoc` and post an event back:

- **`SaveThread`** (`savethread.h:38`): `save(url, doc, isSaveAs)` guards against re-entry with `if(isRunning()) return;` then `start()` (`savethread.cpp:27-38`); `run()` calls `doc->saveDocument(url)` and posts a `SaveDoneEvent` (`User + 100`) to the `KlustersApp` parent (`savethread.cpp:40-54`, `savethread.h:68-101`).
- **`AutoSaveThread`** (`autosavethread.h:38`, default 5-minute schedule): `run()` `fopen`s the temp URL, calls `data.saveClusters(cluFile)`, and posts an `AutoSaveEvent` (`User + 500`) to the doc with an `IOerror` flag on failure (`autosavethread.cpp:28-47`).

---

## 8. Contributor rules — invariants a new background job must respect

1. **Snapshot the epoch at enqueue.** If the job reads `Data` membership, capture `d.currentSnapshot()` in the ctor on the GUI thread and read *only* through it (`waveformthread.h:201-205`). If it does not read `Data`, value-copy every input on the GUI thread (`mergerecommendthread.h:88-107`) — never a mix.
2. **Copy scalar view fields too.** Anything the job reads off the view that is not in the snapshot must be snapshotted at enqueue (`WaveformThread::snapshotViewParams`, `waveformthread.h:164-170`); `run()` must not touch live view fields.
3. **Honor the token's generation.** Capture `jobGeneration` at enqueue, poll `cancelled()` (`generation != jobGeneration`) at every loop boundary and before posting, and for an interruptible compute route the check into the kernel via an external-stop predicate (`errormatrixthread.cpp:57`, `driftmatrixthread.cpp:214-216`).
4. **Account for yourself in `active`.** `fetch_add` at enqueue, `fetch_sub` as the **last** statement of `run()` — after every touch of shared `Data` (`waveformthread.cpp:67,77`). The synchronous quiesce and the pool drain depend on this ordering.
5. **Deliver results only by queued event, under the fence.** Post via `QApplication::postEvent` inside `QMutexLocker(&token->postMutex)` with an early return on `viewDead`; the event carries the captured generation so the view can drop a superseded result; if the event owns heap results, free them in its destructor for the dropped case (`waveformthread.cpp:97-105`, `errormatrixthread.cpp:68-79`).
6. **Pick the lane by `Priority`.** Interactive for work the user is watching land, Batch for the O(clusters²) matrix computes, Background for caches/scans nobody is watching (`klustersjobpool.h:38-55`).
7. **Never retire in-flight jobs with a blocking wait on the edit path.** Use `supersedeAllViewThreads()`; the snapshot makes the table swap safe without a wait (`klustersview.cpp:560-579`, `klustersdoc_undo.cpp:294`).
8. **If the job references `Data`, it must be drainable.** The document's `drain()` before `delete Data` is the only lifetime choke point; a job that outlives the drain without being counted in `active` is a use-after-free (`klustersdoc.cpp:129-135`).

---

## 9. Cross-check against `STANDARDIZATION.md`

| Claim | Verdict | Evidence |
|---|---|---|
| §3.4 `probabilities` must init to `nullptr` to avoid a spurious `delete` on garbage at first thread completion | **Holds** | `ErrorMatrixThread` ctor initializes `probabilities(nullptr)` (`errormatrixthread.cpp:40`); event frees it (`errormatrixthread.h:79`) |
| §3.1 realign/nudge preserve the spike-file invariant; `.res.pending` authoritative | **Holds** — and motivates serialization | realign runs one-at-a-time on `SerialJobQueue` writing the pending files ([DOCUMENT_MODEL.md](DOCUMENT_MODEL.md) §7.4; `realignjob.h:28-31`) |
| §2.5 / §6.2 "read current → write temp → commit, never pre-mutate before `prepareUndo`" | **Holds** — the concurrency half is the pre-swap quiesce | `supersedeAllViewThreads()` before `clusteringData->undo()` (`klustersdoc_undo.cpp:294,299`) |
| §2.6 / §6.3 one multi-cluster signal per commit | Out of scope here (a view-notification rule, not a threading rule) | see [DOCUMENT_MODEL.md](DOCUMENT_MODEL.md) §6.1 |

### Cautions for future editors

1. **`MinMaxThread` is a plain `QThread` owned by `Data`, not a pool worker** (`minmaxthread.h:35`, `data.h:1352`, `data.cpp:65,381-382`). It has no token, no `ClusteringSnapshot`, and no `RequestTicket`; it is stopped by `Data`'s internal booleans (`data.h:1838,1841`). The `*thread` suffix does not imply the `QRunnable`+token pattern.
2. **`stopAllViewThreads()` and the per-widget `…Sync()` twins are zero-caller** (`waveformview.cpp:1366`, `waveformview.h:82`). Comments claiming the blocking quiesce "remains for the `.spk` byte writers (the realign paths)" are **stale** — the realign paths use `supersedeAllViewThreads()` (`klustersdoc_realign.cpp:2519,2553`). Keep the blocking primitive documented but do not re-wire it in on the strength of the comments.
3. **Two worker flavors exist.** Data-snapshot workers (waveform/correlation/matrix/pairxcorr) pin `currentSnapshot()`; value-copy workers (`mergerecommend`, `driftshift`) copy everything on the GUI thread and must **not** read `Data`. A new job must be clearly one or the other.
4. **`KlustersJobToken::active` is an atomic count, not a flag**, and the death fence is `postMutex`+`viewDead` (`klustersjobpool.h:88-101`). `active == 0` is the quiesce contract the drain and the views' stop methods rely on.
5. **`ErrorMatrixThread` picks its lane at runtime** (`seedOnly ? Background : Batch`, `errormatrixthread.cpp:64-65`) and its view holds **two** tokens (display + warmer, `errormatrixview.cpp:160-161`). Grep for its priority accordingly.

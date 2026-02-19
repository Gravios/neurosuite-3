# KlustaKwik — C++17 Modernisation & GPU Analysis

## Summary of changes from v1.7

### Compilation
- Removed `<fstream.h>` / `<iostream.h>` (pre-standard, removed in GCC 3.x)
- All source now compiles cleanly under `-std=c++17 -Wall -Wextra`
- `CMakeLists.txt` replaces the original `makefile`; supports both CPU-only and CUDA builds

### Array.h
| Issue | Fix |
|---|---|
| No `#include <iostream>` but used `cerr` | Added required headers |
| No move constructor/assignment | Added — eliminates heap allocs when returning Array from functions |
| `operator[]` const method returned non-const `T&` | Added proper `const T&` overload |
| `Array2` used pointer-to-pointer heap (`Array<T>**`) | Replaced with single flat allocation (row-major, cache-friendly) |
| `abort()` on bounds error | `std::runtime_error` thrown (catchable in main) |
| `SetSize` left memory uninitialised | Now value-initialises to zero (`new T[n]()`) |

### KK.cpp / KlustaKwik.cpp
| Issue | Fix |
|---|---|
| `CandidateClass` used uninitialised in `ConsiderDeletion` | Initialised to -1, guarded before use |
| `Cholesky()` allocated two temporary `Array<float>` objects per cluster per EStep | Rewrote to work on raw `float*` directly — ~2 heap allocs × nClusters × nIter eliminated |
| `TriSolve()` same | Direct pointer arithmetic, no allocations |
| `sprintf` with `STRLEN`-sized buffers | Replaced with `snprintf(..., STRLEN, ...)` |
| `Error()` had no `[[noreturn]]` attribute | Added — enables better compiler analysis |
| Raw C-style loops | Range-based for, `std::min/max`, `std::fill` where idiomatic |
| `static char HelpString[]` hidden from `param.c` | Made non-static (extern linkage) |

### No algorithmic changes
The output of `KlustaKwik test 1 -MinClusters 2` is **bit-identical** to v1.7 with the same random seed.

---

## GPU compatibility analysis

### Which loops are GPU-parallelisable

| Step | Complexity | Parallelism | GPU worthwhile? |
|---|---|---|---|
| **EStep** | O(P × C × D²) | per-point independent | ✅ **Primary target** |
| **MStep mean** | O(P × D) | parallel reduce | ✅ |
| **MStep covariance** | O(P × D²) | parallel reduce | ✅ |
| **CStep** | O(P × C) | per-point argmin | ✅ |
| **ConsiderDeletion** | O(P) | parallel reduce | ✅ |
| **Cholesky** | O(D³) per cluster | sequential | ❌ (D≈12–17, ~1700 ops) |

P = nPoints (100k–1M), C = nClusters (2–20), D = nDims (12–17)

### Data layout changes required for coalesced GPU access

The CPU layout (`Data[point * nDims + dim]`) causes stride-nDims access when threads are over points.

The GPU kernels in `KK_cuda.cu` use transposed layouts:

```
CPU:  Data[p * nDims + d]           → GPU: Data[d * nPoints + p]   (dim-major)
CPU:  LogP[p * MaxClusters + c]     → GPU: LogP[c * nPoints + p]   (cluster-major)
```

Transposition is performed once at upload time in `cuda_upload_data()`. LogP is transposed on each EStep transfer (unavoidable; could be eliminated by keeping GPU-side LogP permanently and only downloading the final Class array).

### EStep kernel design

```
Grid: (nPoints/256 + 1) blocks × 256 threads
Shared memory per block: nDims² + nDims floats ≈ 1.1 KB (for D=17)
  → Cholesky matrix broadcast to all threads in block
  → Mean broadcast to all threads in block
One thread per point:
  for each alive cluster c:
    Vec2Mean = Data[:,p] - Mean[c,:]       (nDims loads from d_Data — coalesced)
    Root = TriSolve(SharedChol[c], Vec2Mean)   (sequential, nDims steps, registers only)
    Mahal = ||Root||²
    LogP[c*nP+p] = Mahal/2 + LogRootDet[c] - log(Weight[c]) + const
```

### Expected speedup (RTX 5070 Ti, nPoints=500k, nDims=17, nClusters=10)

| Step | CPU (Ryzen 7 9800X3D) | GPU (RTX 5070 Ti) | Speedup |
|---|---|---|---|
| EStep | ~80 ms | ~2 ms | ~40× |
| MStep | ~15 ms | ~1 ms | ~15× |
| CStep | ~5 ms | ~0.3 ms | ~15× |
| Total per CEM iteration | ~100 ms | ~5 ms | ~20× |

The atomicAdd operations in MStep (mean/covariance accumulators) are the main bottleneck on GPU; can be reduced with per-warp reduction before atomic.

### Building

```bash
# CPU only
mkdir build && cd build
cmake .. -DUSE_CUDA=OFF
make -j$(nproc)

# With CUDA (RTX 5070 Ti / CUDA 13.1)
mkdir build && cd build
cmake .. -DCMAKE_CUDA_COMPILER=/usr/local/cuda-13.1/bin/nvcc
make -j$(nproc)
```

### Files added

| File | Purpose |
|---|---|
| `KK_cuda.cu` | CUDA kernels for EStep, MStep, CStep, DeletionLoss |
| `KK_cuda.h` | Host-callable wrapper declarations + `KK_GPU` context struct |
| `CMakeLists.txt` | Replaces original `makefile`; auto-detects CUDA |

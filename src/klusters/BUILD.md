# Building klusters

Klusters is built with the rest of neurosuite-3 from the top-level
`CMakeLists.txt`; there is no separate Klusters build.

- Installation, all platforms: [doc/INSTALL.md](../../doc/INSTALL.md)
- Build options, GPU backends (including the `icpx` compiler requirement for
  SYCL) and the skip flags for building Klusters alone:
  [doc/BUILD.md](../../doc/BUILD.md) — §3 *Build options*, §4 *GPU backends*,
  §5 *The build graph and skip flags*

## Disabling GPU backends explicitly

```sh
cmake -B build -DUSE_CUDA=OFF -DUSE_HIP=OFF -DUSE_SYCL=OFF
```

The OpenMP CPU path is always built.

## Backend selection at runtime

The realignment cross-correlation picks its backend when it first runs, in the
order CUDA → HIP → SYCL → OpenMP, among the backends that were built and find
a device. SYCL is opt-in: it is tried only when the environment variable
`KLUSTERS_USE_SYCL` is set (for example `KLUSTERS_USE_SYCL=1 klusters …`),
because probing the oneAPI runtime can abort on some Level Zero drivers. The
chosen backend is printed to standard output as
`[realign] xcorr backend: …`.

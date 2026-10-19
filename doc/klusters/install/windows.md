# Klusters — Windows installation

Klusters is built and installed with the rest of neurosuite-3 in one CMake build;
there is no separate Klusters build. Follow
**[Installing neurosuite-3 → Windows](../../INSTALL.md#windows)**.

After the install, start it from a new terminal:

```bat
klusters
```

To build Klusters without the other programs, add these options to the configure
step:

```
-DNS_SKIP_NEUROSCOPE=ON -DNS_SKIP_NDMANAGER=ON -DNS_SKIP_NDMANAGER_PLUGINS=ON -DNS_SKIP_KILOKLUSTAKWIK=ON
```

## Intel GPU (SYCL)

Build with the `windows-sycl` preset from the Intel oneAPI command prompt
([Installing → Intel GPU](../../INSTALL.md#intel-gpu-sycl)). The grouping
assistant then picks the GPU by itself; realignment's cross-correlation uses
it with `set KLUSTERS_USE_SYCL=1`. The log line
`[klusters] GPU backend: SYCL (Intel oneAPI)` confirms it (redirect the output
to a file: Klusters is a windowed program without a console).

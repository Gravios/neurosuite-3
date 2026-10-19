# Klusters — Linux installation

Klusters is built and installed with the rest of neurosuite-3 in one CMake build;
there is no separate Klusters build. Follow
**[Installing neurosuite-3 → Linux](../../INSTALL.md#linux)**.

After the install, start it from a new terminal:

```bash
klusters &
```

To build Klusters without the other programs, add these options to the configure
step:

```
-DNS_SKIP_NEUROSCOPE=ON -DNS_SKIP_NDMANAGER=ON -DNS_SKIP_NDMANAGER_PLUGINS=ON -DNS_SKIP_KILOKLUSTAKWIK=ON
```

## GPU

CUDA, ROCm and SYCL backends are picked up at configure time when their
toolkits are installed; see [gpu/README.md](../../gpu/README.md).

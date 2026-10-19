# NeuroScope — Windows installation

NeuroScope is built and installed with the rest of neurosuite-3 in one CMake build;
there is no separate NeuroScope build. Follow
**[Installing neurosuite-3 → Windows](../../INSTALL.md#windows)**.

After the install, start it from a new terminal:

```bat
neuroscope
```

To build NeuroScope without the other programs, add these options to the configure
step:

```
-DNS_SKIP_KLUSTERS=ON -DNS_SKIP_NDMANAGER=ON -DNS_SKIP_NDMANAGER_PLUGINS=ON -DNS_SKIP_KILOKLUSTAKWIK=ON
```

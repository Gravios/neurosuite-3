# Klusters — macOS installation

Klusters is built and installed with the rest of neurosuite-3 in one CMake build;
there is no separate Klusters build. Follow
**[Installing neurosuite-3 → macOS](../../INSTALL.md#macos)**.

After the install, start it from a new terminal:

```bash
klusters &
```

To build Klusters without the other programs, add these options to the configure
step:

```
-DNS_SKIP_NEUROSCOPE=ON -DNS_SKIP_NDMANAGER=ON -DNS_SKIP_NDMANAGER_PLUGINS=ON -DNS_SKIP_KILOKLUSTAKWIK=ON
```

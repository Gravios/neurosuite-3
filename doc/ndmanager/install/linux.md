# NDManager — Linux installation

NDManager is built and installed with the rest of neurosuite-3 in one CMake build;
there is no separate NDManager build. Follow
**[Installing neurosuite-3 → Linux](../../INSTALL.md#linux)**.

After the install, start it from a new terminal:

```bash
ndmanager &
```

To build NDManager without the other programs, add these options to the configure
step:

```
-DNS_SKIP_KLUSTERS=ON -DNS_SKIP_NEUROSCOPE=ON -DNS_SKIP_KILOKLUSTAKWIK=ON
```

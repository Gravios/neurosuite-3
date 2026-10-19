# NDManager — Windows installation

NDManager is built and installed with the rest of neurosuite-3 in one CMake build;
there is no separate NDManager build. Follow
**[Installing neurosuite-3 → Windows](../../INSTALL.md#windows)**.

After the install, start it from a new terminal:

```bat
ndmanager
```

To build NDManager without the other programs, add these options to the configure
step:

```
-DNS_SKIP_KLUSTERS=ON -DNS_SKIP_NEUROSCOPE=ON -DNS_SKIP_KILOKLUSTAKWIK=ON
```

## Pipelines on Windows

NDManager edits sessions and their pipelines on Windows. The `ndm_*` steps it
lists run through Git Bash via their `.bat` launchers; KiloKlustaKwik
(`ndm_klustakwik`) is not built on Windows yet, so a pipeline that clusters
runs under WSL2 ([Installing → WSL2](../../INSTALL.md#kiloklustakwik-and-the-full-pipeline-wsl2)).
Opening a saved pipeline file needs a real Python 3 answering to `python3`.

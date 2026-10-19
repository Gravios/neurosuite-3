# SpikeRealign — Installation

SpikeRealign is not a standalone binary. The waveform realignment engine
(`realign_xcorr`) is compiled into **klusters** (for interactive GUI realignment)
and **kiloklustakwik** (for Phase 1.5 batch realignment after chunked CEM sorting).

To get GPU-accelerated realignment, build klusters and/or kiloklustakwik with the
appropriate GPU backend enabled. Follow the corresponding platform guide:

- **klusters:** [../../klusters/install/windows.md](../../klusters/install/windows.md)
- **KiloKlustaKwik:** not part of the Windows build; run it under WSL2 — see [Installing neurosuite-3 → KiloKlustaKwik and the full pipeline: WSL2](../../INSTALL.md#kiloklustakwik-and-the-full-pipeline-wsl2)

The realignment kernel uses the SYCL backend only when the environment
variable `KLUSTERS_USE_SYCL=1` is set; otherwise it runs on the CPU.

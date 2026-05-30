# SpikeRealign — Installation

SpikeRealign is not a standalone binary. The waveform realignment engine
(`realign_xcorr`) is compiled into **klusters** (for interactive GUI realignment)
and **kiloklustakwik** (for Phase 1.5 batch realignment after chunked CEM sorting).

To get GPU-accelerated realignment, build klusters and/or kiloklustakwik with the
appropriate GPU backend enabled. Follow the corresponding platform guide:

- **klusters:** [../../klusters/install/wsl2-sycl.md](../../klusters/install/wsl2-sycl.md)
- **klustakwik:** [../../kiloklustakwik/install/wsl2-sycl.md](../../kiloklustakwik/install/wsl2-sycl.md)

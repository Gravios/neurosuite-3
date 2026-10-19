# neurosuite-3

neurosuite-3 takes a raw extracellular recording to sorted single units:
convert and filter the signal, detect and automatically sort spikes, curate
the result by hand, and inspect everything against the LFP, events and the
animal's position. It is a Qt 6 / C++20 revival of the Neurosuite programs,
built as one project for Linux, Windows and macOS, with optional GPU
acceleration over a CPU path that always works.

| Program | What it does |
|---|---|
| [**NDManager**](ndmanager/README.md) | Edits a session: acquisition parameters, probes, channel groups, and the processing pipeline |
| [**Processing pipeline**](ndmanager-plugins/README.md) | The `ndm_*` steps and `process_*` programs: filtering, spike detection and extraction, features, clustering, drift and collision handling |
| [**KiloKlustaKwik**](kiloklustakwik/README.md) | Automatic spike sorting (chunked CEM, GPU backends) |
| [**Klusters**](klusters/README.md) | Manual cluster curation |
| [**NeuroScope**](handbook/01-introduction.md) | Viewing raw traces, LFP, spikes, events and positions |

## Start here

- **[Install neurosuite-3](INSTALL.md)** — Linux, Windows, macOS.
- **[Sort your first recording](workflows/first-time-sort.md)** — the end-to-end walkthrough.
- **[Workflows](workflows/README.md)** — curation, drift correction, collisions, re-extraction, priors.
- **[File formats](ndmanager-plugins/formats/README.md)** and the **[glossary](GLOSSARY.md)**.

The source, issue tracker and release history are on
[GitHub](https://github.com/Gravios/neurosuite-3); the
[changelog](CHANGELOG.md) lists every change.

# neurosuite-3 documentation

A **prototype** of a modern documentation site for neurosuite-3, built with
[MkDocs](https://www.mkdocs.org/) + [Material](https://squidfunk.github.io/mkdocs-material/).
It unifies, in one searchable site, two bodies of documentation that already
exist in the repository:

- the **User Handbook** — converted from the canonical DocBook source
  (`src/neuroscope/doc/en/index.docbook`) with [pandoc](https://pandoc.org/),
  replacing the retired KDE meinproc / DocBook-XSL toolchain; and
- the **Developer Guide** and its component references — the Markdown that
  already lives at the repository root and under `src/*/docs/`, included here by
  symlink (single source of truth, no duplication).

## Why this exists

The user handbook used to be built from DocBook XML to chunked HTML by a KDE4
toolchain that is no longer available, so the shipped HTML could not be
regenerated. This prototype rebuilds the same content with pandoc — which
installs anywhere — into both a rich web site (this) and a plain,
QTextBrowser-safe HTML page for NeuroScope's in-app **Help ▸ Handbook (F1)**
viewer. See the repository `doc/site/README.md` for how it is generated and how
to evaluate it.

## Start reading

- **[User Handbook ▸ Introduction](handbook/01-introduction.md)**
- **[Developer Guide ▸ Overview](developer/developer-guide.md)**

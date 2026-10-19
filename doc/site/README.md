# doc/site — the documentation site (MkDocs + pandoc)

The project's documentation site, published to GitHub Pages
(<https://gravios.github.io/neurosuite-3/>) by `.github/workflows/docs.yml`.
It is built with **[MkDocs](https://www.mkdocs.org/) +
[Material](https://squidfunk.github.io/mkdocs-material/)** from:

- the repository's Markdown, included **by symlink** with its directory layout
  kept (`docs/klusters -> doc/klusters`, `docs/INSTALL.md -> doc/INSTALL.md`, …),
  so a page's relative links work the same on GitHub and on the site;
  `hooks.py` sends links to files the site does not carry (source, `src/*`
  READMEs) to the file on GitHub;
- the **NeuroScope handbook**, converted from its DocBook source
  (`src/neuroscope/doc/en/index.docbook`) by `gen.sh` with
  [pandoc](https://pandoc.org/); the same run writes the single
  QTextBrowser-safe page NeuroScope's **Help ▸ Handbook (F1)** shows.

## Why

The handbook was built from DocBook XML by a KDE4 toolchain that is no longer
available (see `src/neuroscope/doc/CMakeLists.txt`), so the shipped HTML could
not be regenerated. `pandoc` installs everywhere and reads this DocBook directly,
so it becomes the durable conversion bridge: one source, two rendered outputs.

## Layout

```
doc/site/
  mkdocs.yml            MkDocs + Material config and nav
  requirements.txt      mkdocs-material (install into a venv)
  gen.sh                regenerate both outputs from the DocBook source
  CMakeLists.txt        standalone, opt-in CMake targets (not in the main build)
  tools/                the conversion pipeline (see "How gen.sh works")
  hooks.py              rewrites links that leave the site to GitHub URLs
  docs/
    index.md            site landing page
    handbook/*.md       the NeuroScope handbook, converted — GENERATED, committed
    handbook/Images     -> symlink to src/neuroscope/doc/en/Images
    INSTALL.md, BUILD.md, GLOSSARY.md, CHANGELOG.md, klusters/, neuroscope/,
    ndmanager/, ndmanager-plugins/, kiloklustakwik/, workflows/, gpu/,
    spikerealign/, dev/, design/
                        -> symlinks into doc/ (and the top-level CHANGELOG.md)
    developer/*.md      -> symlinks to src/*/docs/*.md
  build/                generated in-app HTML copy (git-ignored)
```

The developer docs are **symlinked**, not copied: the repo Markdown stays the
single source of truth. The handbook `*.md` are generated but committed so the
site runs without first running `gen.sh`; regenerate them any time with `gen.sh`.

## Use it

Prerequisites: `pandoc`, `xmllint` (libxml2-utils), `iconv`, `python3`, and for
the site, `mkdocs-material`.

```sh
cd doc/site
python3 -m venv .venv && . .venv/bin/activate
pip install -r requirements.txt

./gen.sh            # (re)generate docs/handbook/*.md and build/inapp/index.html
mkdocs serve        # preview the site at http://127.0.0.1:8000
mkdocs build        # render the static site into ./site/
```

### Regenerate via CMake (demonstrates the in-app-HTML build step)

```sh
cmake -S doc/site -B build/docs
cmake --build build/docs --target handbook_inapp_html   # -> build/docs/inapp/index.html (+ Images/)
```

This is the standalone equivalent of the dead meinproc step. To open the result
exactly as the app would: it is read by `QHelpViewer::setHtml()`, which sets the
file's directory as the `QTextBrowser` search path, so `index.html` and its
sibling `Images/` render together.

## How gen.sh works

The canonical DocBook uses KDE DTD entities and declares UTF-8 while actually
being Latin-1, so the pipeline is:

1. **Self-contain + resolve** — prepend `tools/doctype.xml` (defines every entity
   the handbook uses, dropping the external KDE DTD), transcode the body
   Latin-1 → UTF-8, and `xmllint --noent` to expand entities.
2. **Preserve anchors** (`tools/inject_anchors.py`) — inject explicit `<anchor>`
   for DocBook ids on non-heading elements (`<para>`, `<listitem>`,
   `<varlistentry>`, figures), which pandoc would otherwise drop.
3. **Markdown** — `pandoc -f docbook -t markdown`, then `tools/tidy.py`
   (`<menuchoice>` → bold, DocBook `<note>` → Material `!!! note`, injected
   anchors → real `<a id>`), then `tools/split_rewrite.py` (one file per chapter,
   rewriting every intra-doc link to the chapter that declares the target —
   **this step hard-fails on a broken anchor**, so the handbook's own links are
   verified at generation time).
4. **In-app HTML** — `pandoc -f docbook -t html4` through
   `tools/inapp.html.template` (inline CSS, no JS/flex) → one self-contained page.

## Known gaps

- **A few anchors.** About a dozen in-page anchors use heading ids that the
  GitHub-style slugs (`pymdownx.slugs`) generate differently (numbered or
  punctuated headings); `mkdocs build` lists them as INFO.
- **License boilerplate.** The KDE `&underGPL;` / `&underFDL;` / `&FDLNotice;`
  entities aren't shipped in this tree, so `tools/doctype.xml` gives them standard
  short expansions. Swap in the exact text when cutting over.
- **Generated files are committed** (the handbook pages and NeuroScope's
  `handbook.html`) so neither the site preview nor the app build needs pandoc;
  the docs workflow fails when they are out of date with `index.docbook`.

## GitHub integration (two birds, one stone)

The docs are authored once as Markdown (the developer guide and `src/*/docs`
references) plus the DocBook handbook, and that single source feeds both GitHub
surfaces:

- **In-repo** — GitHub renders the Markdown natively in its file browser, so the
  developer docs are already readable on GitHub with no build. `mkdocs.yml` sets
  `repo_url`, so the site header links back to the repository.
- **Published site** — `.github/workflows/docs.yml` runs `gen.sh` (the *same*
  pandoc pipeline that builds NeuroScope's in-app F1 handbook) to convert the
  DocBook, then `mkdocs build --strict`, and deploys to **GitHub Pages** on every
  push to `main` that touches the docs. One conversion pipeline, two outputs:
  the app's built-in help and the public site.

One-time repo setup: **Settings ▸ Pages ▸ Source = "GitHub Actions"**. After
that, the site publishes to `https://gravios.github.io/neurosuite-3/`. (The
per-page "Edit" button is intentionally off — see the note in `mkdocs.yml`.)

## In-app handbook (F1)

NeuroScope's Help ▸ Handbook opens `src/neuroscope/doc/en/handbook.html`, which
`gen.sh` writes from the DocBook source; it is committed (like the site's
handbook pages) so a build needs no pandoc, and the install puts it beside the
screenshots as `index.html`. Regenerate both outputs with `gen.sh` whenever
`index.docbook` changes.

Klusters and NDManager show their Markdown manuals (`doc/klusters/README.md`,
`doc/ndmanager/README.md`), which the viewer renders directly. Their old KDE
DocBook handbooks and the pre-built 2006–2011 chunked HTML are no longer
installed.

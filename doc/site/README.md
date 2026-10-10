# doc/site — modern documentation prototype (MkDocs + pandoc)

A working **prototype** of a modern documentation system for neurosuite-3, built
to evaluate replacing the retired KDE **DocBook → meinproc → chunked HTML**
handbook toolchain. It produces two things from the *canonical* DocBook source
(`src/neuroscope/doc/en/index.docbook`), with no KDE4 dependency:

1. a **[MkDocs](https://www.mkdocs.org/) + [Material](https://squidfunk.github.io/mkdocs-material/)**
   web site that unifies the user handbook with the existing developer docs; and
2. a single, self-contained, **QTextBrowser-safe** `index.html` for NeuroScope's
   in-app **Help ▸ Handbook (F1)** viewer (`QHelpViewer`, which wraps
   `QTextBrowser` — no JavaScript, HTML4 + a CSS 2.1 subset only).

Nothing here is wired into the normal build, and the existing DocBook source and
shipped HTML are untouched — this sits alongside them for evaluation.

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
  docs/
    index.md            site landing page
    handbook/*.md       the handbook, converted (one file per chapter) — GENERATED
    handbook/Images     -> symlink to src/neuroscope/doc/en/Images
    developer/*.md      -> symlinks to DEVELOPER_GUIDE.md and src/*/docs/*.md
  build/                generated in-app HTML (git-ignored)
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

## Known gaps (prototype)

- **Developer-doc links.** The developer Markdown is symlinked in as-is and still
  carries repo-relative links (`ARCHITECTURE.md`, `../../klusters/docs/…`) and
  GitHub-style numbered anchors (`#4-the-scope-stack`) that don't resolve inside
  the site. `mkdocs build` reports these as INFO (link validation is set to
  `info` in `mkdocs.yml`). To close it in production, either add the referenced
  files to the site or rewrite those links (e.g. the
  [monorepo](https://github.com/backstage/mkdocs-monorepo-plugin) plugin, or a
  small link-rewrite pass like the handbook's).
- **License boilerplate.** The KDE `&underGPL;` / `&underFDL;` / `&FDLNotice;`
  entities aren't shipped in this tree, so `tools/doctype.xml` gives them standard
  short expansions. Swap in the exact text when cutting over.
- **Generated files are committed** for a runnable demo; a production setup would
  generate them in CI instead.

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

## Cutting over

1. Point the install in `src/neuroscope/doc/CMakeLists.txt` at the pandoc output
   (`gen.sh` / the `handbook_inapp_html` target) instead of the pre-built
   `en/html/`, and have the app keep opening `index.html`.
2. Enable the Pages workflow above (the one-time Source setting); it already
   builds and deploys on push.
3. Once happy, the DocBook source can either stay as the authoring format (pandoc
   keeps converting it) or be converted to Markdown once and retired.

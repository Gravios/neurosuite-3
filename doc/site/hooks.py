"""MkDocs hook: links that leave the site point to the file on GitHub.

The site includes the repository's Markdown through symlinks (docs/klusters ->
doc/klusters, ...), so a page's relative links are written against its real
place in the repository.  A link whose target is part of the site is left to
MkDocs.  One whose target is another repository file (source code, a README
under src/, CHANGELOG sections, ...) would 404 on the site; it is rewritten to
the same file on GitHub.  A link to a page the site does carry, but under a
different path than the repository's (developer/*.md, CHANGELOG.md), is
rewritten to the page's place in the site.
"""
import os
import posixpath
import re

REPO_URL = "https://github.com/Gravios/neurosuite-3"
BRANCH = "main"
_LINK = re.compile(r'(\]\()([^)\s]+)(\))')
_site_files = {}      # real path -> docs-relative path
_repo_root = None


def on_files(files, config):
    global _repo_root
    _repo_root = os.path.realpath(os.path.join(config["docs_dir"], "..", "..", ".."))
    _site_files.clear()
    for f in files:
        _site_files[os.path.realpath(f.abs_src_path)] = f.src_uri
    return files


def on_page_markdown(markdown, page, config, files):
    real_page = os.path.realpath(page.file.abs_src_path)
    page_dir = os.path.dirname(real_page)

    def fix(m):
        target = m.group(2)
        if re.match(r'^[a-z][a-z0-9+.-]*:', target, re.I) or target.startswith(("#", "/")):
            return m.group(0)
        path, _, frag = target.partition("#")
        if not path:
            return m.group(0)
        real = os.path.realpath(os.path.join(page_dir, path))
        if real in _site_files:
            here = posixpath.dirname(page.file.src_uri)
            rel = posixpath.relpath(_site_files[real], here or ".")
            return m.group(1) + rel + (f"#{frag}" if frag else "") + m.group(3)
        if not real.startswith(_repo_root + os.sep):
            return m.group(0)
        if not os.path.exists(real):
            return m.group(0)
        rel = posixpath.join(*os.path.relpath(real, _repo_root).split(os.sep))
        kind = "tree" if os.path.isdir(real) else "blob"
        url = f"{REPO_URL}/{kind}/{BRANCH}/{rel}" + (f"#{frag}" if frag else "")
        return m.group(1) + url + m.group(3)

    return _LINK.sub(fix, markdown)

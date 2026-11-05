#!/usr/bin/env python3
"""Every ndm command gets a man page.

  - each ndm* command scripts/CMakeLists.txt installs has a .docbook page or is
    found in the command reference (doc/ndmanager-plugins/commands) by md2man.py;
    a name that is not documented is refused;
  - every generated page is valid roff: .TH with the name, source and manual, a
    NAME line, plain ASCII (other characters as \\[uXXXX]), balanced .nf/.fi and
    .RS/.RE, no Markdown left over (**, ](, ```); with groff installed it renders
    without warnings (line-breaking ones aside);
  - inline Markdown: code, bold around code, links with code, web links, italics;
  - with xsltproc and the DocBook XSL (namespaced) stylesheet installed, a DocBook
    page built the way NdmManPage.cmake builds it has no FIXME in its header.
Exits nonzero on failure (repo test convention).
"""
import glob
import os
import re
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
PLUG = os.path.abspath(os.path.join(HERE, ".."))
ROOT = os.path.abspath(os.path.join(PLUG, "..", ".."))
DOC = os.path.join(ROOT, "doc", "ndmanager-plugins", "commands")
SCRIPTS = os.path.join(PLUG, "scripts")
sys.path.insert(0, os.path.join(PLUG, "cmake"))
import md2man  # noqa: E402

fails = 0


def check(name, cond, detail=""):
    global fails
    print(f"  [{'PASS' if cond else 'FAIL'}] {name}{': ' + detail if detail and not cond else ''}")
    if not cond:
        fails += 1


cm = open(os.path.join(SCRIPTS, "CMakeLists.txt")).read()
block = re.search(r"set\(_ndm_scripts(.*?)\n\)", cm, re.S).group(1)
names = sorted({w for w in re.sub(r"#.*", "", block).split() if re.match(r"^ndm(_|$)", w)})
check("the install list has the ndm commands", len(names) > 40, str(len(names)))

undocumented = [n for n in names if not os.path.exists(os.path.join(SCRIPTS, n + ".docbook"))
                and md2man.find_page(DOC, n) is None]
check("every ndm command has a .docbook page or a command-reference entry", not undocumented,
      " ".join(undocumented))
check("an undocumented name is refused", md2man.find_page(DOC, "ndm_nosuchstep") is None)

# inline
cases = {
    "`a-b`": r"\fBa\-b\fR",
    "**`x` text**": r"\fBx text\fR",
    "see [`.res` format](../formats/res.md)": r"see \fB.res\fR format",
    "[site](https://x.org/a)": "site <https://x.org/a>",
    "a *b* ndm_x _c_": r"a \fIb\fR ndm_x \fIc\fR",
    "4σ → [x → y](z.md)": r"4\[u03C3] \[u2192] x \[u2192] y",
    "back\\slash": r"back\eslash",
}
for src, want in cases.items():
    got = md2man.inline(src)
    check(f"inline {src!r}", got == want, f"{got!r} != {want!r}")

groff = shutil.which("groff")
with tempfile.TemporaryDirectory() as d:
    bad = []
    for n in names:
        if os.path.exists(os.path.join(SCRIPTS, n + ".docbook")):
            continue
        text = md2man.man_page(DOC, n, "1", "neurosuite-3 9.9", "NDManager Plugins", "2026-01-01")
        probs = []
        th = re.search(r'^\.TH "([^"]*)" "1" "2026-01-01" "neurosuite-3 9.9" "NDManager Plugins"$', text, re.M)
        if not th or th.group(1) != n.upper():
            probs.append("TH")
        if not re.search(r'^\.SH "NAME"\n' + re.escape(n.replace("-", "\\-")) + r" \\- \S", text, re.M):
            probs.append("NAME")
        if any(ord(c) > 127 for c in text):
            probs.append("non-ASCII")
        if text.count("\n.nf") != text.count("\n.fi") or text.count("\n.RS") != text.count("\n.RE"):
            probs.append("unbalanced")
        body = re.sub(r"\n\.nf\n.*?\n\.fi", "", text, flags=re.S)       # code blocks may hold anything
        if re.search(r"\*\*|(?<!\\\[u[0-9A-F]{4})\]\(|```", body):     # "\[u2212](" is a minus sign
            probs.append("markdown left")
        if groff:
            p = os.path.join(d, n + ".1")
            open(p, "w").write(text)
            r = subprocess.run([groff, "-man", "-Tutf8", "-ww", "-z", p], capture_output=True, text=True)
            warn = [w for w in r.stderr.splitlines() if "cannot break" not in w and "cannot adjust" not in w]
            if warn:
                probs.append("groff: " + warn[0])
        if probs:
            bad.append(f"{n} ({', '.join(probs)})")
    check("every generated page is clean roff" + ("" if groff else " (groff not installed: not rendered)"),
          not bad, "; ".join(bad[:5]))

    xsl = sorted(glob.glob("/usr/share/xml/docbook/stylesheet/docbook-xsl-ns/manpages/docbook.xsl")
                 + glob.glob("/usr/share/**/docbook-xsl-ns*/manpages/docbook.xsl", recursive=True))
    if shutil.which("xsltproc") and xsl:
        meta = os.path.join(d, "meta.xml")
        open(meta, "w").write("<m><source>neurosuite-3</source><version>9.9</version>"
                              "<manual>NDManager Plugins</manual></m>\n")
        prof = lambda what: ["--param", f"refentry.{what}.profile.enabled", "1", "--stringparam",  # noqa: E731
                             f"refentry.{what}.profile", f"document('{meta}')/m/" + what.split(".")[0]]
        r = subprocess.run(["xsltproc", "--nonet", "--novalid", *prof("source.name"), *prof("version"),
                            *prof("manual"), xsl[0], os.path.join(SCRIPTS, "ndm.docbook")],
                           cwd=d, capture_output=True, text=True)
        th = next((ln for ln in open(os.path.join(d, "ndm.1")) if ln.startswith(".TH")), "") \
            if os.path.exists(os.path.join(d, "ndm.1")) else ""
        check("a DocBook page gets the header and footer (no FIXME)",
              '"neurosuite-3 9.9" "NDManager Plugins"' in th and "FIXME" not in th, th.strip() or r.stderr[-200:])
    else:
        print("  [SKIP] DocBook pages: xsltproc or docbook-xsl-ns not installed")

print("OK (0 failures)" if not fails else f"FAILED ({fails})")
sys.exit(1 if fails else 0)

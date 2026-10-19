#!/usr/bin/env python3
"""The ndmanager Pipeline designer can place every ndm_* plugin, with the
template's parameters.

  - pipelinescriptdefs.inc and parametercatalog.inc are what
    src/ndmanager/tools/pipeline_script_defs.py generates from
    templates/template.yaml (so they are up to date);
  - the parameter catalog (what Add offers on a plugin's page) lists every
    parameter of the template, commented-out ones included, and each of its
    entries is one the plugin's script reads;
  - every plugin with a template block, and every script that reads
    parameters, is in the palette or listed in NOT_IN_PALETTE with a reason
    (a plugin missing from the palette is dropped when a pipeline that uses
    it is loaded);
  - every node parameter is one the plugin's script reads (external plugins
    excepted), and none is one the script only reads with a fallback block,
    which a node value would override;
  - the palette shows every category (it groups by the generated list's
    categories, with no fixed list that could leave one out), every category
    has a style, and every preset names a palette plugin.
Exits nonzero on failure (repo test convention).
"""
import importlib.util
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
GEN = os.path.join(ROOT, "src", "ndmanager", "tools", "pipeline_script_defs.py")
CPP = os.path.join(ROOT, "src", "ndmanager", "src", "pipelinedesignerpage.cpp")


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    mod = importlib.util.module_from_spec(spec)
    sys.modules[name] = mod
    spec.loader.exec_module(mod)
    return mod


gen = load("pipeline_script_defs", GEN)
tp = load("test_template_parameters", os.path.join(HERE, "test_template_parameters.py"))

fails = 0


def check(name, cond, detail=""):
    global fails
    print(f"  [{'PASS' if cond else 'FAIL'}] {name}{'' if cond or not detail else ': ' + detail}")
    if not cond:
        fails += 1


r = subprocess.run([sys.executable, GEN, "--check"], capture_output=True, text=True)
check("pipelinescriptdefs.inc and parametercatalog.inc are up to date", r.returncode == 0, r.stdout.strip())

palette = {m[0]: m for m in gen.META}
aliases = {m[0] for m in gen.META if m[6]}
active, commented = tp.template_parameters()
code = tp.script_parameters()

missing = sorted(p for p in active if p not in palette and p not in gen.NOT_IN_PALETTE)
check("every template program is placeable", not missing, ", ".join(missing))
readers = sorted(b for b, ps in code.items()
                 if any(e["own"] for e in ps.values()) and b not in palette and b not in gen.NOT_IN_PALETTE)
check("every script that reads parameters is placeable", not readers, ", ".join(readers))

inc = open(os.path.join(ROOT, "src", "ndmanager", "src", "pipelinescriptdefs.inc"), encoding="utf-8").read()
nodes = {}
for m in re.finditer(r'^\{ "(ndm_[\w-]+)",.*?(?=^\{ "ndm_|\Z)', inc, re.S | re.M):
    nodes[m.group(1)] = re.findall(r'\{ "([\w.]+)", "[^"]*", "\w+" \}', m.group(0))
check("every palette plugin is in the .inc", set(nodes) == set(palette))

bad, overriding = [], []
for typ, params in nodes.items():
    if typ in tp.EXTERNAL or typ == "ndm_start":
        continue
    known = code.get(typ, {})
    for p in params:
        if p not in known:
            bad.append(f"{typ}.{p}")
        elif not known[p]["own"] and typ not in ("ndm_subcluster_unmatched",):
            overriding.append(f"{typ}.{p}")
check("node parameters are read by the plugin", not bad, ", ".join(bad))
check("no node parameter overrides a fallback block", not overriding, ", ".join(overriding))
check("aliases carry no parameters", all(not nodes[a] for a in aliases))

catalog = open(os.path.join(ROOT, "src", "ndmanager", "src", "parametercatalog.inc"), encoding="utf-8").read()
listed = set(re.findall(r'^\{ "(ndm_[\w-]+)", "([\w.]+)", ', catalog, re.M))
expected = {(b, p) for b, ps in active.items() for p in ps} | \
           {(b, p) for b, ps in commented.items() for p in ps if b.startswith("ndm_")}
check("the catalog lists every template parameter", expected <= listed,
      ", ".join(sorted(f"{b}.{p}" for b, p in expected - listed)))
unread = sorted(f"{b}.{p}" for b, p in listed
                if b not in tp.EXTERNAL and p not in code.get(b, {}))
check("every catalog parameter is read by its plugin", not unread, ", ".join(unread))

cpp = open(CPP, encoding="utf-8").read()
palette_src = cpp[cpp.index("void PipelineDesignerPage::buildPalette()"):]
palette_src = palette_src[:palette_src.index("\n}\n")]
check("every category is shown in the palette",
      "plugincategory::order(ndmCategoryOrder()" in palette_src and "catOrder" not in palette_src)
styled = set(re.findall(r'\{\s*"(\w+)",\s*\{\s*"', cpp[cpp.index("categoryStyles()"):]))
check("every category has a style", {m[3] for m in gen.META} <= styled)
presets = re.findall(r"QStringList\{([^}]*)\}", cpp)
unknown = sorted({t for p in presets for t in re.findall(r'"(ndm_[\w-]+)"', p)} - set(palette))
check("presets name palette plugins", not unknown, ", ".join(unknown))
check("the designer includes the generated list", '#include "pipelinescriptdefs.inc"' in cpp)

print("FAILED" if fails else "OK", f"({fails} failure{'s' * (fails != 1)})")
sys.exit(1 if fails else 0)

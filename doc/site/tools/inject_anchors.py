#!/usr/bin/env python3
"""Inject explicit <anchor> elements for DocBook ids that pandoc would otherwise
drop (ids on <para>, <listitem>, <varlistentry>, figures, ...). Headings keep
their id natively, so they are skipped to avoid duplicate anchors.

Usage: inject_anchors.py <resolved.xml> <anchored.xml>
"""
import sys
import xml.etree.ElementTree as ET

HEADING = {"book", "article", "chapter", "appendix", "preface", "part",
           "sect1", "sect2", "sect3", "sect4", "sect5", "section", "refentry"}

tree = ET.parse(sys.argv[1])
root = tree.getroot()
n = 0
for el in list(root.iter()):
    ident = el.get("id")
    if not ident or el.tag in HEADING:
        continue
    target = el
    for child in el:                     # drop the anchor into an inline-bearing child
        if child.tag in ("term", "para", "simpara"):
            target = child
            break
    a = ET.Element("anchor")
    a.set("id", ident)
    target.insert(0, a)
    n += 1
tree.write(sys.argv[2], encoding="unicode")
print(f"inject_anchors: {n} anchors")

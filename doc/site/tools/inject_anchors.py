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

# Screenshots: pandoc renders a <mediaobject> as an image only at block level,
# and drops a <screenshot> nested in a <para> (all the handbook's figures are
# written that way), leaving just the caption.  Each screenshot becomes a bare
# <mediaobject> holding the PNG (the EPS alternative was for print).  When it is
# the only content of a <para>, the para is replaced by an anchor paragraph
# (keeping the figure's id) followed by the image, at block level.
parent = {c: p for p in root.iter() for c in p}
lifted = 0
for shot in list(root.iter("screenshot")):
    media = shot.find("mediaobject")
    holder = parent.get(shot)
    if media is None or holder is None:
        continue
    for io in list(media.findall("imageobject")):
        data = io.find("imagedata")
        if data is not None and data.get("format", "").upper() == "EPS":
            media.remove(io)
    para = holder
    alone = (para.tag == "para" and len(para) == 1 and not (para.text or "").strip()
             and not (shot.tail or "").strip())
    if alone and para in parent:
        grand = parent[para]
        idx = list(grand).index(para)
        grand.remove(para)
        new = [media]
        if para.get("id"):
            ap = ET.Element("para")
            ap.append(ET.Element("anchor", {"id": para.get("id")}))
            new.insert(0, ap)
        for k, el in enumerate(new):
            grand.insert(idx + k, el)
    else:
        idx = list(holder).index(shot)
        holder.remove(shot)
        media.tail = shot.tail
        holder.insert(idx, media)
    lifted += 1
print(f"inject_anchors: {lifted} screenshots lifted to block level")

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

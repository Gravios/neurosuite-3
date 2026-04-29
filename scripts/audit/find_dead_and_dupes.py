#!/usr/bin/env python3
"""
AST-based dead-code and duplicate-code finder for the Klusters codebase.

Outputs a report; never modifies source.

Limitations: see report header.
"""

import sys, os, re, hashlib
from collections import defaultdict
from tree_sitter_languages import get_parser

ROOT = '/tmp/merged-klusters/src'
TARGETS = []
for d, dirs, files in os.walk(ROOT):
    if '/build' in d or '/.git' in d or '/external' in d:
        continue
    for fn in files:
        if fn.endswith(('.cpp', '.h')):
            TARGETS.append(os.path.join(d, fn))
TARGETS.sort()
print(f'# Scanning {len(TARGETS)} source files...', file=sys.stderr)

parser = get_parser('cpp')

def text(node, src):
    return src[node.start_byte:node.end_byte].decode('utf-8', errors='replace')

def find_declarator_name(node, src):
    if node.type in ('identifier', 'field_identifier'):
        return text(node, src)
    if node.type == 'qualified_identifier':
        for c in reversed(list(node.children)):
            if c.type in ('identifier', 'field_identifier'):
                return text(c, src)
            if c.type == 'qualified_identifier':
                return find_declarator_name(c, src)
    if node.type in ('destructor_name', 'operator_name'):
        return text(node, src)
    for c in node.children:
        n = find_declarator_name(c, src)
        if n: return n
    return None

def make_fingerprint(node):
    """Return (sha256-prefix, num_AST_nodes).  Erases identifiers/literals."""
    if node is None:
        return ('', 0)
    parts = []
    stack = [node]
    while stack:
        n = stack.pop()
        t = n.type
        if t == 'comment':
            continue
        if t in ('identifier', 'field_identifier', 'type_identifier'):
            parts.append('<ID>')
        elif t == 'number_literal':
            parts.append('<N>')
        elif t in ('string_literal', 'char_literal'):
            parts.append('<S>')
        else:
            parts.append(t)
        # Push children in reverse so traversal order is stable
        for c in reversed(list(n.children)):
            stack.append(c)
    h = hashlib.sha256(' '.join(parts).encode()).hexdigest()[:16]
    return (h, len(parts))


# Collected data
fn_definitions  = defaultdict(list)   # name -> [(rel, line, fp, sz, is_virt, class_stack)]
fn_declarations = defaultdict(list)   # name -> [(rel, line, decl_txt, is_virt, in_signal, class_stack)]
identifier_uses = defaultdict(int)
qt_signal_names = set()


def visit(node, src_bytes, rel, in_signal_range, class_stack=()):
    if node.type in ('class_specifier', 'struct_specifier'):
        name = None
        for c in node.children:
            if c.type == 'type_identifier':
                name = text(c, src_bytes); break
        new_stack = class_stack + (name,) if name else class_stack
        for c in node.children:
            visit(c, src_bytes, rel, in_signal_range, new_stack)
        return

    if node.type == 'function_definition':
        decl = None
        for c in node.children:
            if c.type == 'function_declarator':
                decl = c; break
            if c.type == 'pointer_declarator':
                for cc in c.children:
                    if cc.type == 'function_declarator':
                        decl = cc; break
        if decl:
            name = find_declarator_name(decl, src_bytes)
            if name:
                body_node = None
                for c in node.children:
                    if c.type == 'compound_statement':
                        body_node = c; break
                fp, sz = make_fingerprint(body_node)
                line_no = node.start_point[0] + 1
                decl_txt = text(node, src_bytes)
                is_virt = bool(re.search(r'\b(virtual|override)\b', decl_txt[:400]))
                fn_definitions[name].append((rel, line_no, fp, sz, is_virt, class_stack))

    if node.type in ('declaration', 'field_declaration'):
        decl = None
        for c in node.children:
            if c.type == 'function_declarator':
                decl = c; break
        if decl:
            name = find_declarator_name(decl, src_bytes)
            if name:
                line_no = node.start_point[0] + 1
                decl_txt = text(node, src_bytes)
                is_virt = bool(re.search(r'\b(virtual|override)\b', decl_txt))
                in_sig = in_signal_range(node.start_byte)
                if in_sig:
                    qt_signal_names.add(name)
                fn_declarations[name].append((rel, line_no, decl_txt, is_virt, in_sig, class_stack))

    if node.type in ('identifier', 'field_identifier'):
        identifier_uses[text(node, src_bytes)] += 1

    for c in node.children:
        visit(c, src_bytes, rel, in_signal_range, class_stack)


for path in TARGETS:
    rel = os.path.relpath(path, ROOT)
    src_bytes = open(path, 'rb').read()
    src_text  = src_bytes.decode('utf-8', errors='replace')
    tree = parser.parse(src_bytes)

    signal_ranges = []
    for m in re.finditer(r'\b(Q_SIGNALS|signals)\s*:', src_text):
        start = m.end()
        # Convert byte offset (text offset is char-offset == byte-offset for ASCII; mostly OK)
        next_marker = re.search(r'\b(public|private|protected|Q_SIGNALS|signals)\s*(slots\s*)?:', src_text[start:])
        end = start + next_marker.start() if next_marker else len(src_text)
        signal_ranges.append((start, end))

    def in_signal_range(byte_pos, ranges=signal_ranges):
        return any(s <= byte_pos < e for s, e in ranges)

    visit(tree.root_node, src_bytes, rel, in_signal_range, ())


print(f'# Found {len(fn_definitions)} distinct fn-names with definitions', file=sys.stderr)
print(f'# Found {len(fn_declarations)} distinct fn-names with declarations', file=sys.stderr)
print(f'# Found {len(qt_signal_names)} Qt signals', file=sys.stderr)


# ---------------------------------------------------------------------------
# Analysis 1: dead candidates
# ---------------------------------------------------------------------------

def call_count(name):
    free_mentions = len(fn_declarations.get(name, [])) + len(fn_definitions.get(name, []))
    return identifier_uses.get(name, 0) - free_mentions


SPECIAL = {
    'main', 'qt_metacall', 'qt_static_metacall', 'metaObject',
    'tr', 'trUtf8', 'qt_check_for_QGADGET_macro', 'staticMetaObject',
    'qt_metacast', 'event', 'eventFilter', 'paintEvent', 'mousePressEvent',
    'mouseReleaseEvent', 'mouseMoveEvent', 'mouseDoubleClickEvent',
    'keyPressEvent', 'keyReleaseEvent', 'resizeEvent', 'closeEvent',
    'showEvent', 'hideEvent', 'focusInEvent', 'focusOutEvent',
    'enterEvent', 'leaveEvent', 'wheelEvent', 'dragEnterEvent',
    'dragLeaveEvent', 'dragMoveEvent', 'dropEvent', 'customEvent',
    'changeEvent', 'tabletEvent', 'contextMenuEvent', 'inputMethodEvent',
    'timerEvent', 'sizeHint', 'minimumSizeHint',
    # Standard library hooks called by templates
    'begin', 'end', 'cbegin', 'cend', 'rbegin', 'rend', 'size', 'empty',
}

dead_candidates = []
for name in set(list(fn_declarations.keys()) + list(fn_definitions.keys())):
    if not name: continue
    if name in qt_signal_names: continue
    if re.match(r'^(slot|on)[A-Z_]', name): continue
    if name in SPECIAL: continue
    if name.startswith('~') or name.startswith('operator'): continue

    in_classes = set()
    for entry in fn_declarations.get(name, []):
        if entry[5]: in_classes.update(entry[5])
    for entry in fn_definitions.get(name, []):
        if entry[5]: in_classes.update(entry[5])
    if name in in_classes: continue   # constructor

    is_virt = any(e[3] for e in fn_declarations.get(name, []))
    is_virt = is_virt or any(e[4] for e in fn_definitions.get(name, []))
    if is_virt: continue

    cc = call_count(name)
    if cc <= 0:
        sites = []
        for entry in fn_declarations.get(name, []):
            sites.append((entry[0], entry[1], 'decl'))
        for entry in fn_definitions.get(name, []):
            sites.append((entry[0], entry[1], 'def'))
        dead_candidates.append((name, sites, cc))


# ---------------------------------------------------------------------------
# Analysis 2: clones
# ---------------------------------------------------------------------------

fp_groups = defaultdict(list)
for name, defs in fn_definitions.items():
    for entry in defs:
        rel, line_no, fp, sz, is_virt, class_stack = entry
        if sz < 12:
            continue
        fp_groups[fp].append((name, rel, line_no, sz))

clone_groups = []
for fp, group in fp_groups.items():
    distinct_locations = set((g[1], g[2]) for g in group)
    if len(distinct_locations) >= 2:
        names = set(g[0] for g in group)
        clone_groups.append((fp, group, names))


# ---------------------------------------------------------------------------
# Report
# ---------------------------------------------------------------------------

print()
print('=' * 78)
print('# DEAD-CODE CANDIDATES (functions declared/defined, never referenced)')
print('=' * 78)
print('# CAVEATS — review every entry; do not auto-delete:')
print('#   - Qt connect() with string-literal SLOT()/SIGNAL() macros are invisible to AST analysis.')
print('#   - Functions taken via &Class::method or std::function pointer count as "uses"')
print('#     only if their bare name appears textually; some uses may still be missed.')
print('#   - Templates / template specialisations may parse oddly.')
print('#   - Library exports (called from outside this tree) will look unused.')
print('#   - Methods marked override / virtual are excluded from this list.')
print('#   - Methods named slotXxx / onXxx are excluded (Qt slot convention).')
print()
dead_candidates.sort()
for name, sites, cc in dead_candidates:
    if len(sites) > 5:
        continue
    print(f'  {name}    (call_count={cc})')
    for f, line, kind in sites:
        print(f'    {kind:5s}  {f}:{line}')
    print()
print(f'# Total dead-code candidates: {len(dead_candidates)}')

print()
print('=' * 78)
print('# CLONE CANDIDATES (function bodies with identical structural fingerprints)')
print('=' * 78)
print('# Identifiers and literals are erased before hashing.  Identical fingerprint')
print('# = identical control flow + same shape of expressions.  May indicate either')
print('# (a) a near-twin pair worth refactoring into a shared helper, or')
print('# (b) two natural-mirror functions that legitimately have the same shape')
print('#     (e.g. left/right halves, undo/redo, increase/decrease).')
print()
# Sort by AST-node count descending → most interesting first
clone_groups.sort(key=lambda x: -x[1][0][3])
shown = 0
for fp, group, names in clone_groups:
    sz = group[0][3]
    if sz < 30: continue
    print(f'  Group ({len(group)} functions, fingerprint size={sz} AST nodes)')
    for nm, f, ln, _ in group:
        print(f'    {nm:35s}  {f}:{ln}')
    print()
    shown += 1
    if shown >= 30: break
print(f'# Shown: {shown} of {len(clone_groups)} clone groups (size>=30 nodes only)')

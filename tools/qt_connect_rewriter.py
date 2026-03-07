#!/usr/bin/env python3
"""
tools/qt_connect_rewriter.py

AST-aware converter: Qt SIGNAL()/SLOT() string-based connects → PMF syntax.
Uses libclang Python bindings + compile_commands.json for full type resolution.

Requirements:
    pip install libclang --break-system-packages

Usage:
    # Dry run — show what would change, no writes:
    python tools/qt_connect_rewriter.py --db tools/clazy_db/compile_commands.json

    # Apply to all eligible files:
    python tools/qt_connect_rewriter.py --db tools/clazy_db/compile_commands.json --apply

    # Single file:
    python tools/qt_connect_rewriter.py --db tools/clazy_db/compile_commands.json --apply \\
        src/klusters/src/klusters.cpp

    # Also apply medium-confidence (overloads, unverified slots — review carefully):
    python tools/qt_connect_rewriter.py --db tools/clazy_db/compile_commands.json \\
        --apply --min-confidence medium

Confidence levels:
    high   — type resolved unambiguously, no overloads → safe PMF: &Class::method
    medium — type resolved but method not found in TU headers (emit anyway, verify),
             OR method is overloaded (static_cast<> emitted, verify signature)
    low    — type could not be resolved → left as-is, listed in skipped report

After running:
    1. git diff src/       — review changes
    2. cmake --build build — confirm clean compile
    3. git add -p && git commit
"""

from __future__ import annotations

import re
import sys
import json
import shlex
import argparse
import subprocess
from pathlib import Path
from dataclasses import dataclass, field
from typing import Optional

try:
    import clang.cindex as ci
except ImportError:
    sys.exit(
        "libclang Python bindings not found.\n"
        "Install:  pip install libclang --break-system-packages\n"
        "Verify:   python3 -c 'import clang.cindex'"
    )

# ──────────────────────────────────────────────────────────────────────────────
#  Constants
# ──────────────────────────────────────────────────────────────────────────────

_INDEX = ci.Index.create()

# Matches SIGNAL(method(args)) / SLOT(method(args)).
# Handles one level of nested parens in arg list (covers most Qt signal sigs).
_MACRO_RE = re.compile(
    r'\b(SIGNAL|SLOT)\s*\((\w+)\s*(\((?:[^()]*|\([^()]*\))*\))\)'
)

_CONF_RANK: dict[str, int] = {'high': 2, 'medium': 1, 'low': 0}

# ──────────────────────────────────────────────────────────────────────────────
#  Data types
# ──────────────────────────────────────────────────────────────────────────────

@dataclass
class Rep:
    abs_start:  int
    abs_end:    int
    original:   str          # e.g. SIGNAL(clicked())
    new_text:   str          # e.g. &QPushButton::clicked
    confidence: str          # 'high' | 'medium' | 'low'
    note:       str = ''


@dataclass
class FileResult:
    path:    str
    applied: list[Rep] = field(default_factory=list)
    skipped: list[Rep] = field(default_factory=list)

# ──────────────────────────────────────────────────────────────────────────────
#  Compile DB helpers
# ──────────────────────────────────────────────────────────────────────────────

def load_db(db_path: Path) -> dict[Path, dict]:
    with open(db_path) as f:
        raw = json.load(f)
    return {Path(e['file']).resolve(): e for e in raw}


def _gcc_version() -> str:
    r = subprocess.run(['gcc', '-dumpversion'], capture_output=True, text=True)
    return r.stdout.strip().split('.')[0] if r.returncode == 0 else '13'


def _clang_builtin_include() -> Optional[str]:
    """
    Find clang builtin headers (stddef.h, stdarg.h, etc.).
    The pip libclang package bundles only the .so, not the headers.
    Ask system clang binaries via -print-resource-dir, or glob.
    """
    import glob as _glob

    for binary in ['clang-15', 'clang-18', 'clang-17', 'clang-16', 'clang-14', 'clang']:
        r = subprocess.run([binary, '-print-resource-dir'],
                           capture_output=True, text=True)
        if r.returncode == 0:
            inc = Path(r.stdout.strip()) / 'include'
            if inc.is_dir():
                return str(inc)

    hits = sorted(
        _glob.glob('/usr/lib/llvm-*/lib/clang/*/include') +
        _glob.glob('/usr/lib/clang/*/include'),
        key=lambda p: [int(x) if x.isdigit() else 0 for x in re.split(r'[\-./]', p)],
        reverse=True,
    )
    return hits[0] if hits else None


_BUILTIN_INCLUDE: Optional[str] = _clang_builtin_include()


def parse_flags(entry: dict) -> list[str]:
    """
    Extract compile flags from a compile DB entry and adapt them for libclang.

    Key issues handled:
    - CMake emits Qt includes as two-token  -isystem /path  pairs; collect both.
    - The pip libclang package has no bundled headers; inject system clang builtins.
    - GCC's implicit C++ stdlib headers must be made explicit for clang.
    """
    if 'arguments' in entry:
        tokens = list(entry['arguments'])
    else:
        tokens = shlex.split(entry['command'])

    result: list[str] = []
    i = 1                               # skip compiler executable
    TWO_TOKEN = {'-isystem', '-include', '-x', '-arch',
                 '--gcc-toolchain', '--sysroot', '-target',
                 '-o', '-MF', '-MT', '-MQ'}
    SKIP_ONLY = {'-o', '-MF', '-MT', '-MQ', '-c'}

    while i < len(tokens):
        tok = tokens[i]
        i  += 1

        if tok in SKIP_ONLY:
            if tok != '-c':
                i += 1          # skip following value too
            continue

        if tok.startswith('-M') and tok not in ('-MD', '-MMD', '-MG'):
            continue

        if tok in TWO_TOKEN:
            if tok not in SKIP_ONLY and i < len(tokens):
                result.append(tok)
                result.append(tokens[i])
                i += 1
            continue

        if any(tok.startswith(p) for p in
               ('-I', '-D', '-U', '-std=', '-W', '-f', '-O', '-g')):
            result.append(tok)
            continue

        # Drop source file and object file positional args
        if tok.endswith(('.cpp', '.cxx', '.cc', '.c', '.o')):
            continue

    # GCC stdlib headers (implicit for GCC, must be explicit for clang)
    v = _gcc_version()
    result += [
        '--gcc-toolchain=/usr',
        f'-I/usr/include/c++/{v}',
        f'-I/usr/include/x86_64-linux-gnu/c++/{v}',
    ]

    # Clang builtin headers (stddef.h, stdarg.h, etc.)
    if _BUILTIN_INCLUDE:
        result.append(f'-I{_BUILTIN_INCLUDE}')

    return result

# ──────────────────────────────────────────────────────────────────────────────
#  Type resolution
# ──────────────────────────────────────────────────────────────────────────────

def _deref(t: ci.Type) -> ci.Type:
    """Strip pointer/reference qualifiers and return canonical type."""
    while t.kind in (ci.TypeKind.POINTER,
                     ci.TypeKind.LVALUEREFERENCE,
                     ci.TypeKind.RVALUEREFERENCE):
        t = t.get_pointee()
    return t.get_canonical()


def _innermost_type(cursor: ci.Cursor, _depth: int = 0) -> ci.Type:
    """
    Return the most specific (most-derived) type for an argument cursor.

    Qt's connect() takes 'const QObject*', so the compiler inserts implicit
    casts that widen e.g. QAction* → const QObject*.  libclang presents these
    ImplicitCastExpr nodes as argument children, hiding the original type.

    Strategy:
      - DeclRefExpr  → use the referenced VarDecl's declared type
      - CXXThisExpr  → type is already exact (MyClass* const)
      - CallExpr     → use the call's return type as-is
      - Anything else (ImplicitCastExpr, UnaryOp, …) → recurse into first child
    """
    if _depth > 6:
        return cursor.type

    k = cursor.kind

    if k == ci.CursorKind.DECL_REF_EXPR:
        ref = cursor.referenced
        if ref is not None and ref.type.kind not in (
                ci.TypeKind.INVALID, ci.TypeKind.UNEXPOSED):
            return ref.type
        return cursor.type

    if k == ci.CursorKind.CXX_THIS_EXPR:
        return cursor.type   # already exact

    if k == ci.CursorKind.CALL_EXPR:
        return cursor.type   # return type of inner call

    if k == ci.CursorKind.MEMBER_REF_EXPR:
        ref = cursor.referenced
        if ref is not None and ref.type.kind not in (
                ci.TypeKind.INVALID, ci.TypeKind.UNEXPOSED):
            return ref.type
        return cursor.type

    # For implicit casts, paren exprs, and other wrappers: drill into first child.
    children = list(cursor.get_children())
    if children:
        return _innermost_type(children[0], _depth + 1)

    return cursor.type


def class_of_type(t: ci.Type) -> Optional[str]:
    """Return the class name for a QObject pointer/reference type, or None."""
    d = _deref(t).get_declaration()
    if d.kind in (ci.CursorKind.CLASS_DECL,
                  ci.CursorKind.STRUCT_DECL,
                  ci.CursorKind.CLASS_TEMPLATE,
                  ci.CursorKind.CLASS_TEMPLATE_PARTIAL_SPECIALIZATION):
        return d.spelling or None
    return None


def enclosing_class(cursor: ci.Cursor) -> Optional[str]:
    """Walk semantic parents to find the innermost enclosing class name."""
    p = cursor.semantic_parent
    while p and p.kind != ci.CursorKind.TRANSLATION_UNIT:
        if p.kind in (ci.CursorKind.CLASS_DECL, ci.CursorKind.STRUCT_DECL):
            return p.spelling
        p = p.semantic_parent
    return None

# ──────────────────────────────────────────────────────────────────────────────
#  Overload detection
# ──────────────────────────────────────────────────────────────────────────────

# Simple cache so we don't re-walk the TU for every lookup in the same file.
_method_cache: dict[tuple, list[ci.Cursor]] = {}


def find_methods(tu: ci.TranslationUnit, cls: str, name: str) -> list[ci.Cursor]:
    """Return all CXX_METHOD cursors for cls::name visible in this TU."""
    key = (id(tu), cls, name)
    if key in _method_cache:
        return _method_cache[key]

    found: list[ci.Cursor] = []

    def walk(node: ci.Cursor) -> None:
        if node.kind in (ci.CursorKind.CLASS_DECL, ci.CursorKind.STRUCT_DECL):
            if node.spelling == cls:
                found.extend(
                    c for c in node.get_children()
                    if c.kind == ci.CursorKind.CXX_METHOD and c.spelling == name
                )
                return           # found the class; stop recursing into siblings
        for c in node.get_children():
            walk(c)

    walk(tu.cursor)
    _method_cache[key] = found
    return found


def overload_cast(m: ci.Cursor) -> str:
    """Build  static_cast<void(Class::*)(params)>  for an overloaded method."""
    cls    = m.semantic_parent.spelling
    params = ', '.join(p.type.spelling for p in m.get_arguments())
    return f'static_cast<void ({cls}::*)({params})>'

# ──────────────────────────────────────────────────────────────────────────────
#  Connect-call analysis
# ──────────────────────────────────────────────────────────────────────────────

def _arg_cursors(call: ci.Cursor) -> list[ci.Cursor]:
    """
    Return the argument child cursors of a CALL_EXPR, skipping the callee.
    Works for both:
      static form:  QObject::connect(sender, SIGNAL(...), recv, SLOT(...))
      member form:  this->connect(SIGNAL(...), recv, SLOT(...))
    In both cases the first child is the callee reference; the rest are args.
    """
    return list(call.get_children())[1:]


def process_connect(
    call: ci.Cursor,
    src:  bytes,
    tu:   ci.TranslationUnit,
) -> list[Rep]:
    """
    Analyze one connect() CALL_EXPR and return Rep objects for every
    SIGNAL()/SLOT() macro argument that can be converted.
    """
    cs   = call.extent.start.offset
    ce   = call.extent.end.offset
    text = src[cs:ce].decode('utf-8', errors='replace')

    macros = list(_MACRO_RE.finditer(text))
    if not macros:
        return []

    args = _arg_cursors(call)
    reps: list[Rep] = []

    for m in macros:
        kind      = m.group(1)     # 'SIGNAL' or 'SLOT'
        meth      = m.group(2)     # method name, e.g. 'clicked'
        arg_types = m.group(3)     # arg string, e.g. '(int, bool)'
        a_start   = cs + m.start()
        a_end     = cs + m.end()
        orig      = m.group(0)

        # ── Find the argument cursor immediately before this macro ─────────
        # That cursor's type is the sender (for SIGNAL) or receiver (for SLOT).
        preceding: Optional[ci.Cursor] = None
        for ac in args:
            ac_end = ac.extent.end.offset
            if ac_end <= a_start:
                if preceding is None or ac_end > preceding.extent.end.offset:
                    preceding = ac

        # ── Resolve the class owning this signal/slot ──────────────────────
        cls: Optional[str] = None
        if preceding is not None:
            # Use _innermost_type to drill past implicit casts
            # (Qt's connect() takes const QObject*, causing QAction*→QObject* widening)
            cls = class_of_type(_innermost_type(preceding))
            # Fallback: try the cursor's own type / referenced decl type
            if cls is None:
                cls = class_of_type(preceding.type)
            if cls is None and preceding.referenced is not None:
                cls = class_of_type(preceding.referenced.type)
            # Reject QObject itself — it means we failed to find the real type
            if cls == 'QObject':
                cls = None

        if cls is None:
            # No preceding cursor → member-form connect, object is `this`
            cls = enclosing_class(call)

        if not cls:
            reps.append(Rep(a_start, a_end, orig, orig, 'low',
                            f'type unresolved for {kind}({meth})'))
            continue

        # ── Overload check ─────────────────────────────────────────────────
        overloads = find_methods(tu, cls, meth)

        if len(overloads) <= 1:
            new_text = f'&{cls}::{meth}'
            conf     = 'high' if overloads else 'medium'
            note     = '' if overloads else f'{cls}::{meth} not found in TU headers'
        else:
            # Overloaded: pick the overload whose arity matches the macro args.
            inner  = arg_types.strip('() ')
            n_args = len(inner.split(',')) if inner.strip() else 0
            match_ = next(
                (ov for ov in overloads
                 if sum(1 for _ in ov.get_arguments()) == n_args),
                overloads[0],
            )
            new_text = f'{overload_cast(match_)}(&{cls}::{meth})'
            conf     = 'medium'
            note     = f'overloaded — verify cast matches {arg_types}'

        reps.append(Rep(a_start, a_end, orig, new_text, conf, note))

    return reps


def _walk_connects(cursor: ci.Cursor):
    """Recursively yield all CALL_EXPR nodes named 'connect'."""
    if cursor.kind == ci.CursorKind.CALL_EXPR and cursor.spelling == 'connect':
        yield cursor
    for c in cursor.get_children():
        yield from _walk_connects(c)

# ──────────────────────────────────────────────────────────────────────────────
#  File driver
# ──────────────────────────────────────────────────────────────────────────────

def process_file(
    path:           Path,
    db:             dict[Path, dict],
    apply:          bool,
    min_confidence: str,
) -> FileResult:
    _method_cache.clear()          # fresh cache per file

    result = FileResult(str(path))
    entry  = db.get(path.resolve())
    if entry is None:
        return result

    flags = parse_flags(entry)
    tu    = _INDEX.parse(
        str(path),
        args=flags,
        options=ci.TranslationUnit.PARSE_INCOMPLETE,
    )
    if tu is None:
        print(f'  [WARN] parse failed: {path}', file=sys.stderr)
        return result

    # Warn if Qt headers weren't found (many errors → type resolution will fail)
    n_err = sum(1 for d in tu.diagnostics if d.severity >= ci.Diagnostic.Error)
    if n_err > 20:
        print(f'  [INFO] {n_err} parse errors in {path.name} — type resolution may be limited',
              file=sys.stderr)

    with open(path, 'rb') as f:
        src = f.read()

    # reps grouped by call: list of (call_start_offset, [Rep, ...])
    call_groups: list[tuple[int, list[Rep]]] = []
    rpath = path.resolve()

    for call in _walk_connects(tu.cursor):
        if call.location.file and Path(call.location.file.name).resolve() == rpath:
            reps = process_connect(call, src, tu)
            if reps:
                call_groups.append((call.extent.start.offset, reps))

    # Deduplicate within each group (AST may visit same call multiple times).
    deduped_groups: list[tuple[int, list[Rep]]] = []
    for call_start, reps in call_groups:
        seen: set[int] = set()
        deduped: list[Rep] = []
        for r in reps:
            if r.abs_start not in seen:
                seen.add(r.abs_start)
                deduped.append(r)
        deduped_groups.append((call_start, deduped))

    # For each connect() call, if ANY rep in the group is low-confidence,
    # suppress ALL reps in that group — a partial PMF+string connect is
    # illegal and will fail to compile.
    min_rank = _CONF_RANK[min_confidence]
    unique: list[Rep] = []
    for call_start, reps in deduped_groups:
        has_low = any(_CONF_RANK[r.confidence] < min_rank or r.original == r.new_text
                      for r in reps)
        if has_low:
            for r in reps:
                result.skipped.append(r)
        else:
            for r in reps:
                result.applied.append(r)

    # Sort applied in reverse offset order for safe in-place byte replacement.
    result.applied.sort(key=lambda r: r.abs_start, reverse=True)

    if apply and result.applied:
        data = bytearray(src)
        for r in result.applied:       # already in reverse offset order → safe
            data[r.abs_start:r.abs_end] = r.new_text.encode()
        with open(path, 'wb') as f:
            f.write(bytes(data))

    return result

# ──────────────────────────────────────────────────────────────────────────────
#  Entry point
# ──────────────────────────────────────────────────────────────────────────────

def main() -> None:
    ap = argparse.ArgumentParser(
        formatter_class=argparse.RawDescriptionHelpFormatter,
        description=__doc__,
    )
    ap.add_argument('--db', required=True,
                    help='Path to merged compile_commands.json')
    ap.add_argument('--apply', action='store_true',
                    help='Write changes to disk (default: dry run)')
    ap.add_argument('--min-confidence', choices=('high', 'medium'), default='high',
                    help='Minimum confidence level to apply (default: medium)')
    ap.add_argument('files', nargs='*',
                    help='Specific .cpp files (default: all files in DB)')
    args = ap.parse_args()

    db = load_db(Path(args.db))

    if args.files:
        targets = [Path(f).resolve() for f in args.files]
    else:
        targets = sorted(p for p in db if p.suffix in ('.cpp', '.cxx', '.cc') and p.exists())

    total_applied = total_skipped = 0

    for path in targets:
        if not path.exists():
            print(f'  [SKIP] not found: {path}')
            continue
        try:
            raw = path.read_text(errors='replace')
        except OSError:
            continue
        if 'SIGNAL(' not in raw and 'SLOT(' not in raw:
            continue

        res = process_file(path, db, args.apply, args.min_confidence)
        na, ns = len(res.applied), len(res.skipped)
        total_applied += na
        total_skipped += ns

        if na == 0 and ns == 0:
            continue

        try:
            rel = path.relative_to(Path.cwd())
        except ValueError:
            rel = path

        all_reps = sorted(res.applied + res.skipped, key=lambda r: r.abs_start)
        applied_set = set(id(r) for r in res.applied)
        print(f'\n  {rel}  ({na} converted, {ns} skipped)')
        for r in all_reps:
            if id(r) in applied_set:
                tag = '✓'
            elif r.confidence == 'medium':
                tag = '~'
            else:
                tag = '✗'
            print(f'    {tag} [{r.confidence:6}]  {r.original!r:45}  →  {r.new_text!r}')
            if r.note:
                print(f'           note: {r.note}')

    print(f'\n{"─" * 60}')
    action = 'applied' if args.apply else 'convertible (dry run)'
    print(f'  Converts {action:<20}: {total_applied}')
    print(f'  Skipped                      : {total_skipped}')
    if not args.apply and total_applied:
        print(f'\n  Re-run with --apply to write changes.')


if __name__ == '__main__':
    main()

"""Usage: dead_and_dupes_textual.py <repo>/src/ <functions.json>   (the json: function_metrics.py)
Needs nothing but the json; find_dead_and_dupes.py is the AST-based one.

Candidates for dead code and duplicated functions in src/klusters/src (textual; triage by hand).
dead: a member function whose bare name occurs nowhere but at its own definition and declaration
      (all of src/klusters/src, test/ and libklustersshared searched; SLOT()/SIGNAL() strings count as uses).
dupes: functions of >= 12 lines whose bodies are identical after dropping comments and whitespace,
       and those identical after also replacing identifiers and literals (same structure)."""
import json, re, glob, sys, collections, hashlib
if len(sys.argv) != 3: sys.exit('usage: dead_and_dupes_textual.py <repo>/src/ <functions.json from function_metrics.py>')
R = sys.argv[1].rstrip('/') + '/'
fns = json.load(open(sys.argv[2]))
texts = {f: open(f, errors='replace').read() for f in glob.glob(R + 'klusters/src/*') + glob.glob(R + 'klusters/test/*.cpp') + glob.glob(R + 'libklustersshared/src/**/*', recursive=True) if f.endswith(('.cpp', '.h', '.ui', '.mm'))}
strip = lambda t: re.sub(r'//[^\n]*', '', re.sub(r'/\*.*?\*/', '', t, flags=re.S))
blob = '\n'.join(strip(t) for t in texts.values()); words = collections.Counter(re.findall(r'[A-Za-z_]\w*', blob))
defs = collections.Counter(f['name'].split('::')[-1] for f in fns)
dead = []
for f in fns:
    if '::' not in f['name']: continue
    n = f['name'].split('::')[-1]
    if n.startswith(('~', 'operator')) or n == f['name'].split('::')[0]: continue
    if re.fullmatch(r'(paint|mouse|key|wheel|resize|show|hide|close|drag|drop|focus|context|timer|change|enter|leave|tablet|event)\w*Event|eventFilter|sizeHint|minimumSizeHint|run|data|rowCount|columnCount|headerData|flags|paintEvent|event|createEditor|setEditorData|setModelData|paint|metaObject|qt_\w+|initializeGL|paintGL|resizeGL', n): continue
    if words[n] <= defs[n] + 1: dead.append(f)       # definitions + one declaration each at most
print('dead-code candidates (name used nowhere else in CODE; comments do not count):', len(dead), 'functions,', sum(f['length'] for f in dead), 'lines')
for f in sorted(dead, key=lambda f: -f['length'])[:40]: print(f"  {f['length']:4d} lines  {f['file']}:{f['line']}  {f['name']}")
# duplicates
src = {f.split('/')[-1]: t.splitlines() for f, t in texts.items() if '/klusters/src/' in f}
def body(f):
    b = '\n'.join(src[f['file']][f['line'] - 1:f['line'] - 1 + f['length']]); b = re.sub(r'/\*.*?\*/', '', b, flags=re.S); b = re.sub(r'//[^\n]*', '', b)
    i = b.find('{'); return b[i:] if i >= 0 else b
ex, st = collections.defaultdict(list), collections.defaultdict(list)
for f in fns:
    if f['length'] < 12 or f['file'] not in src: continue
    b = body(f); e = re.sub(r'\s+', '', b)
    if len(e) < 300: continue
    ex[hashlib.md5(e.encode()).hexdigest()].append(f)
    s = re.sub(r'"[^"]*"', 'S', b); s = re.sub(r'\b\d[\w.]*\b', 'N', s); s = re.sub(r'[A-Za-z_]\w*', 'I', s); s = re.sub(r'\s+', '', s)
    st[hashlib.md5(s.encode()).hexdigest()].append(f)
E1 = [g for g in ex.values() if len(g) > 1]; S1 = [g for g in st.values() if len(g) > 1 and len({x['name'] + x['file'] for x in g}) > 1]
print('\nidentical bodies:', len(E1), 'groups,', sum(sum(x['length'] for x in g[1:]) for g in E1), 'duplicated lines')
for g in sorted(E1, key=lambda g: -g[0]['length'])[:15]: print(f"  {g[0]['length']:4d} lines x{len(g)}: " + ' | '.join(f"{x['file']}:{x['line']} {x['name']}" for x in g)[:230])
exact = {id(x) for g in E1 for x in g}
S2 = [g for g in S1 if not all(id(x) in exact for x in g)]
print('\nsame structure, different names (not counted above):', len(S2), 'groups,', sum(sum(x['length'] for x in g[1:]) for g in S2), 'lines')
for g in sorted(S2, key=lambda g: -g[0]['length'])[:15]: print(f"  {g[0]['length']:4d} lines x{len(g)}: " + ' | '.join(f"{x['file']}:{x['line']} {x['name']}" for x in g)[:230])

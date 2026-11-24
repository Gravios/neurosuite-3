"""Function metrics of a source directory with lizard (pip install lizard): length, cyclomatic
complexity, parameters.  Usage: function_metrics.py <dir, e.g. src/klusters/src> <functions.json to write>"""
import glob, sys, json, lizard, collections
root = sys.argv[1]; files = sorted(glob.glob(root + '/*.cpp') + glob.glob(root + '/*.h'))
fns = []
for f in files:
    r = lizard.analyze_file(f)
    for fn in r.function_list:
        fns.append(dict(file=f.split('/')[-1], name=fn.name, long=fn.long_name[:90], nloc=fn.nloc, ccn=fn.cyclomatic_complexity, params=len(fn.parameters), line=fn.start_line, length=fn.end_line - fn.start_line + 1))
json.dump(fns, open(sys.argv[2], 'w'))
n = len(fns); L = sorted(f['length'] for f in fns)
print('functions', n, '| median length', L[n // 2], '| 90th', L[int(n * .9)], '| 99th', L[int(n * .99)])
for t in (50, 100, 200, 400): print(f'  longer than {t} lines: {sum(l > t for l in L)}  ({sum(l for l in L if l > t) * 100 // sum(L)}% of function lines)')
print('  cyclomatic complexity > 15:', sum(f['ccn'] > 15 for f in fns), '| > 30:', sum(f['ccn'] > 30 for f in fns), '| > 60:', sum(f['ccn'] > 60 for f in fns))
print('  more than 6 parameters:', sum(f['params'] > 6 for f in fns))
print('\nlongest:')
for f in sorted(fns, key=lambda f: -f['length'])[:25]: print(f"  {f['length']:5d} lines  ccn {f['ccn']:4d}  {f['file']}:{f['line']}  {f['name']}")
print('\nmost parameters:')
for f in sorted(fns, key=lambda f: -f['params'])[:8]: print(f"  {f['params']:3d}  {f['file']}:{f['line']}  {f['name']}")
by = collections.Counter(); tot = collections.Counter()
for f in fns:
    tot[f['file']] += 1
    if f['length'] > 100: by[f['file']] += 1
print('\nfiles with the most functions over 100 lines:', by.most_common(10))

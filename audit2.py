import re,glob,os
S='src/klusters/src/'
fnpat=re.compile(r'^[A-Za-z_][\w:<>,\*&\s]*\bKlustersDoc::(\w+)')
raw=re.compile(r'clusterPalette\.(selectItems|updateClusterList)\s*\(')
bad=0
for f in sorted(glob.glob(S+'klustersdoc*.cpp')):
    lines=open(f,encoding='utf-8').read().split('\n')
    idx=[(i,m.group(1)) for i,l in enumerate(lines) if (m:=fnpat.match(l))]
    for n,(st,name) in enumerate(idx):
        en=idx[n+1][0] if n+1<len(idx) else len(lines)
        if name=='refreshActivePalette': continue      # the helper itself
        body=lines[st:en]
        scoped = any('refreshActivePalette(' in l or 'hierarchyChildSelectionRequested' in l for l in body)
        # a raw call is only a problem if it is NOT guarded on the same or a nearby line
        unguarded=[]
        for i,l in enumerate(body):
            if raw.search(l) and not l.strip().startswith('//'):
                ctx="\n".join(body[max(0,i-8):i+1])
                if 'childScopeActive' not in ctx: unguarded.append(i+st+1)
        if scoped and unguarded:
            bad+=1
            print(f"MIXED    {os.path.basename(f):26} {name:28} unguarded raw call(s) at {unguarded}")
print("mixed paths remaining:", bad)

#!/usr/bin/env python3
import subprocess, os
os.makedirs('/tmp/swp', exist_ok=True)
ok=bad=0; badcases=[]
for seed in range(1, 120):
    n=(seed%7)*15+10; m=(seed%5)*10+6
    dens = 0.12 + (seed%5)*0.05
    subprocess.run(['python3','tools/gen_canonical.py',str(n),str(m),str(dens),str(seed),'/tmp/swp/p'],capture_output=True)
    r=subprocess.run(['timeout','30','./lpsolve','/tmp/swp/p.lp'],capture_output=True,text=True)
    if r.returncode!=0: bad+=1; badcases.append((seed,'hang',r.returncode)); continue
    mysol=[l.split()[1] for l in r.stdout.splitlines() if l.startswith('objective:')]
    myst=[l.split()[1] for l in r.stdout.splitlines() if l.startswith('status:')]
    g=subprocess.run(['glpsol','-m','/tmp/swp/p.mod'],capture_output=True,text=True,timeout=30).stdout
    gst='OPTIMAL' if 'OPTIMAL LP SOLUTION FOUND' in g else 'other'
    gobj=[l.split()[1] for l in g.splitlines() if l.startswith('OBJ')]
    if myst==['OPTIMAL'] and gst=='OPTIMAL' and gobj:
        if abs(float(mysol[0])-float(gobj[0]))<=1e-5*max(1,abs(float(gobj[0]))): ok+=1
        else: bad+=1; badcases.append((seed,'obj',mysol[0],gobj[0]))
    else:
        bad+=1; badcases.append((seed,'status',myst,gst))
print(f"canonical sweep n,m varied: OK={ok} BAD={bad}")
for c in badcases[:15]: print("  ",c)

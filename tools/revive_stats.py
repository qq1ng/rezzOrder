"""Revive statistics over arcdps WvW fight logs.

usage: python tools/revive_stats.py <number of most recent logs>

Attribution of a squad member getting up to a revive cast is a timing heuristic (nearest used revive
cast stop within -1.2 s .. +1.5 s), so per-cast revive counts are approximate."""
import sys, glob, os, struct, bisect, collections, math, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import evtc
CAST = {9163:2000,5762:2000,5763:2000,5760:2000,5761:2000,10244:1250,10611:1500,14419:2000,12569:1500,12601:750,69336:750}
# expected ms from cast stop to revive effect (from the single-fight look): spirit slam happens ~850 ms after the spirit cast
files = sorted(glob.glob(r'C:/Users/exoqq/Documents/Guild Wars 2/addons/arcdps/arcdps.cbtlogs/**/*.zevtc', recursive=True), key=os.path.getmtime)[-int(sys.argv[1]):]
t0=time.time()
stats = collections.defaultdict(lambda: collections.Counter())
per_cast_ups = collections.defaultdict(collections.Counter)   # skill -> Counter(n ups attributed)
lag = collections.defaultdict(list); dist = collections.defaultdict(list)
ups_total=0; ups_attr=0; timings=collections.defaultdict(set); fights=0
for f in files:
    try: log = evtc.read(f)
    except Exception as ex: print('skip', f, ex); continue
    fights+=1
    A,S,E = log['agents'], log['skills'], log['events']
    squad = {a for a,v in A.items() if v['is_player'] and v['account'].startswith(':')}
    inst={}
    for e in E:
        if e['statechange']==0 and e['src'] in A: inst.setdefault(e['src_inst'], e['src'])
    pos=collections.defaultdict(list)
    for e in E:
        if e['statechange']==19:
            pos[e['src']].append((e['time'],)+struct.unpack('<fff', struct.pack('<Qi', e['dst'], e['value'])))
    def at(a,t):
        s=pos.get(a)
        if not s: return None
        i=bisect.bisect_right([p[0] for p in s], t)-1
        return s[max(i,0)]
    casts=[]
    for e in E:
        if e['skill'] in CAST and e['statechange']==33 and e['src']==3: timings[S.get(e['skill'])].add(e['dst'])
        if e['skill'] in CAST and e['statechange']==68:
            caster = e['src'] if e['src'] in squad else inst.get(e['src_master'])
            if caster not in squad: continue
            used = e['result']==22 or e['buff_dmg']>=CAST[e['skill']]
            name=S.get(e['skill'],str(e['skill']))
            stats[name]['casts']+=1; stats[name]['used' if used else 'cancelled']+=1
            stats[name]['reason_%d'%e['result']]+=1
            if used: casts.append({'t':e['time'],'skill':name,'id':e['skill'],'src':e['src'],'ups':0})
    downs={}
    for e in E:
        if e['src'] not in squad: continue
        if e['statechange']==5: downs[e['src']]=e['time']
        elif e['statechange'] in (3,4) and e['src'] in downs:
            d0=downs.pop(e['src'])
            if e['statechange']==4: continue
            ups_total+=1; t=e['time']
            best=None
            for c in casts:
                if c['id']==12569: continue  # attribute to the spirit's slams instead
                dt=t-c['t']
                # slams/banner/instant skills: effect at or just before the recorded stop, up to ~1.2 s later
                if -1200<=dt<=1500 and (best is None or abs(dt)<abs(best[1])): best=(c,dt)
            if best:
                ups_attr+=1; c,dt=best; c['ups']+=1; lag[c['skill']].append(dt)
                p=at(e['src'],t); cp=at(c['src'],c['t'])
                if p and cp: dist[c['skill']].append(math.dist(p[1:],cp[1:]))
    for c in casts:
        if c['id']!=12569: per_cast_ups[c['skill']][c['ups']]+=1
print(f'{fights} fights parsed in {time.time()-t0:.0f}s; squad ups {ups_total}, attributed to a revive skill {ups_attr}')
for k,v in sorted(stats.items()):
    print(f"{k:20} {dict(v)}")
print('\nups per used cast (skill: {n_ups: n_casts}):')
for k,v in sorted(per_cast_ups.items()): print(f"  {k:20} {dict(sorted(v.items()))}")
print('\nlag up-minus-stop ms (min/median/max) and caster distance to revived (median/max):')
for k in sorted(lag):
    L=sorted(lag[k]); D=sorted(dist[k]) or [0]
    print(f"  {k:20} lag {L[0]}/{L[len(L)//2]}/{L[-1]} n={len(L)}  dist {D[len(D)//2]:.0f}/{D[-1]:.0f}")
print('\nSKILLTIMING type 3 values:', {k:sorted(v) for k,v in timings.items()})

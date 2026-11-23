from collections import Counter, defaultdict
rows=[]
for line in open("build/writes_all.txt"):
    a,d,m=line.split(); rows.append((int(a,16),int(d,16),int(m,16)))
print("total",len(rows))
# histogram by 64K page
h=Counter(a>>16 for a,d,m in rows)
for page,c in sorted(h.items()): print(f"{page<<16:06X}: {c}")
# for each 64K page, min/max address and sample values
by=defaultdict(list)
for a,d,m in rows: by[a>>16].append((a,d,m))
for page in sorted(by):
    vals=by[page]
    addrs=sorted(set(a for a,d,m in vals))
    print(f"--- {page<<16:06X} count={len(vals)} range={addrs[0]:06X}-{addrs[-1]:06X} distinct_addrs={len(addrs)}")
    for a,d,m in sorted(vals, key=lambda t:(t[0],t[1]))[:8]:
        print(f"   {a:06X} = {d:04X} mask {m:04X}")

pcs=[]
with open("build/trace.txt","r",encoding="utf-16",errors="ignore") as f:
    for line in f:
        if not line[:6].strip(): continue
        try: pcs.append(int(line[:6],16))
        except: pass
print("instructions",len(pcs))
for i,p in enumerate(pcs):
    if 0x2280 <= p < 0x22E0:
        print("first 0x22xx at",i,"context:")
        print(" ".join(f"{x:06X}" for x in pcs[max(0,i-40):i+10]))
        break
print("last 120:")
print(" ".join(f"{x:06X}" for x in pcs[-120:]))
print("1D0A count",pcs.count(0x1D0A),"22D8 count",pcs.count(0x22D8),"1D02 count",pcs.count(0x1D02))

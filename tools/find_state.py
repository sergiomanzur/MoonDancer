import sys
sys.path.insert(0,"tools")
import mwalk
sys.path.insert(0,"tools/arcade-decomp")
from m68000 import disassembler as dis
files=mwalk.load_set("mwalk.zip")
enc=mwalk.build_main_rom(files)
fd=mwalk.FD1094(files["317-0159.key"])
best=[]
for st in range(256):
    # decrypt a 128-byte window on demand
    base=0x1D80; size=0x100
    buf=bytearray(size)
    for off in range(0,size,2):
        v=(enc[base+off]<<8)|enc[base+off+1]
        d=fd.decrypt_word(base+off,v,st,False)
        buf[off]=d>>8; buf[off+1]=d&0xff
    mem=dis.FlatMemory(bytes(buf))
    pc=0x1DC4-base; bad=0; txt=[]
    for i in range(14):
        ins=dis.decode(mem,pc)
        if ins.flow==dis.FLOW_INVALID or ins.mnemonic.startswith("dc."): bad+=1
        txt.append(f"{pc+base:06X}: {ins.text}")
        pc+=ins.length
    best.append((bad,st,txt))
best.sort(key=lambda t:(t[0],t[1]))
for bad,st,txt in best[:3]:
    print(f"=== state {st:02X} invalid={bad} ===")
    print("\n".join(txt))

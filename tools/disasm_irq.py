import sys
sys.path.insert(0,"tools")
import mwalk
sys.path.insert(0,"tools/arcade-decomp")
from m68000 import disassembler as dis
files=mwalk.load_set("mwalk.zip"); enc=mwalk.build_main_rom(files); fd=mwalk.FD1094(files["317-0159.key"])
st=0x39; base=0x1C80; size=0x300
buf=bytearray(size)
for off in range(0,size,2):
    v=(enc[base+off]<<8)|enc[base+off+1]; d=fd.decrypt_word(base+off,v,st,False); buf[off]=d>>8; buf[off+1]=d&0xff
mem=dis.FlatMemory(bytes(buf))
pc=0x1C80
for i in range(110):
    ins=dis.decode(mem,pc-base)
    print(f"{pc:06X}: {ins.text}")
    pc+=ins.length

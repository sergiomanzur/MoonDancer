import sys
sys.path.insert(0,"tools")
import mwalk
from pathlib import Path
sys.path.insert(0,"tools/arcade-decomp")
from m68000 import disassembler as dis

files = mwalk.load_set("mwalk.zip")
enc = mwalk.build_main_rom(files)
fd = mwalk.FD1094(files["317-0159.key"])
for state, start, n in [(0xcc,0x426,80),(0,0x400,20)]:
    rom = fd.decrypt_rom(enc,state)
    mem=dis.FlatMemory(rom)
    pc=start
    print(f"=== state {state:02x} at {start:06x} ===")
    for i in range(n):
        ins=dis.decode(mem,pc)
        print(f"{pc:06X}: {ins.text}")
        if ins.mnemonic=="cmpi" and ins.size=="l" and ins.operands and ins.operands[0].value is not None:
            v=ins.operands[0].value & 0xffffffff
            if (v & 0xffff)==0xffff:
                print(f"  ; FD1094 state -> {v>>16:02x}")
        pc+=ins.length

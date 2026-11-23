import sys
sys.path.insert(0, "tools/arcade-decomp")
from m68000 import disassembler as dis
from pathlib import Path
rom = Path("build/mwalk_decrypted_00.bin").read_bytes()
mem = dis.FlatMemory(rom)
pc = 0x400
for i in range(120):
    ins = dis.decode(mem, pc)
    print(f"{pc:06X}: {ins.text}")
    pc += ins.length

"""Decode instructions at a given FD1094-state/PC pair from the user's ROM."""
import sys
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
ROOT = TOOLS.parent
sys.path.insert(0, str(TOOLS))
sys.path.insert(0, str(ROOT / "tools" / "arcade-decomp"))

import mwalk
from m68000 import disassembler as dis


def main():
    state = int(sys.argv[1], 16)
    pc = int(sys.argv[2], 16)
    count = int(sys.argv[3]) if len(sys.argv) > 3 else 16
    files = mwalk.load_set(str(ROOT / "mwalk.zip"))
    enc = mwalk.build_main_rom(files)
    fd = mwalk.FD1094(files["317-0159.key"])
    base = pc & 0xFFFFF
    buf = bytearray(32 * count)
    for off in range(0, len(buf), 2):
        addr = base + off
        val = (enc[addr] << 8) | enc[addr + 1] if addr + 1 < len(enc) else 0xFFFF
        buf[off:off + 2] = fd.decrypt_word(addr, val, state, False).to_bytes(2, "big")
    mem = dis.FlatMemory(bytes(buf))
    cur = base
    for _ in range(count):
        ins = dis.decode(mem, cur - base)
        print(f"{cur:06X}: {ins.text:32s} len={ins.length} flow={ins.flow} target={ins.target}")
        if ins.length == 0:
            break
        cur += ins.length


if __name__ == "__main__":
    main()

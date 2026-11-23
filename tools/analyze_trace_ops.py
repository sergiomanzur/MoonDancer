"""Decode every (FD1094 state, PC) pair from a runtime trace and report the
instruction subset.  Used to scope the static recompiler emitter."""
import sys
from collections import Counter
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
ROOT = TOOLS.parent
sys.path.insert(0, str(TOOLS))
sys.path.insert(0, str(ROOT / "tools" / "arcade-decomp"))

import mwalk
from m68000 import disassembler as dis


def main(trace_path):
    files = mwalk.load_set(str(ROOT / "mwalk.zip"))
    encrypted = mwalk.build_main_rom(files)
    fd = mwalk.FD1094(files["317-0159.key"])

    entries = []
    for line in Path(trace_path).read_text().splitlines():
        if not line.strip():
            continue
        st_s, pc_s = line.split()
        entries.append((int(st_s, 16), int(pc_s, 16)))

    ops = Counter()
    sizes = Counter()
    modes = Counter()
    instrs = {}
    states = Counter(st for st, _ in entries)

    for state, pc in entries:
        # Decrypt a window large enough for any 68000 instruction.
        base = pc & 0xFFFFF
        buf = bytearray(32)
        for off in range(0, 32, 2):
            addr = base + off
            val = (encrypted[addr] << 8) | encrypted[addr + 1] if addr + 1 < len(encrypted) else 0xFFFF
            dec = fd.decrypt_word(addr, val, state, False)
            buf[off] = dec >> 8
            buf[off + 1] = dec & 0xFF
        ins = dis.decode(dis.FlatMemory(bytes(buf)), 0)
        key = ins.mnemonic + (("." + ins.size) if ins.size else "")
        ops[key] += 1
        sizes[ins.size or "-"] += 1
        for op in ins.operands:
            modes[op.mode] += 1
        instrs[(state, pc)] = ins

    print("trace entries:", len(entries), "states:", len(states))
    print("states used:", " ".join(f"{s:02X}:{n}" for s, n in states.most_common()))
    print("\ninstructions by mnemonic (count):")
    for name, n in ops.most_common(100):
        print(f"  {name:12s} {n}")
    print("\noperand modes:", dict(modes))
    print("sizes:", dict(sizes))

    bad = [(k, v) for k, v in instrs.items()
           if v.flow == dis.FLOW_INVALID or v.mnemonic.startswith("dc.")]
    print("\ninvalid/unknown entries:", len(bad))
    for (st, pc), v in bad[:20]:
        print(f"  {st:02X}:{pc:06X} {v.text}")


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "build/trace.txt")

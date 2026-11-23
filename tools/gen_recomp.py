"""Generate the native C translation of every (FD1094 state, PC) pair that
the interpreter actually executed.

This is a trace-guided static recompiler, not a runtime decoder: each
generated function contains the decoded instruction's operands as C
constants and calls the shared 68000 helper library.  Untraced instructions
fall back to Musashi at run time, so the game remains correct while coverage
grows.
"""
import sys
from collections import Counter
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
ROOT = TOOLS.parent
sys.path.insert(0, str(TOOLS))
sys.path.insert(0, str(ROOT / "tools" / "arcade-decomp"))

import mwalk
from m68000 import disassembler as dis

SIZES = {"b": 1, "w": 2, "l": 4}
CC = {"t": 0, "f": 1, "hi": 2, "ls": 3, "cc": 4, "cs": 5, "ne": 6, "eq": 7,
      "vc": 8, "vs": 9, "pl": 10, "mi": 11, "ge": 12, "lt": 13, "gt": 14, "le": 15}
SHIFTS = {"asl": 0, "asr": 1, "lsl": 2, "lsr": 3, "rol": 4, "ror": 5, "roxl": 6, "roxr": 7}
BITOPS = {"btst": 0, "bset": 1, "bclr": 2, "bchg": 3}
CURRENT_PC = 0


def regnum(name):
    name = name.upper()
    if name.startswith("D"):
        return int(name[1])
    if name.startswith("A"):
        return 8 + int(name[1])
    raise ValueError(name)


def dreg(op):
    return regnum(op.reg)


def areg(op):
    return regnum(op.reg) - 8


def ea(op):
    m = op.mode
    if m == "dreg":
        return f"&(mw_ea_t){{MW_EA_DREG,{dreg(op)},0,0,0,0,0}}"
    if m == "areg":
        return f"&(mw_ea_t){{MW_EA_AREG,{areg(op)},0,0,0,0,0}}"
    if m == "indirect":
        return f"&(mw_ea_t){{MW_EA_IND,{areg(op)},0,0,0,0,0}}"
    if m == "postinc":
        return f"&(mw_ea_t){{MW_EA_POSTINC,{areg(op)},0,0,0,0,0}}"
    if m == "predec":
        return f"&(mw_ea_t){{MW_EA_PREDEC,{areg(op)},0,0,0,0,0}}"
    if m == "disp":
        return f"&(mw_ea_t){{MW_EA_DISP,{areg(op)},0,0,0,{op.disp},0}}"
    if m == "index":
        idx = regnum(op.index)
        isz = 1 if op.index_size == "l" else 0
        return f"&(mw_ea_t){{MW_EA_INDEX,{areg(op)},{idx},{isz},0,{op.disp},0}}"
    if m in ("abs_short", "abs_long"):
        return f"&(mw_ea_t){{MW_EA_ABS,0,0,0,0,0,{op.value & 0xFFFFFFFF}u}}"
    if m == "pc_disp":
        # The decoder reports disp+2; the effective address is relative to
        # the instruction start.
        target = (CURRENT_PC + op.value) & 0xFFFFFFFF
        return f"&(mw_ea_t){{MW_EA_PC_DISP,0,0,0,0,0,{target}u}}"
    if m == "pc_index":
        idx = regnum(op.index)
        isz = 1 if op.index_size == "l" else 0
        # Same PC-relative base as (d16,PC): the instruction address plus
        # the decoder's extension-word offset.
        base = (CURRENT_PC + op.base) & 0xFFFFFFFF
        return f"&(mw_ea_t){{MW_EA_PC_INDEX,0,{idx},{isz},0,{op.disp},{base}u}}"
    if m == "immediate":
        return f"&(mw_ea_t){{MW_EA_IMM,0,0,0,0,0,{op.value & 0xFFFFFFFF}u}}"
    if m == "quick":
        return f"&(mw_ea_t){{MW_EA_QUICK,0,0,0,0,0,{op.value & 0xFFFFFFFF}u}}"
    raise ValueError(f"unsupported EA mode {m}")


def reglist_mask(op):
    mask = 0
    for name in op.registers:
        mask |= 1 << regnum(name)
    return mask


def condition(ins):
    if ins.condition is None:
        raise ValueError("no condition")
    return CC[ins.condition]


def emit(ins, state, pc, opcode, unsupported, body_only=False, set_pc=True, name=None):
    global CURRENT_PC
    CURRENT_PC = pc
    s = name or f"g_{state:02X}_{pc:06X}"
    out = []
    if not body_only:
        out.append(f"/* {pc:06X} [{state:02X}] {ins.text} */")
        out.append(f"static void {s}(s18_t *m, mw68k_t *st)")
        out.append("{")
    else:
        out.append(f"    /* {pc:06X} [{state:02X}] {ins.text} */")
    mn = ins.mnemonic
    ops = ins.operands
    size = SIZES.get(ins.size or "", 0)
    next_pc = pc + ins.length
    cycles = f"mw68k_cycles(0x{opcode:04X})"
    # 8-bit branch targets are relative to the extension word (PC+2);
    # 16-bit and DBcc targets are relative to the instruction start.
    branch_target = pc + ins.target if ins.target is not None else next_pc

    def call(code, fall=True, cyc=cycles):
        out.append("    " + code + ";")
        out.append(f"    st->cycles += {cyc};")
        if fall and set_pc:
            out.append(f"    st->pc = {next_pc}u;")

    try:
        if mn == "move":
            if size == 0:
                src, dst = ops[0], ops[1]
                if dst.mode == "sr":
                    call(f"mw_move_to_sr(m,st,2,{ea(src)},1)")
                elif dst.mode == "ccr":
                    call(f"mw_move_to_sr(m,st,1,{ea(src)},0)")
                elif src.mode == "sr":
                    call(f"mw_move_from_sr(m,st,2,{ea(dst)},1)")
                elif src.mode == "ccr":
                    call(f"mw_move_from_sr(m,st,1,{ea(dst)},0)")
                else:
                    raise ValueError("unsized move without SR/CCR")
            elif ops[1].mode == "areg":
                call(f"mw_movea(m,st,{size},{ea(ops[0])},{areg(ops[1])})")
            else:
                call(f"mw_move(m,st,{size},{ea(ops[0])},{ea(ops[1])})")
        elif mn == "movea":
            call(f"mw_movea(m,st,{size},{ea(ops[0])},{areg(ops[1])})")
        elif mn == "moveq":
            call(f"mw_moveq(st,{ops[0].value & 0xFFFFFFFF}u,{dreg(ops[1])})")
        elif mn == "lea":
            call(f"mw_lea(m,st,{ea(ops[0])},{areg(ops[1])})")
        elif mn == "pea":
            call(f"mw_pea(m,st,{ea(ops[0])})")
        elif mn == "add":
            call(f"mw_add(m,st,{size},{ea(ops[0])},{ea(ops[1])},1)")
        elif mn == "adda":
            call(f"mw_adda(m,st,{size},{ea(ops[0])},{areg(ops[1])})")
        elif mn == "sub":
            call(f"mw_sub(m,st,{size},{ea(ops[0])},{ea(ops[1])},1)")
        elif mn == "suba":
            call(f"mw_suba(m,st,{size},{ea(ops[0])},{areg(ops[1])})")
        elif mn == "cmp":
            call(f"mw_cmp(m,st,{size},{ea(ops[0])},{ea(ops[1])})")
        elif mn == "cmpa":
            call(f"mw_cmpa(m,st,{size},{ea(ops[0])},{areg(ops[1])})")
        elif mn == "and":
            call(f"mw_and(m,st,{size},{ea(ops[0])},{ea(ops[1])},1)")
        elif mn == "or":
            call(f"mw_or(m,st,{size},{ea(ops[0])},{ea(ops[1])},1)")
        elif mn == "eor":
            call(f"mw_eor(m,st,{size},{ea(ops[0])},{ea(ops[1])},1)")
        elif mn in ("addi", "subi", "andi", "ori", "eori", "cmpi"):
            imm = ops[0]
            dst = ops[1]
            if dst.mode in ("ccr", "sr"):
                op = {"andi": 0, "ori": 1, "eori": 2}.get(mn)
                if op is None:
                    raise ValueError(mn + " to CCR")
                call(f"mw_imm_to_ccr(st,{op},{imm.value & 0xFFFFFFFF}u,{1 if dst.mode == 'sr' else 0})")
            else:
                fn = {"addi": "mw_addi", "subi": "mw_subi", "andi": "mw_andi",
                      "ori": "mw_ori", "eori": "mw_eori", "cmpi": "mw_cmpi"}[mn]
                call(f"{fn}(m,st,{size},{ea(dst)},{imm.value & 0xFFFFFFFF}u)")
                if mn == "cmpi" and size == 4 and dst.mode == "dreg" and dreg(dst) == 0 \
                        and (imm.value & 0xFFFF) == 0xFFFF:
                    out.insert(-2, f"    fd1094_cmpi_callback(&m->fd, {imm.value & 0xFFFFFFFF}u, 0);")
        elif mn in ("addq", "subq"):
            fn = "mw_addq" if mn == "addq" else "mw_subq"
            call(f"{fn}(m,st,{size},{ea(ops[1])},{ops[0].value & 0xFFFFFFFF}u)")
        elif mn in ("neg", "not", "clr", "tst"):
            call(f"mw_{mn}(m,st,{size},{ea(ops[0])})")
        elif mn == "negx":
            call(f"mw_negx(m,st,{size},{ea(ops[0])})")
        elif mn in ("addx", "subx"):
            call(f"mw_{mn}(m,st,{size},{ea(ops[0])},{ea(ops[1])})")
        elif mn == "ext":
            call(f"mw_ext(st,{size},{dreg(ops[0])})")
        elif mn == "swap":
            call(f"mw_swap(st,{dreg(ops[0])})")
        elif mn in BITOPS:
            bit, dst = ops[0], ops[1]
            # Bit operations on a data register always operate on the full
            # 32 bits, even though the disassembler omits the .l suffix.
            bsize = 4 if dst.mode == "dreg" else 1
            if bit.mode == "dreg":
                call(f"mw_bitop(m,st,{BITOPS[mn]},{bsize},{ea(dst)},st->d[{dreg(bit)}],1)")
            else:
                call(f"mw_bitop(m,st,{BITOPS[mn]},{bsize},{ea(dst)},{bit.value & 0xFFFFFFFF}u,0)")
        elif mn in SHIFTS:
            op = SHIFTS[mn]
            if len(ops) == 1:
                call(f"mw_shift(m,st,{op},{size or 2},{ea(ops[0])},1,0)")
            else:
                cnt, dst = ops[0], ops[1]
                if cnt.mode == "dreg":
                    call(f"mw_shift(m,st,{op},{size or 2},{ea(dst)},st->d[{dreg(cnt)}],1)")
                else:
                    call(f"mw_shift(m,st,{op},{size or 2},{ea(dst)},{cnt.value & 0xFFFFFFFF}u,0)")
        elif mn in ("mulu", "muls", "divu", "divs"):
            call(f"mw_{mn}(m,st,{size or 2},{ea(ops[0])},{dreg(ops[1])})")
        elif mn == "jmp":
            call(f"mw_jmp(m,st,{ea(ops[0])})", fall=False)
        elif mn == "jsr":
            call(f"mw_jsr(m,st,{ea(ops[0])},{next_pc}u)", fall=False)
        elif mn == "bsr":
            call(f"mw_bsr(m,st,{branch_target & 0xFFFFFFFF}u,{next_pc}u)", fall=False)
        elif mn == "bra":
            out.append(f"    st->cycles += {cycles};")
            if set_pc:
                out.append(f"    st->pc = {branch_target & 0xFFFFFFFF}u;")
        elif mn == "bcc":
            call(f"st->cycles += mw_bcc(st,{condition(ins)},{branch_target & 0xFFFFFFFF}u,{next_pc}u,{cycles},{1 if ins.length == 2 else 0})",
                 fall=False, cyc="0")
        elif mn == "dbcc":
            call(f"st->cycles += mw_dbcc(st,{condition(ins)},{dreg(ops[0])},{branch_target & 0xFFFFFFFF}u,{next_pc}u)",
                 fall=False, cyc="0")
        elif mn == "scc":
            call(f"mw_scc(m,st,{condition(ins)},{ea(ops[0])})")
        elif mn == "rts":
            call("mw_rts(m,st)", fall=False)
        elif mn == "rte":
            call("mw_rte(m,st)", fall=False)
        elif mn == "stop":
            call(f"mw_stop(st,{ops[0].value & 0xFFFFFFFF}u)")
        elif mn == "nop":
            call("")
        elif mn == "movem":
            mask = reglist_mask(ops[0] if ops[0].mode == "reglist" else ops[1])
            data = ops[1] if ops[0].mode == "reglist" else ops[0]
            to_regs = 1 if ops[0].mode == "reglist" and ops[1].mode != "reglist" else 0
            # MOVEM <reglist>,<ea> is a store; MOVEM <ea>,<reglist> is a load.
            to_regs = 1 if ops[0].mode != "reglist" else 0
            call(f"mw_movem(m,st,{size or 2},{ea(data)},0x{mask:04X},{to_regs})")
        elif mn == "movep":
            if ops[0].mode == "disp":
                call(f"mw_movep(m,st,{size or 2},{areg(ops[0])},{ops[0].disp},{dreg(ops[1])},0)")
            else:
                call(f"mw_movep(m,st,{size or 2},{areg(ops[1])},{ops[1].disp},{dreg(ops[0])},1)")
        elif mn == "exg":
            a, b = ops[0], ops[1]
            if a.mode == "dreg" and b.mode == "dreg":
                mode = 0
            elif a.mode == "areg" and b.mode == "areg":
                mode = 1
            else:
                mode = 2
            rx = areg(a) if a.mode == 'areg' else dreg(a)
            ry = areg(b) if b.mode == 'areg' else dreg(b)
            call(f"mw_exg(st,{mode},{rx},{ry})")
        elif mn == "abcd":
            call(f"mw_abcd(m,st,{ea(ops[0])},{ea(ops[1])})")
        elif mn == "sbcd":
            call(f"mw_sbcd(m,st,{ea(ops[0])},{ea(ops[1])})")
        elif mn == "cmpm":
            a, b = ops[0], ops[1]
            call(f"mw_cmpm(m,st,{size or 2},{areg(a)},{areg(b)})")
        else:
            raise ValueError(f"unsupported mnemonic {mn}")
    except Exception as exc:
        unsupported[mn] += 1
        return None, str(exc)
    if not body_only:
        out.append("}")
    return "\n".join(out), None


def decrypt_window(fd, encrypted, state, pc):
    base = pc & 0xFFFFF
    buf = bytearray(32)
    for off in range(0, 32, 2):
        addr = base + off
        val = (encrypted[addr] << 8) | encrypted[addr + 1] if addr + 1 < len(encrypted) else 0xFFFF
        dec = fd.decrypt_word(addr, val, state, False)
        buf[off] = dec >> 8
        buf[off + 1] = dec & 0xFF
    return bytes(buf)


def is_state_change(ins):
    if ins.mnemonic != "cmpi" or ins.size != "l" or len(ins.operands) != 2:
        return None
    imm, dst = ins.operands
    if imm.mode != "immediate" or dst.mode != "dreg" or dst.reg != "D0":
        return None
    if (imm.value & 0xFFFF) != 0xFFFF:
        return None
    return (imm.value >> 16) & 0xFF


def pc_index_targets(ins, pc):
    """Table targets for jmp/jsr (d8,PC,Dn), scanned conservatively."""
    if not any(op.mode == "pc_index" for op in ins.operands):
        return []
    op = next(o for o in ins.operands if o.mode == "pc_index")
    base = (pc + op.base + op.disp) & 0xFFFFFFFF
    out = []
    for step in (2, 4):
        for idx in range(0, 65, step):
            target = (base + idx) & 0xFFFFFFFF
            if 0 <= target < 0x80000 and (target & 1) == 0 and abs(target - pc) < 0x4000:
                out.append(target)
    return sorted(set(out))


def decode_key(fd, encrypted, state, pc):
    buf = decrypt_window(fd, encrypted, state, pc)
    ins = dis.decode(dis.FlatMemory(buf), 0)
    return ins, (buf[0] << 8) | buf[1]


def static_closure(fd, encrypted, seeds, decoded, verbose=True):
    """Close the translated set over direct control flow, vectors, FD1094
    state changes and PC-indexed jump tables.  This is reachability from the
    observed code, not a blind linear sweep, so data is not decoded as code
    unless a real control-flow edge points at it."""
    work = list(seeds)
    seen = set(seeds)
    added = 0

    def add(state, pc):
        nonlocal added
        if state < 0 or state > 0xFF or pc < 0 or pc >= 0x80000 or (pc & 1):
            return
        key = (state, pc)
        if key in seen:
            return
        seen.add(key)
        work.append(key)
        added += 1

    # Hardware vectors: reset runs under the boot key, interrupt handlers under
    # the FD1094's interrupt key (0x39 on this set).
    reset_pc = int.from_bytes(encrypted[4:8], "big")
    add(0x00, reset_pc)
    for vec in range(0x64, 0x80, 4):
        target = int.from_bytes(encrypted[vec:vec + 4], "big")
        if 0x400 <= target < 0x80000 and (target & 1) == 0:
            add(0x39, target)

    while work:
        state, pc = work.pop()
        key = (state, pc)
        if key not in decoded:
            ins, opcode = decode_key(fd, encrypted, state, pc)
            decoded[key] = (ins, opcode)
        ins, opcode = decoded[key]
        if ins.flow == dis.FLOW_INVALID or ins.mnemonic.startswith("dc.") or ins.length == 0:
            continue

        next_pc = (pc + ins.length) & 0xFFFFFFFF
        new_state = is_state_change(ins)
        if new_state is not None:
            add(new_state, next_pc)
            continue

        mn = ins.mnemonic
        if mn in ("bcc", "dbcc", "bra", "bsr"):
            target = (pc + ins.target) & 0xFFFFFFFF
            add(state, target)
            if mn != "bra":
                add(state, next_pc)
        elif mn in ("jmp", "jsr"):
            # JMP/JSR targets are absolute for absolute EAs, but relative to
            # the instruction for (d16,PC) EAs.
            if ins.target is not None:
                op = ins.operands[0]
                target = (pc + ins.target) if op.mode == "pc_disp" else ins.target
                add(state, target & 0xFFFFFFFF)
            else:
                for target in pc_index_targets(ins, pc):
                    add(state, target)
            if mn == "jsr":
                add(state, next_pc)
        elif mn in ("rts", "rte", "stop"):
            pass
        else:
            add(state, next_pc)

    if verbose:
        print(f"static closure added {added} entries (total {len(seen)})")
    return seen


TERMINATORS = {"bcc", "bra", "bsr", "jmp", "jsr", "rts", "rte", "stop", "dbcc"}


def control_targets(ins, pc):
    mn = ins.mnemonic
    if mn in ("bcc", "dbcc", "bra", "bsr"):
        return [(pc + ins.target) & 0xFFFFFFFF]
    if mn in ("jmp", "jsr"):
        if ins.target is not None:
            op = ins.operands[0]
            return [((pc + ins.target) if op.mode == "pc_disp" else ins.target) & 0xFFFFFFFF]
        return pc_index_targets(ins, pc)
    return []


def build_basic_blocks(decoded, emitted):
    starts = set()
    has_pred = set()
    for key in emitted:
        state, pc = key
        ins, _ = decoded[key]
        ns = is_state_change(ins)
        if ns is not None:
            starts.add((ns, pc + ins.length))
            continue
        mn = ins.mnemonic
        if mn in TERMINATORS:
            for target in control_targets(ins, pc):
                starts.add((state, target))
            if mn in ("bcc", "dbcc", "bsr", "jsr"):
                starts.add((state, pc + ins.length))
        else:
            nxt = (state, pc + ins.length)
            if nxt in emitted:
                has_pred.add(nxt)
    for key in emitted:
        if key not in has_pred:
            starts.add(key)
    starts &= emitted

    assigned = set()
    blocks = []

    def walk(start):
        keys = []
        key = start
        while key in emitted and key not in assigned:
            keys.append(key)
            assigned.add(key)
            state, pc = key
            ins, _ = decoded[key]
            if is_state_change(ins) is not None or ins.mnemonic in TERMINATORS:
                break
            nxt = (state, pc + ins.length)
            if nxt not in emitted or nxt in starts:
                break
            key = nxt
        if keys:
            blocks.append((start, keys))

    for start in sorted(starts):
        if start not in assigned:
            walk(start)
    for key in sorted(emitted):
        if key not in assigned:
            walk(key)
    return blocks, starts


def sweep_seeds(fd, encrypted, max_pc=0x21000):
    """ROM-wide direct-control-flow discovery.  Restricts itself to the code
    image (below max_pc) and to targets within +-0x20000 of the instruction,
    so data tables after the program are not decoded as code."""
    seeds = set()
    for state in (0x00, 0x39, 0x51, 0xAA, 0xCC):
        rom = fd.decrypt_rom(encrypted, state)
        mem = dis.FlatMemory(rom)
        limit = min(max_pc, len(rom) - 8)
        for pc in range(0, limit, 2):
            ins = dis.decode(mem, pc)
            if ins.flow == dis.FLOW_INVALID or ins.mnemonic.startswith("dc."):
                continue
            if ins.mnemonic in TERMINATORS:
                for target in control_targets(ins, pc):
                    if 0x400 <= target < max_pc and (target & 1) == 0 and abs(target - pc) < 0x20000:
                        seeds.add((state, target))
    return seeds


def main():
    trace_paths = []
    sweep = True
    sweep_limit = 0x21000
    args = sys.argv[1:]
    i = 0
    while i < len(args):
        if args[i] == "--no-sweep":
            sweep = False
            i += 1
        elif args[i] == "--sweep-limit" and i + 1 < len(args):
            sweep_limit = int(args[i + 1], 0)
            i += 2
        else:
            trace_paths.append(args[i])
            i += 1
    if not trace_paths:
        trace_paths = ["build/trace.txt"]
    files = mwalk.load_set(str(ROOT / "mwalk.zip"))
    encrypted = mwalk.build_main_rom(files)
    fd = mwalk.FD1094(files["317-0159.key"])

    entries = set()
    for path in trace_paths:
        for line in Path(path).read_text().splitlines():
            if line.strip():
                st, pc = line.split()
                entries.add((int(st, 16), int(pc, 16)))

    decoded = {}
    if sweep:
        extra = sweep_seeds(fd, encrypted, sweep_limit)
        print(f"ROM sweep added {len(extra)} direct-control-flow seeds "
              f"(limit {sweep_limit:06X})")
        entries |= extra
    entries = static_closure(fd, encrypted, entries, decoded)

    unsupported = Counter()
    functions = []
    table = []
    emitted = set()
    for state, pc in sorted(entries):
        if (state, pc) not in decoded:
            decoded[(state, pc)] = decode_key(fd, encrypted, state, pc)
        ins, opcode = decoded[(state, pc)]
        if ins.flow == dis.FLOW_INVALID or ins.mnemonic.startswith("dc."):
            unsupported["invalid"] += 1
            continue
        text, err = emit(ins, state, pc, opcode, unsupported)
        if text:
            functions.append(text)
            table.append((state, pc, f"g_{state:02X}_{pc:06X}"))
            emitted.add((state, pc))

    blocks, _starts = build_basic_blocks(decoded, emitted)
    block_names = {start: f"b_{start[0]:02X}_{start[1]:06X}" for start, _ in blocks}
    block_insns = {start: len(keys) for start, keys in blocks}

    block_prototypes = []
    block_functions = []
    for start, keys in blocks:
        state0, pc0 = start
        name = block_names[start]
        lines = [f"/* basic block {state0:02X}:{pc0:06X} ({len(keys)} instructions) */",
                 f"static void {name}(s18_t *m, mw68k_t *st)", "{"]
        for i, key in enumerate(keys):
            st, pc = key
            ins, opcode = decoded[key]
            final = (i == len(keys) - 1)
            body, err = emit(ins, st, pc, opcode, unsupported, body_only=True, set_pc=final)
            if body:
                lines.append(body)
            else:
                raise RuntimeError(f"block emit failed at {st:02X}:{pc:06X}: {err}")

        lstate, lpc = keys[-1]
        lins, _ = decoded[keys[-1]]
        lmn = lins.mnemonic
        if is_state_change(lins) is not None:
            pass
        elif lmn in ("bcc", "dbcc"):
            tgt = (lpc + lins.target) & 0xFFFFFFFF
            nt = (lpc + lins.length) & 0xFFFFFFFF
            if (lstate, tgt) in block_names:
                lines.append(f"    if (st->pc == {tgt}u) st->link_fn = {block_names[(lstate, tgt)]};")
            if (lstate, nt) in block_names:
                lines.append(f"    if (st->pc == {nt}u) st->link_fn = {block_names[(lstate, nt)]};")
        elif lmn == "bra":
            tgt = (lpc + lins.target) & 0xFFFFFFFF
            if (lstate, tgt) in block_names:
                lines.append(f"    st->link_fn = {block_names[(lstate, tgt)]};")
        elif lmn in ("bsr",):
            tgt = (lpc + lins.target) & 0xFFFFFFFF
            if (lstate, tgt) in block_names:
                lines.append(f"    st->link_fn = {block_names[(lstate, tgt)]};")
        elif lmn in ("jmp", "jsr"):
            if lins.target is not None:
                op = lins.operands[0]
                tgt = ((lpc + lins.target) if op.mode == "pc_disp" else lins.target) & 0xFFFFFFFF
                if (lstate, tgt) in block_names:
                    lines.append(f"    st->link_fn = {block_names[(lstate, tgt)]};")
        elif lmn in ("rts", "rte", "stop"):
            pass
        else:
            nxt = (lstate + 0, (lpc + lins.length) & 0xFFFFFFFF)
            if nxt in block_names:
                lines.append(f"    st->link_fn = {block_names[nxt]};")

        lines.append("}")
        block_prototypes.append(f"static void {name}(s18_t *m, mw68k_t *st);")
        block_functions.append("\n".join(lines))

    out = []
    out.append("/* Generated from the user's own ROM by tools/gen_recomp.py. */")
    out.append("/* This file contains translated game code and must not be redistributed. */")
    out.append('#include "recomp.h"')
    out.append("")
    out.extend(functions)
    out.append("")
    out.extend(block_prototypes)
    out.append("")
    out.extend(block_functions)
    out.append("")
    out.append("const mw_block_t mw_blocks[] = {")
    for state, pc, fn in table:
        out.append(f"    {{0x{(state << 24) | pc:08X}u, {fn}, 1}},")
    out.append("};")
    out.append(f"const unsigned mw_block_count = {len(table)};")
    out.append("")
    out.append("const mw_block_t mw_bb_blocks[] = {")
    for start, _ in blocks:
        state, pc = start
        out.append(f"    {{0x{(state << 24) | pc:08X}u, {block_names[start]}, {block_insns[start]}}},")
    out.append("};")
    out.append(f"const unsigned mw_bb_block_count = {len(blocks)};")
    out.append("")

    target = ROOT / "src" / "generated_game.c"
    target.write_text("\n".join(out), encoding="utf-8")
    print(f"generated {len(table)} instruction blocks and {len(blocks)} basic blocks "
          f"from {len(entries)} reachable PCs -> {target}")
    if unsupported:
        print("unsupported:", dict(unsupported))
        print("those PCs will fall back to the interpreter")


if __name__ == "__main__":
    main()

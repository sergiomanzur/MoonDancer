# Porting notes

These are the hardware/program facts recovered while bringing the game up
against MAME 0.289. They are the basis for the static-recompilation step.

## ROM and CPU

| Region | Files | Size |
| --- | --- | --- |
| 68000 | `epr-13235.a6` (even), `epr-13234.a5` (odd) | 512 KiB |
| FD1094 key | `317-0159.key` | 8 KiB |
| tiles | `mpr-13216.b1`, `mpr-13217.b2`, `mpr-13218.b3` | 768 KiB (3 planes) |
| sprites | `mpr-13231.a11/13224.b11`, `mpr-13230.a10/13223.b10`, `mpr-13229.a9/13222.b9`, `epr-13228.a8/13221.b8` | 2 MiB |
| Z80 | `epr-13225.a4`, `mpr-13219.b4`, `mpr-13220.b5`, `mpr-13249.b6` | 2 MiB region |
| MCU | `315-5437.ic4` | 4 KiB (8051 reset vector `ljmp 0x30`) |

Main crystal: 68000 at 10 MHz. Screen is 320x224 visible at 57.23 Hz
(`342x262` total), so a frame is ~174,736 68000 cycles and ~139,787 Z80
cycles at 8 MHz.

## FD1094

The 68000 is Hitachi's FD1094. Instruction fetches are decrypted; data
fetches see the original ROM bytes. The port implements MAME's
`fd1094.cpp` algorithm exactly (`src/fd1094.c`).

- Reset state is `0x00`; the first instructions are a chain of
  `CMPI.L #$xxFFFF,D0` (`0x0C80`) which select new keys (`0xCC`, `0xAA`, ...).
- While servicing an interrupt the chip uses key `key[0]` = `0x39`; `RTE`
  returns to the previous key.
- A `CMPI.L #$xxFFFF,D0` changes to state `xx`; `CMPI` with other immediates
  is ordinary comparison.
- The same PC can therefore contain different instructions in different
  states. A correct static recompiler must key blocks by `(state, PC)`, not
  PC alone. The runtime currently decrypts a 512 KiB view lazily for every
  state used and fetches from it.

## Memory map observed at runtime

The 315-5195 mapper is programmed by the game; the addresses below are the
ones Moonwalker actually uses (confirmed by MAME write taps and by
byte-identical RAM comparisons):

| Address | Device |
| --- | --- |
| `0x000000-0x07FFFF` | main program ROM (data reads encrypted, opcodes decrypted) |
| `0x400000-0x40FFFF` | tile RAM (16 pages x 64x32 16-bit entries) |
| `0x410000-0x410FFF` | text RAM (64x28 cells + scroll latches at `0xE80`) |
| `0x440000-0x4407FF` | System 16B sprite RAM (128 entries x 8 words) |
| `0x840000-0x840FFF` | palette RAM (2048 16-bit entries) |
| `0xC00000-0xC00FFF` | 315-5313 VDP (16-byte register window, mirror) |
| `0xE40000-0xE43FFF` | misc I/O window (315-5296 at low word offsets) |
| `0xFFC000-0xFFFFFF` | 16 KiB work RAM |

MAME's mapper `mirror` values are easy to misread. In particular `0xFE0020`
is **not** a tile-RAM alias; treating it as one adds a bogus 16-word tile
table and draws garbage in the top-left of the screen. It is in unmapped
space/mapper-register space.

I/O word offsets (`(addr - 0xE40000) >> 1`):

- `0x000-0x007` — 315-5296 ports A-H (P1, P2, P3, misc, service, coinage,
  DSW, tile banks)
- `0x008-0x00B` — SEGA signature
- `0x00E` — CNT register (CNT1 = tilemap display enable, CNT2 = VDP enable;
  in MAME these are driven by the MCU, not by the 68000)
- `0x1000` — video control latch: `vdp_mixing`

`vdp_mixing` is `(layer << 1) | priority_enable`; Moonwalker uses layers 2
and 3 (`0x04`, `0x05`, `0x07`).

## Video

System 18 has two scrolling tilemap layers, a text layer and System 16B
sprites, plus the VDP.

- `tilemap_draw(..., which=0/1, map=TILEMAP_FOREGROUND/BACKGROUND, ...)`:
  `which` is the **category** (tile word bit 15), `map` selects the
  **scroll/page set** (foreground -> set 0, background -> set 1). Getting
  this backwards hides the whole isometric city behind a flat backdrop.
- Tile words: bit 15 category, bits 14-6 palette (7 bits), bits 5-0? code
  low 13 bits. Code is banked in 1024-tile banks via the I/O port H writes
  (`rom_5874_bank_w`).
- Tile ROM layout is `gfx_8x8x3_planar`: three separate 256 KiB bitplanes,
  not 24-byte interleaved tiles. Plane 2 (bit 2) is at `+0x80000`, plane 1
  at `+0x40000`, plane 0 at `+0`.
- Text layer: 64x28, `scrolldx = -192`, so screen x maps to text column
  `(x + 192) / 8`; only the rightmost 40 columns are visible.
- Sprite entry: `+0` top/bottom, `+2` x position (origin 184), `+4`
  hide/flip/pitch, `+6` address, `+8` bank/priority/color, `+A` vzoom/hzoom.
  Sprites are drawn in RAM order; priority is `(1 << sprite_priority) >
  current_priority`.
- Palette: `sBGR BBBB GGGG RRRR`; index 0-1023 tilemaps, 0x400-0x7FF
  sprites, 0x800-0xFFF shadow, 0x1000-0x103F VDP CRAM.

At frame 2400 the tile RAM and text RAM dumps match MAME byte-for-byte
(text RAM differs only in the scroll latches by a few pixels because our
frame boundary is slightly different), confirming the bus/mapping model.

## MCU

The 315-5437 is an 8751 with 4 KiB of program and 128 bytes of RAM. It is
not just a lock: it owns the sound mailbox and the 68000 vblank IRQ.

The port runs the real 8051 program with the MIT emu8051 core. Its `MOVX`
bus is the 315-5195 mapper:

- `INT0` is pulsed once per frame (vblank). The handler writes mapper reg
  4 = `0x0B`, which is the negative-logic IRQ4 request to the 68000.
- `INT1` is pulsed when the Z80 writes its sound reply byte.
- The main CPU and MCU exchange a 16-bit mailbox in work RAM at
  `0xFFEDFE`; the MCU reads it with the mapper's DMA read sequence
  (`reg7/8/9`, `reg5 = 2`), processes it, and writes the response back with
  `regA/B/C`, `reg5 = 1`.
- Sound command IDs are written to mapper reg 3 and forwarded to the Z80 as
  an NMI. The 68000's own `reg3` writes come from a scratch table written at
  `0xFE0020` by the vblank handler and must not be forwarded; doing so
  overwrites the MCU's command in the one-byte latch. This scheduling detail
  was the difference between silence and music.

The MCU and sound CPU are now run in slices interleaved with the 68000
(`mw_m68k_run`), so the one-byte sound latch is serviced before the next
writer can overwrite it.

## Static recompilation implementation

1. Record executed `(FD1094 key, PC)` pairs from the interpreter, then close
   over direct branches, calls, fallthroughs, interrupt vectors, FD1094 key
   changes (`CMPI.L #$xxFFFF,D0`) and PC-indexed jump tables.
2. Additionally sweep the code image (`0x000000-0x20FFFF`) for direct
   branch/call/jump targets. This covers functions that randomized gameplay
   did not reach. The sweep is deliberately limited to the code image so
   graphics/sound data is not decoded as code.
3. Group the reachable instructions into basic blocks. Blocks end at
   branches, jumps, returns, `RTE`, `STOP`, unsupported instructions and
   FD1094 key changes. Every branch target and function entry starts a block.
4. Emit one C function per instruction for lockstep tests, and one C function
   per basic block for the runtime. Block functions inline their instructions
   and finish by setting `st->link_fn` to the statically known successor.
5. Dispatch with a sorted `(state << 24) | PC` table. A linked block is called
   directly without a table lookup; unlinked/indirect entries fall back to
   the single-instruction table, and only a true miss goes to Musashi.
6. Use Musashi as the reference: instruction-level and block-level lockstep
   compare registers, SR, FD1094 state, RAM and exact cycles.

## Cycle and IRQ fidelity

- Bcc uses the bus base cost for the taken case; a not-taken byte branch is
  two cycles cheaper and a not-taken word branch two cycles dearer. DBcc is
  12 cycles for condition true, 10 for a taken loop and 14 on expiry.
- Shifts add `count * 2`; MULU adds `2 * popcount(operand)`; MULS adds the
  alternating-bit cost; MOVEM adds 2/3 cycles per moved word/long.
- The instruction-level selftest compares the lifted cycle counter with the
  cycles returned by `m68k_execute(1)` after every instruction. It passes
  1,000,000 instructions exactly. The block-level test does the same per
  basic block.
- IRQ4 is sampled at `m68k_execute`-style chunk boundaries, only when
  `SR & 0x0700 < 0x0400`. The 8751 keeps running while the 68000 is stopped.
- The 8751's vblank INT0 is raised at scanline 224 of 262, not at the frame
  boundary. `s18_run_frame` therefore runs 149,392 visible cycles, pulses
  INT0 and sets the VDP vblank status, then runs the remaining 25,344
  vblank cycles. This matches MAME's screen vblank point.
- Coin inputs are edge-latched by the IRQ handler into `$E0CE`. The game
  ignores coins during the early boot notices; in MAME and this runtime the
  first accepted coin is around frame 900. A coin pulse at frame 940
  increments `$E000`, and a start pulse at frame 1000 consumes it and sets
  `$E036` to `0x18`.

## Coverage-guided expansion

`tools/expand_trace.py` runs the trace-enabled build with deterministic
random inputs, records `REC MISS` fallbacks, merges the coverage traces,
regenerates, rebuilds and repeats. The current trace converged with zero
misses over 40 seeds and a 50,000-frame run.

## Deterministic replay and hashing

`mwalk_headless` accepts `--record-input FILE`, `--replay FILE`,
`--hash-file FILE` and `--random SEED`. The hash covers work RAM (excluding
the stack scratch area), tile/text/sprite/palette RAM, VDP VRAM/CRAM/VSRAM,
VDP registers and the FD1094 state. Record -> replay produces byte-identical
hash files.

## Current limitations and parity

- The translated 68000 and Musashi lockstep exactly at 1,000,000
  instructions, including per-instruction and per-block cycles. At frame
  1200 the compiled and interpreted work RAM differ by 4 bytes (two dead
  stack slots); palette RAM is identical.
- With a fresh NVRAM and matching coin/start read taps, the port matches
  MAME's game logic, tile RAM, text RAM and palette. MAME's
  `register_frame_done` callback samples at the end of a frame, so port
  frame N corresponds to MAME callback N+1. At aligned frames the remaining
  work-RAM difference is about 73-83 bytes: mostly stack/scratch plus a
  handful of live animation cells. Tile/text RAM is byte-identical.
- Rendered frames are pixel-identical to MAME at frames 0, 1, 100, 947,
  1200, 1210 and 1250 (320x224 RGB, compared against MAME's AVI output).
- The earlier reported one-step `$E031` lag was a frame-callback sampling
  offset, not a game-logic lag. The full sequence matches MAME with a
  constant one-callback shift.
- Demo-mode P1 input values are identical at the game's read points
  (`P1=$5E` at frame 1100, etc.). The remaining difference is the intro
  task transition: MAME switches slot 0 from task `$5096` to `$49BC`
  (intro timer `$12`) by frame 1200, while both the native and Musashi
  paths stay on `$5096` (timer `$1F4`). The transition flips if the P1
  demo starts at frame 1104 instead of 1100, but the player state is then
  shifted, so a simple input offset is not sufficient. This is a 4-frame
  input-pipeline / task-order difference in the board model, not a
  translated-68000 error.
- Work RAM is NVRAM-backed on the real board. The SDL build loads/saves
  `nvram/mwalk/nvram`; `--nvram FILE` / `--no-nvram` override it. Use a
  fresh image for comparisons, since a stale MAME NVRAM made the earlier
  port-vs-MAME diff look like ~1.5 KiB.
- A recompiler bug was found in the palette fade: `BSET D7,D4` is a 32-bit
  register bit operation, but the generator emitted a byte-sized bitop
  because the disassembler omits the `.l` suffix. Bits 8-15 were lost,
  producing `$0FFF` instead of `$7FFF` in the palette shadow buffer.
  Register bit operations now always use 32 bits (memory bit operations
  remain byte-sized), and the immediate-bit mask is 0-31 for registers.
- `mwalk_headless --workhash FILE` writes an FNV-1a hash of the live
  0x0000-0x3DFF work RAM every frame. This was the tool that isolated the
  callback sampling offset and the reset/vblank phase.
- A computed jump/call target that was never observed and is not in a
  discovered jump table can still fall back to Musashi. The translated
  program remains derived from the user's ROM and is not redistributed.

### MCU and vblank timing

- The 8751's vblank INT0 is delivered at scanline 224 (149,392 68000
  cycles), not at the frame boundary. This was the main phase bug: the
  MCU's IRQ4 task previously ran about 15 ms early.
- The 315-5195 mapper's reset/halt register is modelled. Writing `$3`
  holds the 68000 in reset; releasing it restarts from the
  FD1094-decrypted reset vector and returns the FD1094 state to `0`. The
  MCU holds the CPU for about 3.1 ms during boot.
- Frame scheduling uses absolute cycle targets. Overshoot from a translated
  basic block is carried into the next frame, so the 68000 cannot gain time
  over long runs.
- `s18_mcu_run` carries the fractional 10 MHz -> 8 MHz clock and the
  12-clock machine-cycle remainder across slices exactly.
- The 8751 state matches MAME at frame 1200: PC `0x16A`, SP `0x60`, DPTR
  `0x173`, IE `0xE5`, IP `0xE4`, TCON `5`, TMOD `0x10`, timers 0,
  P0-P3 `0xFF`. Only 7 bytes of dead stack/scratch internal RAM differ.
- The 315-5296 replays a port's latched output when its direction changes,
  matching MAME's port-write callback behavior.

### VDP note

- VDP enable is driven by CNT2 (bit 2) of the 315-5296, as on the board.
  The 315-5313 layer is now composited only when that bit is set. The old
  hardcoded `frame_count > 900` gate was removed; it was only masking the
  missing CNT2 enable.
- In the tested Moonwalker scenes the VDP planes are blank: R2/R4 point at
  a zeroed nametable and the VDP sprite table is empty. Enabling the real
  CNT2 gate leaves the sampled frames pixel-identical.
- The large comic panels are System 16B tilemap pages, not VDP layers.
  They render identically to MAME once the page-select and scroll latches
  are read from text RAM.
- In gameplay the VDP VRAM is populated (5,316 non-zero bytes at frame
  3000/6000), and the rendered backgrounds match MAME in the compared
  gameplay frames. Player/enemy positions differ in those comparisons
  because the demo input sequence is not bit-identical, not because of a
  rendering difference.
- The VDP sprite attribute table is all zero in the tested gameplay frame,
  so the VDP sprite path is not exercised by Moonwalker. The window plane is
  present, but the sampled frames are pixel-identical to MAME with the
  current plane renderer; the sprite/window paths remain unexercised.

## Static recompilation

The translated program is produced by `tools/gen_recomp.py` from a runtime
trace of `(FD1094 state, PC)` pairs:

- One C function per pair for lockstep testing, plus basic-block functions
  that inline several instructions. Both call the shared helpers in
  `src/recomp.c` for effective addresses, flags, arithmetic and control flow.
- The state is part of the key: the same PC can decode differently in the
  boot, normal and interrupt keys.  `mw_blocks[]` is sorted by
  `(state << 24) | pc` and searched with a binary search.
- Memory accesses still go through the System 18 bus, so the mapper, video
  hardware, MCU mailbox and sound FIFO see the same cycles as the
  interpreter.
- Missing keys return to Musashi for the rest of the frame and are logged as
  `REC MISS`; `tools/expand_trace.py` records another run and adds them to
  the next translation automatically.

Things that had to match the hardware/interpreter exactly:

- Interrupt vectors are read from the **raw data bus** (`s18_read32`), not
  from the FD1094-decrypted instruction view.
- Branch targets from the bundled disassembler are relative to the
  instruction address (`pc + ins.target`), already including the extension
  word.  PC-indexed effective addresses use `pc + extension_word + disp +
  index`.
- IRQ4 must be ignored while `SR & 0x0700 >= 0x0400`.  Taking a nested IRQ4
  inside the handler corrupts the stacked return frame and sends the CPU
  into the vector table.
- A translated `STOP` must stop the block loop immediately.  The 68000 is
  then idle; the 8751 keeps running and raises IRQ4, which wakes it.
- The boot code calibrates CPU speed at `0x0BFE` by counting the `0x0BE2`
  loop until the vblank IRQ decrements `$E02C`, then compares D0 with
  `0x1990`.  Exact Bcc/DBcc/shift/MULU/MULS/MOVEM cycle costs are needed for
  the branch to go the same way as Musashi.
- `CMPI.L #$xxFFFF,D0` changes the FD1094 key; the generated block calls
  `fd1094_cmpi_callback` before updating the PC.  `RTE` calls
  `fd1094_rte`, and IRQ entry calls `fd1094_irq_ack`.

The regression tests are `--selftest N` (lockstep against Musashi from
reset, comparing registers, SR, FD1094 state and RAM after every
instruction) and a long `--play` run with no `REC MISS` lines.

Residual difference: the translated core and Musashi can still disagree on
attract-mode scene boundaries.  The IRQ count is identical, but the exact
instruction at which the vblank MCU mail is sampled shifts the game's task
scheduler by a few frames.  Gameplay, input, video and audio remain
functional; this is a timing-parity issue, not a decoding failure.

# Development tools

These tools are not needed to build or play the port. They document how the
ROM was verified/decrypted and how the native board model was checked
against MAME.

## ROM

```powershell
python tools\verify_rom.py                 # CRC32 check of roms/
python tools\mwalk.py mwalk.zip 0          # decrypt state 0 to build/mwalk_decrypted_00.bin
python tools\mwalk.py mwalk.zip 0x51       # decrypt another FD1094 key state
```

`mwalk.py` also exposes the `FD1094` class and `load_decrypted()` for scripts.

## Disassembly

`disasm_boot.py`, `disasm_states.py`, `disasm_irq.py`, `find_state.py` use
the BSD-licensed m68000 decoder from
[cheviman/arcade-decomp](https://github.com/cheviman/arcade-decomp) (checked
out under `tools/arcade-decomp`). They were used to walk the boot code's
FD1094 state changes and to decode the interrupt handler at key `0x39`.

## MAME oracle

`tools/mame_dist/` is a MAME 0.289 Windows build used only for validation.
`tools/mame_ref/` holds the Lua scripts:

- `capture*.lua` — save a frame at fixed emulated frames.
- `dump_state.lua` — dump CPU/MCU registers and work RAM.
- `dump_writes*.lua` — record distinct writes to the 68000 bus (the write
  taps must be reinstalled after the mapper remaps memory).
- `dump_shares*.lua` — write `:tileram`, `:textram`, `:sprites`,
  `:paletteram`, `:workram` to files for a byte-for-byte comparison.
- `first_frames.lua`, `sprites2400.lua`, `probe_mem.lua` — focused probes.

To run MAME, create `refroms/mwalk.zip` (a copy of your own set) because
MAME expects a zip or a directory named after the set. Then:

```powershell
.\tools\mame_dist\mame.exe mwalk -rompath refroms -video d3d -sound none `
    -nothrottle -seconds_to_run 60 -snapname ref_%i -snapsize 320x224 `
    -autoboot_script tools\mame_ref\capture_attract.lua
```

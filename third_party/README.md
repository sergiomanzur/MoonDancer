# Third-party components

| Component | Version/source | License | Copyright |
| --- | --- | --- | --- |
| Musashi 68000 core | https://github.com/kstenerud/Musashi | MIT | Karl Stenerud |
| z80 | https://github.com/superzazu/z80 (d64fe10a) | MIT | Nicolas Allemand |
| ymfm | https://github.com/aaronsgiles/ymfm | BSD-3-Clause | Aaron Giles |
| emu8051 | https://github.com/jarikomppa/emu8051 | MIT | Jari Komppa |
| SDL2 | https://github.com/libsdl-org/SDL (2.30.11) | zlib | Sam Lantinga et al. |
| MAME reference algorithms | https://github.com/mamedev/mame | BSD-3-Clause | MAMEdev and contributors |

MAME source is not linked into the finished game. It was used as the
authoritative reference for the FD1094 algorithm, the 315-5195 mapper, the
315-5296 I/O chip, the System 16B tilemap/sprite rules and the VDP register
behaviour; those routines were reimplemented in this repository's code, and
the MAME reference scripts under `tools/mame_ref/` are development tools.

The MAME 0.289 binary under `tools/mame_dist/` and the reference sources
under `tools/` are development-only and are not required to build or run the
port.

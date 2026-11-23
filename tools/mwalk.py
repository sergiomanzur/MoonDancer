"""
Moonwalker (Sega System 18) ROM set loading and FD1094 decryption.

The ROM set is read from the user's own mwalk.zip (or an extracted directory).
No game data is stored in this repository.

FD1094 is Hitachi's custom 68000 with a battery-backed decryption key.  The
algorithm here is a direct port of MAME's src/mame/sega/fd1094.cpp
(BSD-3-Clause, Nicola Salmoria / Andreas Naive / Charles MacDonald), so that
the decrypted instruction stream is the one the original silicon fetched.
"""
from __future__ import annotations

import os
import struct
import zlib
from pathlib import Path

MAIN_ROM_SIZE = 0x80000
KEY_SIZE = 0x2000

# MAME ROM_START(mwalk): CRC32 of every file in the set.
ROM_CRCS = {
    "epr-13235.a6": 0x6983E129,
    "epr-13234.a5": 0xC9FD20F2,
    "317-0159.key": 0x507838F0,
    "mpr-13216.b1": 0x862D2C03,
    "mpr-13217.b2": 0x7D1AC3EC,
    "mpr-13218.b3": 0x56D3393C,
    "mpr-13224.b11": 0xC59F107B,
    "mpr-13231.a11": 0xA5E96346,
    "mpr-13223.b10": 0x364F60FF,
    "mpr-13230.a10": 0x9550091F,
    "mpr-13222.b9": 0x523DF3ED,
    "mpr-13229.a9": 0xF40DC45D,
    "epr-13221.b8": 0x9AE7546A,
    "epr-13228.a8": 0xDE3786BE,
    "epr-13225.a4": 0x56C2E82B,
    "mpr-13219.b4": 0x19E2061F,
    "mpr-13220.b5": 0x58D4D9CE,
    "mpr-13249.b6": 0x623EDC5D,
    "315-5437.ic4": 0x4BF63BC1,
}

MASKED_OPCODES = (0x013A, 0x033A, 0x053A, 0x073A, 0x083A, 0x093A, 0x0B3A, 0x0D3A, 0x0F3A)


def _bitswap16(value: int, order: tuple[int, ...]) -> int:
    out = 0
    for src in order:
        out = (out << 1) | ((value >> src) & 1)
    return out


def _build_masked_lookup() -> tuple[list[int], list[int]]:
    aggressive = [bytearray(65536 // 16) for _ in range(2)]
    for table in aggressive:
        for opcode in MASKED_OPCODES:
            table[opcode >> 4] |= 1 << ((opcode >> 1) & 7)
    # the more aggressive table also masks branches and PC-relative operations
    for opcode in range(0, 65536, 2):
        if (opcode & 0xFF80) == 0x4E80 or (opcode & 0xF0F8) == 0x50C8 or (opcode & 0xF000) == 0x6000:
            aggressive[1][opcode >> 4] |= 1 << ((opcode >> 1) & 7)
    return [bytes(t) for t in aggressive]


_MASKED = _build_masked_lookup()


class FD1094:
    """Decrypt FD1094 opcode words for any of the 256 CPU keys."""

    def __init__(self, key: bytes):
        if len(key) != KEY_SIZE:
            raise ValueError(f"FD1094 key must be {KEY_SIZE} bytes, got {len(key)}")
        self.key = key
        self._cache: dict[int, bytes] = {}

    def decrypt_word(self, address: int, value: int, state: int, vector_fetch: bool) -> int:
        """address is the byte address; value the encrypted big-endian 68000 word."""
        key = self.key
        # NOTE: MAME indexes the key with the *word* address.
        word_address = (address >> 1) & 0xFFFFFFFF

        gkey1 = key[1]
        gkey2 = key[2]
        gkey3 = key[3]
        if state & 0x01:
            gkey1 ^= 0x04
            gkey2 ^= 0x80
            gkey3 ^= 0x80
        if state & 0x02:
            gkey1 ^= 0x01
            gkey2 ^= 0x10
            gkey3 ^= 0x01
        if state & 0x04:
            gkey1 ^= 0x80
            gkey2 ^= 0x40
            gkey3 ^= 0x04
        if state & 0x08:
            gkey1 ^= 0x20
            gkey2 ^= 0x02
            gkey3 ^= 0x20
        if state & 0x10:
            gkey1 ^= 0x02
            gkey1 ^= 0x40
            gkey2 ^= 0x08
        if state & 0x20:
            gkey1 ^= 0x08
            gkey3 ^= 0x08
            gkey3 ^= 0x10
        if state & 0x40:
            gkey1 ^= 0x10
            gkey2 ^= 0x20
            gkey2 ^= 0x04
        if state & 0x80:
            gkey2 ^= 0x01
            gkey3 ^= 0x02
            gkey3 ^= 0x40

        if (word_address & 0x0FFC) == 0 and word_address >= 4:
            mainkey = key[(word_address & 0x1FFF) | 0x1000]
        else:
            mainkey = key[word_address & 0x1FFF]

        key_f = (mainkey >> 7) & 1 if (word_address & 0x1000) else (mainkey >> 6) & 1

        if vector_fetch:
            if word_address <= 3:
                gkey3 = 0x00
            if word_address <= 2:
                gkey2 = 0x00
            if word_address <= 1:
                gkey1 = 0x00
                key_f = 0

        global_xor0 = 1 ^ ((gkey1 >> 5) & 1)
        global_xor1 = 1 ^ ((gkey1 >> 2) & 1)
        global_swap2 = 1 ^ ((gkey1 >> 0) & 1)
        global_swap0a = 1 ^ ((gkey2 >> 5) & 1)
        global_swap0b = 1 ^ ((gkey2 >> 2) & 1)
        global_swap3 = 1 ^ ((gkey3 >> 6) & 1)
        global_swap1 = 1 ^ ((gkey3 >> 4) & 1)
        global_swap4 = 1 ^ ((gkey3 >> 2) & 1)

        key_0a = ((mainkey >> 0) & 1) ^ ((gkey3 >> 1) & 1)
        key_0b = ((mainkey >> 0) & 1) ^ ((gkey1 >> 7) & 1)
        key_0c = ((mainkey >> 0) & 1) ^ ((gkey1 >> 1) & 1)
        key_1a = ((mainkey >> 1) & 1) ^ ((gkey2 >> 7) & 1)
        key_1b = ((mainkey >> 1) & 1) ^ ((gkey1 >> 3) & 1)
        key_2a = ((mainkey >> 2) & 1) ^ ((gkey3 >> 7) & 1)
        key_2b = ((mainkey >> 2) & 1) ^ ((gkey1 >> 4) & 1)
        key_3a = ((mainkey >> 3) & 1) ^ ((gkey2 >> 0) & 1)
        key_3b = ((mainkey >> 3) & 1) ^ ((gkey3 >> 3) & 1)
        key_4a = ((mainkey >> 4) & 1) ^ ((gkey2 >> 3) & 1)
        key_4b = ((mainkey >> 4) & 1) ^ ((gkey3 >> 0) & 1)
        key_5a = ((mainkey >> 5) & 1) ^ ((gkey3 >> 5) & 1)
        key_5b = ((mainkey >> 5) & 1) ^ ((gkey1 >> 6) & 1)
        key_6a = ((mainkey >> 6) & 1) ^ ((gkey2 >> 1) & 1)
        key_6b = ((mainkey >> 6) & 1) ^ ((gkey2 >> 6) & 1)
        key_7a = ((mainkey >> 7) & 1) ^ ((gkey2 >> 4) & 1)

        val = value & 0xFFFF
        if val & 0x8000:
            val = _bitswap16(val, (15, 9, 10, 13, 3, 12, 0, 14, 6, 5, 2, 11, 8, 1, 4, 7))
            if not global_xor1 and (~val & 0x0800):
                val ^= 0x3002
            if ~val & 0x0020:
                val ^= 0x0044
            if not key_1b and (~val & 0x0400):
                val ^= 0x0890
            if not global_swap2 and not key_0c:
                val ^= 0x0308
            val ^= 0x6561
            if not key_2b:
                val = _bitswap16(val, (15, 10, 13, 12, 11, 14, 9, 8, 7, 6, 0, 4, 3, 2, 1, 5))

        if val & 0x4000:
            val = _bitswap16(val, (13, 14, 7, 0, 8, 6, 4, 2, 1, 15, 3, 11, 12, 10, 5, 9))
            if not global_xor0 and (val & 0x0010):
                val ^= 0x0468
            if not key_3a and (val & 0x0100):
                val ^= 0x0081
            if not key_6a and (val & 0x0004):
                val ^= 0x0100
            if not key_5b and not key_0b:
                val ^= 0x3012
            val ^= 0x3523
            if not global_swap0b:
                val = _bitswap16(val, (2, 14, 13, 12, 9, 10, 11, 8, 7, 6, 5, 4, 3, 15, 1, 0))

        if val & 0x2000:
            val = _bitswap16(val, (10, 2, 13, 7, 8, 0, 3, 14, 6, 15, 1, 11, 9, 4, 5, 12))
            if not key_4a and (val & 0x0800):
                val ^= 0x010C
            if not key_1a and (val & 0x0080):
                val ^= 0x1000
            if not key_7a and (val & 0x0400):
                val ^= 0x0A21
            if not key_4b and not key_0a:
                val ^= 0x0080
            if not global_swap0a and not key_6b:
                val ^= 0xC000
            val ^= 0x99A5
            if not key_5b:
                val = _bitswap16(val, (15, 14, 13, 12, 11, 1, 9, 8, 7, 10, 5, 6, 3, 2, 4, 0))

        if val & 0xE000:
            val = _bitswap16(val, (15, 13, 14, 5, 6, 0, 9, 10, 4, 11, 1, 2, 12, 3, 7, 8))
            val ^= 0x17FF
            if not global_swap4:
                val = _bitswap16(val, (15, 14, 13, 6, 11, 10, 9, 5, 7, 12, 8, 4, 3, 2, 1, 0))
            if not global_swap3:
                val = _bitswap16(val, (13, 15, 14, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0))
            if not global_swap2:
                val = _bitswap16(val, (15, 14, 13, 12, 11, 2, 9, 8, 10, 6, 5, 4, 3, 0, 1, 7))
            if not key_3b:
                val = _bitswap16(val, (15, 14, 13, 12, 11, 10, 4, 8, 7, 6, 5, 9, 1, 2, 3, 0))
            if not key_2a:
                val = _bitswap16(val, (13, 14, 15, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0))
            if not global_swap1:
                val = _bitswap16(val, (15, 14, 13, 12, 9, 8, 11, 10, 7, 6, 5, 4, 3, 2, 1, 0))
            if not key_5a:
                val = _bitswap16(val, (15, 14, 13, 12, 11, 10, 9, 8, 4, 5, 7, 6, 3, 2, 1, 0))
            if not global_swap0a:
                val = _bitswap16(val, (15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 0, 3, 2, 1))

        val = _bitswap16(val, (12, 15, 14, 13, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0))

        if (val & 0xB080) == 0x8000:
            val ^= 0x4000
        if (val & 0xF000) == 0xC000:
            val ^= 0x0080
        if (val & 0xB100) == 0x0000:
            val ^= 0x4000

        if (_MASKED[key_f][val >> 4] >> ((val >> 1) & 7)) & 1:
            val = 0xFFFF
        return val

    def decrypt_rom(self, encrypted: bytes, state: int) -> bytes:
        """Return the 512 KiB decrypted ROM as the 68000 sees it (big-endian bytes)."""
        cached = self._cache.get(state)
        if cached is not None:
            return cached
        if len(encrypted) % 2:
            raise ValueError("encrypted ROM must have an even length")
        out = bytearray(len(encrypted))
        for offset in range(0, len(encrypted), 2):
            value = (encrypted[offset] << 8) | encrypted[offset + 1]
            dec = self.decrypt_word(offset, value, state, offset < 8)
            out[offset] = dec >> 8
            out[offset + 1] = dec & 0xFF
        result = bytes(out)
        self._cache[state] = result
        return result


def load_rom_directory(path: str | os.PathLike) -> dict[str, bytes]:
    path = Path(path)
    if path.suffix.lower() == ".zip":
        import zipfile

        with zipfile.ZipFile(path) as archive:
            return {Path(name).name: archive.read(name) for name in archive.namelist()}
    return {p.name: p.read_bytes() for p in path.iterdir() if p.is_file()}


def load_set(path: str | os.PathLike = "mwalk.zip", verify: bool = True) -> dict[str, bytes]:
    files = load_rom_directory(path)
    if verify:
        missing = sorted(set(ROM_CRCS) - set(files))
        if missing:
            raise SystemExit("ROM set is missing: " + ", ".join(missing))
        bad = []
        for name, expected in ROM_CRCS.items():
            crc = zlib.crc32(files[name]) & 0xFFFFFFFF
            if crc != expected:
                bad.append(f"{name}: {crc:08x} (expected {expected:08x})")
        if bad:
            raise SystemExit("ROM CRC mismatch:\n  " + "\n  ".join(bad))
    return files


def build_main_rom(files: dict[str, bytes]) -> bytes:
    """Interleave the two 68000 ROMs exactly as MAME's ROM_LOAD16_BYTE does.

    MAME loads epr-13235.a6 at even bytes and epr-13234.a5 at odd bytes.
    The resulting big-endian word is (a6 << 8) | a5.
    """
    lo = files["epr-13235.a6"]
    hi = files["epr-13234.a5"]
    if len(lo) != len(hi):
        raise ValueError("main ROM halves differ in size")
    out = bytearray(len(lo) * 2)
    out[0::2] = lo
    out[1::2] = hi
    return bytes(out)


def load_decrypted(path: str | os.PathLike = "mwalk.zip", state: int = 0) -> tuple[bytes, bytes]:
    files = load_set(path)
    encrypted = build_main_rom(files)
    key = files["317-0159.key"]
    return FD1094(key).decrypt_rom(encrypted, state), key


if __name__ == "__main__":
    import sys

    path = sys.argv[1] if len(sys.argv) > 1 else "mwalk.zip"
    state = int(sys.argv[2]) if len(sys.argv) > 2 else 0
    rom, key = load_decrypted(path, state)
    print(f"decrypted {len(rom)} bytes at FD1094 state {state:#04x}")
    print(f"SP = {int.from_bytes(rom[0:4], 'big'):08x}")
    print(f"PC = {int.from_bytes(rom[4:8], 'big'):08x}")
    out = Path("build") / f"mwalk_decrypted_{state:02x}.bin"
    out.parent.mkdir(exist_ok=True)
    out.write_bytes(rom)
    print("wrote", out)

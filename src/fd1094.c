/*
 * Hitachi FD1094 decryption for Michael Jackson's Moonwalker.
 *
 * Ported from MAME's src/mame/sega/fd1094.cpp
 * (BSD-3-Clause, Nicola Salmoria, Andreas Naive, Charles MacDonald).
 * The instruction stream of the FD1094 CPU is encrypted with a key held in
 * battery-backed RAM; data reads are not decrypted.  The chip has 256 keys
 * and switches between them under software control (CMPI.L #$xxFFFF,D0),
 * enters a fixed key (the first key byte) while servicing interrupts, and
 * leaves it on RTE.
 */
#include "fd1094.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define ROM_SIZE 0x80000u
#define KEY_SIZE 0x2000u

static const uint8_t bs_a[16] = {15, 9, 10, 13, 3, 12, 0, 14, 6, 5, 2, 11, 8, 1, 4, 7};
static const uint8_t bs_b[16] = {15, 10, 13, 12, 11, 14, 9, 8, 7, 6, 0, 4, 3, 2, 1, 5};
static const uint8_t bs_c[16] = {13, 14, 7, 0, 8, 6, 4, 2, 1, 15, 3, 11, 12, 10, 5, 9};
static const uint8_t bs_d[16] = {2, 14, 13, 12, 9, 10, 11, 8, 7, 6, 5, 4, 3, 15, 1, 0};
static const uint8_t bs_e[16] = {10, 2, 13, 7, 8, 0, 3, 14, 6, 15, 1, 11, 9, 4, 5, 12};
static const uint8_t bs_f[16] = {15, 14, 13, 12, 11, 1, 9, 8, 7, 10, 5, 6, 3, 2, 4, 0};
static const uint8_t bs_g[16] = {15, 13, 14, 5, 6, 0, 9, 10, 4, 11, 1, 2, 12, 3, 7, 8};
static const uint8_t bs_h[16] = {15, 14, 13, 6, 11, 10, 9, 5, 7, 12, 8, 4, 3, 2, 1, 0};
static const uint8_t bs_i[16] = {13, 15, 14, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0};
static const uint8_t bs_j[16] = {15, 14, 13, 12, 11, 2, 9, 8, 10, 6, 5, 4, 3, 0, 1, 7};
static const uint8_t bs_k[16] = {15, 14, 13, 12, 11, 10, 4, 8, 7, 6, 5, 9, 1, 2, 3, 0};
static const uint8_t bs_l[16] = {13, 14, 15, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0};
static const uint8_t bs_m[16] = {15, 14, 13, 12, 9, 8, 11, 10, 7, 6, 5, 4, 3, 2, 1, 0};
static const uint8_t bs_n[16] = {15, 14, 13, 12, 11, 10, 9, 8, 4, 5, 7, 6, 3, 2, 1, 0};
static const uint8_t bs_o[16] = {15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 0, 3, 2, 1};
static const uint8_t bs_p[16] = {12, 15, 14, 13, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0};

static inline uint16_t bitswap16(uint16_t value, const uint8_t order[16])
{
    uint16_t out = 0;
    for (int i = 0; i < 16; i++)
        out = (uint16_t)((out << 1) | ((value >> order[i]) & 1));
    return out;
}

static const uint16_t masked_opcodes[] = {
    0x013A, 0x033A, 0x053A, 0x073A, 0x083A, 0x093A, 0x0B3A, 0x0D3A, 0x0F3A
};

static uint8_t masked_lookup[2][65536 / 16];

static void build_masked_lookup(void)
{
    memset(masked_lookup, 0, sizeof(masked_lookup));
    for (int table = 0; table < 2; table++)
        for (unsigned i = 0; i < sizeof(masked_opcodes) / sizeof(masked_opcodes[0]); i++) {
            uint16_t op = masked_opcodes[i];
            masked_lookup[table][op >> 4] |= (uint8_t)(1u << ((op >> 1) & 7));
        }
    for (unsigned op = 0; op < 65536; op += 2)
        if ((op & 0xFF80) == 0x4E80 || (op & 0xF0F8) == 0x50C8 || (op & 0xF000) == 0x6000)
            masked_lookup[1][op >> 4] |= (uint8_t)(1u << ((op >> 1) & 7));
}

static uint16_t decrypt_word(const fd1094_t *fd, unsigned address, uint16_t value, uint8_t state, int vector_fetch)
{
    const uint8_t *key = fd->key;
    unsigned word_address = (address >> 1) & 0xFFFFFFFFu;

    uint8_t gkey1 = key[1];
    uint8_t gkey2 = key[2];
    uint8_t gkey3 = key[3];

    if (state & 0x01) { gkey1 ^= 0x04; gkey2 ^= 0x80; gkey3 ^= 0x80; }
    if (state & 0x02) { gkey1 ^= 0x01; gkey2 ^= 0x10; gkey3 ^= 0x01; }
    if (state & 0x04) { gkey1 ^= 0x80; gkey2 ^= 0x40; gkey3 ^= 0x04; }
    if (state & 0x08) { gkey1 ^= 0x20; gkey2 ^= 0x02; gkey3 ^= 0x20; }
    if (state & 0x10) { gkey1 ^= 0x02; gkey1 ^= 0x40; gkey2 ^= 0x08; }
    if (state & 0x20) { gkey1 ^= 0x08; gkey3 ^= 0x08; gkey3 ^= 0x10; }
    if (state & 0x40) { gkey1 ^= 0x10; gkey2 ^= 0x20; gkey2 ^= 0x04; }
    if (state & 0x80) { gkey2 ^= 0x01; gkey3 ^= 0x02; gkey3 ^= 0x40; }

    /* The first four words and their mirrors use a different part of the key. */
    uint8_t mainkey;
    if ((word_address & 0x0FFC) == 0 && word_address >= 4)
        mainkey = key[(word_address & 0x1FFF) | 0x1000];
    else
        mainkey = key[word_address & 0x1FFF];

    uint8_t key_F = (word_address & 0x1000) ? ((mainkey >> 7) & 1) : ((mainkey >> 6) & 1);

    if (vector_fetch) {
        if (word_address <= 3) gkey3 = 0;
        if (word_address <= 2) gkey2 = 0;
        if (word_address <= 1) { gkey1 = 0; key_F = 0; }
    }

    uint8_t global_xor0   = 1 ^ ((gkey1 >> 5) & 1);
    uint8_t global_xor1   = 1 ^ ((gkey1 >> 2) & 1);
    uint8_t global_swap2  = 1 ^ ((gkey1 >> 0) & 1);
    uint8_t global_swap0a = 1 ^ ((gkey2 >> 5) & 1);
    uint8_t global_swap0b = 1 ^ ((gkey2 >> 2) & 1);
    uint8_t global_swap3  = 1 ^ ((gkey3 >> 6) & 1);
    uint8_t global_swap1  = 1 ^ ((gkey3 >> 4) & 1);
    uint8_t global_swap4  = 1 ^ ((gkey3 >> 2) & 1);

    uint8_t key_0a = ((mainkey >> 0) & 1) ^ ((gkey3 >> 1) & 1);
    uint8_t key_0b = ((mainkey >> 0) & 1) ^ ((gkey1 >> 7) & 1);
    uint8_t key_0c = ((mainkey >> 0) & 1) ^ ((gkey1 >> 1) & 1);
    uint8_t key_1a = ((mainkey >> 1) & 1) ^ ((gkey2 >> 7) & 1);
    uint8_t key_1b = ((mainkey >> 1) & 1) ^ ((gkey1 >> 3) & 1);
    uint8_t key_2a = ((mainkey >> 2) & 1) ^ ((gkey3 >> 7) & 1);
    uint8_t key_2b = ((mainkey >> 2) & 1) ^ ((gkey1 >> 4) & 1);
    uint8_t key_3a = ((mainkey >> 3) & 1) ^ ((gkey2 >> 0) & 1);
    uint8_t key_3b = ((mainkey >> 3) & 1) ^ ((gkey3 >> 3) & 1);
    uint8_t key_4a = ((mainkey >> 4) & 1) ^ ((gkey2 >> 3) & 1);
    uint8_t key_4b = ((mainkey >> 4) & 1) ^ ((gkey3 >> 0) & 1);
    uint8_t key_5a = ((mainkey >> 5) & 1) ^ ((gkey3 >> 5) & 1);
    uint8_t key_5b = ((mainkey >> 5) & 1) ^ ((gkey1 >> 6) & 1);
    uint8_t key_6a = ((mainkey >> 6) & 1) ^ ((gkey2 >> 1) & 1);
    uint8_t key_6b = ((mainkey >> 6) & 1) ^ ((gkey2 >> 6) & 1);
    uint8_t key_7a = ((mainkey >> 7) & 1) ^ ((gkey2 >> 4) & 1);

    uint16_t val = value;

    if (val & 0x8000) {
        val = bitswap16(val, bs_a);
        if (!global_xor1 && (~val & 0x0800)) val ^= 0x3002;
        if (~val & 0x0020) val ^= 0x0044;
        if (!key_1b && (~val & 0x0400)) val ^= 0x0890;
        if (!global_swap2 && !key_0c) val ^= 0x0308;
        val ^= 0x6561;
        if (!key_2b) val = bitswap16(val, bs_b);
    }

    if (val & 0x4000) {
        val = bitswap16(val, bs_c);
        if (!global_xor0 && (val & 0x0010)) val ^= 0x0468;
        if (!key_3a && (val & 0x0100)) val ^= 0x0081;
        if (!key_6a && (val & 0x0004)) val ^= 0x0100;
        if (!key_5b && !key_0b) val ^= 0x3012;
        val ^= 0x3523;
        if (!global_swap0b) val = bitswap16(val, bs_d);
    }

    if (val & 0x2000) {
        val = bitswap16(val, bs_e);
        if (!key_4a && (val & 0x0800)) val ^= 0x010C;
        if (!key_1a && (val & 0x0080)) val ^= 0x1000;
        if (!key_7a && (val & 0x0400)) val ^= 0x0A21;
        if (!key_4b && !key_0a) val ^= 0x0080;
        if (!global_swap0a && !key_6b) val ^= 0xC000;
        val ^= 0x99A5;
        if (!key_5b) val = bitswap16(val, bs_f);
    }

    if (val & 0xE000) {
        val = bitswap16(val, bs_g);
        val ^= 0x17FF;
        if (!global_swap4) val = bitswap16(val, bs_h);
        if (!global_swap3) val = bitswap16(val, bs_i);
        if (!global_swap2) val = bitswap16(val, bs_j);
        if (!key_3b) val = bitswap16(val, bs_k);
        if (!key_2a) val = bitswap16(val, bs_l);
        if (!global_swap1) val = bitswap16(val, bs_m);
        if (!key_5a) val = bitswap16(val, bs_n);
        if (!global_swap0a) val = bitswap16(val, bs_o);
    }

    val = bitswap16(val, bs_p);

    if ((val & 0xB080) == 0x8000) val ^= 0x4000;
    if ((val & 0xF000) == 0xC000) val ^= 0x0080;
    if ((val & 0xB100) == 0x0000) val ^= 0x4000;

    if ((masked_lookup[key_F][val >> 4] >> ((val >> 1) & 7)) & 1)
        val = 0xFFFF;

    return val;
}

void fd1094_init(fd1094_t *fd, const uint8_t *key, const uint8_t *encrypted_rom)
{
    memset(fd, 0, sizeof(*fd));
    build_masked_lookup();
    memcpy(fd->key, key, KEY_SIZE);
    fd->encrypted_rom = encrypted_rom;
    fd->state = 0;
    fd->irqmode = 0;
}

void fd1094_reset(fd1094_t *fd)
{
    fd->state = 0;
    fd->irqmode = 0;
}

void fd1094_shutdown(fd1094_t *fd)
{
    for (int i = 0; i < 256; i++) {
        free(fd->roms[i]);
        fd->roms[i] = NULL;
    }
}

void fd1094_change_state(fd1094_t *fd, int newstate)
{
    switch (newstate & 0x300) {
    case 0x0000:
        fd->state = newstate & 0xFF;
        break;
    case 0x0100:
        fd->state = newstate & 0xFF;
        fd->irqmode = 0;
        break;
    case 0x0200:
        fd->irqmode = 1;
        break;
    case 0x0300:
        fd->irqmode = 0;
        break;
    }
}

void fd1094_cmpi_callback(fd1094_t *fd, uint32_t value, int reg)
{
    if (reg == 0 && (value & 0xFFFF) == 0xFFFF)
        fd1094_change_state(fd, (int)(value >> 16));
}

void fd1094_irq_ack(fd1094_t *fd)
{
    fd1094_change_state(fd, 0x0200);
}

void fd1094_rte(fd1094_t *fd)
{
    fd1094_change_state(fd, 0x0300);
}

uint8_t fd1094_state(const fd1094_t *fd)
{
    return fd->irqmode ? fd->key[0] : fd->state;
}

const uint8_t *fd1094_decrypted_rom(fd1094_t *fd, uint8_t state)
{
    uint8_t *out = fd->roms[state];
    if (out)
        return out;

    out = (uint8_t *)malloc(ROM_SIZE);
    if (!out)
        abort();

    for (unsigned off = 0; off < ROM_SIZE; off += 2) {
        uint16_t enc = (uint16_t)((fd->encrypted_rom[off] << 8) | fd->encrypted_rom[off + 1]);
        uint16_t dec = decrypt_word(fd, off, enc, state, off < 8);
        out[off] = (uint8_t)(dec >> 8);
        out[off + 1] = (uint8_t)(dec & 0xFF);
    }
    fd->roms[state] = out;
    return out;
}

uint16_t fd1094_fetch16(fd1094_t *fd, uint32_t address)
{
    const uint8_t *rom = fd1094_decrypted_rom(fd, fd1094_state(fd));
    address &= 0x7FFFF;
    return (uint16_t)((rom[address] << 8) | rom[address + 1]);
}

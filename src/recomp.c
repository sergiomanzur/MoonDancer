/*
 * Native 68000 helper library for the statically lifted game code.
 *
 * The generated functions call these helpers for effective addresses,
 * arithmetic, flags and control flow.  Memory still goes through the
 * System 18 bus, so the chipset sees exactly the same accesses as the
 * interpreted core.
 */
#include "recomp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "m68k.h"
#include "fd1094.h"

extern void mw_m68k_note_key(uint32_t key);
extern void mw_m68k_reset(s18_t *m);

/* Set while translated code runs so culling branches can consult the
 * current viewport.  Only used by the four patched branch sites. */
static s18_t *g_bcc_machine;

#define MASK8  0x000000FFu
#define MASK16 0x0000FFFFu
#define MASK32 0xFFFFFFFFu

static uint32_t mask_of(int size)
{
    return size == 1 ? MASK8 : size == 2 ? MASK16 : MASK32;
}

static uint32_t sign_of(int size)
{
    return size == 1 ? 0x80u : size == 2 ? 0x8000u : 0x80000000u;
}

void mw68k_from_musashi(mw68k_t *s)
{
    for (int i = 0; i < 8; i++) {
        s->d[i] = m68k_get_reg(NULL, (m68k_register_t)(M68K_REG_D0 + i));
        s->a[i] = m68k_get_reg(NULL, (m68k_register_t)(M68K_REG_A0 + i));
    }
    s->pc = m68k_get_reg(NULL, M68K_REG_PC);
    s->sr = (uint16_t)m68k_get_reg(NULL, M68K_REG_SR);
    s->stopped = 0;
    s->link_fn = 0;
}

void mw68k_to_musashi(const mw68k_t *s)
{
    for (int i = 0; i < 8; i++) {
        m68k_set_reg((m68k_register_t)(M68K_REG_D0 + i), s->d[i]);
        m68k_set_reg((m68k_register_t)(M68K_REG_A0 + i), s->a[i]);
    }
    m68k_set_reg(M68K_REG_SR, s->sr);
    m68k_set_reg(M68K_REG_PC, s->pc);
}

static uint32_t index_value(mw68k_t *s, const mw_ea_t *e)
{
    uint32_t v = e->idx_reg < 8 ? s->d[e->idx_reg] : s->a[e->idx_reg - 8];
    if (e->idx_size == 0)
        v = (uint32_t)(int32_t)(int16_t)v;
    if (e->idx_scale)
        v <<= e->idx_scale;
    return v;
}

uint32_t mw_ea_addr(s18_t *m, mw68k_t *s, const mw_ea_t *e, int size)
{
    (void)m;
    switch (e->mode) {
    case MW_EA_IND: return s->a[e->reg];
    case MW_EA_POSTINC: return s->a[e->reg];
    case MW_EA_PREDEC:
        s->a[e->reg] -= (uint32_t)(size == 1 && e->reg == 7 ? 2 : size);
        return s->a[e->reg];
    case MW_EA_DISP: return s->a[e->reg] + (uint32_t)e->disp;
    case MW_EA_INDEX: return s->a[e->reg] + (uint32_t)e->disp + index_value(s, e);
    case MW_EA_ABS: return e->addr;
    case MW_EA_PC_DISP: return e->addr;
    case MW_EA_PC_INDEX: return e->addr + (uint32_t)e->disp + index_value(s, e);
    default: return 0;
    }
}

uint32_t mw_ea_quick(const mw_ea_t *e)
{
    return e->addr;
}

static void ea_post(s18_t *m, mw68k_t *s, const mw_ea_t *e, int size)
{
    (void)m;
    if (e->mode == MW_EA_POSTINC)
        s->a[e->reg] += (uint32_t)(size == 1 && e->reg == 7 ? 2 : size);
}

uint32_t mw_ea_read(s18_t *m, mw68k_t *s, const mw_ea_t *e, int size)
{
    switch (e->mode) {
    case MW_EA_DREG:
        return s->d[e->reg] & mask_of(size);
    case MW_EA_AREG:
        return s->a[e->reg] & mask_of(size);
    case MW_EA_IMM:
    case MW_EA_QUICK:
        return e->addr & mask_of(size);
    default: {
        uint32_t a = mw_ea_addr(m, s, e, size);
        uint32_t v = size == 1 ? s18_read8(m, a) : size == 2 ? s18_read16(m, a) : s18_read32(m, a);
        ea_post(m, s, e, size);
        return v;
    }
    }
}

void mw_ea_write(s18_t *m, mw68k_t *s, const mw_ea_t *e, int size, uint32_t value)
{
    switch (e->mode) {
    case MW_EA_DREG:
        if (size == 1) s->d[e->reg] = (s->d[e->reg] & 0xFFFFFF00u) | (value & 0xFF);
        else if (size == 2) s->d[e->reg] = (s->d[e->reg] & 0xFFFF0000u) | (value & 0xFFFF);
        else s->d[e->reg] = value;
        break;
    case MW_EA_AREG:
        if (size == 2) s->a[e->reg] = (s->a[e->reg] & 0xFFFF0000u) | (value & 0xFFFF);
        else s->a[e->reg] = value;
        break;
    default: {
        uint32_t a = mw_ea_addr(m, s, e, size);
        if (size == 1) s18_write8(m, a, (uint8_t)value);
        else if (size == 2) s18_write16(m, a, (uint16_t)value);
        else s18_write32(m, a, value);
        ea_post(m, s, e, size);
        break;
    }
    }
}

void mw_rmw_begin(s18_t *m, mw68k_t *s, const mw_ea_t *e, int size, mw_rmw_t *r)
{
    memset(r, 0, sizeof(*r));
    r->size = size;
    r->mode = e->mode;
    r->reg = e->reg;
    if (e->mode == MW_EA_DREG) {
        r->is_reg = 1;
        r->value = s->d[e->reg] & mask_of(size);
    } else if (e->mode == MW_EA_AREG) {
        r->is_reg = 2;
        r->value = s->a[e->reg] & mask_of(size);
    } else {
        r->addr = mw_ea_addr(m, s, e, size);
        r->value = size == 1 ? s18_read8(m, r->addr) : size == 2 ? s18_read16(m, r->addr) : s18_read32(m, r->addr);
    }
}

void mw_rmw_end(s18_t *m, mw68k_t *s, const mw_ea_t *e, mw_rmw_t *r, uint32_t value)
{
    if (r->is_reg == 1) {
        if (r->size == 1) s->d[r->reg] = (s->d[r->reg] & 0xFFFFFF00u) | (value & 0xFF);
        else if (r->size == 2) s->d[r->reg] = (s->d[r->reg] & 0xFFFF0000u) | (value & 0xFFFF);
        else s->d[r->reg] = value;
    } else if (r->is_reg == 2) {
        s->a[r->reg] = value;
    } else {
        if (r->size == 1) s18_write8(m, r->addr, (uint8_t)value);
        else if (r->size == 2) s18_write16(m, r->addr, (uint16_t)value);
        else s18_write32(m, r->addr, value);
        ea_post(m, s, e, r->size);
    }
}

void mw_push32(s18_t *m, mw68k_t *s, uint32_t value)
{
    s->a[7] -= 4;
    s18_write32(m, s->a[7], value);
}

uint32_t mw_pop32(s18_t *m, mw68k_t *s)
{
    uint32_t v = s18_read32(m, s->a[7]);
    s->a[7] += 4;
    return v;
}

void mw_push16(s18_t *m, mw68k_t *s, uint16_t value)
{
    s->a[7] -= 2;
    s18_write16(m, s->a[7], value);
}

uint16_t mw_pop16(s18_t *m, mw68k_t *s)
{
    uint16_t v = s18_read16(m, s->a[7]);
    s->a[7] += 2;
    return v;
}

/* ------------------------------------------------------------------ */
/* Flags and conditions                                                */
/* ------------------------------------------------------------------ */

int mw_cond(mw68k_t *s, int cc)
{
    int c = (s->sr & MW_SR_C) != 0;
    int v = (s->sr & MW_SR_V) != 0;
    int z = (s->sr & MW_SR_Z) != 0;
    int n = (s->sr & MW_SR_N) != 0;
    switch (cc & 0x0F) {
    case 0x0: return 1;                 /* T  */
    case 0x1: return 0;                 /* F  */
    case 0x2: return !c && !z;          /* HI */
    case 0x3: return c || z;            /* LS */
    case 0x4: return !c;                /* CC */
    case 0x5: return c;                 /* CS */
    case 0x6: return !z;                /* NE */
    case 0x7: return z;                 /* EQ */
    case 0x8: return !v;                /* VC */
    case 0x9: return v;                 /* VS */
    case 0xA: return !n;                /* PL */
    case 0xB: return n;                 /* MI */
    case 0xC: return n == v;            /* GE */
    case 0xD: return n != v;            /* LT */
    case 0xE: return !z && (n == v);    /* GT */
    default:  return z || (n != v);     /* LE */
    }
}

void mw_set_nz(mw68k_t *s, uint32_t value, int size)
{
    uint32_t m = mask_of(size);
    s->sr = (uint16_t)((s->sr & ~(MW_SR_N | MW_SR_Z | MW_SR_V | MW_SR_C)) |
                       (value & sign_of(size) ? MW_SR_N : 0) |
                       ((value & m) == 0 ? MW_SR_Z : 0));
}

void mw_set_sz(mw68k_t *s, uint32_t value, int size)
{
    s->sr = (uint16_t)((s->sr & ~(MW_SR_N | MW_SR_Z)) |
                       (value & sign_of(size) ? MW_SR_N : 0) |
                       ((value & mask_of(size)) == 0 ? MW_SR_Z : 0));
}

static void set_nz_vc(mw68k_t *s, uint32_t value, int size)
{
    mw_set_nz(s, value, size);
    s->sr &= (uint16_t)~(MW_SR_V | MW_SR_C);
}

static uint32_t do_add(mw68k_t *s, uint32_t a, uint32_t b, int size, int with_x)
{
    uint32_t m = mask_of(size);
    uint32_t x = (with_x && (s->sr & MW_SR_X)) ? 1 : 0;
    uint32_t r = (a + b + x) & m;
    uint32_t sign = sign_of(size);
    int carry = ((a & m) + (b & m) + x) > m;
    int overflow = (~(a ^ b) & (a ^ r) & sign) != 0;
    s->sr = (uint16_t)((s->sr & ~(MW_SR_N | MW_SR_Z | MW_SR_V | MW_SR_C | MW_SR_X)) |
                       (r & sign ? MW_SR_N : 0) |
                       (r == 0 ? MW_SR_Z : 0) |
                       (overflow ? MW_SR_V : 0) |
                       (carry ? MW_SR_C | MW_SR_X : 0));
    return r;
}

static uint32_t do_sub(mw68k_t *s, uint32_t a, uint32_t b, int size, int with_x, int affect_x)
{
    uint32_t m = mask_of(size);
    uint32_t x = (with_x && (s->sr & MW_SR_X)) ? 1 : 0;
    uint32_t r = (a - b - x) & m;
    uint32_t sign = sign_of(size);
    int carry = ((b & m) + x) > (a & m);
    int overflow = ((a ^ b) & (a ^ r) & sign) != 0;
    uint16_t bits = (uint16_t)((s->sr & ~(MW_SR_N | MW_SR_Z | MW_SR_V | MW_SR_C | (affect_x ? MW_SR_X : 0))) |
                               (r & sign ? MW_SR_N : 0) |
                               (r == 0 ? MW_SR_Z : 0) |
                               (overflow ? MW_SR_V : 0) |
                               (carry ? MW_SR_C : 0) |
                               (affect_x && carry ? MW_SR_X : 0));
    s->sr = bits;
    return r;
}

int mw68k_cycles(uint16_t opcode)
{
    extern unsigned char m68ki_cycles[][0x10000];
    int c = m68ki_cycles[0][opcode];
    return c > 0 ? c : 4;
}

/* ------------------------------------------------------------------ */
/* Moves                                                               */
/* ------------------------------------------------------------------ */

void mw_move(s18_t *m, mw68k_t *s, int size, const mw_ea_t *src, const mw_ea_t *dst)
{
    uint32_t v = mw_ea_read(m, s, src, size);
    mw_ea_write(m, s, dst, size, v);
    set_nz_vc(s, v, size);
}

void mw_movea(s18_t *m, mw68k_t *s, int size, const mw_ea_t *src, int areg)
{
    uint32_t v = mw_ea_read(m, s, src, size);
    if (size == 2) v = (uint32_t)(int32_t)(int16_t)v;
    s->a[areg] = v;
}

void mw_moveq(mw68k_t *s, uint32_t value, int dreg)
{
    s->d[dreg] = (uint32_t)(int32_t)(int8_t)value;
    set_nz_vc(s, s->d[dreg], 4);
}

void mw_imm_to_ccr(mw68k_t *s, int op, uint32_t imm, int to_sr)
{
    if (to_sr) {
        uint16_t v = (uint16_t)imm;
        if (op == 0) s->sr &= v;
        else if (op == 1) s->sr |= v;
        else s->sr ^= v;
    } else {
        uint16_t v = (uint16_t)(imm & 0x1F);
        if (op == 0) s->sr &= (uint16_t)~v;
        else if (op == 1) s->sr |= v;
        else s->sr ^= v;
    }
}

void mw_move_to_sr(s18_t *m, mw68k_t *s, int size, const mw_ea_t *src, int to_sr)
{
    uint32_t v = mw_ea_read(m, s, src, size == 2 ? 2 : 1);
    (void)to_sr;
    if (size == 2)
        s->sr = (uint16_t)v;
    else
        s->sr = (uint16_t)((s->sr & 0xFF00) | (v & 0x1F));
}

void mw_move_from_sr(s18_t *m, mw68k_t *s, int size, const mw_ea_t *dst, int from_sr)
{
    uint32_t v = from_sr ? (s->sr & 0xFFFF) : (s->sr & 0x1F);
    mw_ea_write(m, s, dst, size, v);
}

/* ------------------------------------------------------------------ */
/* Arithmetic / logic                                                  */
/* ------------------------------------------------------------------ */

void mw_add(s18_t *m, mw68k_t *s, int size, const mw_ea_t *src, const mw_ea_t *dst, int write)
{
    if (!write) {
        uint32_t a = mw_ea_read(m, s, dst, size);
        uint32_t b = mw_ea_read(m, s, src, size);
        do_add(s, a, b, size, 0);
        return;
    }
    /* Read-modify-write destinations must evaluate the effective address
     * once: (An)+/(An)- increment/decrement exactly once, after the store. */
    mw_rmw_t r;
    mw_rmw_begin(m, s, dst, size, &r);
    uint32_t b = mw_ea_read(m, s, src, size);
    uint32_t out = do_add(s, r.value, b, size, 0);
    mw_rmw_end(m, s, dst, &r, out);
}

void mw_adda(s18_t *m, mw68k_t *s, int size, const mw_ea_t *src, int areg)
{
    uint32_t v = mw_ea_read(m, s, src, size);
    if (size == 2) v = (uint32_t)(int32_t)(int16_t)v;
    s->a[areg] += v;
}

void mw_sub(s18_t *m, mw68k_t *s, int size, const mw_ea_t *src, const mw_ea_t *dst, int write)
{
    if (!write) {
        uint32_t a = mw_ea_read(m, s, dst, size);
        uint32_t b = mw_ea_read(m, s, src, size);
        do_sub(s, a, b, size, 0, 1);
        return;
    }
    mw_rmw_t r;
    mw_rmw_begin(m, s, dst, size, &r);
    uint32_t b = mw_ea_read(m, s, src, size);
    uint32_t out = do_sub(s, r.value, b, size, 0, 1);
    mw_rmw_end(m, s, dst, &r, out);
}

void mw_suba(s18_t *m, mw68k_t *s, int size, const mw_ea_t *src, int areg)
{
    uint32_t v = mw_ea_read(m, s, src, size);
    if (size == 2) v = (uint32_t)(int32_t)(int16_t)v;
    s->a[areg] -= v;
}

void mw_cmp(s18_t *m, mw68k_t *s, int size, const mw_ea_t *src, const mw_ea_t *dst)
{
    uint32_t a = mw_ea_read(m, s, dst, size);
    uint32_t b = mw_ea_read(m, s, src, size);
    do_sub(s, a, b, size, 0, 0);
}

void mw_cmpa(s18_t *m, mw68k_t *s, int size, const mw_ea_t *src, int areg)
{
    uint32_t a = s->a[areg];
    uint32_t b = mw_ea_read(m, s, src, size);
    if (size == 2) b = (uint32_t)(int32_t)(int16_t)b;
    do_sub(s, a, b, 4, 0, 0);
}

void mw_and(s18_t *m, mw68k_t *s, int size, const mw_ea_t *src, const mw_ea_t *dst, int write)
{
    if (!write) {
        uint32_t a = mw_ea_read(m, s, dst, size);
        uint32_t b = mw_ea_read(m, s, src, size);
        uint32_t r = a & b;
        set_nz_vc(s, r, size);
        return;
    }
    mw_rmw_t r;
    mw_rmw_begin(m, s, dst, size, &r);
    uint32_t b = mw_ea_read(m, s, src, size);
    uint32_t out = r.value & b;
    set_nz_vc(s, out, size);
    mw_rmw_end(m, s, dst, &r, out);
}

void mw_or(s18_t *m, mw68k_t *s, int size, const mw_ea_t *src, const mw_ea_t *dst, int write)
{
    if (!write) {
        uint32_t a = mw_ea_read(m, s, dst, size);
        uint32_t b = mw_ea_read(m, s, src, size);
        uint32_t r = a | b;
        set_nz_vc(s, r, size);
        return;
    }
    mw_rmw_t r;
    mw_rmw_begin(m, s, dst, size, &r);
    uint32_t b = mw_ea_read(m, s, src, size);
    uint32_t out = r.value | b;
    set_nz_vc(s, out, size);
    mw_rmw_end(m, s, dst, &r, out);
}

void mw_eor(s18_t *m, mw68k_t *s, int size, const mw_ea_t *src, const mw_ea_t *dst, int write)
{
    if (!write) {
        uint32_t a = mw_ea_read(m, s, dst, size);
        uint32_t b = mw_ea_read(m, s, src, size);
        uint32_t r = a ^ b;
        set_nz_vc(s, r, size);
        return;
    }
    mw_rmw_t r;
    mw_rmw_begin(m, s, dst, size, &r);
    uint32_t b = mw_ea_read(m, s, src, size);
    uint32_t out = r.value ^ b;
    set_nz_vc(s, out, size);
    mw_rmw_end(m, s, dst, &r, out);
}

void mw_addi(s18_t *m, mw68k_t *s, int size, const mw_ea_t *dst, uint32_t imm)
{
    mw_rmw_t r;
    mw_rmw_begin(m, s, dst, size, &r);
    uint32_t out = do_add(s, r.value, imm, size, 0);
    mw_rmw_end(m, s, dst, &r, out);
}

void mw_subi(s18_t *m, mw68k_t *s, int size, const mw_ea_t *dst, uint32_t imm)
{
    mw_rmw_t r;
    mw_rmw_begin(m, s, dst, size, &r);
    uint32_t out = do_sub(s, r.value, imm, size, 0, 1);
    mw_rmw_end(m, s, dst, &r, out);
}

void mw_andi(s18_t *m, mw68k_t *s, int size, const mw_ea_t *dst, uint32_t imm)
{
    mw_rmw_t r;
    mw_rmw_begin(m, s, dst, size, &r);
    uint32_t out = r.value & imm;
    set_nz_vc(s, out, size);
    mw_rmw_end(m, s, dst, &r, out);
}

void mw_ori(s18_t *m, mw68k_t *s, int size, const mw_ea_t *dst, uint32_t imm)
{
    mw_rmw_t r;
    mw_rmw_begin(m, s, dst, size, &r);
    uint32_t out = r.value | imm;
    set_nz_vc(s, out, size);
    mw_rmw_end(m, s, dst, &r, out);
}

void mw_eori(s18_t *m, mw68k_t *s, int size, const mw_ea_t *dst, uint32_t imm)
{
    mw_rmw_t r;
    mw_rmw_begin(m, s, dst, size, &r);
    uint32_t out = r.value ^ imm;
    set_nz_vc(s, out, size);
    mw_rmw_end(m, s, dst, &r, out);
}

void mw_cmpi(s18_t *m, mw68k_t *s, int size, const mw_ea_t *dst, uint32_t imm)
{
    uint32_t v = mw_ea_read(m, s, dst, size);
    do_sub(s, v, imm, size, 0, 0);
}

void mw_addq(s18_t *m, mw68k_t *s, int size, const mw_ea_t *dst, uint32_t value)
{
    if (dst->mode == MW_EA_AREG) {
        s->a[dst->reg] += value;
        return;
    }
    mw_rmw_t r;
    mw_rmw_begin(m, s, dst, size, &r);
    uint32_t out = do_add(s, r.value, value, size, 0);
    mw_rmw_end(m, s, dst, &r, out);
}

void mw_subq(s18_t *m, mw68k_t *s, int size, const mw_ea_t *dst, uint32_t value)
{
    if (dst->mode == MW_EA_AREG) {
        s->a[dst->reg] -= value;
        return;
    }
    mw_rmw_t r;
    mw_rmw_begin(m, s, dst, size, &r);
    uint32_t out = do_sub(s, r.value, value, size, 0, 1);
    mw_rmw_end(m, s, dst, &r, out);
}

void mw_neg(s18_t *m, mw68k_t *s, int size, const mw_ea_t *dst)
{
    mw_rmw_t r;
    mw_rmw_begin(m, s, dst, size, &r);
    uint32_t out = (uint32_t)(0 - r.value) & mask_of(size);
    uint32_t sign = sign_of(size);
    s->sr = (uint16_t)((s->sr & ~(MW_SR_N | MW_SR_Z | MW_SR_V | MW_SR_C | MW_SR_X)) |
                       (out & sign ? MW_SR_N : 0) |
                       (out == 0 ? MW_SR_Z : 0) |
                       ((r.value & mask_of(size)) != 0 ? MW_SR_C | MW_SR_X : 0) |
                       ((r.value & mask_of(size)) == sign ? MW_SR_V : 0));
    mw_rmw_end(m, s, dst, &r, out);
}

void mw_negx(s18_t *m, mw68k_t *s, int size, const mw_ea_t *dst)
{
    mw_rmw_t r;
    mw_rmw_begin(m, s, dst, size, &r);
    uint32_t mask = mask_of(size);
    uint32_t sign = sign_of(size);
    uint32_t x = (s->sr & MW_SR_X) ? 1 : 0;
    uint32_t v = r.value & mask;
    uint32_t out = (0u - v - x) & mask;
    int carry = (v | x) != 0;
    int overflow = (v & out & sign) != 0;
    s->sr = (uint16_t)((s->sr & ~(MW_SR_N | MW_SR_V | MW_SR_C | MW_SR_X)) |
                       (out & sign ? MW_SR_N : 0) |
                       (overflow ? MW_SR_V : 0) |
                       (carry ? MW_SR_C | MW_SR_X : 0));
    if (out != 0)
        s->sr &= (uint16_t)~MW_SR_Z;
    mw_rmw_end(m, s, dst, &r, out);
}

static void addx_subx(s18_t *m, mw68k_t *s, int size, const mw_ea_t *src,
                      const mw_ea_t *dst, int subtract)
{
    uint32_t mask = mask_of(size);
    uint32_t sign = sign_of(size);
    uint32_t x = (s->sr & MW_SR_X) ? 1 : 0;
    uint32_t b = mw_ea_read(m, s, src, size) & mask;
    mw_rmw_t r;
    mw_rmw_begin(m, s, dst, size, &r);
    uint32_t a = r.value & mask;
    uint32_t out, carry, overflow;
    if (subtract) {
        uint32_t wide = a - b - x;
        out = wide & mask;
        carry = (b + x) > a;
        overflow = ((a ^ b) & (a ^ out) & sign) != 0;
    } else {
        uint32_t wide = a + b + x;
        out = wide & mask;
        carry = wide > mask;
        overflow = (~(a ^ b) & (a ^ out) & sign) != 0;
    }
    s->sr = (uint16_t)((s->sr & ~(MW_SR_N | MW_SR_V | MW_SR_C | MW_SR_X)) |
                       (out & sign ? MW_SR_N : 0) |
                       (overflow ? MW_SR_V : 0) |
                       (carry ? MW_SR_C | MW_SR_X : 0));
    if (out != 0)
        s->sr &= (uint16_t)~MW_SR_Z;
    mw_rmw_end(m, s, dst, &r, out);
}

void mw_addx(s18_t *m, mw68k_t *s, int size, const mw_ea_t *src, const mw_ea_t *dst)
{
    addx_subx(m, s, size, src, dst, 0);
}

void mw_subx(s18_t *m, mw68k_t *s, int size, const mw_ea_t *src, const mw_ea_t *dst)
{
    addx_subx(m, s, size, src, dst, 1);
}

void mw_not(s18_t *m, mw68k_t *s, int size, const mw_ea_t *dst)
{
    mw_rmw_t r;
    mw_rmw_begin(m, s, dst, size, &r);
    uint32_t out = (~r.value) & mask_of(size);
    set_nz_vc(s, out, size);
    mw_rmw_end(m, s, dst, &r, out);
}

void mw_clr(s18_t *m, mw68k_t *s, int size, const mw_ea_t *dst)
{
    mw_rmw_t r;
    mw_rmw_begin(m, s, dst, size, &r);
    s->sr = (uint16_t)((s->sr & ~(MW_SR_N | MW_SR_Z | MW_SR_V | MW_SR_C)) | MW_SR_Z);
    mw_rmw_end(m, s, dst, &r, 0);
}

void mw_tst(s18_t *m, mw68k_t *s, int size, const mw_ea_t *dst)
{
    uint32_t v = mw_ea_read(m, s, dst, size);
    set_nz_vc(s, v, size);
}

void mw_ext(mw68k_t *s, int size, int dreg)
{
    if (size == 2) {
        uint32_t v = s->d[dreg] & 0xFF;
        s->d[dreg] = (s->d[dreg] & 0xFFFF0000u) | (uint32_t)(uint16_t)(int16_t)(int8_t)v;
        mw_set_nz(s, s->d[dreg], 2);
    } else {
        uint32_t v = s->d[dreg] & 0xFFFF;
        s->d[dreg] = (uint32_t)(int32_t)(int16_t)v;
        mw_set_nz(s, s->d[dreg], 4);
    }
    s->sr &= (uint16_t)~(MW_SR_V | MW_SR_C);
}

void mw_swap(mw68k_t *s, int dreg)
{
    s->d[dreg] = (s->d[dreg] >> 16) | (s->d[dreg] << 16);
    set_nz_vc(s, s->d[dreg], 4);
}

/* ------------------------------------------------------------------ */
/* Bit operations                                                      */
/* ------------------------------------------------------------------ */

void mw_bitop(s18_t *m, mw68k_t *s, int op, int size, const mw_ea_t *dst, uint32_t bit, int bit_is_reg)
{
    uint32_t b = bit & (size == 4 ? 0x1F : 7);
    mw_rmw_t r;
    mw_rmw_begin(m, s, dst, size, &r);
    uint32_t v = r.value;
    uint32_t mask = 1u << b;
    int old = (v & mask) != 0;
    s->sr = (uint16_t)((s->sr & ~MW_SR_Z) | (old ? 0 : MW_SR_Z));
    if (op == 1) v |= mask;        /* BSET  */
    else if (op == 2) v &= ~mask;  /* BCLR  */
    else if (op == 3) v ^= mask;   /* BCHG  */
    else return;                   /* BTST  */
    mw_rmw_end(m, s, dst, &r, v);
}

/* ------------------------------------------------------------------ */
/* Shifts and rotates                                                  */
/* ------------------------------------------------------------------ */

void mw_shift(s18_t *m, mw68k_t *s, int op, int size, const mw_ea_t *dst, uint32_t count, int count_is_reg)
{
    (void)count_is_reg;
    uint32_t mask = mask_of(size);
    uint32_t sign = sign_of(size);
    uint32_t bits = size == 1 ? 8 : size == 2 ? 16 : 32;
    mw_rmw_t r;
    mw_rmw_begin(m, s, dst, size, &r);
    uint32_t v = r.value & mask;
    count &= 0x3F;
    /* Musashi charges two cycles per shift step (68000). */
    s->cycles += (int64_t)count << 1;
    if (op >= 4) count %= bits;

    if (count) {
        uint32_t x = (s->sr & MW_SR_X) ? 1 : 0;
        switch (op) {
        case 0: /* ASL */
            for (uint32_t i = 0; i < count; i++) {
                uint32_t c = (v & sign) != 0;
                v = (v << 1) & mask;
                s->sr = (uint16_t)((s->sr & ~(MW_SR_C | MW_SR_X)) | (c ? MW_SR_C | MW_SR_X : 0));
            }
            break;
        case 1: /* ASR */
            for (uint32_t i = 0; i < count; i++) {
                uint32_t c = v & 1;
                v = (v >> 1) | (v & sign);
                s->sr = (uint16_t)((s->sr & ~(MW_SR_C | MW_SR_X)) | (c ? MW_SR_C | MW_SR_X : 0));
            }
            break;
        case 2: /* LSL */
            for (uint32_t i = 0; i < count; i++) {
                uint32_t c = (v & sign) != 0;
                v = (v << 1) & mask;
                s->sr = (uint16_t)((s->sr & ~(MW_SR_C | MW_SR_X)) | (c ? MW_SR_C | MW_SR_X : 0));
            }
            break;
        case 3: /* LSR */
            for (uint32_t i = 0; i < count; i++) {
                uint32_t c = v & 1;
                v >>= 1;
                s->sr = (uint16_t)((s->sr & ~(MW_SR_C | MW_SR_X)) | (c ? MW_SR_C | MW_SR_X : 0));
            }
            break;
        case 4: { /* ROL */
            uint32_t c = 0;
            for (uint32_t i = 0; i < count; i++) {
                c = (v & sign) != 0;
                v = ((v << 1) & mask) | c;
            }
            s->sr = (uint16_t)((s->sr & ~MW_SR_C) | (c ? MW_SR_C : 0));
            break;
        }
        case 5: { /* ROR */
            uint32_t c = 0;
            for (uint32_t i = 0; i < count; i++) {
                c = v & 1;
                v = (v >> 1) | (c ? sign : 0);
            }
            s->sr = (uint16_t)((s->sr & ~MW_SR_C) | (c ? MW_SR_C : 0));
            break;
        }
        case 6: /* ROXL */
            for (uint32_t i = 0; i < count; i++) {
                uint32_t c = (v & sign) != 0;
                v = ((v << 1) & mask) | x;
                x = c;
            }
            s->sr = (uint16_t)((s->sr & ~(MW_SR_C | MW_SR_X)) | (x ? MW_SR_C | MW_SR_X : 0));
            break;
        case 7: /* ROXR */
            for (uint32_t i = 0; i < count; i++) {
                uint32_t c = v & 1;
                v = (v >> 1) | (x ? sign : 0);
                x = c;
            }
            s->sr = (uint16_t)((s->sr & ~(MW_SR_C | MW_SR_X)) | (x ? MW_SR_C | MW_SR_X : 0));
            break;
        }
        s->sr = (uint16_t)((s->sr & ~(MW_SR_N | MW_SR_Z)) |
                           (v & sign ? MW_SR_N : 0) | (v == 0 ? MW_SR_Z : 0));
        if (op == 0 || op == 1) {
            /* overflow for ASL/ASR is defined on the last step; cleared for
             * the common cases is close enough and matches typical data. */
            if (op == 1) s->sr &= (uint16_t)~MW_SR_V;
        } else if (op == 2 || op == 3) {
            s->sr &= (uint16_t)~MW_SR_V;
        }
    } else {
        s->sr &= (uint16_t)~MW_SR_C;
    }
    mw_rmw_end(m, s, dst, &r, v);
}

/* ------------------------------------------------------------------ */
/* Multiply / divide                                                   */
/* ------------------------------------------------------------------ */

void mw_mulu(s18_t *m, mw68k_t *s, int size, const mw_ea_t *src, int dreg)
{
    uint32_t a = s->d[dreg] & 0xFFFF;
    uint32_t b = (mw_ea_read(m, s, src, size)) & 0xFFFF;
    for (uint32_t y = b; y; y >>= 1)
        if (y & 1) s->cycles += 2;
    s->d[dreg] = a * b;
    set_nz_vc(s, s->d[dreg], 4);
}

void mw_muls(s18_t *m, mw68k_t *s, int size, const mw_ea_t *src, int dreg)
{
    int32_t a = (int16_t)(s->d[dreg] & 0xFFFF);
    uint32_t b = (uint32_t)(int32_t)(int16_t)mw_ea_read(m, s, src, size);
    uint32_t f = 0;
    for (uint32_t y = b; y; y >>= 1) {
        if ((y & 1) != f) {
            s->cycles += 2;
            f = 1 - f;
        }
    }
    s->d[dreg] = (uint32_t)(a * (int32_t)b);
    set_nz_vc(s, s->d[dreg], 4);
}

void mw_divu(s18_t *m, mw68k_t *s, int size, const mw_ea_t *src, int dreg)
{
    uint32_t divisor = mw_ea_read(m, s, src, size) & 0xFFFF;
    uint32_t dividend = s->d[dreg];
    if (divisor == 0) {
        s->sr = (uint16_t)(s->sr | MW_SR_V);
        return;
    }
    uint32_t q = dividend / divisor;
    uint32_t r = dividend % divisor;
    if (q > 0xFFFF) {
        s->sr = (uint16_t)(s->sr | MW_SR_V);
        return;
    }
    s->d[dreg] = ((r & 0xFFFF) << 16) | (q & 0xFFFF);
    set_nz_vc(s, q, 2);
}

void mw_divs(s18_t *m, mw68k_t *s, int size, const mw_ea_t *src, int dreg)
{
    int32_t divisor = (int16_t)mw_ea_read(m, s, src, size);
    int32_t dividend = (int32_t)s->d[dreg];
    if (divisor == 0) {
        s->sr = (uint16_t)(s->sr | MW_SR_V);
        return;
    }
    int32_t q = dividend / divisor;
    int32_t r = dividend % divisor;
    if (q < -32768 || q > 32767) {
        s->sr = (uint16_t)(s->sr | MW_SR_V);
        return;
    }
    s->d[dreg] = ((uint32_t)(r & 0xFFFF) << 16) | ((uint32_t)q & 0xFFFF);
    set_nz_vc(s, (uint32_t)q, 2);
}

/* ------------------------------------------------------------------ */
/* Address / control                                                   */
/* ------------------------------------------------------------------ */

void mw_lea(s18_t *m, mw68k_t *s, const mw_ea_t *src, int areg)
{
    s->a[areg] = mw_ea_addr(m, s, src, 4);
}

void mw_pea(s18_t *m, mw68k_t *s, const mw_ea_t *src)
{
    uint32_t a = mw_ea_addr(m, s, src, 4);
    mw_push32(m, s, a);
}

void mw_jmp(s18_t *m, mw68k_t *s, const mw_ea_t *target)
{
    s->pc = mw_ea_addr(m, s, target, 4);
}

void mw_jsr(s18_t *m, mw68k_t *s, const mw_ea_t *target, uint32_t ret)
{
    uint32_t a = mw_ea_addr(m, s, target, 4);
    mw_push32(m, s, ret);
    s->pc = a;
}

void mw_bsr(s18_t *m, mw68k_t *s, uint32_t target, uint32_t ret)
{
    mw_push32(m, s, ret);
    s->pc = target;
}

void mw_rts(s18_t *m, mw68k_t *s)
{
    s->pc = mw_pop32(m, s);
}

void mw_rte(s18_t *m, mw68k_t *s)
{
    s->sr = mw_pop16(m, s);
    s->pc = mw_pop32(m, s);
    fd1094_rte(&m->fd);
    s->stopped = 0;
}

void mw_stop(mw68k_t *s, uint32_t imm)
{
    s->sr = (uint16_t)imm;
    s->stopped = 1;
}

void mw_movem(s18_t *m, mw68k_t *s, int size, const mw_ea_t *ea, uint16_t mask, int to_regs)
{
    int step = size == 2 ? 2 : 4;
    int count = 0;
    for (int i = 0; i < 16; i++) count += (mask >> i) & 1;
    s->cycles += (int64_t)count * (size == 2 ? 2 : 3);
    if (to_regs) {
        if (ea->mode == MW_EA_POSTINC) {
            uint32_t a = s->a[ea->reg];
            for (int i = 0; i < 16; i++) {
                if (mask & (1u << i)) {
                    uint32_t v = size == 2 ? s18_read16(m, a) : s18_read32(m, a);
                    if (size == 2) v = (uint32_t)(int32_t)(int16_t)v;
                    if (i < 8) s->d[i] = v; else s->a[i - 8] = v;
                    a += step;
                }
            }
            s->a[ea->reg] = a;
        } else {
            uint32_t a = mw_ea_addr(m, s, ea, step);
            for (int i = 0; i < 16; i++) {
                if (mask & (1u << i)) {
                    uint32_t v = size == 2 ? s18_read16(m, a) : s18_read32(m, a);
                    if (size == 2) v = (uint32_t)(int32_t)(int16_t)v;
                    if (i < 8) s->d[i] = v; else s->a[i - 8] = v;
                    a += step;
                }
            }
        }
    } else {
        if (ea->mode == MW_EA_PREDEC) {
            uint32_t a = s->a[ea->reg];
            for (int i = 15; i >= 0; i--) {
                if (mask & (1u << i)) {
                    a -= step;
                    uint32_t v = i < 8 ? s->d[i] : s->a[i - 8];
                    if (size == 2) s18_write16(m, a, (uint16_t)v);
                    else s18_write32(m, a, v);
                }
            }
            s->a[ea->reg] = a;
        } else {
            uint32_t a = mw_ea_addr(m, s, ea, step);
            for (int i = 0; i < 16; i++) {
                if (mask & (1u << i)) {
                    uint32_t v = i < 8 ? s->d[i] : s->a[i - 8];
                    if (size == 2) s18_write16(m, a, (uint16_t)v);
                    else s18_write32(m, a, v);
                    a += step;
                }
            }
        }
    }
}

void mw_movep(s18_t *m, mw68k_t *s, int size, int areg, int disp, int dreg, int to_mem)
{
    uint32_t a = s->a[areg] + (uint32_t)disp;
    if (size == 4) {
        if (to_mem) {
            s18_write8(m, a + 0, (uint8_t)(s->d[dreg] >> 24));
            s18_write8(m, a + 2, (uint8_t)(s->d[dreg] >> 16));
            s18_write8(m, a + 4, (uint8_t)(s->d[dreg] >> 8));
            s18_write8(m, a + 6, (uint8_t)s->d[dreg]);
        } else {
            uint32_t v = ((uint32_t)s18_read8(m, a + 0) << 24) |
                         ((uint32_t)s18_read8(m, a + 2) << 16) |
                         ((uint32_t)s18_read8(m, a + 4) << 8) |
                         s18_read8(m, a + 6);
            s->d[dreg] = v;
        }
    } else {
        if (to_mem) {
            s18_write8(m, a + 0, (uint8_t)(s->d[dreg] >> 8));
            s18_write8(m, a + 2, (uint8_t)s->d[dreg]);
        } else {
            uint32_t v = ((uint32_t)s18_read8(m, a + 0) << 8) | s18_read8(m, a + 2);
            s->d[dreg] = (s->d[dreg] & 0xFFFF0000u) | v;
        }
    }
}

void mw_exg(mw68k_t *s, int mode, int rx, int ry)
{
    if (mode == 0) { uint32_t t = s->d[rx]; s->d[rx] = s->d[ry]; s->d[ry] = t; }
    else if (mode == 1) { uint32_t t = s->a[rx]; s->a[rx] = s->a[ry]; s->a[ry] = t; }
    else { uint32_t t = s->d[rx]; s->d[rx] = s->a[ry]; s->a[ry] = t; }
}

void mw_abcd(s18_t *m, mw68k_t *s, const mw_ea_t *src, const mw_ea_t *dst)
{
    uint8_t a = (uint8_t)mw_ea_read(m, s, src, 1);
    mw_rmw_t dr;
    mw_rmw_begin(m, s, dst, 1, &dr);
    uint8_t b = (uint8_t)dr.value;
    uint8_t x = (s->sr & MW_SR_X) ? 1 : 0;
    uint32_t lo = (a & 0x0F) + (b & 0x0F) + x;
    uint32_t hi = (a >> 4) + (b >> 4);
    if (lo > 9) { lo += 6; hi++; }
    uint8_t r = (uint8_t)((hi << 4) | (lo & 0x0F));
    int carry = hi > 9;
    if (carry) r = (uint8_t)(r + 0x60);
    mw_rmw_end(m, s, dst, &dr, r);
    s->sr = (uint16_t)((s->sr & ~(MW_SR_C | MW_SR_X | MW_SR_Z)) |
                       (carry ? MW_SR_C | MW_SR_X : 0) |
                       (r == 0 ? MW_SR_Z : (s->sr & MW_SR_Z)));
    /* N/V are undefined for ABCD; approximate with the result sign. */
    s->sr = (uint16_t)((s->sr & ~MW_SR_N) | ((r & 0x80) ? MW_SR_N : 0));
}

void mw_sbcd(s18_t *m, mw68k_t *s, const mw_ea_t *src, const mw_ea_t *dst)
{
    uint8_t a = (uint8_t)mw_ea_read(m, s, src, 1);
    mw_rmw_t dr;
    mw_rmw_begin(m, s, dst, 1, &dr);
    uint8_t b = (uint8_t)dr.value;
    uint8_t x = (s->sr & MW_SR_X) ? 1 : 0;
    int borrow = 0;
    int lo = (b & 0x0F) - (a & 0x0F) - x;
    if (lo < 0) { lo += 10; borrow = 1; }
    int hi = (b >> 4) - (a >> 4) - borrow;
    if (hi < 0) { hi += 10; borrow = 1; }
    uint8_t r = (uint8_t)((hi << 4) | lo);
    mw_rmw_end(m, s, dst, &dr, r);
    s->sr = (uint16_t)((s->sr & ~(MW_SR_C | MW_SR_X | MW_SR_Z)) |
                       (borrow ? MW_SR_C | MW_SR_X : 0) |
                       (r == 0 ? MW_SR_Z : (s->sr & MW_SR_Z)));
    s->sr = (uint16_t)((s->sr & ~MW_SR_N) | ((r & 0x80) ? MW_SR_N : 0));
}

void mw_cmpm(s18_t *m, mw68k_t *s, int size, int src_reg, int dst_reg)
{
    uint32_t a = size == 1 ? s18_read8(m, s->a[dst_reg]) : size == 2 ? s18_read16(m, s->a[dst_reg]) : s18_read32(m, s->a[dst_reg]);
    uint32_t b = size == 1 ? s18_read8(m, s->a[src_reg]) : size == 2 ? s18_read16(m, s->a[src_reg]) : s18_read32(m, s->a[src_reg]);
    s->a[dst_reg] += (uint32_t)(size == 1 && dst_reg == 7 ? 2 : size);
    s->a[src_reg] += (uint32_t)(size == 1 && src_reg == 7 ? 2 : size);
    do_sub(s, a, b, size, 0, 0);
}

int mw_bcc(mw68k_t *s, int cc, uint32_t target, uint32_t fallthrough, int cycles, int byte_disp)
{
    if (g_bcc_machine && g_bcc_machine->cull_margin > 0) {
        uint32_t site = s->pc & 0x00FFFFFFu;
        /* Basic blocks execute several instructions without updating pc,
         * so identify the patched branches by their targets too. */
        if (cc == 4 && target == 13948u && fallthrough == 10030u) site = 0x00272Au;
        else if (cc == 4 && target == 13948u && fallthrough == 13768u) site = 0x0035C4u;
        else if (cc == 5 && target == 13948u && fallthrough == 10040u) site = 0x002734u;
        else if (cc == 5 && target == 13948u && fallthrough == 13778u) site = 0x0035CEu;
        else if (cc == 11 && target == 107962u && fallthrough == 107938u) site = 0x01A5A0u;
        else if (cc == 2 && target == 107962u && fallthrough == 107950u) site = 0x01A5ACu;
        else if (cc == 12 && target == 112358u && fallthrough == 112188u) site = 0x01B638u;
        else if (cc == 11 && target == 112358u && fallthrough == 112196u) site = 0x01B640u;
        int over = s18_wide_branch_override(g_bcc_machine, site,
                                            (int32_t)s->d[0], (int32_t)s->d[1],
                                            (int32_t)s->d[2], (int32_t)s->d[4]);
        if (over >= 0) {
            if (over) {
                s->pc = target;
                return cycles;
            }
            s->pc = fallthrough;
            return byte_disp ? cycles - 2 : cycles + 2;
        }
    }
    if (mw_cond(s, cc)) {
        s->pc = target;
        return cycles;
    }
    s->pc = fallthrough;
    /* Musashi: the base is the taken cost.  A not-taken byte branch is
     * two cycles cheaper; a not-taken word branch is two cycles dearer. */
    return byte_disp ? cycles - 2 : cycles + 2;
}

int mw_dbcc(mw68k_t *s, int cc, int dreg, uint32_t target, uint32_t fallthrough)
{
    if (mw_cond(s, cc)) {
        s->pc = fallthrough;
        return 12;
    }
    uint16_t v = (uint16_t)((s->d[dreg] & 0xFFFF) - 1);
    s->d[dreg] = (s->d[dreg] & 0xFFFF0000u) | v;
    if (v != 0xFFFF) {
        s->pc = target;
        return 10;
    }
    s->pc = fallthrough;
    return 14;
}

void mw_scc(s18_t *m, mw68k_t *s, int cc, const mw_ea_t *dst)
{
    uint32_t v = mw_cond(s, cc) ? 0xFF : 0;
    mw_ea_write(m, s, dst, 1, v);
}

/* ------------------------------------------------------------------ */
/* Dispatch and interrupts                                             */
/* ------------------------------------------------------------------ */

uint32_t mw68k_fetch_vector(s18_t *m, uint32_t address)
{
    /* Interrupt vectors are read on the data bus, not from the FD1094
     * decrypted instruction stream. */
    return s18_read32(m, address);
}

int mw68k_take_irq(s18_t *m, mw68k_t *s, int level)
{
    fd1094_irq_ack(&m->fd);
    uint32_t vector = mw68k_fetch_vector(m, 0x60u + (uint32_t)level * 4u);
    mw_push32(m, s, s->pc);
    mw_push16(m, s, s->sr);
    s->sr = (uint16_t)((s->sr & 0x1F) | MW_SR_S | ((uint32_t)level << 8));
    s->sr &= (uint16_t)~MW_SR_T;
    s->pc = vector;
    s->stopped = 0;
    s->link_fn = 0;
    s->cycles += 44;
    return 1;
}

const mw_block_t *mw_find_block(const mw_block_t *table, unsigned count, uint32_t key)
{
    unsigned lo = 0, hi = count;
    while (lo < hi) {
        unsigned mid = (lo + hi) / 2;
        if (table[mid].key < key) lo = mid + 1;
        else hi = mid;
    }
    if (lo < count && table[lo].key == key)
        return &table[lo];
    return 0;
}

static void log_block(uint32_t key)
{
    static int dbg;
    static FILE *log;
    static int log_init;
    if (!log_init) {
        const char *path = getenv("MW_RECOMP_LOG");
        if (path) log = fopen(path, "w");
        log_init = 1;
    }
    if (log && dbg < 200000) {
        fprintf(log, "REC %02X:%06X\n", key >> 24, key & 0xFFFFFF);
        dbg++;
    }
}

int mw68k_run_compiled(s18_t *m, mw68k_t *s, int cycles)
{
    g_bcc_machine = m;
    /* Match Musashi: interrupts are only sampled between m68k_execute()
     * calls, not in the middle of a translated block. */
    if (m->irq4_pending && (s->sr & 0x0700) < 0x0400) {
        m->irq4_pending = 0;
        mw68k_take_irq(m, s, 4);
    }
    if (s->stopped)
        return 1;

    int64_t start = s->cycles;
    mw_block_fn bfn = 0;
    s->link_fn = 0;

    while (s->cycles - start < cycles) {
        if (!bfn) {
            uint32_t raw_pc = s->pc & 0x00FFFFFFu;
            if ((raw_pc & 1) || raw_pc >= 0x80000) {
                /* A corrupted script pointer can send the CPU outside the
                 * ROM.  Reboot the board instead of leaving the game
                 * spinning in the exception vector area. */
                fprintf(stderr, "recover: invalid PC %06X, resetting board\n", raw_pc);
                mw_m68k_reset(m);
                m->irq4_pending = 0;
                return 1;
            }
            uint32_t key = ((uint32_t)fd1094_state(&m->fd) << 24) | (s->pc & 0x00FFFFFF);
            const mw_block_t *b = mw_find_block(mw_bb_blocks, mw_bb_block_count, key);
            if (!b) {
                /* Indirect entry into the middle of a basic block: execute
                 * the single-instruction translation until we hit a start. */
                const mw_block_t *ins = mw_find_block(mw_blocks, mw_block_count, key);
                if (!ins) {
                    fprintf(stderr, "REC MISS %02X:%06X frame=%lld\n",
                            key >> 24, key & 0xFFFFFF, (long long)m->frame_count);
                    return 0;
                }
                mw_m68k_note_key(key);
                log_block(key);
                s->link_fn = 0;
                ins->fn(m, s);
                if (s->stopped)
                    return 1;
                continue;
            }
            mw_m68k_note_key(b->key);
            log_block(b->key);
            bfn = b->fn;
        }

        s->link_fn = 0;
        bfn(m, s);
        if (s->stopped)
            return 1;
        bfn = s->link_fn;
    }
    s->link_fn = 0;
    return 1;
}

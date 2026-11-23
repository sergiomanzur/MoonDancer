#ifndef MW_RECOMP_H
#define MW_RECOMP_H

#include <stdint.h>
#include "s18.h"

/* Native 68000 state used by statically lifted code. */
typedef struct mw68k mw68k_t;
typedef void (*mw_block_fn)(s18_t *m, mw68k_t *s);

struct mw68k {
    uint32_t d[8];
    uint32_t a[8];
    uint32_t pc;
    uint16_t sr;
    uint8_t stopped;
    uint8_t irq_level;
    int64_t cycles;
    mw_block_fn link_fn;  /* statically known successor block */
};

enum {
    MW_EA_DREG, MW_EA_AREG, MW_EA_IND, MW_EA_POSTINC, MW_EA_PREDEC,
    MW_EA_DISP, MW_EA_INDEX, MW_EA_ABS, MW_EA_PC_DISP, MW_EA_PC_INDEX,
    MW_EA_IMM, MW_EA_QUICK
};

typedef struct mw_ea {
    uint8_t mode;
    uint8_t reg;
    uint8_t idx_reg;
    uint8_t idx_size;
    uint8_t idx_scale;
    int32_t disp;
    uint32_t addr;
} mw_ea_t;

/* SR bits */
#define MW_SR_C 0x0001
#define MW_SR_V 0x0002
#define MW_SR_Z 0x0004
#define MW_SR_N 0x0008
#define MW_SR_X 0x0010
#define MW_SR_S 0x2000
#define MW_SR_T 0x8000

/* generated-code block ABI */
struct mw_block_s {
    uint32_t key;   /* (fd_state << 24) | pc */
    mw_block_fn fn;
    uint16_t insns; /* instructions executed by this block */
};
typedef struct mw_block_s mw_block_t;

extern const mw_block_t mw_blocks[];
extern const unsigned mw_block_count;
extern const mw_block_t mw_bb_blocks[];
extern const unsigned mw_bb_block_count;

const mw_block_t *mw_find_block(const mw_block_t *table, unsigned count, uint32_t key);

/* dispatch / sync */
void mw68k_from_musashi(mw68k_t *s);
void mw68k_to_musashi(const mw68k_t *s);
int mw68k_run_compiled(s18_t *m, mw68k_t *s, int cycles);
int mw68k_take_irq(s18_t *m, mw68k_t *s, int level);
uint32_t mw68k_fetch_vector(s18_t *m, uint32_t address);

/* effective addresses */
uint32_t mw_ea_addr(s18_t *m, mw68k_t *s, const mw_ea_t *e, int size);
uint32_t mw_ea_read(s18_t *m, mw68k_t *s, const mw_ea_t *e, int size);
void mw_ea_write(s18_t *m, mw68k_t *s, const mw_ea_t *e, int size, uint32_t value);
uint32_t mw_ea_quick(const mw_ea_t *e);

typedef struct {
    uint32_t addr;
    uint32_t value;
    int is_reg;
    int size;
    int mode;
    int reg;
} mw_rmw_t;

void mw_rmw_begin(s18_t *m, mw68k_t *s, const mw_ea_t *e, int size, mw_rmw_t *r);
void mw_rmw_end(s18_t *m, mw68k_t *s, const mw_ea_t *e, mw_rmw_t *r, uint32_t value);

/* stack */
void mw_push32(s18_t *m, mw68k_t *s, uint32_t value);
uint32_t mw_pop32(s18_t *m, mw68k_t *s);
void mw_push16(s18_t *m, mw68k_t *s, uint16_t value);
uint16_t mw_pop16(s18_t *m, mw68k_t *s);

/* flags */
int mw_cond(mw68k_t *s, int cc);
void mw_set_nz(mw68k_t *s, uint32_t value, int size);
void mw_set_sz(mw68k_t *s, uint32_t value, int size);

/* instruction helpers */
void mw_move(s18_t *m, mw68k_t *s, int size, const mw_ea_t *src, const mw_ea_t *dst);
void mw_movea(s18_t *m, mw68k_t *s, int size, const mw_ea_t *src, int areg);
void mw_moveq(mw68k_t *s, uint32_t value, int dreg);
void mw_imm_to_ccr(mw68k_t *s, int op, uint32_t imm, int to_sr);
void mw_move_to_sr(s18_t *m, mw68k_t *s, int size, const mw_ea_t *src, int to_sr);
void mw_move_from_sr(s18_t *m, mw68k_t *s, int size, const mw_ea_t *dst, int from_sr);
void mw_add(s18_t *m, mw68k_t *s, int size, const mw_ea_t *src, const mw_ea_t *dst, int write);
void mw_adda(s18_t *m, mw68k_t *s, int size, const mw_ea_t *src, int areg);
void mw_sub(s18_t *m, mw68k_t *s, int size, const mw_ea_t *src, const mw_ea_t *dst, int write);
void mw_suba(s18_t *m, mw68k_t *s, int size, const mw_ea_t *src, int areg);
void mw_cmp(s18_t *m, mw68k_t *s, int size, const mw_ea_t *src, const mw_ea_t *dst);
void mw_cmpa(s18_t *m, mw68k_t *s, int size, const mw_ea_t *src, int areg);
void mw_and(s18_t *m, mw68k_t *s, int size, const mw_ea_t *src, const mw_ea_t *dst, int write);
void mw_or(s18_t *m, mw68k_t *s, int size, const mw_ea_t *src, const mw_ea_t *dst, int write);
void mw_eor(s18_t *m, mw68k_t *s, int size, const mw_ea_t *src, const mw_ea_t *dst, int write);
void mw_addi(s18_t *m, mw68k_t *s, int size, const mw_ea_t *dst, uint32_t imm);
void mw_subi(s18_t *m, mw68k_t *s, int size, const mw_ea_t *dst, uint32_t imm);
void mw_andi(s18_t *m, mw68k_t *s, int size, const mw_ea_t *dst, uint32_t imm);
void mw_ori(s18_t *m, mw68k_t *s, int size, const mw_ea_t *dst, uint32_t imm);
void mw_eori(s18_t *m, mw68k_t *s, int size, const mw_ea_t *dst, uint32_t imm);
void mw_cmpi(s18_t *m, mw68k_t *s, int size, const mw_ea_t *dst, uint32_t imm);
void mw_addq(s18_t *m, mw68k_t *s, int size, const mw_ea_t *dst, uint32_t value);
void mw_subq(s18_t *m, mw68k_t *s, int size, const mw_ea_t *dst, uint32_t value);
void mw_neg(s18_t *m, mw68k_t *s, int size, const mw_ea_t *dst);
void mw_negx(s18_t *m, mw68k_t *s, int size, const mw_ea_t *dst);
void mw_addx(s18_t *m, mw68k_t *s, int size, const mw_ea_t *src, const mw_ea_t *dst);
void mw_subx(s18_t *m, mw68k_t *s, int size, const mw_ea_t *src, const mw_ea_t *dst);
void mw_not(s18_t *m, mw68k_t *s, int size, const mw_ea_t *dst);
void mw_clr(s18_t *m, mw68k_t *s, int size, const mw_ea_t *dst);
void mw_tst(s18_t *m, mw68k_t *s, int size, const mw_ea_t *dst);
void mw_ext(mw68k_t *s, int size, int dreg);
void mw_swap(mw68k_t *s, int dreg);
void mw_bitop(s18_t *m, mw68k_t *s, int op, int size, const mw_ea_t *dst, uint32_t bit, int bit_is_reg);
void mw_shift(s18_t *m, mw68k_t *s, int op, int size, const mw_ea_t *dst, uint32_t count, int count_is_reg);
void mw_mulu(s18_t *m, mw68k_t *s, int size, const mw_ea_t *src, int dreg);
void mw_muls(s18_t *m, mw68k_t *s, int size, const mw_ea_t *src, int dreg);
void mw_divu(s18_t *m, mw68k_t *s, int size, const mw_ea_t *src, int dreg);
void mw_divs(s18_t *m, mw68k_t *s, int size, const mw_ea_t *src, int dreg);
void mw_lea(s18_t *m, mw68k_t *s, const mw_ea_t *src, int areg);
void mw_pea(s18_t *m, mw68k_t *s, const mw_ea_t *src);
void mw_jmp(s18_t *m, mw68k_t *s, const mw_ea_t *target);
void mw_jsr(s18_t *m, mw68k_t *s, const mw_ea_t *target, uint32_t ret);
void mw_bsr(s18_t *m, mw68k_t *s, uint32_t target, uint32_t ret);
void mw_rts(s18_t *m, mw68k_t *s);
void mw_rte(s18_t *m, mw68k_t *s);
void mw_stop(mw68k_t *s, uint32_t imm);
void mw_movem(s18_t *m, mw68k_t *s, int size, const mw_ea_t *ea, uint16_t mask, int to_regs);
void mw_movep(s18_t *m, mw68k_t *s, int size, int areg, int disp, int dreg, int to_mem);
void mw_exg(mw68k_t *s, int mode, int rx, int ry);
void mw_abcd(s18_t *m, mw68k_t *s, const mw_ea_t *src, const mw_ea_t *dst);
void mw_sbcd(s18_t *m, mw68k_t *s, const mw_ea_t *src, const mw_ea_t *dst);
void mw_cmpm(s18_t *m, mw68k_t *s, int size, int src_reg, int dst_reg);

/* branch helpers */
int mw68k_cycles(uint16_t opcode);
int mw_bcc(mw68k_t *s, int cc, uint32_t target, uint32_t fallthrough, int cycles, int byte_disp);
int mw_dbcc(mw68k_t *s, int cc, int dreg, uint32_t target, uint32_t fallthrough);
void mw_scc(s18_t *m, mw68k_t *s, int cc, const mw_ea_t *dst);

#endif

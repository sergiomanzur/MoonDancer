/*
 * Musashi glue for the Moonwalker board.
 *
 * Musashi's separate-read configuration gives us exactly the split the
 * FD1094 needs:
 *   m68k_read_immediate_*   instruction stream -> decrypted ROM
 *   m68k_read_memory_*      data bus           -> original ROM / devices
 *   m68k_read_pcrelative_*  PC-relative data  -> original ROM
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "m68k.h"
#include "s18.h"
#include "mcu.h"
#include "sound.h"
#include "recomp.h"

/* Declared in m68kcpu.h; the public m68k.h does not expose them. */
void m68k_set_cmpild_instr_callback(void (*callback)(unsigned int, int));
void m68k_set_rte_instr_callback(void (*callback)(void));

static s18_t *g_m;
static int g_trace;
static FILE *g_trace_file;
static uint8_t *g_seen;      /* 256 states x 65536 bytes (one bit per even PC) */
static int g_record;
static mw68k_t g_rec;
static int g_use_recomp = -1;
static int64_t g_cpu_time;

void mw_m68k_note_key(uint32_t key)
{
    if (!g_record || !g_seen)
        return;
    uint8_t st = (uint8_t)(key >> 24);
    unsigned pc = key & 0xFFFFFF;
    if (pc >= 0x80000)
        return;
    unsigned w = (pc & 0x7FFFF) >> 1;
    g_seen[(unsigned)st * 0x10000u + (w >> 3)] |= (uint8_t)(1u << (w & 7));
}

void mw_m68k_set_record(int on)
{
    g_record = on;
    if (on && !g_seen) {
        g_seen = (uint8_t *)calloc(256u * 0x10000u, 1);
    }
}

void mw_m68k_dump_record(const char *path)
{
    if (!g_seen) return;
    FILE *f = fopen(path, "w");
    if (!f) return;
    for (int st = 0; st < 256; st++) {
        for (unsigned w = 0; w < 0x40000; w++) {
            unsigned char m = g_seen[(unsigned)st * 0x10000u + (w >> 3)];
            if (m & (1u << (w & 7)))
                fprintf(f, "%02X %06X\n", st, w * 2);
        }
    }
    fclose(f);
}

uint32_t mw68k_current_pc(void)
{
    return g_rec.pc & 0x00FFFFFFu;
}

void mw_m68k_set_machine(s18_t *m) { g_m = m; }void mw_m68k_set_trace(int on, FILE *out) { g_trace = on; g_trace_file = out; }

/* ---- data bus ---- */

unsigned int m68k_read_memory_8(unsigned int address)
{
    return s18_read8(g_m, address);
}

unsigned int m68k_read_memory_16(unsigned int address)
{
    return s18_read16(g_m, address);
}

unsigned int m68k_read_memory_32(unsigned int address)
{
    return s18_read32(g_m, address);
}

void m68k_write_memory_8(unsigned int address, unsigned int value)
{
    s18_write8(g_m, address, (uint8_t)value);
}

void m68k_write_memory_16(unsigned int address, unsigned int value)
{
    s18_write16(g_m, address, (uint16_t)value);
}

void m68k_write_memory_32(unsigned int address, unsigned int value)
{
    s18_write32(g_m, address, (uint32_t)value);
}

void m68k_write_memory_32_pd(unsigned int address, unsigned int value)
{
    s18_write16(g_m, address + 2, (uint16_t)(value >> 16));
    s18_write16(g_m, address, (uint16_t)value);
}

/* ---- instruction stream (decrypted by the FD1094) ---- */

/* Musashi executes the original culling branch.  The live sprite builder
 * uses BCC.W after `cmpi.w #$200,D2`.  When the expanded viewport allows
 * the sprite, return a never-taken BVS of the same size instead: V is
 * clear for all normal positive X values, so the branch is never taken,
 * with the same instruction length and cycle cost.  When the sprite is
 * still outside the expanded range, keep the original BCC so the cull
 * happens exactly as before.
 *
 * The same substitution covers the four object-visibility branches in the
 * task/spawn helpers (BMI.S/BHI.S at 01A5A0/01A5AC, BGE.W/BMI.W at
 * 01B638/01B640).  They must be widened on the interpreter path too:
 * otherwise a frame that falls back to Musashi culls margin entities with
 * the original 4:3 rules and the spawner recycles them every frame. */
static unsigned int wide_musashi_opcode(unsigned int address, unsigned int op)
{
    if (!g_m || g_m->cull_margin <= 0)
        return op;
    if (address != 0x00272Au && address != 0x0035C4u &&
        address != 0x002734u && address != 0x0035CEu &&
        address != 0x01A5A0u && address != 0x01A5ACu &&
        address != 0x01B638u && address != 0x01B640u)
        return op;
    int over = s18_wide_branch_override(g_m, address,
                                        (int32_t)m68k_get_reg(NULL, M68K_REG_D0),
                                        (int32_t)m68k_get_reg(NULL, M68K_REG_D1),
                                        (int32_t)m68k_get_reg(NULL, M68K_REG_D2),
                                        (int32_t)m68k_get_reg(NULL, M68K_REG_D4));
    if (over < 0)
        return op;
    /* Never-taken BVS of the same size as the branch being replaced: 0x6900 is
     * BVS.W (a 00 low displacement byte selects the word form), 0x6902 is
     * BVS.B.  The lengths must match — 0x6900 on a byte branch makes Musashi
     * eat the following word as a displacement (it derailed at 01A5A0/01A5AC
     * into the next immediate), while 0x6902 on a word branch would leave the
     * original displacement word to be executed as an opcode. */
    return over ? op : ((op & 0xFF) ? 0x6902u : 0x6900u);
}

unsigned int m68k_read_immediate_16(unsigned int address)
{
    return wide_musashi_opcode(address, s18_fetch16(g_m, address));
}

unsigned int m68k_read_immediate_32(unsigned int address)
{
    return s18_fetch32(g_m, address);
}

/* ---- PC-relative data (not decrypted) ---- */

unsigned int m68k_read_pcrelative_8(unsigned int address)
{
    return s18_read8(g_m, address);
}

unsigned int m68k_read_pcrelative_16(unsigned int address)
{
    return s18_read16(g_m, address);
}

unsigned int m68k_read_pcrelative_32(unsigned int address)
{
    return s18_read32(g_m, address);
}

/* ---- disassembler ---- */

unsigned int m68k_read_disassembler_16(unsigned int address)
{
    return s18_read16(g_m, address);
}

unsigned int m68k_read_disassembler_32(unsigned int address)
{
    return s18_read32(g_m, address);
}

/* ---- FD1094 callbacks ---- */

static int int_ack(int level)
{
    fd1094_irq_ack(&g_m->fd);
    /* Musashi's default ack callback clears the line; ours replaced it, so
     * do the same or the handler would be entered forever. */
    m68k_set_irq(0);
    (void)level;
    return M68K_INT_ACK_AUTOVECTOR;
}

static void cmpi_callback(unsigned int value, int reg)
{
    fd1094_cmpi_callback(&g_m->fd, value, reg);
}

static void rte_callback(void)
{
    fd1094_rte(&g_m->fd);
}

static void reset_callback(void)
{
    /* A RESET instruction on the 68000 resets the board, but Moonwalker
     * never uses it during normal play.  Keep the CPU state. */
}

static int g_illg_storm;

static int illg_callback(int opcode)
{
    fprintf(stderr, "*** 68000 illegal instruction %04X at PC=%06X state=%02X\n",
            opcode, m68k_get_reg(NULL, M68K_REG_PC), fd1094_state(&g_m->fd));
    /* A long untraced attract path can derail into a garbage-opcode loop.
     * Count the hits; the frame loop turns this into a board reset so the
     * game reboots instead of freezing forever. */
    g_illg_storm++;
    return 0;
}

static void instruction_hook(unsigned int pc)
{
    if (g_record && g_seen && pc < 0x80000) {
        uint8_t st = fd1094_state(&g_m->fd);
        unsigned w = (pc & 0x7FFFF) >> 1;
        g_seen[(unsigned)st * 0x10000u + (w >> 3)] |= (uint8_t)(1u << (w & 7));
    }
    if (!g_trace) return;
    if (g_m->executed < 20000000ull) {
        fprintf(g_trace_file ? g_trace_file : stderr,
                "%06X st=%02X D0=%08X D1=%08X D2=%08X D3=%08X A0=%08X A7=%08X SR=%04X\n", pc, fd1094_state(&g_m->fd),
                m68k_get_reg(NULL, M68K_REG_D0), m68k_get_reg(NULL, M68K_REG_D1),
                m68k_get_reg(NULL, M68K_REG_D2), m68k_get_reg(NULL, M68K_REG_D3),
                m68k_get_reg(NULL, M68K_REG_A0), m68k_get_reg(NULL, M68K_REG_A7),
                m68k_get_reg(NULL, M68K_REG_SR));
    }
    g_m->executed++;
}

void mw_m68k_init(s18_t *m)
{
    mw_m68k_set_machine(m);
    m68k_init();
    m68k_set_cpu_type(M68K_CPU_TYPE_68000);
    m68k_set_int_ack_callback(int_ack);
    m68k_set_cmpild_instr_callback(cmpi_callback);
    m68k_set_rte_instr_callback(rte_callback);
    m68k_set_reset_instr_callback(reset_callback);
    m68k_set_illg_instr_callback(illg_callback);
    m68k_set_instr_hook_callback(instruction_hook);
    if (g_use_recomp < 0) {
        const char *no = getenv("MW_NO_RECOMP");
        g_use_recomp = (!no && mw_block_count > 0) ? 1 : 0;
    }
}

void mw_m68k_reset(s18_t *m)
{
    s18_reset(m);
    m68k_pulse_reset();

    /* Musashi's reset reads the vectors through the data bus, which sees
     * the encrypted ROM.  The FD1094 fetches them from the decrypted
     * instruction stream with the special vector rules. */
    uint32_t sp = s18_fetch32(m, 0);
    uint32_t pc = s18_fetch32(m, 4);
    m68k_set_reg(M68K_REG_SP, sp);
    m68k_set_reg(M68K_REG_PC, pc);
    m68k_set_reg(M68K_REG_SR, 0x2700);
    if (sp == 0xFFFFFFFFu || pc == 0xFFFFFFFFu)
        fprintf(stderr, "bad reset vector: SP=%08X PC=%08X\n", sp, pc);

    mw68k_from_musashi(&g_rec);
    g_rec.cycles = 0;
    g_rec.stopped = 0;
    g_cpu_time = 0;
}

static void mw68k_bus_reset_native(s18_t *m, mw68k_t *s)
{
    fd1094_reset(&m->fd);
    uint32_t sp = s18_fetch32(m, 0);
    uint32_t pc = s18_fetch32(m, 4);
    s->a[7] = sp;
    s->pc = pc;
    s->sr = 0x2700;
    s->stopped = 0;
    s->irq_level = 0;
    s->link_fn = 0;
}

static void mw68k_bus_reset_musashi(s18_t *m)
{
    fd1094_reset(&m->fd);
    m68k_pulse_reset();
    uint32_t sp = s18_fetch32(m, 0);
    uint32_t pc = s18_fetch32(m, 4);
    m68k_set_reg(M68K_REG_SP, sp);
    m68k_set_reg(M68K_REG_PC, pc);
    m68k_set_reg(M68K_REG_SR, 0x2700);
}

int64_t mw68k_time(void)
{
    return g_cpu_time;
}

static void run_musashi(int remaining, s18_t *m)
{
    while (remaining > 0) {
        if (m->cpu_reset_pending && !m->cpu_reset_hold) {
            mw68k_bus_reset_musashi(m);
            m->cpu_reset_pending = 0;
            m->irq4_pending = 0;
        }
        if (m->cpu_reset_hold) {
            int slice = remaining > 200 ? 200 : remaining;
            if (m->mcu)
                s18_mcu_run(m->mcu, slice);
            if (m->audio)
                s18_audio_run(m->audio, (slice * 8) / 10);
            g_cpu_time += slice;
            remaining -= slice;
            continue;
        }
        int chunk = remaining > 200 ? 200 : remaining;
        int ran = m68k_execute(chunk);
        if (ran <= 0)
            break;
        remaining -= ran;
        g_cpu_time += ran;
        if (m->mcu)
            s18_mcu_run(m->mcu, ran);
        if (m->audio)
            s18_audio_run(m->audio, (ran * 8) / 10);
        if (m->irq4_pending) {
            m->irq4_pending = 0;
            m68k_set_irq(4);
        }
    }
}

int mw_m68k_selftest(const uint8_t *rom, const uint8_t *key, int steps)
{
    static s18_t a, b;
    s18_init(&a, rom, key);
    s18_init(&b, rom, key);
    mw_m68k_init(&a);
    mw_m68k_reset(&a);
    s18_reset(&b);
    /* Consume Musashi's reset cycles and the first instruction (a BRA at
     * the reset vector), so both sides start at the same next PC. */
    for (;;) {
        uint32_t before = m68k_get_reg(NULL, M68K_REG_PC);
        m68k_execute(1);
        if (m68k_get_reg(NULL, M68K_REG_PC) != before)
            break;
    }
    mw68k_t r;
    mw68k_from_musashi(&r);
    r.cycles = 0;
    r.stopped = 0;
    int64_t cycles_a = 0;

    for (int i = 0; i < steps; i++) {
        uint32_t key_before = ((uint32_t)fd1094_state(&b.fd) << 24) | (r.pc & 0xFFFFFF);
        int64_t cycles_before = r.cycles;
        int ran = m68k_execute(1);
        if (ran <= 0) break;
        cycles_a += ran;

        const mw_block_t *blk = mw_find_block(mw_blocks, mw_block_count, key_before);
        if (!blk) {
            fprintf(stderr, "SELFTEST missing block %02X:%06X step %d\n",
                    key_before >> 24, key_before & 0xFFFFFF, i);
            return 2;
        }
        blk->fn(&b, &r);

        uint32_t pc_a = m68k_get_reg(NULL, M68K_REG_PC);
        uint32_t sr_a = m68k_get_reg(NULL, M68K_REG_SR);
        for (int rgi = 0; rgi < 8; rgi++) {
            if (r.d[rgi] != m68k_get_reg(NULL, (m68k_register_t)(M68K_REG_D0 + rgi)) ||
                r.a[rgi] != m68k_get_reg(NULL, (m68k_register_t)(M68K_REG_A0 + rgi))) {
                fprintf(stderr, "SELFTEST reg diff step %d at %02X:%06X pcA=%06X pcB=%06X D%d=%08X/%08X A%d=%08X/%08X\n",
                        i, key_before >> 24, key_before & 0xFFFFFF, pc_a, r.pc, rgi,
                        m68k_get_reg(NULL, (m68k_register_t)(M68K_REG_D0 + rgi)), r.d[rgi],
                        rgi, m68k_get_reg(NULL, (m68k_register_t)(M68K_REG_A0 + rgi)), r.a[rgi]);
                return 1;
            }
        }
        if (r.pc != pc_a || r.sr != (uint16_t)sr_a) {
            fprintf(stderr, "SELFTEST cpu diff step %d at %02X:%06X pcA=%06X pcB=%06X srA=%04X srB=%04X\n",
                    i, key_before >> 24, key_before & 0xFFFFFF, pc_a, r.pc, sr_a, r.sr);
            return 1;
        }
        if (r.cycles - cycles_before != ran) {
            fprintf(stderr, "SELFTEST cycle diff step %d at %02X:%06X mus=%d lifted=%lld totalA=%lld totalB=%lld\n",
                    i, key_before >> 24, key_before & 0xFFFFFF, ran,
                    (long long)(r.cycles - cycles_before),
                    (long long)cycles_a, (long long)r.cycles);
            return 4;
        }
        if (fd1094_state(&a.fd) != fd1094_state(&b.fd)) {
            fprintf(stderr, "SELFTEST fd diff step %d at %02X:%06X A=%02X B=%02X\n",
                    i, key_before >> 24, key_before & 0xFFFFFF,
                    fd1094_state(&a.fd), fd1094_state(&b.fd));
            return 1;
        }
        if (memcmp(a.workram, b.workram, sizeof(a.workram)) ||
            memcmp(a.tileram, b.tileram, sizeof(a.tileram)) ||
            memcmp(a.textram, b.textram, sizeof(a.textram)) ||
            memcmp(a.spriteram, b.spriteram, sizeof(a.spriteram)) ||
            memcmp(a.paletteram, b.paletteram, sizeof(a.paletteram))) {
            fprintf(stderr, "SELFTEST mem diff step %d at %02X:%06X pcA=%06X pcB=%06X\n",
                    i, key_before >> 24, key_before & 0xFFFFFF, pc_a, r.pc);
            return 3;
        }
    }
    fprintf(stderr, "SELFTEST passed %d steps\n", steps);
    return 0;
}

int mw_m68k_block_selftest(const uint8_t *rom, const uint8_t *key, int steps)
{
    static s18_t a, b;
    s18_init(&a, rom, key);
    s18_init(&b, rom, key);
    mw_m68k_init(&a);
    mw_m68k_reset(&a);
    s18_reset(&b);
    for (;;) {
        uint32_t before = m68k_get_reg(NULL, M68K_REG_PC);
        m68k_execute(1);
        if (m68k_get_reg(NULL, M68K_REG_PC) != before)
            break;
    }
    mw68k_t r;
    mw68k_from_musashi(&r);
    r.cycles = 0;
    r.stopped = 0;
    r.link_fn = 0;
    int64_t cycles_a = 0;
    int done = 0;

    while (done < steps) {
        uint32_t key_before = ((uint32_t)fd1094_state(&b.fd) << 24) | (r.pc & 0xFFFFFF);
        const mw_block_t *blk = mw_find_block(mw_bb_blocks, mw_bb_block_count, key_before);
        if (!blk) {
            blk = mw_find_block(mw_blocks, mw_block_count, key_before);
            if (!blk) {
                fprintf(stderr, "BLOCK SELFTEST missing block %02X:%06X step %d\n",
                        key_before >> 24, key_before & 0xFFFFFF, done);
                return 2;
            }
        }
        int count = blk->insns ? blk->insns : 1;
        int64_t block_cycles = r.cycles;
        r.link_fn = 0;
        blk->fn(&b, &r);
        if (r.link_fn) {
            uint32_t nkey = ((uint32_t)fd1094_state(&b.fd) << 24) | (r.pc & 0xFFFFFF);
            const mw_block_t *nb = mw_find_block(mw_bb_blocks, mw_bb_block_count, nkey);
            if (!nb || nb->fn != r.link_fn) {
                fprintf(stderr, "BLOCK SELFTEST bad link step %d at %02X:%06X -> %02X:%06X\n",
                        done, key_before >> 24, key_before & 0xFFFFFF,
                        nkey >> 24, nkey & 0xFFFFFF);
                return 5;
            }
        }

        int64_t mus_cycles = 0;
        int ran_ok = 1;
        for (int j = 0; j < count; j++) {
            int ran = m68k_execute(1);
            if (ran <= 0) { ran_ok = 0; break; }
            mus_cycles += ran;
        }
        if (!ran_ok)
            break;
        cycles_a += mus_cycles;

        uint32_t pc_a = m68k_get_reg(NULL, M68K_REG_PC);
        uint32_t sr_a = m68k_get_reg(NULL, M68K_REG_SR);
        for (int rgi = 0; rgi < 8; rgi++) {
            if (r.d[rgi] != m68k_get_reg(NULL, (m68k_register_t)(M68K_REG_D0 + rgi)) ||
                r.a[rgi] != m68k_get_reg(NULL, (m68k_register_t)(M68K_REG_A0 + rgi))) {
                fprintf(stderr, "BLOCK SELFTEST reg diff step %d at %02X:%06X D%d/A%d\n",
                        done, key_before >> 24, key_before & 0xFFFFFF, rgi, rgi);
                return 1;
            }
        }
        if (r.pc != pc_a || r.sr != (uint16_t)sr_a) {
            fprintf(stderr, "BLOCK SELFTEST cpu diff step %d at %02X:%06X pcA=%06X pcB=%06X srA=%04X srB=%04X\n",
                    done, key_before >> 24, key_before & 0xFFFFFF, pc_a, r.pc, sr_a, r.sr);
            return 1;
        }
        if (r.cycles - block_cycles != mus_cycles) {
            fprintf(stderr, "BLOCK SELFTEST cycle diff step %d at %02X:%06X mus=%lld lifted=%lld\n",
                    done, key_before >> 24, key_before & 0xFFFFFF,
                    (long long)mus_cycles, (long long)(r.cycles - block_cycles));
            return 4;
        }
        if (fd1094_state(&a.fd) != fd1094_state(&b.fd)) {
            fprintf(stderr, "BLOCK SELFTEST fd diff step %d at %02X:%06X A=%02X B=%02X\n",
                    done, key_before >> 24, key_before & 0xFFFFFF,
                    fd1094_state(&a.fd), fd1094_state(&b.fd));
            return 1;
        }
        if (memcmp(a.workram, b.workram, sizeof(a.workram)) ||
            memcmp(a.tileram, b.tileram, sizeof(a.tileram)) ||
            memcmp(a.textram, b.textram, sizeof(a.textram)) ||
            memcmp(a.spriteram, b.spriteram, sizeof(a.spriteram)) ||
            memcmp(a.paletteram, b.paletteram, sizeof(a.paletteram))) {
            fprintf(stderr, "BLOCK SELFTEST mem diff step %d at %02X:%06X\n",
                    done, key_before >> 24, key_before & 0xFFFFFF);
            return 3;
        }
        done += count;
        if (r.stopped)
            break;
    }
    fprintf(stderr, "BLOCK SELFTEST passed %d instructions\n", done);
    return 0;
}

void mw_m68k_run(int cycles, s18_t *m)
{
    if (g_illg_storm > 64) {
        /* The long untraced attract/demo path can run off a script address
         * the static recompiler never saw and spin on garbage opcodes.
         * Power-cycle the board like an operator would, keeping the
         * battery-backed work RAM (credits, bookkeeping) intact. */
        static uint8_t sram[0x4000];
        fprintf(stderr, "recover: illegal-opcode storm, power-cycling board\n");
        g_illg_storm = 0;
        memcpy(sram, m->workram, sizeof(m->workram));
        mw_m68k_reset(m);
        memcpy(m->workram, sram, sizeof(m->workram));
        m->irq4_pending = 0;
        return;
    }
    if (!g_use_recomp) {
        if (m->irq4_pending) {
            m->irq4_pending = 0;
            m68k_set_irq(4);
        }
        run_musashi(cycles, m);
        return;
    }

    int remaining = cycles;
    while (remaining > 0) {
        /* The 8751 can hold the 68000 in reset while it initializes or
         * updates shared memory.  The 68000 only restarts when released. */
        if (m->cpu_reset_pending && !m->cpu_reset_hold) {
            mw68k_bus_reset_native(m, &g_rec);
            m->cpu_reset_pending = 0;
            m->irq4_pending = 0;
        }
        if (m->cpu_reset_hold) {
            int slice = remaining > 200 ? 200 : remaining;
            if (m->mcu)
                s18_mcu_run(m->mcu, slice);
            if (m->audio)
                s18_audio_run(m->audio, (slice * 8) / 10);
            g_rec.cycles += slice;
            g_cpu_time += slice;
            remaining -= slice;
            continue;
        }
        /* A stopped 68000 wakes when the 8751 raises IRQ4. */
        if (g_rec.stopped && m->irq4_pending && (g_rec.sr & 0x0700) < 0x0400) {
            m->irq4_pending = 0;
            mw68k_take_irq(m, &g_rec, 4);
        }
        /* A stopped 68000 does not execute, but the 8751 keeps running and
         * turns the vblank pulse into IRQ4.  Advance the peripherals in
         * slices so the idle loop and the real board behave alike. */
        if (g_rec.stopped) {
            /* The 68000 waits; the 8751 keeps running and raises IRQ4.
             * Advance the board in small slices and wake as soon as the
             * interrupt is allowed. */
            int slice = remaining > 200 ? 200 : remaining;
            if (m->mcu)
                s18_mcu_run(m->mcu, slice);
            if (m->audio)
                s18_audio_run(m->audio, (slice * 8) / 10);
            g_rec.cycles += slice;
            remaining -= slice;
            if (m->irq4_pending && (g_rec.sr & 0x0700) < 0x0400) {
                m->irq4_pending = 0;
                mw68k_take_irq(m, &g_rec, 4);
            }
            continue;
        }

        int chunk = remaining > 200 ? 200 : remaining;
        int64_t before = g_rec.cycles;
        int ok = mw68k_run_compiled(m, &g_rec, chunk);
        int spent = (int)(g_rec.cycles - before);
        if (spent < 0) spent = 0;
        g_cpu_time += spent;
        remaining -= spent;
        if (spent > 0) {
            if (m->mcu)
                s18_mcu_run(m->mcu, spent);
            if (m->audio)
                s18_audio_run(m->audio, (spent * 8) / 10);
        }
        if (!ok) {
            /* Untraced code: hand the current context to Musashi for the
             * rest of the frame, then resume the lifted path next frame. */
            mw68k_to_musashi(&g_rec);
            if (m->irq4_pending) {
                m->irq4_pending = 0;
                m68k_set_irq(4);
            }
            /* A miss at the very end of a frame must still make progress,
             * otherwise the same block would be retried forever. */
            run_musashi(remaining > 0 ? remaining : 1, m);
            mw68k_from_musashi(&g_rec);
            return;
        }
        if (spent == 0)
            break;
    }
    mw68k_to_musashi(&g_rec);
}

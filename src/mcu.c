/*
 * 8751 protection/sound MCU on the System 18 board.
 *
 * Uses the MIT-licensed emu8051 core (Jari Komppa) as a portable MCS-51
 * interpreter.  The MCU's external data bus is the 315-5195 mapper: MOVX
 * accesses go to the mapper's 0x00-0x1F register window, exactly like
 * MAME's mcu_data_map.
 */
#include "mcu.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "emu8051.h"
#include "sound.h"

struct s18_mcu {
    s18_t *machine;
    struct em8051 cpu;
    uint8_t rom[0x1000];
    uint8_t ext[0x100];
    int int0_pending;
    int int1_pending;
    int64_t clock_accum; /* 68000 cycles * 8, divided by 10 for MCU clocks */
    int tick_accum;      /* leftover MCU clocks, divided by 12 for ticks */
};

/* The core's callbacks receive only the CPU pointer, so keep a global
 * mapping from the CPU instance to the board.  There is only one MCU. */
static s18_mcu_t *g_mcu;

static uint8_t mcu_xread_cb(struct em8051 *cpu, uint16_t address)
{
    (void)cpu;
    return s18_mapper_read(g_mcu->machine, address & 0xFF);
}

static void mcu_xwrite_cb(struct em8051 *cpu, uint16_t address, uint8_t value)
{
    (void)cpu;
    s18_mapper_write(g_mcu->machine, address & 0xFF, value);
    if ((address & 0x1F) == 3)
        s18_audio_nmi(g_mcu->machine->audio);
}

static void mcu_exception(struct em8051 *cpu, int code)
{
    (void)cpu;
    if (g_mcu && g_mcu->machine)
        fprintf(stderr, "MCU exception %d at PC=%04X\n", code, g_mcu->cpu.mPC);
}

s18_mcu_t *s18_mcu_create(s18_t *machine, const char *romdir)
{
    s18_mcu_t *m = (s18_mcu_t *)calloc(1, sizeof(*m));
    if (!m) return NULL;
    m->machine = machine;

    char path[1024];
    snprintf(path, sizeof(path), "%s/315-5437.ic4", romdir);
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "mcu: cannot open %s\n", path);
        free(m);
        return NULL;
    }
    if (fread(m->rom, 1, sizeof(m->rom), f) != sizeof(m->rom)) {
        fprintf(stderr, "mcu: short read on %s\n", path);
        fclose(f);
        free(m);
        return NULL;
    }
    fclose(f);

    memset(&m->cpu, 0, sizeof(m->cpu));
    m->cpu.mCodeMem = m->rom;
    m->cpu.mCodeMemMaxIdx = sizeof(m->rom) - 1;
    m->cpu.mExtData = m->ext;
    m->cpu.mExtDataMaxIdx = sizeof(m->ext) - 1;
    m->cpu.except = mcu_exception;

    reset(&m->cpu, false);
    g_mcu = m;
    m->cpu.xread = mcu_xread_cb;
    m->cpu.xwrite = mcu_xwrite_cb;
    machine->mcu = m;
    g_mcu = m;
    return m;
}

void s18_mcu_destroy(s18_mcu_t *mcu)
{
    if (!mcu) return;
    if (mcu->machine) mcu->machine->mcu = NULL;
    if (g_mcu == mcu) g_mcu = NULL;
    free(mcu);
}

void s18_mcu_reset(s18_mcu_t *mcu)
{
    if (!mcu) return;
    reset(&mcu->cpu, false);
    mcu->int0_pending = 0;
    mcu->int1_pending = 0;
    mcu->clock_accum = 0;
    mcu->tick_accum = 0;
}

void s18_mcu_int0(s18_mcu_t *mcu)
{
    if (mcu) mcu->int0_pending = 1;
}

void s18_mcu_int1(s18_mcu_t *mcu)
{
    if (mcu) mcu->int1_pending = 1;
}

void s18_mcu_dump_state(s18_mcu_t *mcu, uint8_t *ram128, uint8_t *sfr128)
{
    if (!mcu) return;
    for (int i = 0; i < 128; i++) {
        ram128[i] = mcu->cpu.mLowerData[i];
        sfr128[i] = mcu->cpu.mSFR[i];
    }
}

void s18_mcu_run(s18_mcu_t *mcu, int cycles)
{
    if (!mcu || cycles <= 0) return;

    /* Exact 10 MHz 68000 -> 8 MHz 8751 ratio, carrying the fractional
     * MCU clock and machine-cycle remainders across slices.  The old
     * (cycles*8/10)/12 truncation lost up to a tick per 200-cycle slice. */
    mcu->clock_accum += (int64_t)cycles * 8;
    int64_t mcu_clocks = mcu->clock_accum / 10;
    mcu->clock_accum %= 10;
    mcu->tick_accum += (int)mcu_clocks;
    int ticks = mcu->tick_accum / 12;
    mcu->tick_accum %= 12;
    if (ticks <= 0) return;

    for (int i = 0; i < ticks; i++) {
        if (mcu->int0_pending) {
            mcu->int0_pending = 0;
            mcu->cpu.mSFR[REG_TCON] |= TCONMASK_IE0;
        }
        if (mcu->int1_pending) {
            mcu->int1_pending = 0;
            mcu->cpu.mSFR[REG_TCON] |= TCONMASK_IE1;
        }

        tick(&mcu->cpu);

        /* Edge-triggered external interrupts are latched by the core but
         * not cleared when the vector is taken; the real 8051 clears them. */
        if (mcu->cpu.mPC == ISR_INT0)
            mcu->cpu.mSFR[REG_TCON] &= (uint8_t)~TCONMASK_IE0;
        else if (mcu->cpu.mPC == ISR_INT1)
            mcu->cpu.mSFR[REG_TCON] &= (uint8_t)~TCONMASK_IE1;
    }
}

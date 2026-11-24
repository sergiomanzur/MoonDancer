/*
 * Sega System 18 board (Moonwalker) - native machine model.
 *
 * This is the board as the program sees it: the 68000 bus, the 315-5195
 * memory mapper, the 315-5296 I/O chip, work/video RAM, the 315-5313 VDP
 * and the inputs.  The video renderer and audio devices are in their own
 * translation units.
 */
#include "s18.h"
#include "sound.h"
#include "mcu.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Palette                                                             */
/* ------------------------------------------------------------------ */

/* 5-bit resistor-ladder colour values, plus the shadow (pull-down) and
 * highlight (pull-up) variants used by the palette's bit 15. */
static uint8_t pal_normal[32], pal_shadow[32], pal_hilight[32];
static int palette_ready;

static void palette_init_tables(void)
{
    static const double r[5] = { 3900.0, 2000.0, 1000.0, 500.0, 250.0 };
    double c[5], sum = 0.0;
    for (int i = 0; i < 5; i++) {
        c[i] = 1.0 / r[i];
        sum += c[i];
    }
    const double pull = 1.0 / 470.0;
    for (int v = 0; v < 32; v++) {
        double cur = 0.0;
        for (int i = 0; i < 5; i++)
            if (v & (1 << i)) cur += c[i];
        double n = 255.0 * cur / sum;
        double s = 255.0 * cur / (sum + pull);
        double h = 255.0 * (cur + pull) / (sum + pull);
        if (n > 255.0) n = 255.0;
        if (s > 255.0) s = 255.0;
        if (h > 255.0) h = 255.0;
        pal_normal[v] = (uint8_t)(n + 0.5);
        pal_shadow[v] = (uint8_t)(s + 0.5);
        pal_hilight[v] = (uint8_t)(h + 0.5);
    }
    palette_ready = 1;
}

void s18_palette_write(s18_t *m, uint32_t offset, uint16_t data, uint16_t mask)
{
    offset &= 0x7FF;
    uint16_t old = (uint16_t)((m->paletteram[offset * 2] << 8) | m->paletteram[offset * 2 + 1]);
    uint16_t val = (uint16_t)((old & ~mask) | (data & mask));
    m->paletteram[offset * 2] = (uint8_t)(val >> 8);
    m->paletteram[offset * 2 + 1] = (uint8_t)val;

    if (!palette_ready)
        palette_init_tables();
    uint8_t r5 = (uint8_t)((((val >> 12) & 1) | ((val << 1) & 0x1E)) & 0x1F);
    uint8_t g5 = (uint8_t)((((val >> 13) & 1) | ((val >> 3) & 0x1E)) & 0x1F);
    uint8_t b5 = (uint8_t)((((val >> 14) & 1) | ((val >> 7) & 0x1E)) & 0x1F);
    m->palette_rgb[offset] = 0xFF000000u | ((uint32_t)pal_normal[r5] << 16) |
                             ((uint32_t)pal_normal[g5] << 8) | pal_normal[b5];
    if (val & 0x8000) {
        m->palette_rgb[offset + 2048] = 0xFF000000u | ((uint32_t)pal_hilight[r5] << 16) |
                                        ((uint32_t)pal_hilight[g5] << 8) | pal_hilight[b5];
    } else {
        m->palette_rgb[offset + 2048] = 0xFF000000u | ((uint32_t)pal_shadow[r5] << 16) |
                                        ((uint32_t)pal_shadow[g5] << 8) | pal_shadow[b5];
    }
}

/* ------------------------------------------------------------------ */
/* 315-5296 I/O                                                        */
/* ------------------------------------------------------------------ */

static uint8_t io_input(s18_t *m, unsigned port)
{
    switch (port) {
    case 0: return m->inputs.p1;
    case 1: return m->inputs.p2;
    case 2: return m->inputs.p3;
    case 4: return m->inputs.service;
    case 5: return m->inputs.coinage;
    case 6: return m->inputs.dsw;
    default: return 0xFF;
    }
}

uint8_t s18_io_read(s18_t *m, uint32_t offset)
{
    offset &= 0x3F;
    switch (offset) {
    case 0x0: case 0x1: case 0x2: case 0x3:
    case 0x4: case 0x5: case 0x6: case 0x7:
        if (m->io_dir & (1u << offset))
            return m->io_output[offset];
        return io_input(m, offset);
    case 0x8: return 'S';
    case 0x9: return 'E';
    case 0xA: return 'G';
    case 0xB: return 'A';
    case 0xC: case 0xE: return m->io_cnt;
    case 0xD: case 0xF: return m->io_dir;
    default: return 0xFF;
    }
}

static void misc_outputs_w(s18_t *m, uint8_t data)
{
    m->grayscale = (data >> 6) & 1;
    (void)data;
}

static void rom_5874_bank_w(s18_t *m, uint8_t data)
{
    for (int i = 0; i < 4; i++) {
        m->tilebank[0 + i] = (uint8_t)(((data & 0x0F) * 4) + i);
        m->tilebank[4 + i] = (uint8_t)((((data >> 4) & 0x0F) * 4) + i);
    }
}

void s18_io_write(s18_t *m, uint32_t offset, uint8_t data)
{
    offset &= 0x3F;
    switch (offset) {
    case 0x0: case 0x1: case 0x2: case 0x3:
    case 0x4: case 0x5: case 0x6: case 0x7:
        if (m->io_dir & (1u << offset)) {
            switch (offset) {
            case 3: misc_outputs_w(m, data); break;
            case 7: rom_5874_bank_w(m, data); break;
            default: break;
            }
        }
        m->io_output[offset] = data;
        break;
    case 0xE:
        /* CNT0/CNT1 gate display; CNT2 gates the 315-5313 VDP layer. */
        m->io_cnt = data;
        m->display_enable = 1;
        m->vdp_enable = (data >> 2) & 1;
        break;
    case 0xF:
        if (m->io_dir != data) {
            for (int i = 0; i < 8; i++) {
                if ((m->io_dir ^ data) & (1u << i)) {
                    /* Match the 315-5296: when a port changes direction,
                     * replay its latched value (or 0 when it becomes an
                     * input).  Port H drives the tile banks and port 3 the
                     * grayscale output. */
                    uint8_t out = (data & (1u << i)) ? m->io_output[i] : 0;
                    if (i == 3) misc_outputs_w(m, out);
                    else if (i == 7) rom_5874_bank_w(m, out);
                }
            }
            m->io_dir = data;
        }
        break;
    default:
        break;
    }
}

/* misc_io: 16 KiB window at the base the mapper gives it.  The low byte
 * is the 315-5296 or a service port; the high byte is open bus. */
static uint16_t misc_io_read16(s18_t *m, uint32_t off)
{
    /* 'off' is the byte address in the 16 KiB window; the mapper's
     * handlers use word offsets (address >> 1). */
    off = (off >> 1) & 0x1FFF;
    switch (off & 0x1800) {
    case 0x0000:
    case 0x0800:
        return (uint16_t)(0xFF00u | s18_io_read(m, off & 0x3F));
    case 0x1000:
        return (uint16_t)(0xFF00u | ((off & 1) ? m->inputs.coinage : m->inputs.service));
    default:
        return 0xFFFF;
    }
}

static void misc_io_write16(s18_t *m, uint32_t off, uint16_t data)
{
    off = (off >> 1) & 0x1FFF;
    switch (off & 0x1800) {
    case 0x0000:
    case 0x0800:
        s18_io_write(m, off & 0x3F, (uint8_t)data);
        break;
    case 0x1000:
        m->vdp_mixing = (uint8_t)data;
        break;
    default:
        break;
    }
}

/* ------------------------------------------------------------------ */
/* 315-5313 VDP                                                        */
/* ------------------------------------------------------------------ */

void s18_vdp_cram_write(s18_t *m, unsigned index, uint16_t data)
{
    index &= 0x3F;
    m->vdp_cram[index] = data;
    uint8_t r = (uint8_t)((data >> 9) & 7);
    uint8_t g = (uint8_t)((data >> 5) & 7);
    uint8_t b = (uint8_t)((data >> 1) & 7);
    uint32_t rr = (uint32_t)(r * 255 / 7);
    uint32_t gg = (uint32_t)(g * 255 / 7);
    uint32_t bb = (uint32_t)(b * 255 / 7);
    m->palette_rgb[0x1000 + index] = 0xFF000000u | (rr << 16) | (gg << 8) | bb;
    m->palette_rgb[0x1040 + index] = 0xFF000000u | ((rr * 2 / 3) << 16) | ((gg * 2 / 3) << 8) | (bb * 2 / 3);
    m->palette_rgb[0x1080 + index] = 0xFF000000u | (rr < 128 ? rr + 128 : 255) << 16 |
                                     (gg < 128 ? gg + 128 : 255) << 8 | (bb < 128 ? bb + 128 : 255);
}

static void vdp_address_inc(s18_t *m)
{
    m->vdp_addr = (uint16_t)((m->vdp_addr + m->vdp_regs[0x0F]) & 0xFFFF);
}

static void vdp_vram_write(s18_t *m, uint16_t data)
{
    unsigned address = m->vdp_addr & 0xFFFF;
    if (address & 1) {
        m->vdp_vram[address & 0xFFFE] = (uint8_t)(data & 0xFF);
    } else {
        m->vdp_vram[address & 0xFFFE] = (uint8_t)(data >> 8);
        m->vdp_vram[(address & 0xFFFE) + 1] = (uint8_t)(data & 0xFF);
    }
    vdp_address_inc(m);
}

static void vdp_dma_68k_to_vram(s18_t *m, uint32_t source, unsigned length)
{
    if (length == 0) length = 0x10000;
    for (unsigned i = 0; i < length; i += 2) {
        uint16_t value = s18_read16(m, source);
        vdp_vram_write(m, value);
        source += 2;
    }
    m->vdp_regs[0x13] = 0;
    m->vdp_regs[0x14] = 0;
}

static void vdp_dma_68k_to_cram(s18_t *m, uint32_t source, unsigned length)
{
    if (length == 0) length = 0x10000;
    for (unsigned i = 0; i < length; i += 2) {
        s18_vdp_cram_write(m, (m->vdp_addr & 0x7E) >> 1, s18_read16(m, source));
        source += 2;
        vdp_address_inc(m);
    }
    m->vdp_regs[0x13] = 0;
    m->vdp_regs[0x14] = 0;
}

static void vdp_dma_68k_to_vsram(s18_t *m, uint32_t source, unsigned length)
{
    if (length == 0) length = 0x10000;
    for (unsigned i = 0; i < length; i += 2) {
        m->vdp_vsram[(m->vdp_addr & 0x7E) >> 1] = s18_read16(m, source);
        source += 2;
        vdp_address_inc(m);
    }
    m->vdp_regs[0x13] = 0;
    m->vdp_regs[0x14] = 0;
}

static void vdp_vram_copy(s18_t *m, uint32_t source, unsigned length)
{
    if (length == 0) length = 0x10000;
    for (unsigned i = 0; i < length; i++) {
        uint8_t byte;
        if (source & 1) byte = m->vdp_vram[(source & 0xFFFF) & 0xFFFE] & 0xFF;
        else byte = (m->vdp_vram[(source & 0xFFFF) & 0xFFFE] >> 8) & 0xFF;
        if (m->vdp_addr & 1) m->vdp_vram[(m->vdp_addr & 0xFFFE) + 1] = byte;
        else m->vdp_vram[m->vdp_addr & 0xFFFE] = byte;
        source++;
        vdp_address_inc(m);
    }
}

static void vdp_handle_dma(s18_t *m)
{
    unsigned type = (m->vdp_regs[0x17] >> 6) & 3;
    unsigned code = m->vdp_code & 0x0F;
    unsigned length = ((unsigned)m->vdp_regs[0x13] | ((unsigned)m->vdp_regs[0x14] << 8)) * 2;
    if (length == 0) length = 0x20000;
    if (!(m->vdp_regs[0x01] & 0x10)) return; /* DMA enable */

    if (type == 0 || type == 1) {
        uint32_t source = ((uint32_t)m->vdp_regs[0x15] |
                           ((uint32_t)m->vdp_regs[0x16] << 8) |
                           (((uint32_t)m->vdp_regs[0x17] & 0x7F) << 16)) << 1;
        if (code == 0x01) vdp_dma_68k_to_vram(m, source, length);
        else if (code == 0x03) vdp_dma_68k_to_cram(m, source, length);
        else if (code == 0x05) vdp_dma_68k_to_vsram(m, source, length);
    } else if (type == 2) {
        if (code == 0x01 || code == 0x03 || code == 0x05) {
            m->vdp_vram_fill_pending = 1;
            m->vdp_vram_fill_length = (uint16_t)((m->vdp_regs[0x13] | (m->vdp_regs[0x14] << 8)));
        }
    } else {
        uint32_t source = (uint32_t)m->vdp_regs[0x15] | ((uint32_t)m->vdp_regs[0x16] << 8);
        vdp_vram_copy(m, source, length);
    }
}

static void vdp_ctrl_port_w(s18_t *m, uint16_t data)
{
    m->vdp_vram_fill_pending = 0;

    if (m->vdp_command_pending) {
        m->vdp_command_pending = 0;
        uint16_t part1 = m->vdp_command_part1;
        m->vdp_code = (uint8_t)(((part1 & 0xC000) >> 14) | ((data & 0x00F0) >> 2));
        m->vdp_addr = (uint16_t)((part1 & 0x3FFF) | ((data & 3) << 14));
        if (m->vdp_code & 0x20)
            vdp_handle_dma(m);
    } else if ((data & 0xC000) == 0x8000) {
        unsigned reg = (data & 0x3F00) >> 8;
        unsigned value = data & 0xFF;
        if (reg <= 0x1F)
            m->vdp_regs[reg] = (uint8_t)value;
    } else {
        m->vdp_command_part1 = data;
        m->vdp_command_pending = 1;
    }
}

static void vdp_data_port_w(s18_t *m, uint16_t data)
{
    m->vdp_command_pending = 0;

    if (m->vdp_vram_fill_pending) {
        unsigned fill_len = m->vdp_vram_fill_length;
        unsigned address = m->vdp_addr & 0xFFFF;
        /* the first byte written is the fill value, then repeat */
        if (address & 1) m->vdp_vram[(address & 0xFFFE) + 1] = (uint8_t)(data & 0xFF);
        else m->vdp_vram[address & 0xFFFE] = (uint8_t)(data >> 8);
        vdp_address_inc(m);
        for (unsigned i = 0; i <= fill_len; i++) {
            address = m->vdp_addr & 0xFFFF;
            if (address & 1) m->vdp_vram[(address & 0xFFFE) + 1] = (uint8_t)(data >> 8);
            else m->vdp_vram[(address & 0xFFFE) + 1] = (uint8_t)(data & 0xFF);
            vdp_address_inc(m);
        }
        m->vdp_vram_fill_pending = 0;
        m->vdp_regs[0x13] = 0;
        m->vdp_regs[0x14] = 0;
        return;
    }

    switch (m->vdp_code & 0x0F) {
    case 0x01:
        vdp_vram_write(m, data);
        break;
    case 0x03:
        s18_vdp_cram_write(m, (m->vdp_addr & 0x7E) >> 1, data);
        vdp_address_inc(m);
        break;
    case 0x05:
        m->vdp_vsram[(m->vdp_addr & 0x7E) >> 1] = data;
        vdp_address_inc(m);
        break;
    default:
        break;
    }
    m->vdp_data_latch = data;
}

static uint16_t vdp_read_data(s18_t *m)
{
    unsigned address = m->vdp_addr & 0xFFFF;
    uint16_t value;
    switch (m->vdp_code & 0x0F) {
    case 0x00:
        value = (uint16_t)((m->vdp_vram[address & 0xFFFE] << 8) |
                           m->vdp_vram[(address & 0xFFFE) + 1]);
        break;
    case 0x08:
        value = m->vdp_cram[(address & 0x7E) >> 1];
        break;
    case 0x04:
        value = m->vdp_vsram[(address & 0x7E) >> 1];
        break;
    default:
        value = 0xFFFF;
        break;
    }
    vdp_address_inc(m);
    return value;
}

uint16_t s18_vdp_read(s18_t *m, uint32_t offset, uint16_t mask)
{
    (void)mask;
    switch (offset & 0x0F) {
    case 0x0: case 0x2:
        return vdp_read_data(m);
    case 0x4: case 0x6: {
        uint16_t status = 0x3400; /* FIFO empty, no DMA */
        if (m->vdp_status & 0x0008) status |= 0x0200; /* vblank */
        m->vdp_command_pending = 0;
        return status;
    }
    case 0x8: case 0xA: case 0xC: case 0xE:
        /* HV counter: approximate with the current scanline */
        return (uint16_t)((m->frame_count & 0xFF) << 8);
    default:
        return 0;
    }
}

void s18_vdp_write(s18_t *m, uint32_t offset, uint16_t data, uint16_t mask)
{
    (void)mask;
    switch (offset & 0x0F) {
    case 0x0: case 0x2:
        vdp_data_port_w(m, data);
        break;
    case 0x4: case 0x6:
        vdp_ctrl_port_w(m, data);
        break;
    default:
        break;
    }
}

/* ------------------------------------------------------------------ */
/* 315-5195 memory mapper                                              */
/* ------------------------------------------------------------------ */

uint8_t s18_mapper_read(s18_t *m, uint32_t offset)
{
    offset &= 0x1F;
    switch (offset) {
    case 0x00:
    case 0x01:
        return m->mapper_regs[offset];
    case 0x02:
        return (uint8_t)((m->mapper_regs[2] & 3) == 3 ? 0x00 : 0x0F);
    case 0x03:
        return m->mapper_from_sound;
    default:
        return 0xFF; /* open bus */
    }
}

void s18_mapper_write(s18_t *m, uint32_t offset, uint8_t data)
{
    offset &= 0x1F;
    uint8_t old = m->mapper_regs[offset];
    m->mapper_regs[offset] = data;

    switch (offset) {
    case 0x02:
        /* Reset/halt control.  The 8751 holds the 68000 in reset while it
         * initializes shared memory at boot.  MAME asserts the CPU reset
         * line for $3 and releases it for any other value. */
        {
            int asserted = (data & 3) == 3;
            if (asserted && !m->cpu_reset_hold) {
                m->cpu_reset_hold = 1;
            } else if (!asserted && m->cpu_reset_hold) {
                m->cpu_reset_hold = 0;
                m->cpu_reset_pending = 1;
            }
        }
        break;
    case 0x03:
        /* The 68000 also writes this register as part of a 16-word
         * scratch table at 0xFE0020; those bytes are not sound commands.
         * The real sound FIFO is driven by the MCU. */
        m->mapper_to_sound = data;
        break;
    case 0x04:
        /* negative-logic IRQ control: $B signals IRQ4 */
        if ((data & 7) != 7) {
            unsigned line = (unsigned)(~data & 7);
            if (line == 4) m->irq4_pending = 1;
        }
        break;
    case 0x05: {
        /* 1 = write latched word to the address in 0A/0B/0C,
           2 = read word from the address in 07/08/09 into 00/01 */
        if (data == 0x01) {
            uint32_t addr = ((uint32_t)m->mapper_regs[0x0A] << 17) |
                            ((uint32_t)m->mapper_regs[0x0B] << 9) |
                            ((uint32_t)m->mapper_regs[0x0C] << 1);
            uint16_t value = (uint16_t)((m->mapper_regs[0x00] << 8) | m->mapper_regs[0x01]);
            s18_write16(m, addr, value);
        } else if (data == 0x02) {
            uint32_t addr = ((uint32_t)m->mapper_regs[0x07] << 17) |
                            ((uint32_t)m->mapper_regs[0x08] << 9) |
                            ((uint32_t)m->mapper_regs[0x09] << 1);
            uint16_t value = s18_read16(m, addr);
            m->mapper_regs[0x00] = (uint8_t)(value >> 8);
            m->mapper_regs[0x01] = (uint8_t)value;
        }
        break;
    }
    default:
        if (offset >= 0x10 && m->log_mapper)
            fprintf(stderr, "mapper[%02X] <- %02X\n", offset, data);
        break;
    }
}

/* ------------------------------------------------------------------ */
/* Bus                                                                 */
/* ------------------------------------------------------------------ */

static inline uint16_t be16(const uint8_t *p)
{
    return (uint16_t)((p[0] << 8) | p[1]);
}

static inline void set_be16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

uint8_t s18_read8(s18_t *m, uint32_t address)
{
    if (address >= 0xE40000 && address <= 0xE40007)
        return (uint8_t)(misc_io_read16(m, address) & 0xFF);
    address &= 0xFFFFFF;
    if (address < 0x80000)
        return m->rom[address];
    if (address >= 0xFFC000) {
        return m->workram[address & 0x3FFF];
    }
    if (address >= 0xE40000 && address < 0xE44000)
        return (uint8_t)(misc_io_read16(m, address) & 0xFF);
    if (address >= 0x840000 && address < 0x841000)
        return m->paletteram[address & 0xFFF];
    if (address >= 0x400000 && address < 0x410000)
        return m->tileram[address & 0xFFFF];
    if (address >= 0x410000 && address < 0x411000)
        return m->textram[address & 0xFFF];
    if (address >= 0x440000 && address < 0x440800)
        return m->spriteram[address & 0x7FF];
    if (address >= 0xC00000 && address < 0xC01000)
        return (uint8_t)(s18_vdp_read(m, address & 0x0F, 0x00FF) & 0xFF);
    return s18_mapper_read(m, address);
}

uint16_t s18_read16(s18_t *m, uint32_t address)
{
    address &= 0xFFFFFF;
    if (address < 0x80000)
        return be16(&m->rom[address]);
    if (address >= 0xFFC000) {
        return be16(&m->workram[address & 0x3FFF]);
    }
    if (address >= 0xE40000 && address < 0xE44000)
        return misc_io_read16(m, address);
    if (address >= 0x840000 && address < 0x841000)
        return be16(&m->paletteram[address & 0xFFF]);
    if (address >= 0x400000 && address < 0x410000)
        return be16(&m->tileram[address & 0xFFFF]);
    if (address >= 0x410000 && address < 0x411000)
        return be16(&m->textram[address & 0xFFF]);
    if (address >= 0x440000 && address < 0x440800)
        return be16(&m->spriteram[address & 0x7FF]);
    if (address >= 0xC00000 && address < 0xC01000)
        return s18_vdp_read(m, address & 0x0F, 0xFFFF);
    return (uint16_t)(0xFF00 | s18_mapper_read(m, address));
}

uint32_t s18_read32(s18_t *m, uint32_t address)
{
    return ((uint32_t)s18_read16(m, address) << 16) | s18_read16(m, address + 2);
}

void s18_write8(s18_t *m, uint32_t address, uint8_t data)
{
    address &= 0xFFFFFF;
    if (address < 0x80000) {
        /* ROM writes are ignored; the mapper's write-through path is
         * handled by s18_mapper_write for the unmapped window. */
        return;
    }
    if (address >= 0xFFC000) {
        m->workram[address & 0x3FFF] = data;
        return;
    }
    if (address >= 0xE40000 && address < 0xE44000) {
        misc_io_write16(m, address, (uint16_t)(0xFF00u | data));
        return;
    }
    if (address >= 0x840000 && address < 0x841000) {
        m->paletteram[address & 0xFFF] = data;
        return;
    }
    if (address >= 0x400000 && address < 0x410000) {
        m->tileram[address & 0xFFFF] = data;
        return;
    }
    if (address >= 0x410000 && address < 0x411000) {
        m->textram[address & 0xFFF] = data;
        return;
    }
    if (address >= 0x440000 && address < 0x440800) {
        m->spriteram[address & 0x7FF] = data;
        return;
    }
    if (address >= 0xC00000 && address < 0xC01000) {
        s18_vdp_write(m, address & 0x0F, (uint16_t)(data << 8), 0xFF00);
        return;
    }
    s18_mapper_write(m, address, data);
}

void s18_write16(s18_t *m, uint32_t address, uint16_t data)
{
    address &= 0xFFFFFF;
    if (address < 0x80000)
        return;
    if (address >= 0xFFC000) {
        set_be16(&m->workram[address & 0x3FFF], data);
        return;
    }
    if (address >= 0xE40000 && address < 0xE44000) {
        misc_io_write16(m, address, data);
        return;
    }
    if (address >= 0x840000 && address < 0x841000) {
        s18_palette_write(m, (address & 0xFFF) >> 1, data, 0xFFFF);
        return;
    }
    if (address >= 0x400000 && address < 0x410000) {
        set_be16(&m->tileram[address & 0xFFFF], data);
        return;
    }
    if (address >= 0x410000 && address < 0x411000) {
        set_be16(&m->textram[address & 0xFFF], data);
        return;
    }
    if (address >= 0x440000 && address < 0x440800) {
        set_be16(&m->spriteram[address & 0x7FF], data);
        return;
    }
    if (address >= 0xC00000 && address < 0xC01000) {
        s18_vdp_write(m, address & 0x0F, data, 0xFFFF);
        return;
    }
    /* The 315-5195 register file is byte-wide and is addressed by the
     * 68000 word address (A1..A5 select the register).  MAME's main map
     * uses umask16(0x00ff), so a 16-bit write lands on register
     * (address >> 1) & 0x1F with the low data byte.  That is what the
     * region-table copy at $FE0020 relies on: its 16 words program
     * mapper registers $10-$1F (the per-region size/base pairs) and
     * must not touch the MCU command latches at $00-$0F. */
    s18_mapper_write(m, address >> 1, (uint8_t)data);
}

void s18_write32(s18_t *m, uint32_t address, uint32_t data)
{
    s18_write16(m, address, (uint16_t)(data >> 16));
    s18_write16(m, address + 2, (uint16_t)data);
}

uint16_t s18_fetch16(s18_t *m, uint32_t address)
{
    address &= 0xFFFFFF;
    if (address < 0x80000)
        return fd1094_fetch16(&m->fd, address);
    return s18_read16(m, address);
}

uint32_t s18_fetch32(s18_t *m, uint32_t address)
{
    return ((uint32_t)s18_fetch16(m, address) << 16) | s18_fetch16(m, address + 2);
}

/* ------------------------------------------------------------------ */
/* Machine                                                             */
/* ------------------------------------------------------------------ */

void s18_init(s18_t *m, const uint8_t *rom, const uint8_t *key)
{
    memset(m, 0, sizeof(*m));
    memcpy(m->rom, rom, sizeof(m->rom));
    memcpy(m->key, key, sizeof(m->key));
    fd1094_init(&m->fd, m->key, m->rom);
    m->inputs.p1 = m->inputs.p2 = m->inputs.p3 = 0xFF;
    m->inputs.service = 0xFF;
    m->inputs.coinage = 0xFF;
    m->inputs.dsw = 0xFD;
    m->display_enable = 1;
    m->vdp_enable = 0;
    m->vdp_mixing = 0;
    m->view_w = S18_SCREEN_W;
    m->view_left = 0;
    m->cull_margin = 0;
    palette_init_tables();
    for (int i = 0; i < 2048; i++)
        m->palette_rgb[i] = 0xFF000000u;
    for (int i = 0; i < 2048; i++)
        m->palette_rgb[i + 2048] = 0xFF000000u;
    for (int i = 0; i < 192; i++)
        m->palette_rgb[4096 + i] = 0xFF000000u;
}

void s18_shutdown(s18_t *m)
{
    fd1094_shutdown(&m->fd);
}

void s18_set_inputs(s18_t *m, const s18_inputs_t *in)
{
    m->inputs = *in;
}

void s18_set_view(s18_t *m, int enhanced)
{
    if (!m) return;
    if (enhanced) {
        m->view_w = S18_MAX_SCREEN_W;
        m->view_left = S18_VIEW_MARGIN;
        m->cull_margin = S18_CULL_MARGIN;
    } else {
        m->view_w = S18_SCREEN_W;
        m->view_left = 0;
        m->cull_margin = 0;
    }
}

int s18_wide_branch_override(const s18_t *m, uint32_t pc, int32_t d0, int32_t d1,
                             int32_t d2, int32_t d4)
{
    if (!m || m->cull_margin <= 0)
        return -1;
    int32_t x = (int16_t)(uint16_t)d0;
    int32_t y = (int16_t)(uint16_t)d1;
    uint16_t sx = (uint16_t)d2;
    uint16_t ex = (uint16_t)d4;
    int32_t lo = -m->cull_margin;
    int32_t hi = S18_SCREEN_W + m->cull_margin;
    uint16_t sprite_hi = (uint16_t)(512 + m->cull_margin);
    uint16_t sprite_lo = (uint16_t)(185 - m->cull_margin);
    switch (pc & 0x00FFFFFFu) {
    case 0x00272Au: /* bcc: live sprite left edge >= 512 */
    case 0x0035C4u: /* bcc: same check on the scaled sprite path */
        return sx >= sprite_hi ? 1 : 0;
    case 0x002734u: /* bcs: live sprite right edge left of the left margin */
    case 0x0035CEu: /* bcs: same check on the scaled sprite path */
        return ex < sprite_lo ? 1 : 0;
    case 0x01A5A0u: /* bmi: object left edge */
        return x < lo ? 1 : 0;
    case 0x01A5ACu: /* bhi: object right edge (signed compare) */
        return x > hi ? 1 : 0;
    case 0x01B638u: /* bge: sprite left edge */
        return x >= hi ? 1 : 0;
    case 0x01B640u: /* bmi: sprite right edge */
        return y < lo ? 1 : 0;
    default:
        return -1;
    }
}
void s18_reset(s18_t *m)
{
    memset(m->workram, 0, sizeof(m->workram));
    memset(m->paletteram, 0, sizeof(m->paletteram));
    memset(m->tileram, 0, sizeof(m->tileram));
    memset(m->textram, 0, sizeof(m->textram));
    memset(m->spriteram, 0, sizeof(m->spriteram));
    memset(m->mapper_regs, 0, sizeof(m->mapper_regs));
    memset(m->vdp_regs, 0, sizeof(m->vdp_regs));
    memset(m->vdp_vram, 0, sizeof(m->vdp_vram));
    memset(m->vdp_cram, 0, sizeof(m->vdp_cram));
    memset(m->vdp_vsram, 0, sizeof(m->vdp_vsram));
    m->io_dir = 0;
    m->io_cnt = 0;
    memset(m->io_output, 0, sizeof(m->io_output));
    for (int i = 0; i < 8; i++) m->tilebank[i] = (uint8_t)i;
    m->frame_count = 0;
    m->cpu_cycles = 0;
    m->cpu_reset_hold = 0;
    m->cpu_reset_pending = 0;
    m->vdp_status = 0;
    m->vdp_pending = 0;
    m->vdp_command_pending = 0;
    m->vdp_vram_fill_pending = 0;
    if (m->mcu)
        s18_mcu_reset(m->mcu);
    m->vdp_addr = 0;
    m->vdp_code = 0;
    fd1094_reset(&m->fd);
}

int s18_load_nvram(s18_t *m, const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return -1;
    size_t n = fread(m->workram, 1, sizeof(m->workram), f);
    fclose(f);
    return n == sizeof(m->workram) ? 0 : -1;
}

/* Writes the workram image through a sibling .tmp file and renames it over the
 * destination.  The bytes written are unchanged (the whole workram, same order);
 * only the file dance is new: a process killed mid-write must not leave a short
 * image behind, because s18_load_nvram rejects a wrong-sized file and the next
 * boot would silently start clean.  rename() is atomic and replaces the target
 * on POSIX (Android/Linux); on Windows rename() refuses an existing target, so
 * the old file is removed first there -- the same non-atomic window the
 * truncate-and-write path always had on that host. */
int s18_save_nvram(const s18_t *m, const char *path)
{
    char tmp[2100];
    FILE *f;
    size_t n;

    if (!m || !path || !*path) return -1;
    if (snprintf(tmp, sizeof(tmp), "%s.tmp", path) >= (int)sizeof(tmp)) return -1;

    f = fopen(tmp, "wb");
    if (!f)
        return -1;
    n = fwrite(m->workram, 1, sizeof(m->workram), f);
    if (n != sizeof(m->workram) || fclose(f) != 0) {
        remove(tmp);
        return -1;
    }
#ifdef _WIN32
    remove(path);
#endif
    if (rename(tmp, path) != 0) {
        remove(tmp);
        return -1;
    }
    return 0;
}

void s18_run_frame(s18_t *m)
{
    extern void mw_m68k_run(int cycles, s18_t *m);
    extern uint32_t mw68k_current_pc(void);
    extern void mw_m68k_reset(s18_t *m);

    /* Visible scanlines first.  The 8751's vblank interrupt (which becomes
     * the 68000's IRQ4) fires at scanline 224, not at the frame boundary.
     * Use absolute cycle targets so a translated block that runs past its
     * budget cannot make the port gain time across frames. */
    extern int64_t mw68k_time(void);
    int64_t base = m->cpu_cycles;
    int64_t visible = base + S18_VISIBLE_CYCLES;
    int64_t now = mw68k_time();
    if (visible > now)
        mw_m68k_run((int)(visible - now), m);

    if (m->mcu)
        s18_mcu_int0(m->mcu);
    else
        m->irq4_pending = 1;
    m->vdp_status |= 0x0008;
    int64_t frame_end = base + S18_CYCLES_PER_FRAME;
    now = mw68k_time();
    if (frame_end > now)
        mw_m68k_run((int)(frame_end - now), m);
    m->vdp_status &= (uint16_t)~0x0008;
    m->cpu_cycles = frame_end;
    m->frame_count++;

    /* The original stage-2 attract palette fade can wedge its palette
     * queue handler at $00252A-$002556, leaving the demo permanently
     * frozen.  Reboot the board after ten seconds in that handler. */
    {
        static int palette_watch;
        uint32_t pc = mw68k_current_pc();
        if (pc >= 0x00252Au && pc <= 0x002556u) {
            if (++palette_watch > 600) {
                fprintf(stderr, "recover: palette fade watchdog, resetting board\n");
                palette_watch = 0;
                mw_m68k_reset(m);
            }
        } else {
            palette_watch = 0;
        }
    }
}

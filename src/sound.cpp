/*
 * Moonwalker sound board: Z80 + two YM3438 (YM2612-compatible) chips and
 * the RF5C68 PCM chip.  FM music/effects are produced by ymfm
 * (BSD-3-Clause, Aaron Giles); the RF5C68 plays the streamed PCM tracks
 * (percussion and the dance-music cues).
 */
#include "sound.h"
#include "mcu.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern "C" {
#include "z80.h"
}

#include "ymfm.h"
#include "ymfm_opn.h"

#define SOUND_ROM_SIZE 0x200000
#define SOUND_RAM_SIZE 0x2000
#define SOUND_PCM_RAM  0x10000
#define RING_SAMPLES   65536

struct mw_ym_interface : public ymfm::ymfm_interface {
    bool irq = false;
    int64_t timer[2] = { -1, -1 };

    void ymfm_update_irq(bool asserted) override { irq = asserted; }

    void ymfm_set_timer(uint32_t tnum, int32_t duration_in_clocks) override
    {
        if (tnum < 2)
            timer[tnum] = (duration_in_clocks < 0) ? -1 : duration_in_clocks;
    }

    void tick(int clocks)
    {
        for (int t = 0; t < 2; t++) {
            if (timer[t] < 0) continue;
            timer[t] -= clocks;
            if (timer[t] <= 0) {
                timer[t] = -1;
                if (m_engine) m_engine->engine_timer_expired((uint32_t)t);
            }
        }
    }
};

struct s18_audio {
    s18_t *machine = nullptr;
    z80 cpu{};
    mw_ym_interface i1, i2;
    ymfm::ym3438 ym1{i1};
    ymfm::ym3438 ym2{i2};
    uint8_t rom[SOUND_ROM_SIZE];
    uint8_t ram[SOUND_RAM_SIZE];
    uint8_t pcm[SOUND_PCM_RAM];
    uint8_t bank = 0;
    int64_t ym_frac = 0;
    int64_t z80_remaining = 0;

    /* RF5C68 PCM */
    struct rf_chan {
        uint8_t enable = 0, env = 0, pan = 0;
        uint16_t step = 0, loopst = 0, start = 0;
        uint32_t addr = 0;
    } rf[8];
    uint8_t rf_cbank = 0, rf_enable = 0;
    uint16_t rf_wbank = 0;
    int64_t rf_frac = 0;
    int16_t rf_l = 0, rf_r = 0;

    int16_t ring[RING_SAMPLES];
    volatile unsigned ring_head = 0;
    volatile unsigned ring_tail = 0;

    static constexpr int sample_rate = 8000000 / 144;
};

static s18_audio *g_audio;

static void rf_reg_write(s18_audio *a, unsigned offset, uint8_t data);
static uint8_t rf_mem_read(s18_audio *a, uint16_t addr);

static uint8_t z80_read(void *user, uint16_t addr)
{
    s18_audio *a = (s18_audio *)user;
    if (addr < 0xA000) return a->rom[addr];
    if (addr < 0xC000) return a->rom[((unsigned)a->bank * 0x2000u + (addr - 0xA000)) & (SOUND_ROM_SIZE - 1)];
    if (addr < 0xE000) {
        if (!(addr & 0x1000) && (addr & 0xFFF) <= 8) return 0xFF;
        return rf_mem_read(a, addr);
    }
    return a->ram[addr & 0x1FFF];
}

static void z80_write(void *user, uint16_t addr, uint8_t data)
{
    s18_audio *a = (s18_audio *)user;
    if (addr >= 0xC000 && addr < 0xE000) {
        if (!(addr & 0x1000) && (addr & 0xFFF) <= 8)
            rf_reg_write(a, addr & 0x0F, data);
        else
            a->pcm[a->rf_wbank | (addr & 0xFFF)] = data;
    } else if (addr >= 0xE000) {
        a->ram[addr & 0x1FFF] = data;
    }
}

static uint8_t z80_port_in(z80 *z, uint8_t port)
{
    s18_audio *a = (s18_audio *)z->userdata;
    switch (port & 0xF0) {
    case 0x80: return a->ym1.read(port & 3);
    case 0x90: return a->ym2.read(port & 3);
    case 0xC0: return a->machine->mapper_to_sound; /* 315-5195 pread() */
    default: return 0xFF;
    }
}

static void z80_port_out(z80 *z, uint8_t port, uint8_t data)
{
    s18_audio *a = (s18_audio *)z->userdata;
    switch (port & 0xF0) {
    case 0x80: a->ym1.write(port & 3, data); break;
    case 0x90: a->ym2.write(port & 3, data); break;
    case 0xA0: a->bank = data; break;
    case 0xC0:
        a->machine->mapper_from_sound = data;
        s18_mcu_int1(a->machine->mcu);
        break;
    default: break;
    }
}

static uint8_t rf_mem_read(s18_audio *a, uint16_t addr)
{
    return a->pcm[a->rf_wbank | (addr & 0xFFF)];
}

static void rf_reg_write(s18_audio *a, unsigned offset, uint8_t data)
{
    s18_audio::rf_chan &chan = a->rf[a->rf_cbank];
    switch (offset) {
    case 0x00: chan.env = data; break;
    case 0x01: chan.pan = data; break;
    case 0x02: chan.step = (uint16_t)((chan.step & 0xFF00) | data); break;
    case 0x03: chan.step = (uint16_t)((chan.step & 0x00FF) | (data << 8)); break;
    case 0x04: chan.loopst = (uint16_t)((chan.loopst & 0xFF00) | data); break;
    case 0x05: chan.loopst = (uint16_t)((chan.loopst & 0x00FF) | (data << 8)); break;
    case 0x06:
        chan.start = data;
        if (!chan.enable) chan.addr = (uint32_t)chan.start << (8 + 11);
        break;
    case 0x07:
        a->rf_enable = (data >> 7) & 1;
        if (data & 0x40) a->rf_cbank = data & 7;
        else a->rf_wbank = (uint16_t)((data & 0x0F) << 12); /* A12-A15 */
        break;
    case 0x08:
        for (int i = 0; i < 8; i++) {
            a->rf[i].enable = (uint8_t)((~data >> i) & 1);
            if (!a->rf[i].enable) a->rf[i].addr = (uint32_t)a->rf[i].start << (8 + 11);
        }
        break;
    default: break;
    }
}

static void rf_generate(s18_audio *a)
{
    int32_t mixl = 0, mixr = 0;
    if (a->rf_enable) {
        for (int c = 0; c < 8; c++) {
            s18_audio::rf_chan &chan = a->rf[c];
            if (!chan.enable) continue;
            int lv = (chan.pan & 0x0F) * chan.env;
            int rv = ((chan.pan >> 4) & 0x0F) * chan.env;
            int sample = a->pcm[(chan.addr >> 11) & 0xFFFF];
            if (sample == 0xFF) {
                chan.addr = (uint32_t)chan.loopst << 11;
                sample = a->pcm[(chan.addr >> 11) & 0xFFFF];
                if (sample == 0xFF) continue;
            }
            chan.addr += chan.step;
            if (sample & 0x80) {
                sample &= 0x7F;
                mixl += (sample * lv) >> 5;
                mixr += (sample * rv) >> 5;
            } else {
                mixl -= (sample * lv) >> 5;
                mixr -= (sample * rv) >> 5;
            }
        }
    }
    if (mixl > 32767) mixl = 32767;
    if (mixl < -32768) mixl = -32768;
    if (mixr > 32767) mixr = 32767;
    if (mixr < -32768) mixr = -32768;
    a->rf_l = (int16_t)mixl;
    a->rf_r = (int16_t)mixr;
}

static int load(const char *dir, const char *name, uint8_t *dst, size_t expected)
{
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    size_t got = fread(dst, 1, expected, f);
    fclose(f);
    return got == expected ? 0 : -1;
}

extern "C" s18_audio_t *s18_audio_create(s18_t *machine, const char *romdir)
{
    s18_audio *a = new s18_audio();
    a->machine = machine;
    memset(a->rom, 0xFF, sizeof(a->rom));
    memset(a->ram, 0, sizeof(a->ram));
    memset(a->pcm, 0, sizeof(a->pcm));

    if (load(romdir, "epr-13225.a4", a->rom, 0x20000) ||
        load(romdir, "mpr-13219.b4", a->rom + 0x080000, 0x40000) ||
        load(romdir, "mpr-13220.b5", a->rom + 0x100000, 0x40000) ||
        load(romdir, "mpr-13249.b6", a->rom + 0x180000, 0x40000)) {
        fprintf(stderr, "sound: cannot load sound ROMs from %s\n", romdir);
        delete a;
        return nullptr;
    }

    z80_init(&a->cpu);
    a->cpu.read_byte = z80_read;
    a->cpu.write_byte = z80_write;
    a->cpu.port_in = z80_port_in;
    a->cpu.port_out = z80_port_out;
    a->cpu.userdata = a;
    a->ym1.reset();
    a->ym2.reset();
    a->ring_head = a->ring_tail = 0;
    g_audio = a;
    machine->audio = a;
    return a;
}

extern "C" void s18_audio_destroy(s18_audio_t *audio)
{
    if (audio) {
        if (audio->machine) audio->machine->audio = nullptr;
        if (g_audio == audio) g_audio = nullptr;
        delete audio;
    }
}

extern "C" void s18_audio_nmi(s18_audio_t *audio)
{
    if (audio) z80_gen_nmi(&audio->cpu);
}

static int g_audio_fast = 1;

extern "C" void s18_audio_set_fast(int on)
{
    g_audio_fast = on ? 1 : 0;
}

extern "C" void s18_audio_run(s18_audio_t *audio, int cycles)
{
    if (!audio) return;
    unsigned long start = audio->cpu.cyc;
    /* Deliver a pending YM timer interrupt if the Z80 can take one. */
    while ((long)(audio->cpu.cyc - start) < cycles) {
        if (audio->i1.irq && audio->cpu.iff1) {
            z80_gen_int(&audio->cpu, 0xFF);
            audio->i1.irq = false;
        }
        z80_step(&audio->cpu);
    }

    /* Muted mode: the Z80 still services the MCU handshake, but there is no
     * reason to run the FM/PCM synthesis when no device is consuming it. */
    if (g_audio_fast) {
        audio->i1.tick(cycles);
        audio->i2.tick(cycles);
        return;
    }

    /* One YM3438 sample every 144 input clocks (8 MHz / 144 = 55.6 kHz). */
    audio->ym_frac += cycles;
    int samples = (int)(audio->ym_frac / 144);
    audio->ym_frac %= 144;
    for (int i = 0; i < samples; i++) {
        audio->i1.tick(144);
        audio->i2.tick(144);
        audio->rf_frac += 180; /* RF5C68 is clocked at 10 MHz (not the Z80's 8) */
        while (audio->rf_frac >= 384) {
            audio->rf_frac -= 384;
            rf_generate(audio);
        }
        ymfm::ym3438::output_data o1{}, o2{};
        audio->ym1.generate(&o1, 1);
        audio->ym2.generate(&o2, 1);
        int32_t ymv = (o1.data[0] + o1.data[1] + o2.data[0] + o2.data[1]);
        /* The RF5C68 stereo pair is a full-level mono source; the old /8
         * buried the PCM track (the dance music) under the FM voices. */
        int32_t rfv = (audio->rf_l + audio->rf_r);
        int32_t mix = ymv + rfv;
        if (mix > 32767) mix = 32767;
        if (mix < -32768) mix = -32768;
        unsigned next = (audio->ring_head + 1) % RING_SAMPLES;
        if (next != audio->ring_tail) {
            audio->ring[audio->ring_head] = (int16_t)mix;
            audio->ring_head = next;
        }
    }
}

extern "C" int s18_audio_read(s18_audio_t *audio, int16_t *out, int frames)
{
    if (!audio) {
        memset(out, 0, (size_t)frames * sizeof(int16_t));
        return frames;
    }
    for (int i = 0; i < frames; i++) {
        unsigned tail = audio->ring_tail;
        if (tail == audio->ring_head) {
            out[i] = 0;
        } else {
            out[i] = audio->ring[tail];
            audio->ring_tail = (tail + 1) % RING_SAMPLES;
        }
    }
    return frames;
}

extern "C" int s18_audio_sample_rate(void)
{
    return s18_audio::sample_rate;
}

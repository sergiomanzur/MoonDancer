#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "m68k.h"
#include "s18.h"
#include "rom_loader.h"
#include "video.h"
#include "sound.h"
#include "mcu.h"
#include "settings.h"

void mw_m68k_init(s18_t *m);
void mw_m68k_reset(s18_t *m);
void mw_m68k_set_trace(int on, FILE *out);
void mw_m68k_set_record(int on);
void mw_m68k_dump_record(const char *path);
int mw_m68k_selftest(const uint8_t *rom, const uint8_t *key, int steps);
int mw_m68k_block_selftest(const uint8_t *rom, const uint8_t *key, int steps);

static uint64_t hash_bytes(uint64_t h, const void *data, size_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    for (size_t i = 0; i < len; i++)
        h = (h ^ p[i]) * 1099511628211ull;
    return h;
}

/* Semantic state: excludes the 68000 stack area so deterministic replay
 * comparisons do not fail on dead stack slots.  The full hash includes it. */
static uint64_t state_hash(s18_t *m, int exclude_stack)
{
    uint64_t h = 1469598103934665603ull;
    h = hash_bytes(h, m->workram, exclude_stack ? 0x3E00 : sizeof(m->workram));
    h = hash_bytes(h, m->tileram, sizeof(m->tileram));
    h = hash_bytes(h, m->textram, sizeof(m->textram));
    h = hash_bytes(h, m->spriteram, sizeof(m->spriteram));
    h = hash_bytes(h, m->paletteram, sizeof(m->paletteram));
    h = hash_bytes(h, m->vdp_vram, sizeof(m->vdp_vram));
    h = hash_bytes(h, m->vdp_cram, sizeof(m->vdp_cram));
    h = hash_bytes(h, m->vdp_vsram, sizeof(m->vdp_vsram));
    h = hash_bytes(h, m->vdp_regs, sizeof(m->vdp_regs));
    h = hash_bytes(h, &m->vdp_status, sizeof(m->vdp_status));
    h = hash_bytes(h, &m->vdp_control, sizeof(m->vdp_control));
    uint8_t fd = fd1094_state(&m->fd);
    h = hash_bytes(h, &fd, 1);
    return h;
}

static uint64_t rng_next(uint64_t *s)
{
    uint64_t x = *s;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    *s = x;
    return x;
}

/* Returns 0 on a parsed input line, -1 for comments/blank/EOF. */
static int parse_input_line(char *line, s18_inputs_t *in)
{
    char *p = line;
    while (*p == ' ' || *p == '\t') p++;
    if (*p == '#' || *p == '\n' || *p == '\r' || *p == '\0')
        return -1;
    unsigned v[5];
    if (sscanf(p, "%x %x %x %x %x", &v[0], &v[1], &v[2], &v[3], &v[4]) != 5)
        return -1;
    in->p1 = (uint8_t)v[0];
    in->p2 = (uint8_t)v[1];
    in->p3 = (uint8_t)v[2];
    in->service = (uint8_t)v[3];
    in->coinage = (uint8_t)v[4];
    return 0;
}

int main(int argc, char **argv)
{
    const char *romdir = "roms";
    int frames = 240;
    int trace = 0;
    int log_mapper = 0;
    const char *shot = NULL;
    const char *dump = NULL;
    const char *record = NULL;
    const char *tracefile = NULL;
    int selftest = 0;
    int block_selftest = 0;
    int hash = 0;
    int audio_on = 1;
    int audio_capture = 0;
    int play = 0;
    int demo = 0;
    int display_mode = MW_DISPLAY_ORIGINAL;
    int sprite_stats = 0;
    int shot_full = 0;
    const char *replay = NULL;
    const char *record_input = NULL;
    const char *hash_file = NULL;
    const char *watch = NULL;
    const char *nvram = NULL;
    const char *workhash = NULL;
    unsigned dsw = 0xFD;
    uint64_t random_seed = 0;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--rom-dir") && i + 1 < argc) romdir = argv[++i];
        else if (!strcmp(argv[i], "--frames") && i + 1 < argc) frames = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--trace")) trace = 1;
        else if (!strcmp(argv[i], "--log-mapper")) log_mapper = 1;
        else if (!strcmp(argv[i], "--shot") && i + 1 < argc) shot = argv[++i];
        else if (!strcmp(argv[i], "--dump") && i + 1 < argc) dump = argv[++i];
        else if (!strcmp(argv[i], "--audio")) audio_capture = 1;
        else if (!strcmp(argv[i], "--no-audio")) audio_on = 0;
        else if (!strcmp(argv[i], "--record") && i + 1 < argc) record = argv[++i];
        else if (!strcmp(argv[i], "--trace-file") && i + 1 < argc) tracefile = argv[++i];
        else if (!strcmp(argv[i], "--selftest") && i + 1 < argc) selftest = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--block-selftest") && i + 1 < argc) block_selftest = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--hash")) hash = 1;
        else if (!strcmp(argv[i], "--hash-file") && i + 1 < argc) hash_file = argv[++i];
        else if (!strcmp(argv[i], "--watch") && i + 1 < argc) watch = argv[++i];
        else if (!strcmp(argv[i], "--dsw") && i + 1 < argc) dsw = (unsigned)strtoul(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--nvram") && i + 1 < argc) nvram = argv[++i];
        else if (!strcmp(argv[i], "--workhash") && i + 1 < argc) workhash = argv[++i];
        else if (!strcmp(argv[i], "--play")) play = 1;
        else if (!strcmp(argv[i], "--demo")) demo = 1;
        else if (!strcmp(argv[i], "--replay") && i + 1 < argc) replay = argv[++i];
        else if (!strcmp(argv[i], "--record-input") && i + 1 < argc) record_input = argv[++i];
        else if (!strcmp(argv[i], "--random") && i + 1 < argc) random_seed = strtoull(argv[++i], NULL, 0);
    else if (!strcmp(argv[i], "--display") && i + 1 < argc) display_mode = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--sprite-stats")) sprite_stats = 1;
    else if (!strcmp(argv[i], "--shot-full")) shot_full = 1;
        else {
            fprintf(stderr,
                    "usage: %s [--rom-dir DIR] [--frames N] [--trace] [--log-mapper]\n"
                    "          [--shot FILE] [--dump PREFIX] [--audio] [--no-audio] [--play] [--demo]\n"
                    "          [--replay FILE] [--record-input FILE] [--random SEED]\n"
                    "          [--hash] [--hash-file FILE] [--record FILE] [--trace-file FILE]\n"
                    "          [--nvram FILE] [--dsw HEX] [--watch OFFSET]\n"
                    "          [--selftest N]\n", argv[0]);
            return 1;
        }
    }

    static s18_t machine;
    uint8_t rom[0x80000], key[0x2000];
    if (mw_load_roms(romdir, rom, key))
        return 1;
    if (selftest > 0) return mw_m68k_selftest(rom, key, selftest);
    if (block_selftest > 0) return mw_m68k_block_selftest(rom, key, block_selftest);

    s18_init(&machine, rom, key);
    machine.inputs.dsw = (uint8_t)dsw;
    machine.log_mapper = log_mapper;
    mw_m68k_init(&machine);
    mw_m68k_reset(&machine);
    /* --nvram loads the workram image after the board reset has cleared it,
     * so the continuation test starts with the captured gameplay memory.
     * (The game's own boot code still initialises its variables, so this is
     * a memory image, not a resumable save state.) */
    if (nvram) {
        if (s18_load_nvram(&machine, nvram) == 0) {
            fprintf(stderr, "NVRAM: loaded %s\n", nvram);
        } else {
            /* A short image leaves a partial copy in work RAM; reset again so
             * "starting clean" means what it says (same contract as the SDL
             * front end). */
            mw_m68k_reset(&machine);
            fprintf(stderr, "NVRAM: starting clean (no usable image at %s)\n", nvram);
        }
    }
    mw_m68k_set_trace(trace || tracefile, tracefile ? fopen(tracefile, "w") : NULL);
    if (record) mw_m68k_set_record(1);
    if (s18_video_init(&machine, romdir)) return 1;
    if (display_mode < 0 || display_mode >= MW_DISPLAY_COUNT)
        display_mode = MW_DISPLAY_ORIGINAL;
    s18_set_view(&machine, display_mode != MW_DISPLAY_ORIGINAL);
    s18_mcu_t *mcu = s18_mcu_create(&machine, romdir);
    /* The protection MCU services the vblank IRQ and waits for the sound
     * CPU to acknowledge commands; the board deadlocks if the Z80 is not
     * running.  Always run it, but only capture samples when asked. */
    s18_audio_t *audio = audio_on ? s18_audio_create(&machine, romdir) : NULL;
    if (audio_on && !audio) {
        fprintf(stderr, "could not create the sound CPU (missing audio ROMs)\n");
        return 1;
    }
    s18_audio_set_fast(audio_capture ? 0 : 1);
    int16_t *samples = NULL;
    size_t sample_count = 0, sample_cap = 0;
    if (audio_capture) { sample_cap = 2000000; samples = (int16_t *)malloc(sample_cap * sizeof(int16_t)); }

    FILE *replay_fp = replay ? fopen(replay, "r") : NULL;
    if (replay && !replay_fp) {
        fprintf(stderr, "could not open replay file %s\n", replay);
        return 1;
    }
    FILE *input_log = record_input ? fopen(record_input, "w") : NULL;
    if (record_input && !input_log) {
        fprintf(stderr, "could not open input log %s\n", record_input);
        return 1;
    }
    FILE *workhash_fp = workhash ? fopen(workhash, "w") : NULL;
    FILE *hash_fp = hash_file ? fopen(hash_file, "w") : NULL;
    if (hash_file && !hash_fp) {
        fprintf(stderr, "could not open hash file %s\n", hash_file);
        return 1;
    }

    fprintf(stderr, "reset PC=%06X SP=%08X\n",
            m68k_get_reg(NULL, M68K_REG_PC), m68k_get_reg(NULL, M68K_REG_SP));

    uint64_t rng = random_seed ? random_seed : 0x9E3779B97F4A7C15ull;
    uint8_t rnd_p1 = 0xFF, rnd_service = 0xFF;

    for (int f = 0; f < frames; f++) {
        s18_inputs_t in = machine.inputs;
        in.p1 = in.p2 = in.p3 = 0xFF;
        in.service = 0xFF;
        int have_inputs = 0;

        if (replay_fp) {
            char line[256];
            while (fgets(line, sizeof(line), replay_fp)) {
                if (parse_input_line(line, &in) == 0) {
                    have_inputs = 1;
                    break;
                }
            }
        } else if (random_seed) {
            /* Structured fuzzing: get into the game, then vary movement and
             * buttons while occasionally pulsing coin/start. */
            if (f >= 900 && f < 940) in.service &= (uint8_t)~0x01;
            if (f >= 1000 && f < 1040) in.service &= (uint8_t)~0x10;
            if (f >= 1500 && (f % 600) < 8) in.service &= (uint8_t)~0x01;
            if (f >= 1800 && (f % 900) < 8) in.service &= (uint8_t)~0x10;
            if (f >= 1100 && (f % 6) == 0) {
                uint64_t r = rng_next(&rng);
                rnd_p1 = (uint8_t)(r & 0xFF);
                rnd_service = (uint8_t)((r >> 8) & 0xFF);
            }
            if (f >= 1100) {
                /* Bias toward movement and attacks so the game actually
                 * plays instead of idling in one place. */
                uint8_t r = rnd_p1;
                if (r & 0x10) in.p1 &= (uint8_t)~0x40;   /* right */
                else if (r & 0x20) in.p1 &= (uint8_t)~0x80; /* left */
                if (r & 0x40) in.p1 &= (uint8_t)~0x20;   /* up */
                if (r & 0x80) in.p1 &= (uint8_t)~0x10;   /* down */
                if (r & 0x03) in.p1 &= (uint8_t)~0x01;   /* attack */
                if (r & 0x0C) in.p1 &= (uint8_t)~0x02;   /* jump */
                /* Rare service/start edges reach test/continue screens. */
                if ((rnd_service & 0x3F) == 0) in.service &= (uint8_t)~0x04;
                if ((rnd_service & 0x3F) == 1) in.service &= (uint8_t)~0x10;
            }
            have_inputs = 1;
        } else if (play || demo) {
            if (f >= 900 && f < 940) in.service &= (uint8_t)~0x01;
            if (f >= 1000 && f < 1040) in.service &= (uint8_t)~0x10;
            if (demo && f >= 1100) {
                if ((f / 90) & 1) in.p1 &= (uint8_t)~0x40;      /* right */
                else in.p1 &= (uint8_t)~0x80;                    /* left */
                if (f % 20 < 4) in.p1 &= (uint8_t)~0x01;         /* attack */
                if (f % 120 < 30) in.p1 &= (uint8_t)~0x20;       /* up */
            }
            have_inputs = 1;
        }

        if (have_inputs)
            s18_set_inputs(&machine, &in);
        if (input_log)
            fprintf(input_log, "%02X %02X %02X %02X %02X\n",
                    in.p1, in.p2, in.p3, in.service, in.coinage);

        s18_run_frame(&machine);
        if (sprite_stats && (f % 100) == 0) {
            int count = 0, minx = 0x7FFF, maxx = -0x8000, over = 0;
            for (int si = 0; si < 0x800; si += 16) {
                unsigned w1 = ((unsigned)machine.spriteram[si + 2] << 8) |
                              machine.spriteram[si + 3];
                unsigned w2 = ((unsigned)machine.spriteram[si + 4] << 8) |
                              machine.spriteram[si + 5];
                if (w2 & 0x8000) break;
                int x = (int)(w1 & 0x7FF);
                int sx = x - 184;
                if (sx < minx) minx = sx;
                if (sx > maxx) maxx = sx;
                if (x >= 512) over++;
                count++;
            }
            fprintf(stderr, "SPRITES f=%d n=%d sx=[%d,%d] x>=512=%d\n",
                    f, count, count ? minx : 0, count ? maxx : 0, over);
        }
        if (audio_capture && samples) {
            int want = 1024;
            if (sample_count + (size_t)want > sample_cap) want = (int)(sample_cap - sample_count);
            if (want > 0) sample_count += (size_t)s18_audio_read(audio, samples + sample_count, want);
        }
        if (workhash_fp) {
            uint64_t h = 1469598103934665603ull;
            for (unsigned wi = 0; wi < 0x3E00; wi++)
                h = (h ^ machine.workram[wi]) * 1099511628211ull;
            fprintf(workhash_fp, "%d %016llX\n", f, (unsigned long long)h);
        }
        if (watch) {
            unsigned woff = (unsigned)strtoul(watch, NULL, 0) & 0x3FFF;
            fprintf(stdout, "WATCH %d %02X %06X\n", f, machine.workram[woff],
                    (unsigned)m68k_get_reg(NULL, M68K_REG_PC));
            fflush(stdout);
        }
        if (hash || hash_fp) {
            uint64_t sem = state_hash(&machine, 1);
            uint64_t full = state_hash(&machine, 0);
            if (hash_file)
                fprintf(hash_fp, "%d %016llX %016llX %06X\n", f,
                        (unsigned long long)sem, (unsigned long long)full,
                        (unsigned)m68k_get_reg(NULL, M68K_REG_PC));
            if (hash) {
                fprintf(stdout, "HASH %d %016llX %016llX PC=%06X\n", f,
                        (unsigned long long)sem, (unsigned long long)full,
                        (unsigned)m68k_get_reg(NULL, M68K_REG_PC));
                fflush(stdout);
            }
        }
        if ((f % 10) == 0 || f == frames - 1) {
            fprintf(stderr, "frame %4d PC=%06X SR=%04X D0=%08X A7=%08X fd=%02X\n",
                    f, m68k_get_reg(NULL, M68K_REG_PC), m68k_get_reg(NULL, M68K_REG_SR),
                    m68k_get_reg(NULL, M68K_REG_D0), m68k_get_reg(NULL, M68K_REG_A7),
                    fd1094_state(&machine.fd));
        }
    }

    int nz = 0;
    for (int i = 0; i < 0x4000; i++) if (machine.workram[i]) nz++;
    fprintf(stderr, "workram nonzero: %d bytes\n", nz);
    nz = 0;
    for (int i = 0; i < 0x10000; i++) if (machine.tileram[i]) nz++;
    fprintf(stderr, "tileram nonzero: %d bytes\n", nz);
    nz = 0;
    for (int i = 0; i < 0x800; i++) if (machine.spriteram[i]) nz++;
    fprintf(stderr, "spriteram nonzero: %d bytes\n", nz);
    nz = 0;
    for (int i = 0; i < 0x10000; i++) if (machine.vdp_vram[i]) nz++;
    fprintf(stderr, "vdp vram nonzero: %d bytes\n", nz);

    if (record) mw_m68k_dump_record(record);
    if (dump) {
        char path[1024];
        #define DUMP(name, ptr, len) do { snprintf(path, sizeof(path), "%s_%s.bin", dump, name); FILE *df = fopen(path, "wb"); if (df) { fwrite(ptr, 1, len, df); fclose(df); } } while (0)
        DUMP("tileram", machine.tileram, sizeof(machine.tileram));
        DUMP("textram", machine.textram, sizeof(machine.textram));
        DUMP("spriteram", machine.spriteram, sizeof(machine.spriteram));
        DUMP("palette", machine.paletteram, sizeof(machine.paletteram));
        DUMP("vram", machine.vdp_vram, sizeof(machine.vdp_vram));
        DUMP("cram", machine.vdp_cram, sizeof(machine.vdp_cram));
        DUMP("vsram", machine.vdp_vsram, sizeof(machine.vdp_vsram));
        DUMP("workram", machine.workram, sizeof(machine.workram));
        DUMP("vdpregs", machine.vdp_regs, sizeof(machine.vdp_regs));
        DUMP("vdpstatus", &machine.vdp_status, sizeof(machine.vdp_status));
        DUMP("vdpcontrol", &machine.vdp_control, sizeof(machine.vdp_control));
        DUMP("tilebank", machine.tilebank, sizeof(machine.tilebank));
        { uint8_t mr[128], ms[128]; s18_mcu_dump_state(mcu, mr, ms); DUMP("mcuram", mr, sizeof(mr)); DUMP("mcusfr", ms, sizeof(ms)); }
        #undef DUMP
    }
    if (shot) {
        uint32_t *frame = s18_render_frame(&machine);
        int sw = shot_full ? machine.view_w : mw_display_crop_width(display_mode);
        int sx = shot_full ? 0 : mw_display_crop_x(display_mode, machine.view_w);
        if (s18_save_bmp_rect(shot, frame, machine.view_w, sx, 0, sw, S18_SCREEN_H))
            fprintf(stderr, "could not write %s\n", shot);
        else
            fprintf(stderr, "wrote %s\n", shot);
    }
    if (audio_capture && samples) {
        FILE *wf = fopen("build/audio.wav", "wb");
        if (wf) {
            uint32_t data_bytes = (uint32_t)(sample_count * 2);
            uint32_t riff = 36 + data_bytes;
            uint16_t fmt = 1, ch = 1, bits = 16;
            uint32_t rate = (uint32_t)s18_audio_sample_rate();
            uint32_t byte_rate = rate * 2;
            uint16_t block = 2;
            uint32_t fmt_size = 16;
            fwrite("RIFF", 1, 4, wf); fwrite(&riff, 4, 1, wf); fwrite("WAVE", 1, 4, wf);
            fwrite("fmt ", 1, 4, wf); fwrite(&fmt_size, 4, 1, wf);
            fwrite(&fmt, 2, 1, wf); fwrite(&ch, 2, 1, wf); fwrite(&rate, 4, 1, wf);
            fwrite(&byte_rate, 4, 1, wf); fwrite(&block, 2, 1, wf); fwrite(&bits, 2, 1, wf);
            fwrite("data", 1, 4, wf); fwrite(&data_bytes, 4, 1, wf);
            fwrite(samples, 2, sample_count, wf);
            fclose(wf);
            fprintf(stderr, "wrote build/audio.wav (%zu samples)\n", sample_count);
        }
        free(samples);
    }
    if (nvram)
        s18_save_nvram(&machine, nvram);
    if (replay_fp) fclose(replay_fp);
    if (input_log) fclose(input_log);
    if (workhash_fp) fclose(workhash_fp);
    if (hash_fp) fclose(hash_fp);
    s18_audio_destroy(audio);
    s18_mcu_destroy(mcu);
    s18_video_shutdown();
    s18_shutdown(&machine);
    return 0;
}

/*
 * Moonwalker native port - SDL2 front end.
 */
#include <SDL.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#define MW_MKDIR(p) _mkdir(p)
#else
#include <sys/stat.h>
#define MW_MKDIR(p) mkdir(p, 0755)
#endif

#include "s18.h"
#include "rom_loader.h"
#include "video.h"
#include "cpu68k.h"
#include "sound.h"
#include "mcu.h"
#include "settings.h"
#include "input.h"
#include "ui.h"
#include "zip_loader.h"
#ifdef _WIN32
#include <windows.h>
#include <dwmapi.h>
#include <SDL_syswm.h>
#endif

static uint8_t key_state[SDL_NUM_SCANCODES];
static uint8_t key_latch[SDL_NUM_SCANCODES];
static mw_input_result g_input; /* keyboard + 3 pads, refreshed every frame */
static int g_demo;
static uint8_t g_dsw = 0xFD;
static s18_audio_t *g_sdl_audio;
static SDL_AudioDeviceID g_audio_dev;
static mw_settings g_settings;
static mw_cheats g_cheats; /* session-only, all off; never saved */

static void sdl_audio_cb(void *userdata, Uint8 *stream, int len)
{
    (void)userdata;
    int16_t *samples = (int16_t *)stream;
    int count = len / 2;
    s18_audio_read(g_sdl_audio, samples, count);
    float vol = (float)g_settings.volume / 100.0f;
    if (vol < 0.999f) {
        for (int i = 0; i < count; i++)
            samples[i] = (int16_t)(samples[i] * vol);
    }
}

/* Open (on != 0) or close the SDL audio device for the shared sound board.
 *
 * g_sdl_audio itself is never invalidated: s18_audio_read() is only ever
 * called from sdl_audio_cb, which SDL invokes on its audio thread only while
 * a device is open.  Pausing and closing therefore leaves no reader at all,
 * so the emulator keeps running the Z80/MCU handshake with FM/PCM synthesis
 * skipped (s18_audio_set_fast(1)), exactly like the old muted default.
 * Reopening uses the same want spec as boot so the sample rate never changes
 * underneath the ring buffer. */
static void audio_apply(int on)
{
    if (on) {
        s18_audio_set_fast(0);
        if (g_audio_dev || !g_sdl_audio) return;
        SDL_AudioSpec want;
        SDL_zero(want);
        want.freq = s18_audio_sample_rate();
        want.format = AUDIO_S16SYS;
        want.channels = 1;
        want.samples = 1024;
        want.callback = sdl_audio_cb;
        g_audio_dev = SDL_OpenAudioDevice(NULL, 0, &want, NULL, 0);
        if (g_audio_dev) SDL_PauseAudioDevice(g_audio_dev, 0);
    } else {
        if (g_audio_dev) {
            SDL_PauseAudioDevice(g_audio_dev, 1);
            SDL_CloseAudioDevice(g_audio_dev);
            g_audio_dev = 0;
        }
        s18_audio_set_fast(1);
    }
}

static int pressed(SDL_Scancode code)
{
    return key_state[code] != 0 || key_latch[code] != 0;
}

/* The input module reports MW_IN_* action bits; the machine wants the S18
 * port layout (active-low): LEFT 0x80 RIGHT 0x40 UP 0x20 DOWN 0x10
 * B1 0x01 B2 0x02 B3 0x04. */
static uint8_t game_action_bits(uint8_t actions)
{
    uint8_t bits = 0;
    if (actions & MW_IN_LEFT)  bits |= 0x80;
    if (actions & MW_IN_RIGHT) bits |= 0x40;
    if (actions & MW_IN_UP)    bits |= 0x20;
    if (actions & MW_IN_DOWN)  bits |= 0x10;
    if (actions & MW_IN_B1)    bits |= 0x01;
    if (actions & MW_IN_B2)    bits |= 0x02;
    if (actions & MW_IN_B3)    bits |= 0x04;
    return bits;
}

static void update_inputs(s18_t *m)
{
    s18_inputs_t in = m->inputs;
    in.p1 = in.p2 = in.p3 = 0xFF;
    in.service = 0xFF;
    in.coinage = 0xFF;
    in.dsw = g_dsw;

    /* g_input already holds the merged keyboard + pad state for all three
     * players (see src/input.h): a single mapper, no per-device branches. */
    in.p1 &= (uint8_t)~game_action_bits(g_input.actions[0]);
    in.p2 &= (uint8_t)~game_action_bits(g_input.actions[1]);
    in.p3 &= (uint8_t)~game_action_bits(g_input.actions[2]);

    /* Service: the pads contribute start/coin bits; the keyboard's service
     * keys keep their original layout untouched. */
    in.service &= (uint8_t)~(g_input.start[0] | g_input.coin[0] |
                             g_input.start[1] | g_input.coin[1] |
                             g_input.start[2] | g_input.coin[2]);
    if (pressed(SDL_SCANCODE_5))  in.service &= (uint8_t)~0x01;
    if (pressed(SDL_SCANCODE_6))  in.service &= (uint8_t)~0x02;
    if (pressed(SDL_SCANCODE_7))  in.service &= (uint8_t)~0x08;
    if (pressed(SDL_SCANCODE_8))  in.service &= (uint8_t)~0x40;
    if (pressed(SDL_SCANCODE_1))  in.service &= (uint8_t)~0x10;
    if (pressed(SDL_SCANCODE_2))  in.service &= (uint8_t)~0x20;
    if (pressed(SDL_SCANCODE_TAB)) in.service &= (uint8_t)~0x04;
    if (pressed(SDL_SCANCODE_3))  in.service &= (uint8_t)~0x80;

    if (g_demo) {
        int64_t f = m->frame_count;
        if (f >= 900 && f < 940) in.service &= (uint8_t)~0x01;
        if (f >= 1000 && f < 1040) in.service &= (uint8_t)~0x10;
        if (f >= 1100) {
            if ((f / 90) & 1) in.p1 &= (uint8_t)~0x40;
            else in.p1 &= (uint8_t)~0x80;
            if (f % 20 < 4) in.p1 &= (uint8_t)~0x01;
            if (f % 120 < 30) in.p1 &= (uint8_t)~0x20;
        }
    }

    s18_set_inputs(m, &in);
}

static SDL_Rect game_rect(SDL_Window *window, int src_w, int src_h, int integer_scale)
{
    int ww = 0, wh = 0;
    SDL_GetWindowSize(window, &ww, &wh);
    SDL_Rect r = { 0, 0, 0, 0 };
    if (ww <= 0 || wh <= 0 || src_w <= 0 || src_h <= 0) return r;
    const float aspect = (float)src_w / (float)src_h;
    if (integer_scale) {
        int sx = ww / src_w;
        int sy = wh / src_h;
        int sc = sx < sy ? sx : sy;
        if (sc < 1) sc = 1;
        r.w = src_w * sc;
        r.h = src_h * sc;
    } else {
        if ((float)ww / (float)wh > aspect) {
            r.h = wh;
            r.w = (int)(wh * aspect + 0.5f);
        } else {
            r.w = ww;
            r.h = (int)(ww / aspect + 0.5f);
        }
    }
    r.x = (ww - r.w) / 2;
    r.y = (wh - r.h) / 2;
    return r;
}

static void apply_fullscreen(SDL_Window *window, int on)
{
    SDL_SetWindowFullscreen(window, on ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
}

/* Window geometry is user-editable in settings.ini, so it can hold anything
 * (0, negative, a stale monitor size).  Clamp at application time, before
 * the value reaches SDL_CreateWindow/SDL_SetWindowSize: 640x448 is the
 * SDL_SetWindowMinimumSize floor below, and the upper bound keeps garbage
 * like INT_MAX from turning into a degenerate window.  The clamped value is
 * written back so the panel and the saved file agree. */
static void clamp_window_size(int *w, int *h)
{
    if (!w || !h) return;
    int cw = *w;
    int ch = *h;
    if (cw <= 0) cw = 1280;   /* 0x0 = "remember nothing", use the preset */
    if (ch <= 0) ch = 720;
    if (cw < 640) cw = 640;
    if (ch < 448) ch = 448;
    if (cw > 7680) cw = 7680;
    if (ch > 4320) ch = 4320;
    *w = cw;
    *h = ch;
}

/* One RGB channel scaled by an 8.8 fixed-point multiplier. */
static uint32_t shade_pixel(uint32_t p, unsigned mul)
{
    uint32_t r = ((((p >> 16) & 0xFFu) * mul) >> 8) & 0xFFu;
    uint32_t g = ((((p >> 8) & 0xFFu) * mul) >> 8) & 0xFFu;
    uint32_t b = (((p & 0xFFu) * mul) >> 8) & 0xFFu;
    return (p & 0xFF000000u) | (r << 16) | (g << 8) | b;
}

/* Source-space CRT presets.  Runs on the full 560x224 frame between
 * s18_render_frame() and SDL_UpdateTexture(), so the pattern is one scanline
 * per emulated line at any window scale (the retired window-space overlay
 * beat against the 224 source lines at non-integer scales).
 *
 *   0 OFF        byte-exact no-op: returns before touching the buffer
 *   1 SCANLINES  odd source lines scaled toward black by intensity*0.45/100
 *   2 CRT        SCANLINES plus a vertical 3-column aperture grille at half
 *                that factor (bloom approximation intentionally omitted:
 *                not wanted in the 224-line source frame)
 *
 * `intensity` is 0..100; 0 is also a no-op because nothing is attenuated. */
static void apply_shader(uint32_t *frame, int w, int h, int preset, int intensity)
{
    if (!frame || w <= 0 || h <= 0 || preset <= 0 || intensity <= 0) return;
    if (intensity > 100) intensity = 100;
    if (preset > 2) preset = 2;

    const float f = (float)intensity / 100.0f;
    const unsigned line_mul = (unsigned)((1.0f - f * 0.45f) * 256.0f + 0.5f);
    const unsigned grille_mul = (unsigned)((1.0f - f * 0.45f * 0.5f) * 256.0f + 0.5f);

    for (int y = 1; y < h; y += 2) {
        uint32_t *row = frame + (size_t)y * (size_t)w;
        for (int x = 0; x < w; x++)
            row[x] = shade_pixel(row[x], line_mul);
    }
    if (preset != 2) return;

    /* Aperture grille: one attenuated column per 3-pixel period, on every
     * line (so it reads as vertical lines, not a checkerboard). */
    for (int y = 0; y < h; y++) {
        uint32_t *row = frame + (size_t)y * (size_t)w;
        for (int x = 0; x < w; x += 3)
            row[x] = shade_pixel(row[x], grille_mul);
    }
}

/* Per-frame cheat writes.  Values and addresses are the verified table in
 * docs/cheats-addresses.md (Task 6); every offset is a byte in m->workram.
 *
 * Called immediately after s18_run_frame() returns, so the game reads the
 * frozen value on its next frame.  A single write would be overwritten by the
 * next hit/death/dance, hence one write per frame rather than "refill once".
 *
 * The HUD digits are drawn from the value captured at the start of the frame,
 * so a freeze shows up one frame late.  Judge it by surviving (lives, health)
 * or by the dance staying available, not by the digit. */
static void cheats_apply(s18_t *m, const mw_cheats *c)
{
    /* Player status blocks: +0x08 lives, +0x0D player id, +0x29 magic charge.
     * Player object blocks (+0x1C health) are 0xC0 apart. */
    static const uint16_t k_pid[3] = { 0x1A5D, 0x1A9D, 0x1ADD };
    static const uint16_t k_lives[3] = { 0x1A58, 0x1A98, 0x1AD8 };
    static const uint16_t k_magic[3] = { 0x1A79, 0x1AB9, 0x1AF9 };
    static const uint16_t k_health[3] = { 0x001C, 0x00DC, 0x019C };
    if (!m || !c) return;

    /* Credits live outside the player blocks and are also shown on the attract
     * screen, so this one applies before a game starts too.  0x2000 is the
     * live coin counter; 0x2029 is the DSW lives template and is never
     * written. */
    if (c->free_credits) m->workram[0x2000] = 9;

    /* Gameplay cheats only for slots that are actually in play: the status
     * blocks are zeroed in attract, and the player id byte (0x37/0x38/0x39)
     * is the stable marker for "this player joined".  Writing lives/health to
     * an idle slot would invent a phantom player row.  This is cleaner than
     * tracking game_started (which would miss a pad-only coin+start). */
    for (int p = 0; p < 3; p++) {
        if (!m->workram[k_pid[p]]) continue;
        /* Freeze 9: the counter (spare lives = value - 1) is subtracted on
         * death, so a per-frame write keeps the player alive indefinitely.
         * This is the freeze the doc verified. */
        if (c->infinite_lives) m->workram[k_lives[p]] = 9;
        /* Freeze at 0x30 under either SW2:4 vitality setting.  That is the Low
         * max (the default DSW); on High it reads 75% but still prevents
         * death, while 0x40 on Low would overrun the bar graphic. */
        if (c->invincibility) m->workram[k_health[p]] = 0x30;
        /* One dance spends one charge, so refill every frame.  5 keeps the
         * HUD icon row inside the 1P column (9 runs into the CREDIT text). */
        if (c->infinite_magic) m->workram[k_magic[p]] = 5;
    }
}

static int extract_archive(const char *zip, char *romdir, int cap)
{
    char *pref = SDL_GetPrefPath("MoonwalkerRecomp", "Moonwalker");
    if (!pref) return 0;
    char cache[2048];
    snprintf(cache, sizeof(cache), "%sroms", pref);
    SDL_free(pref);
    return mw_extract_zip(zip, cache, romdir, cap) == 0;
}

int main(int argc, char **argv)
{
    const char *romdir = "roms";
    const char *zip = NULL;
    const char *nvram = "nvram/mwalk/nvram";
    int nvram_on = 1;
    int start_settings = 0;
    int cli_audio = 0;
    int cli_display = -1;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--rom-dir") && i + 1 < argc) romdir = argv[++i];
        else if (!strcmp(argv[i], "--zip") && i + 1 < argc) zip = argv[++i];
        else if (!strcmp(argv[i], "--demo")) g_demo = 1;
        else if (!strcmp(argv[i], "--audio")) cli_audio = 1;
        else if (!strcmp(argv[i], "--dsw") && i + 1 < argc) g_dsw = (uint8_t)strtoul(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--nvram") && i + 1 < argc) nvram = argv[++i];
        else if (!strcmp(argv[i], "--no-nvram")) nvram_on = 0;
        else if (!strcmp(argv[i], "--settings")) start_settings = 1;
        else if (!strcmp(argv[i], "--display") && i + 1 < argc) cli_display = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--help")) {
            fprintf(stderr, "usage: %s [--rom-dir DIR | --zip ZIP] [--demo] [--audio] [--dsw HEX] [--nvram FILE] [--no-nvram] [--display 0..3]\n", argv[0]);
            return 0;
        }
    }

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_GAMECONTROLLER | SDL_INIT_AUDIO) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }

    char settings_path[2048];
    mw_settings_path(settings_path, (int)sizeof(settings_path));
    mw_settings_load(&g_settings, settings_path);
    if (cli_display >= 0 && cli_display < MW_DISPLAY_COUNT)
        g_settings.display_mode = cli_display;

    /* Input: open up to three controllers and load the per-player maps.
     * The module owns gamepad hot-plug and binding capture from here on. */
    mw_input_init();
    mw_input_load_maps(&g_settings);

    /* Sound follows the saved setting (on by default); --audio and MW_AUDIO
     * stay force-on overrides and win over the panel toggle. */
    const int audio_forced = cli_audio || getenv("MW_AUDIO") != NULL;
    int audio_on = audio_forced || g_settings.sound_on;

    static char extracted_romdir[2048];
    if (zip) {
        if (!extract_archive(zip, extracted_romdir, (int)sizeof(extracted_romdir))) {
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Moonwalker ROM archive",
                "Could not read the ZIP archive.\n\nSelect the Moonwalker (World) mwalk.zip with the "
                "System 18 ROMs and the 315-5437.ic4 MCU.", NULL);
            SDL_Quit();
            return 1;
        }
        romdir = extracted_romdir;
    }

    static s18_t machine;
    uint8_t rom[0x80000], key[0x2000];
    if (mw_load_roms(romdir, rom, key)) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Moonwalker ROMs",
            "Could not load the Moonwalker ROM set.\n\nSelect mwalk.zip, or place the extracted "
            "ROM files in the roms folder.", NULL);
        SDL_Quit();
        return 1;
    }

    s18_init(&machine, rom, key);
    if (nvram_on) {
        MW_MKDIR("nvram");
        MW_MKDIR("nvram/mwalk");
        s18_load_nvram(&machine, nvram);
    }
    mw_m68k_init(&machine);
    mw_m68k_reset(&machine);
    machine.display_enable = 1;
    machine.vdp_enable = 0;
    /* Keep the widescreen game view (and expanded culling) active from
     * boot.  Attract screens are still shown in 4:3 by cropping the center
     * 320 columns; changing the game view mid-cutscene changes entity
     * culling and can softlock the intro. */
    s18_set_view(&machine, 1);
    if (s18_video_init(&machine, romdir)) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Moonwalker ROMs",
            "The ROM set is missing the System 18 tile or sprite ROMs.", NULL);
        SDL_Quit();
        return 1;
    }

    s18_mcu_t *mcu = s18_mcu_create(&machine, romdir);
    if (!mcu) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Moonwalker",
            "The MCU ROM could not be loaded from the archive.", NULL);
        SDL_Quit();
        return 1;
    }
    /* The protection MCU deadlocks if the sound CPU is absent, so always
     * run it.  FM/PCM synthesis and the SDL device follow the SOUND setting
     * (on by default) through audio_apply(); the Z80/MCU handshake keeps
     * running even while muted. */
    s18_audio_t *audio = s18_audio_create(&machine, romdir);
    if (!audio) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Moonwalker",
            "The audio ROM could not be loaded from the archive.", NULL);
        SDL_Quit();
        return 1;
    }

    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");
    clamp_window_size(&g_settings.window_w, &g_settings.window_h);
    SDL_Window *window = SDL_CreateWindow("Michael Jackson's Moonwalker",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        g_settings.window_w, g_settings.window_h,
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!window) {
        fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        return 1;
    }
    SDL_SetWindowMinimumSize(window, 640, 448);
#ifdef _WIN32
    {
        SDL_SysWMinfo info;
        SDL_VERSION(&info.version);
        if (SDL_GetWindowWMInfo(window, &info) && info.subsystem == SDL_SYSWM_WINDOWS) {
            BOOL dark = TRUE;
            if (FAILED(DwmSetWindowAttribute(info.info.win.window, 20, &dark, sizeof(dark))))
                DwmSetWindowAttribute(info.info.win.window, 19, &dark, sizeof(dark));
        }
    }
#endif
    SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer)
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    if (!renderer) {
        fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError());
        return 1;
    }
    if (g_settings.fullscreen)
        apply_fullscreen(window, 1);

    SDL_Texture *texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING, S18_MAX_SCREEN_W, S18_SCREEN_H);
    if (!texture) {
        fprintf(stderr, "SDL_CreateTexture: %s\n", SDL_GetError());
        return 1;
    }
    SDL_SetTextureScaleMode(texture, SDL_ScaleModeNearest);

    const char *base_sdl = SDL_GetBasePath();
    char base[2048];
    snprintf(base, sizeof(base), "%s", base_sdl ? base_sdl : "./");
    mw_ui *ui = mw_ui_create(window, renderer, base, NULL);
    if (!ui) {
        fprintf(stderr, "Could not initialize the settings overlay.\n");
        return 1;
    }

    g_sdl_audio = audio;
    audio_apply(audio_on);

    int running = 1;
    int paused = 0;
    int game_started = 0;
    int settings_open = start_settings;
    int settings_dirty = 0;
    int applied_fullscreen = g_settings.fullscreen;
    int applied_audio = audio_on;
    int applied_window_w = g_settings.window_w;
    int applied_window_h = g_settings.window_h;
    Uint32 last_save = 0;
    double fps = 60.0;
    Uint64 fps_last = SDL_GetPerformanceCounter();
    uint64_t last = SDL_GetPerformanceCounter();
    const double frame_time = 1.0 / S18_FPS;

    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            /* The rebinding capture belongs to the CONTROLLERS panel: while it
             * waits, the panel owns the key that arrives, so the host hotkeys
             * (F1/ESC/P/F5/F11/F12) must not also fire on it. */
            int capture_busy = mw_input_capture_active();
            mw_ui_process_event(ui, &event);
            /* Hot-plug, binding capture and the pad's own buttons.  The
             * keyboard state is fed through mw_input_set_keyboard() below so
             * the module and its harness share one merge path. */
            mw_input_process_event(&event);
            switch (event.type) {
            case SDL_QUIT:
                running = 0;
                break;
            case SDL_KEYDOWN:
                if (capture_busy) break;
                key_state[event.key.keysym.scancode] = 1;
                key_latch[event.key.keysym.scancode] = 1;
                mw_input_set_keyboard(event.key.keysym.scancode, 1, 1);
                if (event.key.repeat) break;
                if (event.key.keysym.scancode == SDL_SCANCODE_1 ||
                    event.key.keysym.scancode == SDL_SCANCODE_2 ||
                    event.key.keysym.scancode == SDL_SCANCODE_3) {
                    if (machine.workram[0x2000] > 0)
                        game_started = 1;
                }
                if (event.key.keysym.scancode == SDL_SCANCODE_F1) {
                    settings_open = !settings_open;
                    if (!settings_open) settings_dirty = 1;
                } else if (event.key.keysym.scancode == SDL_SCANCODE_ESCAPE) {
                    if (settings_open) {
                        settings_open = 0;
                        settings_dirty = 1;
                    } else {
                        running = 0;
                    }
                } else if (event.key.keysym.scancode == SDL_SCANCODE_P) {
                    paused = !paused;
                } else if (event.key.keysym.scancode == SDL_SCANCODE_F12) {
                    int em = game_started ? g_settings.display_mode : MW_DISPLAY_ORIGINAL;
                    int sw = mw_display_crop_width(em);
                    int sx = mw_display_crop_x(em, machine.view_w);
                    uint32_t *frame = s18_render_frame(&machine);
                    s18_save_bmp_rect("moonwalker.bmp", frame, machine.view_w,
                                      sx, 0, sw, S18_SCREEN_H);
                } else if (event.key.keysym.scancode == SDL_SCANCODE_F5) {
                    mw_m68k_reset(&machine);
                    /* A reboot clears the cheats: they are session-only, and
                     * the panel says so.  Toggles start off again after F5. */
                    memset(&g_cheats, 0, sizeof(g_cheats));
                    paused = 0;
                } else if (event.key.keysym.scancode == SDL_SCANCODE_F11) {
                    g_settings.fullscreen = g_settings.fullscreen ? 0 : 1;
                    apply_fullscreen(window, g_settings.fullscreen);
                    applied_fullscreen = g_settings.fullscreen;
                    settings_dirty = 1;
                }
                break;
            case SDL_KEYUP:
                key_state[event.key.keysym.scancode] = 0;
                mw_input_set_keyboard(event.key.keysym.scancode, 0, 0);
                break;
            default:
                break;
            }
        }

        /* Refresh the merged keyboard + pad state once per frame, even while
         * paused or in the panel, so the one-frame tap latch drains and no
         * press leaks into the first frame after resuming. */
        mw_input_poll(&g_input);

        if (!paused && !settings_open) {
            update_inputs(&machine);
            s18_run_frame(&machine);
            /* Freeze order: after the frame, so the game reads the frozen
             * values on its next frame.  Cheats are session-only; everything
             * stays zero unless a toggle was flipped in the CHEATS panel. */
            cheats_apply(&machine, &g_cheats);
        }
        memset(key_latch, 0, sizeof(key_latch));
        memset(key_latch, 0, sizeof(key_latch));

        int effective_display = game_started ? g_settings.display_mode : MW_DISPLAY_ORIGINAL;

        SDL_Rect rect = game_rect(window,
                                   mw_display_crop_width(effective_display),
                                   S18_SCREEN_H, g_settings.integer_scale);
        {
            uint32_t *frame = s18_render_frame(&machine);
            /* Shader presets run in source space, so every emulated line
             * gets exactly one scanline regardless of the window size. */
            apply_shader(frame, machine.view_w, S18_SCREEN_H,
                         g_settings.shader_preset, g_settings.scanline_intensity);
            SDL_Rect upd = { 0, 0, machine.view_w, S18_SCREEN_H };
            SDL_UpdateTexture(texture, &upd, frame, machine.view_w * (int)sizeof(uint32_t));
        }

        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        SDL_Rect src_rect = {
            mw_display_crop_x(effective_display, machine.view_w),
            0,
            mw_display_crop_width(effective_display),
            S18_SCREEN_H
        };
        SDL_RenderCopy(renderer, texture, &src_rect, &rect);

        mw_ui_begin(ui);
        if (g_settings.show_fps)
            mw_ui_draw_fps(ui, (float)fps);
        int was_open = settings_open;
        if (settings_open) {
            int reset_request = 0, quit_request = 0, changed = 0;
            if (mw_ui_tab(ui) == MW_UI_TAB_CONTROLLERS)
                mw_ui_draw_controllers(ui, &g_settings, &settings_open, &changed);
            else if (mw_ui_tab(ui) == MW_UI_TAB_CHEATS)
                mw_ui_draw_cheats(ui, &g_cheats, &settings_open);
            else
                mw_ui_draw_settings(ui, &g_settings, &settings_open, 1,
                                    &reset_request, &quit_request, &changed);
            if (changed) settings_dirty = 1;
            if (reset_request) {
                mw_m68k_reset(&machine);
                paused = 0;
            }
            if (quit_request) running = 0;
        }
        /* Ruling: no capture outlives the panel.  This single cover handles
         * F1/ESC/X, the CLOSE button, a tab switch and the launcher handing
         * the window to mwalk.exe. */
        if (!(settings_open && mw_ui_tab(ui) == MW_UI_TAB_CONTROLLERS))
            mw_input_cancel_capture();
        if (was_open && !settings_open)
            settings_dirty = 1;
        mw_ui_end(ui);
        SDL_RenderPresent(renderer);

        if (settings_dirty && SDL_GetTicks() - last_save > 250) {
            mw_settings_save(&g_settings, settings_path);
            settings_dirty = 0;
            last_save = SDL_GetTicks();
        }
        if (g_settings.fullscreen != applied_fullscreen) {
            apply_fullscreen(window, g_settings.fullscreen);
            applied_fullscreen = g_settings.fullscreen;
        }
        /* RESOLUTION presets apply immediately, like fullscreen above.
         * While fullscreen the desktop size rules, so the window size is
         * only pushed once the user is back in a window; the pending value
         * is picked up on the next frame after leaving fullscreen. */
        clamp_window_size(&g_settings.window_w, &g_settings.window_h);
        if (!g_settings.fullscreen &&
            (g_settings.window_w != applied_window_w || g_settings.window_h != applied_window_h)) {
            SDL_SetWindowSize(window, g_settings.window_w, g_settings.window_h);
            applied_window_w = g_settings.window_w;
            applied_window_h = g_settings.window_h;
        }
        /* The F1 panel's SOUND toggle applies live, like fullscreen above.
         * A forced-on --audio / MW_AUDIO keeps the device open. */
        int want_audio = audio_forced || g_settings.sound_on;
        if (want_audio != applied_audio) {
            audio_apply(want_audio);
            applied_audio = want_audio;
        }

        uint64_t now = SDL_GetPerformanceCounter();
        double elapsed = (double)(now - last) / (double)SDL_GetPerformanceFrequency();
        double fdt = (double)(now - fps_last) / (double)SDL_GetPerformanceFrequency();
        if (fdt > 0.00001) {
            double inst = 1.0 / fdt;
            fps = fps * 0.9 + inst * 0.1;
        }
        fps_last = now;
        if (elapsed < frame_time) {
            double wait = frame_time - elapsed;
            if (wait > 0.002)
                SDL_Delay((Uint32)((wait - 0.001) * 1000.0));
            while ((double)(SDL_GetPerformanceCounter() - last) / (double)SDL_GetPerformanceFrequency() < frame_time) {
                /* spin the last millisecond for stable pacing */
            }
        }
        last = SDL_GetPerformanceCounter();
    }

    if (settings_dirty)
        mw_settings_save(&g_settings, settings_path);
    if (g_audio_dev) {
        SDL_PauseAudioDevice(g_audio_dev, 1);
        SDL_CloseAudioDevice(g_audio_dev);
        g_audio_dev = 0;
    }
    s18_audio_destroy(audio);
    s18_mcu_destroy(mcu);
    SDL_DestroyTexture(texture);
    mw_ui_destroy(ui);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    mw_input_shutdown();
    SDL_Quit();
    if (nvram_on)
        s18_save_nvram(&machine, nvram);
    s18_video_shutdown();
    s18_shutdown(&machine);
    return 0;
}

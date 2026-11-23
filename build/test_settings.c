/*
 * Throwaway harness for the extended mw_settings schema (Task 1).
 * Not part of CMake; build it by hand from the repo root:
 *
 *   gcc -I src -I third_party/sdl2_dl/SDL2-2.30.11/x86_64-w64-mingw32/include/SDL2 \
 *       -o build/test_settings.exe build/test_settings.c src/settings.c \
 *       -L third_party/sdl2_dl/SDL2-2.30.11/x86_64-w64-mingw32/lib -lSDL2
 *
 * Exits 0 and prints "ALL SETTINGS TESTS PASSED" when every assertion holds.
 */
#include "settings.h"

#define SDL_MAIN_HANDLED
#include <SDL.h>

#include <stdio.h>
#include <string.h>

static int g_failures;

static void check_int(const char *what, int got, int want)
{
    if (got != want) {
        printf("[FAIL] %s: got %d want %d\n", what, got, want);
        g_failures++;
    } else {
        printf("[ ok ] %s = %d\n", what, got);
    }
}

static void check_str(const char *what, const char *got, const char *want)
{
    if (strcmp(got, want) != 0) {
        printf("[FAIL] %s: got \"%s\" want \"%s\"\n", what, got, want);
        g_failures++;
    } else {
        printf("[ ok ] %s = \"%s\"\n", what, got);
    }
}

static void check_key_map(const char *what, const mw_settings *s, int p,
                          const int want[7])
{
    int a;
    for (a = 0; a < 7; a++) {
        char label[96];
        snprintf(label, sizeof(label), "%s key_map[%d][%d]", what, p, a);
        check_int(label, s->key_map[p][a], want[a]);
    }
}

static void check_pad_map(const char *what, const mw_settings *s, int p,
                          const int want[7])
{
    int a;
    for (a = 0; a < 7; a++) {
        char label[96];
        snprintf(label, sizeof(label), "%s pad_map[%d][%d]", what, p, a);
        check_int(label, s->pad_map[p][a], want[a]);
    }
}

static int write_file(const char *path, const char *text)
{
    FILE *f = fopen(path, "w");
    if (!f) {
        printf("[FAIL] cannot write %s\n", path);
        g_failures++;
        return 0;
    }
    fputs(text, f);
    fclose(f);
    return 1;
}

int main(void)
{
    const char *rt_path = "build/test_settings_rt.ini";
    const char *old_path = "build/test_settings_old.ini";
    const char *clamp_path = "build/test_settings_clamp.ini";

    /* ---- (a) documented defaults ------------------------------------- */
    {
        mw_settings s;
        int i;

        memset(&s, 0xAB, sizeof(s));
        mw_settings_defaults(&s);

        printf("-- (a) defaults --\n");
        check_int("volume", s.volume, 80);
        check_int("fullscreen", s.fullscreen, 0);
        check_int("scanlines", s.scanlines, 0);
        check_int("integer_scale", s.integer_scale, 1);
        check_int("show_fps", s.show_fps, 0);
        check_int("display_mode", s.display_mode, MW_DISPLAY_16_9);

        check_int("sound_on", s.sound_on, 1);
        check_int("music_on", s.music_on, 1);
        check_int("shader_preset", s.shader_preset, 1);
        check_int("scanline_intensity", s.scanline_intensity, 50);
        check_int("window_w", s.window_w, 1280);
        check_int("window_h", s.window_h, 720);

        {
            const int p1[7] = { SDL_SCANCODE_UP, SDL_SCANCODE_DOWN, SDL_SCANCODE_LEFT,
                                SDL_SCANCODE_RIGHT, SDL_SCANCODE_Z, SDL_SCANCODE_X,
                                SDL_SCANCODE_C };
            const int p2[7] = { SDL_SCANCODE_W, SDL_SCANCODE_S, SDL_SCANCODE_A,
                                SDL_SCANCODE_D, SDL_SCANCODE_J, SDL_SCANCODE_K,
                                SDL_SCANCODE_L };
            const int p3[7] = { SDL_SCANCODE_KP_8, SDL_SCANCODE_KP_2, SDL_SCANCODE_KP_4,
                                SDL_SCANCODE_KP_6, SDL_SCANCODE_KP_1, SDL_SCANCODE_KP_2,
                                SDL_SCANCODE_KP_3 };
            const int pad[7] = { SDL_CONTROLLER_BUTTON_DPAD_UP,
                                 SDL_CONTROLLER_BUTTON_DPAD_DOWN,
                                 SDL_CONTROLLER_BUTTON_DPAD_LEFT,
                                 SDL_CONTROLLER_BUTTON_DPAD_RIGHT,
                                 SDL_CONTROLLER_BUTTON_A, SDL_CONTROLLER_BUTTON_B,
                                 SDL_CONTROLLER_BUTTON_X };

            check_key_map("default", &s, 0, p1);
            check_key_map("default", &s, 1, p2);
            check_key_map("default", &s, 2, p3);
            check_pad_map("default", &s, 0, pad);
            check_pad_map("default", &s, 1, pad);
            check_pad_map("default", &s, 2, pad);
        }

        for (i = 0; i < 3; i++) {
            char label[64];
            snprintf(label, sizeof(label), "default pad_guid[%d]", i);
            check_str(label, s.pad_guid[i], "");
        }
    }

    /* ---- (b) save -> load round trip --------------------------------- */
    {
        static const int kp1[7] = { SDL_SCANCODE_Q, SDL_SCANCODE_E, SDL_SCANCODE_R,
                                    SDL_SCANCODE_T, SDL_SCANCODE_Y, SDL_SCANCODE_U,
                                    SDL_SCANCODE_I };
        static const int kp2[7] = { SDL_SCANCODE_F, SDL_SCANCODE_G, SDL_SCANCODE_H,
                                    SDL_SCANCODE_V, SDL_SCANCODE_B, SDL_SCANCODE_N,
                                    SDL_SCANCODE_M };
        static const int kp3[7] = { SDL_SCANCODE_1, SDL_SCANCODE_2, SDL_SCANCODE_3,
                                    SDL_SCANCODE_4, SDL_SCANCODE_5, SDL_SCANCODE_6,
                                    SDL_SCANCODE_7 };
        static const int pp1[7] = { 0, 1, 2, 3, 0, 1, 2 };
        static const int pp2[7] = { 4, 5, 6, 7, -1, 9, 10 };
        static const int pp3[7] = { 11, 12, 13, -1, 14, -1, 15 };
        const char *guid_p2 = "030000005e0400008e02000014010000";
        mw_settings a, b;
        int p;

        printf("-- (b) round trip --\n");
        mw_settings_defaults(&a);
        a.volume = 33;
        a.fullscreen = 1;
        a.scanlines = 1;
        a.integer_scale = 0;
        a.show_fps = 1;
        a.display_mode = MW_DISPLAY_21_9;
        a.sound_on = 0;
        a.music_on = 0;
        a.shader_preset = 2;
        a.scanline_intensity = 77;
        a.window_w = 1920;
        a.window_h = 1080;
        for (p = 0; p < 7; p++) {
            a.key_map[0][p] = kp1[p];
            a.key_map[1][p] = kp2[p];
            a.key_map[2][p] = kp3[p];
            a.pad_map[0][p] = pp1[p];
            a.pad_map[1][p] = pp2[p];
            a.pad_map[2][p] = pp3[p];
        }
        snprintf(a.pad_guid[0], sizeof(a.pad_guid[0]), "%s", "");
        snprintf(a.pad_guid[1], sizeof(a.pad_guid[1]), "%s", "aa000000bb000000cc000000dd000000");
        snprintf(a.pad_guid[2], sizeof(a.pad_guid[2]), "%s", guid_p2);

        if (mw_settings_save(&a, rt_path)) {
            memset(&b, 0xCD, sizeof(b));
            if (mw_settings_load(&b, rt_path)) {
                check_int("rt volume", b.volume, 33);
                check_int("rt fullscreen", b.fullscreen, 1);
                check_int("rt scanlines", b.scanlines, 1);
                check_int("rt integer_scale", b.integer_scale, 0);
                check_int("rt show_fps", b.show_fps, 1);
                check_int("rt display_mode", b.display_mode, MW_DISPLAY_21_9);
                check_int("rt sound_on", b.sound_on, 0);
                check_int("rt music_on", b.music_on, 0);
                check_int("rt shader_preset", b.shader_preset, 2);
                check_int("rt scanline_intensity", b.scanline_intensity, 77);
                check_int("rt window_w", b.window_w, 1920);
                check_int("rt window_h", b.window_h, 1080);
                check_key_map("rt", &b, 0, kp1);
                check_key_map("rt", &b, 1, kp2);
                check_key_map("rt", &b, 2, kp3);
                check_pad_map("rt", &b, 0, pp1);
                check_pad_map("rt", &b, 1, pp2);
                check_pad_map("rt", &b, 2, pp3);
                check_str("rt pad_guid[0]", b.pad_guid[0], "");
                check_str("rt pad_guid[1]", b.pad_guid[1], "aa000000bb000000cc000000dd000000");
                check_str("rt pad_guid[2]", b.pad_guid[2], guid_p2);
            } else {
                printf("[FAIL] load of round-trip file %s failed\n", rt_path);
                g_failures++;
            }
        } else {
            printf("[FAIL] save to %s failed\n", rt_path);
            g_failures++;
        }
        remove(rt_path);
    }

    /* ---- (c) old settings.ini keeps old values, new fields default --- */
    {
        mw_settings s;
        const int pad[7] = { SDL_CONTROLLER_BUTTON_DPAD_UP,
                             SDL_CONTROLLER_BUTTON_DPAD_DOWN,
                             SDL_CONTROLLER_BUTTON_DPAD_LEFT,
                             SDL_CONTROLLER_BUTTON_DPAD_RIGHT,
                             SDL_CONTROLLER_BUTTON_A, SDL_CONTROLLER_BUTTON_B,
                             SDL_CONTROLLER_BUTTON_X };
        const int p1[7] = { SDL_SCANCODE_UP, SDL_SCANCODE_DOWN, SDL_SCANCODE_LEFT,
                            SDL_SCANCODE_RIGHT, SDL_SCANCODE_Z, SDL_SCANCODE_X,
                            SDL_SCANCODE_C };
        const int p2[7] = { SDL_SCANCODE_W, SDL_SCANCODE_S, SDL_SCANCODE_A,
                            SDL_SCANCODE_D, SDL_SCANCODE_J, SDL_SCANCODE_K,
                            SDL_SCANCODE_L };
        const int p3[7] = { SDL_SCANCODE_KP_8, SDL_SCANCODE_KP_2, SDL_SCANCODE_KP_4,
                            SDL_SCANCODE_KP_6, SDL_SCANCODE_KP_1, SDL_SCANCODE_KP_2,
                            SDL_SCANCODE_KP_3 };

        printf("-- (c) legacy file --\n");
        if (write_file(old_path,
                       "volume=42\n"
                       "fullscreen=1\n"
                       "scanlines=1\n"
                       "integer_scale=0\n"
                       "show_fps=1\n"
                       "display_mode=3\n")) {
            memset(&s, 0xEF, sizeof(s));
            if (mw_settings_load(&s, old_path)) {
                check_int("legacy volume", s.volume, 42);
                check_int("legacy fullscreen", s.fullscreen, 1);
                check_int("legacy scanlines", s.scanlines, 1);
                check_int("legacy integer_scale", s.integer_scale, 0);
                check_int("legacy show_fps", s.show_fps, 1);
                check_int("legacy display_mode", s.display_mode, MW_DISPLAY_21_9);

                check_int("legacy sound_on", s.sound_on, 1);
                check_int("legacy music_on", s.music_on, 1);
                check_int("legacy shader_preset", s.shader_preset, 1);
                check_int("legacy scanline_intensity", s.scanline_intensity, 50);
                check_int("legacy window_w", s.window_w, 1280);
                check_int("legacy window_h", s.window_h, 720);
                check_key_map("legacy", &s, 0, p1);
                check_key_map("legacy", &s, 1, p2);
                check_key_map("legacy", &s, 2, p3);
                check_pad_map("legacy", &s, 0, pad);
                check_pad_map("legacy", &s, 1, pad);
                check_pad_map("legacy", &s, 2, pad);
                check_str("legacy pad_guid[0]", s.pad_guid[0], "");
                check_str("legacy pad_guid[1]", s.pad_guid[1], "");
                check_str("legacy pad_guid[2]", s.pad_guid[2], "");
            } else {
                printf("[FAIL] load of legacy file %s failed\n", old_path);
                g_failures++;
            }
        }
        remove(old_path);
    }

    /* ---- (d) out-of-range values clamp on load ----------------------- */
    {
        mw_settings s;

        printf("-- (d) clamping --\n");
        if (write_file(clamp_path,
                       "shader_preset=9\n"
                       "scanline_intensity=500\n")) {
            mw_settings_defaults(&s);
            if (mw_settings_load(&s, clamp_path)) {
                check_int("clamp shader_preset", s.shader_preset, 2);
                check_int("clamp scanline_intensity", s.scanline_intensity, 100);
            } else {
                printf("[FAIL] load of clamp file %s failed\n", clamp_path);
                g_failures++;
            }
        }
        if (write_file(clamp_path,
                       "shader_preset=-4\n"
                       "scanline_intensity=-9\n")) {
            mw_settings_defaults(&s);
            if (mw_settings_load(&s, clamp_path)) {
                check_int("clamp low shader_preset", s.shader_preset, 0);
                check_int("clamp low scanline_intensity", s.scanline_intensity, 0);
            } else {
                printf("[FAIL] load of low-clamp file %s failed\n", clamp_path);
                g_failures++;
            }
        }
        remove(clamp_path);
    }

    if (g_failures) {
        printf("\nSETTINGS TESTS FAILED (%d assertion%s)\n", g_failures,
               g_failures == 1 ? "" : "s");
        return 1;
    }
    printf("\nALL SETTINGS TESTS PASSED\n");
    return 0;
}

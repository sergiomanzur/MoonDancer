#include "settings.h"

#include <stdio.h>
#include <string.h>
#include <SDL.h>

#define MW_PLAYERS 3
#define MW_ACTIONS 7

/* Per-player keyboard defaults: UP, DOWN, LEFT, RIGHT, B1, B2, B3. */
static const int k_default_key_map[MW_PLAYERS][MW_ACTIONS] = {
    { SDL_SCANCODE_UP, SDL_SCANCODE_DOWN, SDL_SCANCODE_LEFT, SDL_SCANCODE_RIGHT,
      SDL_SCANCODE_Z, SDL_SCANCODE_X, SDL_SCANCODE_C },
    { SDL_SCANCODE_W, SDL_SCANCODE_S, SDL_SCANCODE_A, SDL_SCANCODE_D,
      SDL_SCANCODE_J, SDL_SCANCODE_K, SDL_SCANCODE_L },
    { SDL_SCANCODE_KP_8, SDL_SCANCODE_KP_2, SDL_SCANCODE_KP_4, SDL_SCANCODE_KP_6,
      SDL_SCANCODE_KP_1, SDL_SCANCODE_KP_2, SDL_SCANCODE_KP_3 }
};

/* Gamepad defaults are identical for all three players. */
static const int k_default_pad_map[MW_ACTIONS] = {
    SDL_CONTROLLER_BUTTON_DPAD_UP, SDL_CONTROLLER_BUTTON_DPAD_DOWN,
    SDL_CONTROLLER_BUTTON_DPAD_LEFT, SDL_CONTROLLER_BUTTON_DPAD_RIGHT,
    SDL_CONTROLLER_BUTTON_A, SDL_CONTROLLER_BUTTON_B, SDL_CONTROLLER_BUTTON_X
};

void mw_settings_defaults(mw_settings *s)
{
    int p, a;
    if (!s) return;
    s->volume = 80;
#ifdef __ANDROID__
    /* A handheld game app that launches with the status/navigation bars on
     * screen looks broken.  SDL maps this flag to the activity's sticky
     * immersive mode, and the FULLSCREEN toggle still turns it off for a
     * session and persists the choice.  Desktop keeps its windowed default. */
    s->fullscreen = 1;
#else
    s->fullscreen = 0;
#endif
    s->scanlines = 0;
    s->integer_scale = 1;
    s->show_fps = 0;
    s->display_mode = MW_DISPLAY_16_9;
    s->sound_on = 1;
    s->music_on = 1;
    s->shader_preset = 1;
    s->scanline_intensity = 50;
    s->window_w = 1280;
    s->window_h = 720;
    s->touch_on = 1;
    s->touch_mode = 1; /* MW_TOUCH_STICK: the native-Android convention */
    for (p = 0; p < MW_PLAYERS; p++) {
        for (a = 0; a < MW_ACTIONS; a++) {
            s->key_map[p][a] = k_default_key_map[p][a];
            s->pad_map[p][a] = k_default_pad_map[a];
        }
        s->pad_guid[p][0] = '\0';
    }
}

static int clampi(int v, int lo, int hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

int mw_settings_path(char *buf, int cap)
{
    if (!buf || cap <= 0) return 0;
    char *pref = SDL_GetPrefPath("MoonwalkerRecomp", "Moonwalker");
    if (!pref) {
        snprintf(buf, (size_t)cap, "settings.ini");
        return 1;
    }
    snprintf(buf, (size_t)cap, "%ssettings.ini", pref);
    SDL_free(pref);
    return 1;
}

/* Parses count comma-separated integers from src. Returns 1 only when the
   whole list parses; out is left untouched otherwise. */
static int parse_int_list(const char *src, int *out, int count)
{
    const char *p = src;
    int tmp[MW_ACTIONS];
    int i;
    if (count <= 0 || count > MW_ACTIONS) return 0;
    for (i = 0; i < count; i++) {
        int v = 0;
        int used = 0;
        while (*p == ' ' || *p == '\t') p++;
        if (sscanf(p, "%d%n", &v, &used) != 1) return 0;
        p += used;
        tmp[i] = v;
        if (i + 1 < count) {
            while (*p == ' ' || *p == '\t') p++;
            if (*p != ',') return 0;
            p++;
        }
    }
    memcpy(out, tmp, (size_t)count * sizeof(out[0]));
    return 1;
}

/* A remap line is adopted only when every entry is a usable index, so a
   corrupt file cannot push an out-of-range value into the input layer. */
static int valid_key_map(const int *map)
{
    int a;
    for (a = 0; a < MW_ACTIONS; a++) {
        if (map[a] < 0 || map[a] >= SDL_NUM_SCANCODES) return 0;
    }
    return 1;
}

static int valid_pad_map(const int *map)
{
    int a;
    for (a = 0; a < MW_ACTIONS; a++) {
        if (map[a] < -1 || map[a] >= SDL_CONTROLLER_BUTTON_MAX) return 0;
    }
    return 1;
}

int mw_settings_load(mw_settings *s, const char *path)
{
    if (!s) return 0;
    mw_settings_defaults(s);
    if (!path) return 0;
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        char key[64];
        char text[64];
        const char *raw;
        int value;
        if (sscanf(line, " %63[^=]", key) != 1) continue;
        raw = strchr(line, '=');
        if (!raw) continue;
        raw++;

        /* GUIDs are the only string values, and a GUID that starts with a
           digit would otherwise look numeric, so handle them first. */
        if (!strncmp(key, "pad_guid_p", 10)) {
            int player = key[10] - '0';
            if (key[11] != '\0' || player < 0 || player >= MW_PLAYERS) continue;
            if (sscanf(raw, " %32s", text) == 1) {
                snprintf(s->pad_guid[player], sizeof(s->pad_guid[player]), "%s", text);
            } else {
                s->pad_guid[player][0] = '\0';
            }
            continue;
        }

        if (sscanf(raw, " %d", &value) != 1) continue;
        if (!strcmp(key, "volume")) s->volume = clampi(value, 0, 100);
        else if (!strcmp(key, "fullscreen")) s->fullscreen = value ? 1 : 0;
        else if (!strcmp(key, "scanlines")) s->scanlines = value ? 1 : 0;
        else if (!strcmp(key, "integer_scale")) s->integer_scale = value ? 1 : 0;
        else if (!strcmp(key, "show_fps")) s->show_fps = value ? 1 : 0;
        else if (!strcmp(key, "display_mode")) {
            if (value < 0 || value >= MW_DISPLAY_COUNT) value = MW_DISPLAY_16_9;
            s->display_mode = value;
        }
        else if (!strcmp(key, "sound_on")) s->sound_on = value ? 1 : 0;
        else if (!strcmp(key, "music_on")) s->music_on = value ? 1 : 0;
        else if (!strcmp(key, "shader_preset")) s->shader_preset = clampi(value, 0, 2);
        else if (!strcmp(key, "scanline_intensity")) s->scanline_intensity = clampi(value, 0, 100);
        else if (!strcmp(key, "window_w")) s->window_w = value;
        else if (!strcmp(key, "window_h")) s->window_h = value;
        else if (!strcmp(key, "touch_on")) s->touch_on = value ? 1 : 0;
        else if (!strcmp(key, "touch_mode")) s->touch_mode = clampi(value, 0, 1);
        else if (!strncmp(key, "key_map_p", 9)) {
            int player = key[9] - '0';
            int map[MW_ACTIONS];
            if (key[10] != '\0' || player < 0 || player >= MW_PLAYERS) continue;
            if (parse_int_list(raw, map, MW_ACTIONS) && valid_key_map(map)) {
                memcpy(s->key_map[player], map, sizeof(map));
            }
        }
        else if (!strncmp(key, "pad_map_p", 9)) {
            int player = key[9] - '0';
            int map[MW_ACTIONS];
            if (key[10] != '\0' || player < 0 || player >= MW_PLAYERS) continue;
            if (parse_int_list(raw, map, MW_ACTIONS) && valid_pad_map(map)) {
                memcpy(s->pad_map[player], map, sizeof(map));
            }
        }
    }
    fclose(f);
    return 1;
}

int mw_settings_save(const mw_settings *s, const char *path)
{
    int p, a;
    if (!s || !path) return 0;
    FILE *f = fopen(path, "w");
    if (!f) return 0;
    fprintf(f, "volume=%d\nfullscreen=%d\nscanlines=%d\ninteger_scale=%d\nshow_fps=%d\ndisplay_mode=%d\n",
            clampi(s->volume, 0, 100), s->fullscreen ? 1 : 0, s->scanlines ? 1 : 0,
            s->integer_scale ? 1 : 0, s->show_fps ? 1 : 0,
            (s->display_mode >= 0 && s->display_mode < MW_DISPLAY_COUNT) ? s->display_mode : MW_DISPLAY_16_9);
    fprintf(f, "sound_on=%d\nmusic_on=%d\nshader_preset=%d\nscanline_intensity=%d\nwindow_w=%d\nwindow_h=%d\n",
            s->sound_on ? 1 : 0, s->music_on ? 1 : 0, clampi(s->shader_preset, 0, 2),
            clampi(s->scanline_intensity, 0, 100), s->window_w, s->window_h);
    fprintf(f, "touch_on=%d\ntouch_mode=%d\n",
            s->touch_on ? 1 : 0, clampi(s->touch_mode, 0, 1));
    for (p = 0; p < MW_PLAYERS; p++) {
        fprintf(f, "key_map_p%d=", p);
        for (a = 0; a < MW_ACTIONS; a++) fprintf(f, "%s%d", a ? "," : "", s->key_map[p][a]);
        fputc('\n', f);
    }
    for (p = 0; p < MW_PLAYERS; p++) {
        fprintf(f, "pad_map_p%d=", p);
        for (a = 0; a < MW_ACTIONS; a++) fprintf(f, "%s%d", a ? "," : "", s->pad_map[p][a]);
        fputc('\n', f);
    }
    for (p = 0; p < MW_PLAYERS; p++) {
        fprintf(f, "pad_guid_p%d=%.32s\n", p, s->pad_guid[p]);
    }
    fclose(f);
    return 1;
}

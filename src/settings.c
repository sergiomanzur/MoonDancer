#include "settings.h"

#include <stdio.h>
#include <string.h>
#include <SDL.h>

void mw_settings_defaults(mw_settings *s)
{
    if (!s) return;
    s->volume = 80;
    s->fullscreen = 0;
    s->scanlines = 0;
    s->integer_scale = 1;
    s->show_fps = 0;
    s->display_mode = MW_DISPLAY_16_9;
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
        int value;
        if (sscanf(line, " %63[^=]=%d", key, &value) == 2) {
            if (!strcmp(key, "volume")) s->volume = clampi(value, 0, 100);
            else if (!strcmp(key, "fullscreen")) s->fullscreen = value ? 1 : 0;
            else if (!strcmp(key, "scanlines")) s->scanlines = value ? 1 : 0;
            else if (!strcmp(key, "integer_scale")) s->integer_scale = value ? 1 : 0;
            else if (!strcmp(key, "show_fps")) s->show_fps = value ? 1 : 0;
            else if (!strcmp(key, "display_mode")) {
                if (value < 0 || value >= MW_DISPLAY_COUNT) value = MW_DISPLAY_16_9;
                s->display_mode = value;
            }
        }
    }
    fclose(f);
    return 1;
}

int mw_settings_save(const mw_settings *s, const char *path)
{
    if (!s || !path) return 0;
    FILE *f = fopen(path, "w");
    if (!f) return 0;
    fprintf(f, "volume=%d\nfullscreen=%d\nscanlines=%d\ninteger_scale=%d\nshow_fps=%d\ndisplay_mode=%d\n",
            clampi(s->volume, 0, 100), s->fullscreen ? 1 : 0, s->scanlines ? 1 : 0,
            s->integer_scale ? 1 : 0, s->show_fps ? 1 : 0,
            (s->display_mode >= 0 && s->display_mode < MW_DISPLAY_COUNT) ? s->display_mode : MW_DISPLAY_16_9);
    fclose(f);
    return 1;
}

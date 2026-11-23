/*
 * Moonwalker native port - host portability layer.
 *
 * Every path the game writes or reads from the install tree goes through
 * these functions so the Android and SteamOS ports can swap the policy in one
 * place.  Both entry points are written as a single per-platform branch;
 * Windows and Linux share the portable-folder layout for now.
 */
#include "host_port.h"

#include <stdio.h>

#include <SDL.h>

/* Read-only asset dir.  Packaged builds keep fonts/ and the launcher
 * background beside the executable, which is what SDL_GetBasePath reports.
 * Returns the path with SDL's trailing separator. */
int mw_host_base_dir(char *buf, int cap)
{
    char *base;
    int n;

    if (!buf || cap <= 0) return 0;
    buf[0] = '\0';

    base = SDL_GetBasePath();
    if (!base) return 0;
    n = snprintf(buf, (size_t)cap, "%s", base);
    SDL_free(base);
    if (n < 0 || n >= cap) {
        buf[0] = '\0';
        return 0;
    }
    return 1;
}

/* Writable state (NVRAM, screenshots).  Same folder as the assets today, so a
 * packaged install behaves exactly as it did when these paths were CWD
 * relative and the launcher started the game in the exe directory.  The Linux
 * `portable` marker check and the Android preference directory slot in here. */
int mw_host_data_dir(char *buf, int cap)
{
    return mw_host_base_dir(buf, cap);
}

/* No APK asset bundle to unpack on desktop hosts: fonts and the background are
 * already files next to the executable. */
void mw_host_extract_assets(void)
{
}

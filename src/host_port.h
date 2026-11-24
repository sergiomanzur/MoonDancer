#ifndef MW_HOST_PORT_H
#define MW_HOST_PORT_H

#include <SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Writable dir for NVRAM + screenshots, trailing separator, 1 on success.
 * Windows/Linux: SDL_GetBasePath() - packaged behavior identical to today.
 * Linux override: a `portable` marker dir beside the binary switches back on.
 * Android: SDL_GetPrefPath("MoonwalkerRecomp","Moonwalker"), the app's files
 * directory (there is no writable install tree). */
int mw_host_data_dir(char *buf, int cap);
/* Read-only asset dir (fonts/, launcher background). Same platform map;
 * on Android it is the same files directory, populated by
 * mw_host_extract_assets(). */
int mw_host_base_dir(char *buf, int cap);
/* Android only: copy the APK assets (fonts, background, optional bundled
 * mwalk.zip) into the data dir. One-time: a version-stamped marker
 * (files/assets/.extracted) skips the copy once the payload is in place, and a
 * newer stamp re-extracts on an existing install. Desktop: no-op.
 * Returns 1 when the assets are in place, 0 when a copy failed (the launcher
 * surfaces that on its status line and the next launch retries). */
int mw_host_extract_assets(void);
/* Game front end, callable as a function so a host process can run it
 * in-process. The desktop exe reaches it through a thin main(); on Android
 * the launcher calls it directly and owns SDL_Init/SDL_Quit. Returns 0 on a
 * clean exit, non-zero on a startup failure. */
int mw_game_main(int argc, char **argv);
/* 1 = a native picker handled the browse flow. Windows: GetOpenFileNameW was
 * shown and out[cap] is filled on success, left untouched on cancel. Android:
 * the activity's SAF picker was requested and out is untouched, because the
 * imported archive arrives later as an SDL_DROPFILE event carrying the copied
 * files/mwalk.zip path. 0 = host declined (Linux/SteamOS, or Android without a
 * reachable picker): caller opens the in-UI browser. */
int mw_host_browse_archive(SDL_Window *window, char *out, int cap);
/* Launch the game; blocks until it exits, returns its exit code.
 * Windows: current CreateProcessW + AllowSetForegroundWindow, hidden-after-
 * grant, verbatim. Linux: fork + execl of "mwalk" beside the launcher.
 * Android: in-process mw_game_main() on the activity's SDL thread; the caller
 * must release its own window first (SDL allows one window per activity). */
int mw_host_spawn_game(const char *base, const char *archive, SDL_Window *self);
/* Native dark title bar where the host supports one; no-op elsewhere. */
void mw_host_dark_titlebar(SDL_Window *window);

#ifdef __cplusplus
}
#endif

#endif

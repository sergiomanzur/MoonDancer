#ifndef MW_HOST_PORT_H
#define MW_HOST_PORT_H

#include <SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Writable dir for NVRAM + screenshots, trailing separator, 1 on success.
 * Windows/Linux: SDL_GetBasePath() - packaged behavior identical to today.
 * Linux override: a `portable` marker dir beside the binary switches back on.
 * Android: SDL_GetPrefPath("MoonwalkerRecomp","Moonwalker"). */
int mw_host_data_dir(char *buf, int cap);
/* Read-only asset dir (fonts/, launcher background). Same platform map. */
int mw_host_base_dir(char *buf, int cap);
/* Android only: one-time copy of APK assets (fonts, background, optional
 * bundled mwalk.zip) into the data dir. No-op elsewhere. */
void mw_host_extract_assets(void);
/* Game front end, callable as a function so a host process can run it
 * in-process. The desktop exe reaches it through a thin main(); on Android
 * the launcher calls it directly and owns SDL_Init/SDL_Quit. Returns 0 on a
 * clean exit, non-zero on a startup failure. */
int mw_game_main(int argc, char **argv);
/* 1 = a native picker handled the browse flow: Windows GetOpenFileNameW was
 * shown and out[cap] is filled on success, left untouched on cancel. 0 = host
 * declined: caller opens the in-UI browser.  Android's SAF picker (Task 8/9)
 * uses the same 1 = handled contract, with the chosen path delivered later as an
 * SDL_DROPFILE event. */
int mw_host_browse_archive(SDL_Window *window, char *out, int cap);
/* Launch the game; blocks until it exits, returns its exit code.
 * Windows: current CreateProcessW + AllowSetForegroundWindow, hidden-after-
 * grant, verbatim. Linux: fork + execl of "mwalk" beside the launcher.
 * Android: in-process mw_game_main (added in Task 8's build config). */
int mw_host_spawn_game(const char *base, const char *archive, SDL_Window *self);
/* Native dark title bar where the host supports one; no-op elsewhere. */
void mw_host_dark_titlebar(SDL_Window *window);

#ifdef __cplusplus
}
#endif

#endif

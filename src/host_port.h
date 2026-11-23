#ifndef MW_HOST_PORT_H
#define MW_HOST_PORT_H

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

#ifdef __cplusplus
}
#endif

#endif

#ifndef MW_LAUNCHER_MUSIC_H
#define MW_LAUNCHER_MUSIC_H

#ifdef __cplusplus
extern "C" {
#endif

/* Procedural synthwave loop for the launcher.  No audio files and no extra
 * dependencies: the samples are synthesised from the song position inside
 * the SDL audio callback.  mw_music_start() opens its own 44.1 kHz mono
 * S16 device and returns 1 on success; mw_music_stop() closes it and is
 * safe to call when nothing is playing.  Both are launcher-only and are
 * never referenced by the game or the headless targets. */
int mw_music_start(void);
void mw_music_stop(void);

#ifdef __cplusplus
}
#endif

#endif

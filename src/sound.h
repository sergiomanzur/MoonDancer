#ifndef MW_SOUND_H
#define MW_SOUND_H

#include <stdint.h>
#include "s18.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct s18_audio s18_audio_t;

s18_audio_t *s18_audio_create(s18_t *machine, const char *romdir);
void s18_audio_destroy(s18_audio_t *audio);
void s18_audio_run(s18_audio_t *audio, int cycles);
void s18_audio_nmi(s18_audio_t *audio);
/* 1 = run the Z80/MCU handshake but skip FM/PCM synthesis (muted). */
void s18_audio_set_fast(int on);
int s18_audio_read(s18_audio_t *audio, int16_t *out, int frames);
int s18_audio_sample_rate(void);

#ifdef __cplusplus
}
#endif

#endif

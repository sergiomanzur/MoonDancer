#ifndef MW_CPU68K_H
#define MW_CPU68K_H

#include "s18.h"

void mw_m68k_init(s18_t *m);
void mw_m68k_reset(s18_t *m);
void mw_m68k_run(int cycles, s18_t *m);
void mw_m68k_set_trace(int on, void *out);
void mw_m68k_set_record(int on);
void mw_m68k_dump_record(const char *path);
int mw_m68k_selftest(const uint8_t *rom, const uint8_t *key, int steps);

#endif

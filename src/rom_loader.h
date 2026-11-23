#ifndef MW_ROM_LOADER_H
#define MW_ROM_LOADER_H

#include <stddef.h>
#include <stdint.h>

/* Load the two encrypted 68000 ROMs and the FD1094 key from a directory
 * containing the extracted mwalk set. Returns 0 on success. */
int mw_load_roms(const char *directory, uint8_t rom[0x80000], uint8_t key[0x2000]);

#endif

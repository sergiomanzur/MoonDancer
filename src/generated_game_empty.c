#include "recomp.h"

/* Built when src/generated_game.c has not been produced from the user's
 * ROM yet.  The runtime sees no translated blocks and uses Musashi. */
const mw_block_t mw_blocks[] = { { 0u, 0, 0 } };
const unsigned mw_block_count = 0;

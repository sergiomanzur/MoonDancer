#ifndef MW_MCU_H
#define MW_MCU_H

#include <stdint.h>
#include "s18.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct s18_mcu s18_mcu_t;

s18_mcu_t *s18_mcu_create(s18_t *machine, const char *romdir);
void s18_mcu_destroy(s18_mcu_t *mcu);
void s18_mcu_reset(s18_mcu_t *mcu);
void s18_mcu_run(s18_mcu_t *mcu, int cycles);
void s18_mcu_int0(s18_mcu_t *mcu);
void s18_mcu_int1(s18_mcu_t *mcu);
void s18_mcu_dump_state(s18_mcu_t *mcu, uint8_t *ram128, uint8_t *sfr128);

#ifdef __cplusplus
}
#endif

#endif /* MW_MCU_H */

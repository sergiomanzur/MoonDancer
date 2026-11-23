#ifndef MW_FD1094_H
#define MW_FD1094_H

#include <stdint.h>

#define FD1094_ROM_SIZE 0x80000u
#define FD1094_KEY_SIZE 0x2000u

typedef struct fd1094 {
    uint8_t key[FD1094_KEY_SIZE];
    const uint8_t *encrypted_rom;
    uint8_t *roms[256];   /* decrypted instruction streams, one per key */
    uint8_t state;        /* selected key */
    uint8_t irqmode;      /* while servicing an interrupt, key = key[0] */
} fd1094_t;

void fd1094_init(fd1094_t *fd, const uint8_t *key, const uint8_t *encrypted_rom);
void fd1094_reset(fd1094_t *fd);
void fd1094_shutdown(fd1094_t *fd);
void fd1094_change_state(fd1094_t *fd, int newstate);
void fd1094_cmpi_callback(fd1094_t *fd, uint32_t value, int reg);
void fd1094_irq_ack(fd1094_t *fd);
void fd1094_rte(fd1094_t *fd);
uint8_t fd1094_state(const fd1094_t *fd);
const uint8_t *fd1094_decrypted_rom(fd1094_t *fd, uint8_t state);
uint16_t fd1094_fetch16(fd1094_t *fd, uint32_t address);

#endif

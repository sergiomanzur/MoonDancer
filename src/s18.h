#ifndef MW_S18_H
#define MW_S18_H

#include <stdint.h>

#include "fd1094.h"

#ifdef __cplusplus
extern "C" {
#endif

#define S18_SCREEN_W 320
#define S18_MAX_SCREEN_W 560
#define S18_SCREEN_H 224
#define S18_FPS 57.23
/* Extra world visible on each side in enhanced widescreen mode. */
#define S18_VIEW_MARGIN ((S18_MAX_SCREEN_W - S18_SCREEN_W) / 2)
/* Entities are kept alive a little beyond the visible edges so sprites
 * never pop in at the boundary of a widescreen crop. */
#define S18_CULL_MARGIN 160
#define S18_CPU_HZ 10000000
#define S18_CYCLES_PER_FRAME 174736  /* 10 MHz / 57.23 Hz */
/* 262 total scanlines, 224 visible: vblank starts at line 224. */
#define S18_VISIBLE_CYCLES ((S18_CYCLES_PER_FRAME * 224) / 262)
#define S18_VBLANK_CYCLES  (S18_CYCLES_PER_FRAME - S18_VISIBLE_CYCLES)

typedef struct s18_inputs {
    uint8_t p1;          /* active low: 1=B1 2=B2 4=B3 10=down 20=up 40=right 80=left */
    uint8_t p2;
    uint8_t p3;
    uint8_t service;     /* active low: 1=coin1 2=coin2 4=service 8=service1 10=start1 20=start2 40=service2 80=coin3 */
    uint8_t coinage;
    uint8_t dsw;
} s18_inputs_t;

typedef struct s18 s18_t;
typedef struct s18_audio s18_audio_t;
typedef struct s18_mcu s18_mcu_t;

struct s18 {
    /* ROM and decryption */
    uint8_t rom[0x80000];          /* original ROM, as the data bus sees it */
    uint8_t key[FD1094_KEY_SIZE];
    fd1094_t fd;

    /* board RAM */
    uint8_t workram[0x4000];
    uint8_t paletteram[0x1000];
    uint8_t tileram[0x10000];
    uint8_t textram[0x1000];
    uint8_t spriteram[0x800];

    uint32_t palette_rgb[4288];

    /* 315-5195 memory mapper */
    uint8_t mapper_regs[0x20];
    uint8_t mapper_to_sound;
    uint8_t mapper_from_sound;
    uint8_t mapper_open_bus;

    /* 315-5296 I/O */
    uint8_t io_output[8];
    uint8_t io_dir;
    uint8_t io_cnt;
    uint8_t tilebank[8];
    uint8_t display_enable;
    uint8_t vdp_enable;
    uint8_t vdp_mixing;
    uint8_t grayscale;

    /* VDP registers / status */
    uint8_t vdp_regs[0x20];
    uint16_t vdp_status;
    uint16_t vdp_control;
    uint16_t vdp_addr;
    uint8_t vdp_code;
    uint8_t vdp_command_pending;
    uint16_t vdp_command_part1;
    uint8_t vdp_vram_fill_pending;
    uint16_t vdp_vram_fill_length;
    uint8_t vdp_vram[0x10000];
    uint16_t vdp_cram[64];
    uint16_t vdp_vsram[64];
    uint8_t vdp_pending;
    uint16_t vdp_data_latch;
    uint32_t vdp_cycles;

    /* inputs / timing */
    s18_inputs_t inputs;
    s18_audio_t *audio;
    s18_mcu_t *mcu;
    int64_t cpu_cycles;
    int64_t frame_count;
    int irq4_pending;
    int cpu_reset_hold;    /* MCU holds the 68000 in reset via mapper reg 2 */
    int cpu_reset_pending; /* reset released; restart at the next CPU step */

    /* viewport: original 320-wide framing or the expanded 560-wide world */
    int view_w;
    int view_left;
    int cull_margin;

    /* debug */
    uint32_t trace_pc;
    unsigned long long executed;
    int log_mapper;
};

void s18_init(s18_t *m, const uint8_t *rom, const uint8_t *key);
void s18_shutdown(s18_t *m);
void s18_reset(s18_t *m);
void s18_run_frame(s18_t *m);
int s18_load_nvram(s18_t *m, const char *path);
int s18_save_nvram(const s18_t *m, const char *path);
void s18_set_inputs(s18_t *m, const s18_inputs_t *in);

/* Viewport: enhanced=0 keeps the original 320x224 framing and original
 * culling; enhanced=1 renders the 560x224 expanded world. */
void s18_set_view(s18_t *m, int enhanced);

/* Culling override used by both the translated and Musashi CPU paths.
 * Returns 1 when the branch at pc should be taken (object rejected),
 * 0 when it should fall through, or -1 when this is not a patched site
 * or original framing is active. */
int s18_wide_branch_override(const s18_t *m, uint32_t pc, int32_t d0, int32_t d1,
                             int32_t d2, int32_t d4);

/* bus, used by the Musashi glue and by the recompiled code */
uint8_t s18_read8(s18_t *m, uint32_t address);
uint16_t s18_read16(s18_t *m, uint32_t address);
uint32_t s18_read32(s18_t *m, uint32_t address);
void s18_write8(s18_t *m, uint32_t address, uint8_t data);
void s18_write16(s18_t *m, uint32_t address, uint16_t data);
void s18_write32(s18_t *m, uint32_t address, uint32_t data);

uint16_t s18_fetch16(s18_t *m, uint32_t address);
uint32_t s18_fetch32(s18_t *m, uint32_t address);

/* devices */
uint16_t s18_vdp_read(s18_t *m, uint32_t offset, uint16_t mask);
void s18_vdp_write(s18_t *m, uint32_t offset, uint16_t data, uint16_t mask);
uint8_t s18_io_read(s18_t *m, uint32_t offset);
void s18_io_write(s18_t *m, uint32_t offset, uint8_t data);
void s18_palette_write(s18_t *m, uint32_t offset, uint16_t data, uint16_t mask);
void s18_vdp_cram_write(s18_t *m, unsigned index, uint16_t data);
void s18_mapper_write(s18_t *m, uint32_t offset, uint8_t data);
uint8_t s18_mapper_read(s18_t *m, uint32_t offset);

#ifdef __cplusplus
}
#endif

#endif

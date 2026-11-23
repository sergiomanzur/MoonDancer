#ifndef MW_VIDEO_H
#define MW_VIDEO_H

#include <stdint.h>
#include "s18.h"

int s18_video_init(s18_t *m, const char *romdir);
void s18_video_shutdown(void);
uint32_t *s18_render_frame(s18_t *m);
uint8_t *s18_priority_bitmap(void);
int s18_save_bmp(const char *path, uint32_t *pixels);
int s18_save_bmp_rect(const char *path, uint32_t *pixels, int stride,
                      int src_x, int src_y, int w, int h);

#endif

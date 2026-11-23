#ifndef MW_SETTINGS_H
#define MW_SETTINGS_H

#ifdef __cplusplus
extern "C" {
#endif

#define MW_DISPLAY_ORIGINAL 0
#define MW_DISPLAY_16_9     1
#define MW_DISPLAY_16_10    2
#define MW_DISPLAY_21_9     3
#define MW_DISPLAY_COUNT    4

typedef struct mw_settings {
    int volume;         /* 0..100 */
    int fullscreen;     /* 0/1 */
    int scanlines;      /* 0/1 */
    int integer_scale;  /* 0/1 */
    int show_fps;       /* 0/1 */
    int display_mode;   /* MW_DISPLAY_* */
} mw_settings;

void mw_settings_defaults(mw_settings *s);
int mw_settings_load(mw_settings *s, const char *path);
int mw_settings_save(const mw_settings *s, const char *path);

/* Fills buf with the per-user settings path. Returns 1 on success. */
int mw_settings_path(char *buf, int cap);

/* Display mode helpers shared by the front end and the overlay. */
const char *mw_display_label(int mode);
int mw_display_crop_width(int mode);             /* pixels at 224 lines */
int mw_display_crop_x(int mode, int view_width); /* source offset */

#ifdef __cplusplus
}
#endif

#endif

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

    /* Front-end additions: audio, video presets, window geometry and the
       three per-player control maps. Missing keys in an older settings.ini
       keep these defaults, so old files load cleanly. */
    int sound_on;           /* 0/1, default 1 */
    int shader_preset;      /* 0 OFF / 1 SCANLINES / 2 CRT, default 1 */
    int scanline_intensity; /* 0..100, default 50 */
    int window_w;           /* default 1280, 0 = use the preset list */
    int window_h;           /* default 720,  0 = use the preset list */
    int key_map[3][7];      /* SDL_Scancode per player, actions:
                               [0]=UP [1]=DOWN [2]=LEFT [3]=RIGHT
                               [4]=B1 [5]=B2 [6]=B3 */
    int pad_map[3][7];      /* SDL_GameControllerButton, -1 = unbound */
    char pad_guid[3][33];   /* 32-char SDL joystick GUID + NUL; "" = auto-assign */
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

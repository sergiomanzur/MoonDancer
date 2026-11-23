#ifndef MW_UI_H
#define MW_UI_H

#include <SDL.h>
#include "settings.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct mw_ui mw_ui;

/* Session-only cheat toggles.  Never saved to settings.ini and never seen by
 * the headless targets; the host owns the storage and calls cheats_apply()
 * after s18_run_frame().  All toggles are global (they cover 1P, 2P and 3P).
 * There is deliberately no act_override: the act byte is not writable state
 * (docs/cheats-addresses.md). */
typedef struct {
    int infinite_lives;
    int invincibility;
    int infinite_magic;
    int free_credits;
} mw_cheats;

mw_ui *mw_ui_create(SDL_Window *window, SDL_Renderer *renderer,
                    const char *base_path, const char *background_path);
void mw_ui_destroy(mw_ui *ui);

void mw_ui_begin(mw_ui *ui);
void mw_ui_end(mw_ui *ui);
void mw_ui_process_event(mw_ui *ui, const SDL_Event *event);
int mw_ui_wants_input(mw_ui *ui);

void mw_ui_draw_background(mw_ui *ui);

void mw_ui_draw_launcher(mw_ui *ui, mw_settings *settings,
                         const char *archive_path, const char *archive_status,
                         int *browse, int *start, int *open_settings, int *quit,
                         int *changed);

void mw_ui_draw_settings(mw_ui *ui, mw_settings *settings, int *open,
                         int in_game, int *reset_game, int *quit_game,
                         int *changed);

/* Overlay navigation: the settings, controllers and cheats panels are tabs of
 * the same overlay, and all of them draw the tab chips.  The host reads
 * mw_ui_tab() and draws one panel per frame; it must also disarm input capture
 * whenever the controllers panel is not the one being drawn
 * (mw_input_cancel_capture).  The CHEATS tab exists only in the game overlay:
 * the launcher shows two chips (see mw_ui_draw_settings' in_game flag). */
#define MW_UI_TAB_SETTINGS    0
#define MW_UI_TAB_CONTROLLERS 1
#define MW_UI_TAB_CHEATS      2
int mw_ui_tab(mw_ui *ui);

/* CONTROLLERS panel: live per-player pad state, the stored-GUID assignment
 * row and the seven-action key/pad rebind table.  Clicking a key or pad chip
 * starts a capture ("PRESS A KEY OR BUTTON...", ESC cancels); a captured or
 * cleared binding is written through mw_input_save_maps() into *settings and
 * *changed is set.  *open is cleared by the panel's own CLOSE/X buttons. */
void mw_ui_draw_controllers(mw_ui *ui, mw_settings *settings, int *open,
                            int *changed);

/* CHEATS panel: four session-only global toggles (infinite lives,
 * invincibility, infinite magic, free credits).  The panel writes straight
 * through to *cheats; the host applies them to work RAM after every
 * s18_run_frame().  *open is cleared by the panel's own CLOSE/X buttons. */
void mw_ui_draw_cheats(mw_ui *ui, mw_cheats *cheats, int *open);

void mw_ui_draw_fps(mw_ui *ui, float fps);

#ifdef __cplusplus
}
#endif

#endif

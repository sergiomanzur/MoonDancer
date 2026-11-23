#ifndef MW_UI_H
#define MW_UI_H

#include <SDL.h>
#include "settings.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct mw_ui mw_ui;

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
                         int *browse, int *start, int *open_settings, int *quit);

void mw_ui_draw_settings(mw_ui *ui, mw_settings *settings, int *open,
                         int in_game, int *reset_game, int *quit_game,
                         int *changed);

void mw_ui_draw_fps(mw_ui *ui, float fps);

#ifdef __cplusplus
}
#endif

#endif

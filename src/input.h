/*
 * Moonwalker native port - three-player input merge (keyboard + gamepads).
 *
 * The module owns the connected SDL_GameController objects and the merge of
 * keyboard and pad state for players 1..3.  The SDL front end keeps its own
 * key_state/key_latch arrays and feeds them through mw_input_set_keyboard()
 * on KEYDOWN/KEYUP, so the merge itself has no dependency on an SDL display
 * and can be exercised by build/test_input.c.
 *
 * Today's service bits:
 *   coin 1  0x01    start 1  0x10
 *   coin 2  0x02    start 2  0x20
 *   coin 3  0x80    start 3  0x80   (shared: pad 3 Back is unmapped)
 */
#ifndef MW_INPUT_H
#define MW_INPUT_H

#include <stdint.h>
#include <SDL.h>

#include "settings.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Action bits, one namespace shared by keyboard and pad. */
#define MW_IN_B1    0x01
#define MW_IN_B2    0x02
#define MW_IN_B3    0x04
#define MW_IN_UP    0x08
#define MW_IN_DOWN  0x10
#define MW_IN_LEFT  0x20
#define MW_IN_RIGHT 0x40

#define MW_IN_PLAYERS 3
#define MW_IN_ACTIONS 7

/* Action slot order, identical to settings key_map/pad_map:
   [0]=UP [1]=DOWN [2]=LEFT [3]=RIGHT [4]=B1 [5]=B2 [6]=B3. */
#define MW_IN_ACT_UP    0
#define MW_IN_ACT_DOWN  1
#define MW_IN_ACT_LEFT  2
#define MW_IN_ACT_RIGHT 3
#define MW_IN_ACT_B1    4
#define MW_IN_ACT_B2    5
#define MW_IN_ACT_B3    6

typedef struct {
    uint8_t actions[MW_IN_PLAYERS]; /* MW_IN_* per player, keyboard OR pad */
    uint8_t start[MW_IN_PLAYERS];   /* service-bit contributions per player */
    uint8_t coin[MW_IN_PLAYERS];
} mw_input_result;

void mw_input_init(void);
void mw_input_shutdown(void);
void mw_input_load_maps(const mw_settings *s);
void mw_input_save_maps(mw_settings *s);
void mw_input_process_event(const SDL_Event *e);   /* hot-plug + capture */
void mw_input_poll(mw_input_result *out);          /* merge keyboard+pads */
int  mw_input_pad_count(void);                     /* open pads, 0..3 */
const char *mw_input_pad_name(int player);         /* that player's pad, "" if none */
const char *mw_input_pad_guid(int player);         /* 32-char GUID, "" if none */
/* Binds player to the device with this GUID (NULL/"" clears).  A stored GUID
 * wins over connection order and reserves the player while unplugged; the
 * remaining players auto-fill in connection order.  Persist with
 * mw_input_save_maps(). */
void mw_input_assign(const char *guid, int player);
/* Rebinding capture.  begin_capture(player, action, pad) arms the listener:
 * pad!=0 waits for the next controller button, pad==0 for the next key
 * (ESCAPE cancels).  capture_result() reports 0 while idle/waiting, then 1
 * with the SDL scancode / SDL_GameControllerButton, or -1 on cancel; the
 * terminal answer is sticky until the next begin_capture().  A capture also
 * writes the new binding into the module's maps, so mw_input_save_maps()
 * persists it for the caller. */
void mw_input_begin_capture(int player, int action, int pad);
int  mw_input_capture_result(int *code);  /* 0 waiting, 1 captured, -1 cancel */
/* Disarms a waiting capture and forgets the last answer.  Panels call this
 * when they close or lose focus, so no capture ever outlives the UI that
 * armed it (a later mw_input_capture_result() then reports 0 again). */
void mw_input_cancel_capture(void);
/* 1 while begin_capture() waits for its key/button.  The host uses it to
 * keep its own hotkeys (F1/ESC/P/...) from firing mid-capture, since the
 * module cannot consume an event before the host sees it. */
int  mw_input_capture_active(void);

/* Keyboard feeding.  down=0/1 updates the held state, latch!=0 marks the
 * key as tapped this frame (a press+release inside one frame still counts).
 * main_sdl.c's event loop calls this next to its own key_state arrays. */
void mw_input_set_keyboard(int scancode, int down, int latch);

/* Pure merge, no SDL state: the testable core of mw_input_poll(). */
void mw_input_merge_for_test(const uint8_t key_bits[MW_IN_PLAYERS],
                             const uint8_t pad_bits[MW_IN_PLAYERS],
                             const uint8_t pad_start[MW_IN_PLAYERS],
                             const uint8_t pad_coin[MW_IN_PLAYERS],
                             mw_input_result *out);

#ifdef __cplusplus
}
#endif

#endif /* MW_INPUT_H */

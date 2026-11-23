/*
 * Moon Dancer native port - on-screen touch controls, mapping core.
 *
 * This module turns SDL_FINGER* events into the game's per-player input model.
 * It owns no SDL objects and draws nothing, so build/test_touch.c drives the
 * exact code the front end uses.  The host feeds events with mw_touch_finger()
 * and, once per emulated frame, calls mw_touch_frame() and forwards the result
 * to mw_input_set_touch_action()/mw_input_set_touch_service() before
 * mw_input_poll().  Touch drives player 1 only; pads cover P2/P3.
 *
 * Coordinates are SDL's normalized window coordinates: x and y in [0,1],
 * origin at the top-left.  The geometry below is in the same space, so the
 * drawing pass (Task 7) and this module share one source of truth.  Values are
 * chosen for the landscape lock; MW_TOUCH_STICK_RX/RY are equal in pixels at
 * 16:9, which is what the device class targets.
 *
 * Lifecycle (exactly at most once per frame):
 *   mw_touch_set(enabled, mode)   config change only; drops transient fingers
 *   mw_touch_finger(...)          SDL_FINGERDOWN / FINGERMOTION / FINGERUP
 *   mw_touch_frame(&state)        once per frame: held state + drains latches
 *
 * Latches: menu, start and coin are one-frame latches.  They are set by the
 * press that triggers them and cleared by mw_touch_frame(), so the host must
 * consume the frame state before the next emulated frame.  Action bits stay
 * held while their finger is down (a quick tap is latched for one frame too).
 *
 * MENU is the always-available escape hatch: its hit rect is evaluated even
 * when enabled=0.  enabled=0 suppresses only the stick/D-pad/buttons/coin/
 * start, never MENU (see the plan's Review Focus #2).
 */
#ifndef MW_TOUCH_CONTROLS_H
#define MW_TOUCH_CONTROLS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MW_TOUCH_DPAD   0
#define MW_TOUCH_STICK  1

typedef struct {
    uint8_t actions;   /* MW_IN_* bits for P1 this frame */
    int     menu;      /* MENU corner button tapped (latch) */
    int     start, coin;
    float   stick_x, stick_y; /* -1..1, for drawing */
    int     stick_finger;     /* finger id anchoring the stick, -1 none */
} mw_touch_state;

/* ------------------------------------------------------------------ */
/* geometry (normalized window coordinates, landscape)                 */
/* ------------------------------------------------------------------ */

/* Axis-aligned hit rect, shared by hit-testing and the drawing pass. */
#define MW_TOUCH_RECT_HIT(px, py, x0, y0, x1, y1) \
    ((px) >= (x0) && (px) <= (x1) && (py) >= (y0) && (py) <= (y1))

/* MENU: top-right corner, roughly 7% of the short (height) side.  Drawn and
 * hit even when the rest of the touch layer is hidden. */
#define MW_TOUCH_MENU_X0 0.945f
#define MW_TOUCH_MENU_Y0 0.020f
#define MW_TOUCH_MENU_X1 0.985f
#define MW_TOUCH_MENU_Y1 0.090f
#define MW_TOUCH_MENU_HIT(x, y) \
    MW_TOUCH_RECT_HIT((x), (y), MW_TOUCH_MENU_X0, MW_TOUCH_MENU_Y0, \
                      MW_TOUCH_MENU_X1, MW_TOUCH_MENU_Y1)
#define MW_TOUCH_MENU_CX ((MW_TOUCH_MENU_X0 + MW_TOUCH_MENU_X1) * 0.5f)
#define MW_TOUCH_MENU_CY ((MW_TOUCH_MENU_Y0 + MW_TOUCH_MENU_Y1) * 0.5f)

/* D-pad: fixed cross region on the lower left.  In D-pad mode only presses
 * inside this rect claim the thumb; the position inside it selects the
 * direction (4-way with corner diagonal zones, centre idle). */
#define MW_TOUCH_DPAD_X0 0.035f
#define MW_TOUCH_DPAD_Y0 0.630f
#define MW_TOUCH_DPAD_X1 0.235f
#define MW_TOUCH_DPAD_Y1 0.950f
#define MW_TOUCH_DPAD_CX ((MW_TOUCH_DPAD_X0 + MW_TOUCH_DPAD_X1) * 0.5f)
#define MW_TOUCH_DPAD_CY ((MW_TOUCH_DPAD_Y0 + MW_TOUCH_DPAD_Y1) * 0.5f)
#define MW_TOUCH_DPAD_HALF_W ((MW_TOUCH_DPAD_X1 - MW_TOUCH_DPAD_X0) * 0.5f)
#define MW_TOUCH_DPAD_HALF_H ((MW_TOUCH_DPAD_Y1 - MW_TOUCH_DPAD_Y0) * 0.5f)

/* Joystick: the stick anchors where the finger lands in the left half.  RX/RY
 * are the travel radius per axis (a circle at 16:9); offsets below the
 * dead-zone fraction of that radius emit no direction bits, which removes the
 * jitter a resting thumb would otherwise turn into ghost diagonals. */
#define MW_TOUCH_STICK_RX 0.075f
#define MW_TOUCH_STICK_RY 0.133f
#define MW_TOUCH_STICK_DEADZONE 0.18f

/* Action buttons, right half: B1 punch (lower left), B2 jump (upper), B3
 * magic (lower right).  The rects are the hit areas; Task 7 draws the round
 * icons inside them. */
#define MW_TOUCH_B1_X0 0.760f
#define MW_TOUCH_B1_Y0 0.735f
#define MW_TOUCH_B1_X1 0.855f
#define MW_TOUCH_B1_Y1 0.855f
#define MW_TOUCH_B1_CX ((MW_TOUCH_B1_X0 + MW_TOUCH_B1_X1) * 0.5f)
#define MW_TOUCH_B1_CY ((MW_TOUCH_B1_Y0 + MW_TOUCH_B1_Y1) * 0.5f)

#define MW_TOUCH_B2_X0 0.865f
#define MW_TOUCH_B2_Y0 0.565f
#define MW_TOUCH_B2_X1 0.955f
#define MW_TOUCH_B2_Y1 0.680f
#define MW_TOUCH_B2_CX ((MW_TOUCH_B2_X0 + MW_TOUCH_B2_X1) * 0.5f)
#define MW_TOUCH_B2_CY ((MW_TOUCH_B2_Y0 + MW_TOUCH_B2_Y1) * 0.5f)

#define MW_TOUCH_B3_X0 0.880f
#define MW_TOUCH_B3_Y0 0.760f
#define MW_TOUCH_B3_X1 0.975f
#define MW_TOUCH_B3_Y1 0.880f
#define MW_TOUCH_B3_CX ((MW_TOUCH_B3_X0 + MW_TOUCH_B3_X1) * 0.5f)
#define MW_TOUCH_B3_CY ((MW_TOUCH_B3_Y0 + MW_TOUCH_B3_Y1) * 0.5f)

/* Service chips, bottom centre: coin slot and the 1P START arcade button.
 * Taps latch one frame each; holding does not auto-repeat. */
#define MW_TOUCH_COIN_X0 0.395f
#define MW_TOUCH_COIN_Y0 0.815f
#define MW_TOUCH_COIN_X1 0.470f
#define MW_TOUCH_COIN_Y1 0.935f
#define MW_TOUCH_COIN_CX ((MW_TOUCH_COIN_X0 + MW_TOUCH_COIN_X1) * 0.5f)
#define MW_TOUCH_COIN_CY ((MW_TOUCH_COIN_Y0 + MW_TOUCH_COIN_Y1) * 0.5f)

#define MW_TOUCH_START_X0 0.500f
#define MW_TOUCH_START_Y0 0.815f
#define MW_TOUCH_START_X1 0.625f
#define MW_TOUCH_START_Y1 0.935f
#define MW_TOUCH_START_CX ((MW_TOUCH_START_X0 + MW_TOUCH_START_X1) * 0.5f)
#define MW_TOUCH_START_CY ((MW_TOUCH_START_Y0 + MW_TOUCH_START_Y1) * 0.5f)

/* Left half anchors the stick / hosts the D-pad; right half hosts buttons. */
#define MW_TOUCH_SPLIT_X 0.500f

/* ------------------------------------------------------------------ */
/* API                                                                 */
/* ------------------------------------------------------------------ */

/* Applies a configuration change and forgets every finger in progress (a
 * config flip never leaves a held bit behind).  Call only when the values
 * actually change; the host tracks the applied pair.  mode is clamped to
 * MW_TOUCH_DPAD / MW_TOUCH_STICK (anything else means MW_TOUCH_STICK). */
void mw_touch_set(int enabled, int mode);

/* One SDL finger event.  down=1 for FINGERDOWN and FINGERMOTION, down=0 for
 * FINGERUP.  x/y are SDL's normalized window coordinates.  The module owns
 * multi-finger tracking: buttons are presses (a second finger can hold a
 * button while the first anchors the stick), the stick anchors on its first
 * left-half press, and only motion of the owning finger moves a control.
 *
 * out (optional) receives the state after this event, latches included; it
 * does not drain them.  Informational for immediate drawing; the authoritative
 * per-frame state comes from mw_touch_frame().
 */
void mw_touch_finger(int down, float x, float y, int finger_id,
                     mw_touch_state *out);

/* Once per emulated frame: fills out with the held controls and this frame's
 * latched menu/start/coin, then clears those latches for the next frame.
 * Call exactly once per frame, before feeding mw_input_set_touch_*. */
void mw_touch_frame(mw_touch_state *out);

#ifdef __cplusplus
}
#endif

#endif /* MW_TOUCH_CONTROLS_H */

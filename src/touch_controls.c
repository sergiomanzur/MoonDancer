/*
 * Moon Dancer native port - on-screen touch controls, mapping core.
 *
 * One table of in-progress fingers is the whole state machine.  A press claims
 * a control (MENU, coin, start, an action button, the D-pad or the stick); a
 * motion updates only the stick/D-pad it owns; a release frees the slot.  The
 * frame state is recomputed from the table every frame, so a second finger
 * cannot disturb the first and no bit can stick after its finger is gone.
 */
#include "touch_controls.h"

#include "input.h"

#include <math.h>
#include <string.h>

#define MW_TOUCH_MAX_FINGERS 10

/* tan(22.5 deg): the half-angle of each cardinal octant.  A direction whose
 * minor axis clears it is diagonal; below it the dominant axis alone wins. */
#define MW_TOUCH_DIAGONAL 0.41421356f

enum {
    T_FREE = 0,   /* slot unused */
    T_IGNORE,     /* finger down, owns no control: motion must not claim one */
    T_MENU,
    T_COIN,
    T_START,
    T_B1,
    T_B2,
    T_B3,
    T_DPAD,
    T_STICK
};

typedef struct {
    int   id;                    /* SDL finger id, valid when kind != T_FREE */
    int   kind;
    float x, y;                  /* latest position */
    float anchor_x, anchor_y;    /* stick only */
} mw_touch_finger_rec;

static struct {
    int enabled;
    int mode;
    mw_touch_finger_rec fingers[MW_TOUCH_MAX_FINGERS];
    uint8_t action_latch;        /* taps seen since the last frame */
    uint8_t menu_latch;          /* one-frame service/menu latches */
    uint8_t start_latch;
    uint8_t coin_latch;
} g_touch;

static float clampf(float v, float lo, float hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static void clear_fingers(void)
{
    memset(g_touch.fingers, 0, sizeof(g_touch.fingers));
    g_touch.action_latch = 0;
    g_touch.menu_latch = 0;
    g_touch.start_latch = 0;
    g_touch.coin_latch = 0;
}

static mw_touch_finger_rec *find_finger(int id)
{
    int i;
    for (i = 0; i < MW_TOUCH_MAX_FINGERS; i++) {
        if (g_touch.fingers[i].kind != T_FREE &&
            g_touch.fingers[i].id == id)
            return &g_touch.fingers[i];
    }
    return NULL;
}

static mw_touch_finger_rec *free_finger(void)
{
    int i;
    for (i = 0; i < MW_TOUCH_MAX_FINGERS; i++) {
        if (g_touch.fingers[i].kind == T_FREE)
            return &g_touch.fingers[i];
    }
    return NULL;
}

/* At most one stick and one D-pad may be owned at a time; every button and
 * service chip is independent, which is what lets a second thumb tap B1 while
 * the first anchors the stick. */
static int kind_owned(int kind)
{
    int i;
    for (i = 0; i < MW_TOUCH_MAX_FINGERS; i++) {
        if (g_touch.fingers[i].kind == kind) return 1;
    }
    return 0;
}

/* Stick offset in radius units (unclamped: direction cares about the angle). */
static void stick_offset(const mw_touch_finger_rec *f, float *nx, float *ny)
{
    *nx = (f->x - f->anchor_x) / MW_TOUCH_STICK_RX;
    *ny = (f->y - f->anchor_y) / MW_TOUCH_STICK_RY;
}

/* D-pad offset in half-region units; the same octant/dead-zone rule as the
 * stick keeps a tap in the cross centre idle. */
static void dpad_offset(const mw_touch_finger_rec *f, float *nx, float *ny)
{
    *nx = (f->x - MW_TOUCH_DPAD_CX) / MW_TOUCH_DPAD_HALF_W;
    *ny = (f->y - MW_TOUCH_DPAD_CY) / MW_TOUCH_DPAD_HALF_H;
}

/* 8-way direction with the dead-zone applied to the radius magnitude.  Inside
 * the dead-zone nothing is emitted, so a resting thumb cannot manufacture a
 * diagonal; past it, diagonals appear only in the corner octants. */
static uint8_t dir_bits(float nx, float ny)
{
    uint8_t bits = 0;
    float ax = fabsf(nx);
    float ay = fabsf(ny);

    if (sqrtf(nx * nx + ny * ny) < MW_TOUCH_STICK_DEADZONE) return 0;
    if (ax > ay) {
        bits |= (nx < 0.0f) ? MW_IN_LEFT : MW_IN_RIGHT;
        if (ay > ax * MW_TOUCH_DIAGONAL)
            bits |= (ny < 0.0f) ? MW_IN_UP : MW_IN_DOWN;
    } else {
        bits |= (ny < 0.0f) ? MW_IN_UP : MW_IN_DOWN;
        if (ax > ay * MW_TOUCH_DIAGONAL)
            bits |= (nx < 0.0f) ? MW_IN_LEFT : MW_IN_RIGHT;
    }
    return bits;
}

static uint8_t held_actions(void)
{
    uint8_t bits = 0;
    int i;
    for (i = 0; i < MW_TOUCH_MAX_FINGERS; i++) {
        const mw_touch_finger_rec *f = &g_touch.fingers[i];
        float nx, ny;
        switch (f->kind) {
        case T_B1: bits |= MW_IN_B1; break;
        case T_B2: bits |= MW_IN_B2; break;
        case T_B3: bits |= MW_IN_B3; break;
        case T_DPAD:
            dpad_offset(f, &nx, &ny);
            bits |= dir_bits(nx, ny);
            break;
        case T_STICK:
            stick_offset(f, &nx, &ny);
            bits |= dir_bits(nx, ny);
            break;
        default:
            break;
        }
    }
    return bits;
}

/* The one place a state is assembled, shared by mw_touch_finger (immediate
 * feedback) and mw_touch_frame (authoritative, latches drained by the caller). */
static void fill_state(mw_touch_state *out)
{
    int i;
    if (!out) return;
    memset(out, 0, sizeof(*out));
    out->actions = (uint8_t)(held_actions() | g_touch.action_latch);
    out->menu = g_touch.menu_latch;
    out->start = g_touch.start_latch;
    out->coin = g_touch.coin_latch;
    out->stick_finger = -1;
    for (i = 0; i < MW_TOUCH_MAX_FINGERS; i++) {
        const mw_touch_finger_rec *f = &g_touch.fingers[i];
        if (f->kind != T_STICK) continue;
        stick_offset(f, &out->stick_x, &out->stick_y);
        out->stick_x = clampf(out->stick_x, -1.0f, 1.0f);
        out->stick_y = clampf(out->stick_y, -1.0f, 1.0f);
        out->stick_finger = f->id;
        break;
    }
}

/* Assigns a freshly pressed finger to its control.  MENU is checked first and
 * unconditionally: it is the escape hatch even when the layer is hidden.
 * Every other region is suppressed while disabled, and the finger still gets a
 * slot so its motion cannot later be mistaken for a new press. */
static void claim(mw_touch_finger_rec *f, float x, float y)
{
    f->x = x;
    f->y = y;
    /* Until a region claims it, the press is deliberately inert: dragging an
     * unclaimed thumb onto a button (or MENU) is not a new press. */
    f->kind = T_IGNORE;

    if (MW_TOUCH_MENU_HIT(x, y)) {
        f->kind = T_MENU;
        g_touch.menu_latch = 1;
        return;
    }
    if (!g_touch.enabled) return;

    if (MW_TOUCH_RECT_HIT(x, y, MW_TOUCH_COIN_X0, MW_TOUCH_COIN_Y0,
                          MW_TOUCH_COIN_X1, MW_TOUCH_COIN_Y1)) {
        f->kind = T_COIN;
        g_touch.coin_latch = 1;
        return;
    }
    if (MW_TOUCH_RECT_HIT(x, y, MW_TOUCH_START_X0, MW_TOUCH_START_Y0,
                          MW_TOUCH_START_X1, MW_TOUCH_START_Y1)) {
        f->kind = T_START;
        g_touch.start_latch = 1;
        return;
    }
    if (MW_TOUCH_RECT_HIT(x, y, MW_TOUCH_B1_X0, MW_TOUCH_B1_Y0,
                          MW_TOUCH_B1_X1, MW_TOUCH_B1_Y1)) {
        f->kind = T_B1;
        return;
    }
    if (MW_TOUCH_RECT_HIT(x, y, MW_TOUCH_B2_X0, MW_TOUCH_B2_Y0,
                          MW_TOUCH_B2_X1, MW_TOUCH_B2_Y1)) {
        f->kind = T_B2;
        return;
    }
    if (MW_TOUCH_RECT_HIT(x, y, MW_TOUCH_B3_X0, MW_TOUCH_B3_Y0,
                          MW_TOUCH_B3_X1, MW_TOUCH_B3_Y1)) {
        f->kind = T_B3;
        return;
    }
    if (x >= MW_TOUCH_SPLIT_X) return; /* right half is buttons only */

    if (g_touch.mode == MW_TOUCH_DPAD) {
        if (MW_TOUCH_RECT_HIT(x, y, MW_TOUCH_DPAD_X0, MW_TOUCH_DPAD_Y0,
                              MW_TOUCH_DPAD_X1, MW_TOUCH_DPAD_Y1) &&
            !kind_owned(T_DPAD))
            f->kind = T_DPAD;
    } else if (!kind_owned(T_STICK)) {
        f->kind = T_STICK;
        f->anchor_x = x;
        f->anchor_y = y;
    }
}

void mw_touch_set(int enabled, int mode)
{
    g_touch.enabled = enabled ? 1 : 0;
    g_touch.mode = (mode == MW_TOUCH_DPAD) ? MW_TOUCH_DPAD : MW_TOUCH_STICK;
    clear_fingers();
}

void mw_touch_finger(int down, float x, float y, int finger_id,
                     mw_touch_state *out)
{
    mw_touch_finger_rec *f;

    /* SDL can overshoot the window by a hair on edge swipes. */
    x = clampf(x, 0.0f, 1.0f);
    y = clampf(y, 0.0f, 1.0f);

    f = find_finger(finger_id);
    if (down) {
        if (!f) {
            f = free_finger();
            if (!f) {
                fill_state(out); /* more fingers than slots: ignore this one */
                return;
            }
            memset(f, 0, sizeof(*f));
            f->id = finger_id;
            claim(f, x, y);
        } else {
            f->x = x;
            f->y = y;
        }
        /* Latch every bit any control currently contributes, so a tap that
         * ends before the frame is still delivered for one frame. */
        g_touch.action_latch |= held_actions();
    } else if (f) {
        memset(f, 0, sizeof(*f)); /* kind T_FREE releases the control */
    }
    fill_state(out);
}

void mw_touch_frame(mw_touch_state *out)
{
    fill_state(out);
    g_touch.action_latch = 0;
    g_touch.menu_latch = 0;
    g_touch.start_latch = 0;
    g_touch.coin_latch = 0;
}

/*
 * Throwaway harness for the on-screen touch mapping module (Task 6).
 * Not part of CMake; build it by hand from the repo root:
 *
 *   gcc -I src -I third_party/sdl2_dl/SDL2-2.30.11/x86_64-w64-mingw32/include/SDL2 \
 *       -o build/test_touch.exe build/test_touch.c src/touch_controls.c \
 *       -L third_party/sdl2_dl/SDL2-2.30.11/x86_64-w64-mingw32/lib -lSDL2
 *
 * Exits 0 and prints "ALL TOUCH TESTS PASSED" when every assertion holds.
 * No SDL display is opened; the geometry comes from touch_controls.h so the
 * drawing pass (Task 7) and these assertions share one set of rects.
 *
 * Lifecycle under test: events go in through mw_touch_finger(); the host calls
 * mw_touch_frame() exactly once per emulated frame, which returns the held
 * state and drains menu/start/coin (one-frame latches).
 */
#define SDL_MAIN_HANDLED
#include <SDL.h>

#include "touch_controls.h"
#include "input.h"

#include <math.h>
#include <stdio.h>

static int g_failures;

static void check_int(const char *what, int got, int want)
{
    if (got != want) {
        printf("[FAIL] %s: got 0x%02X want 0x%02X\n", what, got, want);
        g_failures++;
    } else {
        printf("[ ok ] %s = 0x%02X\n", what, got);
    }
}

static void check_near(const char *what, float got, float want)
{
    if (fabsf(got - want) > 0.001f) {
        printf("[FAIL] %s: got %.4f want %.4f\n", what, (double)got, (double)want);
        g_failures++;
    } else {
        printf("[ ok ] %s = %.4f\n", what, (double)got);
    }
}

/* One emulated frame: returns the state and drains the one-frame latches. */
static void frame(mw_touch_state *st)
{
    mw_touch_frame(st);
}

int main(void)
{
    mw_touch_state st;
    const float anchor_x = 0.30f;
    const float anchor_y = 0.80f;

    /* ---- (a) D-pad: tap the up arm -> UP only, release clears --------- */
    printf("-- (a) dpad tap --\n");
    mw_touch_set(1, MW_TOUCH_DPAD);
    mw_touch_finger(1, MW_TOUCH_DPAD_CX,
                    MW_TOUCH_DPAD_CY - 0.6f * MW_TOUCH_DPAD_HALF_H, 1, &st);
    frame(&st);
    check_int("a up tap actions", st.actions, MW_IN_UP);
    check_int("a up tap menu", st.menu, 0);
    check_int("a up tap start", st.start, 0);
    check_int("a up tap coin", st.coin, 0);
    check_int("a up tap no stick", st.stick_finger, -1);
    frame(&st);
    check_int("a up held", st.actions, MW_IN_UP);
    mw_touch_finger(0, MW_TOUCH_DPAD_CX,
                    MW_TOUCH_DPAD_CY - 0.6f * MW_TOUCH_DPAD_HALF_H, 1, &st);
    frame(&st);
    check_int("a up release", st.actions, 0);

    /* ---- (b) D-pad: 4-way with corner diagonal zones, centre idle ----- */
    printf("-- (b) dpad directions --\n");
    mw_touch_set(1, MW_TOUCH_DPAD);
    mw_touch_finger(1, MW_TOUCH_DPAD_CX + 0.7f * MW_TOUCH_DPAD_HALF_W,
                    MW_TOUCH_DPAD_CY, 2, &st);
    frame(&st);
    check_int("b right arm", st.actions, MW_IN_RIGHT);
    mw_touch_finger(1, MW_TOUCH_DPAD_CX + 0.75f * MW_TOUCH_DPAD_HALF_W,
                    MW_TOUCH_DPAD_CY + 0.75f * MW_TOUCH_DPAD_HALF_H, 2, &st);
    frame(&st);
    check_int("b corner diagonal", st.actions, MW_IN_RIGHT | MW_IN_DOWN);
    mw_touch_finger(1, MW_TOUCH_DPAD_CX, MW_TOUCH_DPAD_CY, 2, &st);
    frame(&st);
    check_int("b centre idle", st.actions, 0);
    mw_touch_finger(1, MW_TOUCH_DPAD_CX,
                    MW_TOUCH_DPAD_CY - 0.7f * MW_TOUCH_DPAD_HALF_H, 2, &st);
    frame(&st);
    check_int("b up arm", st.actions, MW_IN_UP);
    mw_touch_finger(0, MW_TOUCH_DPAD_CX,
                    MW_TOUCH_DPAD_CY - 0.7f * MW_TOUCH_DPAD_HALF_H, 2, &st);
    frame(&st);
    check_int("b release clears", st.actions, 0);

    /* ---- (c) stick: anchors where the finger lands, LEFT only --------- */
    printf("-- (c) stick anchor --\n");
    mw_touch_set(1, MW_TOUCH_STICK);
    mw_touch_finger(1, anchor_x, anchor_y, 5, &st);
    frame(&st);
    check_int("c anchor actions", st.actions, 0);
    check_int("c anchor finger", st.stick_finger, 5);
    check_near("c anchor x", st.stick_x, 0.0f);
    check_near("c anchor y", st.stick_y, 0.0f);
    mw_touch_finger(1, anchor_x - 0.75f * MW_TOUCH_STICK_RX, anchor_y, 5, &st);
    frame(&st);
    check_int("c left actions", st.actions, MW_IN_LEFT);
    check_int("c left finger", st.stick_finger, 5);
    check_near("c left x", st.stick_x, -0.75f);
    check_near("c left y", st.stick_y, 0.0f);
    mw_touch_finger(1, anchor_x - 0.75f * MW_TOUCH_STICK_RX,
                    anchor_y - 0.75f * MW_TOUCH_STICK_RY, 5, &st);
    frame(&st);
    check_int("c up-left diagonal", st.actions, MW_IN_LEFT | MW_IN_UP);
    mw_touch_finger(1, anchor_x + 3.0f * MW_TOUCH_STICK_RX, anchor_y, 5, &st);
    frame(&st);
    check_int("c far right clamps direction", st.actions, MW_IN_RIGHT);
    check_near("c far right x clamps", st.stick_x, 1.0f);
    mw_touch_finger(0, anchor_x + 3.0f * MW_TOUCH_STICK_RX, anchor_y, 5, &st);
    frame(&st);
    check_int("c release actions", st.actions, 0);
    check_int("c release finger", st.stick_finger, -1);
    check_near("c release x", st.stick_x, 0.0f);

    /* ---- (d) dead-zone: 0.18 of the stick radius, no ghost diagonals -- */
    printf("-- (d) stick dead-zone --\n");
    mw_touch_set(1, MW_TOUCH_STICK);
    mw_touch_finger(1, anchor_x, anchor_y, 6, &st);
    mw_touch_finger(1, anchor_x + 0.10f * MW_TOUCH_STICK_RX, anchor_y, 6, &st);
    frame(&st);
    check_int("d 0.10 radius -> idle", st.actions, 0);
    check_near("d drawing still shows offset", st.stick_x, 0.10f);
    mw_touch_finger(1, anchor_x + 0.17f * MW_TOUCH_STICK_RX, anchor_y, 6, &st);
    frame(&st);
    check_int("d 0.17 radius -> idle", st.actions, 0);
    mw_touch_finger(1, anchor_x + 0.19f * MW_TOUCH_STICK_RX, anchor_y, 6, &st);
    frame(&st);
    check_int("d 0.19 radius -> RIGHT", st.actions, MW_IN_RIGHT);
    mw_touch_finger(0, anchor_x + 0.19f * MW_TOUCH_STICK_RX, anchor_y, 6, &st);
    frame(&st);

    /* ---- (e) MENU fires even when touch controls are disabled --------- */
    printf("-- (e) menu escape hatch --\n");
    mw_touch_set(0, MW_TOUCH_STICK);
    check_int("e menu hit macro inside",
              MW_TOUCH_MENU_HIT(MW_TOUCH_MENU_CX, MW_TOUCH_MENU_CY), 1);
    check_int("e menu hit macro outside",
              MW_TOUCH_MENU_HIT(0.10f, 0.10f), 0);
    mw_touch_finger(1, MW_TOUCH_MENU_CX, MW_TOUCH_MENU_CY, 7, &st);
    frame(&st);
    check_int("e disabled menu still fires", st.menu, 1);
    check_int("e disabled menu actions", st.actions, 0);
    check_int("e disabled menu start", st.start, 0);
    check_int("e disabled menu coin", st.coin, 0);
    frame(&st);
    check_int("e menu latch drains once", st.menu, 0);
    mw_touch_finger(0, MW_TOUCH_MENU_CX, MW_TOUCH_MENU_CY, 7, &st);
    frame(&st);

    /* Disabled suppresses the game controls but never the menu. */
    mw_touch_finger(1, MW_TOUCH_B1_CX, MW_TOUCH_B1_CY, 8, &st);
    frame(&st);
    check_int("e disabled button suppressed", st.actions, 0);
    mw_touch_finger(0, MW_TOUCH_B1_CX, MW_TOUCH_B1_CY, 8, &st);
    frame(&st);

    mw_touch_set(1, MW_TOUCH_STICK);
    mw_touch_finger(1, MW_TOUCH_MENU_CX, MW_TOUCH_MENU_CY, 9, &st);
    frame(&st);
    check_int("e enabled menu fires", st.menu, 1);
    check_int("e menu is not a game control", st.actions, 0);
    mw_touch_finger(0, MW_TOUCH_MENU_CX, MW_TOUCH_MENU_CY, 9, &st);
    frame(&st);

    /* A press that claims no region stays inert even if it then drags over
     * MENU: only a fresh press is a tap. */
    mw_touch_set(0, MW_TOUCH_STICK);
    mw_touch_finger(1, 0.10f, 0.50f, 13, &st);
    mw_touch_finger(1, MW_TOUCH_MENU_CX, MW_TOUCH_MENU_CY, 13, &st);
    frame(&st);
    check_int("e drag onto menu is not a press", st.menu, 0);
    mw_touch_finger(0, MW_TOUCH_MENU_CX, MW_TOUCH_MENU_CY, 13, &st);
    frame(&st);

    /* ---- (f) coin / start are one-frame latches ----------------------- */
    printf("-- (f) coin/start latches --\n");
    mw_touch_set(1, MW_TOUCH_STICK);
    mw_touch_finger(1, MW_TOUCH_COIN_CX, MW_TOUCH_COIN_CY, 10, &st);
    frame(&st);
    check_int("f coin press", st.coin, 1);
    check_int("f coin press start", st.start, 0);
    check_int("f coin press actions", st.actions, 0);
    frame(&st);
    check_int("f coin latch drains once", st.coin, 0);
    mw_touch_finger(0, MW_TOUCH_COIN_CX, MW_TOUCH_COIN_CY, 10, &st);
    frame(&st);
    check_int("f coin release", st.coin, 0);

    mw_touch_finger(1, MW_TOUCH_START_CX, MW_TOUCH_START_CY, 11, &st);
    frame(&st);
    check_int("f start press", st.start, 1);
    check_int("f start press coin", st.coin, 0);
    frame(&st);
    check_int("f start latch drains once", st.start, 0);
    mw_touch_finger(0, MW_TOUCH_START_CX, MW_TOUCH_START_CY, 11, &st);
    frame(&st);

    /* A tap that starts and ends between two frames still lands once. */
    mw_touch_finger(1, MW_TOUCH_COIN_CX, MW_TOUCH_COIN_CY, 12, &st);
    mw_touch_finger(0, MW_TOUCH_COIN_CX, MW_TOUCH_COIN_CY, 12, &st);
    frame(&st);
    check_int("f sub-frame coin tap", st.coin, 1);
    frame(&st);
    check_int("f sub-frame coin drains", st.coin, 0);

    /* ---- (g) multi-finger: stick holds while a button taps ------------ */
    printf("-- (g) multi-finger --\n");
    mw_touch_set(1, MW_TOUCH_STICK);
    mw_touch_finger(1, anchor_x, anchor_y, 20, &st);
    mw_touch_finger(1, anchor_x - 0.70f * MW_TOUCH_STICK_RX, anchor_y, 20, &st);
    frame(&st);
    check_int("g stick left", st.actions, MW_IN_LEFT);
    check_int("g stick finger", st.stick_finger, 20);
    mw_touch_finger(1, MW_TOUCH_B1_CX, MW_TOUCH_B1_CY, 21, &st);
    frame(&st);
    check_int("g stick + punch", st.actions, MW_IN_LEFT | MW_IN_B1);
    check_int("g stick still anchored", st.stick_finger, 20);
    check_near("g stick x held", st.stick_x, -0.70f);
    mw_touch_finger(1, MW_TOUCH_B3_CX, MW_TOUCH_B3_CY, 22, &st);
    frame(&st);
    check_int("g third finger magic", st.actions,
              MW_IN_LEFT | MW_IN_B1 | MW_IN_B3);
    mw_touch_finger(0, MW_TOUCH_B1_CX, MW_TOUCH_B1_CY, 21, &st);
    frame(&st);
    check_int("g punch release keeps stick+magic", st.actions,
              MW_IN_LEFT | MW_IN_B3);
    mw_touch_finger(0, MW_TOUCH_B3_CX, MW_TOUCH_B3_CY, 22, &st);
    frame(&st);
    check_int("g magic release keeps stick", st.actions, MW_IN_LEFT);
    mw_touch_finger(0, anchor_x - 0.70f * MW_TOUCH_STICK_RX, anchor_y, 20, &st);
    frame(&st);
    check_int("g all released", st.actions, 0);
    check_int("g stick finger cleared", st.stick_finger, -1);

    /* ---- (h) three buttons, config change drops held gestures --------- */
    printf("-- (h) buttons + config reset --\n");
    mw_touch_set(1, MW_TOUCH_STICK);
    mw_touch_finger(1, MW_TOUCH_B2_CX, MW_TOUCH_B2_CY, 30, &st);
    frame(&st);
    check_int("h jump", st.actions, MW_IN_B2);
    mw_touch_finger(0, MW_TOUCH_B2_CX, MW_TOUCH_B2_CY, 30, &st);
    frame(&st);
    check_int("h jump release", st.actions, 0);
    mw_touch_finger(1, MW_TOUCH_B3_CX, MW_TOUCH_B3_CY, 31, &st);
    frame(&st);
    check_int("h magic", st.actions, MW_IN_B3);
    mw_touch_finger(0, MW_TOUCH_B3_CX, MW_TOUCH_B3_CY, 31, &st);
    frame(&st);

    mw_touch_finger(1, MW_TOUCH_B1_CX, MW_TOUCH_B1_CY, 32, &st);
    frame(&st);
    check_int("h punch", st.actions, MW_IN_B1);
    mw_touch_set(1, MW_TOUCH_STICK);
    mw_touch_finger(1, 0.868f, 0.715f, 33, &st); /* gap between the rects */
    frame(&st);
    check_int("h gap press is inert", st.actions, 0);
    mw_touch_finger(1, MW_TOUCH_B1_CX, MW_TOUCH_B1_CY, 33, &st);
    frame(&st);
    check_int("h drag into a button is not a press", st.actions, 0);
    mw_touch_finger(0, MW_TOUCH_B1_CX, MW_TOUCH_B1_CY, 33, &st);
    frame(&st);

    mw_touch_finger(1, MW_TOUCH_B1_CX, MW_TOUCH_B1_CY, 34, &st);
    frame(&st);
    check_int("h punch again", st.actions, MW_IN_B1);
    mw_touch_set(1, MW_TOUCH_DPAD); /* config change forgets held fingers */
    frame(&st);
    check_int("h config change clears", st.actions, 0);
    mw_touch_finger(0, MW_TOUCH_B1_CX, MW_TOUCH_B1_CY, 34, &st);
    frame(&st);

    /* ---- (i) mode is clamped; unknown values mean joystick ------------ */
    printf("-- (i) mode clamp --\n");
    mw_touch_set(1, 9);
    mw_touch_finger(1, anchor_x, anchor_y, 40, &st);
    mw_touch_finger(1, anchor_x - 0.60f * MW_TOUCH_STICK_RX, anchor_y, 40, &st);
    frame(&st);
    check_int("i mode clamp sticks", st.actions, MW_IN_LEFT);
    check_int("i clamp anchors a finger", st.stick_finger, 40);
    mw_touch_finger(0, anchor_x - 0.60f * MW_TOUCH_STICK_RX, anchor_y, 40, &st);
    frame(&st);
    check_int("i released", st.actions, 0);

    if (g_failures) {
        printf("\nTOUCH TESTS FAILED (%d assertion%s)\n", g_failures,
               g_failures == 1 ? "" : "s");
        return 1;
    }
    printf("\nALL TOUCH TESTS PASSED\n");
    return 0;
}

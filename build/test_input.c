/*
 * Throwaway harness for the 3-player input merge module (Task 4).
 * Not part of CMake; build it by hand from the repo root:
 *
 *   gcc -I src -I third_party/sdl2_dl/SDL2-2.30.11/x86_64-w64-mingw32/include/SDL2 \
 *       -o build/test_input.exe build/test_input.c src/input.c src/settings.c \
 *       -L third_party/sdl2_dl/SDL2-2.30.11/x86_64-w64-mingw32/lib -lSDL2
 *
 * Exits 0 and prints "ALL INPUT TESTS PASSED" when every assertion holds.
 * No SDL display is opened.  Sections (a)-(f) use the pure merge and the
 * keyboard feed; section (g) attaches SDL virtual gamepads (when no physical
 * controller is present) to exercise the pad path, Start/Back service bits,
 * the left stick, and a real SDL_CONTROLLERDEVICEREMOVED hot-unplug.
 * Run it with build\SDL2.dll on PATH or in the working directory.
 */
#define SDL_MAIN_HANDLED
#include <SDL.h>

#include "input.h"
#include "settings.h"

#include <stdio.h>
#include <string.h>

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

static void check_str(const char *what, const char *got, const char *want)
{
    if (strcmp(got, want) != 0) {
        printf("[FAIL] %s: got \"%s\" want \"%s\"\n", what, got, want);
        g_failures++;
    } else {
        printf("[ ok ] %s = \"%s\"\n", what, got);
    }
}

/* Pokes a virtual gamepad's button so mw_input_poll() sees it. */
static void pad_button(SDL_Joystick *js, int button, int down)
{
    SDL_JoystickSetVirtualButton(js, button, (Uint8)(down ? 1 : 0));
    SDL_PumpEvents();
}

int main(void)
{
    uint8_t key[3], pad[3], pstart[3], pcoin[3];
    mw_input_result r;

    /* ---- (a) keyboard-only merge returns the keyboard bits ------------ */
    printf("-- (a) keyboard only --\n");
    memset(key, 0, sizeof(key));
    memset(pad, 0, sizeof(pad));
    memset(pstart, 0, sizeof(pstart));
    memset(pcoin, 0, sizeof(pcoin));
    memset(&r, 0xAA, sizeof(r)); /* poison: the merge must write every field */
    key[0] = (uint8_t)(MW_IN_UP | MW_IN_B1);
    key[1] = MW_IN_LEFT;
    key[2] = (uint8_t)(MW_IN_DOWN | MW_IN_B2);
    mw_input_merge_for_test(key, pad, pstart, pcoin, &r);
    check_int("a p1 actions", r.actions[0], MW_IN_UP | MW_IN_B1);
    check_int("a p2 actions", r.actions[1], MW_IN_LEFT);
    check_int("a p3 actions", r.actions[2], MW_IN_DOWN | MW_IN_B2);
    check_int("a p1 start", r.start[0], 0);
    check_int("a p2 start", r.start[1], 0);
    check_int("a p3 start", r.start[2], 0);
    check_int("a p1 coin", r.coin[0], 0);
    check_int("a p2 coin", r.coin[1], 0);
    check_int("a p3 coin", r.coin[2], 0);

    /* ---- (b) pad bits OR into the keyboard bits ----------------------- */
    printf("-- (b) pad ORs in --\n");
    memset(key, 0, sizeof(key));
    memset(pad, 0, sizeof(pad));
    memset(pstart, 0, sizeof(pstart));
    memset(pcoin, 0, sizeof(pcoin));
    memset(&r, 0xAA, sizeof(r));
    key[1] = MW_IN_LEFT;
    pad[1] = (uint8_t)(MW_IN_RIGHT | MW_IN_B3);
    mw_input_merge_for_test(key, pad, pstart, pcoin, &r);
    check_int("b p2 actions", r.actions[1], MW_IN_LEFT | MW_IN_RIGHT | MW_IN_B3);
    check_int("b p1 actions untouched", r.actions[0], 0);
    check_int("b p3 actions untouched", r.actions[2], 0);
    check_int("b p2 start untouched", r.start[1], 0);
    check_int("b p2 coin untouched", r.coin[1], 0);

    /* ---- (c) P3 pad Back contributes nothing (0x80 collision) --------- */
    printf("-- (c) P3 Back is unmapped --\n");
    memset(key, 0, sizeof(key));
    memset(pad, 0, sizeof(pad));
    memset(pstart, 0, sizeof(pstart));
    memset(pcoin, 0, sizeof(pcoin));
    memset(&r, 0xAA, sizeof(r));
    key[2] = MW_IN_UP;
    pad[2] = MW_IN_B1;
    /* A naive P3 coin mapping would feed the shared 0x80 bit here. */
    pcoin[2] = 0x80;
    mw_input_merge_for_test(key, pad, pstart, pcoin, &r);
    check_int("c p3 actions", r.actions[2], MW_IN_UP | MW_IN_B1);
    check_int("c p3 coin", r.coin[2], 0);
    check_int("c p3 start", r.start[2], 0);
    {
        uint8_t service = 0xFF;
        service &= (uint8_t)~(r.start[2] | r.coin[2]);
        check_int("c p3 service byte untouched", service, 0xFF);
    }

    /* ---- (d) P3 pad Start fires 0x80 exactly once --------------------- */
    printf("-- (d) P3 Start -> 0x80 once --\n");
    memset(key, 0, sizeof(key));
    memset(pad, 0, sizeof(pad));
    memset(pstart, 0, sizeof(pstart));
    memset(pcoin, 0, sizeof(pcoin));
    memset(&r, 0xAA, sizeof(r));
    pstart[2] = 0x80;
    pcoin[2] = 0x80; /* Start+Back both held must still yield one 0x80 */
    mw_input_merge_for_test(key, pad, pstart, pcoin, &r);
    check_int("d p3 start", r.start[2], 0x80);
    check_int("d p3 coin", r.coin[2], 0);
    check_int("d p3 start/coin disjoint", r.start[2] & r.coin[2], 0);
    {
        int p;
        int owners = 0;
        for (p = 0; p < 3; p++) {
            if ((r.start[p] | r.coin[p]) & 0x80) owners++;
        }
        check_int("d 0x80 has exactly one owner", owners, 1);
    }
    check_int("d p1 unaffected", r.start[0] | r.coin[0], 0);
    check_int("d p2 unaffected", r.start[1] | r.coin[1], 0);

    /* ---- (e) pad unplug clears pad bits, keyboard survives ------------ */
    printf("-- (e) hot-unplug keeps keyboard --\n");
    memset(key, 0, sizeof(key));
    memset(pad, 0, sizeof(pad));
    memset(pstart, 0, sizeof(pstart));
    memset(pcoin, 0, sizeof(pcoin));
    memset(&r, 0xAA, sizeof(r));
    key[0] = (uint8_t)(MW_IN_UP | MW_IN_B1);
    pad[0] = (uint8_t)(MW_IN_DOWN | MW_IN_B2);
    pstart[0] = 0x10;
    pcoin[0] = 0x01;
    mw_input_merge_for_test(key, pad, pstart, pcoin, &r);
    check_int("e merged p1 actions", r.actions[0],
              MW_IN_UP | MW_IN_DOWN | MW_IN_B1 | MW_IN_B2);
    check_int("e p1 start", r.start[0], 0x10);
    check_int("e p1 coin", r.coin[0], 0x01);
    /* SDL_CONTROLLERDEVICEREMOVED: the pad's contributions are dropped. */
    memset(pad, 0, sizeof(pad));
    memset(pstart, 0, sizeof(pstart));
    memset(pcoin, 0, sizeof(pcoin));
    mw_input_merge_for_test(key, pad, pstart, pcoin, &r);
    check_int("e p1 actions after unplug", r.actions[0], MW_IN_UP | MW_IN_B1);
    check_int("e p1 start after unplug", r.start[0], 0);
    check_int("e p1 coin after unplug", r.coin[0], 0);

    /* ---- (f) key_map -> poll path, P3 double-bound KP_2 preserved ---- */
    printf("-- (f) key_map poll path --\n");
    {
        static const uint8_t action_bits[MW_IN_ACTIONS] = {
            MW_IN_UP, MW_IN_DOWN, MW_IN_LEFT, MW_IN_RIGHT,
            MW_IN_B1, MW_IN_B2, MW_IN_B3
        };
        mw_settings s;
        int p, a;
        mw_settings_defaults(&s);
        mw_input_load_maps(&s);
        memset(&r, 0xAA, sizeof(r));

        /* Every default binding, every player: the pressed scancode sets the
         * bits of all actions it is mapped to and nothing on other players. */
        for (p = 0; p < MW_IN_PLAYERS; p++) {
            for (a = 0; a < MW_IN_ACTIONS; a++) {
                int sc = s.key_map[p][a];
                int want = 0, other, b;
                char label[96];

                for (b = 0; b < MW_IN_ACTIONS; b++) {
                    if (s.key_map[p][b] == sc) want |= action_bits[b];
                }
                mw_input_set_keyboard(sc, 1, 1);
                mw_input_poll(&r);
                snprintf(label, sizeof(label), "f p%d action %d (sc %d)", p + 1, a, sc);
                check_int(label, r.actions[p], want);
                for (other = 0; other < MW_IN_PLAYERS; other++) {
                    if (other == p) continue;
                    snprintf(label, sizeof(label), "f p%d action %d no leak p%d",
                             p + 1, a, other + 1);
                    check_int(label, r.actions[other], 0);
                }
                mw_input_set_keyboard(sc, 0, 0);
                mw_input_poll(&r);
                check_int("f release clears", r.actions[p], 0);
            }
        }

        /* The P3 KP_2 double bind is deliberate: DOWN and B2 together. */
        mw_input_set_keyboard(SDL_SCANCODE_KP_2, 1, 1);
        mw_input_poll(&r);
        check_int("f p3 KP_2 -> DOWN|B2", r.actions[2], MW_IN_DOWN | MW_IN_B2);
        check_int("f p1 unaffected by KP_2", r.actions[0], 0);
        mw_input_set_keyboard(SDL_SCANCODE_KP_2, 0, 0);
        mw_input_poll(&r);

        check_int("f no pad start", r.start[0] | r.start[1] | r.start[2], 0);
        check_int("f no pad coin", r.coin[0] | r.coin[1] | r.coin[2], 0);
        check_int("f pad count", mw_input_pad_count(), 0);
        check_str("f pad name p1", mw_input_pad_name(0), "");
        check_str("f pad guid p1", mw_input_pad_guid(0), "");
    }

    /* ---- (g) real pad path via SDL virtual gamepads -------------------- */
    printf("-- (g) pad path (SDL virtual gamepads) --\n");
    if (SDL_Init(SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS) != 0) {
        printf("[skip] SDL_Init: %s\n", SDL_GetError());
    } else if (SDL_NumJoysticks() != 0) {
        printf("[skip] %d physical controller(s) attached\n", SDL_NumJoysticks());
    } else {
        SDL_Joystick *jh[3] = { NULL, NULL, NULL };
        int vi[3];
        int code = 0;
        int i;

        for (i = 0; i < 3; i++)
            vi[i] = SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_GAMECONTROLLER, 6, 15, 0);
        mw_input_init();
        check_int("g three pads opened", mw_input_pad_count(), 3);
        for (i = 0; i < 3; i++) {
            char label[64];
            const char *guid = mw_input_pad_guid(i);
            snprintf(label, sizeof(label), "g p%d pad name", i + 1);
            check_str(label, mw_input_pad_name(i), "Virtual Controller");
            snprintf(label, sizeof(label), "g p%d pad guid length", i + 1);
            check_int(label, (int)strlen(guid), 32);
            jh[i] = SDL_JoystickOpen(vi[i]);
        }

        /* dpad + face buttons through pad_map defaults (UP, B1). */
        pad_button(jh[0], SDL_CONTROLLER_BUTTON_DPAD_UP, 1);
        pad_button(jh[0], SDL_CONTROLLER_BUTTON_A, 1);
        mw_input_poll(&r);
        check_int("g p1 dpad UP + A", r.actions[0], MW_IN_UP | MW_IN_B1);
        check_int("g p2 silent", r.actions[1], 0);
        check_int("g p3 silent", r.actions[2], 0);
        pad_button(jh[0], SDL_CONTROLLER_BUTTON_DPAD_UP, 0);
        pad_button(jh[0], SDL_CONTROLLER_BUTTON_A, 0);
        mw_input_poll(&r);
        check_int("g p1 released", r.actions[0], 0);

        /* Left stick acts as a second dpad. */
        SDL_JoystickSetVirtualAxis(jh[0], SDL_CONTROLLER_AXIS_LEFTX, -32768);
        SDL_JoystickSetVirtualAxis(jh[0], SDL_CONTROLLER_AXIS_LEFTY, 30000);
        SDL_PumpEvents();
        mw_input_poll(&r);
        check_int("g p1 stick LEFT+DOWN", r.actions[0], MW_IN_LEFT | MW_IN_DOWN);
        SDL_JoystickSetVirtualAxis(jh[0], SDL_CONTROLLER_AXIS_LEFTX, 0);
        SDL_JoystickSetVirtualAxis(jh[0], SDL_CONTROLLER_AXIS_LEFTY, 0);
        SDL_PumpEvents();
        mw_input_poll(&r);
        check_int("g p1 stick centered", r.actions[0], 0);

        /* Start / Back map to each player's service bits. */
        pad_button(jh[0], SDL_CONTROLLER_BUTTON_START, 1);
        mw_input_poll(&r);
        check_int("g p1 pad start", r.start[0], 0x10);
        check_int("g p1 pad start no coin", r.coin[0], 0);
        pad_button(jh[0], SDL_CONTROLLER_BUTTON_START, 0);
        pad_button(jh[0], SDL_CONTROLLER_BUTTON_BACK, 1);
        mw_input_poll(&r);
        check_int("g p1 pad back = coin1", r.coin[0], 0x01);
        pad_button(jh[0], SDL_CONTROLLER_BUTTON_BACK, 0);

        pad_button(jh[1], SDL_CONTROLLER_BUTTON_START, 1);
        pad_button(jh[1], SDL_CONTROLLER_BUTTON_BACK, 1);
        mw_input_poll(&r);
        check_int("g p2 pad start", r.start[1], 0x20);
        check_int("g p2 pad back = coin2", r.coin[1], 0x02);
        pad_button(jh[1], SDL_CONTROLLER_BUTTON_START, 0);
        pad_button(jh[1], SDL_CONTROLLER_BUTTON_BACK, 0);

        /* P3: Start owns the shared 0x80; Back contributes nothing. */
        pad_button(jh[2], SDL_CONTROLLER_BUTTON_BACK, 1);
        pad_button(jh[2], SDL_CONTROLLER_BUTTON_START, 1);
        mw_input_poll(&r);
        check_int("g p3 pad start", r.start[2], 0x80);
        check_int("g p3 pad back unmapped", r.coin[2], 0);
        check_int("g p3 start/coin disjoint", r.start[2] & r.coin[2], 0);
        {
            uint8_t service = 0xFF;
            int p;
            for (p = 0; p < MW_IN_PLAYERS; p++)
                service &= (uint8_t)~(r.start[p] | r.coin[p]);
            check_int("g p3 start clears 0x80 once", service, 0x7F);
        }
        pad_button(jh[2], SDL_CONTROLLER_BUTTON_BACK, 0);
        pad_button(jh[2], SDL_CONTROLLER_BUTTON_START, 0);
        mw_input_poll(&r);
        check_int("g p3 back alone = nothing", r.start[2] | r.coin[2], 0);

        /* Binding capture: a pad button lands in pad_map for that action. */
        mw_input_begin_capture(1, MW_IN_ACT_B3, 1);
        check_int("g pad capture waiting", mw_input_capture_result(&code), 0);
        {
            SDL_Event ce;
            memset(&ce, 0, sizeof(ce));
            ce.type = SDL_CONTROLLERBUTTONDOWN;
            ce.cbutton.which = 0;
            ce.cbutton.button = SDL_CONTROLLER_BUTTON_Y;
            mw_input_process_event(&ce);
        }
        check_int("g pad capture done", mw_input_capture_result(&code), 1);
        check_int("g pad capture code", code, SDL_CONTROLLER_BUTTON_Y);
        check_int("g pad capture sticky", mw_input_capture_result(&code), 1);
        pad_button(jh[1], SDL_CONTROLLER_BUTTON_Y, 1);
        mw_input_poll(&r);
        check_int("g captured button drives P2 B3", r.actions[1], MW_IN_B3);
        pad_button(jh[1], SDL_CONTROLLER_BUTTON_Y, 0);
        mw_input_poll(&r);

        /* Stored GUID wins over connection order.  All three virtual pads
         * share one GUID, so binding player 3 to that GUID must put it on
         * slot 0: slot 0's dpad then drives P3 and nobody else. */
        {
            char shared[33];
            snprintf(shared, sizeof(shared), "%s", mw_input_pad_guid(0));
            check_int("g virtual pads share a GUID",
                      (int)(strlen(shared) == 32 &&
                            strcmp(shared, mw_input_pad_guid(1)) == 0 &&
                            strcmp(shared, mw_input_pad_guid(2)) == 0), 1);

            mw_input_assign(shared, 2);
            pad_button(jh[0], SDL_CONTROLLER_BUTTON_DPAD_UP, 1);
            mw_input_poll(&r);
            check_int("g stored GUID puts p3 on slot 0", r.actions[2], MW_IN_UP);
            check_int("g stored GUID p1 does not steal", r.actions[0], 0);
            check_int("g stored GUID p2 does not steal", r.actions[1], 0);
            pad_button(jh[0], SDL_CONTROLLER_BUTTON_DPAD_UP, 0);
            mw_input_poll(&r);

            /* A stored GUID that matches no connected pad reserves that
             * player: the three attached pads fill the other players only. */
            mw_input_assign("", 2); /* back to plain connection order */
            mw_input_assign("deadbeefdeadbeefdeadbeefdeadbeef", 0);
            check_str("g reserved player has no pad", mw_input_pad_guid(0), "");
            check_int("g reserved keeps all pads open", mw_input_pad_count(), 3);
            pad_button(jh[0], SDL_CONTROLLER_BUTTON_DPAD_LEFT, 1);
            mw_input_poll(&r);
            check_int("g reserved p1 stays empty", r.actions[0], 0);
            check_int("g slot 0 auto-fills p2 instead", r.actions[1], MW_IN_LEFT);
            check_int("g p3 untouched by reservation", r.actions[2], 0);
            pad_button(jh[0], SDL_CONTROLLER_BUTTON_DPAD_LEFT, 0);
            mw_input_poll(&r);

            /* Clearing the binding returns to connection order for the
             * remaining assertions. */
            mw_input_assign("", 0);
            pad_button(jh[0], SDL_CONTROLLER_BUTTON_DPAD_DOWN, 1);
            mw_input_poll(&r);
            check_int("g reset returns slot 0 to p1", r.actions[0], MW_IN_DOWN);
            pad_button(jh[0], SDL_CONTROLLER_BUTTON_DPAD_DOWN, 0);
            mw_input_poll(&r);
        }

        /* Hot-unplug: the pad's bits vanish, the keyboard's survive. */
        mw_input_set_keyboard(SDL_SCANCODE_Z, 1, 1);
        pad_button(jh[0], SDL_CONTROLLER_BUTTON_DPAD_DOWN, 1);
        pad_button(jh[0], SDL_CONTROLLER_BUTTON_START, 1);
        mw_input_poll(&r);
        check_int("g p1 pad+key merged", r.actions[0], MW_IN_DOWN | MW_IN_B1);
        check_int("g p1 pad start live", r.start[0], 0x10);

        SDL_JoystickClose(jh[0]);
        SDL_JoystickDetachVirtual(vi[0]);
        {
            SDL_Event e;
            SDL_PumpEvents();
            while (SDL_PollEvent(&e)) mw_input_process_event(&e);
        }
        check_int("g pad removed", mw_input_pad_count(), 2);
        mw_input_poll(&r);
        check_int("g keyboard survives unplug", r.actions[0], MW_IN_B1);
        check_int("g pad start dropped", r.start[0], 0);
        check_int("g pad coin dropped", r.coin[0], 0);
        mw_input_set_keyboard(SDL_SCANCODE_Z, 0, 0);
        mw_input_poll(&r);
        check_int("g keyboard released", r.actions[0], 0);

        mw_input_shutdown();
        for (i = 1; i < 3; i++) {
            SDL_JoystickClose(jh[i]);
            SDL_JoystickDetachVirtual(vi[i]);
        }
        check_int("g shutdown closed pads", mw_input_pad_count(), 0);
        SDL_Quit();
    }

    /* ---- (h) keyboard binding capture, no SDL display needed ---------- */
    printf("-- (h) keyboard capture --\n");
    {
        mw_settings s;
        SDL_Event ke;
        int code = 0;

        mw_settings_defaults(&s);
        mw_input_load_maps(&s);

        mw_input_begin_capture(0, MW_IN_ACT_B3, 0);
        check_int("h waiting", mw_input_capture_result(&code), 0);
        memset(&ke, 0, sizeof(ke));
        ke.type = SDL_KEYDOWN;
        ke.key.repeat = 0;
        ke.key.keysym.scancode = SDL_SCANCODE_ESCAPE;
        mw_input_process_event(&ke);
        check_int("h escape cancels", mw_input_capture_result(&code), -1);

        mw_input_begin_capture(0, MW_IN_ACT_B3, 0);
        memset(&ke, 0, sizeof(ke));
        ke.type = SDL_KEYDOWN;
        ke.key.keysym.scancode = SDL_SCANCODE_Q;
        mw_input_process_event(&ke);
        check_int("h key capture done", mw_input_capture_result(&code), 1);
        check_int("h key capture code", code, SDL_SCANCODE_Q);

        mw_input_set_keyboard(SDL_SCANCODE_Q, 1, 1);
        mw_input_poll(&r);
        check_int("h captured key drives P1 B3", r.actions[0], MW_IN_B3);
        mw_input_set_keyboard(SDL_SCANCODE_Q, 0, 0);
        mw_input_poll(&r);

        mw_input_begin_capture(-1, MW_IN_ACT_B1, 0); /* disarm */
        check_int("h idle result", mw_input_capture_result(&code), 0);
    }

    if (g_failures) {
        printf("\nINPUT TESTS FAILED (%d assertion%s)\n", g_failures,
               g_failures == 1 ? "" : "s");
        return 1;
    }
    printf("\nALL INPUT TESTS PASSED\n");
    return 0;
}


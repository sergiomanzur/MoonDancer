/*
 * Moonwalker native port - three-player input merge (keyboard + gamepads).
 */
#include "input.h"

#include <stdio.h>
#include <string.h>

#define MW_IN_MAX_PADS   3
#define MW_IN_DEADZONE   12000   /* left-stick axis threshold (of 32767) */
#define MW_IN_PADNAME_MAX 64u

/* Action slot -> MW_IN_* bit; order matches settings key_map/pad_map. */
static const uint8_t k_action_bit[MW_IN_ACTIONS] = {
    MW_IN_UP, MW_IN_DOWN, MW_IN_LEFT, MW_IN_RIGHT,
    MW_IN_B1, MW_IN_B2, MW_IN_B3
};

/* Per-player service-bit contributions.  Player 3's start shares coin3's
 * 0x80 and P3's Back is deliberately unmapped (0 = contributes nothing);
 * coin3 does not exist in the MAME service layout. */
static const uint8_t k_pad_start_bit[MW_IN_PLAYERS] = { 0x10, 0x20, 0x80 };
static const uint8_t k_pad_coin_bit[MW_IN_PLAYERS]  = { 0x01, 0x02, 0x00 };

typedef struct {
    SDL_GameController *ctrl;
    SDL_JoystickID instance;
    char guid[33];                 /* 32-char SDL GUID string */
    char name[MW_IN_PADNAME_MAX];
} mw_pad_slot;

static struct {
    mw_settings maps;                        /* key_map, pad_map, pad_guid */
    int maps_loaded;
    mw_pad_slot pads[MW_IN_MAX_PADS];        /* open pads, connection order */
    int slots;
    int player_slot[MW_IN_PLAYERS];          /* player -> slot, -1 = keyboard */
    uint8_t key_state[SDL_NUM_SCANCODES];
    uint8_t key_latch[SDL_NUM_SCANCODES];
    /* Touch is a third source for player 1 only.  Held state plus a one-frame
     * tap latch, mirroring key_state/key_latch. */
    uint8_t touch_state[MW_IN_ACTIONS];
    uint8_t touch_latch[MW_IN_ACTIONS];
    uint8_t touch_start_held, touch_coin_held;   /* P1 service, held */
    uint8_t touch_start_latch, touch_coin_latch; /* P1 service, one frame */
    /* rebinding capture */
    int capture_state;                       /* 0 idle, 1 waiting, 2 done */
    int capture_player;
    int capture_action;
    int capture_pad;
    int capture_code;
    int capture_status;
} g_in;

/* ------------------------------------------------------------------ */
/* pad bookkeeping                                                     */
/* ------------------------------------------------------------------ */

/* GUID strings are hex; settings.ini may hold either case. */
static int guid_equal(const char *a, const char *b)
{
    int i;
    if (!a || !b) return 0;
    for (i = 0; i < 32; i++) {
        char ca = a[i], cb = b[i];
        if (!ca || !cb) return ca == cb;
        if (ca >= 'A' && ca <= 'F') ca = (char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'F') cb = (char)(cb - 'A' + 'a');
        if (ca != cb) return 0;
    }
    return 1;
}

/* Stored pad_guid[player] wins over connection order; pads whose player has
 * no stored GUID fill the remaining players in connection order.  A stored
 * GUID that is not connected reserves its player: another pad is never
 * silently promoted into that slot (the UI clears the binding instead). */
static void assign_pads(void)
{
    int used[MW_IN_MAX_PADS] = { 0, 0, 0 };
    int p, s;

    for (p = 0; p < MW_IN_PLAYERS; p++) g_in.player_slot[p] = -1;

    for (p = 0; p < MW_IN_PLAYERS; p++) {
        if (!g_in.maps.pad_guid[p][0]) continue;
        for (s = 0; s < g_in.slots; s++) {
            if (used[s]) continue;
            if (guid_equal(g_in.maps.pad_guid[p], g_in.pads[s].guid)) {
                used[s] = 1;
                g_in.player_slot[p] = s;
                break;
            }
        }
    }

    for (s = 0; s < g_in.slots; s++) {
        if (used[s]) continue;
        for (p = 0; p < MW_IN_PLAYERS; p++) {
            if (g_in.player_slot[p] < 0 && !g_in.maps.pad_guid[p][0]) {
                g_in.player_slot[p] = s;
                used[s] = 1;
                break;
            }
        }
    }
}

static int slot_of_instance(SDL_JoystickID id)
{
    int s;
    for (s = 0; s < g_in.slots; s++) {
        if (g_in.pads[s].instance == id) return s;
    }
    return -1;
}

static void open_pad(int device_index)
{
    SDL_GameController *ctrl;
    SDL_Joystick *joy;
    SDL_JoystickID instance;
    mw_pad_slot *slot;
    const char *name;
    int s;

    if (device_index < 0 || g_in.slots >= MW_IN_MAX_PADS) return;
    if (!SDL_IsGameController(device_index)) return;

    ctrl = SDL_GameControllerOpen(device_index);
    if (!ctrl) return;
    joy = SDL_GameControllerGetJoystick(ctrl);
    if (!joy) {
        SDL_GameControllerClose(ctrl);
        return;
    }
    instance = SDL_JoystickInstanceID(joy);
    for (s = 0; s < g_in.slots; s++) {
        if (g_in.pads[s].instance == instance) { /* already open */
            SDL_GameControllerClose(ctrl);
            return;
        }
    }

    slot = &g_in.pads[g_in.slots];
    memset(slot, 0, sizeof(*slot));
    slot->ctrl = ctrl;
    slot->instance = instance;
    SDL_JoystickGetGUIDString(SDL_JoystickGetGUID(joy), slot->guid,
                              (int)sizeof(slot->guid));
    name = SDL_GameControllerName(ctrl);
    snprintf(slot->name, sizeof(slot->name), "%s", name ? name : "Controller");
    g_in.slots++;
    assign_pads();
}

static void close_slot(int s)
{
    if (s < 0 || s >= g_in.slots) return;
    if (g_in.pads[s].ctrl) SDL_GameControllerClose(g_in.pads[s].ctrl);
    if (s + 1 < g_in.slots) {
        memmove(&g_in.pads[s], &g_in.pads[s + 1],
                (size_t)(g_in.slots - s - 1) * sizeof(g_in.pads[0]));
    }
    g_in.slots--;
    memset(&g_in.pads[g_in.slots], 0, sizeof(g_in.pads[0]));
    assign_pads();
}

/* ------------------------------------------------------------------ */
/* lifecycle                                                           */
/* ------------------------------------------------------------------ */

void mw_input_init(void)
{
    int n, i;

    /* Idempotent: release anything a previous init opened. */
    for (i = 0; i < g_in.slots; i++) {
        if (g_in.pads[i].ctrl) SDL_GameControllerClose(g_in.pads[i].ctrl);
    }

    memset(g_in.key_state, 0, sizeof(g_in.key_state));
    memset(g_in.key_latch, 0, sizeof(g_in.key_latch));
    memset(g_in.touch_state, 0, sizeof(g_in.touch_state));
    memset(g_in.touch_latch, 0, sizeof(g_in.touch_latch));
    g_in.touch_start_held = g_in.touch_coin_held = 0;
    g_in.touch_start_latch = g_in.touch_coin_latch = 0;
    memset(g_in.pads, 0, sizeof(g_in.pads));
    memset(g_in.player_slot, 0xFF, sizeof(g_in.player_slot));
    g_in.slots = 0;
    g_in.maps_loaded = 0;
    g_in.capture_state = 0;

    /* Defaults keep the merge sane even before load_maps() is called. */
    mw_settings_defaults(&g_in.maps);
    g_in.maps_loaded = 1;

    n = SDL_NumJoysticks();
    for (i = 0; i < n && g_in.slots < MW_IN_MAX_PADS; i++)
        open_pad(i);
    assign_pads();
}

void mw_input_shutdown(void)
{
    int s;
    for (s = 0; s < g_in.slots; s++) {
        if (g_in.pads[s].ctrl) SDL_GameControllerClose(g_in.pads[s].ctrl);
    }
    memset(g_in.pads, 0, sizeof(g_in.pads));
    g_in.slots = 0;
    memset(g_in.player_slot, 0xFF, sizeof(g_in.player_slot));
    memset(g_in.key_state, 0, sizeof(g_in.key_state));
    memset(g_in.key_latch, 0, sizeof(g_in.key_latch));
    memset(g_in.touch_state, 0, sizeof(g_in.touch_state));
    memset(g_in.touch_latch, 0, sizeof(g_in.touch_latch));
    g_in.touch_start_held = g_in.touch_coin_held = 0;
    g_in.touch_start_latch = g_in.touch_coin_latch = 0;
    g_in.capture_state = 0;
    g_in.maps_loaded = 0; /* next init/poll re-seeds the defaults */
}

void mw_input_load_maps(const mw_settings *s)
{
    if (!s) return;
    memcpy(g_in.maps.key_map, s->key_map, sizeof(g_in.maps.key_map));
    memcpy(g_in.maps.pad_map, s->pad_map, sizeof(g_in.maps.pad_map));
    memcpy(g_in.maps.pad_guid, s->pad_guid, sizeof(g_in.maps.pad_guid));
    g_in.maps.pad_guid[0][32] = '\0';
    g_in.maps.pad_guid[1][32] = '\0';
    g_in.maps.pad_guid[2][32] = '\0';
    g_in.maps_loaded = 1;
    assign_pads();
}

void mw_input_save_maps(mw_settings *s)
{
    if (!s) return;
    memcpy(s->key_map, g_in.maps.key_map, sizeof(s->key_map));
    memcpy(s->pad_map, g_in.maps.pad_map, sizeof(s->pad_map));
    memcpy(s->pad_guid, g_in.maps.pad_guid, sizeof(s->pad_guid));
}

void mw_input_assign(const char *guid, int player)
{
    const char *g = guid ? guid : "";
    if (player < 0 || player >= MW_IN_PLAYERS) return;
    /* Only full 32-char SDL GUID strings are storable; anything else clears
     * the binding so a truncated value cannot reserve a player forever. */
    if (g[0] && strlen(g) != 32) g = "";
    snprintf(g_in.maps.pad_guid[player], sizeof(g_in.maps.pad_guid[player]),
             "%s", g);
    assign_pads();
}

int mw_input_pad_count(void)
{
    return g_in.slots;
}

const char *mw_input_pad_name(int player)
{
    int s;
    if (player < 0 || player >= MW_IN_PLAYERS) return "";
    s = g_in.player_slot[player];
    if (s < 0 || s >= g_in.slots) return "";
    return g_in.pads[s].name;
}

const char *mw_input_pad_guid(int player)
{
    int s;
    if (player < 0 || player >= MW_IN_PLAYERS) return "";
    s = g_in.player_slot[player];
    if (s < 0 || s >= g_in.slots) return "";
    return g_in.pads[s].guid;
}

/* ------------------------------------------------------------------ */
/* keyboard feeding                                                    */
/* ------------------------------------------------------------------ */

void mw_input_set_keyboard(int scancode, int down, int latch)
{
    if (scancode < 0 || scancode >= SDL_NUM_SCANCODES) return;
    g_in.key_state[scancode] = down ? 1 : 0;
    if (latch) g_in.key_latch[scancode] = 1;
}

void mw_input_set_touch_action(int action, int down)
{
    if (action < 0 || action >= MW_IN_ACTIONS) return;
    g_in.touch_state[action] = down ? 1 : 0;
    if (down) g_in.touch_latch[action] = 1;
}

void mw_input_set_touch_service(int start, int coin)
{
    /* Only P1's own service bits; the merge masks again, so an accidental
     * P2/P3 bit can never escape this feed. */
    g_in.touch_start_held = start ? k_pad_start_bit[0] : 0;
    g_in.touch_coin_held = coin ? k_pad_coin_bit[0] : 0;
    if (start) g_in.touch_start_latch = k_pad_start_bit[0];
    if (coin) g_in.touch_coin_latch = k_pad_coin_bit[0];
}

/* ------------------------------------------------------------------ */
/* merge                                                               */
/* ------------------------------------------------------------------ */

void mw_input_merge_for_test(const uint8_t key_bits[MW_IN_PLAYERS],
                             const uint8_t pad_bits[MW_IN_PLAYERS],
                             const uint8_t pad_start[MW_IN_PLAYERS],
                             const uint8_t pad_coin[MW_IN_PLAYERS],
                             uint8_t touch_bits, uint8_t touch_start,
                             uint8_t touch_coin,
                             mw_input_result *out)
{
    int p;
    if (!out) return;
    for (p = 0; p < MW_IN_PLAYERS; p++) {
        uint8_t kb = key_bits ? key_bits[p] : 0;
        uint8_t pb = pad_bits ? pad_bits[p] : 0;
        out->actions[p] = (uint8_t)(kb | pb);
        /* Mask each player to its own service bits: a player can never
         * contribute another player's coin/start, and P3's coin mask of 0
         * keeps the shared 0x80 owned solely by P3's start. */
        out->start[p] = (uint8_t)((pad_start ? pad_start[p] : 0) &
                                  k_pad_start_bit[p]);
        out->coin[p] = (uint8_t)((pad_coin ? pad_coin[p] : 0) &
                                 k_pad_coin_bit[p]);
    }
    /* Touch is player 1's third source; mask its service contribution to
     * P1's own bits so it cannot impersonate another player. */
    out->actions[0] = (uint8_t)(out->actions[0] | touch_bits);
    out->start[0] = (uint8_t)(out->start[0] |
                              (touch_start & k_pad_start_bit[0]));
    out->coin[0] = (uint8_t)(out->coin[0] | (touch_coin & k_pad_coin_bit[0]));
}

void mw_input_poll(mw_input_result *out)
{
    uint8_t key_bits[MW_IN_PLAYERS] = { 0, 0, 0 };
    uint8_t pad_bits[MW_IN_PLAYERS] = { 0, 0, 0 };
    uint8_t pad_start[MW_IN_PLAYERS] = { 0, 0, 0 };
    uint8_t pad_coin[MW_IN_PLAYERS] = { 0, 0, 0 };
    int p, a;

    if (!out) return;
    if (!g_in.maps_loaded) {
        mw_settings_defaults(&g_in.maps);
        g_in.maps_loaded = 1;
    }

    /* Keyboard: held state OR the one-frame tap latch.  A scancode bound to
     * two actions (P3's KP_2 is DOWN and B2) sets both bits. */
    for (p = 0; p < MW_IN_PLAYERS; p++) {
        for (a = 0; a < MW_IN_ACTIONS; a++) {
            int sc = g_in.maps.key_map[p][a];
            if (sc < 0 || sc >= SDL_NUM_SCANCODES) continue;
            if (g_in.key_state[sc] || g_in.key_latch[sc])
                key_bits[p] |= k_action_bit[a];
        }
    }

    /* Pads: mapped buttons, left stick as a second set of directions, and
     * Start/Back as the player's service bits. */
    for (p = 0; p < MW_IN_PLAYERS; p++) {
        int s = g_in.player_slot[p];
        SDL_GameController *ctrl;
        Sint16 ax, ay;

        if (s < 0 || s >= g_in.slots) continue;
        ctrl = g_in.pads[s].ctrl;
        if (!ctrl) continue;

        for (a = 0; a < MW_IN_ACTIONS; a++) {
            int b = g_in.maps.pad_map[p][a];
            if (b < 0 || b >= SDL_CONTROLLER_BUTTON_MAX) continue;
            if (SDL_GameControllerGetButton(ctrl, (SDL_GameControllerButton)b))
                pad_bits[p] |= k_action_bit[a];
        }

        ax = SDL_GameControllerGetAxis(ctrl, SDL_CONTROLLER_AXIS_LEFTX);
        ay = SDL_GameControllerGetAxis(ctrl, SDL_CONTROLLER_AXIS_LEFTY);
        if (ax < -MW_IN_DEADZONE) pad_bits[p] |= MW_IN_LEFT;
        if (ax > MW_IN_DEADZONE) pad_bits[p] |= MW_IN_RIGHT;
        if (ay < -MW_IN_DEADZONE) pad_bits[p] |= MW_IN_UP;
        if (ay > MW_IN_DEADZONE) pad_bits[p] |= MW_IN_DOWN;

        if (SDL_GameControllerGetButton(ctrl, SDL_CONTROLLER_BUTTON_START))
            pad_start[p] |= k_pad_start_bit[p];
        if (SDL_GameControllerGetButton(ctrl, SDL_CONTROLLER_BUTTON_BACK))
            pad_coin[p] |= k_pad_coin_bit[p];
    }

    /* Touch (player 1): held bit OR the one-frame tap latch, the same rule as
     * the keyboard.  Service latches are drained with the key latch below. */
    {
        uint8_t touch_bits = 0;
        uint8_t touch_start = (uint8_t)(g_in.touch_start_held |
                                        g_in.touch_start_latch);
        uint8_t touch_coin = (uint8_t)(g_in.touch_coin_held |
                                       g_in.touch_coin_latch);
        for (a = 0; a < MW_IN_ACTIONS; a++) {
            if (g_in.touch_state[a] || g_in.touch_latch[a])
                touch_bits |= k_action_bit[a];
        }
        mw_input_merge_for_test(key_bits, pad_bits, pad_start, pad_coin,
                                touch_bits, touch_start, touch_coin, out);
    }
    memset(g_in.key_latch, 0, sizeof(g_in.key_latch));
    memset(g_in.touch_latch, 0, sizeof(g_in.touch_latch));
    g_in.touch_start_latch = 0;
    g_in.touch_coin_latch = 0;
}

/* ------------------------------------------------------------------ */
/* capture                                                             */
/* ------------------------------------------------------------------ */

static void capture_finish(int status, int code)
{
    g_in.capture_status = status;
    g_in.capture_code = code;
    g_in.capture_state = 2;
}

static void capture_apply(int code)
{
    if (g_in.capture_pad) {
        g_in.maps.pad_map[g_in.capture_player][g_in.capture_action] = code;
    } else {
        g_in.maps.key_map[g_in.capture_player][g_in.capture_action] = code;
    }
    capture_finish(1, code);
}

void mw_input_begin_capture(int player, int action, int pad)
{
    mw_input_cancel_capture();
    if (player < 0 || player >= MW_IN_PLAYERS) return;
    if (action < 0 || action >= MW_IN_ACTIONS) return;
    g_in.capture_player = player;
    g_in.capture_action = action;
    g_in.capture_pad = pad ? 1 : 0;
    g_in.capture_state = 1;
}

void mw_input_cancel_capture(void)
{
    g_in.capture_state = 0;
    g_in.capture_code = 0;
    g_in.capture_status = 0;
}

int mw_input_capture_active(void)
{
    return g_in.capture_state == 1;
}

int mw_input_capture_result(int *code)
{
    if (code) *code = 0;
    if (g_in.capture_state != 2) return 0;
    if (code) *code = g_in.capture_code;
    return g_in.capture_status;
}

/* ------------------------------------------------------------------ */
/* events                                                              */
/* ------------------------------------------------------------------ */

void mw_input_process_event(const SDL_Event *e)
{
    if (!e) return;
    switch (e->type) {
    case SDL_CONTROLLERDEVICEADDED:
        open_pad(e->cdevice.which); /* device index, not instance id */
        break;
    case SDL_CONTROLLERDEVICEREMOVED: {
        int s = slot_of_instance(e->cdevice.which);
        if (s >= 0) close_slot(s); /* pad contributions drop, keys survive */
        break;
    }
    case SDL_KEYDOWN:
        if (g_in.capture_state != 1 || e->key.repeat) break;
        if (e->key.keysym.scancode == SDL_SCANCODE_ESCAPE) {
            /* Escape cancels either capture mode, so a pad capture with no
             * controller attached is never a dead end. */
            capture_finish(-1, 0);
        } else if (!g_in.capture_pad) {
            capture_apply((int)e->key.keysym.scancode);
        }
        break;
    case SDL_CONTROLLERBUTTONDOWN:
        if (g_in.capture_state != 1 || !g_in.capture_pad) break;
        /* Source guard: a player that already owns a pad only captures from
         * that pad's instance.  A stray press on another controller (or on a
         * second pad bound to another player) would otherwise steal the
         * binding for the wrong device.  Players without a pad accept any
         * controller, so a reservation can still be filled by plugging in. */
        {
            int s = g_in.player_slot[g_in.capture_player];
            if (s >= 0 && s < g_in.slots &&
                g_in.pads[s].instance != e->cbutton.which)
                break;
        }
        capture_apply((int)e->cbutton.button);
        break;
    default:
        break;
    }
}

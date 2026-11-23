/*
 * Throwaway harness (build/ only, never in CMake): drives the real
 * mw_ui_draw_controllers() panel in a real SDL window with synthetic events,
 * exactly the way main_sdl.c / main_launcher.c dispatch it, and captures the
 * rendered frames to BMP.
 *
 * modes:
 *   panel  <ini>   exercise the panel: reserved slot, ESC cancel, rebind,
 *                  pad-capture cancel, tab chips, close disarms capture
 *   rebind <ini>   click P1/B1 KEY, press Q, save settings.ini
 *   check  <ini>   reload the ini and assert Q (not Z) drives P1 B1
 */
#define SDL_MAIN_HANDLED
#include <SDL.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "imgui.h"

#include "input.h"
#include "settings.h"
#include "ui.h"

static SDL_Window *g_win;
static SDL_Renderer *g_ren;
static mw_ui *g_ui;
static mw_settings g_st;
static const char *g_ini;
static const char *g_base = "C:/Users/sergi/Homestead/code/moonwalkerRecomp/";
static const char *g_shots = "C:/Users/sergi/Homestead/code/moonwalkerRecomp/build/t5_shots";
static int g_open = 1;
static int g_changed;
static int g_fail;
static const char *g_pending_shot;

static void check(int cond, const char *what)
{
    printf("%s %s\n", cond ? "[ ok ]" : "[FAIL]", what);
    if (!cond) g_fail++;
}

static void save_if_dirty(void)
{
    if (g_changed && g_ini) {
        mw_settings_save(&g_st, g_ini);
        g_changed = 0;
        printf("saved %s\n", g_ini);
    }
}

static void pump(void)
{
    mw_ui_begin(g_ui);
    if (g_open) {
        if (mw_ui_tab(g_ui) == MW_UI_TAB_CONTROLLERS)
            mw_ui_draw_controllers(g_ui, &g_st, &g_open, &g_changed);
        else
            mw_ui_draw_settings(g_ui, &g_st, &g_open, 0, nullptr, nullptr, &g_changed);
    }
    /* The hosts' one-line disarm cover, repeated verbatim. */
    if (!(g_open && mw_ui_tab(g_ui) == MW_UI_TAB_CONTROLLERS))
        mw_input_cancel_capture();
    mw_ui_end(g_ui);

    if (g_pending_shot) {
        int w = 0, h = 0;
        char path[512];
        SDL_GetRendererOutputSize(g_ren, &w, &h);
        SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
        if (s) {
            SDL_RenderReadPixels(g_ren, nullptr, SDL_PIXELFORMAT_ARGB8888,
                                 s->pixels, s->pitch);
            snprintf(path, sizeof(path), "%s/%s.bmp", g_shots, g_pending_shot);
            SDL_SaveBMP(s, path);
            SDL_FreeSurface(s);
            printf("shot %s\n", path);
        }
        g_pending_shot = nullptr;
    }
    SDL_RenderPresent(g_ren);
    save_if_dirty();
    SDL_Delay(16);
}

static void shot(const char *name)
{
    g_pending_shot = name;
    pump();
}

/* Clicks are injected straight into ImGui's IO queue: the SDL event plumbing
 * in mw_ui_process_event() is a thin, unchanged wrapper, and this keeps the
 * coordinates deterministic regardless of where the OS pointer happens to be. */
static void mouse_pos(int x, int y)
{
    ImGui::GetIO().AddMousePosEvent((float)x, (float)y);
}

static void click(int x, int y)
{
    ImGuiIO &io = ImGui::GetIO();
    io.AddMousePosEvent((float)x, (float)y);
    pump();
    io.AddMouseButtonEvent(0, true);
    pump();
    io.AddMouseButtonEvent(0, false);
    pump();
}

static void key(SDL_Scancode sc)
{
    SDL_Event e;
    int busy = mw_input_capture_active();
    memset(&e, 0, sizeof(e));
    e.type = SDL_KEYDOWN;
    e.key.keysym.scancode = sc;
    e.key.keysym.sym = SDL_GetKeyFromScancode(sc);
    mw_ui_process_event(g_ui, &e);
    mw_input_process_event(&e);
    memset(&e, 0, sizeof(e));
    e.type = SDL_KEYUP;
    e.key.keysym.scancode = sc;
    e.key.keysym.sym = SDL_GetKeyFromScancode(sc);
    mw_ui_process_event(g_ui, &e);
    mw_input_process_event(&e);
    pump();
    if (!busy) printf("note: host hotkey path would also see scancode %d\n", (int)sc);
}

/* ---- virtual gamepads (SDL 2.0.14+); the pad page sees them as real pads ---- */
static SDL_GameController *g_vc[3];
static SDL_Joystick *g_vj[3];
static int g_vn;

static void attach_virtual_pads(int n)
{
    for (int i = 0; i < n && i < 3; i++) {
        int idx = SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_GAMECONTROLLER, 6, 15, 0);
        if (idx < 0) {
            printf("attach virtual pad failed: %s\n", SDL_GetError());
            return;
        }
        g_vc[i] = SDL_GameControllerOpen(idx);
        if (!g_vc[i]) {
            printf("virtual pad open failed: %s\n", SDL_GetError());
            return;
        }
        g_vj[i] = SDL_GameControllerGetJoystick(g_vc[i]);
        g_vn++;
    }
    printf("attached %d virtual pads\n", g_vn);
}

/* SDL does not queue button events for virtual pads (Task 4 hit the same
 * wall), so hand the module the event struct SDL itself would produce: its
 * fields are the ones mw_input_process_event() reads. */
static void press_pad(int pad, int button)
{
    SDL_Event e;
    if (pad < 0 || pad >= g_vn) return;
    memset(&e, 0, sizeof(e));
    e.type = SDL_CONTROLLERBUTTONDOWN;
    e.cbutton.which = SDL_JoystickInstanceID(g_vj[pad]);
    e.cbutton.button = (Uint8)button;
    e.cbutton.state = SDL_PRESSED;
    mw_input_process_event(&e);
    pump();
    e.type = SDL_CONTROLLERBUTTONUP;
    e.cbutton.state = SDL_RELEASED;
    mw_input_process_event(&e);
    pump();
}

static std::string read_file(const char *path)
{
    std::string out;
    FILE *f = path ? fopen(path, "rb") : nullptr;
    if (!f) return out;
    char buf[512];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
    fclose(f);
    return out;
}

/* Panel geometry mirrors mw_ui_draw_controllers (DisplaySize 1280x720, s=1). */
static const float SCALE_W = 1080.0f, SCALE_H = 700.0f;
static const float PANEL_X = (1280.0f - SCALE_W) * 0.5f;
static const float PANEL_Y = (720.0f - SCALE_H) * 0.5f;
static const float PX = PANEL_X + 38.0f;
static const float PY = PANEL_Y + 28.0f;
static const float COLPX = (SCALE_W - 76.0f - 18.0f * 2) / 3.0f;
static const float TOP = PY + 104.0f;

static int col_x(int p) { return (int)(PX + p * (COLPX + 18.0f)); }
static int keychip_x(int p) { return col_x(p) + 108 + 38; }
static int padchip_x(int p) { return col_x(p) + 192 + 65; }
static int row_y(int a) { return (int)(TOP + 158.0f + a * 30.0f + 12.0f); }
static int assign_x(int p) { return (int)(col_x(p) + COLPX - 105.0f); }
static int assign_y(void) { return (int)(TOP + 102.0f + 14.0f); }
static int tab_x(int i) { return (int)(PANEL_X + SCALE_W - 54.0f - 254.0f + i * 130.0f + 62.0f); }
static int tab_y(void) { return (int)(PANEL_Y + 34.0f); }
/* The SETTINGS panel is narrower; its nav chips sit further left. */
static int stab_x(int i) { return (int)((1280.0f - 780.0f) * 0.5f + 780.0f - 54.0f - 254.0f + i * 130.0f + 62.0f); }
static int close_x(void) { return (int)(PANEL_X + SCALE_W - 38.0f - 105.0f); }
static int close_y(void) { return (int)(PY + 618.0f + 24.0f); }

static void run_rebind(void)
{
    pump(); /* first frame: overlay opens on SETTINGS */
    click(stab_x(1), tab_y());
    pump();
    printf("P1 B1 before: key=%d\n", g_st.key_map[0][MW_IN_ACT_B1]);
    check(g_st.key_map[0][MW_IN_ACT_B1] == SDL_SCANCODE_Z, "P1 B1 starts as Z");
    click(keychip_x(0), row_y(MW_IN_ACT_B1));
    check(mw_input_capture_active() == 1, "clicking the P1 B1 KEY chip arms capture");
    key(SDL_SCANCODE_Q);
    check(mw_input_capture_active() == 0, "Q answers the capture");
    check(g_st.key_map[0][MW_IN_ACT_B1] == SDL_SCANCODE_Q, "capture wrote Q into key_map_p1 B1");
    shot("r_bound");
    save_if_dirty();
}

static void run_check(void)
{
    mw_input_result r;
    mw_input_load_maps(&g_st);
    printf("settings.ini key_map_p0 (P1) B1 = %d (SDL_SCANCODE_Q = %d)\n",
           g_st.key_map[0][MW_IN_ACT_B1], (int)SDL_SCANCODE_Q);
    check(g_st.key_map[0][MW_IN_ACT_B1] == SDL_SCANCODE_Q, "reloaded ini carries Q for P1 B1");
    mw_input_set_keyboard(SDL_SCANCODE_Q, 1, 1);
    mw_input_poll(&r);
    check((r.actions[0] & MW_IN_B1) != 0, "Q drives P1 B1 through the merge");
    mw_input_set_keyboard(SDL_SCANCODE_Q, 0, 0);
    mw_input_poll(&r);
    mw_input_set_keyboard(SDL_SCANCODE_Z, 1, 1);
    mw_input_poll(&r);
    check((r.actions[0] & MW_IN_B1) == 0, "Z no longer drives P1 B1");
    mw_input_set_keyboard(SDL_SCANCODE_Z, 0, 0);
    mw_input_poll(&r);
}

static void run_panel(void)
{
    int code = 0;
    std::string ini;

    /* Enter through the real path: the overlay opens on SETTINGS and its
     * CONTROLLERS chip switches the tab. */
    pump();
    click(stab_x(1), tab_y());
    check(mw_ui_tab(g_ui) == MW_UI_TAB_CONTROLLERS,
          "settings panel's CONTROLLERS chip switches the tab");
    pump();
    shot("c0_idle");

    /* ---- reserved slot (ruling 1) ---- */
    snprintf(g_st.pad_guid[2], sizeof(g_st.pad_guid[2]), "%s",
             "deadbeefdeadbeefdeadbeefdeadbeef");
    mw_input_load_maps(&g_st);
    g_changed = 1;
    pump();
    ini = read_file(g_ini);
    check(ini.find("pad_guid_p2=deadbeefdeadbeefdeadbeefdeadbeef") != std::string::npos,
          "reserved GUID persisted to settings.ini");
    shot("c1_reserved");
    click(assign_x(2), assign_y());
    check(g_st.pad_guid[2][0] == '\0', "first click on a reserved PAD chip clears it");
    ini = read_file(g_ini);
    check(ini.find("pad_guid_p2=\r\n") != std::string::npos ||
          ini.find("pad_guid_p2=\n") != std::string::npos,
          "cleared GUID persisted to settings.ini");
    shot("c2_reserved_cleared");

    /* ---- keyboard capture: ESC cancels (binding unchanged) ---- */
    click(keychip_x(0), row_y(MW_IN_ACT_B1));
    check(mw_input_capture_active() == 1, "KEY chip arms a keyboard capture");
    key(SDL_SCANCODE_ESCAPE);
    code = 123;
    check(mw_input_capture_result(&code) == -1, "ESC reports a cancelled capture");
    check(mw_input_capture_active() == 0, "cancel disarms the capture");
    check(g_st.key_map[0][MW_IN_ACT_B1] == SDL_SCANCODE_Z, "ESC left the binding alone");

    /* ---- rebind P1 B1 -> Q (persistence check) ---- */
    click(keychip_x(0), row_y(MW_IN_ACT_B1));
    check(mw_input_capture_active() == 1, "KEY chip arms again");
    shot("c3_waiting");
    key(SDL_SCANCODE_Q);
    check(g_st.key_map[0][MW_IN_ACT_B1] == SDL_SCANCODE_Q, "Q stored into key_map_p1 B1");
    ini = read_file(g_ini);
    check(ini.find("key_map_p1=") != std::string::npos, "settings.ini has a key_map_p1 line");
    shot("c3_bound");

    /* ---- pad capture with no controller attached: ESC is the way out ---- */
    click(padchip_x(1), row_y(MW_IN_ACT_DOWN));
    check(mw_input_capture_active() == 1, "PAD chip arms a pad capture");
    key(SDL_SCANCODE_ESCAPE);
    check(mw_input_capture_active() == 0, "ESC also disarms a pad capture");

    /* ---- tab chips ---- */
    click(tab_x(0), tab_y());
    check(mw_ui_tab(g_ui) == MW_UI_TAB_SETTINGS, "SETTINGS chip switches the overlay tab");
    shot("c4_settings");
    click(stab_x(1), tab_y());
    check(mw_ui_tab(g_ui) == MW_UI_TAB_CONTROLLERS, "CONTROLLERS chip switches back");

    /* ---- closing the panel mid-capture disarms (ruling 2) ---- */
    click(keychip_x(2), row_y(MW_IN_ACT_UP));
    check(mw_input_capture_active() == 1, "P3 UP KEY chip arms a capture");
    click(close_x(), close_y());
    check(g_open == 0, "CLOSE closed the panel");
    check(mw_input_capture_active() == 0, "closing the panel disarmed the capture");

    /* ---- reopening starts clean ---- */
    g_open = 1;
    pump();
    check(mw_input_capture_active() == 0, "reopened panel has no capture armed");
}

static void run_pads(void)
{
    std::string n1, g1;
    pump();
    click(stab_x(1), tab_y());
    check(mw_ui_tab(g_ui) == MW_UI_TAB_CONTROLLERS, "overlay switched to CONTROLLERS");
    pump();

    n1 = mw_input_pad_name(0);
    g1 = mw_input_pad_guid(0);
    check(!n1.empty(), "P1 shows the live pad name");
    check(mw_input_pad_name(1) && mw_input_pad_name(1)[0], "P2 shows the live pad name");
    check(g1.size() == 32, "P1 shows a 32-char live GUID");
    printf("P1 live pad '%s' guid %s\n", n1.c_str(), g1.c_str());
    printf("harness joystick instances: %d %d\n",
           (int)SDL_JoystickInstanceID(g_vj[0]), (int)SDL_JoystickInstanceID(g_vj[1]));
    shot("c5_live");

    /* Assignment chip: AUTO -> connected pad -> ... -> AUTO. */
    click(assign_x(0), assign_y());
    check(g_st.pad_guid[0][0] != 0, "PAD chip stores a connected pad's GUID");
    printf("stored pad_guid_p1 %s\n", g_st.pad_guid[0]);
    {
        int guard = 0;
        while (g_st.pad_guid[0][0] && guard < 5) {
            click(assign_x(0), assign_y());
            guard++;
        }
        check(g_st.pad_guid[0][0] == 0, "cycling past the last pad returns to AUTO");
    }

    /* Capture source guard (Task 4 Minor 9): P2 owns slot 1, so a button on
     * P1's pad must be ignored while P2's own pad answers. */
    click(padchip_x(1), row_y(MW_IN_ACT_DOWN));
    check(mw_input_capture_active() == 1, "P2 DOWN PAD chip arms a pad capture");
    shot("c6_capture");
    press_pad(0, SDL_CONTROLLER_BUTTON_A);
    check(mw_input_capture_active() == 1, "a button on another player's pad is ignored");
    check(g_st.pad_map[1][MW_IN_ACT_DOWN] == SDL_CONTROLLER_BUTTON_DPAD_DOWN,
          "the ignored press left P2 DOWN alone");
    press_pad(1, SDL_CONTROLLER_BUTTON_A);
    check(mw_input_capture_active() == 0, "the player's own pad answers the capture");
    check(g_st.pad_map[1][MW_IN_ACT_DOWN] == SDL_CONTROLLER_BUTTON_A,
          "the captured button landed in pad_map_p2 DOWN");
    shot("c7_padbound");
}

int main(int argc, char **argv)
{
    const char *mode = argc > 2 ? argv[2] : "panel";
    g_ini = argc > 1 ? argv[1] : nullptr;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_GAMECONTROLLER) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    mw_settings_load(&g_st, g_ini);
    if (!strcmp(mode, "pads")) attach_virtual_pads(2);
    mw_input_init();
    mw_input_load_maps(&g_st);

    if (!strcmp(mode, "rebind")) {
        /* needs the window only for the panel's geometry/focus behaviour */
    } else if (!strcmp(mode, "check")) {
        run_check();
        mw_input_shutdown();
        SDL_Quit();
        if (g_fail) printf("HARNESS FAILED (%d)\n", g_fail);
        else printf("ALL UI CHECKS PASSED\n");
        return g_fail ? 1 : 0;
    }

    g_win = SDL_CreateWindow("mw ui harness", 80, 80, 1280, 720, SDL_WINDOW_SHOWN);
    if (!g_win) {
        fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        return 1;
    }
    g_ren = SDL_CreateRenderer(g_win, -1, SDL_RENDERER_ACCELERATED);
    if (!g_ren) g_ren = SDL_CreateRenderer(g_win, -1, SDL_RENDERER_SOFTWARE);
    if (!g_ren) {
        fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError());
        return 1;
    }
    g_ui = mw_ui_create(g_win, g_ren, g_base, nullptr);
    if (!g_ui) {
        fprintf(stderr, "mw_ui_create failed\n");
        return 1;
    }
    /* Deliberately not focused: the SDL2 backend would otherwise paste the OS
     * cursor position over our synthetic one every frame. */
    pump();

    if (!strcmp(mode, "rebind"))
        run_rebind();
    else if (!strcmp(mode, "pads"))
        run_pads();
    else
        run_panel();

    mw_ui_destroy(g_ui);
    SDL_DestroyRenderer(g_ren);
    SDL_DestroyWindow(g_win);
    mw_input_shutdown();
    SDL_Quit();
    if (g_fail) printf("HARNESS FAILED (%d)\n", g_fail);
    else printf("ALL UI CHECKS PASSED\n");
    return g_fail ? 1 : 0;
}

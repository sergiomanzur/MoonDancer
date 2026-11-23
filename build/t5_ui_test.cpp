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
 *   pads   <ini>   virtual pads: live names/GUIDs, assignment, pad capture
 *   nav    <ini>   gamepad navigation: the pad walks the launcher card and the
 *                  overlay with no mouse event, and the mouse still behaves
 *                  exactly as before
 *   browse <ini>   in-UI archive browser: a real fixture tree on disk is
 *                  listed, directories enter, ".." walks back out, .zip files
 *                  and the current folder are selectable, other files are not,
 *                  and the D-pad + A reach a row with no mouse event
 *
 * Build (from the repo root, after `cmake --build build` refreshed the libs):
 *   g++ -std=c++17 -O2 -I src -I third_party/imgui -I third_party/imgui/backends
 *       -I third_party/stb
 *       -I third_party/sdl2_dl/SDL2-2.30.11/x86_64-w64-mingw32/include/SDL2
 *       build/t5_ui_test.cpp -x c src/input.c
 *       -L build -L third_party/sdl2_dl/SDL2-2.30.11/x86_64-w64-mingw32/lib
 *       -lmwui -limgui -lSDL2main -lSDL2 -o build/t5_ui_test.exe
 */
#define SDL_MAIN_HANDLED
#include <SDL.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#endif

#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"

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

/* nav mode: the launcher card instead of an overlay panel, and a frame loop that
 * can pretend a gamepad backend is present (see pump). */
static int g_launcher;
static int g_pad_capability;
static const char *g_archive;
static const char *g_status;
static int g_browse, g_start, g_open_settings, g_quit;

/* browse mode: the in-UI archive browser owns the frame, with no launcher and
 * no overlay behind it (which is how the host draws it too). */
static mw_file_browser g_fb;
static const char *g_fb_base;

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
    if (!g_pad_capability) {
        mw_ui_begin(g_ui);
    } else {
        /* mw_ui_begin() is exactly these three calls. The SDL2 backend clears
         * ImGuiBackendFlags_HasGamepad on every NewFrame when no controller is
         * open, and ImGui polls pad state rather than reading events, so a
         * virtual pad cannot drive it (SDL reports no button state for one).
         * The capability is therefore asserted between the backend frame and
         * ImGui::NewFrame(), and the pad keys are queued by hand with
         * AddKeyEvent, which is the same call the backend makes for a real pad. */
        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::GetIO().BackendFlags |= ImGuiBackendFlags_HasGamepad;
        ImGui::NewFrame();
    }
    if (g_fb.active) {
        /* The host suppresses the launcher while the browser is modal, so the
         * nav focus order never reaches a hidden button. */
        mw_ui_file_browser(g_ui, &g_fb, g_fb_base);
    } else if (g_launcher) {
        mw_ui_draw_launcher(g_ui, &g_st, g_archive, g_status, &g_browse,
                            &g_start, &g_open_settings, &g_quit, &g_changed);
    } else if (g_open) {
        if (mw_ui_tab(g_ui) == MW_UI_TAB_CONTROLLERS)
            mw_ui_draw_controllers(g_ui, &g_st, &g_open, &g_changed);
        else
            mw_ui_draw_settings(g_ui, &g_st, &g_open, 0, nullptr, nullptr, &g_changed);
    }
    /* The hosts' one-line disarm cover, repeated verbatim. */
    if (!(g_launcher || (g_open && mw_ui_tab(g_ui) == MW_UI_TAB_CONTROLLERS)))
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

/* ---- gamepad navigation (Task 4) ----
 * Each press is held for one frame and released on the next, so one tap is one
 * nav move or one nav activation. */
static void pad_tap(SDL_GameControllerButton button)
{
    static const struct { SDL_GameControllerButton btn; ImGuiKey key; } map[] = {
        { SDL_CONTROLLER_BUTTON_A,         ImGuiKey_GamepadFaceDown  },
        { SDL_CONTROLLER_BUTTON_DPAD_UP,   ImGuiKey_GamepadDpadUp    },
        { SDL_CONTROLLER_BUTTON_DPAD_DOWN, ImGuiKey_GamepadDpadDown  },
        { SDL_CONTROLLER_BUTTON_DPAD_LEFT, ImGuiKey_GamepadDpadLeft  },
        { SDL_CONTROLLER_BUTTON_DPAD_RIGHT, ImGuiKey_GamepadDpadRight },
    };
    ImGuiKey key = ImGuiKey_None;
    for (int i = 0; i < (int)(sizeof(map) / sizeof(map[0])); i++)
        if (map[i].btn == button) key = map[i].key;
    if (key == ImGuiKey_None) return;
    ImGui::GetIO().AddKeyEvent(key, true);
    pump();
    ImGui::GetIO().AddKeyEvent(key, false);
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

/* Value of a `key=value` line, or "" when absent. */
static std::string ini_value(const std::string &ini, const char *key)
{
    std::string out;
    std::string k = std::string(key) + "=";
    size_t at = ini.find(k);
    if (at == std::string::npos) return out;
    at += k.size();
    while (at < ini.size() && ini[at] != '\r' && ini[at] != '\n')
        out.push_back(ini[at++]);
    return out;
}

/* Comma-separated slot of a key_map_pN line.  settings.c persists the actions
 * as UP,DOWN,LEFT,RIGHT,B1,B2,B3. */
static int ini_slot(const std::string &line, int idx, int *found)
{
    size_t at = 0;
    int i = 0;
    *found = 0;
    while (at <= line.size()) {
        size_t comma = line.find(',', at);
        if (comma == std::string::npos) comma = line.size();
        if (i == idx) {
            *found = 1;
            return atoi(line.substr(at, comma - at).c_str());
        }
        i++;
        at = comma + 1;
    }
    return -1;
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
    {
        /* The line exists in every saved file, so assert the content: B1 (slot
         * 4 of UP,DOWN,LEFT,RIGHT,B1,B2,B3) must now hold Q and not the old Z.
         * settings.c writes key_map_p%d with a zero-based p, so player index 0
         * (P1) is key_map_p0. */
        int found = 0;
        int b1 = ini_slot(ini_value(ini, "key_map_p0"), MW_IN_ACT_B1, &found);
        printf("settings.ini key_map_p0 [B1] = %d (SDL_SCANCODE_Q = %d)\n",
               b1, (int)SDL_SCANCODE_Q);
        check(found && b1 == SDL_SCANCODE_Q,
              "settings.ini key_map_p0 holds Q in the B1 slot");
        check(b1 != SDL_SCANCODE_Z,
              "settings.ini key_map_p0 no longer holds Z in the B1 slot");
    }
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

/* Launcher card geometry at DisplaySize 1280x720 (s = 1):
 * panel (56,330) 500x326, px 84, py 354, field (84,382), field_w 332. */
static int l_center(int which, int axis)
{
    switch (which) {
    case 0: return axis ? 405 : 478;   /* BROWSE       428,382 100x46 */
    case 1: return axis ? 502 : 306;   /* START GAME    84,472 444x60 */
    case 2: return axis ? 570 : 164;   /* SETTINGS      84,548 160x44 */
    case 3: return axis ? 570 : 330;   /* MUSIC        260,548 140x44 */
    default: return axis ? 570 : 472;  /* QUIT         416,548 112x44 */
    }
}

/* ---- gamepad nav: the pad alone has to reach the same callbacks a click does,
 * and the mouse path has to be left untouched.  Focus order is ImGui's: from a
 * fresh launcher the first D-pad press lands on START GAME, and one more Down
 * reaches SETTINGS; in a fresh settings panel Up reaches the SETTINGS chip,
 * Right the CONTROLLERS chip, and two Downs walk volume -> SOUND. ---- */
static void run_nav(void)
{
    ImGuiIO &io = ImGui::GetIO();
    int sound0;

    /* The config contract from the brief: pad yes, keyboard arrows no. */
    check((io.ConfigFlags & ImGuiConfigFlags_NavEnableGamepad) != 0,
          "mw_ui_create enables gamepad navigation");
    check((io.ConfigFlags & ImGuiConfigFlags_NavEnableKeyboard) == 0,
          "keyboard arrows stay out of the UI (no keyboard nav)");

    g_archive = "moonwalker.zip";
    g_status = "READY";
    g_launcher = 1;
    g_pad_capability = 1;
    pump();
    shot("n0_launcher");

    /* ---- the pad: START GAME, then SETTINGS, no mouse at all ---- */
    pad_tap(SDL_CONTROLLER_BUTTON_DPAD_DOWN);
    check(g_start == 0 && g_browse == 0 && g_open_settings == 0 && g_quit == 0,
          "D-pad alone only moves the cursor, it never activates");
    shot("n1_cursor");

    pad_tap(SDL_CONTROLLER_BUTTON_A);
    check(g_start == 1, "D-pad + A starts the game without a mouse");
    check(g_browse == 0 && g_open_settings == 0 && g_quit == 0,
          "the pad activated START GAME and nothing else");
    g_start = 0;

    pad_tap(SDL_CONTROLLER_BUTTON_DPAD_DOWN);
    pad_tap(SDL_CONTROLLER_BUTTON_A);
    check(g_open_settings == 1, "D-pad + A opens SETTINGS");
    g_open_settings = 0;

    /* ---- the overlay's tab chips ---- */
    g_launcher = 0;
    g_open = 1;
    pump();
    pad_tap(SDL_CONTROLLER_BUTTON_DPAD_UP);
    check(mw_ui_tab(g_ui) == MW_UI_TAB_SETTINGS,
          "focus alone does not switch the overlay tab");
    pad_tap(SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
    pad_tap(SDL_CONTROLLER_BUTTON_A);
    check(mw_ui_tab(g_ui) == MW_UI_TAB_CONTROLLERS,
          "D-pad + A switches to the CONTROLLERS chip");
    shot("n2_controllers");
    pad_tap(SDL_CONTROLLER_BUTTON_DPAD_LEFT);
    pad_tap(SDL_CONTROLLER_BUTTON_A);
    check(mw_ui_tab(g_ui) == MW_UI_TAB_SETTINGS,
          "D-pad + A switches back to SETTINGS");

    /* ---- a toggle, twice: exactly one action per press ---- */
    sound0 = g_st.sound_on;
    pad_tap(SDL_CONTROLLER_BUTTON_DPAD_DOWN);
    pad_tap(SDL_CONTROLLER_BUTTON_DPAD_DOWN);
    pad_tap(SDL_CONTROLLER_BUTTON_A);
    check(g_st.sound_on == !sound0, "A on the SOUND toggle flips it exactly once");
    sound0 = g_st.sound_on;
    pad_tap(SDL_CONTROLLER_BUTTON_A);
    check(g_st.sound_on == !sound0, "a second A flips it back exactly once");
    shot("n3_pad_toggle");

    /* ---- mouse parity: same callbacks, and one click is still one action ---- */
    g_pad_capability = 0;
    g_open = 1;
    pump();
    sound0 = g_st.sound_on;
    click(640, 247);
    check(g_st.sound_on == !sound0, "mouse: SOUND flips exactly once per click");
    sound0 = g_st.sound_on;
    click(640, 247);
    check(g_st.sound_on == !sound0, "mouse: a second click flips it back once");

    g_launcher = 1;
    g_open = 0;
    g_start = g_browse = g_open_settings = g_quit = 0;
    pump();
    click(l_center(1, 0), l_center(1, 1));
    check(g_start == 1, "mouse: START GAME still starts the game");
    g_start = 0;
    click(l_center(0, 0), l_center(0, 1));
    check(g_browse == 1, "mouse: BROWSE still opens the browser");
    g_browse = 0;
    click(l_center(2, 0), l_center(2, 1));
    check(g_open_settings == 1, "mouse: SETTINGS still opens the overlay");
    g_open_settings = 0;
    shot("n4_mouse");
}

/* ---- in-UI archive browser (Task 5) ----
 * Fixture geometry mirrors mw_ui_file_browser at DisplaySize 1280x720 (s = 1):
 * panel (220,30) 840x660, px/py 258/58, list at y 200 with 38-px rows. */
static const char *FB_ROOT = "C:/Users/sergi/Homestead/code/moonwalkerRecomp/build/fb_fixture";
static const char *FB_NAV  = "C:/Users/sergi/Homestead/code/moonwalkerRecomp/build/fb_fixture/nav";
static const char *FB_MANY = "C:/Users/sergi/Homestead/code/moonwalkerRecomp/build/fb_fixture/many";
static const float FB_PW = 840.0f, FB_PH = 660.0f;
static const float FB_PANEL_X = (1280.0f - FB_PW) * 0.5f;
static const float FB_PANEL_Y = (720.0f - FB_PH) * 0.5f;
static const float FB_PX = FB_PANEL_X + 38.0f;
static const float FB_PY = FB_PANEL_Y + 28.0f;
static const float FB_ROW_H = 38.0f;
static const float FB_LIST_Y = FB_PY + 126.0f;
static const float FB_BTN_Y = FB_PANEL_Y + FB_PH - 38.0f - 48.0f;

static int fb_row_x(void) { return (int)(FB_PX + 30.0f); }
static int fb_row_y(int i) { return (int)(FB_LIST_Y + i * FB_ROW_H + FB_ROW_H * 0.5f); }
static int fb_btn_cy(void) { return (int)(FB_BTN_Y + 24.0f); }
static int fb_select_x(void) { return (int)(FB_PX + 120.0f); }
static int fb_close_x(void) { return (int)(FB_PANEL_X + FB_PW - 38.0f - 180.0f + 90.0f); }
static int fb_x_x(void) { return (int)(FB_PANEL_X + FB_PW - 54.0f + 16.0f); }
static int fb_x_y(void) { return (int)(FB_PANEL_Y + 20.0f + 16.0f); }

static int path_is(const char *p, const char *want)
{
    return p && want && strcmp(p, want) == 0;
}

static int path_ends(const char *p, const char *suffix)
{
    size_t n, m;
    if (!p || !suffix) return 0;
    n = strlen(p);
    m = strlen(suffix);
    return n >= m && strcmp(p + n - m, suffix) == 0;
}

static void make_dir(const char *p)
{
    if (!p || !*p) return;
#ifdef _WIN32
    _mkdir(p);
#else
    mkdir(p, 0755);
#endif
}

static void make_file(const char *p, const char *body)
{
    FILE *f = p ? fopen(p, "wb") : nullptr;
    if (!f) {
        printf("could not create fixture %s\n", p);
        return;
    }
    if (body) fwrite(body, 1, strlen(body), f);
    fclose(f);
}

/* build/fb_fixture/{a.zip, b.txt, sub/c.zip, nav/only.zip, many/p00..p15.zip} */
static void make_fixtures(void)
{
    std::string root = g_base;
    int i;
    char name[64];
    root += "build/fb_fixture";
    make_dir("C:/Users/sergi/Homestead/code/moonwalkerRecomp");
    make_dir("C:/Users/sergi/Homestead/code/moonwalkerRecomp/build");
    make_dir(root.c_str());
    make_dir((root + "/sub").c_str());
    make_dir((root + "/nav").c_str());
    make_dir((root + "/many").c_str());
    make_file((root + "/a.zip").c_str(), "PK\x05\x06fixture");
    make_file((root + "/b.txt").c_str(), "not a rom archive");
    make_file((root + "/sub/c.zip").c_str(), "PK\x05\x06fixture");
    make_file((root + "/nav/only.zip").c_str(), "PK\x05\x06fixture");
    /* More entries than the ten visible rows, so nav has to scroll. */
    for (i = 0; i < 16; i++) {
        snprintf(name, sizeof(name), "/many/p%02d.zip", i);
        make_file((root + name).c_str(), "PK\x05\x06fixture");
    }
}

static void fb_open(const char *dir)
{
    memset(&g_fb, 0, sizeof(g_fb));
    g_fb.active = 1;
    snprintf(g_fb.cwd, sizeof(g_fb.cwd), "%s", dir);
    g_fb_base = dir;
    pump();
}

static void run_browse(void)
{
    g_launcher = 0;
    g_open = 0;
    make_fixtures();

    fb_open(FB_ROOT);
    check(g_fb.active == 1, "browser draws with active = 1");
    check(g_fb.done == 0, "a freshly opened browser has no selection");
    check(path_is(g_fb.cwd, FB_ROOT), "the host-set cwd is kept");
    shot("f0_open");

    /* Rows at the fixture root, sorted: .. , many , nav , sub , a.zip , b.txt */
    click(fb_row_x(), fb_row_y(4));
    check(g_fb.done == 1, "clicking a.zip sets done");
    check(path_ends(g_fb.selection, "/a.zip"),
          "the selection is the absolute a.zip path");
    check(g_fb.active == 1, "choosing a target leaves the host to close the panel");
    shot("f1_selected");

    fb_open(FB_ROOT);
    click(fb_row_x(), fb_row_y(5));
    check(g_fb.done == 0, "a .txt row is listed but not selectable");
    check(path_is(g_fb.cwd, FB_ROOT), "a .txt click does not navigate");

    /* Directories enter, ".." walks back out. */
    fb_open(FB_ROOT);
    click(fb_row_x(), fb_row_y(3));
    check(path_ends(g_fb.cwd, "/sub"), "clicking sub enters the directory");
    check(g_fb.done == 0, "entering a directory is not a selection");
    shot("f2_sub");
    click(fb_row_x(), fb_row_y(0));
    check(path_is(g_fb.cwd, FB_ROOT), "the .. row walks back to the parent");
    check(g_fb.done == 0, "walking up is not a selection");

    /* The current folder is itself a target (roms/ is a valid archive path). */
    fb_open(FB_ROOT);
    click(fb_select_x(), fb_btn_cy());
    check(g_fb.done == 1, "SELECT THIS FOLDER selects the current directory");
    check(path_is(g_fb.selection, FB_ROOT),
          "the folder selection is the absolute cwd");

    fb_open(FB_ROOT);
    click(fb_close_x(), fb_btn_cy());
    check(g_fb.active == 0 && g_fb.done == 0,
          "CLOSE hides the browser without a selection");

    fb_open(FB_ROOT);
    click(fb_x_x(), fb_x_y());
    check(g_fb.active == 0 && g_fb.done == 0, "the X hides the browser too");

    /* Gamepad: nav reaches a row and A activates it, no mouse event at all.
     * nav/ holds .. and only.zip, so one Down from the nav init lands on the
     * zip (the same first-move rule the launcher nav mode relies on). */
    g_pad_capability = 1;
    fb_open(FB_NAV);
    shot("f3_nav_open");
    pad_tap(SDL_CONTROLLER_BUTTON_DPAD_DOWN);
    check(ImGui::IsAnyItemFocused(), "the D-pad puts a focus ring on a browser row");
    shot("f4_nav_focus");
    pad_tap(SDL_CONTROLLER_BUTTON_A);
    check(g_fb.done == 1 && path_ends(g_fb.selection, "/only.zip"),
          "D-pad + A selects a zip with no mouse event");

    /* More entries than the ten visible rows: the nav cursor has to scroll the
     * list and still reach files past the first viewport.  Up to the top
     * first, so the Down count does not depend on the previous cursor. */
    fb_open(FB_MANY);
    for (int i = 0; i < 3; i++) pad_tap(SDL_CONTROLLER_BUTTON_DPAD_UP);
    for (int i = 0; i < 15; i++) pad_tap(SDL_CONTROLLER_BUTTON_DPAD_DOWN);
    shot("f5_nav_scrolled");
    pad_tap(SDL_CONTROLLER_BUTTON_A);
    {
        int idx = -1;
        if (g_fb.done && strlen(g_fb.selection) > 6)
            idx = atoi(g_fb.selection + strlen(g_fb.selection) - 6);
        printf("long-list nav landed on p%d.zip\n", idx);
        check(g_fb.done == 1 && idx >= 10 && path_ends(g_fb.selection, ".zip"),
              "nav scrolls a long folder past the first visible page");
    }
    g_pad_capability = 0;
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
    else if (!strcmp(mode, "nav"))
        run_nav();
    else if (!strcmp(mode, "browse"))
        run_browse();
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

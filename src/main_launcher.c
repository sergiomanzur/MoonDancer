/*
 * Moonwalker launcher - SDL2 + Dear ImGui front end.
 */
#include <SDL.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#include <commdlg.h>
#include <dwmapi.h>
#include <SDL_syswm.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include "settings.h"
#include "host_port.h"
#include "input.h"
#include "ui.h"
#include "launcher_music.h"

static int file_exists(const char *p)
{
    if (!p || !*p) return 0;
    FILE *f = fopen(p, "rb");
    if (!f) return 0;
    fclose(f);
    return 1;
}

static int dir_exists(const char *p)
{
    if (!p || !*p) return 0;
#ifdef _WIN32
    DWORD a = GetFileAttributesA(p);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
#else
    struct stat st;
    return stat(p, &st) == 0 && S_ISDIR(st.st_mode);
#endif
}

static const char *archive_status(const char *path)
{
    if (!path || !*path) return "SELECT YOUR mwalk.zip";
    if (dir_exists(path)) return "READY // ROM FOLDER SELECTED";
    if (file_exists(path)) return "READY // ARCHIVE SELECTED";
    return "NOT FOUND // CHECK THE ARCHIVE PATH";
}

static void find_default_zip(const char *base, char *out, int cap)
{
    out[0] = 0;
    char candidates[4][2048];
    snprintf(candidates[0], sizeof(candidates[0]), "%smwalk.zip", base);
    snprintf(candidates[1], sizeof(candidates[1]), "%s../mwalk.zip", base);
    snprintf(candidates[2], sizeof(candidates[2]), "mwalk.zip");
    snprintf(candidates[3], sizeof(candidates[3]), "%sroms", base);
    for (int i = 0; i < 4; i++) {
        if (dir_exists(candidates[i]) || file_exists(candidates[i])) {
            snprintf(out, (size_t)cap, "%s", candidates[i]);
            return;
        }
    }
}

#ifdef _WIN32
static HWND window_hwnd(SDL_Window *window)
{
    SDL_SysWMinfo info;
    SDL_VERSION(&info.version);
    if (SDL_GetWindowWMInfo(window, &info) && info.subsystem == SDL_SYSWM_WINDOWS)
        return info.info.win.window;
    return NULL;
}

static void pick_archive(SDL_Window *window, char *out, int cap)
{
    wchar_t file[MAX_PATH] = L"";
    static const wchar_t filter[] =
        L"Moonwalker ROM archive (*.zip)\0*.zip\0All files (*.*)\0*.*\0\0";
    OPENFILENAMEW ofn;
    memset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = window_hwnd(window);
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = L"Select the Moonwalker ROM archive";
    ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameW(&ofn)) {
        WideCharToMultiByte(CP_UTF8, 0, file, -1, out, cap, NULL, NULL);
    }
}
#else
static void pick_archive(SDL_Window *window, char *out, int cap)
{
    (void)window;
    (void)out;
    (void)cap;
}
#endif

static void last_archive_file(char *buf, int cap)
{
    buf[0] = 0;
    char *pref = SDL_GetPrefPath("MoonwalkerRecomp", "Moonwalker");
    if (pref) {
        snprintf(buf, (size_t)cap, "%slast_archive.txt", pref);
        SDL_free(pref);
    }
}

static void load_last_archive(char *out, int cap)
{
    out[0] = 0;
    char path[2048];
    last_archive_file(path, (int)sizeof(path));
    if (!path[0]) return;
    FILE *f = fopen(path, "r");
    if (!f) return;
    if (fgets(out, cap, f)) {
        size_t n = strlen(out);
        while (n && (out[n - 1] == '\n' || out[n - 1] == '\r')) out[--n] = 0;
    }
    fclose(f);
}

static void save_last_archive(const char *archive)
{
    if (!archive || !*archive) return;
    char path[2048];
    last_archive_file(path, (int)sizeof(path));
    if (!path[0]) return;
    FILE *f = fopen(path, "w");
    if (!f) return;
    fputs(archive, f);
    fputc('\n', f);
    fclose(f);
}

static int spawn_game(const char *base, const char *archive, SDL_Window *window)
{
    int is_dir = dir_exists(archive);
#ifdef _WIN32
    char exe[2048];
    snprintf(exe, sizeof(exe), "%smwalk.exe", base);
    if (!file_exists(exe)) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Moonwalker",
                                 "mwalk.exe was not found next to the launcher.", NULL);
        return -1;
    }
    char cmd[4096];
    snprintf(cmd, sizeof(cmd), "\"%s\" %s \"%s\"", exe,
             is_dir ? "--rom-dir" : "--zip", archive);

    wchar_t wexe[2048], wcmd[4096], wdir[2048];
    MultiByteToWideChar(CP_UTF8, 0, exe, -1, wexe, 2048);
    MultiByteToWideChar(CP_UTF8, 0, cmd, -1, wcmd, 4096);
    MultiByteToWideChar(CP_UTF8, 0, base, -1, wdir, 2048);

    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    memset(&pi, 0, sizeof(pi));
    if (!CreateProcessW(wexe, wcmd, NULL, NULL, FALSE, CREATE_NO_WINDOW,
                        NULL, wdir, &si, &pi)) {
        char msg[512];
        snprintf(msg, sizeof(msg),
                 "Could not launch mwalk.exe (error %lu).", (unsigned long)GetLastError());
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Moonwalker", msg, NULL);
        return -1;
    }
    /* This process is still the foreground window right now, so grant the
     * child the right to take foreground BEFORE hiding.  Hiding first
     * makes the launcher non-foreground, and Windows then denies the new
     * game window keyboard focus: the game looks fine but the keyboard is
     * completely dead (gamepads are polled, so they keep working). */
    AllowSetForegroundWindow(pi.dwProcessId);
    if (window) SDL_HideWindow(window);
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return (int)code;
#else
    char exe[2048];
    snprintf(exe, sizeof(exe), "%smwalk", base);
    if (!file_exists(exe)) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Moonwalker",
                                 "mwalk was not found next to the launcher.", NULL);
        return -1;
    }
    pid_t pid = fork();
    if (pid == 0) {
        if (chdir(base) != 0) _exit(127);
        if (is_dir)
            execl(exe, exe, "--rom-dir", archive, (char *)NULL);
        else
            execl(exe, exe, "--zip", archive, (char *)NULL);
        _exit(127);
    }
    if (pid < 0) return -1;
    int status = 0;
    waitpid(pid, &status, 0);
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
#endif
}

int main(int argc, char **argv)
{
    int start_settings = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--settings")) start_settings = 1;
    }
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_GAMECONTROLLER) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
    SDL_Window *window = SDL_CreateWindow("Moonwalker // Launcher",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 720,
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!window) {
        fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        return 1;
    }
    SDL_Renderer *renderer = SDL_CreateRenderer(window, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer)
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    if (!renderer) {
        fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError());
        return 1;
    }
    SDL_SetRenderDrawColor(renderer, 2, 3, 8, 255);
#ifdef _WIN32
    {
        HWND hwnd = window_hwnd(window);
        BOOL dark = TRUE;
        if (hwnd) {
            if (FAILED(DwmSetWindowAttribute(hwnd, 20, &dark, sizeof(dark))))
                DwmSetWindowAttribute(hwnd, 19, &dark, sizeof(dark));
        }
    }
#endif

    char base[2048];
    if (!mw_host_base_dir(base, (int)sizeof(base))) {
        fprintf(stderr, "Could not determine the launcher asset directory.\n");
        return 1;
    }

    char settings_path[2048];
    mw_settings_path(settings_path, (int)sizeof(settings_path));
    mw_settings settings;
    mw_settings_load(&settings, settings_path);
    /* Live device view in the launcher: the controller subsystem is up (see
     * SDL_Init above) and the module mirrors the stored assignment/maps. */
    mw_input_init();
    mw_input_load_maps(&settings);

    char background[2048];
    snprintf(background, sizeof(background), "%sassets/launcher_background.jfif", base);
    if (!file_exists(background))
        snprintf(background, sizeof(background), "%slauncher_background.jfif", base);

    mw_ui *ui = mw_ui_create(window, renderer, base, background);
    if (!ui) {
        fprintf(stderr, "Could not initialize the launcher UI.\n");
        return 1;
    }

    char archive[2048];
    find_default_zip(base, archive, (int)sizeof(archive));
    if (!archive[0])
        load_last_archive(archive, (int)sizeof(archive));

    int running = 1;
    int settings_open = start_settings;
    int settings_dirty = 0;
    int music_playing = 0;
    Uint32 last_save = 0;

    if (settings.music_on)
        music_playing = mw_music_start();

    while (running) {
        int browse = 0, start = 0, open_settings = 0, quit = 0;
        int reset_game = 0, quit_game = 0, changed = 0;

        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            /* The CONTROLLERS panel owns the key that answers its capture, so
             * the launcher hotkeys (F1/ESC) stay out of the way while armed. */
            int capture_busy = mw_input_capture_active();
            mw_ui_process_event(ui, &event);
            mw_input_process_event(&event);
            switch (event.type) {
            case SDL_QUIT:
                running = 0;
                break;
            case SDL_DROPFILE:
                if (event.drop.file) {
                    snprintf(archive, sizeof(archive), "%s", event.drop.file);
                    save_last_archive(archive);
                    SDL_free(event.drop.file);
                }
                break;
            case SDL_KEYDOWN:
                if (event.key.repeat) break;
                if (capture_busy) break;
                if (event.key.keysym.scancode == SDL_SCANCODE_F1) {
                    settings_open = !settings_open;
                } else if (event.key.keysym.scancode == SDL_SCANCODE_ESCAPE) {
                    if (settings_open) settings_open = 0;
                    else running = 0;
                }
                break;
            default:
                break;
            }
        }

        mw_ui_begin(ui);
        if (!settings_open)
            mw_ui_draw_launcher(ui, &settings, archive, archive_status(archive),
                                &browse, &start, &open_settings, &quit, &changed);
        if (settings_open) {
            if (mw_ui_tab(ui) == MW_UI_TAB_CONTROLLERS)
                mw_ui_draw_controllers(ui, &settings, &settings_open, &changed);
            else
                mw_ui_draw_settings(ui, &settings, &settings_open, 0, NULL, NULL, &changed);
        }
        /* Same ruling as the game overlay: what the panel does not own is
         * disarmed, so no capture survives a close, tab switch or START. */
        if (!(settings_open && mw_ui_tab(ui) == MW_UI_TAB_CONTROLLERS))
            mw_input_cancel_capture();
        SDL_RenderClear(renderer);
        mw_ui_draw_background(ui);
        mw_ui_end(ui);
        SDL_RenderPresent(renderer);

        if (settings_open) {
            browse = start = quit = 0;
        }
        if (changed) settings_dirty = 1;
        /* The launcher's MUSIC toggle writes straight through to settings;
         * follow it immediately instead of waiting for the next run. */
        if (settings.music_on && !music_playing)
            music_playing = mw_music_start();
        else if (!settings.music_on && music_playing) {
            mw_music_stop();
            music_playing = 0;
        }
        if (open_settings) settings_open = 1;
        if (browse && !settings_open) {
            char before[2048];
            snprintf(before, sizeof(before), "%s", archive);
            pick_archive(window, archive, (int)sizeof(archive));
            if (strcmp(before, archive) != 0)
                save_last_archive(archive);
        }
        if (start && !settings_open) {
            if (!archive[0]) {
                pick_archive(window, archive, (int)sizeof(archive));
            } else if (!file_exists(archive) && !dir_exists(archive)) {
                SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Moonwalker",
                                         "The selected archive does not exist.", window);
            } else {
                if (settings_dirty) {
                    mw_settings_save(&settings, settings_path);
                    settings_dirty = 0;
                }
                save_last_archive(archive);
                /* Release the pads before handing the window to mwalk.exe so
                 * the game opens them as the only consumer, then rebuild the
                 * device view once control comes back.  The launcher music
                 * releases its audio device for the same reason. */
                mw_music_stop();
                music_playing = 0;
                mw_input_shutdown();
                int rc = spawn_game(base, archive, window);
                SDL_ShowWindow(window);
                SDL_RaiseWindow(window);
                SDL_SetWindowInputFocus(window);
                mw_settings_load(&settings, settings_path);
                mw_input_init();
                mw_input_load_maps(&settings);
                if (settings.music_on)
                    music_playing = mw_music_start();
                if (rc != 0 && rc != -1) {
                    char msg[256];
                    snprintf(msg, sizeof(msg), "mwalk.exe exited with code %d.", rc);
                    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_WARNING, "Moonwalker", msg, window);
                }
            }
        }
        if (quit && !settings_open) running = 0;

        Uint32 now = SDL_GetTicks();
        if (settings_dirty && now - last_save > 250) {
            mw_settings_save(&settings, settings_path);
            settings_dirty = 0;
            last_save = now;
        }
        if (changed)
            last_save = now;

        SDL_Delay(1);
    }

    if (settings_dirty)
        mw_settings_save(&settings, settings_path);
    mw_music_stop();
    mw_input_shutdown();
    mw_ui_destroy(ui);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}

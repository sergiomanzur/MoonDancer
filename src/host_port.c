/*
 * Moonwalker native port - host portability layer.
 *
 * Every path the game writes or reads from the install tree goes through
 * these functions so the Android and SteamOS ports can swap the policy in one
 * place.  Each entry point is written as a single per-platform branch;
 * Windows and Linux share the portable-folder layout for now.
 */
#include "host_port.h"

#include <stdio.h>
#include <string.h>

#include <SDL.h>

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

/* Read-only asset dir.  Packaged builds keep fonts/ and the launcher
 * background beside the executable, which is what SDL_GetBasePath reports.
 * Returns the path with SDL's trailing separator. */
int mw_host_base_dir(char *buf, int cap)
{
    char *base;
    int n;

    if (!buf || cap <= 0) return 0;
    buf[0] = '\0';

    base = SDL_GetBasePath();
    if (!base) return 0;
    n = snprintf(buf, (size_t)cap, "%s", base);
    SDL_free(base);
    if (n < 0 || n >= cap) {
        buf[0] = '\0';
        return 0;
    }
    return 1;
}

/* Writable state (NVRAM, screenshots).  Same folder as the assets today, so a
 * packaged install behaves exactly as it did when these paths were CWD
 * relative and the launcher started the game in the exe directory.  The Linux
 * `portable` marker check and the Android preference directory slot in here. */
int mw_host_data_dir(char *buf, int cap)
{
    return mw_host_base_dir(buf, cap);
}

/* No APK asset bundle to unpack on desktop hosts: fonts and the background are
 * already files next to the executable. */
void mw_host_extract_assets(void)
{
}

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

#ifdef _WIN32
static HWND window_hwnd(SDL_Window *window)
{
    SDL_SysWMinfo info;
    SDL_VERSION(&info.version);
    if (SDL_GetWindowWMInfo(window, &info) && info.subsystem == SDL_SYSWM_WINDOWS)
        return info.info.win.window;
    return NULL;
}
#endif

/* Native archive picker.  Windows keeps the shipped GetOpenFileNameW dialog;
 * the other hosts decline (0) so the launcher can open its in-UI browser. */
int mw_host_browse_archive(SDL_Window *window, char *out, int cap)
{
#ifdef _WIN32
    wchar_t file[MAX_PATH] = L"";
    static const wchar_t filter[] =
        L"Moonwalker ROM archive (*.zip)\0*.zip\0All files (*.*)\0*.*\0\0";
    OPENFILENAMEW ofn;
    if (!out || cap <= 0) return 0;
    memset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = window_hwnd(window);
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = L"Select the Moonwalker ROM archive";
    ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameW(&ofn)) {
        /* Claim filled only when the UTF-8 conversion actually wrote out: a
         * short cap leaves out untouched, and the launcher must then fall back
         * to its in-UI browser rather than trust a stale path. */
        int n = WideCharToMultiByte(CP_UTF8, 0, file, -1, out, cap, NULL, NULL);
        return n > 0;
    }
    return 0;
#else
    (void)window;
    (void)out;
    (void)cap;
    return 0;
#endif
}

/* Start the game executable beside the launcher and wait for it.  The Win32
 * path hands keyboard focus across the process boundary; the POSIX path just
 * inherits the terminal and the window manager's focus policy. */
int mw_host_spawn_game(const char *base, const char *archive, SDL_Window *self)
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
    if (self) SDL_HideWindow(self);
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return (int)code;
#else
    (void)self;
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

/* Matches the window frame to the launcher's near-black UI so the title bar
 * does not glare above it.  Only Windows offers the documented attribute. */
void mw_host_dark_titlebar(SDL_Window *window)
{
#ifdef _WIN32
    HWND hwnd = window_hwnd(window);
    BOOL dark = TRUE;
    if (hwnd) {
        if (FAILED(DwmSetWindowAttribute(hwnd, 20, &dark, sizeof(dark))))
            DwmSetWindowAttribute(hwnd, 19, &dark, sizeof(dark));
    }
#else
    (void)window;
#endif
}

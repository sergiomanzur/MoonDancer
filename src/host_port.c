/*
 * Moonwalker native port - host portability layer.
 *
 * Every path the game writes or reads from the install tree goes through
 * these functions so the Android and SteamOS ports can swap the policy in one
 * place.  Each entry point is written as a single per-platform branch;
 * Windows and Linux share the portable-folder layout, Android uses the app's
 * internal files directory.
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

#ifdef __ANDROID__
#include "host_android.h"

/* Android has no writable install tree: SDL_GetBasePath() is unsupported
 * there (it returns NULL), and the APK's assets/ are read-only.  Both the
 * asset dir and the writable data dir are the app's internal files directory;
 * mw_host_extract_assets() copies the files the UI loads into it on first
 * run.  Returns the path with a trailing separator. */
static int pref_dir(char *buf, int cap)
{
    char *pref;
    int n;

    if (!buf || cap <= 0) return 0;
    buf[0] = '\0';
    pref = SDL_GetPrefPath("MoonwalkerRecomp", "Moonwalker");
    if (!pref) return 0;
    n = snprintf(buf, (size_t)cap, "%s", pref);
    SDL_free(pref);
    if (n < 0 || n >= cap) {
        buf[0] = '\0';
        return 0;
    }
    return 1;
}
#endif

/* Read-only asset dir.  Packaged desktop builds keep fonts/ and the launcher
 * background beside the executable, which is what SDL_GetBasePath reports.
 * Android uses the app's files directory, where the APK assets are extracted.
 * Returns the path with SDL's trailing separator. */
int mw_host_base_dir(char *buf, int cap)
{
#ifdef __ANDROID__
    return pref_dir(buf, cap);
#else
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
#endif
}

/* Writable state (NVRAM, screenshots).  Desktop keeps the same folder as the
 * assets, so a packaged install behaves exactly as it did when these paths were
 * CWD relative and the launcher started the game in the exe directory: the
 * forward below is SDL_GetBasePath() and nothing else, on Windows and Linux
 * alike.  Android is the one platform that slots a different directory in
 * there, through pref_dir(). */
int mw_host_data_dir(char *buf, int cap)
{
    return mw_host_base_dir(buf, cap);
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

#ifdef __ANDROID__
/* Everything the UI loads at runtime: copied out of the APK into the app's
 * files directory (mw_host_base_dir) so ImGui's plain-path font loader and
 * stb_image can read them with ordinary file I/O. */
static const char *const k_apk_assets[] = {
    "fonts/Audiowide-Regular.ttf",
    "fonts/Rajdhani-Bold.ttf",
    "fonts/Rajdhani-Regular.ttf",
    "fonts/Rajdhani-SemiBold.ttf",
    "fonts/OFL-Audiowide.txt",
    "fonts/OFL-Rajdhani.txt",
    "launcher_background.jfif",
};

/* APK asset -> file.  SDL_RWFromFile reads APK assets through SDL's JNI
 * bridge; a plain fopen() cannot see them at all. */
static int copy_asset(const char *src, const char *dst)
{
    SDL_RWops *in = SDL_RWFromFile(src, "rb");
    SDL_RWops *out;
    char buf[16384];
    size_t n;
    int ok = 1;

    if (!in) return 0;
    out = SDL_RWFromFile(dst, "wb");
    if (!out) {
        SDL_RWclose(in);
        return 0;
    }
    while ((n = SDL_RWread(in, buf, 1, sizeof(buf))) > 0) {
        if (SDL_RWwrite(out, buf, 1, n) != n) {
            ok = 0;
            break;
        }
    }
    SDL_RWclose(in);
    if (SDL_RWclose(out) != 0) ok = 0;
    return ok;
}

/* Version stamp written into the extraction marker.  Bump it whenever the
 * payload list above changes (a new font, a replaced background): an install
 * whose marker holds a different stamp re-extracts on the next launch, which
 * is how an updated APK populates files on a device that already has data.
 * The stamp is only written after a clean pass, so a failed copy is retried
 * on every launch too. */
static const char k_assets_version[] = "1";

/* Is the marker present and stamped with the current payload version?
 * Whitespace is ignored so a hand-edited or older file cannot wedge the
 * upgrade path (Task 8 wrote a bare "1"). */
static int marker_matches(const char *path)
{
    char buf[64];
    size_t n, len;
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = '\0';
    len = strlen(buf);
    while (len && (buf[len - 1] == '\n' || buf[len - 1] == '\r' ||
                   buf[len - 1] == ' ' || buf[len - 1] == '\t'))
        buf[--len] = '\0';
    return strcmp(buf, k_assets_version) == 0;
}

/* First run: copy the APK's assets/ payload into the files directory.  A
 * version-stamped marker file in that directory skips the copy on later runs
 * (delete it, or the app's data, to force a re-extract).  assets/mwalk.zip is
 * only present in private builds; when it is there it is unpacked to
 * files/mwalk.zip, which is exactly where the launcher's find_default_zip()
 * looks.
 *
 * Returns 1 when the payload is in place and 0 when something could not be
 * copied: the launcher surfaces 0 on its status line (a silent failure used to
 * leave the UI on the fallback font with no way for the player to tell). */
int mw_host_extract_assets(void)
{
    char base[2048], dst[3072], path[3072];
    char marker[3072];
    size_t i;
    FILE *m;
    int ok = 1;

    if (!mw_host_base_dir(base, (int)sizeof(base))) {
        SDL_Log("Asset extraction: no writable files directory");
        return 0;
    }
    snprintf(marker, sizeof(marker), "%sassets/.extracted", base);
    if (marker_matches(marker)) return 1;

    SDL_Log("Asset extraction: populating %sassets (stamp v%s)",
            base, k_assets_version);

    snprintf(path, sizeof(path), "%sassets", base);
    (void)mkdir(path, 0755);
    snprintf(path, sizeof(path), "%sassets/fonts", base);
    (void)mkdir(path, 0755);

    for (i = 0; i < sizeof(k_apk_assets) / sizeof(k_apk_assets[0]); i++) {
        snprintf(dst, sizeof(dst), "%sassets/%s", base, k_apk_assets[i]);
        if (!copy_asset(k_apk_assets[i], dst)) {
            fprintf(stderr, "Could not extract APK asset %s\n", k_apk_assets[i]);
            SDL_Log("Asset extraction: could not copy %s", k_apk_assets[i]);
            ok = 0;
        }
    }

    /* Optional: a ROM archive bundled for private builds (never the public
     * artifact).  Do not clobber a file the player already placed there. */
    {
        SDL_RWops *rom = SDL_RWFromFile("mwalk.zip", "rb");
        if (rom) {
            SDL_RWclose(rom);
            snprintf(dst, sizeof(dst), "%smwalk.zip", base);
            if (!file_exists(dst) && !copy_asset("mwalk.zip", dst)) {
                fprintf(stderr, "Could not extract the bundled mwalk.zip\n");
                SDL_Log("Asset extraction: could not copy the bundled mwalk.zip");
                ok = 0;
            }
        }
    }

    /* No marker on a partial copy: the next launch retries instead of leaving
     * the UI permanently on the fallback font. */
    if (!ok) {
        SDL_Log("Asset extraction incomplete; retrying on the next launch");
        return 0;
    }
    m = fopen(marker, "wb");
    if (m) {
        fputs(k_assets_version, m);
        fputc('\n', m);
        fclose(m);
    }
    SDL_Log("Asset extraction complete");
    return 1;
}
#else
/* No APK asset bundle to unpack on desktop hosts: fonts and the background are
 * already files next to the executable, so there is nothing that can fail. */
int mw_host_extract_assets(void)
{
    return 1;
}
#endif

/* Native archive picker.  The return value reports whether the browse flow was
 * handled, not whether a file was picked: 1 = a native picker was shown (out is
 * filled on success and left untouched on cancel); 0 = this host has no native
 * picker, so the launcher opens its in-UI browser.  Windows always handles the
 * flow, which is what keeps it native-only (a cancelled dialog does nothing).
 * Android always handles it too, but with no path yet: the SAF picker runs in
 * the activity and the copied archive arrives later as an SDL_DROPFILE event. */
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
        /* Convert before touching out: on a conversion failure, or when the
         * caller's buffer is too small, out stays untouched so the launcher
         * adopts nothing and never trusts a half-written path. */
        char converted[2048];
        int n = WideCharToMultiByte(CP_UTF8, 0, file, -1, converted,
                                    (int)sizeof(converted), NULL, NULL);
        if (n > 0 && n <= cap)
            memcpy(out, converted, (size_t)n);
    }
    return 1;
#elif defined(__ANDROID__)
    (void)window;
    (void)out;
    (void)cap;
    /* The picker lives in the activity (SAF), so out is deliberately untouched:
     * the launcher must not treat the archive as chosen until the DROPFILE for
     * the copied files/mwalk.zip arrives. */
    if (mw_android_open_rom_picker()) return 1;
    /* No JNI bridge / no activity: fall back to the in-UI browser so BROWSE
     * still does something on a device whose picker cannot be reached. */
    SDL_Log("Browse: SAF picker unavailable, opening the in-UI browser");
    return 0;
#else
    (void)window;
    (void)out;
    (void)cap;
    return 0;
#endif
}

/* Start the game and wait for it.  Windows: separate mwalk.exe process, with
 * focus handed across the process boundary.  Linux: fork + exec of "mwalk"
 * beside the launcher.  Android: there is no second executable, so
 * mw_game_main() runs in this process on the activity's SDL thread; the
 * caller has already released the launcher window (one window per activity).
 * Returns the game's exit code. */
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
#elif defined(__ANDROID__)
    /* Same argv shape the desktop launcher passes to mwalk.exe: a zip archive
     * or an extracted ROM directory.  Runs synchronously on SDLActivity's SDL
     * thread, which is where mw_game_main must create its window and audio. */
    {
        char *argv[4];
        int argc = 0;
        (void)base;
        (void)self;
        argv[argc++] = (char *)"mwalk";
        argv[argc++] = (char *)(is_dir ? "--rom-dir" : "--zip");
        argv[argc++] = (char *)archive;
        argv[argc] = NULL;
        return mw_game_main(argc, argv);
    }
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

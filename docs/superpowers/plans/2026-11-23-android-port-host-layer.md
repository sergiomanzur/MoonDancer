# Moon Dancer — Host Portability Layer + Android Port Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ship the full Moon Dancer 0.9.0b feature set on Android (one APK, launcher + game in one process, on-screen touch controls) with 1:1 parity, by first extracting a small host-portability layer from the Windows launcher.

**Architecture:** A new `src/host_port.c/.h` absorbs the launcher's Win32 blocks (browse/spawn/base+data dirs) and gains Linux and Android implementations; the game front end is refactored to a callable `mw_game_main()` so Android runs launcher + game in one process; a new `touch_controls` module maps `SDL_FINGER*` events to the existing per-player input model; an SDL2 `SDLActivity` Gradle project wraps the whole front end.

**Tech Stack:** C11/C++17, CMake, SDL2 2.30.11, ImGui 1.91.8 (vendored), Android NDK 26.2.11394342, Gradle + JDK 17, adb (Retroid Pocket 6, Android 13, arm64-v8a).

**Spec:** `docs/superpowers/specs/2026-11-23-crossplatform-android-steamos-design.md` — the plan argues from it; executors read both.

## Global Constraints

- **Windows regression suite green after every task**: `cmake --build build` (all targets, no new warnings), `.\build\mwalk_headless.exe --rom-dir roms --selftest 1000000` → passed, `.\build\mwalk_headless.exe --rom-dir roms --frames 30000 --play --display 1` → `recover=0 REC_MISS=0 illegal=0` (PowerShell stderr logs are UTF-16LE; decode BOM-aware).
- **Windows user-visible behavior unchanged** (mouse flow, spawn, focus). Only additive: gamepad menu navigation.
- **No ROM data in git or any public artifact.** `mwalk.zip`, `roms/`, private-bundle outputs stay gitignored.
- No changes to emulator core semantics, `src/generated_game.c`, or the cheat address table (`docs/cheats-addresses.md`).
- No new third-party dependencies except vendored SDL2 2.30.11 source for the Android build.
- Harnesses compile with the exact command documented in each harness file's header (`build/test_settings.c` pattern).
- Commit per task; never push (the orchestrator pushes at milestones).
- Environment facts: adb at `%LOCALAPPDATA%\Android\Sdk\platform-tools\adb.exe`; NDK 26.2.11394342; JDK `scoop\apps\zulu17-jdk\current`; WSL distro `Ubuntu-24.04` (root via `wsl -d Ubuntu-24.04 -u root`); test device = Retroid Pocket 6 (Android 13, arm64-v8a).

## Review Focus

Failure modes the spec implies but task tests don't fully exercise; each pinned to its owning task's checklist:

1. **Android Back exits the app instead of opening the overlay** — the trap hint must be set before `SDL_Init`. Task 8 asserts `SDL_HINT_ANDROID_TRAP_BACK_BUTTON` is set and Back toggles the overlay (on-device, Task 9 checklist step).
2. **Touch controls hidden with no way back** — the MENU corner button must render even when `touch_on=0`. Task 7's harness asserts the menu hit rect is evaluated whenever the game runs on `__ANDROID__`, regardless of `touch_on`.
3. **Old `settings.ini` without `touch_on`/`touch_mode` keys** loads defaults — Task 6's old-file harness assertion (same pattern as 0.9.0b fields).
4. **Floating joystick ghost diagonals** — a finger resting at the stick edge must not register UP+RIGHT simultaneously; Task 6's mapping harness asserts the dead-zone: offset `< 0.18` of the stick radius yields zero direction bits.
5. **In-process re-entry leak** — QUIT GAME → START GAME again in one process must re-open audio/pads cleanly; Task 8's device steps run the game twice in one activity session.
6. **NVRAM silently not written on Android** — Task 9 asserts a score survives an app restart (`adb shell am force-stop` + relaunch).

---

### Task 1: Host portability layer — base/data dirs and game path policy

**Files:**
- Create: `src/host_port.h`, `src/host_port.c`
- Modify: `src/main_sdl.c:317,382-386,553-559,729-730` (NVRAM + screenshot paths), `src/main_launcher.c` (base dir only)
- Test: `build/test_hostport.c` (new harness, compile command in its header, same pattern as `build/test_settings.c`)
- Modify: `CMakeLists.txt` — `src/host_port.c` into both `mwalk` and `mwalk_launcher` targets

**Interfaces:**
- Produces (in `src/host_port.h`, consumed by every later task):

```c
/* Writable dir for NVRAM + screenshots, trailing separator, 1 on success.
 * Windows/Linux: SDL_GetBasePath() — packaged behavior identical to today.
 * Linux override: a `portable` marker dir beside the binary switches back on.
 * Android: SDL_GetPrefPath("MoonwalkerRecomp","Moonwalker"). */
int mw_host_data_dir(char *buf, int cap);
/* Read-only asset dir (fonts/, launcher background). Same platform map. */
int mw_host_base_dir(char *buf, int cap);
/* Android only: one-time copy of APK assets (fonts, background, optional
 * bundled mwalk.zip) into the data dir. No-op elsewhere. */
void mw_host_extract_assets(void);
```

- [ ] **Step 1: Write the failing harness** — `build/test_hostport.c`: assert `mw_host_data_dir` returns 1 with a non-empty path that exists and is writable (create+delete a temp file under it); assert `mw_host_base_dir` returns 1.
- [ ] **Step 2: Run it to verify it fails** (host_port.h doesn't exist yet).
- [ ] **Step 3: Implement `host_port.{c,h}`** — Windows branch first: both functions return `SDL_GetBasePath()`. In `main_sdl.c`, join the NVRAM path (`nvram/mwalk/nvram` default and the `--nvram FILE` override stays absolute-unchanged) and `moonwalker.bmp` onto `mw_host_data_dir()`; the `MW_MKDIR` calls use the joined root. Keep the packaged-folder behavior byte-identical (data dir == exe dir).
- [ ] **Step 4: Run harness + standing regression suite** — all green.
- [ ] **Step 5: Commit** — `feat: host data/base dirs; NVRAM and screenshots leave CWD-relative paths`

### Task 2: In-process game entry — `mw_game_main()`

**Files:**
- Modify: `src/main_sdl.c:313-334,338-341,728` (entry, SDL init/quit), `src/host_port.h` (declare entry)
- Test: standing regression + live Windows run (identical to pre-refactor, including the launcher focus handoff test from commit 98ec4f9's verification)

**Interfaces:**
- Produces: `int mw_game_main(int argc, char **argv);` — the entire current `main()` body; behavior identical when called as a desktop exe. On `__ANDROID__`: skips `SDL_Init`/`SDL_Quit` (the launcher process owns SDL) — guarded so the desktop path is untouched.

- [ ] **Step 1: Rename `main()` → `mw_game_main()`; add the guarded wrapper** — `#ifndef __ANDROID__` `int main(int argc, char **argv) { return mw_game_main(argc, argv); }` `#endif`; wrap the `SDL_Init`/`SDL_Quit` pair in the same guard.
- [ ] **Step 2: Verify** — full Windows regression suite + a live `build\mwalk.exe --zip mwalk.zip` run boots and plays; launcher → START → game still lands keyboard focus (the e2e mechanism test in `build/focus_mech_test.ps1` may be reused).
- [ ] **Step 3: Commit** — `refactor: game front end exposes mw_game_main for in-process hosting`

### Task 3: Launcher host glue + WSL Linux build

**Files:**
- Modify: `src/host_port.c/.h` (move the Win32 bodies in), `src/main_launcher.c:72-107,147-215` (drop `windows.h` includes; call host glue), `CMakeLists.txt:123-138` (link `comdlg32`/`dwmapi` only on WIN32 — already conditional)
- Test: Windows live launcher (START spawns game, focus lands), WSL Ubuntu-24.04 build + headless run

**Interfaces:**
- Consumes: Task 1's `mw_host_*` declarations.
- Produces (host_port.h):

```c
/* 1 = a native dialog filled out[cap] (Windows GetOpenFileNameW, verbatim
 * current behavior). 0 = host declined: caller opens the in-UI browser. */
int mw_host_browse_archive(SDL_Window *window, char *out, int cap);
/* Launch the game; blocks until it exits, returns its exit code.
 * Windows: current CreateProcessW + AllowSetForegroundWindow, hidden-after-
 * grant, verbatim. Linux: fork + execl of "mwalk" beside the launcher.
 * Android: in-process mw_game_main (added in Task 8's build config). */
int mw_host_spawn_game(const char *base, const char *archive, SDL_Window *self);
```

- [ ] **Step 1: Move, don't rewrite** — `pick_archive`/`spawn_game`/`window_hwnd` bodies relocate into `host_port.c` with identical logic; `main_launcher.c` keeps only the calls. The existing POSIX fork/waitpid branch already present in `spawn_game` moves with it.
- [ ] **Step 2: Windows verification** — launcher live: BROWSE opens the native dialog; START spawns with keyboard focus (regression suite too).
- [ ] **Step 3: WSL build** — `wsl -d Ubuntu-24.04 -u root -- apt-get install -y libsdl2-dev build-essential cmake` (once), then from the repo: `wsl -d Ubuntu-24.04 -- bash -lc "cmake -S . -B build-linux -DCMAKE_BUILD_TYPE=Release && cmake --build build-linux -j"` → `mwalk`, `mwalk_launcher`, `mwalk_headless` build `-Wall` clean; `./build-linux/mwalk_headless --rom-dir roms --selftest 1000000` → passed. (If /mnt/c I/O makes this crawl, copying the tree into `~/` inside WSL is acceptable — the built binaries are throwaway verification, not artifacts.)
- [ ] **Step 4: Commit** — `feat: launcher Win32 glue moves behind host_port; Linux build verified in WSL`

### Task 4: Gamepad menu navigation in the shared widgets

**Files:**
- Modify: `src/ui.cpp` (`neon_button` ~:495-527, tab chips ~:407-411, toggles/sliders helpers, launcher window flags ~:858-863, `mw_ui_create` ~:588) and `src/ui.h` (no signature changes)
- Test: `build/t5_ui_test.cpp` extended (compile pattern in its header)

**Interfaces:**
- Consumes: ImGui 1.91.8 (`IsItemActivated`, `IsItemFocused`).
- Produces: every widget helper activates on `IsItemClicked() || IsItemActivated()` and highlights on `IsItemHovered() || IsItemFocused()`; `mw_ui_create` sets `io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad`; launcher/overlay windows drop `ImGuiWindowFlags_NoNavFocus`; a nav-focused chip gets the same highlight color as hover. Mouse behavior byte-identical.

- [ ] **Step 1: Extend the UI harness with a nav test** — create `mw_ui` offscreen, feed `ImGui::GetIO().AddKeyEvent(ImGuiKey_GamepadFaceDown, ...)` plus `AddKeyAnalogEvent(ImGuiKey_GamepadLStickRight/Down, ...)` to focus the START chip, assert the START flag arrives without any mouse event; assert a mouse-only run still sets the same flag (parity).
- [ ] **Step 2: Run to verify it fails** (nav flag not enabled yet → focus never lands).
- [ ] **Step 3: Implement the helper changes** — one pass over every widget helper in `ui.cpp`; no new files.
- [ ] **Step 4: Harness + regression green; live launcher check** — with a gamepad attached, D-pad + A navigates launcher tabs and START; with the mouse, everything behaves exactly as before.
- [ ] **Step 5: Commit** — `feat: shared UI widgets are gamepad-navigable (launcher + overlay)`

### Task 5: In-UI file browser

**Files:**
- Modify: `src/ui.h` (new type + draw call), `src/ui.cpp` (browser panel), `src/main_launcher.c:308-338` (BROWSE wiring + `SDL_DROPFILE` stays)
- Test: `build/t5_ui_test.cpp` extension with a fixture directory tree

**Interfaces:**
- Produces (`ui.h`):

```c
typedef struct {
    int active;            /* host sets 1 to open; panel clears when done */
    int done;             /* 1 when selection[] is valid */
    char cwd[2048];
    char selection[2048]; /* absolute path of the chosen .zip or dir */
} mw_file_browser;
/* Draws over the launcher while fb->active. Directories enterable,
 * ".." up, ZIP files and roms/ dirs selectable, sorted, nav + touch
 * friendly (full-row hit targets). */
void mw_ui_file_browser(mw_ui *ui, mw_file_browser *fb, const char *base);
```

- Launcher BROWSE flow: `if (!mw_host_browse_archive(window, archive, sizeof(archive))) { fb.active = 1; fb.cwd = base; }`; when `fb.done`, store selection + `save_last_archive` as the native path does today. Windows unchanged (native dialog); Linux/Android use the in-UI browser.

- [ ] **Step 1: Failing harness** — fixture tree `build/fb_fixture/{a.zip, b.txt, sub/c.zip}`; open browser at fixture root via harness; simulate a click (or nav activation) on `a.zip`; assert `fb.done` and `selection` ends in `a.zip`; navigate into `sub` and back out via `..`.
- [ ] **Step 2: Run to verify it fails.**
- [ ] **Step 3: Implement the panel** — plain-C path scan (`opendir`/`FindFirstFile` via existing `dir_exists`-style guards or SDL's `SDL_GetPrefPath`-free helpers already in the launcher), highlight = existing neon style; no new dependencies.
- [ ] **Step 4: Harness + regression green; live Windows launcher** — BROWSE still opens the native dialog.
- [ ] **Step 5: Commit** — `feat: in-UI archive browser (controller/touch navigable)`

### Task 6: Touch core — settings, mapping module, input feeding

**Files:**
- Create: `src/touch_controls.h`, `src/touch_controls.c`
- Modify: `src/settings.h:14-36` (two fields), `src/settings.c` (load/save/defaults), `src/input.h:87-97` + `src/input.c` (touch feeding), `src/main_sdl.c` (FINGER event pump), `CMakeLists.txt` (touch module into `mwalk`)
- Test: `build/test_settings.c` + `build/test_input.c` extensions (documented compile pattern), new `build/test_touch.c`

**Interfaces:**
- Settings: `int touch_on; /* 0/1, default 1 */`, `int touch_mode; /* 0 D-pad, 1 joystick, default 1 */` — keys `touch_on=`, `touch_mode=`.
- Input (`input.h`): `void mw_input_set_touch_action(int action, int down);` (player 1, `MW_IN_ACT_*` slots, held+latch semantics identical to `mw_input_set_keyboard`) and `void mw_input_set_touch_service(int start, int coin);` (P1 start/coin bits 0x10/0x01).
- Touch module (`touch_controls.h`):

```c
#define MW_TOUCH_DPAD   0
#define MW_TOUCH_STICK  1
typedef struct {
    uint8_t actions;   /* MW_IN_* bits for P1 this frame */
    int     menu;      /* MENU corner button tapped (latch) */
    int     start, coin;
    float   stick_x, stick_y; /* -1..1, for drawing */
    int     stick_finger;     /* finger id anchoring the stick, -1 none */
} mw_touch_state;
void mw_touch_set(int enabled, int mode);
/* Normalized window coords [0,1]. Buttons and dpad are stateless hits;
 * the joystick anchors where the finger lands. Dead-zone: offsets below
 * 0.18 of the stick radius produce no direction bits. */
void mw_touch_finger(int down, float x, float y, int finger_id, mw_touch_state *out);
/* Geometry constants exported for the drawing pass and the tests. */
#define MW_TOUCH_MENU_HIT(x,y,w,h) /* rect test helper, defined in header */
```

- [ ] **Step 1: Extend `test_settings.c`** — defaults (touch_on=1, touch_mode=1), round-trip, old-file-without-keys → defaults. **Extend `test_input.c`** — touch action + service merge asserts. **Write `test_touch.c`** — dpad tap → UP bit; stick lands center-left → LEFT bit only (dead-zone assert: tiny offset → zero bits); MENU hit rect fires `menu=1` even when `enabled=0`; coin/start latches.
- [ ] **Step 2: Run all three; verify the new tests fail.**
- [ ] **Step 3: Implement settings fields, input feeding, and the touch module.** In `main_sdl.c`, pump `SDL_FINGERDOWN/UP/MOTION` into `mw_touch_finger`, then per frame feed `mw_input_set_touch_action`/`_service` from the state — desktop Windows never sees finger events, so the desktop path is dead code until a touch screen appears.
- [ ] **Step 4: All harnesses + regression green.**
- [ ] **Step 5: Commit** — `feat: touch input core - mapping, settings, input-module feeding`

### Task 7: Touch rendering, icons, toggles, MENU wiring

**Files:**
- Modify: `src/ui.h` + `src/ui.cpp` (draw call + CONTROLLERS panel rows), `src/main_sdl.c` (MENU → overlay toggle, draw hook), `src/main_launcher.c` + `src/ui.cpp` launcher row (TOUCH ON/OFF chip beside MUSIC)
- Test: `build/t5_ui_test.cpp` extension; live Windows visual check via the F12 screenshot path

**Interfaces:**
- Produces (`ui.h`): `void mw_ui_draw_touch(mw_ui *ui, const mw_touch_state *t, int enabled, int mode);` — draws the D-pad cross / floating stick, three round action buttons (procedural neon icons: **fist** = punch, **rising figure** = jump, **sparkles** = magic), coin-slot and "1P START" chips, and the always-on **MENU** pause-bars corner button (rendered and hit even when `enabled=0` on `__ANDROID__`).
- Produces (`ui.h`, spec's "capability flags, not forks"): `#define MW_UI_CAP_NO_RESOLUTION 1` and `void mw_ui_set_caps(mw_ui *ui, int caps);` — the Android host passes `MW_UI_CAP_NO_RESOLUTION` so the SETTINGS panel hides its RESOLUTION preset row (window presets are meaningless there); FULLSCREEN stays and maps to Android immersive via SDL's normal fullscreen path on that platform. Desktop hosts pass 0 — panels unchanged.
- Overlay CONTROLLERS panel: TOUCH ON/OFF toggle + DPAD/JOYSTICK mode chips → `settings` + dirty. `main_sdl.c`: `t.menu` toggles `settings_open` exactly like F1.

- [ ] **Step 1: Failing harness** — offscreen `mw_ui`, force `__ANDROID__`-shaped state (enabled=0): assert `mw_ui_draw_touch` still draws the menu button (capture the draw list: vertex count > 0 near the top-right corner region); enabled=1: vertex count > 0 in left-half and right-half control regions.
- [ ] **Step 2: Run to verify it fails.**
- [ ] **Step 3: Implement the draw pass and wiring.** Icons are ImGui draw-list geometry in the existing neon palette — no image assets.
- [ ] **Step 4: Harness + regression green; live visual check** — F12 screenshots while pushing synthetic finger events (small debug-only `SDL_PushEvent` loop or touch-capable window if available) show the controls.
- [ ] **Step 5: Commit** — `feat: on-screen touch controls with semantic icons, D-pad/joystick modes, MENU button`

### Task 8: Android Gradle project + first APK on device

**Files:**
- Create: `tools/fetch_sdl2_source.py` (downloads SDL2 2.30.11 source into `third_party/SDL2`, gitignored, mirroring the existing sdl2_dl pattern), `android/` (Gradle project from SDL2's `android-project`: `app/build.gradle.kts` or `.gradle`, `AndroidManifest.xml`, `MoonDancerActivity.java`, `res/` launcher icon, `app/src/main/jni/CMakeLists.txt` building `libmain.so` = `mwsystem18` + `mwui` + `host_port` + `touch_controls` + `launcher_music` + `input.c` + `main_launcher.c` + `main_sdl.c` via `add_subdirectory` of the SDL2 source with its Android CMake config)
- Modify: `src/main_launcher.c` (Android boot: `SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON, "1")` before `SDL_Init`; `mw_host_extract_assets()`; `mw_ui_set_caps(ui, MW_UI_CAP_NO_RESOLUTION)`; Back KEYDOWN → QUIT in launcher; **window handoff around the in-process spawn — SDL2 allows one window per Android activity, so the launcher must `mw_ui_destroy` + `SDL_DestroyRenderer` + `SDL_DestroyWindow` before `mw_host_spawn_game()` and rebuild them after return, the exact structural mirror of the Windows hide/show dance**), `src/host_port.c` (Android impls: `mw_host_spawn_game` = `mw_game_main` call; browse returns 0; dirs = pref path), `.gitignore`
- Test: on-device via adb

**Interfaces:**
- Consumes: `mw_game_main` (Task 2), `host_port` (Tasks 1/3), nav + browser (Tasks 4/5) — the launcher runs unmodified as the app's `SDL_main`.
- Produces: `android/` project; APK id `com.moondancer.recomp`, label "Moon Dancer", `minSdk 26`, `targetSdk 34`, `screenOrientation="landscape"`, `debuggable` for this phase; assets: `fonts/` + `launcher_background.jfif` + (optionally, private builds only) `mwalk.zip`.

- [ ] **Step 1: Fetch script + project skeleton** — `python tools/fetch_sdl2_source.py` → `third_party/SDL2`; copy SDL2's `android-project` as the Gradle base, retarget `externalNativeBuild` at our CMake file with `ANDROID_ABI=arm64-v8a`.
- [ ] **Step 2: Build** — `gradlew assembleDebug` with `JAVA_HOME` = zulu17 and `ANDROID_HOME` = `%LOCALAPPDATA%\Android\Sdk` → `android/app/build/outputs/apk/debug/app-debug.apk`.
- [ ] **Step 3: Install + smoke on device** — `adb install -r` → launch → screencap: launcher renders with fonts/background (asset extraction worked); music plays (logcat audio thread alive); ROM present: `adb push mwalk.zip /data/local/tmp/ && adb shell run-as com.moondancer.recomp cp /data/local/tmp/mwalk.zip files/mwalk.zip` → START GAME runs in-process; built-in RP6 gamepad controls the game (SDL gamecontroller); BACK from the game opens the overlay; QUIT GAME returns to the launcher; **second START runs the game again cleanly** (re-entry check).
- [ ] **Step 4: Windows regression still green** (Android files don't touch desktop targets, but prove it).
- [ ] **Step 5: Commit** — `feat: Android port - SDLActivity Gradle project, in-process launcher+game, first on-device run`

### Task 9: SAF ROM import + on-device functional checklist

**Files:**
- Modify: `android/app/src/main/java/.../MoonDancerActivity.java` (SAF picker: `ACTION_OPEN_DOCUMENT` application/zip → copy stream to `files/mwalk.zip`), `src/main_launcher.c` (BROWSE on Android → JNI call into the activity, then re-run `find_default_zip`; JNI env via `SDL_AndroidGetJNIEnv()`), `src/host_port.c` (browse hook)
- Test: on-device checklist (this task's definition of done)

**Interfaces:**
- Produces: `mw_host_browse_archive` on Android triggers the system picker and returns 0 (the launcher's own re-scan finds the copied zip via `mw_host_base_dir`).

- [ ] **Step 1: Implement the Java + JNI path.**
- [ ] **Step 2: On-device checklist, each item verified by screencap/logcat and recorded in the task report:** SAF import from Downloads works and START follows; touch controls render + control P1 (dpad and both modes via the CONTROLLERS panel); touch hidden → MENU reopens overlay → TOUCH toggle restores them; coin/start by touch; cheats panel reachable + QUIT GAME returns to launcher; NVRAM: insert coins, force-stop, relaunch → credits persist; scores persist across a game restart.
- [ ] **Step 3: Windows regression green.**
- [ ] **Step 4: Commit** — `feat: SAF ROM import + full on-device parity checklist`

### Task 10: Device tuning, private bundle, public APK, docs

**Files:**
- Modify: `src/touch_controls.h` (geometry constants only, tuned values), `android/` (icon polish, release signing config placeholder), `README.md` (Android section), `.gitignore`
- Create: `package_android_private.ps1` (copies `mwalk.zip` into `android/app/src/main/assets/`, assembles the APK, tags the filename `MoonDancer-…-private.apk`, reminds never to publish)
- Test: on-device final pass; public APK is ROM-free

**Interfaces:**
- Consumes: everything above.
- Produces: tuned touch geometry; `MoonDancer-v0.9.0b-android.apk` (ROM-free) and the private bundle command; README "Android" section (import flow, controls, both modes, Back/MENU behavior, private bundle warning).

- [ ] **Step 1: Tuning pass on device** — hit-box sizes, stick radius, button spacing per the user's hands-on feedback loop; screencap evidence in the report.
- [ ] **Step 2: Public + private packaging both build; public APK contains no ROM data** (unzip the APK, assert no `mwalk.zip`, no `epr-*`/`mpr-*`/`317-0159.key` entries).
- [ ] **Step 3: README section + regression suite green.**
- [ ] **Step 4: Commit** — `feat: Android v1 complete - tuned touch, packaging, docs`

---

**Out of scope (SteamOS AppImage):** separate plan, `docs/superpowers/plans/2026-11-24-steamos-appimage.md`, written after this plan lands — it consumes the verified `host_port` layer and the WSL pipeline from Task 3.

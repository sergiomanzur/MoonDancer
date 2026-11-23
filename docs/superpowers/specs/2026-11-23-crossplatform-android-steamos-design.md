# Moon Dancer cross-platform design — Android port + SteamOS AppImage

Date: 2026-11-23
Status: awaiting user approval (builder check complete; resolutions folded in)
Precedes: implementation plan (superpowers:writing-plans)

## Intent and why

Ship Moon Dancer 0.9.0b's full feature set — launcher (with synthwave music),
all settings/video/cheats/controls panels, widescreen, three-player gamepad
support — on **Android** (APK) and **SteamOS/Steam Deck** (AppImage), with
**1:1 feature parity with the Windows build**. The why: one maintainable
codebase; every feature exists once and works everywhere.

Delivery order (decided with the user): **Android first** — the user owns an
Android device, so every build lands on testable hardware. **SteamOS second** —
verified on this Windows host via WSL2 (build + GUI smoke runs under WSLg) and
handed over Deck-ready; real-Deck verification happens when the user gets
hardware.

## Goals

- **Android APK**: full launcher + game in one app; on-screen touch controls
  with icons that show exactly what each control does (punch, jump, magic,
  coin, start); D-pad **and** analog joystick modes selectable in SETTINGS;
  touch controls hideable/showable from the launcher menu AND the in-game F1
  overlay; full gamepad support; system file picker imports `mwalk.zip` into
  app-private storage.
- **SteamOS AppImage**: launcher + game, UI fully controller-navigable (Deck
  game mode has no mouse), 16:10 mode fits the Deck's 1280x800 panel, runs as a
  non-Steam game.
- **ROM delivery (decided)**: two flows —
  1. *Public*: all artifacts ROM-free; the launcher gains a
     controller-navigable ImGui file browser (also what Deck game mode uses).
  2. *Private*: a one-command bundle script bakes the user's own `mwalk.zip`
     into a personal AppImage/APK that is never published.
- Windows keeps its shipped behavior and the standing regression suite stays
  green after every task (see Non-goals for the one permitted exception:
  additive gamepad menu navigation).

## Non-goals / anti-goals

- No per-platform forks of UI or feature code — platform differences live only
  in thin host glue and small capability flags.
- No ROM data in any public artifact (Moonwalker is © Sega; same rule the
  GitHub release already follows).
- No Steam store integration, no Android TV mode, no cloud saves, no
  translation/localization, no portrait orientation in this release.
- Do not churn the shipped Windows architecture. *One permitted, additive
  exception (my call, see Approach 6)*: shared UI widgets become gamepad-
  navigable, so Windows gains optional pad-driven menus. Mouse behavior and
  the headless regression suite are unchanged; nothing Windows users do today
  behaves differently.

## Constraints

- Host is Windows 11. Android verification = the user's device (adb access
  question pending with the user; verification loop set accordingly). Linux/
  AppImage verification = WSL2 + WSLg.
- Existing stack: C11/C++17, CMake 3.20+, SDL2 2.30.x, SDL_Renderer-based
  ImGui backends (`imgui_impl_sdl2` + `imgui_impl_sdlrenderer2`), settings in
  `%APPDATA%` on Windows via SDL pref path.
- Launcher Win32 blocks to abstract: browse (comdlg32), game spawn
  (`CreateProcessW`), foreground grant (`AllowSetForegroundWindow`). The game
  itself only has the DWM/focus block, already `#ifdef _WIN32`.
- No touch handling exists anywhere today (`SDL_FINGER*` unhandled) — the
  virtual-control layer is genuinely new code.
- Android specifics that shape the design (builder-check findings):
  `SDL_GetBasePath()` returns NULL on Android and `SDL_GetPrefPath()` is the
  internal files dir; the app runs in-process (no launcher chdir), so CWD is
  unusable for NVRAM/screenshots/settings; the Android Back button terminates
  the activity unless `SDL_HINT_ANDROID_TRAP_BACK_BUTTON` is set; the current
  UI is 16:9-designed and every widget is a mouse-only `InvisibleButton`.
- Android: minSdk 26 (Android 8+), phones and tablets, **landscape locked**
  (arcade game; portrait unsupported v1 — my call); NDK + SDL2's
  android-project template (SDL2 source vendored for the NDK build).
- SteamOS 3 is Arch-based: build the AppImage against an older glibc than the
  target (WSL Ubuntu is fine); AppImage bundles everything else via linuxdeploy.

## Approaches considered

1. **Launcher/game process model**
   - (a) Two-process spawn on every platform — max symmetry, but child-process
     games on Android are fragile (app lifecycle, input routing, debugging).
   - (b) Single-process rewrite on all platforms including Windows — cleanest
     conceptually, but churns the shipped Windows binary for zero user-visible
     gain. Rejected.
   - **(c) Hybrid — PICKED**: Windows/Linux keep the existing spawn; Android
     runs launcher and game in one process behind a single `mw_host_*`
     interface. The launcher UI code is shared verbatim; only the host glue
     differs per platform.
2. **Android host & UI**
   - (a) SDL2 `SDLActivity` Gradle project wrapping the existing C front end —
     the standard SDL path; the whole ImGui launcher/settings/cheats UI rides
     along untouched. **PICKED.**
   - (b) Native Kotlin/Compose UI — abandons the entire existing UI; violates
     the parity requirement. Dropped.
3. **Touch input**
   - (a) Raw `SDL_FINGER*` events → virtual controls drawn with the existing
     ImGui background draw lists (procedural neon icons in code, matching the
     synthwave theme, no binary asset files), feeding the same per-player
     `mw_input` state pads/keys already use. **PICKED.**
   - (b) Native Android overlay views — fights the SDL surface, breaks parity,
     double input pipeline. Dropped.
4. **ROM import**
   - Shared ImGui file browser module (used by Linux/SteamOS and the desktop
     fallback) + Android system file picker (SAF → app files dir) + the private
     bundle script. **PICKED** (the user chose "both flows").
5. **SteamOS packaging**
   - (a) AppImage via linuxdeploy assembled in WSL. **PICKED** (user asked for
     AppImage).
   - (b) Flatpak — the officially blessed SteamOS route but much heavier
     scaffolding. Dropped; the AppImage also works on any Linux desktop.
6. **Menu navigation without a mouse** (builder check: plain
   `NavEnableGamepad` is insufficient — every widget in `ui.cpp` is a
   hand-drawn `InvisibleButton` acting on `IsItemClicked()`, and the launcher
   window sets `ImGuiWindowFlags_NoNavFocus`)
   - (a) Rework the shared widget helpers (`neon_button`, tabs, toggles,
     sliders) to be nav-aware: highlight on `IsItemHovered()` **or**
     nav-focus, act on `IsItemClicked()` **or** nav-activate; drop
     `NoNavFocus` on the launcher window; enable `NavEnableGamepad`
     everywhere. One implementation in shared code; Windows gains pad menus
     additively. **PICKED.**
   - (b) A nav-specific layer bolted around the shared UI — a platform-ish UI
     fork; violates the parity rule. Dropped.

## Parts of the how (decided, by phase)

### Phase A — shared host layer (both platforms build on this)

- New `src/host_port.c/.h`: `mw_host_spawn_game()` (Win: existing
  CreateProcessW + AllowSetForegroundWindow; Linux: fork/exec; Android:
  in-process call), `mw_host_browse_archive()`, `mw_host_focus_game()`,
  `mw_host_data_dir()` (see path policy below). The launcher includes this
  instead of `windows.h`.
- **Host path policy** (builder check: NVRAM/screenshot paths are CWD-relative
  today and silently break on both new targets):
  - Windows: unchanged — portable `nvram/` + `settings.ini` next to the exe.
  - Android: everything (NVRAM, screenshots, imported zip, extracted assets,
    settings) under `SDL_GetPrefPath()` (app files dir).
  - Linux/AppImage: `$XDG_DATA_HOME`/`~/.local/share/MoonwalkerRecomp` (NVRAM,
    screenshots, settings via existing SDL pref path); a `portable/` marker
    folder next to the AppImage switches to binary-relative storage for
    USB-stick users. Public AppImage default = XDG.
  - One `mw_host_data_dir()` call replaces the CWD-relative writes; the game
    core and loader are untouched.
- Nav-aware widget rework (Approach 6a) across launcher + F1 overlay.
- ImGui file browser (controller + touch navigable) as a `ui.cpp` panel.
- Shared overlay additions so every keyboard-only function has a reachable
  button on touch/Deck (builder check): the in-game overlay gains **RESET**
  (was F5-only) and **QUIT GAME** (was Esc-only) buttons — additive on all
  platforms, including Windows.
- Settings additions, same schema pattern as 0.9.0b:
  - `touch_on` (default: on for Android builds, off on desktop),
  - `touch_mode` (0 = D-pad, 1 = floating joystick; default joystick — the
    native-Android convention),
  - touch visibility toggles in the launcher menu row and the in-game
    CONTROLLERS panel.
  - Touch drives player 1; gamepads cover P2/P3 (one screen, one player's
    thumbs — stated as a call, correctable).
- CMake hardened for Linux (`-Wall`, no GNU extensions, system SDL2 path
  already present).

### Phase B — Android

- `android/` Gradle project from SDL2's android-project template: JNI bridge
  into the existing front end, vendored SDL2 source, NDK pinned in the plan.
- SAF import: system picker copies `mwalk.zip` into the app files dir; the
  launcher's default-archive search finds it there (base path = files dir).
- **Asset delivery** (builder check: `SDL_GetBasePath()` is NULL on Android):
  fonts + launcher background ship in the APK's `assets/`; on first run the
  host extracts them into the files dir, and `mw_ui_create` /
  `find_default_zip` use the files dir as base — identical code paths after a
  one-time copy. Window icon is an Android adaptive launcher icon instead.
- **In-game menu access without a keyboard** (builder check): a small
  always-visible **MENU button** (pause-bars icon, top-right corner, present
  even when touch controls are hidden — it is how you get them back) opens the
  same overlay F1 opens, pausing the game. The Android Back button is trapped
  (`SDL_HINT_ANDROID_TRAP_BACK_BUTTON=1`): in game it toggles the overlay; in
  the launcher it is QUIT. With the RESET/QUIT buttons from Phase A, every
  keyboard-only function (coin/start/pause/reset/cheats/quit) is touch-
  reachable; only F12 screenshots stay desktop-only.
- Touch layer (as approach 3): left side = D-pad cross **or** floating joystick
  (thumb lands, stick appears), right side = three round buttons with
  procedural neon icons — **fist** (punch), **upward figure** (jump),
  **sparkles** (magic) — plus a coin-slot icon and a "1P START" arcade button.
  Sizes/tolerances tuned on the user's device.
- **Capability flags, not forks** (builder check): a small platform-capability
  set in `mw_ui` decides what each panel shows — RESOLUTION presets hidden on
  Android; FULLSCREEN maps to Android immersive mode there; everything else
  identical everywhere.
- **Landscape lock** via manifest (`screenOrientation=landscape`,
  `screenSize|orientation` config changes handled without restart).
- APK packaging + a private-bundle script that embeds the user's `mwalk.zip`;
  public APK stays ROM-free.
- Verification: standing regression suite; `-Wall` clean NDK build; `adb
  install` + on-device checklist — loop shape pending the user's adb answer.

### Phase C — SteamOS AppImage

- `package_linux.sh`: WSL build → linuxdeploy AppImage containing launcher +
  game (+ fonts + background; the music engine is code-side, no assets).
- Deck polish: 1280x800 is a 16:10 panel (mode exists), SDL raw gamepad sees
  the Deck controls without Steam Input, non-Steam shortcut instructions in
  the README; launcher fully pad-navigable via Approach 6.
- Private variant of the script bakes in `mwalk.zip`.
- Verification: WSL2 `--selftest`/regression runs of the Linux binary; WSLg GUI
  smoke run (launcher navigation by gamepad/keyboard, game start, settings
  save in the XDG dir); ROM-free public artifact.

## What is left to the builder

- NDK version pin and SDL2-source vendoring strategy for Android.
- Exact SAF/JNI plumbing and Gradle packaging details.
- Icon glyph drawing polish (draw-list geometry) — tuned on device.
- linuxdeploy version and AppImage glibc-compat base.
- SteamOS on-hardware checklist to run when the user gets a Deck.

## Copyright rule (applies to both phases)

Public artifacts carry no ROM data and no ROM-derived binaries — on Android the
ROM is imported at runtime; for the personal bundle builds, the user's own
`mwalk.zip` stays in their private copy. Same principle as the 0.9.0b release.

# Launcher, Video Options, Cheats, and 3-Player Controllers — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ship the presentation layer the verified core deserves: a current launcher with sound-by-default, a video options panel (resolution presets, source-space shader presets, scanline intensity), a global cheat menu, and three simultaneous gamepads with full remapping.

**Architecture:** All work lives in the SDL front end and shared UI/settings code — `src/settings.h/.c` (schema), a new `src/input.h/.c` (keyboard+pad merge), `src/main_sdl.c` (audio wiring, shader pass, cheat apply), `src/ui.cpp/.h` (three new panels), `src/main_launcher.c` (rebuild + device view). The emulator core, `generated_game.c`, and the FD1094 path are untouched; cheats are per-frame work RAM writes from the front end only.

**Tech Stack:** C/C++17, SDL2 (video, audio, gamecontroller), Dear ImGui (existing `mwui`), MinGW + Ninja via the existing `build` dir. No new dependencies.

**Spec:** `docs/superpowers/specs/2026-11-19-launcher-video-cheats-controls-design.md` (includes the five builder-check resolutions; the partner approved global cheats on 2026-11-23).

## Global Constraints

- No changes to `src/s18.c` core semantics, `src/recomp.c`, `src/fd1094.c`, or `src/generated_game.c`. Front-end files only (plus CMake source lists).
- SDL2 is the only new API surface; no third-party libraries.
- Windows is the shipping target; nothing SDL-facing may be Windows-only.
- **This workspace is NOT a git repository.** There are no commit steps; every task ends with its verification steps instead.
- The standing regression suite must stay green at every integration: `--selftest 1000000`, `--block-selftest 1000000`, `--frames 60000 --display 1` (0 recover/MISS/illegal), `--frames 30000 --play --display 1` (0/0/0), kids-room replay `--frames 30000 --replay build/kids2b.txt --display 1` (0/0/0), 16:9-center identity at `--frames 1200 --replay build/left_input.txt` (0 px diff).
- Headless builds must remain unaffected: new code compiles only into `mwalk` and `mwalk_launcher` targets.
- PowerShell `2>`/stderr redirection writes UTF-16LE — decode BOM-aware in Python.
- Throwaway test harnesses live under `build/` only, never in `src/`, never in CMakeLists.txt.

## Review Focus

The five conditions most likely to bite a player that no task's tests naturally exercise:

1. **Old `settings.ini` without the new keys** → must load with defaults, not crash or garbage. Pinned in Task 1's harness: load a file written by the old schema, assert every new field equals its default.
2. **Shader preset OFF** → output must be pixel-identical to the pre-change build (no accidental filter residue). Pinned in Task 3: raw-frame hash with OFF equals the pre-task F12 hash.
3. **Pad hot-unplug mid-game** → game continues on keyboard, no crash or stuck bits. Pinned in Task 4's harness: simulated `SDL_CONTROLLERDEVICEREMOVED` clears the pad's contributions while keyboard bits survive.
4. **Audio toggled off/on at runtime** → device closes without deadlock and reopens cleanly. Pinned in Task 2's window test: toggle twice via F1, log stays empty, sound visibly stops/resumes.
5. **P3 start/coin bit collision (service bit 0x80)** → P3 pad Start must fire bit 0x80 once; P3 pad Back must contribute nothing by default. Pinned in Task 4's harness as an explicit assertion.

---

### Task 1: Settings schema extension

**Files:**
- Modify: `src/settings.h`, `src/settings.c`
- Test: `build/test_settings.c` (throwaway harness, compiled manually with gcc, never added to CMake)

**Interfaces:**
- Produces (used by Tasks 2, 3, 4, 5, 8): the new `mw_settings` fields, exact names and defaults:

```c
/* src/settings.h — add to struct mw_settings */
int sound_on;           /* default 1 */
int shader_preset;      /* 0 OFF / 1 SCANLINES / 2 CRT, default 1 */
int scanline_intensity; /* 0..100, default 50 */
int window_w;           /* default 1280 */
int window_h;           /* default 720 */
int key_map[3][7];      /* SDL_Scancode per player, actions:
                           [0]=UP [1]=DOWN [2]=LEFT [3]=RIGHT
                           [4]=B1 [5]=B2 [6]=B3 */
int pad_map[3][7];      /* SDL_GameControllerButton, -1 = unbound */
char pad_guid[3][33];   /* 32-char SDL joystick GUID + NUL; "" = auto-assign */
```

- Key defaults: P1 = {UP, DOWN, LEFT, RIGHT, Z, X, C}; P2 = {W, S, A, D, J, K, L}; P3 = {KP_8, KP_2, KP_4, KP_6, KP_1, KP_2, KP_3} (SDL_Scancode values).
- Pad defaults: {DPAD_UP, DPAD_DOWN, DPAD_LEFT, DPAD_RIGHT, A, B, X} for all three players.
- Serialization: extend `mw_settings_save`/`mw_settings_load` with lines `sound_on=%d`, `shader_preset=%d`, `scanline_intensity=%d`, `window_w=%d`, `window_h=%d`, `key_map_p%d=%d,...` (7 comma values), `pad_map_p%d=%d,...`, `pad_guid_p%d=%s`. Missing keys keep the in-struct default (backward compatible with old files). Clamp `shader_preset` to 0..2 and `scanline_intensity` to 0..100 on load.

- [ ] **Step 1: Write the failing harness `build/test_settings.c`**

Asserts, in order: (a) `mw_settings_defaults` produces the exact defaults above; (b) save→load round-trip preserves every field including `pad_guid_p2="03000000...test"`; (c) loading a file that contains ONLY the old keys (`volume`, `fullscreen`, `scanlines`, `integer_scale`, `show_fps`, `display_mode`) yields old values intact plus every new field at its default; (d) `shader_preset=9` and `scanline_intensity=500` in a file clamp to 2 and 100.

- [ ] **Step 2: Compile and run the harness, verify it fails**

Run: `gcc -I src -o build/test_settings.exe build/test_settings.c src/settings.c && ./build/test_settings.exe`
Expected: FAIL — new fields do not exist (compile error). Record it.

- [ ] **Step 3: Implement the fields, defaults, and save/load in `src/settings.h/.c`**

Follow the existing parse style in `mw_settings_load` (the `sscanf`-per-line pattern already there). GUID lines use `%32s`.

- [ ] **Step 4: Re-run the harness, verify PASS**

Run: `gcc -I src -o build/test_settings.exe build/test_settings.c src/settings.c && ./build/test_settings.exe`
Expected: PASS with "ALL SETTINGS TESTS PASSED".

- [ ] **Step 5: Verify the game and launcher still build**

Run: `cmake --build build --target mwalk_headless` and `cmake --build build` (mwalk + launcher)
Expected: clean builds; `--selftest 1000000` still prints `SELFTEST passed`.

---

### Task 2: Audio wiring — `sound_on` default + runtime toggle

**Files:**
- Modify: `src/main_sdl.c`

**Interfaces:**
- Consumes: Task 1's `settings.sound_on`; existing `s18_audio_set_fast(int)` from `src/sound.h`; existing `--audio` CLI flag and `MW_AUDIO` env.
- Produces: `static void audio_apply(int on)` in `main_sdl.c` (opens/closes the SDL audio device and calls `s18_audio_set_fast(on ? 0 : 1)`); startup honors `sound_on || cli_audio || MW_AUDIO`; a per-frame check applies F1 changes (same pattern as the existing fullscreen `applied_*` tracking).

- [ ] **Step 1: Implement `audio_apply` and startup/runtime wiring**

Rules: startup computes `audio_on` from settings/CLI/env as above. Device close must pause the callback first (`SDL_PauseAudioDevice(dev, 1)` then `SDL_CloseAudioDevice`); reopen mirrors the existing `want` spec block. `g_sdl_audio` stays valid (the reader tolerates a dead device: verify `s18_audio_read` behavior when the callback is absent — it is only called from the callback itself, so no other path exists; state this in a comment).

- [ ] **Step 2: Add the SOUND toggle row to the shared settings panel**

In `mw_ui_draw_settings` (`src/ui.cpp`): a `toggle_row` "SOUND" bound to `settings->sound_on` (existing row style; `in_game` rows already apply live).

- [ ] **Step 3: Window verification (audio path is manual-only)**

Launch `build\mwalk.exe --zip mwalk.zip` from `build\`: stderr log must stay 0 bytes and the game must be audible. Open F1, toggle SOUND off → audio stops; on → resumes. Toggle twice more; log still 0 bytes; close.
Expected: silent log, audible toggling, no hang.

- [ ] **Step 4: Headless regression**

Run: `.\build\mwalk_headless.exe --rom-dir roms --frames 30000 --play --display 1`
Expected: 0 recover / 0 REC MISS / 0 illegal (headless never touches SDL audio).

---

### Task 3: Video pipeline — resolution presets + source-space shader pass

**Files:**
- Modify: `src/main_sdl.c` (window preset application; shader pass on the frame), `src/ui.cpp`, `src/ui.h`
- Test: numeric window-capture analysis (below) + raw-frame hash

**Interfaces:**
- Consumes: Task 1's `window_w/window_h`, `shader_preset`, `scanline_intensity`.
- Produces: `static void apply_shader(uint32_t *frame, int w, int h, int preset, int intensity)` in `main_sdl.c`, called on the 560x224 frame between `s18_render_frame` and `SDL_UpdateTexture`; `mw_ui_draw_crt` in `src/ui.cpp` is REMOVED from the frame path (the settings scanlines toggle maps to `shader_preset != 0`). Window presets: on settings change, `SDL_SetWindowSize(window, window_w, window_h)`; fullscreen unchanged.

- [ ] **Step 1: Implement `apply_shader`**

Presets: OFF → return immediately (byte-exact no-op). SCANLINES → for each odd source line y, scale pixel RGB toward black by `intensity/100.0 * 0.45`. CRT → SCANLINES plus a vertical grille (RGB channels attenuated in a 3-column period by the same factor scaled by 0.5) — bloom approximation optional; cut it if it looks wrong on the window test. One scanline per SOURCE line (224 lines), independent of window size.

- [ ] **Step 2: Wire presets + resolution rows into the settings panel**

Rows: "SHADER" cycles OFF / SCANLINES / CRT; "SCANLINE INTENSITY" slider 0..100 (hidden when preset is OFF); "RESOLUTION" cycles 1280x720 / 1600x900 / 1920x1080 / 2560x1440 (writes `window_w/h`, marks changed). Window-size change applies immediately via the `applied_*` pattern.

- [ ] **Step 3: OFF-preset regression — raw frame hash**

Run the packaged-path check: with preset OFF, `F12` screenshot at `--frames 2200` equivalent... F12 is in-game; instead launch the window, reach attract, F12 twice 5 s apart: two `build/moonwalker.bmp` hashes both differing from each other (animation) — and diff one against a pre-task capture of the same attract moment is NOT required (attract timing drifts); instead assert in code review that OFF returns before touching the buffer, and pin it in Task 9's suite (16:9 center identity must stay 0 with defaults OFF? Default preset is SCANLINES — the center-identity headless check uses no SDL, so unaffected; note this).
Expected: OFF is a proven no-op by inspection; window test shows three visibly distinct presets.

- [ ] **Step 4: Numeric window verification of SCANLINES**

Launch the game, PrintWindow-capture during gameplay for presets OFF and SCANLINES@50; Python check: SCANLINES capture shows per-source-row luminance periodicity (odd/even row mean-delta > 8/255 in the game area) while OFF shows < 2/255. Report the two numbers.

- [ ] **Step 5: Headless regression**

Run: `.\build\mwalk_headless.exe --rom-dir roms --frames 60000 --display 1`
Expected: 0/0/0 (headless never renders SDL).

---

### Task 4: Input module — 3 gamepads + keyboard merge

**Files:**
- Create: `src/input.h`, `src/input.c`
- Modify: `src/main_sdl.c` (use the module), `CMakeLists.txt` (add `src/input.c` to `mwalk` and `mwalk_launcher` source lists only)
- Test: `build/test_input.c` (throwaway harness)

**Interfaces:**
- Produces (used by Tasks 5, 8):

```c
/* src/input.h */
#define MW_IN_B1 0x01
#define MW_IN_B2 0x02
#define MW_IN_B3 0x04
#define MW_IN_UP 0x08
#define MW_IN_DOWN 0x10
#define MW_IN_LEFT 0x20
#define MW_IN_RIGHT 0x40

typedef struct {
    uint8_t actions[3];  /* MW_IN_* per player, keyboard OR pad */
    uint8_t start[3];    /* service-bit contributions per player */
    uint8_t coin[3];
} mw_input_result;

void mw_input_init(void);
void mw_input_shutdown(void);
void mw_input_load_maps(const mw_settings *s);
void mw_input_save_maps(mw_settings *s);
void mw_input_process_event(const SDL_Event *e);   /* hot-plug + capture */
void mw_input_poll(mw_input_result *out);          /* merge keyboard+pads */
int  mw_input_pad_count(void);
const char *mw_input_pad_name(int player);          /* "" if none */
void mw_input_assign(const char *guid, int player); /* stored GUID wins */
/* rebinding capture */
void mw_input_begin_capture(int player, int action, int pad);
int  mw_input_capture_result(int *code);  /* 0 waiting, 1 captured, -1 cancel */
```

- Rules: up to 3 `SDL_GameController*`; auto-assign by connection order unless a stored `pad_guid[player]` matches a connected pad (stored wins, per the spec resolution). Pad Start → player's START service bit; pad Back/Select → player's COIN service bit, EXCEPT player 3 whose Back is unmapped (bit 0x80 collision, spec resolution 5). Pad action bits use `pad_map` defaults {DPAD_UP, DPAD_DOWN, DPAD_LEFT, DPAD_RIGHT, A, B, X} plus left-stick axis → directions.
- `main_sdl.c`: keep its `key_state/key_latch` arrays and feed them via a new `mw_input_set_keyboard(int scancode, int down, int latch)` so the merge stays testable; `update_inputs` becomes a thin mapper from `mw_input_result` to `s18_inputs_t` bits (existing bit layout: p1 LEFT 0x80 RIGHT 0x40 UP 0x20 DOWN 0x10 B1 0x01 B2 0x02 B3 0x04; service: coin1 0x01 coin2 0x02 coin3 0x80 start1 0x10 start2 0x20, P3 start shares coin3's 0x80).

- [ ] **Step 1: Write the failing harness `build/test_input.c`**

The merge core must be pure: expose `mw_input_merge_for_test(const uint8_t key_bits[3], const uint8_t pad_bits[3], const uint8_t pad_start[3], const uint8_t pad_coin[3], mw_input_result *out)`. Assertions: (a) keyboard-only merge returns keyboard bits; (b) pad ORs in; (c) P3 pad Back contributes NOTHING to coin/start; (d) P3 pad Start contributes bit 0x80 exactly once; (e) pad hot-unplug simulation (pad bits zeroed) leaves keyboard bits intact.

- [ ] **Step 2: Compile and run, verify FAIL** (module doesn't exist)

Run: `gcc -I src -o build/test_input.exe build/test_input.c src/input.c src/settings.c -lSDL2 && ./build/test_input.exe` (adjust include/lib paths to the project's SDL2; the harness may stub SDL types if linking is awkward — pure-merge assertions must not require an SDL display).
Expected: FAIL — undefined references.

- [ ] **Step 3: Implement `src/input.h/.c` and wire `main_sdl.c` + CMake**

Follow the spec rules above. `mw_input_poll` reads the SDL keyboard state array passed at init plus open controllers each frame.

- [ ] **Step 4: Re-run harness, verify PASS**

Run the same command. Expected: `ALL INPUT TESTS PASSED`.

- [ ] **Step 5: Window smoke test + headless regression**

Launch `build\mwalk.exe --zip mwalk.zip`: keyboard controls still work through the new path (arrows, Z/X/C, 5/1). Run `.\build\mwalk_headless.exe --rom-dir roms --selftest 1000000` and the 30k play: selftest passes, 0/0/0.

---

### Task 5: Remapping UI + persistence + launcher device view

**Files:**
- Modify: `src/ui.h`, `src/ui.cpp`, `src/main_sdl.c`, `src/main_launcher.c`

**Interfaces:**
- Consumes: Task 4's capture API, `mw_input_pad_name/count`, Task 1's map fields.
- Produces: `void mw_ui_draw_controllers(mw_ui *ui, mw_settings *settings, int *open, int *changed);` — a CONTROLLERS panel: per player, pad name/connection status, a "PLAYER n" assignment selector (writes `pad_guid_pN` via `mw_input_assign`), and a 7-row rebind table (UP/DOWN/LEFT/RIGHT/B1/B2/B3) where clicking a row starts capture ("PRESS A KEY OR BUTTON…", ESC cancels) writing into `key_map`/`pad_map` and marking settings dirty. F1 opens it as a third tab/panel; the launcher shows the same panel (live state works there per spec resolution 2 — the launcher initializes `SDL_INIT_GAMECONTROLLER`).

- [ ] **Step 1: Implement the panel and capture flow**

Overlay open pauses the game, so capture is safe. After capture, `mw_input_save_maps(settings)` keeps `settings.ini` current; assignment rows write GUIDs.

- [ ] **Step 2: Persistence verification (numeric)**

Window test: rebind P1 B1 to Q, check `settings.ini` contains the new scancode on the `key_map_p1` line; relaunch; Q attacks in game, Z does nothing. Unplug/replug phase is manual for the partner.

- [ ] **Step 3: Headless regression**

`--selftest 1000000` + `--frames 30000 --play --display 1` → pass + 0/0/0.

---

### Task 6: Cheat address discovery (headless investigation)

**Files:**
- Create: `docs/cheats-addresses.md` (findings + evidence); scripts under `build/` only.

**Interfaces:**
- Consumes: headless `--watch HEXOFFSET`, `--dump`, replay files (`build/left_input.txt`, `build_snd/dance_1750.txt`, `build/kids2b.txt`).
- Produces (used by Task 7): work RAM byte/word addresses for per-player LIVES, the player HEALTH/damage value, and the DANCE MAGIC meter, each with the frame-level evidence (address, width, delta direction, the replay + frame that proves it). Known already: score `0x1A42` (word), credits `0x2000` (byte), act `0x21E1` (byte), camera `0x2110`, child count `0x2120`.

- [ ] **Step 1: Design the delta watch**

Run replays that (a) deliberately die, (b) use dance magic at frame ~1755, (c) spend a credit; use `--watch` sweeps over the object/task table region `0x0000-0x3BFF` (bounded: watch one candidate per run, or add a temporary env-gated block-diff hook in `main_headless.c` that prints workram bytes whose value changed between frame N-1 and N — remove it after; that hook is the efficient path, mirroring the earlier `tilelog` pattern).

- [ ] **Step 2: Verify each address**

For each candidate: the delta occurs at exactly the death/magic/coin frame, on the expected player slot, and no unrelated write pattern matches. Document in `docs/cheats-addresses.md` with the evidence lines.

- [ ] **Step 3: No code changes remain**

`main_headless.c` temporary hooks removed; `git`-less workspace — the findings doc is the deliverable.

---

### Task 7: Cheat menu + apply logic

**Files:**
- Modify: `src/main_sdl.c`, `src/ui.h`, `src/ui.cpp`

**Interfaces:**
- Consumes: Task 6's address table.
- Produces:

```c
typedef struct {
    int infinite_lives;    /* freeze every player's lives value */
    int invincibility;     /* freeze health at max each frame */
    int infinite_magic;    /* freeze magic meter at max */
    int free_credits;     /* write 9 to credits every frame */
    int act_override;      /* 0 = off, else act value applied at next transition */
} mw_cheats;
static void cheats_apply(s18_t *m, const mw_cheats *c);  /* called AFTER s18_run_frame each frame */
```

`mw_ui_draw_cheats(mw_ui*, mw_cheats*, int *open)` — F1 CHEATS panel, GLOBAL toggles per the partner's decision (one toggle affects all players), plus an ACT selector per Task 6's findings on reload semantics. Cheats never persist and never touch headless.

- [ ] **Step 1: Implement apply + panel**

Freeze order: write AFTER `s18_run_frame` returns so the game reads frozen values on its next frame. Act select follows the discovery's safe scope (start-of-run vs transition); if live jump corrupts the act table, ship start-of-run and label it.

- [ ] **Step 2: Window verification**

Launch with audio: enable infinite lives → die → lives unchanged; free credits → coin slot shows 9; infinite magic → dance always ready; disable all → behavior returns to normal within one frame.

- [ ] **Step 3: Regression with cheats never enabled**

`--selftest 1000000`, `--block-selftest 1000000`, 60k attract, 30k play, kids replay — all unchanged (cheats are structurally absent from headless).

---

### Task 8: Launcher refresh + README

**Files:**
- Modify: `src/main_launcher.c` (rebuild only + CONTROLLERS panel inclusion), `README.md`, `CMakeLists.txt` (only if the launcher needs the input module link — it does, from Task 4)

**Interfaces:**
- Consumes: Tasks 1-5 outputs; existing `spawn_game` flow.
- Produces: launcher rebuilt from current source; START path unchanged (settings are already saved before spawn, so `sound_on` and all maps flow into the game automatically — no new launch arguments); README sections updated: audio is verified (remove the stale "muted until verified" note), SOUND toggle, shader/resolution options, pad support + defaults + P3 note, cheat menu, controller remapping.

- [ ] **Step 1: Rebuild and verify the launcher START path**

`cmake --build build` then launch `build\mwalk_launcher.exe`: select archive, START → game window opens WITH sound (settings default `sound_on=1`); stderr log empty.

- [ ] **Step 2: README updates**

Cover: audio verification status change, new settings fields, three pads + defaults + P3 start/coin note, cheats (global, session-only, F1), remapping persistence.

---

### Task 9: Integration, regression, package, relaunch (orchestrator)

**Files:**
- Modify: none (verification + packaging only)

**Interfaces:**
- Consumes: all prior tasks merged in the working tree.

- [ ] **Step 1: Full standing suite (silent headless)**

`--selftest 1000000`, `--block-selftest 1000000`, `--frames 60000 --display 1`, `--frames 30000 --play --display 1`, `--frames 30000 --replay build/kids2b.txt --display 1`, 16:9-center identity at frame 1200.
Expected: all pass / 0/0/0 / 0 px.

- [ ] **Step 2: Package**

`package_windows.bat`; clean `dist/moonwalker-windows` of nvram/bmp/settings/logs; zip; verify 19 entries, no ROM/generated content.

- [ ] **Step 3: Launch for partner manual test WITH audio**

Launch the packaged `mwalk.exe --zip mwalk.zip --audio` equivalent (or via the launcher); confirm window + sound + log clean; hand over for the 3-pad manual session.

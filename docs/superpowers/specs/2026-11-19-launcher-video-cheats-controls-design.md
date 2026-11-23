# Launcher, Video Options, Cheat Menu, and 3-Player Controllers — Design

Date: 2026-11-19
Status: awaiting builder check, then partner approval

## Intent

Make the shipped Moonwalker port a complete product for manual play: a current
launcher that starts the game with sound, a real video options panel, a live
cheat menu, and proper support for three physical game controllers with a
remapping UI. Everything runs on top of the verified emulator core; none of it
may regress the core's correctness guarantees.

## Why

The core port is now stable and oracle-verified (60k-frame attract, 30k-frame
gameplay, dance-magic replay: zero recoveries / fallbacks / illegal ops; both
1M-step lockstep selftests pass). The remaining gaps are presentation and
convenience: the launcher binary is two days stale and never passes the new
`--audio` flag, the settings panel covers only a fraction of what a player
expects, and input is keyboard-only despite the game being a 3-player arcade
title.

## Goals

1. **Launcher refresh** — `mwalk_launcher.exe` rebuilt from current source;
   a Sound toggle (default ON) that launches `mwalk.exe` with full audio;
   everything else about the START flow unchanged.
2. **Video options** (shared settings panel, so the launcher and the in-game
   F1 overlay both get them):
   - Aspect ratio: already present (4:3 / 16:9 / 16:10 / 21:9); keep.
   - Resolution: window-size presets (1280x720, 1600x900, 1920x1080,
     2560x1440, plus the existing resizable window and fullscreen).
   - Shader presets: OFF / SCANLINES / CRT (scanlines + aperture-grille mask
     + soft bloom), implemented as a CPU post-process pass over the frame
     before texture upload.
   - Scanline intensity: 0-100% slider (replaces the on/off toggle).
   - Existing options stay: fullscreen, integer scale, FPS display, volume.
3. **Cheat menu** — new in-game F1 panel, session-scoped toggles:
   - Infinite lives
   - Invincibility
   - Infinite dance magic
   - Free credits
   - Stage/act select
4. **Controllers** — up to three SDL game controllers connected at once,
   auto-assigned to players 1/2/3 in connection order; a remapping menu that
   covers both pad buttons and keyboard keys per player; mappings persisted.

## Non-goals

- Netplay, save states, rewind, or movie recording.
- Rebinding arcade service functions (coin/start remain on keyboard 5/6/3 and
  1/2; pads map their own Start/Select buttons to the same service bits).
- Cheat persistence across runs (cheats are per-session; defaults off).
- Touch/mobile input.

## Anti-goals

- Any change to the translated game code (`src/generated_game.c` semantics),
  the FD1094/decrypt path, or the CPU core. Cheats are front-end memory
  writes only.
- Any regression in the standing verification suite (selftests, 60k/30k
  headless runs, 16:9-center identity).
- New third-party dependencies (SDL2 only).

## Constraints

- Portable C/C++ in the existing codebase style; Windows is the shipping
  target but nothing may be Windows-only in the SDL-facing code.
- The frame budget: the CPU shader pass runs at <= 560x224 (125k pixels) —
  trivial, but it must not run when the preset is OFF.
- The in-game overlay already pauses emulation while open; binding capture
  and menu interactions happen there.
- Headless builds and selftests must be unaffected: all new code lives in
  the SDL front end (`src/main_sdl.c`, `src/ui.cpp`, `src/settings.*`,
  `src/main_launcher.c`) plus a new input module.

## Approaches

### Shaders
- **CPU post-process presets (chosen)** — the front end composites the frame
  in software before uploading the texture. Portable, fits the existing
  SDL_Renderer code, cheap at 224p, and the current scanline overlay
  (`mw_ui_draw_crt`) becomes one of the presets.
- GPU shaders via an OpenGL backend — rejected: a rendering-backend rewrite
  for a 224p game, with platform fragility and no visible gain at this
  resolution.
- Keep the single existing overlay — rejected: the user asked for shader
  options.

### Cheats
- **Front-end per-frame workram writes (chosen)** — after `s18_run_frame`
  returns, the front end re-writes the target values (lives, magic, credits)
  so the game reads the frozen value next frame. Reversible, live-toggleable,
  zero game-code changes; identical in spirit to how arcade cheat devices
  worked.
- Patching the recompiled branches for god-mode — rejected: corrupts the
  verified trajectory and re-opens coverage churn.
- Debugger-style breakpoints — rejected: overkill.

### Controllers
- **SDL_GameController API (chosen)** — standard layouts out of the box
  (dpad/stick -> directions, face buttons -> B1/B2/B3), hot-plug events, GUID
  identification; custom remapping on top.
- Raw SDL_Joystick — rejected: no standard mapping, every pad would need
  manual defaults.
- External input toolkit — rejected: new dependency.

### Persistence
- **Extend `settings.ini` (chosen)** — one file, already shared between the
  launcher and the game; add the new fields with backward-compatible
  defaults so old files load cleanly.
- Separate `controls.ini` — rejected: a second file with no benefit.

## Parts

### 1. Settings and persistence (`src/settings.h/.c`)
New fields (with defaults):
- `sound_on` (default 1) — full-synthesis audio; replaces the CLI-only
  `--audio`/`MW_AUDIO` path (both remain as overrides).
- `shader_preset` (0 OFF / 1 SCANLINES / 2 CRT, default 1).
- `scanline_intensity` (0-100, default 50).
- `window_w`, `window_h` (default 1280x720; 0x0 = "remember nothing,
  use preset list").
- Per-player control maps (see part 4) serialized as compact lines, e.g.
  `p1_keys=...`, `p1_pad=...`.
The loader must accept files written by older versions (missing keys ->
defaults).

### 2. Audio wiring (`src/main_sdl.c`)
- Startup: honor `settings.sound_on` (CLI `--audio` / `MW_AUDIO=1` still
  force it on).
- Runtime toggle in F1: switch `s18_audio_set_fast`, and open/close the SDL
  audio device safely (pause callback, close, reopen). No audio-thread
  state may be touched from the main thread without the device paused.

### 3. Video pipeline (`src/main_sdl.c`, `src/ui.cpp`)
- Resolution presets set the window size (fullscreen keeps desktop
  resolution); the game rect logic is unchanged.
- The CPU shader pass runs on the cropped output region between
  `s18_render_frame` and `SDL_UpdateTexture`, or (simpler, chosen) as a
  post-blit overlay pass equivalent to today's `mw_ui_draw_crt` — builder
  picks the cleaner of the two; visual result is what matters:
  - SCANLINES: horizontal line darkening scaled by intensity.
  - CRT: scanlines + vertical RGB-ish grille + mild bloom approximation.
- `mw_ui_draw_crt` becomes the intensity-aware implementation of the
  SCANLINES/CRT presets.

### 4. Input and controllers (new `src/input.c/.h`; wiring in
   `src/main_sdl.c`)
- A small input module owns: keyboard state (existing scancode mapping as
  defaults), up to three `SDL_GameController*` handles, per-player maps
  (keyboard scancode -> action, pad button -> action), and a merge function
  that produces the `s18_inputs_t` bits by OR-ing keyboard and pad.
- Auto-assign: controllers attach in connection order to the first free
  player slot; the UI shows name + GUID and lets the player reassign.
- Defaults (pad): dpad/stick -> directions; south/east/west face buttons ->
  B1/B2/B3; Start -> that player's START service bit; Back/Select -> that
  player's COIN service bit. P2/P3 keyboard defaults stay WASD/JKL and
  numpad as today.
- Remapping UI (F1 -> CONTROLLERS panel, also in the launcher): per player,
  a table of actions (Up/Down/Left/Right/B1/B2/B3) with "press a key/button
  to rebind" capture; the overlay pauses the game, so capture is safe.
  Capture accepts either a keyboard scancode or a pad button; Escape cancels.

### 5. Cheat menu (`src/main_sdl.c` + `src/ui.cpp`)
- F1 -> CHEATS panel: five toggles + a stage/act selector.
- Discovery (builder, headless): find workram addresses for lives, health,
  magic meter by delta-watching controlled replays (known addresses today:
  score `0x1A42`, credits `0x2000`, act `0x21E1`, camera `0x2110`, child
  count `0x2120`).
- Application: after each `s18_run_frame`, write frozen values. Invincibility
  = freeze the health/damage byte at max; if the game uses one-shot damage
  scripts instead of a health value, fall back to restoring it immediately
  (same mechanism).
- Stage select: sets the act byte and triggers a room reload; safest scope is
  "act select at next room transition / from the character-select screen."
  If live act-jumping proves to corrupt the act object table, ship act select
  as start-of-run only and note the limitation.

## Builder-check resolutions

1. **Cheats per-player or global** — OPEN QUESTION for the partner.
   Proposal: apply to all active players (one toggle freeze-writes every
   player slot). Per-player rows only if the partner wants them.
2. **Pad→player assignment persistence** — settled (my call): persist
   GUID→player assignments in `settings.ini` (one line per pad); stored
   assignment wins over connection order; the launcher initializes the
   game-controller subsystem and shows live device state so reassignment
   there is meaningful.
3. **Audio verified?** — settled (already answered by the project): the
   audible path was verified against the MAME oracle (dance-magic WAV
   envelope correlation, attract/gameplay/movement replay runs with zero
   errors). The README's "audio muted until verified" note is stale and
   MUST be updated as part of this work. `sound_on` default 1 stands.
4. **Scanlines in frame space vs window space** — settled (my call): the
   shader pass runs in SOURCE frame space (one scanline per game scanline,
   pre-blit over the cropped region), not as a window-space overlay. The
   current `mw_ui_draw_crt` overlay is retired; at non-integer scales it
   beats against the 224 source lines. CRT preset = source-space scanlines
   + grille; bloom approximation is optional and cut first if it looks bad.
5. **P3 start/coin service-bit collision (bit 0x80)** — settled (my call,
   constraint-driven): the service register muxes P3 start and coin 3 on
   the same bit, so P3's pad Start maps to bit 0x80 (the game interprets it
   contextually); P3's pad Select is unmapped by default and manually
   bindable. Documented in the CONTROLLERS panel.

## What is left to the builder

- Exact workram addresses for lives / health / magic (headless discovery).
- Stage-select mechanics (act byte + reload semantics) and its safe scope.
- Shader preset visual tuning (intensity curves, grille pattern).
- ImGui capture-flow details for rebinding.
- Whether the shader pass pre-blit or overlay is cleaner to implement.
- Window preset interactions with `integer_scale` and existing
  `game_rect` logic.

## Verification

- Standing regression suite after every integration: `--selftest 1000000`,
  `--block-selftest 1000000`, 60k-frame attract, 30k-frame gameplay,
  kids-room replay — all zero recover/fallback/illegal; 16:9 center
  identity check at frame 1200.
- Headless replay with pads unavailable (no SDL) must behave exactly as
  today.
- Manual window tests (audio ON per the partner's preference): launcher
  START runs the game with sound; each shader preset visibly distinct;
  scanline slider works; three pads control three players; rebinding
  survives restart; each cheat toggles live and off cleanly; the standing
  suite still passes with cheats never enabled (they are front-end only,
  so headless runs are unaffected by construction).

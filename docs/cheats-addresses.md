# Cheat addresses — lives, health, dance magic (headless discovery)

Discovery for Task 7 (cheat menu). All addresses are **work RAM byte offsets**
(the 16 KiB `m->workram` array, the same space as the known `0x1A42` score /
`0x2000` credits / `0x21E1` act). Front-end code writes these per frame after
`s18_run_frame()`.

Status: verified on the recompiled core with the standing headless harness
(`build\mwalk_headless.exe`). Temporary debug hooks used for the search
(`MW_DIFF` block diff, `MW_POKE` byte poke) were removed before commit; the
findings below were re-confirmed with plain `--dump` runs on the final build.

## Summary table

| Cheat | 1P | 2P | 3P | Width | Range / initial | Direction |
|---|---|---|---|---|---|---|
| Lives | `0x1A58` | `0x1A98` | `0x1AD8` | byte | init from `0x2029` (DSW Lives: 2 or 3) | **−1** per death |
| Health (vitality) | `0x001C` | `0x00DC` | `0x019C` | byte | max `0x30` with SW2:4 *Player Vitality* = Low (default DSW `0xFD`); `0x40` when SW2:4 is High (bit `0x08` clear, e.g. `0xF5`) | **−damage** per hit |
| Dance magic | `0x1A79` | `0x1AB9` | `0x1AF9` | byte | starts at `0x01`; no hard cap seen (HUD draws one icon per unit) | **−1** per dance |

Freeze values for Task 7:

- **Infinite lives** — write `0x09` to `0x1A58`, `0x1A98`, `0x1AD8` every frame.
- **Invincibility** — write `0x30` to `0x001C`, `0x00DC`, `0x019C` every frame,
  under either vitality setting (see the caveat below; `0x40` is only the High
  max). Only needed for slots that are in play; writing the byte while the
  player object is idle is harmless (the game overwrites it on spawn).
- **Infinite dance magic** — write `0x05` to `0x1A79`, `0x1AB9`, `0x1AF9`
  every frame (each dance consumes one unit, so the freeze keeps them refilled;
  5 keeps the HUD icon row inside the 1P column).

## Structure (why the addresses look like this)

Two regular per-player block tables:

1. **Player status block** — base `0x1A50 + 0x40 * (player-1)`:

   | offset | meaning | 1P | 2P | 3P |
   |---|---|---|---|---|
   | `+0x03` | frame counter (increments every frame in all three blocks) | `0x1A53` | `0x1A93` | `0x1AD3` |
   | `+0x08` | **lives** | `0x1A58` | `0x1A98` | `0x1AD8` |
   | `+0x0D` | player id (`0x37`/`0x38`/`0x39`) | `0x1A5D` | `0x1A9D` | `0x1ADD` |
   | `+0x29` | **dance magic charges** | `0x1A79` | `0x1AB9` | `0x1AF9` |

   Dump of the three blocks at f=2600 of the three-player run
   (`build\cheat_3p.txt`, `--dsw 0xED`):

   ```
   1A50: 00 00 06 0D 00 01 00 00 01 00 00 00 E0 37 E0 00
   1A90: 00 00 05 C7 00 01 00 00 02 00 00 00 E0 38 E0 00
   1AD0: 00 00 05 81 00 01 00 00 02 00 00 00 E0 39 E0 00
   ```

   (`+0x08` = 01/02/02 lives, `+0x0D` = 37/38/39 player ids; the HUD at the same
   frame reads `1P x0 / 2P x1 / 3P x1`, i.e. the digit is *spare* lives =
   value − 1.)

2. **Player object block** — base `0x0000 + 0xC0 * (player-1)`:

   | offset | meaning | 1P | 2P | 3P |
   |---|---|---|---|---|
   | `+0x00` | object state byte (`0x88/0xA8/0xAC`) | `0x0000` | `0x00C0` | `0x0180` |
   | `+0x06` | hit/state pulse (`00→02`/`00→04`) | `0x0006` | `0x00C6` | `0x0186` |
   | `+0x1C` | **health / vitality** | `0x001C` | `0x00DC` | `0x019C` |

   Three-player dump at f=2600 (`--dsw 0xED`):

   ```
   0000: 88 00 00 00 49 BC 00 08 ...   (+0x1C = 30  full bar)
   00C0: A8 02 00 00 55 5C 02 72 ...   (+0x1C = 08  short bar)
   0180: AC 04 00 00 5F 38 02 71 ...   (+0x1C = 04  empty bar)
   ```

   The same frame's screenshot (`build\3p.bmp` / `build\3p.png`) shows exactly
   those bar lengths on the 1P / 2P / 3P HUD rows.

   Slot mapping checked independently with a **2P-only** run
   (`build\cheat_2p_only.txt`: coin + START2, no 1P/3P, f=2200):

   ```
   obj 0000 state=00 health=00   |  blk 1A50 lives=00 magic=00 pid=00
   obj 00C0 state=A8 health=30   |  blk 1A90 lives=01 magic=01 pid=38
   obj 0180 state=00 health=00   |  blk 1AD0 lives=00 magic=00 pid=00
   ```

   i.e. 2P really is the `0x00C0` object / `0x1A90` status block (pid byte
   `0x38`), and 3P is the third slot (`0x0180` / `0x1AD0`, pid `0x39`).

Shared setup value: `0x2029` = the DSW "Lives" setting (2 or 3). It is the
template copied into `+0x08` of each status block when a player joins; it is
*not* the counter to freeze.

## Evidence

Replays (all start with 1 credit at f=900 and P1 START at f=1000):

- `build/cheat_idle.txt` — idle after START: deliberately dies.
- `build_snd/dance_1750.txt` — idle + B2/X press at f=1755 (dance).
- `build/cheat_dance3.txt` — three B2 presses at f=1755/1875/1995.
- `build/cheat_3p.txt` — 3 coins, 1P/2P/3P start (used for the stride check).
- `build/kids2b.txt` — standing combat replay.

### Lives — `0x1A58` (1P), byte, decrements on death

Block diff (`MW_DIFF`) over `build/cheat_idle.txt`, frames 2050–2120:

```
f=2079 addr=1A58 old=02 new=01
```

The HUD spare-lives digit (an 8×15 px glyph at screen x 56..63, y 193..207)
changes at f=2081 (bisect on the glyph box), i.e. two frames after the write:

```
f<=2080 glyph = "1"   (vertical stroke)
f>=2081 glyph = "0"   (hollow box)
```

Second death of the same run, from dumps:

```
f=2540: 1A58=01   1C=08   (last life, health nearly empty)
f=2620: 1A58=01   1C=00   (dead, still last life)
f=2700: 1A58=00   1C=00   (life spent -> CONTINUE? screen)
```

DSW cross-check at f=2200 (only the Lives bit changes):

```
--dsw 0xF9 (DIP lives = 3): 1A58=02  2029=03
--dsw 0xFD (DIP lives = 2): 1A58=01  2029=02
```

Three-player cross-check at f=2600 (`--dsw 0xED`): `1A58=01`, `1A98=02`,
`1AD8=02`, exactly matching the three HUD rows.

Freeze verification (`MW_POKE 0x1A58=9` every frame, `build/cheat_idle.txt`):

```
f=4200 unfrozen : GAME OVER
f=4200 frozen   : still in stage 1 gameplay (HUD shows x7 after two deaths)
```

### Health — `0x001C` (1P), byte, decrements per hit

Block diff over `build/cheat_idle.txt` (second life, frames 2100–3100):

```
f=2225 addr=001C old=30 new=24
f=2284 addr=001C old=24 new=18
f=2343 addr=001C old=18 new=0C
f=2420 addr=001C old=0C new=08
f=2551 addr=001C old=08 new=00     <- death
```

Each of those frames is also the frame where the yellow HUD bar shrinks and
where the player state byte pulses:

```
f=2225 addr=0006 old=00 new=02     (0x0006 = player object +0x06)
f=2225 addr=204C old=00 new=01     (damage flash, unrelated scratch flag)
```

Death → respawn refills it (first death of the same run):

```
f=2070: 1A58=02  001C=00   (dead)
f=2095: 1A58=01  001C=30   (respawned, bar full)
```

Max value follows the DSW "Player Vitality" bit (dumped at f=1500, full bar):

```
--dsw 0xFD (Vitality Low) : 001C=30  (48)
--dsw 0xF5 (Vitality High): 001C=40  (64)
```

It really is a subtractive counter (not a one-shot damage script): the value
steps down by 0x0C/0x04/0x08 per hit and stays at its new value between hits.

Freeze verification (`MW_POKE 0x001C=0x30` every frame, `build/cheat_idle.txt`):

```
f=4200 unfrozen : GAME OVER
f=4200 frozen   : alive, health bar full, lives still at initial value
```

### Dance magic — `0x1A79` (1P), byte, decrements per dance

Block diff over `build_snd/dance_1750.txt` (B2/X pressed f=1755–1775):

```
f=1755 addr=1A79 old=01 new=00     <- exactly the B2 press frame
f=1755 addr=1A60 old=00 new=02     (dance pose flag; 1A61 at 1756, 1A62 at 1757)
f=1755 addr=0006 old=00 new=04     (player state -> dance)
f=1795 addr=0006 old=04 new=03     (dance effect phase; effect sprites 0x2D42.. set at 1755)
```

Dumps of the same replay: `f=1740 1A79=01`, `f=1760 1A79=00`.

With `build/cheat_dance3.txt` the byte drops only at the first press
(f=1755 `01→00`); presses at f=1875 and f=1995 still set the pose flags
`1A60/61/62=02` but there is no charge left to consume — i.e. **the byte is a
charge count, not an input latch**. Each dance spends one unit.

HUD cross-check — the purple "dancer" icon row after the loves digit is drawn
one icon per unit:

- three-player run, f=2600: all three bytes `01`, each HUD row shows exactly
  one dancer icon;
- poking `0x1A79 = 5` draws five icons, `0x1A79 = 9` draws nine (the row runs
  into the CREDIT text, so 5 is the sensible freeze value).

Magic is `0x01` in normal play (`build/cheat_idle.txt`, `build/cheat_fight.txt`
at f=2000) and `0x00` in `build/kids2b.txt` at f=2000/f=2600 (that replay burns
through continues), so it is a per-life/room charge that has to be re-written
every frame to be "infinite".

## Act select (`0x21E1`) — not usable for cheats

`0x21E1` is `0x00` until f=1010 and `0x07` from f=1011 onward in the idle
replay (`--watch 0x21E1`), i.e. f=1011 is the room build and 7 is the act of the
first room. Experiments (single-frame and continuous pokes, screenshots plus
the byte trace):

| write | result |
|---|---|
| `0x21E1 = 0x05` at f=2200 (live room) | corrupt: at f=2400 the player is shoved to the right edge of the map with an empty bar, HUD still `ROUND 1 / STAGE 1`, act 5 never loads; still broken at f=4200 |
| `0x21E1 = 0x09` at f=2200 (live room) | same: layout shifts, no act 9 room, bar empty |
| `0x21E1 = 0x05` continuously from f=1020 | same broken end-of-room scroll |
| single-frame `0x21E1 = 0x05` at f=1008 / 1011 / 1012 / 1013 / 1015 / 1020 / 1050 (spanning the f=1011 build) | byte reads 5 until f=1011 (for f=1008) or f=1196 (for the later frames), then the game rewrites it to 7; the f=2500 screen is **frame-identical to the un-poked run** (pixel diff 0) — the act-5 room never loads |
| single-frame `0x21E1 = 0x05` at f=2200 (live room) | byte is back to `0x07` at f=2202 — the game rewrites the act byte every frame |

Conclusion: **no verified window exists.** `0x21E1` is derived state that the
game recomputes from its own room state (writes are reverted within 1–2 frames),
not a request/selection byte, and a one-frame mismatch only trips the
end-of-room logic and wedges the player object. **Task 7 must not expose act
selection** — a single act-byte write is not a supported mechanism here; the
byte is read-only for cheat purposes (the real act/room selection state is
unknown).

## Caveats

- **HUD semantics**: the HUD lives digit shows *spare* lives (`0x1A58 - 1`).
  With DSW Lives = 2 (default `0xFD`) the game starts with `0x1A58 = 2` and
  shows `x01`; after one death `0x1A58 = 1` and it shows `x00`.
- **Health max is DSW-dependent** (`0x30` Low / `0x40` High). The DIP is
  `SW2:4` "Player Vitality": bit `0x08` set = Low (default `0xFD`), clear = High
  (sample `0xF5`). Freezing at `0x30` is safe on the default config and still
  prevents death on High (the bar just reads 75%); **always writing `0x30` and
  ignoring the vitality setting is the recommended behaviour** — writing `0x40`
  on Low would overrun the bar graphic.
- **HUD lag**: the HUD digits are drawn from the value at the start of the frame,
  so a per-frame freeze shows up one frame late. In the frozen-lives run the
  digit read `x7` at f=4200 even though `0x1A58` was rewritten to `0x09` every
  frame (the death sequence subtracts from it more than once), so the displayed
  digit trails the frozen value; judge a freeze by the byte and by surviving,
  not by the digit.
- **Width**: all three values are single bytes — the adjacent byte (lives
  `+0x09`, health `+0x1D`, magic `+0x2A`) reads `0x00` in every dump, so there is
  no 16-bit counter hiding next to them.
- **Damage is a plain counter**, so "invincibility = freeze at max" is correct;
  no one-shot damage script exists. Keep the freeze applied *every* frame — a
  single write is overwritten by the next hit.
- **Magic charges**: `0x1A79` is spent by the dance and also reads `0x00` in
  some replay states (e.g. `build/kids2b.txt` after continues). Freeze it at a
  positive value (`0x05`) rather than trying to "refill once".
- **Do not freeze `0x2029`** — it is the DSW setting, not the counter.
- `--watch HEXOFFSET` parses its argument with `strtoul(..., 0)`, so it needs a
  `0x` prefix (`--watch 0x1A79`); `--watch 1A79` silently watches offset 1.
  All numbers in this document come from `--dump` snapshots and the block-diff
  logs, not from bare-hex `--watch` runs.

## Reproduction

```
# LIVES: death at f=2079 in the idle replay
build\mwalk_headless.exe --rom-dir roms --frames 2095 --replay build\cheat_idle.txt --dump build\chk

# HEALTH: dumps at f=1500 / DSW vitality switch
build\mwalk_headless.exe --rom-dir roms --frames 1500 --replay build\cheat_idle.txt --dump build\chk --dsw 0xFD
build\mwalk_headless.exe --rom-dir roms --frames 1500 --replay build\cheat_idle.txt --dump build\chk --dsw 0xF5

# MAGIC: dance trigger
build\mwalk_headless.exe --rom-dir roms --frames 1740 --replay build_snd\dance_1750.txt --dump build\chk
build\mwalk_headless.exe --rom-dir roms --frames 1760 --replay build_snd\dance_1750.txt --dump build\chk

# THREE PLAYERS (stride check)
build\mwalk_headless.exe --rom-dir roms --frames 2600 --replay build\cheat_3p.txt --dsw 0xED --dump build\chk

# ACT: the byte is derived state - value trace without any poke
build\mwalk_headless.exe --rom-dir roms --frames 2500 --replay build\cheat_idle.txt --watch 0x21E1
```

All address dumps above were run on the final, hook-free `mwalk_headless.exe`;
only the act-select probe table used the temporary `MW_POKE` hook (removed
again before commit). The 30 000-frame `--play` regression is green
(0 recover / 0 REC MISS / 0 illegal).

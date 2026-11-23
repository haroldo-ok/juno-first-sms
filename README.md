# Juno First for the Sega Master System

A tribute to Konami's 1983 arcade shooter *Juno First*, written for the Sega
Master System in C (SDCC + devkitSMS) with the hot paths in Z80 assembly.
The arcade game and Champ Games' Atari 2600 port were the references; all
graphics, sounds and code here are original (the start jingle is the opening
of Bach's Toccata in D minor, which is in the public domain).

`junofirst.sms` is a 64 KB ROM (Sega mapper, 4 banks). It runs in any SMS
emulator, and a player has reported it working on a Mark III.

## Playing

| Control       | Action                                                    |
|---------------|-----------------------------------------------------------|
| D-pad left/right | Steer                                                  |
| D-pad up      | Thrust forward (the ship coasts for seconds when released) |
| D-pad down    | Brake / reverse                                           |
| Button 1      | Fire: one shot per press, no auto-fire                    |
| Button 2      | Warp: emergency escape (see below)                        |
| Pause         | Pause                                                     |
| Left/right on the title screen | 3-D glasses: OFF, ON, or ON SWAPPED |

Fly over the perspective field and destroy every formation to clear the
wave. Each wave sends formations of eight enemies (up to three in the air
at once) in columns, diagonals, twin columns and snaking lines. Enemies fire
at you, peel off to dive at you, and loop back to their place in the
formation. Formations that you leave behind turn round and come back.

There are **50 waves**, and the game gets harder all the way: more and faster
enemies, more shots and more divers. **Every tenth wave is a boss**. It
approaches from far ahead (watch the radar), then holds its distance in front
of you, sweeping sideways and firing aimed spreads. It launches escort
formations and, from the third boss on, lunges closer now and then. Its health
shows on the HUD. Each boss is bigger than the last and keeps the previous
one's parts while adding new ones:

| Wave | Boss        | Size  | New parts                              | Hits |
|------|-------------|-------|----------------------------------------|------|
| 10   | Scout       | 24×16 | hull, dome, core                       | 24   |
| 20   | Raider      | 32×24 | engine pods, running lights            | 32   |
| 30   | Destroyer   | 40×32 | dorsal fins, three cannons; lunges     | 40   |
| 40   | Dreadnought | 48×40 | antenna mast, armour plates, 5 cannons | 48   |
| 50   | Mothership  | 48×48 | wing arrays, spikes, big core; 5-way spreads | 72 |

The bigger bosses also hover closer to you. Destroy the wave-50 Mothership
to see the ending.

**Warp** is an emergency escape: it jumps you a random distance ahead with a
moment of invulnerability, and clears every enemy shot in the air. You get
one per wave; it refills at the start of every wave and whenever you lose a
ship.

- **Radar** (top centre): the box shows depth from just behind you (bottom)
  to far ahead (top); the orange ticks on each side mark the part that is on
  screen. Enemy formations show as orange blips, you are the yellow blip, and
  the astronaut blinks white.
- **Astronaut**: every so often a destroyed enemy leaves an astronaut
  floating towards you. Collect it for 500 points and about ten seconds of
  bonus mode: the field turns red and each kill scores 200, 400, 600 … up
  to 2,000.
- **Points**: saucer 100, dart 150, spinner 200, bomber 300. Each hit on a boss
  scores 10, and destroying one scores 5,000 times its number (5,000 for the
  wave-10 boss up to 25,000 for the final one). The ending adds 10,000 for
  every ship you have left.
- **Ships**: you start with 3 and get an extra ship every 100,000 points, with no
  practical limit (the counter goes to 99). A full game played well scores
  roughly 1–1.4 million, so that's about 10–13 extra ships over the 50 waves.
- **Score** has seven digits (up to 9,999,999).
- **Music**: the melodic song plays during normal waves and the menacing growl
  during boss fights. The Toccata opening plays at the start and the song again
  in the ending.
- **Attract mode**: leave the title screen alone for 10 seconds and the
  game plays itself; press any button to return.

## 3-D glasses (optional)

The game supports the SegaScope 3-D glasses. Select **3-D GLASSES: ON** on the
title screen with left/right. Frames then alternate between the left and
right eye, with the shutters switched in sync:

- Enemies, shots, particles and the astronaut are drawn with a sideways offset
  that depends on their depth. Things near the horizon sit about 5 px "behind"
  the screen, your ship's row is at the screen plane, and the strip below
  your ship comes slightly out of it.
- The field itself has depth too. On left-eye frames a line interrupt
  re-scrolls the background every 24 scanlines, so each band of the floor sits
  at its own depth. The HUD and radar stay flat at the screen plane.
- Each eye gets 30 images a second, as in Sega's own 3-D games, and the
  picture flickers without the glasses.

I couldn't confirm from any source which value of the glasses' bit opens
which eye. The game opens the left eye with bit 0 = 0 (the convention of the
Famicom's equivalent glasses, which take the same signal). If the depth looks
inside out (the horizon in front, your ship behind), choose **ON SWAPPED**.

Hardware note: the glasses register sits at `$FFF8` in the RAM mirror, so each
write also lands in RAM at `$DFF8`. The stack starts at `$DFF0` and game data
ends near `$C6C0`, so nothing uses that byte.

## What's in it

- **Perspective field**: a sparse dot lattice whose rows stream towards you
  by palette cycling (11 phases), with faint dotted column lines converging
  on the horizon so the field reads as lanes receding into the distance.
  Everything on it is placed in true perspective: x is scaled towards the
  vanishing point with a quarter-square multiply, and sprites switch between
  three sizes with distance.
- **Inertia**: thrust builds speed gradually and the ship keeps gliding when
  you let go; stopping takes a deliberate brake.
- **Bosses and an ending**, which the arcade game doesn't have: five bosses of
  growing size and detail, and an ending sequence where
  the field races past, your ship flies off towards the horizon and fireworks
  burst overhead.
- **Williams-style effects**: dashed laser shots; enemies burst into eight
  particles flying outward; when your ship is hit, the whole field turns
  into an expanding ring explosion (tilemap swap plus palette animation)
  centred on the ship.
- **No sprite flicker by default**: a software limiter counts sprites per
  scanline band and never lets the VDP's 8-per-line limit drop an enemy or an
  enemy shot. Over budget, a near enemy falls back to its one-sprite far image
  for a frame, and only particles or your own shots can be skipped.
- **Sound**: an arcade-style menacing growl (two detuned low square waves
  with pitch sag, creeping chromatically in D minor) or the melodic 32-bar
  song from the previous version; plus SFX on the two remaining channels.

## Building

Requirements: SDCC ≥ 4.2 and devkitSMS (SMSlib, PSGlib, `ihx2sms`), expected
in `~/.devkitsms` (override with `make DEVKIT=/path`), and Python 3 for the
asset generators.

```sh
make            # regenerates src/gfx.h, src/snd.h, src/snd.c, builds junofirst.sms
make playtest   # builds, then runs the headless test scripts (needs smstest)
make clean
```

Graphics, tables and music are generated by Python scripts, so tweak those
rather than the generated files:

- `tools/gen_gfx.py`: sprite art (as ASCII pictures), the field tiles and
  tilemap, perspective tables, the starburst rings, particle tables, radar
  lookups, and the title logo.
- `tools/gen_snd.py`: the growl, the song, the Toccata jingle, the game-over
  tune and all SFX, written straight to PSGlib's stream format. The sound
  data lives in ROM bank 2, which stays mapped for the whole game.

ROM layout: banks 0–1 hold code and the tables used every frame. Bank 2 holds
the music, SFX and the five bosses' tiles (`src/bossgfx.c`), and stays mapped
in slot 2. Only the current boss is in VRAM: its tiles stream into a 40-tile
sprite slot, 4 per frame during VBlank, while the wave's title is showing.
Bank 3 (`src/gfxbank.c`) holds the bulk tile graphics. Those are only needed
when VRAM is (re)loaded, so `load_tiles_b3()` maps bank 3 briefly, and the
frame interrupt skips the music player meanwhile.

Link order matters: `ihx2sms` keeps only the first run of records for each
bank, so every object that goes into bank 2 must be linked before anything in
bank 3. `tests/check_rom.py` (part of `make playtest`) compares every banked
array in the finished ROM with its source, so a dropped bank shows up at once.

## Testing

`smstest` is the headless SMS emulator that ships with the devkitSMS
toolchain skill. The scripts in `tests/` drive the real ROM with scripted
input and check game variables:

- `playtest.in`: title → game → shooting a diver (particles) → inertia →
  frame rate under load → astronaut bonus → warp → ship explosion → game over
  → back to the title. `tests/mkplaytest.py` resolves `{symbol+offset}` pokes
  into addresses from `junofirst.noi`.
- `demo.txt`: attract mode starts on its own and a button cancels it.
- `boss.in`: wave 10 brings the boss. Its tiles finish uploading, the growl
  plays, and the boss closes in and holds its distance. It fires, launches
  escorts and takes damage, the frame rate holds during the fight, and
  destroying it pays 5,000 and starts wave 11, with the song back and the warp
  refilled.
- `playtest.in` also checks that crossing 100,000 awards a ship.
- `ending.in`: the wave-50 boss is the final one. Destroying it plays the
  ending (no wave 51), and a button returns to the title with the high score
  kept.
- `glasses.in`: 3-D mode. The title option cycles, the shutter value (read back
  through the RAM mirror at `$DFF8`) alternates every frame, the line interrupt
  fires 8 times per left-eye frame, and the frame rate holds.
- `stereo.in`: with a `-DSTEREODBG` build, snapshots the finished sprite list
  each frame so a script can confirm every enemy is offset by its depth's
  parallax on left-eye frames and not offset on right-eye frames.
- `soak.txt`: long play session, pause, sound log.
- `mkseed.py`, `mkai.py`: full games played by the built-in autopilot
  (`poke demo 2`) for difficulty tuning.
- `lines.in`: dumps VRAM over a long late-wave run; a short Python check of
  the sprite table confirms no scanline ever has more than 8 sprites.
- `load24.in`: worst-case frame-rate scenario (formations pinned on screen).

## Source layout

```
src/main.c        game: state machine, player, formations, radar, HUD,
                  ship explosion, attract-mode autopilot, and Z80 assembly
                  for the per-frame hot paths (projection, enemy pass,
                  scaled-sprite emitter with scanline limiter, bullet hits,
                  particles, shot drawing, radar plotting)
src/gfx.h         generated graphics and tables
src/snd.h/.c      generated music and SFX (bank 2)
tools/            generators
tests/            smstest scripts and helpers
```

### Performance notes

The Z80 has about 59,700 cycles per frame. 24 enemies with perspective,
scaling and a sprite limiter don't fit in C alone, so:

- Formations own fixed slots (formation *g* uses enemies 8g…8g+7). A formation
  that is entirely off screen costs almost nothing.
- Formations move at 30 Hz with double steps, and members refresh their
  projected x on alternate frames, but every member's depth is recomputed
  every frame so motion stays smooth.
- One assembly pass per frame positions every formation member, tests it
  against the ship and draws it; bullet hits scan only the formations that
  are on screen.
- The radar is rebuilt one enemy per frame over a 32-frame cycle, erasing
  only the pixels it drew last time, and is uploaded to VRAM in six slices.

## Known limitations

- The game slows down when a lot is on screen at once. In ordinary play
  (autopilot games) over 99% of frames run at full speed, flying flat out
  into the formations while firing gives about 98%, and waves 10–11 about
  97%. With around 16 enemies on screen at once it drops to roughly 75%,
  and about 20 on screen with constant firing to roughly 50% (`tests/load24.in`
  pins three formations on screen to reproduce this). The 8-sprites-per-line
  limit is never exceeded.
- Difficulty is tuned against the built-in autopilot and one player's full
  clear on a Mark III (with a rapid-fire pad), which found it well balanced.
  Kept alive, the autopilot finishes all 50 waves in about 22 minutes. The boss
  fights run at 93–98% speed.
- Enemy flight patterns, scoring and wave structure are my interpretation of
  the arcade game, not a measured reproduction.
- 3-D mode was verified in the emulator only (shutter writes, per-band scroll,
  sprite offsets, and a red/cyan anaglyph of a frame pair). It hasn't been
  viewed through real glasses, and the eye order is a best guess (see above).

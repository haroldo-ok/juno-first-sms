# Juno First SMS

A playable, arcade-style clone of Konami's 1983 classic Juno First for the Sega Master System.

This project is written in C and built with `sdcc` using `devkitSMS`. The game includes a title screen, score tracking, enemy waves, bonus rescue logic, and a built-in test harness for automated playtesting.

## Features

- Side-scrolling arcade shooter gameplay inspired by Juno First
- Multiple enemy formations and wave progression
- Bonus astronaut rescue objective
- Score, high score, lives, warp system, and HUD
- Built with the Sega Master System graphics and sound libraries
- Includes test input files for automated validation

## Controls

- 1: Fire
- 2: Warp
- Left / Right: move
- Up / Down: adjust altitude

## Building

You will need:

- `sdcc`
- `devkitSMS`
- `make`
- `ihx2sms`
- a Sega Master System emulator or flash cart to run the ROM

From the repository root:

```bash
make
```

This produces:

```bash
junofirst.sms
```

To run the playtest suite:

```bash
make playtest
```

## Repository layout

- `src/` — game source code
- `tools/` — asset generation scripts
- `tests/` — playtest and emulator input scripts
- `Makefile` — build rules
- `junofirst.sms` — generated ROM

## Notes

The game is intentionally built to resemble the original arcade feel while running on the limitations of the Sega Master System hardware.

## License

This repository does not currently include a license file. If you plan to distribute or reuse it, add an explicit license before publishing.

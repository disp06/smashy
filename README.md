# Smashy Road: Wanted — native PS Vita remake

A native PS Vita version of Smashy Road: Wanted 1.5.1. The game is rewritten in C and draws directly with the Vita GPU (sceGxm).
It uses the original models, textures, sounds and font extracted from `game_armeabi-v7a.apk`.
No Unity, no Android loader, no vitaGL, and no `libshacccg.suprx` are needed.

## Install

1. Build `SmashyRoadVita.vpk` (see below) and copy it to the Vita and install it with VitaShell.
2. Generate the data files from your APK (see below) and copy them to **`ux0:data/smashy/`**. It must contain:
   ```
   ux0:data/smashy/models.pak
   ux0:data/smashy/textures.pak
   ux0:data/smashy/ui.pak
   ux0:data/smashy/sound.pak
   ```
3. Start **Smashy Road: Wanted** from LiveArea (title ID `SMSH00001`).

Your save is written to `ux0:data/smashy/save.dat`. If startup fails, an error is written to `ux0:data/smashy/log.txt`.
A dark red screen means the data folder is missing or in the wrong place.

## Controls

| Action | Buttons / touch |
|---|---|
| Steer | Left stick, D-pad, or hold the left/right half of the front screen (like the phone game) |
| Accelerate | Automatic (like the original) |
| Brake / reverse | Square, L, D-pad down, or touch both halves |
| Drift (handbrake) | R |
| Horn | Circle |
| Pause | Start, or the pause icon |
| Menus | X = Play / select, Triangle = Garage, Square = Prize machine (100 coins), Circle = back |

## Gameplay

- Endless procedurally generated city. It is built from the game's own road tiles (straights, T-junctions, crossroads) and the city, desert and countryside building sets.
- Wanted level goes from 1 to 7 stars. It rises with time and with the number of cops you destroy:
  - 1–2 stars: police
  - 3 stars: SWAT
  - 4 stars: police helicopter
  - 5 stars: army jeeps
  - 6 stars: tanks firing shells, plus an army helicopter firing rockets
  - From 2 stars on: roadblocks
- Collect cash on the road. Smash bushes and trees, civilian traffic, and police cars.
- All 30 player cars are included. The prize machine (100 coins) unlocks a random car; a duplicate refunds 40 coins.

## Performance

- The CPU runs at 444 MHz and the GPU at 222 MHz (set automatically at startup).
- The game runs a fixed 60 Hz simulation and is vsync-locked to 60 fps.
- Measured load on the desktop test build: about 130–200 draw calls and 20k–47k vertices per frame. Game logic takes about 0.04 ms per tick on the desktop; on the Vita CPU that should be roughly 1–2 ms.
- Textures are 8-bit palettized, so the GPU needs about 23 MB of video memory for them.

## Differences from the phone version

- This is a remake, not the original binary, so handling, AI and spawn timing are close to the original but not identical.
- Not included: rivers and bridges, ads, the shop and IAP, online leaderboards.
- Garage icons are the real 3D car models instead of the phone's icon sprites.

## Building from source

- **Vita:** with VitaSDK installed, run `mkdir build && cd build && cmake .. && make`. This produces `SmashyRoadVita.vpk`.
- **Desktop test build:** `make -f Makefile.desktop` builds a headless software-rendered version. It runs scripted input and saves screenshots (see `src/plat_pc.c`).
- **Data:** game assets are not included in this repository. Generate them from your own copy of the game:
  ```
  pip install UnityPy Pillow numpy fontTools TypeTreeGeneratorAPI
  tools/build_data.sh path/to/game_armeabi-v7a.apk
  ```
  This writes `work/data/*.pak` (copy them to `ux0:data/smashy/`) and regenerates `src/gamedata.h`.

The precompiled shaders in `src/shaders.h` come from libvita2d (MIT licence).

## Repository layout

| Path | What it is |
|---|---|
| `src/` | Game, renderer (`gfx_gxm.c` for Vita, `gfx_soft.c` for desktop tests), platform layers, audio mixer, generated `gamedata.h` |
| `tools/` | APK → Vita data pipeline (UnityPy) |
| `sce_sys/` | LiveArea icon and images |

Smashy Road: Wanted is © its original developers. This project contains no game assets; you need your own copy of the game.

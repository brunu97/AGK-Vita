# AGK Vita Port (unofficial)

An unofficial PS Vita port of the AppGameKit engine. It lets AppGameKit 2D
games run on a homebrew-enabled PS Vita.

This is not an official product of The Game Creators.

## Layout

```
Player/   one-click tool that turns a compiled AGK game into an installable .vpk
Src/      the port source (Vita platform layer, app entry points, engine patch)
```

## Requires

This repo contains only the Vita port — not the engine. You need:

- The original AppGameKit engine: <https://github.com/TheGameCreators/AGKRepo>
- The Vita SDK: <https://vitasdk.org>
- vitaGL: <https://github.com/Rinnegatamante/vitaGL>
- vdpm packages: `freetype`, `libogg`, `libvorbis`, `bzip2`
- CMake 3.16+ and a `make` (e.g. mingw32-make)

## What works

Verified on real Vita hardware (HENkaku/Enso, firmware 3.60):

- 2D rendering, text, sprites (texture / colour / rotation), `CreateImageColor`
- `LoadImage` from PNG files
- Sound effects (`LoadSound` / `PlaySound`) via a software mixer on `sceAudioOut`
- OGG music streaming (`LoadMusicOGG` / `PlayMusicOGG`)
- Input: buttons + front touch panel
- File I/O, physics (Bullet / Box2D)

Not done: 3D rendering (compiles/links but untested), the legacy non-OGG
music API.

## Building (Src/)

Let `AGKREPO` be your clone of the original AppGameKit engine.

1. Copy the port layer into the engine tree:

   ```
   Src/platform-vita/Source/*   ->  AGKREPO/AGK/platform/vita/Source/
   Src/apps/template_vita/      ->  AGKREPO/AGK/apps/template_vita/
   Src/apps/interpreter_vita/   ->  AGKREPO/AGK/apps/interpreter_vita/
   ```

2. Apply the engine patch (6 small additive `#ifdef AGK_VITA` blocks).
   From the `AGKREPO` root:

   ```
   git apply Src/patches/engine.patch
   ```

3. Build the interpreter (runs compiled `.agc` bytecode):

   ```
   cd AGKREPO/AGK/apps/interpreter_vita
   cmake -S . -B build -G "MinGW Makefiles" \
         -DCMAKE_TOOLCHAIN_FILE=$VITASDK/share/vita.toolchain.cmake
   cmake --build build
   ```

   The C++ template (`apps/template_vita`) builds the same way.

## Running a game (Player/)

The interpreter is game-agnostic — it loads whatever `bytecode.byc` is bundled
in the VPK.

1. Build `apps/interpreter_vita` (above) and copy the resulting `.self` to
   `Player/bin/eboot.bin`.
2. Build your game in the AppGameKit IDE; copy the contents of its `media`
   folder (including `bytecode.byc`) into `Player/game/`.
3. Edit `Player/config.txt` (game name + a unique 9-character title id).
4. Run `Player/MAKE_VPK.bat`.
5. Install the resulting `.vpk` on the Vita with VitaShell.

## Notes

- LiveArea PNGs in `sce_sys/` must be 8-bit paletted (not RGBA), or the package
  install fails with error `0x8010113D`.
- The prebuilt vitaGL does not support OpenGL immediate mode.
- The AppGameKit engine belongs to The Game Creators — get it from the original
  repo above. This project only adds the Vita port on top of it.

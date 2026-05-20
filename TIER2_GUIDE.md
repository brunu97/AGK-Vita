# Tier 2 Guide — porting AGK C++ games to the PS Vita

Build an `.vpk` for the Vita from an AGK Tier 2 (C++) game.

## You need

- **vitasdk** — `VITASDK` env var pointing at it.
- **CMake 3.16+** and **mingw32-make**.
- **AGK engine source** from <https://github.com/TheGameCreators/AGKRepo>,
  with this port applied:
    - `Src/platform-vita/` copied into `AGKRepo/AGK/platform/vita/`
    - `Src/apps/template_vita/` copied into `AGKRepo/AGK/apps/template_vita/`
    - `Src/patches/engine.patch` applied to the engine

  (See the main `README.md`.) The engine itself is *not* in this repo — only
  the Vita port layer. After setup, you work inside
  `AGKRepo/AGK/apps/template_vita/`.

## Folder layout

```
template_vita/
├── MAKE_VPK.bat     double-click to build
├── config.txt       game name + TITLE_ID
├── game/            ← put your .cpp / .h here
│   └── template.cpp (demo, replace with your code)
├── media/           ← put assets here (auto-bundled)
├── sce_sys/         icon + LiveArea PNGs (8-bit paletted)
├── tools/make_vpk.ps1
├── CMakeLists.txt   don't edit
├── Core.cpp         don't edit
└── template.h       don't edit
```

Everything in `game/` is compiled. Everything in `media/` is bundled into
the VPK at the same relative path (`media/sprites/x.png` →
`app0:/sprites/x.png`).

## Workflow

1. Drop your C++ into `game/` (replace the demo).
2. Drop assets into `media/`.
3. Edit `config.txt`:
   ```ini
   GAME_NAME   = My Game
   TITLE_ID    = MYGM00001     # 4 uppercase + 5 digits, unique per game
   APP_VERSION = 01.00
   ```
4. Replace the three `sce_sys/` PNGs — **must be 8-bit paletted**:
   ```python
   from PIL import Image
   Image.open("src.png").convert("P", palette=Image.ADAPTIVE).save("out.png")
   ```
   (icon0 128×128, bg 840×500, startup 280×158)
5. Double-click `MAKE_VPK.bat`. Output: `<GameName>.vpk`.

## Built-in demo

`game/template.cpp` is a self-contained validator (no assets needed): a
coloured block, D-Pad / left stick to move, Cross to flash, FPS counter.
If it runs at 60 fps, the toolchain is correctly set up. Then replace it
with your game.

## What works on the Vita

2D rendering · `LoadImage` / `CreateImageColor` · audio (`PlaySound`,
`PlayMusicOGG`) · pad as raw joystick (`GetRawJoystick*`) · touch · Box2D /
Bullet physics · file I/O · JSON · ZIP.

## What does NOT work

Video (`LoadVideo`) · network / social / IAP · camera. 3D and runtime
shaders compile but are unverified on hardware.

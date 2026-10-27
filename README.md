# NetHack 5.0 for Nintendo 3DS

A full port of [NetHack 5.0](https://github.com/NetHack/NetHack) to the
Nintendo 3DS. This repository contains the complete NetHack source tree needed
to build the port, the 3DS-specific front end and system integration, and all
assets (tiles, sounds, music, Home Menu banner) required to produce playable
`.3dsx` and `.cia` files.

The port is built on top of the upstream NetHack 5.0 code base; all game logic,
levels, items, monsters and options are stock NetHack. What is new here is the
3DS presentation layer: a dual-screen interface, touch and button controls, a
tiled view, a sound engine, and the packaging for the console.

---

## Features

- **Dual-screen interface** — the map fills the 400x240 top screen while a
  compact status block, a message log and the touch controls live on the
  bottom screen.
- **Full touch + button controls** — D-pad movement (hold L to run), circle pad
  panning, soft keyboard, and a quick-command grid. See `sys/3ds/README.md`
  for the complete control scheme.
- **Context menu** — press A on the map to get a contextual menu of everything
  the hero can do on their square and the eight surrounding squares.
- **Touch-friendly inventories and menus** — every in-game menu is a scrollable
  list on the top screen; item action menus mean using, dropping, eating or
  examining an object never requires typing.
- **Two tilesets** — the default NetHack tiles (`tiles.bmp`) and the bundled
  PixelHack artwork (`PixelHack.png`), both selectable in-game. ASCII/symbol
  display is also supported.
- **Camera modes** — classic scrolling view or hero-centered view, switchable
  from the settings screen.
- **Options/Settings screen** — in-game configuration for display, tiles,
  camera, volumes and common NetHack options, saved to `3dsui.cfg`.
- **Sound engine** — CSND-based effects and looping background music (menu and
  gameplay tracks) via NetHack's standard `soundlib` interface. The bundled pack
  can be overridden per-effect from the SD card.
- **Combat feedback** — hit flashes, blood droplets and floor stains on tile
  hits.
- **Saves and settings on SD** — games, bones files, configuration and custom
  assets live under `sdmc:/3ds/nethack/` and are shared between the 3DSX and
  CIA builds.
- **Home Menu integration** — a 48x48 icon, a 3D CGFX banner (built from the
  included knight statue model) and a full `nethack.rsf` title descriptor.
- **Russian localization data** — the translated game text is part of the
  packaged data (see `dat/*.ru` and the tools in `.tools/`).

See `sys/3ds/README.md` for the detailed developer-facing documentation of the
port (controls, tilesets, camera, sound pack layout, banner pipeline, and the
RSF security descriptor).

---

## Requirements

- **devkitPro** with the **devkitARM** toolchain and **libctru** installed
  (the Makefile defaults to `C:/devkitPro`; set `DEVKITPRO` /
  `DEVKITARM` to override).
- **makerom** for CIA builds — from
  [Project_CTR releases](https://github.com/3DSGuy/Project_CTR/releases),
  placed in `C:/devkitPro/tools/bin/makerom.exe`.
- **bannertool** (or `smdhtool`) for icon/banner metadata — from
  [3ds-bannertool releases](https://github.com/carstene1ns/3ds-bannertool/releases).
  The build still succeeds without it, but the banner is left out.
- **ffmpeg** on `PATH` (or pass `FFMPEG=...`) for audio conversion. Without
  it the build produces a silent game but still succeeds.
- **Python 3** (with Pillow/numpy) only if you regenerate the 3D banner
  (`make -f sys/3ds/Makefile.3ds banner-cgfx`).

---

## Building

From the repository root:

```
make -f sys/3ds/Makefile.3ds
```

This produces two files:

| File            | Use                                                                  |
|-----------------|----------------------------------------------------------------------|
| `nethack.3dsx` | Homebrew Launcher / `3dslink` — copy to `sdmc:/3ds/`                 |
| `nethack.cia`  | Installable title — install with FBI or another title manager        |

To build only one of them:

```
make -f sys/3ds/Makefile.3ds 3dsx
make -f sys/3ds/Makefile.3ds cia
```

Notes:

- **Never run the `clean` target** — it also deletes the host tools
  (`util/makedefs.exe`, `util/dlb.exe`) that this Makefile cannot rebuild.
- The build regenerates all generated data (headers, `dat/nhdat`, `romfs/`,
  sounds, and the `build3ds/` object tree) from the sources in this repository.
- To re-pack the sound/music assets after editing `soundpack/`:

  ```
  make -f sys/3ds/Makefile.3ds sounds
  ```

- Optional targets: `host-tools`, `data`, `tiles` (regenerate tile art) and
  `banner-cgfx` (regenerate the Home Menu banner from `banner.glb`).

---

## Repository layout

| Path              | Contents                                                    |
|-------------------|-------------------------------------------------------------|
| `src/`, `include/` | NetHack core game source and headers                        |
| `dat/`            | Game data and Lua level/quest descriptions (incl. Russian)  |
| `util/`           | Host tools (`makedefs`, `dlb`) and tile generators           |
| `nhlua/lua/`      | Bundled Lua interpreter used by the game                     |
| `win/tty/`        | Shared tty window-port sources used by the front end         |
| `win/share/`      | Portable tile readers and the text tile art                  |
| `sys/share/`      | Portable system code used by the port (`pcmain`, `pcsys`, …) |
| `sys/3ds/`        | **The Nintendo 3DS port** (front end, sound, tiles, banner, Makefile) |
| `soundpack/`      | Source audio for effects and music loops                     |
| `knight.glb`      | Original model used to generate the Home Menu banner         |
| `.tools/`         | Dev scripts used to generate port headers / translations, pycgfx |

---

## License

NetHack is distributed under the NetHack General Public License (see
`dat/license`). The bundled Lua interpreter is distributed under the MIT
license (see `nhlua/lua/doc/readme.html`). PixelHack tiles are bundled as
supplied; see the port documentation for details.

---

## Documentation

- `sys/3ds/README.md` — the 3DS port documentation (controls, tilesets,
  camera, sound, saves, banner, RSF).
- `doc/` — upstream NetHack documentation (guidebook, options, sound, etc.).

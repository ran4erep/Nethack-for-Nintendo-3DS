# NetHack for Nintendo 3DS

## Building

From the repository root:

```
C:/devkitPro/msys2/usr/bin/make.exe -f sys/3ds/Makefile.3ds
```

Two files are produced:

| File           | Use                                                                 |
|----------------|---------------------------------------------------------------------|
| `nethack.3dsx` | Homebrew Launcher / `3dslink`, run from `sdmc:/3ds/`                 |
| `nethack.cia`  | Installable title (FBI, Title Manager, ...), installed to the SD card |

`make -f sys/3ds/Makefile.3ds 3dsx` or `... cia` builds only one of them.
Never run the `clean` target: it also removes host tools (`util/makedefs.exe`,
`util/dlb.exe`) that cannot be rebuilt from this makefile.

The CIA needs `makerom` next to the other devkitPro tools
(`C:/devkitPro/tools/bin/makerom.exe`), from
<https://github.com/3DSGuy/Project_CTR/releases> (`makerom-v*-win_x86_64.zip`).

## The RSF (what a CIA is launched with)

`nethack.rsf` describes the title to the 3DS loader: which services it may
open, which system modules it depends on, which parts of the filesystem it may
touch, where its boot splash comes from.  Two of those lists have to agree
with each other:

* `ServiceAccessControl` - the services the title opens (`APT:U`, `fs:USER`, ...)
* `Dependency` - the modules that provide those services, plus everything else
  the title links against (`am`, `fs`, `gsp`, ...)

If a service is listed without the module that provides it - or a module is
listed that the console does not have - the loader gives up before the title's
code runs.  The symptom is very misleading: **the boot splash animation never
gives way to the game and no error screen appears**.  For that reason both
lists are kept identical to the devkitPro/Steveice10 title template that all
known-good homebrew CIAs are built from, rather than trimmed by hand.  Only
`act`, `nfc` and `qtm` are left out: those modules do not exist below
firmware 4.x.

Other settings worth knowing about:

* `Logo: Nintendo` makes the console play the classic Nintendo 3DS boot splash.
  (`Homebrew` plays makerom's built-in homebrew animation instead.)  Leaving
  out `-exefslogo` keeps the splash in the NCCH logo region, which is where
  firmware 5.0 and later reads it from.
* `EnableCompress: true` stores `exefs:/.code` compressed; the loader unpacks
  it at startup.  Turning it off makes the CIA about 1.5 MB larger, which is
  only worth doing while debugging: the uncompressed code can then be compared
  byte for byte against the ELF, so a build can be checked without a console.

## Launcher / Home Menu metadata

The name, description and author shown by the Homebrew Launcher and the Home
Menu are set in `sys/3ds/Makefile.3ds`:

```make
APP_TITLE  := NetHack for 3DS
APP_DESC   := NetHack 5.0 for Nintendo 3DS
APP_AUTHOR := ran4erep
```

## Icon

Put the icon here:

```
sys/3ds/icon.png
```

Requirements:

* **PNG**, **exactly 48x48 pixels**, square.
* Choose `32-bit RGBA` so any rounded corners stay transparent; fully opaque
  artwork is fine too.
* The same file is used by the 3DSX (Homebrew Launcher) and the CIA (Home Menu),
  so it shows up in both. The 3DS scales it up for the larger Home Menu
  display, so keep the artwork simple with strong contrast and avoid thin
  details or small text.
* Until the file exists, devkitPro's generic placeholder icon is used.

`sys/3ds/icon_48.png` is accepted as an alternative name. After dropping the
file in, just rebuild; the SMDH, the 3DSX and the CIA are regenerated
automatically (delete `build3ds/nethack.smdh` first if your file has an older
timestamp than an existing build).

Note: SMDH icons have no alpha channel, so transparency is not preserved.
The icon is packed by `bannertool makesmdh` when bannertool is available (the
same tool that builds the 3D banner); `smdhtool` is only the fallback.

### 3D banner (CGFX)

The Home Menu shows a rotating 3D scene from:

```
sys/3ds/banner.cgfx
```

When that file exists it wins over any PNG.  It is packed with
`bannertool makebanner -ci`, and the SMDH gets the `extendedbanner` flag, which
is what makes the Home Menu use the scene instead of a flat image.  The whole
CGFX has to stay below 512 KiB.

The checked-in `banner.cgfx` is built from `sys/3ds/banner.glb` (a cleaned-up
knight statue: node transforms baked, UVs pre-transformed, base colour only,
every submesh hung off a single `COMMON` bone, 14 units tall, centred on the
Home Menu's reference camera).  `banner.glb` itself comes from the original
model with:

```
python sys/3ds/build_banner_glb.py --source knight.glb --output sys/3ds/banner.glb \
    --no-paint --texture-size 256 --gamma 0.6 --brightness 1.0
```

and the CGFX is then regenerated (needs Python with Pillow/numpy and pycgfx in
`.tools/pycgfx`; not needed for a normal build):

```
make -f sys/3ds/Makefile.3ds banner-cgfx
```

That pipeline also runs `sys/3ds/verify_banner.py`, which checks the parts the
Home Menu is strict about: the CMDL is named `COMMON`, there is a `COMMON` bone
between `Scene root` and the meshes, no soft skinning, each vertex stream stays
under 64 KiB, every index stays inside its shape, and the texture is a valid
RGBA4 image (the format every banner known to work on real hardware uses).  It
also rebuilds the CGFX from the GLB and fails if the two are not byte-identical.

`build_glb()` merges vertices whose position, normal and UV are all equal: the
source model stores one corner per face, so the same vertex appears up to three
times.  That is lossless - equal attributes rasterise identically - and drops
the knight from 7263 to 6811 vertices (232544 bytes of vertex data).

The shipped banner is deliberately the plain statue: the model's own 2048x2048
photographic texture, its own UVs, one material, roughness 1.0.  A 256x256
RGBA4 texture is 128 KiB, which is why the CGFX lands at ~400 KiB.

The model's own UV layout cannot be repainted, because unrelated parts of the
mesh share the same texels, so `build_banner_glb.py` has an experimental
`--paint` mode that gives every triangle its own tile in a generated palette
texture (one material, one 128x128 RGBA4 texture, triplanar UVs).  It builds,
passes the checks above and renders correctly off-console, but on real hardware
that CGFX freezes the Home Menu before the banner appears.  The cause was never
established, so the shipped banner is the plain textured statue and `--paint`
must not be used for a release build.

The material is left as pycgfx builds it - its specular stage, which `--matte`
removes, is what gives the stone its highlights.  The scene light is left alone
as well: pycgfx writes ambient = diffuse = 1 with the light along -Z, which is
what the banners known to work on hardware use.  `--light-ambient` /
`--light-diffuse` can still dim it for experiments.

### Flat PNG fallback

Put it here:

```
sys/3ds/banner.png
```

Requirements: **PNG, exactly 256x128 pixels**. Alpha is supported.  This file
is ignored while `banner.cgfx` exists.  Without either file the CIA is still
built, but the banner area is empty.

Both banner forms are converted with `bannertool` (`makebanner`), which has to
be installed as `C:/devkitPro/tools/bin/bannertool.exe`:
<https://github.com/carstene1ns/3ds-bannertool/releases>
(`bannertool-*-windows.zip`). If bannertool is missing, the build prints a
warning and leaves the banner out.

bannertool refuses to build a banner without an audio track.  Put the tune in

```
sys/3ds/banner.wav
```

The build converts it with `ffmpeg` into `build3ds/banner_audio.wav` as
44.1 kHz, 16-bit stereo, trimmed to 3 seconds (the Home Menu's limit), and
passes that to `bannertool`.  If `banner.wav` is absent, the 0.25-second silent
`sys/3ds/banner_silence.wav` is used and the banner has no tune.  It is a valid
44.1 kHz stereo WAV on purpose: bannertool turns a zero-length or mono file
into a BCWAV that the Home Menu may reject, which can prevent the 3D scene from
loading at all.  If `ffmpeg` is not on `PATH`, the source WAV is used as-is;
set `FFMPEG=` on the make command line to point at a specific executable.

The supplied tune is already 16 kHz, so the 44.1 kHz conversion adds no detail.
To pass the source straight to `bannertool` instead:

```
make -f sys/3ds/Makefile.3ds BANNER_AUDIO=sys/3ds/banner.wav
```

That produces a 16 kHz stereo CWAV and a smaller BNR (roughly 447 KiB instead
of 682 KiB with the current model).  It is purely a size choice; the 44.1 kHz
track is the safer one to keep.  Delete `build3ds/banner.bnr`,
`build3ds/nethack.smdh` and `nethack.cia` first so make rebuilds the banner
with the changed recipe.

The Home Menu caches banners on the SD card.  After reinstalling a CIA with a
changed banner, power the console off and on again before judging the result;
until then the old banner (or an empty one) may still be shown.

## Tilesets

The OPTIONS screen (and the in-game SETTINGS screen) offers two tilesets when
`Map display` is set to `TILES`:

* **Default** - `tiles.bmp`, generated from NetHack's own text tile art.
* **PixelHack** - `PixelHack.png`, the bundled PixelHack artwork.

The choice is stored as `tileset=0` / `tileset=1` in `3dsui.cfg`.  At run time
the game looks for `tiles.bmp` / `PixelHack.png` in `sdmc:/3ds/nethack/tiles/`
first and falls back to the copy inside RomFS.  The folder is created empty;
drop a custom tileset there to override the built-in one, or leave it empty
to read the bundled file directly from RomFS.  Nothing is unpacked onto the
SD card at startup.

`sys/3ds/PixelHack.png` is the 8-bit palette artwork, kept exactly as supplied
and packed into RomFS as a PNG.  devkitPro provides no libpng/zlib for the ARM
target, so the PNG is decoded by the small self-contained reader in
`sys/3ds/3dspng.c`, which uses the public-domain `puff` inflater
(`sys/3ds/puff.c`).  Only 8-bit, non-interlaced PNGs (palette, RGB, RGBA,
grey) are supported.

PixelHack has the room floor painted underneath every monster and object.
That baked floor hides the real terrain behind sprites and looks wrong in
corridors, dark rooms, water and so on.  The front end strips it while the
tile cache is built (`_3ds_pixelhack_cleanup()` in `sys/3ds/3dstiles.c`),
copying the reference NetHack-3D front end in `example/`: pixels that exactly
match the preset background tile (2304) are cleared, and the flat chroma
colour `#83aba2` is cleared everywhere.  There is no tolerance or feathering,
matching the reference implementation.  Ground tiles and structural art
(walls, water, lava, ...) are left untouched.

## Camera

The OPTIONS screen (and the in-game SETTINGS screen) has a `Camera` row:

* **Scroll** (default) - the traditional tty behaviour: the map only scrolls
  when the hero reaches the edge of the visible area.
* **Centered** - the hero stays in the middle of the view on every step
  (the view still stops at the edges of the level).

With graphical tiles the visible area is derived from `tile_width` /
`tile_height`, and the unused part of the 400x240 top screen is split evenly
around the tile grid, so the camera is centred correctly at every tile size,
not only at the default 16x16.  In symbols/ASCII mode the map instead uses the
`Symbol width` / `Symbol height` rows (stored as `char_width` / `char_height`);
cells larger than the console's native 8x8 grid are scaled into the
framebuffer, so the same look-cursor highlight keeps working.  The camera
choice is stored as `camera=0` (scroll) or `camera=1` (centered) in
`config/3dsui.cfg`.

The circle pad pans the view directly, in whole map cells, without moving the
hero; once the hero moves again the camera returns to following them.  While a
look/glance command is active the circle pad moves the cursor instead.

## Options screen

The OPTIONS/SETTINGS screen is split into three collapsible categories:

* **3DS UI settings** - map display (tiles or symbols), tile or symbol width
  and height, camera, tileset (tiles only), and the `--More--` prompt.
  `More prompts = Press Enter` keeps the traditional stop; `Auto` pushes the
  message straight into the log without waiting for a keypress.
* **Sound** - master volume, music volume and sound volume, each 0-100%.
  Master scales both groups; music defaults to 80% and effects to 100%.
  Changes are audible immediately.  The music row controls the gameplay
  loop only; the menu music always plays at the master level.
* **NetHack settings** - the useful game options that otherwise have to be
  written into `NetHack.cnf` (in `config/`): tutorial prompt (ask every game /
  always / never), experience and equipment status fields, autopickup,
  autoopen, safe pet, attack confirmations, hints, command assistance, wall
  feedback, inverse video, pet and pile highlighting (pile highlighting also
  needs inverse video), loot sorting, and damage messages.

A opens a dropdown list for ordinary rows.  The two size rows and the three
volume rows also respond to left/right: sizes step through the preset list,
volumes change by 5% (clamped at 0 and 100).  Categories open and close with
A, and start collapsed every time the screen is opened.  With everything
open the list is taller than the screen, so it scrolls to keep the cursor
visible.  The choices are saved immediately to `config/3dsui.cfg` and
take effect on the next turn (game options), immediately (volumes), or when
the screen is closed (display size and tileset changes).  The options screen
is applied after `NetHack.cnf`, so its values win for the options it exposes;
everything else still comes from the config file.

The volumes are stored as `master_volume`, `music_volume` and `sfx_volume` in
`config/3dsui.cfg`.

## Controls and status

* **D-pad** - move the hero (hold to repeat).  Holding **L** runs: the port
  sends the `G` run prefix followed by the direction, so it works with both
  digit and letter movement keys.
* **Circle pad** - pan the map view, or move the look cursor during a
  look/glance command (hold to repeat).
* **A / B / X / Y** - Enter / Escape / inventory / search.  On the map,
  **A** opens the context menu described below instead of Enter.  **Y**
  searches (like `s`) and repeats while held.  While a look/glance/target
  cursor is active, **A** confirms the tile under the cursor (like `.`),
  and a waiting `--More--` is confirmed with the same button.
* **START / SELECT** - pause menu / help.
* **Touch screen** - the soft keyboard and quick-command grid; holding a
  character key (including the digit row) repeats it.  The digit row uses
  number-pad directions, so `1` and `3` are the down-left and down-right
  diagonals.  The port forces `number_pad:1` so this works even if an older
  `NetHack.cnf` in `config/` says otherwise.

Look/glance commands (`;`, `:`, `m`, ...) always show the cursor: the tiled map
gets a bright frame around the current tile, and ASCII mode highlights the
cell background in yellow under the glyph.  The bottom 8-pixel line of the top
screen is reserved for the hero summary (name and rank title, HP, power and
gold; it turns red when HP is low).  The touch screen carries a compact
three-line status block: all six ability scores on the first line; alignment,
AC, experience, the turn counter, hit dice and dungeon level on the second;
and the hero's current status on the third (hunger, conditions such as
poisoning or blindness, encumbrance, and equipment/terrain or score when
those options are enabled).  The two screens do not duplicate the same
fields.

The latest messages are also mirrored into a message log on the touch screen,
just below the soft-keyboard toggle button, so the log stays visible while
the map fills the top screen.  The log runs down to the bottom edge of the
screen, newest messages on top, with long messages wrapped to the strip's
width; the soft keyboard sits on top of it when rolled out, without hiding
it.  Ordinary messages are not shown on the top screen anymore (only
questions, `--More--` and cursor feedback stay there).  Textual discoveries
(engravings, headstones, ...) open their own window with an OK button.

## Context menu

With the map visible, **A** opens a menu of everything the hero can do on
their own square and on the eight surrounding squares.  The hero's own
square is listed first, so the actions for the object under the hero
(stairs, a container, a fountain, ...) come before the actions for the ring
around him.  The offered actions follow the same rules as the NetHack-3D
port: a staircase offers ascend or descend, a door can be opened, closed,
kicked or searched, a container can be tipped, forced or applied, a corpse
or food can be picked up or eaten, a fountain or other water can be quaffed,
searched and picked up from, traps and furniture (thrones, altars, graves,
trees, bars, boulders, statues) can be searched and picked up from, walls
can be searched for secret doors, and a monster can be talked to.  "Search",
"Cast" and "Info" appear once per menu regardless of how many tiles trigger
them, and the hero's own situation always offers Swap weapons, Two-weapon
combat, Attributes and Look at what is here.  Kicking a door asks how many
kicks to perform (once, a count chosen with left/right, or until the door
breaks).  Entries name the direction they belong to (for example `Open the
door (east)`) and the list scrolls with the D-pad or the touch screen.
Choosing an action that belongs to a neighbouring object walks the hero onto
that square first when needed (staircases, fountains, picking up and eating),
while containers and doors are worked on from where the hero stands.  Once
the menu is open, **A** chooses, **B** cancels, and **START** confirms a
multiple-selection list.  While in a prompt, `--More--`, a direction
question or a look command, **A** keeps its ordinary Enter meaning.

## Menus and inventory

Every in-game menu (inventory, pick-up and drop lists, item action menus,
spell lists, and read-only information pages) is drawn as a scrollable list on
the top screen rather than a tty page that has to be answered with a typed
letter:

* **D-pad up/down** moves the cursor one row, **left/right** also work, and
  **L/R** move a page at a time.
* **A** chooses the highlighted row; in a multiple-selection menu it toggles
  the row and moves on, and **START** confirms.
* **B** cancels the menu, restoring the previous selection state.
* **Touch** - tapping a row highlights it, tapping it again activates it.
  Read-only pages scroll a page at a time.
* Inventory rows carry the object's tile icon; headings are dimmed and skipped
  by the cursor.

Choosing an item from the inventory opens that item's action menu, so using,
dropping, eating or examining an object never needs the soft keyboard.  The
menu offers the same category-filtered action set as the NetHack-3D port
(apply, invoke, tip, loot, drop, pay, eat, quaff, read, rub, throw, wield,
quiver, wear, take off, put on, remove, zap, untrap, offer, name, call,
adjust, engrave, dip and info), with each action shown only when it makes
sense for that object (potion → quaff, food → eat, scroll/spellbook → read,
wand → zap, armor → wear/take off, ring/amulet → put on/remove, weapon →
wield/throw/quiver, container → tip/loot/apply, unpaid shop item → pay, and
so on) and with the item's primary action listed first (potion → quaff
before drop, and so on).  Apply is offered for every item.  "Info" opens a
read-only information page in its own window, closed with A or B.  Any
"which object?" question (throw, drop, engrave, fire, apply, eat, wield,
...) is presented as a scrollable list on the top screen instead of asking
for a typed letter on the soft keyboard.  When the game asks for text to
type (engraving, calling/renaming, ...), the soft keyboard opens
automatically.

## Combat feedback

When a creature is hit its tile flashes red for a moment; the hero's tile does
the same when the hero takes damage.  With graphical tiles the hit also throws
a short spray of blood droplets away from the attacker, and the square just
beyond the victim keeps a permanent stain (blood is only left on ordinary room
or corridor floor, never on walls, doors or other terrain).  The stain is drawn
underneath objects, stairs and creatures, and it is forgotten when the hero
changes level.  In symbols/ASCII mode only the cell background flashes red and
no droplets are drawn, so the map stays readable.

## Sound

Sound effects go through NetHack's own soundlib interface (see
`doc/sound.txt` and `include/sndprocs.h`), so the usual options work:

```
OPTIONS=soundlib:sound3ds     # default; the 3DS libctru/CSND backend
OPTIONS=!sounds               # mute all effects
```

The sound library is registered before `NetHack.cnf` is read, so a
`soundlib:` line in the config can still override it (`nosound` is the only
other backend compiled in).  A deaf hero suppresses the sounds the hero
would have to hear; the few cues that are the player's own feedback
(footsteps, level-up) keep playing.

### Pack layout

The build reads effects from `soundpack/new/<key>/<n>.mp3` when that folder
exists: `0` is the base sound and `1..` are variations.  Each file is decoded
to 16-bit mono 22050 Hz PCM and normalized at build time.  The loudest 50 ms
of every clip is driven to -3 dBFS, and a limiter keeps peaks below about
-1 dBFS.  Peak-heavy clips stop a few dB short of the target once their
transients reach the limiter, while quiet recordings are boosted by up to
50 dB; the result is much closer in level than the raw pack.  All variations
get the same level; the old per-file volume values
in `manifest.json` are not used for this tree.  The port also plays every
effect trigger at that level: cues NetHack would normally make quieter
(eating, missed attacks, faint splashes, door kicks) are not attenuated, so
one effect is not drowned out by another.

If `soundpack/new` is absent, the script falls back to the manifest-based
pack (`archivePath` plus `volume`), which is how the original pack worked.

Inside the game, each key maps to one base sound and any number of
variations:

```
sounds/<key>/0.wav       base
sounds/<key>/1.wav       variation 1
sounds/<key>/2.wav       variation 2, ...
```

One variation is picked at random for every play.  A variation whose file is
missing falls back to `0.wav`; a key with no file at all is simply silent.
Files must be plain 16-bit mono PCM WAV (22050 Hz is what the build writes;
other rates are played at their own rate).  Anything over 512 KiB is dropped
instead of loaded.  Effects are decoded once at startup and kept in RAM (up
to 4 MB), so fast repeated actions do not read from storage again; keep them
short - a second or two each is plenty.

Lookup order at playback time:

```
sdmc:/3ds/nethack/sounds/<key>/<n>.wav    player's own, on the SD card
/3ds/nethack/sounds/<key>/<n>.wav         same, relative to the working directory
romfs:/sounds/<key>/<n>.wav               the pack built into the game
```

### Replacing the sounds

Nothing has to be rebuilt to change a sound.  The built-in pack is read
directly from RomFS; the game does not unpack it onto the SD card.  To
override one sound, create the matching file under
`sdmc:/3ds/nethack/sounds/`; the lookup checks the SD card before RomFS:

```
sdmc:/3ds/nethack/sounds/door-opens/0.wav
sdmc:/3ds/nethack/sounds/hit/0.wav
sdmc:/3ds/nethack/sounds/hit/1.wav
```

Only the files you place there are used from the SD card; everything else
falls back to the built-in pack.  Delete a file to use the built-in version
again.  A file must be a plain 16-bit mono PCM WAV (the same format the
build writes).  The folder is created empty on startup.  Older builds used
to fill it with a copy of the pack, so if it already contains such a copy,
delete it once to hear the current built-in sounds.

Effects are cached in RAM after the sound engine starts, so replace files
before launching the game: changing a WAV while it is running takes effect
only after a restart.  The built-in pack fits in the 4 MB cache; if custom
files push the total over that, the remaining effects are loaded on demand.

One CSND channel is reserved for the music (menu or gameplay), so effects
overlap on the remaining eleven; sounds layer on top of each other, and
only when every effect channel is busy is the oldest one cut off to make
room for a new one.

### Background music

Two looping tracks are built in: one for the main menu and character
creation, one for gameplay.

The menu track starts when the front end's menu appears, keeps playing while
the character is rolled, and is replaced by the gameplay loop when the first
turn begins (`moveloop`).  It starts again after the game returns to the
main menu.

Because the 3DS plays PCM rather than MP3, each bundled track is decoded at
build time into a loop that fits the roughly 2 MB budget:

```
romfs/music/menu.wav        22050 Hz, 16-bit mono, 45 s (about 1.98 MB)
romfs/music/game.wav        22050 Hz, 16-bit mono, ~35 s (about 1.5 MB)
```

`sys/3ds/build_sounds.py` turns `soundpack/menu.mp3` into the menu loop and
`soundpack/loop.mp3` into the gameplay loop.  Each takes 45 seconds of the
source after its fade-in (the first 4 seconds are skipped, so the loop does
not run into the near-silent intro) and crossfades the tail into the 0.75
seconds preceding the loop point, so the hardware loop wraps without a click
or a silent gap.  Both loops are normalized to -5 dBFS loudness.  If the
source MP3 is shorter, the loop is shortened accordingly.  CD-quality
stereo would be about five times larger for the same duration, so the loops
are mono and cut; the full tracks are not stored on the 3DS.

Like the effects, the loops are read directly from RomFS.  To replace one,
create `sdmc:/3ds/nethack/music/menu.wav` or `game.wav`; the SD file is
tried first:

```
sdmc:/3ds/nethack/music/menu.wav     player's own, 16-bit mono PCM, <= 3 MB
sdmc:/3ds/nethack/music/game.wav     player's own, 16-bit mono PCM, <= 3 MB
/3ds/nethack/music/<name>.wav        same, relative to the working directory
romfs:/music/<name>.wav              the loop built into the game
```

Delete the SD file to fall back to the built-in loop.  A replacement may be
longer (up to 3 MB) or a different sample rate; the engine plays it at its
own rate.  The music volume row in the settings screen controls the gameplay
loop only, and changes take effect without restarting the track; the menu
track always plays at the master volume.

### Rebuilding the bundled pack

The pack in `soundpack/` is the source for the sounds inside `romfs/`.  The
preferred layout is `soundpack/new/<key>/<n>.mp3`; if it is absent, the
script falls back to `soundpack/manifest.json` (schema `nh3d-soundpack`,
versions 1 and 2), which lists each key, its base file, its variations and
their volumes.  After replacing the MP3s:

```
make -f sys/3ds/Makefile.3ds sounds
```

That runs `sys/3ds/build_sounds.py`, which converts every effect to 16-bit
mono 22050 Hz PCM (`ffmpeg`, so ffmpeg has to be on `PATH` or passed as
`FFMPEG=...`), normalizes their loudness, writes `romfs/sounds/`, and
regenerates the key table in `build3ds/sndpack.h`.  If `soundpack/menu.mp3`
(or `menu.ogg`/`menu.wav`/`menu.flac`) is present, it also writes the
looping `romfs/music/menu.wav`; `soundpack/loop.mp3` becomes the gameplay
loop `romfs/music/game.wav`.  The normal build re-runs it whenever the
manifest or any MP3 changes, and always after `make ... sounds` because that
target deletes the outputs first.  Entries with an empty path are skipped,
so a manifest may list keys that are not supplied yet.  Without a manifest
or a `new` tree, or without ffmpeg, the build prints a warning and still
succeeds, producing a silent game; a missing music source just leaves the
corresponding track quiet.

The key table is the single source of truth for what can be played; if you
add a key there (or in the manifest) and want it triggered, either give it a
soundlib id mapping in `sys/3ds/3dssnd.c` or call `_3ds_sfx("<key>", volume)`
from the relevant core file.

### Keys

The bundled pack provides these keys.  "Soundlib" means the trigger arrives
through the core's sound interface (sound effect ids / achievements);
"port hook" means the port calls `_3ds_sfx()` directly from the core code.

| Key | Files | Trigger |
|-----|-------|---------|
| `player-walk` | 3 | port hook: every step the hero takes |
| `boulder-push` | 2 | port hook: pushing a boulder |
| `hit` | 3 | port hook: a melee attack connects — hero, monster, pet, or a kick |
| `missed-attack` | 5 | port hook: an attack misses — hero, pet, or a kick |
| `thrown-weapon` | 3 | port hook: throwing or firing a missile |
| `monster-killed` | 3 | port hook: the hero kills a monster in view |
| `monster-killed-other` | 1 | port hook: another monster dies in view |
| `door-opens` | 1 | port hook: the hero opens a door |
| `door-closes` | 1 | port hook: the hero closes a door |
| `door-distant` | 2 | port hook: a door opens somewhere out of sight |
| `door-kick` | 3 | port hook: a kick lands but the door holds |
| `door-smash` | 1 | soundlib: a door breaks (crashes open or shatters) |
| `unlock` | 1 | soundlib + port hook: a lock gives way |
| `force-chest` | 1 | port hook: forcing a locked chest succeeds |
| `pay` | 1 | port hook: gold changes hands to a shopkeeper |
| `swap` | 1 | port hook: the #swap command succeeds |
| `walk-down-stairs` | 1 | port hook: descending |
| `walk-up-stairs` | 1 | port hook: climbing |
| `eating` | 4 | port hook: starting or resuming a meal |
| `drink` | 1 | port hook: drinking from a fountain |
| `quaff-potion` | 2 | port hook: drinking a potion |
| `pickup-gold` | 2 | port hook: picking up gold |
| `pickup-item` | 3 | port hook: picking up anything else |
| `find-hidden` | 2 | port hook: searching out a secret |
| `thunder` | 1 | soundlib: distant thunder (killing a tame pet) |
| `counting-coins` | 1 | port hook: a vault's gold being counted |
| `footsteps` | 1 | soundlib: the footsteps of a guard on patrol |
| `level-up` | 1 | soundlib achievement: experience level gain |
| `splash` | 3 | soundlib: entering water, dipping, splashing |

A file in `sdmc:/3ds/nethack/sounds/` overrides the bundled copy for that key.
A silent or malformed WAV is rejected and the lookup continues to the next
location, but a valid older WAV still wins; remove the SD folder when testing
a new build.

## Saves and settings

Both builds use the same files on the SD card, so a save made with the 3DSX
opens in the CIA build and vice versa:

```
sdmc:/3ds/nethack/            nhdat (game data) and the folders below
sdmc:/3ds/nethack/save/       saved games
sdmc:/3ds/nethack/bones/      bones files (unexplored levels of dead heroes)
sdmc:/3ds/nethack/levels/     transient per-game level files (wiped at start)
sdmc:/3ds/nethack/tiles/      optional custom tilesets (tiles.bmp, PixelHack.png)
sdmc:/3ds/nethack/sounds/     optional custom sound effects (<key>/<n>.wav)
sdmc:/3ds/nethack/music/      optional custom music (menu.wav, game.wav)
sdmc:/3ds/nethack/config/     NetHack.cnf, 3dsui.cfg, scores
```

The `save/`, `bones/`, `tiles/`, `sounds/`, `music/` and `config/` folders are created
automatically on first run.  The asset folders are left empty; the bundled
tiles, effects and music are read from RomFS unless a replacement is placed
there.  On startup the game also sweeps the `levels/`
folder and removes stale `*_99.*` lock/level files that older builds left in
the nethack root, so the top-level folder never fills up with junk (a
cluttered folder makes the FAT layer noticeably slow on the 3DS).

The CIA title id is `0004000000A3D000` (see `UniqueId` in
`sys/3ds/nethack.rsf`); reinstalling replaces the previous version in place.
The RSF also declares the access descriptor: the game needs the `DirectSdmc`
and `DirectSdmcWrite` filesystem rights to write those files, which is why the
descriptor is written out in full instead of using a makerom preset.

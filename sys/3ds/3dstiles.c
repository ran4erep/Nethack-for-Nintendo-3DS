/* NetHack 5.0	3dstiles.c */
/* Nintendo 3DS tile renderer.
 *
 * Draws graphical tiles on the top screen by blitting RGB565 pixels
 * directly into the libctru console framebuffer.  consoleInit() sets the
 * top screen to GSP_RGB565_OES, so the u16 framebuffer is shared with the
 * character console; tiles and text mix cleanly in the same buffer.
 *
 * The default tileset is generated at build time from NetHack's bundled
 * text tile art (the *.txt files under win/share) into src/tiles.bmp by
 * win/share/tile2bmp.c.  On the 3DS both that bitmap and the bundled
 * PixelHack.png are read with the portable tileset reader (win/share/
 * tileset.c, plus the self-contained PNG decoder in 3dspng.c), resampled to
 * the requested tile size and cached as RGB565.  The options screen can
 * switch to PixelHack instead; _3ds_set_tileset() below reloads the cache
 * when that choice changes.
 *
 * PixelHack's author painted the room floor underneath every monster and
 * object sprite.  NetHack already draws the real terrain as the background
 * glyph, so that baked floor hides it (and looks especially wrong in dark
 * rooms, corridors and water).  _3ds_pixelhack_cleanup() strips it while the
 * cache is built, copying the reference NetHack-3D front end (see the
 * example/ tree): pixels that exactly match the preset background tile are
 * cleared, and the flat chroma colour #83aba2 is cleared everywhere.  Ground
 * tiles and structural art (walls, water, lava, ...) are left untouched.
 *
 * The map is laid out on a grid of tile_width x tile_height pixels which is
 * independent of the 8x8 character console.  wintty.c clips the tty map
 * window to however many tile cells fit on screen (see setclipped()), and
 * this file blits each cell at its pixel position.  Status and message text
 * keep using the normal character grid at the top and bottom of the screen.
 *
 * Tile size is controlled from the config file:
 *     OPTIONS=tile_width:16
 *     OPTIONS=tile_height:16
 * It defaults to 16x16 when left unset.
 *
 * NetHack may be freely redistributed.  See license for details.
 */

#ifdef NINTENDO_3DS

#include "hack.h"
#include "tileset.h"
#include <3ds.h>
#include <stdlib.h>
#include <string.h>

extern PrintConsole topConsole;

/* ASCII map cursor (see the end of this file).  It is shared by the tiled
   and non-tiled renderers, so it lives outside TILES_IN_GLYPHMAP. */
static void _3ds_ascii_cursor(int vcol, int vrow, int show);
static void _3ds_ascii_cursor_invalidate(void);
static void _3ds_ascii_cursor_refresh(int vcol, int vrow);

#ifdef TILES_IN_GLYPHMAP

extern int total_tiles_used; /* from src/tile.c */

#define _3DS_SCR_W 400        /* top screen width in pixels  */
#define _3DS_SCR_H 240        /* top screen height in pixels */
#define _3DS_MAP_TOP 8        /* first map row (below the 8px message line) */
#define _3DS_STATUS_H 8       /* bottom line reserved for hero status     */
#define _3DS_TILE_DEFAULT 16  /* default tile size when not configured */
#define _3DS_TILE_MIN 8       /* never go below one console cell */

#define TILES_PER_ROW 40

/* One cached cell of the visible map, used both for the glance/look
   cursor and to put the cell back when the cursor leaves it. */
#define _3DS_VIEW_MAX_COLS 64
#define _3DS_VIEW_MAX_ROWS 32

static int cell_fg[_3DS_VIEW_MAX_COLS][_3DS_VIEW_MAX_ROWS];
static int cell_bg[_3DS_VIEW_MAX_COLS][_3DS_VIEW_MAX_ROWS];
static int cell_under[_3DS_VIEW_MAX_COLS][_3DS_VIEW_MAX_ROWS];
static boolean cell_valid[_3DS_VIEW_MAX_COLS][_3DS_VIEW_MAX_ROWS];
static int cursor_col = -1, cursor_row = -1;
static boolean cursor_shown = FALSE;

/* Graphical map cells also remember which map square they show, so that a
   hit reported in map coordinates can be painted without asking wintty.c
   to translate it again. */
static int cell_mapx[_3DS_VIEW_MAX_COLS][_3DS_VIEW_MAX_ROWS];
static int cell_mapy[_3DS_VIEW_MAX_COLS][_3DS_VIEW_MAX_ROWS];

/* A freshly hit creature's cell blinks red for a handful of frames.  The
   cell is simply tinted in the framebuffer (which works for graphical tiles
   and for both ASCII paths alike).  When the flash expires the core is asked
   to update that square, then the row is redrawn from the core's glyph
   buffer: a monster that died or moved during the flash cannot be
   resurrected from a stale copy of the pixels, and the tint goes away even
   while the game is still waiting for a key. */
#define _3DS_FLASH_FRAMES 12
static boolean flash_on = FALSE;
static int flash_mapx = -1, flash_mapy = -1;
static int flash_frames = 0;
static int flash_phase = 0; /* 1 = currently tinted, 0 = plain tile */

/* Persistent blood, one level per map square.  It survives until the hero
   changes level and is painted between the terrain and whatever object,
   stair or creature stands on the square. */
static unsigned char blood_amt[COLNO][ROWNO];

/* When non-zero, a potion-shatter stain replaces the default red droplets on
   that square with the potion's own colour. */
static u16 stain_color[COLNO][ROWNO];
static int blood_dnum = -1, blood_dlevel = -1;

/* Short-lived droplets that spurt away from the attacker in tiles mode.
   They are purely decorative; the stain they leave behind is the persistent
   blood above. */
#define _3DS_MAX_PARTICLES 24
struct _3ds_particle {
    float px, py, vx, vy;
    int life;
};
static struct _3ds_particle particles[_3DS_MAX_PARTICLES];
static int particle_count = 0;

static void _3ds_ascii_cell_size(int *, int *);
static boolean _3ds_map_cell_rect(int, int, int *, int *, int *, int *);
static void _3ds_repaint_cell(int, int);
static void _3ds_draw_blood(int, int, int, int, int, int);

static u16 *tile_cache = NULL; /* ntiles * cache_w * cache_h RGB565 */
static u8 *tile_alpha = NULL;  /* ntiles * cache_w * cache_h, 1 == opaque */
static int ntiles = 0;
static int cache_w = 0, cache_h = 0;

static char tiles_path[160] = ""; /* where the tileset was found */
static boolean tiles_ready = FALSE;

/* Which tileset the player picked in the 3DS settings screen:
 *   0 = the built-in tileset (tiles.bmp, generated from NetHack's own art)
 *   1 = PixelHack (PixelHack.png, bundled in RomFS)
 * `_3ds_set_tileset()` below switches between them. */
static int tileset_choice = 0;

/* defined below; the look cursor needs it before the cache is built */
boolean _3ds_tiles_active(void);

static const char *const default_tileset_paths[] = {
    "sdmc:/3ds/nethack/tiles/tiles.bmp",
    "/3ds/nethack/tiles/tiles.bmp",
    "sdmc:/3ds/nethack/tiles.bmp",
    "romfs:/tiles.bmp",
    NULL
};
static const char *const pixelhack_tileset_paths[] = {
    "sdmc:/3ds/nethack/tiles/PixelHack.png",
    "/3ds/nethack/tiles/PixelHack.png",
    "sdmc:/3ds/nethack/PixelHack.png",
    "romfs:/PixelHack.png",
    NULL
};

/* --------------------------------------------------------------------- */

static u16
rgb565(unsigned char r, unsigned char g, unsigned char b)
{
    return (u16) ((((unsigned) r >> 3) << 11)
                  | (((unsigned) g >> 2) << 5) | ((unsigned) b >> 3));
}

/* Effective pixel size of one map cell.  tile_width/tile_height come from
   the config file; a value of 0 (unset) means "use the default". */
void
_3ds_tile_size(int *w, int *h)
{
    int tw = iflags.wc_tile_width;
    int th = iflags.wc_tile_height;

    if (tw <= 0)
        tw = _3DS_TILE_DEFAULT;
    if (th <= 0)
        th = _3DS_TILE_DEFAULT;
    if (tw < _3DS_TILE_MIN)
        tw = _3DS_TILE_MIN;
    if (th < _3DS_TILE_MIN)
        th = _3DS_TILE_MIN;
    if (tw > _3DS_SCR_W)
        tw = _3DS_SCR_W;
    if (th > _3DS_SCR_H)
        th = _3DS_SCR_H;
    *w = tw;
    *h = th;
}

/* Number of tile columns that fit across the top screen. */
int
_3ds_map_view_cols(void)
{
    int tw, th;

    _3ds_tile_size(&tw, &th);
    return _3DS_SCR_W / tw;
}

/* Number of tile rows that fit on the top screen below the message line.
   The last 8-pixel line is reserved for the hero's vital statistics (see
   _3ds_art_top_status()), so the map never covers it. */
int
_3ds_map_view_rows(void)
{
    int tw, th, avail;

    _3ds_tile_size(&tw, &th);
    avail = _3DS_SCR_H - _3DS_MAP_TOP - _3DS_STATUS_H;
    if (avail < th)
        avail = th;
    return avail / th;
}

/* Pixel origin of the tile grid.  Whole tiles are kept (the camera scrolls
   in cells, not pixels), but the part of the screen which no whole tile
   covers is split evenly around the map, so the view is centred at every
   tile size instead of being pinned to the top-left.  With the default
   16x16 tiles this is exactly the old origin (0, 8). */
static void
_3ds_map_origin(int *ox, int *oy)
{
    int tw, th, vw, vh, top, bottom;

    _3ds_tile_size(&tw, &th);
    vw = _3ds_map_view_cols();
    if (vw > COLNO - 1)
        vw = COLNO - 1;
    vh = min(_3ds_map_view_rows(), ROWNO);
    *ox = (_3DS_SCR_W - vw * tw) / 2;
    /* centre the grid in the band between the message line and the
       reserved hero-status line */
    top = _3DS_MAP_TOP;
    bottom = _3DS_SCR_H - _3DS_STATUS_H;
    *oy = top + (bottom - top - vh * th) / 2;
    if (*oy < top)
        *oy = top;
}

/* --------------------------------------------------------------------- */
/* Raw framebuffer access.  The 3DS top screen is stored rotated, so the
   pixel at (px,py) lives at fb[px * 240 + (239 - py)]. */

static void
_3ds_fill_block(int px, int py, int w, int h, u16 color)
{
    u16 *fb = topConsole.frameBuffer;
    u16 *base;
    int x, y;

    if (!fb)
        return;
    base = fb + px * _3DS_SCR_H + (_3DS_SCR_H - 1 - py);
    for (y = 0; y < h; y++) {
        u16 *row = base - y;

        for (x = 0; x < w; x++)
            row[x * _3DS_SCR_H] = color;
    }
}

/* Copy one cached tile, skipping transparent (palette index 0) pixels. */
static void
_3ds_blit_block(int px, int py, int tileidx, int w, int h)
{
    u16 *fb = topConsole.frameBuffer;
    const u16 *src;
    const u8 *msk;
    u16 *base;
    int x, y;

    if (!fb || !tile_cache || tileidx < 0 || tileidx >= ntiles)
        return;

    base = fb + px * _3DS_SCR_H + (_3DS_SCR_H - 1 - py);
    src = &tile_cache[(size_t) tileidx * w * h];
    msk = &tile_alpha[(size_t) tileidx * w * h];
    for (y = 0; y < h; y++) {
        u16 *row = base - y;
        const u16 *srow = src + y * w;
        const u8 *mrow = msk + y * w;

        for (x = 0; x < w; x++)
            if (mrow[x])
                row[x * _3DS_SCR_H] = srow[x];
    }
}

/* Draw a small icon (for menus) from the tile cache, scaled down to the
   requested size.  Kept separate from _3ds_blit_block() because the menu
   wants one tile squeezed into an 8-pixel text row. */
void
_3ds_draw_icon(int px, int py, int size, int tileidx)
{
    u16 *fb = topConsole.frameBuffer;
    const u16 *src;
    const u8 *msk;
    int x, y;

    if (!fb || !tile_cache || tileidx < 0 || tileidx >= ntiles
        || size < 1 || cache_w < 1 || cache_h < 1)
        return;
    src = &tile_cache[(size_t) tileidx * cache_w * cache_h];
    msk = &tile_alpha[(size_t) tileidx * cache_w * cache_h];
    for (y = 0; y < size; y++) {
        int sy = (y * cache_h) / size;
        int pxx, pyy = py + y;

        if ((unsigned) pyy >= (unsigned) _3DS_SCR_H)
            continue;
        for (x = 0; x < size; x++) {
            int sx = (x * cache_w) / size;

            if (!msk[sy * cache_w + sx])
                continue;
            pxx = px + x;
            if ((unsigned) pxx >= (unsigned) _3DS_SCR_W)
                continue;
            fb[pxx * _3DS_SCR_H + (_3DS_SCR_H - 1 - pyy)]
                = src[sy * cache_w + sx];
        }
    }
}

/* --------------------------------------------------------------------- */
/* Map cursor for glance/look/far-look.                                  */
/*                                                                       */
/* The core moves the tty cursor with curs(WIN_MAP,x,y); the 3DS console */
/* has no visible hardware cursor, and with graphical tiles the terminal */
/* cell positions do not even line up with the tile grid.  Instead the    */
/* tile under the cursor is remembered and a bright frame is painted     */
/* around that tile, so the player can see exactly where a look command  */
/* is aimed.  Putting the cell back is a matter of re-blitting the two   */
/* cached tile indices (background and foreground).                      */

/* Throw away every shadowed map cell (used when the whole view is about to
   change or be repainted). */
static void
_3ds_reset_view_cache(void)
{
    int c, r;

    for (c = 0; c < _3DS_VIEW_MAX_COLS; c++)
        for (r = 0; r < _3DS_VIEW_MAX_ROWS; r++) {
            cell_fg[c][r] = -1;
            cell_bg[c][r] = -1;
            cell_under[c][r] = -1;
            cell_mapx[c][r] = cell_mapy[c][r] = 0;
            cell_valid[c][r] = FALSE;
        }
    cursor_col = cursor_row = -1;
    cursor_shown = FALSE;
}

/* --------------------------------------------------------------------- */
/* Hit flashes and blood.                                                */
/*                                                                       */
/* Combat code reports hits in map coordinates; these helpers translate  */
/* them to visible cells and paint the framebuffer directly.  Nothing is */
/* allowed to touch the game's RNG: the display RNG is used for cosmetic */
/* particle spread and for the stain pattern.                            */
/* --------------------------------------------------------------------- */

static void
_3ds_blood_check_level(void)
{
    if (blood_dnum != u.uz.dnum || blood_dlevel != u.uz.dlevel) {
        memset(blood_amt, 0, sizeof blood_amt);
        memset(stain_color, 0, sizeof stain_color);
        blood_dnum = u.uz.dnum;
        blood_dlevel = u.uz.dlevel;
        particle_count = 0;
    }
}

/* Pixel rectangle of a map square in the current view, if it is on
   screen.  Works for every map mode, including plain 8x8 ASCII. */
static boolean
_3ds_map_cell_rect(int mapx, int mapy, int *px, int *py, int *pw, int *ph)
{
    int vcol, vrow, cx, cy, tw, th, ox, oy;

    if (!topConsole.frameBuffer || !isok(mapx, mapy))
        return FALSE;
    _3ds_map_view_offset(&cx, &cy);
    vcol = (mapx - 1) - cx;
    vrow = mapy - cy;
    if (vcol < 0 || vrow < 0)
        return FALSE;

    if (iflags.wc_tiled_map) {
        if (!_3ds_tiles_active())
            return FALSE;
        _3ds_tile_size(&tw, &th);
        _3ds_map_origin(&ox, &oy);
        *px = ox + vcol * tw;
        *py = oy + vrow * th;
    } else {
        _3ds_ascii_cell_size(&tw, &th);
        *px = vcol * tw;
        *py = _3DS_MAP_TOP + vrow * th;
    }
    *pw = tw;
    *ph = th;
    if (*px < 0 || *py < 0 || *px + tw > _3DS_SCR_W
        || *py + th > _3DS_SCR_H)
        return FALSE;
    return TRUE;
}

/* Pull a rectangle of pixels towards red, leaving enough of the original
   tile visible that the player can still tell what was hit.  In character
   mode only the cell background changes colour: the glyph pixels keep
   their own colour so the symbol stays readable. */
static void
_3ds_tint_block(int px, int py, int w, int h, boolean bg_only)
{
    u16 *fb = topConsole.frameBuffer;
    u16 bg = 0;
    int x, y;

    if (!fb)
        return;
    if (bg_only && w > 0 && h > 0 && (unsigned) px < (unsigned) _3DS_SCR_W
        && (unsigned) py < (unsigned) _3DS_SCR_H)
        bg = fb[px * _3DS_SCR_H + (_3DS_SCR_H - 1 - py)];
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++) {
            int pxx = px + x, pyy = py + y;
            u16 *p, c;
            unsigned r, g, b;

            if ((unsigned) pxx >= (unsigned) _3DS_SCR_W
                || (unsigned) pyy >= (unsigned) _3DS_SCR_H)
                continue;
            p = &fb[pxx * _3DS_SCR_H + (_3DS_SCR_H - 1 - pyy)];
            c = *p;
            if (bg_only && c != bg)
                continue;
            r = (c >> 11) & 0x1f;
            g = (c >> 5) & 0x3f;
            b = c & 0x1f;
            r = (r * 5) / 8 + 20;
            if (r > 31)
                r = 31;
            g = (g * 5) / 16;
            b = (b * 5) / 16;
            *p = (u16) ((r << 11) | (g << 5) | b);
        }
}

/* Tint only the glyph (foreground) pixels of a character-mode cell, leaving
   the background alone, so the symbol itself flashes red instead of the
   empty background square around it. */
static void
_3ds_tint_glyph(int px, int py, int w, int h)
{
    u16 *fb = topConsole.frameBuffer;
    u16 bg = 0;
    int x, y;

    if (!fb || w <= 0 || h <= 0)
        return;
    if ((unsigned) px < (unsigned) _3DS_SCR_W
        && (unsigned) py < (unsigned) _3DS_SCR_H)
        bg = fb[px * _3DS_SCR_H + (_3DS_SCR_H - 1 - py)];
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++) {
            int pxx = px + x, pyy = py + y;
            u16 *p, c;
            unsigned r, g, b;

            if ((unsigned) pxx >= (unsigned) _3DS_SCR_W
                || (unsigned) pyy >= (unsigned) _3DS_SCR_H)
                continue;
            p = &fb[pxx * _3DS_SCR_H + (_3DS_SCR_H - 1 - pyy)];
            c = *p;
            if (c == bg)
                continue; /* background pixel: leave it alone */
            r = (c >> 11) & 0x1f;
            g = (c >> 5) & 0x3f;
            b = c & 0x1f;
            r = (r * 5) / 8 + 20;
            if (r > 31)
                r = 31;
            g = (g * 5) / 16;
            b = (b * 5) / 16;
            *p = (u16) ((r << 11) | (g << 5) | b);
        }
}

/* Tint only the opaque (sprite) pixels of a tile, so the flash makes the
   creature itself glow red while the terrain background stays put. */
static void
_3ds_tint_sprite(int px, int py, int tw, int th, int tileidx)
{
    u16 *fb = topConsole.frameBuffer;
    const u8 *msk;
    int x, y;

    if (!fb || !tile_cache || tileidx < 0 || tileidx >= ntiles)
        return;
    msk = &tile_alpha[(size_t) tileidx * tw * th];
    for (y = 0; y < th; y++) {
        int pyy = py + y;

        for (x = 0; x < tw; x++) {
            int pxx;
            u16 *p, c;
            unsigned r, g, b;

            if (!msk[y * tw + x])
                continue;
            pxx = px + x;
            if ((unsigned) pxx >= (unsigned) _3DS_SCR_W
                || (unsigned) pyy >= (unsigned) _3DS_SCR_H)
                continue;
            p = &fb[pxx * _3DS_SCR_H + (_3DS_SCR_H - 1 - pyy)];
            c = *p;
            r = (c >> 11) & 0x1f;
            g = (c >> 5) & 0x3f;
            b = c & 0x1f;
            r = (r * 5) / 8 + 20;
            if (r > 31)
                r = 31;
            g = (g * 5) / 16;
            b = (b * 5) / 16;
            *p = (u16) ((r << 11) | (g << 5) | b);
        }
    }
}

static void
_3ds_flash_paint(void)
{
    int px, py, w, h;

    if (!flash_on || !flash_phase)
        return;
    if (iflags.wc_tiled_map) {
        /* Tiles mode: flash only the sprite (the foreground tile's opaque
           pixels) instead of the whole cell, so the background terrain does
           not turn red with the creature.  Fall back to a whole-cell tint
           if we cannot find a foreground tile to flash. */
        int c, r, tile = -1;

        for (c = 0; c < _3DS_VIEW_MAX_COLS && tile < 0; c++)
            for (r = 0; r < _3DS_VIEW_MAX_ROWS && tile < 0; r++)
                if (cell_valid[c][r] && cell_mapx[c][r] == flash_mapx
                    && cell_mapy[c][r] == flash_mapy)
                    tile = cell_fg[c][r];
        /* Tint only the creature's own opaque pixels.  No whole-cell
           fallback: that would turn the terrain underneath (a door, floor,
           ...) red along with the sprite.  If no foreground sprite can be
           found, don't flash the cell at all. */
        if (tile >= 0 && tile < ntiles)
            if (_3ds_map_cell_rect(flash_mapx, flash_mapy,
                                   &px, &py, &w, &h))
                _3ds_tint_sprite(px, py, w, h, tile);
    } else {
        if (_3ds_map_cell_rect(flash_mapx, flash_mapy, &px, &py, &w, &h))
            _3ds_tint_glyph(px, py, w, h);
    }
}

/* Repaint the flashing square from the core's glyph buffer instead of from
   our own shadow copy of it: the creature may have moved or died in the
   meantime.  The tile renderer tints it again while flash_phase is set. */
static void
_3ds_flash_repaint(void)
{
    if (isok(flash_mapx, flash_mapy))
        row_refresh(flash_mapx, flash_mapx, flash_mapy);
}

/* End the current flash.  When redraw is TRUE the core repaints that one
   square so the screen shows the creature's current state (dead, moved,
   still there) rather than whatever we tinted. */
static void
_3ds_flash_clear(boolean redraw)
{
    int x = flash_mapx, y = flash_mapy;
    boolean was_on = flash_on;

    flash_on = FALSE;
    flash_phase = 0;
    flash_frames = 0;
    flash_mapx = flash_mapy = -1;
    if (redraw && was_on && isok(x, y)) {
        /* make sure the glyph buffer reflects the creature's current state
           (it may have died or moved while the flash was running), then
           repaint the square; no tint now: flash_on is already cleared */
        newsym(x, y);
        row_refresh(x, x, y);
    }
}

/* The core tells us that a creature was just hit. */
void
_3ds_hit_flash(int mapx, int mapy)
{
    int px, py, w, h;

    _3ds_blood_check_level();
    if (!isok(mapx, mapy))
        return;
    if (flash_on && (flash_mapx != mapx || flash_mapy != mapy))
        _3ds_flash_clear(TRUE);
    flash_mapx = mapx;
    flash_mapy = mapy;
    flash_frames = _3DS_FLASH_FRAMES;
    flash_phase = 1;
    flash_on = _3ds_map_cell_rect(mapx, mapy, &px, &py, &w, &h);
    if (flash_on)
        _3ds_flash_paint();
}

/* Deterministic pseudo-random pattern for a cell's stain. */
static unsigned
_3ds_blood_hash(int x, int y, int i)
{
    unsigned h = (unsigned) x * 73856093u ^ (unsigned) y * 19349663u
                 ^ (unsigned) (i + 1) * 83492791u;

    h ^= h >> 13;
    h *= 2654435761u;
    h ^= h >> 16;
    return h;
}

static void
_3ds_draw_blood(int px, int py, int tw, int th, int mapx, int mapy)
{
    /* dark to bright red, in RGB565 */
    static const u16 shades[4] = { 0x6000, 0x8000, 0x9800, 0xB000 };
    int amt, i;

    if (!isok(mapx, mapy) || tw < 4 || th < 4)
        return;
    _3ds_blood_check_level();
    amt = (int) blood_amt[mapx][mapy];
    if (!amt)
        return;
    /* Scattered droplets, one per stain unit: a single pixel each (a few
       splats are 2x2) instead of a pile of overlapping blocks, which used to
       look like red stripes across the whole tile.  The position and shade
       are derived from the square and the index, so a stain always looks
       the same instead of sparkling on every redraw. */
    for (i = 0; i < amt; i++) {
        unsigned h = _3ds_blood_hash(mapx, mapy, i);
        int bx = 1 + (int) (h % (unsigned) (tw - 2));
        int by = 1 + (int) ((h >> 9) % (unsigned) (th - 2));
        int size = (((h >> 18) & 3) == 0) ? 2 : 1;
        u16 col = stain_color[mapx][mapy]
                      ? stain_color[mapx][mapy]
                      : shades[(h >> 21) & 3];

        _3ds_fill_block(px + bx, py + by, size, size, col);
    }
}

/* Find the visible cell showing a map square and repaint it from the shadow
   copy of its tiles.  Used while the core is running (a fresh stain appears
   during combat), where calling back into the window port would be
   intrusive; the square is redrawn by the core soon afterwards anyway. */
static void
_3ds_repaint_mapcell(int mapx, int mapy)
{
    int c, r;

    for (c = 0; c < _3DS_VIEW_MAX_COLS; c++)
        for (r = 0; r < _3DS_VIEW_MAX_ROWS; r++)
            if (cell_valid[c][r] && cell_mapx[c][r] == mapx
                && cell_mapy[c][r] == mapy) {
                _3ds_repaint_cell(c, r);
                return;
            }
}

/* Add a persistent stain where a droplet landed.  The droplet coordinates
   are pixels on the top screen; turn them back into a map square.  This is
   called while the game waits for a key, so the square can be repainted
   from the core's glyph buffer (which also carries the new stain). */
static void
_3ds_stain_pixel(int px, int py)
{
    int tw, th, ox, oy, cx, cy, mapx, mapy;

    if (!iflags.wc_tiled_map || !_3ds_tiles_active())
        return;
    _3ds_tile_size(&tw, &th);
    _3ds_map_origin(&ox, &oy);
    _3ds_map_view_offset(&cx, &cy);
    mapx = (px - ox) / tw + cx + 1;
    mapy = (py - oy) / th + cy;
    if (!isok(mapx, mapy))
        return;
    if (levl[mapx][mapy].typ != ROOM && levl[mapx][mapy].typ != CORR)
        return;
    if (blood_amt[mapx][mapy] < 12)
        blood_amt[mapx][mapy]++;
    row_refresh(mapx, mapx, mapy);
}

/* Rebuild the visible cells overlapping a pixel rectangle (used to erase
   the previous position of the droplets).  The glyph buffer is the
   authority here, not the shadow copy of the pixels: blood keeps flying
   while the hero moves, and a stale cached tile would briefly resurrect him
   on his old square. */
static void
_3ds_repaint_pixel_area(int x0, int y0, int x1, int y1)
{
    int tw, th, ox, oy, c, r;

    _3ds_tile_size(&tw, &th);
    _3ds_map_origin(&ox, &oy);
    for (c = 0; c < _3DS_VIEW_MAX_COLS; c++)
        for (r = 0; r < _3DS_VIEW_MAX_ROWS; r++) {
            int px, py;

            if (!cell_valid[c][r])
                continue;
            px = ox + c * tw;
            py = oy + r * th;
            if (px + tw <= x0 || px >= x1 || py + th <= y0 || py >= y1)
                continue;
            if (isok(cell_mapx[c][r], cell_mapy[c][r]))
                row_refresh(cell_mapx[c][r], cell_mapx[c][r],
                            cell_mapy[c][r]);
        }
}

static void
_3ds_draw_particles(void)
{
    int i;

    for (i = 0; i < particle_count; i++) {
        int px = (int) particles[i].px;
        int py = (int) particles[i].py;

        if (px < 0 || py < _3DS_MAP_TOP || px + 2 > _3DS_SCR_W
            || py + 2 > _3DS_SCR_H - _3DS_STATUS_H)
            continue;
        _3ds_fill_block(px, py, 2, 2, (i & 1) ? 0xA000 : 0xC000);
    }
}

/* Called by the combat code with the victim's square and the direction
   away from the attacker.  The persistent stain goes on the first floor
   square beyond the victim; the droplets are tiles-mode eye candy. */
void
_3ds_blood_spawn(int vx, int vy, int dx, int dy)
{
    int lx, ly, px, py, w, h, i;

    _3ds_blood_check_level();
    dx = sgn(dx);
    dy = sgn(dy);
    lx = vx + dx;
    ly = vy + dy;
    if (isok(lx, ly)
        && (levl[lx][ly].typ == ROOM || levl[lx][ly].typ == CORR)) {
        if (blood_amt[lx][ly] < 12)
            blood_amt[lx][ly]++;
        _3ds_repaint_mapcell(lx, ly);
    }

    /* Characters on a text-mode map have no sprite to spurt from. */
    if (!iflags.wc_tiled_map || !_3ds_tiles_active())
        return;
    if (!_3ds_map_cell_rect(vx, vy, &px, &py, &w, &h))
        return;
    if (dx == 0 && dy == 0)
        dx = 1;
    for (i = 0; i < 7; i++) {
        struct _3ds_particle *p;
        int slot = particle_count, j;

        if (slot >= _3DS_MAX_PARTICLES) {
            /* replace the droplet nearest to the end of its life */
            slot = 0;
            for (j = 1; j < _3DS_MAX_PARTICLES; j++)
                if (particles[j].life < particles[slot].life)
                    slot = j;
        } else {
            particle_count++;
        }
        p = &particles[slot];
        p->px = px + w / 2.0f;
        p->py = py + h / 2.0f;
        p->vx = dx * (1.8f + (rn2_on_display_rng(120) / 100.0f))
                + (rn2_on_display_rng(160) - 80) / 130.0f;
        p->vy = dy * (1.8f + (rn2_on_display_rng(120) / 100.0f))
                + (rn2_on_display_rng(160) - 80) / 130.0f - 0.4f;
        p->life = 6 + rn2_on_display_rng(4);
    }
}

/* Map a potion's description ("ruby", "blue", ...) onto an RGB565 colour
   for its shatter stain. */
static u16
_3ds_potion_stain_rgb(const char *descr)
{
    if (!descr)
        return 0xF800;
    if (strstr(descr, "ruby") || strstr(descr, "red"))
        return 0xF800; /* red */
    if (strstr(descr, "golden") || strstr(descr, "yellow")
        || strstr(descr, "orange") || strstr(descr, "amber"))
        return 0xFBE0; /* gold/orange */
    if (strstr(descr, "green") || strstr(descr, "emerald")
        || strstr(descr, "olive") || strstr(descr, "swirly"))
        return 0x07E0; /* green */
    if (strstr(descr, "cyan") || strstr(descr, "aqua")
        || strstr(descr, "blue") || strstr(descr, "silver"))
        return 0x07FF; /* cyan/blue */
    if (strstr(descr, "purple") || strstr(descr, "violet")
        || strstr(descr, "magenta") || strstr(descr, "pink"))
        return 0xF81F; /* purple/magenta/pink */
    if (strstr(descr, "brown") || strstr(descr, "tan"))
        return 0x9340; /* brown */
    if (strstr(descr, "black") || strstr(descr, "dark"))
        return 0x0000; /* black/dark */
    /* clear / cloudy / smoky / murky / muddy / runny / oily / smelling /
       effervescent / milky ... */
    return 0xFFFF; /* white-ish */
}

/* A potion just shattered on the floor: leave a small stain in the potion's
   own colour and play the breaking-glass sound. */
void
_3ds_potion_shatter(struct obj *obj, int x, int y)
{
    u16 rgb;

    _3ds_blood_check_level();
    rgb = _3ds_potion_stain_rgb(OBJ_DESCR(objects[obj->otyp]));
    if (isok(x, y) && (levl[x][y].typ == ROOM || levl[x][y].typ == CORR)) {
        if (blood_amt[x][y] < 10)
            blood_amt[x][y] += 4;
        stain_color[x][y] = rgb;
        _3ds_repaint_mapcell(x, y);
    }
#ifdef NINTENDO_3DS
    _3ds_sfx("shattered-glass", 100);
#endif
}

/* --------------------------------------------------------------------- */
/* Floating combat numbers (damage, healing, "Level up")                 */
/* --------------------------------------------------------------------- */

#define _3DS_MAX_FLOATERS 16
struct _3ds_floater {
    int mapx, mapy;
    char text[12];
    u16 color;
    int total, life;
};
static struct _3ds_floater floaters[_3DS_MAX_FLOATERS];
static int floater_count = 0;
/* When set, the per-cell floater drawing is skipped.  Used while a tick is
   repainting the map region currently covered by the floaters so that stale
   labels are erased before the refreshed ones are drawn. */
static int floater_suppress = 0;

/* Queue a short floating label above a map square (damage taken, healing,
   level ups).  It floats upward and fades out over about 2/3 of a second. */
void
_3ds_floating_text(int x, int y, const char *text, u16 color)
{
    struct _3ds_floater *f;
    int slot, i;

    if (!text || !*text || !isok(x, y))
        return;
    if (floater_count < _3DS_MAX_FLOATERS) {
        slot = floater_count++;
    } else {
        slot = 0; /* reuse the floater closest to expiring */
        for (i = 1; i < _3DS_MAX_FLOATERS; i++)
            if (floaters[i].life < floaters[slot].life)
                slot = i;
    }
    f = &floaters[slot];
    f->mapx = x;
    f->mapy = y;
    copynchars(f->text, text, sizeof f->text - 1);
    f->text[sizeof f->text - 1] = '\0';
    f->color = color;
    f->total = f->life = 50;
}

/* Draw an active floater over its cell, risen and faded by age.  The cell
   geometry is passed in from the drawing routine so the label is anchored to
   exactly the tile being drawn. */
static void
_3ds_draw_floater(const struct _3ds_floater *f,
                  int px, int py, int tw, int th)
{
    int rise, textw, tx, ty;

    if (f->life <= 0)
        return;
    textw = (int) strlen(f->text) * 8;
    rise = ((f->total - f->life) * th) / f->total; /* drifts upward */
    tx = px + (tw - textw) / 2;
    ty = py - 6 - rise;
    /* faint dark backing so the label stays readable over any terrain */
    if (tx >= 0 && ty >= 0 && ty + 8 <= _3DS_SCR_H)
        _3ds_fill_block(tx - 1, ty, textw + 2, 8, 0x0000);
    _3ds_art_text_top(tx, ty, f->text, f->color);
}

static void
_3ds_draw_cell_floaters(int mapx, int mapy, int px, int py, int tw, int th)
{
    int i;

    if (floater_suppress)
        return;
    for (i = 0; i < floater_count; i++)
        if (floaters[i].life > 0 && floaters[i].mapx == mapx
            && floaters[i].mapy == mapy)
            _3ds_draw_floater(&floaters[i], px, py, tw, th);
}

/* Per-frame animation.  Called from the input loop while it waits. */
void
_3ds_effects_tick(void)
{
    int i, n, x0, y0, x1, y1;

    if (flash_on) {
        /* Blink: a few frames tinted, a few frames plain, rather than
           re-tinting the same pixels every tick (which only darkened them
           into a solid red block and never looked like a flash). */
        if (--flash_frames <= 0) {
            _3ds_flash_clear(TRUE);
        } else {
            int ph = (flash_frames / 3) & 1;

            if (ph != flash_phase) {
                flash_phase = ph;
                _3ds_flash_repaint();
            }
        }
    }

    /* advance the floating combat numbers; repaint the region they cover
       each frame so they drift upward and fade out.  A floater can spill
       onto the cells around its own while it rises, so clearing only its own
       cell would leave the spilled pixels stuck on the map after the label
       expires; the whole region the label occupies is scrubbed instead. */
    x0 = _3DS_SCR_W;
    y0 = _3DS_SCR_H;
    x1 = y1 = 0;
    for (i = 0; i < floater_count; i++) {
        struct _3ds_floater *f = &floaters[i];
        int px, py, tw, th, rise, textw, tx, ty;

        if (f->life <= 0)
            continue;
        if (!_3ds_map_cell_rect(f->mapx, f->mapy, &px, &py, &tw, &th))
            continue;
        textw = (int) strlen(f->text) * 8;
        rise = ((f->total - f->life) * th) / f->total; /* drifts upward */
        tx = px + (tw - textw) / 2;
        ty = py - 6 - rise;
        /* same visibility guard as _3ds_draw_floater() */
        if (tx < 0 || ty < 0 || ty + 8 > _3DS_SCR_H)
            continue;
        if (tx - 1 < x0)
            x0 = tx - 1;
        if (ty < y0)
            y0 = ty;
        if (tx + textw + 1 > x1)
            x1 = tx + textw + 1;
        if (ty + 8 > y1)
            y1 = ty + 8;
    }
    if (x1 > x0 && y1 > y0) {
        floater_suppress = 1; /* don't redraw labels while scrubbing */
        _3ds_repaint_pixel_area(x0, y0, x1, y1);
        floater_suppress = 0;
    }
    /* age the floaters (a floater whose life reaches 0 is simply not
       redrawn below, so it disappears without leaving residue) */
    for (i = 0; i < floater_count; i++)
        if (floaters[i].life > 0)
            floaters[i].life--;

    if (particle_count == 0)
        goto draw_floaters;

    x0 = _3DS_SCR_W;
    y0 = _3DS_SCR_H;
    x1 = y1 = 0;
    for (i = 0; i < particle_count; i++) {
        if ((int) particles[i].px < x0)
            x0 = (int) particles[i].px;
        if ((int) particles[i].py < y0)
            y0 = (int) particles[i].py;
        if ((int) particles[i].px + 2 > x1)
            x1 = (int) particles[i].px + 2;
        if ((int) particles[i].py + 2 > y1)
            y1 = (int) particles[i].py + 2;
    }
    if (x0 > x1 || y0 > y1) {
        x0 = 0;
        y0 = _3DS_MAP_TOP;
        x1 = _3DS_SCR_W;
        y1 = _3DS_SCR_H - _3DS_STATUS_H;
    }

    n = 0;
    for (i = 0; i < particle_count; i++) {
        struct _3ds_particle *p = &particles[i];

        p->px += p->vx;
        p->py += p->vy;
        p->vy += 0.35f;
        p->life--;
        if (p->life <= 0) {
            /* the droplet lands: leave a small stain behind so a fight
               leaves a trail of blood on the floor instead of the drops
               simply vanishing */
            _3ds_stain_pixel((int) p->px, (int) p->py);
            continue;
        }
        particles[n++] = *p;
        if ((int) p->px < x0)
            x0 = (int) p->px;
        if ((int) p->py < y0)
            y0 = (int) p->py;
        if ((int) p->px + 2 > x1)
            x1 = (int) p->px + 2;
        if ((int) p->py + 2 > y1)
            y1 = (int) p->py + 2;
    }
    particle_count = n;

    _3ds_repaint_pixel_area(x0 - 1, y0 - 1, x1 + 1, y1 + 1);
    _3ds_draw_particles();

draw_floaters:
    /* redraw the live floaters at their new positions on top of the
       freshly scrubbed map */
    for (i = 0; i < floater_count; i++) {
        struct _3ds_floater *f = &floaters[i];
        int px, py, tw, th;

        if (f->life <= 0)
            continue;
        if (_3ds_map_cell_rect(f->mapx, f->mapy, &px, &py, &tw, &th))
            _3ds_draw_floater(f, px, py, tw, th);
    }
}

/* A menu or console clear is about to paint over the map: drop the effect
   state without redrawing the old square into the new picture. */
void
_3ds_effects_discard(void)
{
    flash_on = FALSE;
    flash_phase = 0;
    flash_frames = 0;
    flash_mapx = flash_mapy = -1;
    particle_count = 0;
    floater_count = 0;
}

/* repaint one visible map cell from the shadow cache */
static void
_3ds_repaint_cell(int vcol, int vrow)
{
    int tw, th, px, py, ox, oy, bg, fg, under;

    if (vcol < 0 || vrow < 0 || vcol >= _3DS_VIEW_MAX_COLS
        || vrow >= _3DS_VIEW_MAX_ROWS || !cell_valid[vcol][vrow])
        return;
    _3ds_tile_size(&tw, &th);
    _3ds_map_origin(&ox, &oy);
    px = ox + vcol * tw;
    py = oy + vrow * th;
    if (px + tw > _3DS_SCR_W || py + th > _3DS_SCR_H - _3DS_STATUS_H)
        return;

    _3ds_fill_block(px, py, tw, th, 0);
    bg = cell_bg[vcol][vrow];
    if (bg >= 0 && bg < ntiles)
        _3ds_blit_block(px, py, bg, tw, th);
    _3ds_draw_blood(px, py, tw, th, cell_mapx[vcol][vrow],
                    cell_mapy[vcol][vrow]);
    under = cell_under[vcol][vrow];
    if (under >= 0 && under < ntiles)
        _3ds_blit_block(px, py, under, tw, th);
    fg = cell_fg[vcol][vrow];
    if (fg >= 0 && fg < ntiles)
        _3ds_blit_block(px, py, fg, tw, th);
}

/* Paint (or take away) the frame around the cell the look cursor is on.
   show == FALSE just restores the cell the cursor used to occupy. */
void
_3ds_map_cursor(int vcol, int vrow, int show)
{
    int tw, th, px, py, ox, oy, t;
    u16 col;

    /* In ASCII mode the cursor is a highlighted background under the cell
       instead of a frame around a tile.  This also covers the scaled ASCII
       renderer, which draws into the framebuffer but is still characters. */
    if (!_3ds_tiles_active() || !iflags.wc_tiled_map) {
        _3ds_ascii_cursor(vcol, vrow, show);
        return;
    }

    if (cursor_shown) {
        if (show && vcol == cursor_col && vrow == cursor_row)
            return; /* nothing moved, leave the frame alone */
        _3ds_repaint_cell(cursor_col, cursor_row);
        cursor_shown = FALSE;
        cursor_col = cursor_row = -1;
    }
    if (!show || vcol < 0 || vrow < 0)
        return;

    _3ds_tile_size(&tw, &th);
    _3ds_map_origin(&ox, &oy);
    px = ox + vcol * tw;
    py = oy + vrow * th;
    if (px + tw > _3DS_SCR_W || py + th > _3DS_SCR_H - _3DS_STATUS_H)
        return;

    /* a bright yellow frame, two pixels wide when the tile allows it */
    col = 0xFFE0;
    t = (tw >= 12 && th >= 12) ? 2 : 1;
    _3ds_fill_block(px, py, tw, t, col);
    _3ds_fill_block(px, py + th - t, tw, t, col);
    _3ds_fill_block(px, py, t, th, col);
    _3ds_fill_block(px + tw - t, py, t, th, col);

    cursor_col = vcol;
    cursor_row = vrow;
    cursor_shown = TRUE;
}

/* getpos() has finished: drop the frame without leaving the last cell
   highlighted for the rest of the game. */
void
_3ds_map_cursor_off(void)
{
    if (cursor_shown) {
        _3ds_repaint_cell(cursor_col, cursor_row);
        cursor_shown = FALSE;
        cursor_col = cursor_row = -1;
    }
    _3ds_ascii_cursor(-1, -1, 0);
}

/* The whole screen is about to be cleared.  Forget the cursor without
   restoring its saved pixels: restoring them after the clear would paint
   stale data back over the freshly cleared map. */
void
_3ds_map_cursor_discard(void)
{
    cursor_shown = FALSE;
    cursor_col = cursor_row = -1;
    _3ds_ascii_cursor_invalidate();
}

/* Resample the loaded tileset into an RGB565 cache of dw x dh pixels per
   tile.  When dw/dh match the source tile size this is a straight copy. */
static boolean
_3ds_build_tile_cache(struct TileSetImage *img, int dw, int dh)
{
    int cols, sw, sh, n, t;
    unsigned char *idx;
    struct Pixel *px;

    if (!img->indexes || !img->pixels || !img->tile_width
        || !img->tile_height)
        return FALSE;

    cols = (int) (img->width / img->tile_width);
    if (cols <= 0)
        cols = TILES_PER_ROW;
    sw = (int) img->tile_width;
    sh = (int) img->tile_height;
    idx = img->indexes;
    px = img->pixels;

    n = total_tiles_used;
    if (n <= 0)
        return FALSE;

    tile_cache = (u16 *) malloc((size_t) n * dw * dh * sizeof(u16));
    tile_alpha = (u8 *) malloc((size_t) n * dw * dh);
    if (!tile_cache || !tile_alpha) {
        if (tile_cache) {
            free(tile_cache);
            tile_cache = NULL;
        }
        if (tile_alpha) {
            free(tile_alpha);
            tile_alpha = NULL;
        }
        return FALSE;
    }
    ntiles = n;

    for (t = 0; t < n; t++) {
        int tcol = t % cols;
        int trow = t / cols;
        int sxbase = tcol * sw;
        int sybase = trow * sh;
        int ox, oy;

        for (oy = 0; oy < dh; oy++) {
            int sy0 = sybase + (oy * sh) / dh;
            int sy1 = sybase + ((oy + 1) * sh) / dh;

            if (sy1 <= sy0)
                sy1 = sy0 + 1;
            for (ox = 0; ox < dw; ox++) {
                int sx0 = sxbase + (ox * sw) / dw;
                int sx1 = sxbase + ((ox + 1) * sw) / dw;
                unsigned rr = 0, gg = 0, bb = 0, cnt = 0;
                int sx, sy;
                size_t dst = (size_t) t * dw * dh + (size_t) oy * dw + ox;

                if (sx1 <= sx0)
                    sx1 = sx0 + 1;
                for (sy = sy0; sy < sy1; sy++) {
                    if (sy < 0 || sy >= (int) img->height)
                        continue;
                    for (sx = sx0; sx < sx1; sx++) {
                        struct Pixel *p;

                        if (sx < 0 || sx >= (int) img->width)
                            continue;
                        p = &px[(size_t) sy * img->width + sx];
                        if (idx[(size_t) sy * img->width + sx] == 0)
                            continue; /* palette index 0 is transparent */
                        rr += p->r;
                        gg += p->g;
                        bb += p->b;
                        cnt++;
                    }
                }
                if (cnt == 0) {
                    tile_cache[dst] = 0;
                    tile_alpha[dst] = 0;
                } else {
                    tile_cache[dst] = rgb565((unsigned char) (rr / cnt),
                                             (unsigned char) (gg / cnt),
                                             (unsigned char) (bb / cnt));
                    tile_alpha[dst] = 1;
                }
            }
        }
    }
    return TRUE;
}

/* --------------------------------------------------------------------- */
/* PixelHack floor removal.
 *
 * The reference front end has a per-tileset background-removal preset for
 * PixelHack: background tile 2304 with exact-colour removal (no tolerance or
 * feathering), combined with a solid chroma key of #83aba2 (the flat light
 * floor colour).  We do the same on the image as it is read, before it is
 * resampled into the RGB565 cache.
 *
 * Ground tiles (other.txt 19/20/22/23) must keep the floor, and structural
 * art such as walls, water and lava must not be erased, so those are skipped.
 * "other" overlay tiles (traps, engravings, ...) are cleaned only when they
 * are mostly a sprite sitting on the floor. */

#define PH_REF_TILE    2304 /* decal_delimiter, repurposed as the preset floor */
#define PH_MON_END     789
#define PH_OBJ_START   789
#define PH_OBJ_END     1272
#define PH_OTHER_START 1272
#define PH_OTHER_END   1515
#define PH_GRAY_START  1515
#define PH_GRAY_END    2304
#define PH_DECAL_START 2304
#define PH_DECAL_END   2307

#define PH_SOLID_R 131 /* #83aba2 */
#define PH_SOLID_G 171
#define PH_SOLID_B 162

static void
_3ds_pixelhack_cleanup(struct TileSetImage *img)
{
    unsigned char *idx = img->indexes;
    struct Pixel *px = img->pixels;
    int cols, tw, th, t, x, y;
    size_t npix, i;
    int refx0, refy0;

    if (!idx || !px || !img->tile_width || !img->tile_height)
        return;

    cols = (int) (img->width / img->tile_width);
    if (cols <= 0)
        return;
    tw = (int) img->tile_width;
    th = (int) img->tile_height;
    npix = (size_t) img->width * img->height;

    if ((PH_REF_TILE / cols + 1) * th > (int) img->height)
        return; /* the reference tile is not in this image */
    refx0 = (PH_REF_TILE % cols) * tw;
    refy0 = (PH_REF_TILE / cols) * th;

    /* Original palette index i becomes i + 1, so index 0 can mark removal
       (the colour that used to live at index 0 is the light floor shade,
       which is also used inside some sprites and must stay visible). */
    for (i = 0; i < npix; i++)
        if (idx[i] < 255)
            idx[i]++;

    for (t = 0; t < total_tiles_used; t++) {
        int tcol = t % cols;
        int trow = t / cols;
        boolean apply;

        if ((trow + 1) * th > (int) img->height)
            break; /* beyond the image (shouldn't happen) */

        if (t < PH_MON_END
            || (t >= PH_OBJ_START && t < PH_OBJ_END)
            || (t >= PH_GRAY_START && t < PH_GRAY_END)
            || (t >= PH_DECAL_START && t < PH_DECAL_END)) {
            apply = TRUE;
        } else if (t >= PH_OTHER_START && t < PH_OTHER_END) {
            int oi = t - PH_OTHER_START;
            int matches = 0;

            if (oi == 19 || oi == 20 || oi == 22 || oi == 23)
                continue; /* the ground itself: keep it as drawn */
            for (y = 0; y < th; y++)
                for (x = 0; x < tw; x++) {
                    struct Pixel *p = &px[(size_t) (trow * th + y)
                                              * img->width + tcol * tw + x];
                    struct Pixel *r = &px[(size_t) (refy0 + y)
                                              * img->width + refx0 + x];

                    if (p->r == r->r && p->g == r->g && p->b == r->b)
                        matches++;
                }
            apply = (matches * 100 >= 35 * tw * th) ? TRUE : FALSE;
        } else {
            continue;
        }
        if (!apply)
            continue;

        for (y = 0; y < th; y++)
            for (x = 0; x < tw; x++) {
                size_t di =
                    (size_t) (trow * th + y) * img->width + tcol * tw + x;
                struct Pixel *p = &px[di];
                struct Pixel *r =
                    &px[(size_t) (refy0 + y) * img->width + refx0 + x];

                if ((p->r == r->r && p->g == r->g && p->b == r->b)
                    || (p->r == PH_SOLID_R && p->g == PH_SOLID_G
                        && p->b == PH_SOLID_B))
                    idx[di] = 0;
            }
    }
}

/* Build (or rebuild) the cache if its size does not match the config. */
static void
_3ds_ensure_cache(void)
{
    int tw, th;
    struct TileSetImage img;

    if (!tiles_ready)
        return;
    _3ds_tile_size(&tw, &th);
    if (tile_cache && tw == cache_w && th == cache_h)
        return;
    if (tile_cache) {
        free(tile_cache);
        tile_cache = NULL;
    }
    if (tile_alpha) {
        free(tile_alpha);
        tile_alpha = NULL;
    }
    ntiles = 0;
    _3ds_reset_view_cache();
    if (read_tile_image(&img, tiles_path, FALSE)) {
        if (tileset_choice == 1)
            _3ds_pixelhack_cleanup(&img);
        if (_3ds_build_tile_cache(&img, tw, th)) {
            cache_w = tw;
            cache_h = th;
        }
        free_tile_image(&img);
    }
}

/* --------------------------------------------------------------------- */

/* Forget the loaded tileset and its cache.  The next _3ds_tiles_active()
   rebuilds it from tiles_path. */
static void
_3ds_tiles_unload(void)
{
    if (tile_cache) {
        free(tile_cache);
        tile_cache = NULL;
    }
    if (tile_alpha) {
        free(tile_alpha);
        tile_alpha = NULL;
    }
    ntiles = 0;
    cache_w = cache_h = 0;
    tiles_ready = FALSE;
    tiles_path[0] = '\0';
    _3ds_effects_discard();
    _3ds_reset_view_cache();
}

/* Locate the chosen tileset and remember its path; the RGB565 cache is built
   lazily on first use so the tile size from the config file is honoured. */
void
_3ds_init_tiles(void)
{
    const char *const *paths = (tileset_choice == 1)
                                   ? pixelhack_tileset_paths
                                   : default_tileset_paths;
    FILE *f;
    int i;

    if (tiles_ready)
        return;

    for (i = 0; paths[i]; i++) {
        f = fopen(paths[i], "rb");
        if (f) {
            fclose(f);
            strncpy(tiles_path, paths[i], sizeof(tiles_path) - 1);
            tiles_path[sizeof(tiles_path) - 1] = '\0';
            tiles_ready = TRUE;
            return;
        }
    }
}

/* Pick the tileset to draw with (0 = built-in, 1 = PixelHack).  Safe to
   call while a game is running: it drops the old cache so the next redraw
   rebuilds from the new file. */
void
_3ds_set_tileset(int which)
{
    if (which != 1)
        which = 0;
    if (which == tileset_choice && tiles_ready)
        return;
    tileset_choice = which;
    _3ds_tiles_unload();
    _3ds_init_tiles();
}

/* ASCII cells larger than the console's native 8x8 grid are painted by the
   same direct-to-framebuffer path as graphical tiles, so that the character
   size selected in the options screen actually affects the map. */
static boolean
_3ds_ascii_scaled(void)
{
    int tw = iflags.wc_tile_width, th = iflags.wc_tile_height;

    if (iflags.wc_tiled_map)
        return FALSE;
    if (tw <= 0)
        tw = 8;
    if (th <= 0)
        th = 8;
    return (tw != 8 || th != 8) ? TRUE : FALSE;
}

/* TRUE when map cells are drawn directly into the framebuffer instead of
   with the character console.  This also makes sure the cached tiles match
   the requested size. */
boolean
_3ds_tiles_active(void)
{
    if (topConsole.frameBuffer == NULL)
        return FALSE;
    if (!iflags.wc_tiled_map)
        return _3ds_ascii_scaled();
    if (!tiles_ready)
        return FALSE;
    _3ds_ensure_cache();
    return (tile_cache != NULL) ? TRUE : FALSE;
}

/* Erase the tile area (used when the console screen is cleared so that no
   tile fragments bleed through text windows drawn over the map). */
void
_3ds_clear_tiles(void)
{
    int px, py;

    /* Drop any cursor highlight and shadowed cells even when the console
       path is active; a screen clear invalidates both. */
    _3ds_map_cursor_off();
    _3ds_effects_discard();
    _3ds_reset_view_cache();
    if (!_3ds_tiles_active())
        return;
    /* Wipe the whole map band (below the message line), including the
       margins left by centring the tile grid, so no old tile pixels can
       bleed through a window drawn over the map. */
    for (py = _3DS_MAP_TOP; py < _3DS_SCR_H - _3DS_STATUS_H; py++)
        for (px = 0; px < _3DS_SCR_W; px++)
            topConsole.frameBuffer[px * _3DS_SCR_H
                                   + (_3DS_SCR_H - 1 - py)] = 0;
}

/* ---------------------------------------------------------------- */
/* scaled ASCII cells                                               */
/* ---------------------------------------------------------------- */

static u16
ascii_rgb565(uint32 rgb)
{
    unsigned r = (rgb >> 16) & 0xff, g = (rgb >> 8) & 0xff, b = rgb & 0xff;

    return (u16) (((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

static u16
ascii_color(int idx)
{
    static const u16 palette[16] = {
        0x0000, 0xF800, 0x07E0, 0xAD55, 0x001F, 0xF81F, 0x07FF, 0xC618,
        0xFFFF, 0xF800, 0x07E0, 0xFFE0, 0x001F, 0xF81F, 0x07FF, 0xFFFF
    };

    if (idx == NO_COLOR)
        return 0xFFFF;
    if (idx < 0 || idx >= 16)
        idx = CLR_GRAY;
    return palette[idx];
}

static u16
ascii_glyph_color(const glyph_info *gi)
{
    uint32 custom;
    int idx;

    if (!iflags.use_color)
        return 0xFFFF;
    custom = gi ? gi->gm.customcolor : 0;
    if (custom) {
        if (custom & NH_BASIC_COLOR)
            return ascii_color((int) COLORVAL(custom));
        return ascii_rgb565(custom);
    }
    idx = gi ? (int) gi->gm.sym.color : CLR_GRAY;
    return ascii_color(idx);
}

static u16
ascii_bg_color(const glyph_info *bgi)
{
    if (iflags.use_color && bgi && bgi->framecolor != NO_COLOR)
        return ascii_color((int) bgi->framecolor);
    return 0x0000;
}

static boolean
ascii_inverse(const glyph_info *gi)
{
    unsigned special = gi ? gi->gm.glyphflags : 0;

    /* Pet highlighting is independent of the 'use_inverse' option, just as
       it is in tty_print_glyph().  The port leaves wc2_petattr at its
       default (inverse video). */
    if ((special & MG_PET) != 0 && iflags.hilite_pet
        && (iflags.wc2_petattr & ATR_INVERSE) != 0)
        return TRUE;
    if (!iflags.use_inverse)
        return FALSE;
    if ((special & MG_OBJPILE) != 0 && iflags.hilite_pile)
        return TRUE;
    if ((special & (MG_DETECT | MG_BW_LAVA | MG_BW_ICE | MG_BW_SINK
                    | MG_BW_ENGR)) != 0)
        return TRUE;
    if ((special & MG_FEMALE) != 0 && wizard && iflags.wizmgender)
        return TRUE;
    return FALSE;
}

/* Blow one 8x8 console glyph up to the requested cell size. */
static void
_3ds_draw_ascii_cell(int px, int py, int w, int h,
                     const glyph_info *glyphinfo,
                     const glyph_info *bkglyphinfo)
{
    const u8 *gfx = (const u8 *) topConsole.font.gfx;
    int off = (int) topConsole.font.asciiOffset;
    int num = (int) topConsole.font.numChars;
    int ch = glyphinfo ? (int) (unsigned char) glyphinfo->ttychar : ' ';
    u16 fg = ascii_glyph_color(glyphinfo);
    u16 bg = ascii_bg_color(bkglyphinfo);
    int idx, x, y, sx, sy;
    u8 bits;

    if (ascii_inverse(glyphinfo)) {
        u16 tmp = fg;

        fg = bg;
        bg = tmp;
    }

    _3ds_fill_block(px, py, w, h, bg);
    if (!gfx || w < 1 || h < 1)
        return;
    idx = ch - off;
    if (idx < 0 || idx >= num)
        return;

    for (y = 0; y < h; y++) {
        sy = (y * 8) / h;
        bits = gfx[idx * 8 + sy];
        if (!bits)
            continue;
        for (x = 0; x < w; x++) {
            int pxx, pyy;

            sx = (x * 8) / w;
            if (!(bits & (0x80 >> sx)))
                continue;
            pxx = px + x;
            pyy = py + y;
            if ((unsigned) pxx < (unsigned) _3DS_SCR_W
                && (unsigned) pyy < (unsigned) _3DS_SCR_H)
                topConsole.frameBuffer[pxx * _3DS_SCR_H
                                       + (_3DS_SCR_H - 1 - pyy)] = fg;
        }
    }
}

/* A glyph worth drawing underneath a creature: an item, a trap, stairs,
   an altar, a fountain, and so on.  Plain floor is already supplied by the
   background glyph, so it is excluded to avoid drawing it twice. */
static boolean
_3ds_interesting_glyph(int g)
{
    if (g == NO_GLYPH || g == GLYPH_UNEXPLORED)
        return FALSE;
    if (glyph_is_object(g) || glyph_is_trap(g))
        return TRUE;
    if (glyph_is_cmap(g)) {
        if (g == cmap_to_glyph(S_room) || g == cmap_to_glyph(S_stone)
            || g == cmap_to_glyph(S_corr) || g == cmap_to_glyph(S_litcorr))
            return FALSE;
        return TRUE;
    }
    return FALSE;
}

/* The object/terrain sprite that belongs between the floor and a monster
   or the hero.  NetHack's map glyph is the topmost thing on the square, so
   a creature on a pile of loot or on the stairs used to hide it. */
static int
_3ds_underlay_tile(int mapx, int mapy, const glyph_info *glyphinfo)
{
    int fg = glyphinfo ? glyphinfo->glyph : NO_GLYPH;
    glyph_info gi = nul_glyphinfo;
    int g;

    if (!isok(mapx, mapy))
        return -1;
    if (!((fg != NO_GLYPH && glyph_is_monster(fg)) || u_at(mapx, mapy)))
        return -1;

    if (cansee(mapx, mapy) && OBJ_AT(mapx, mapy)) {
        g = obj_to_glyph(svl.level.objects[mapx][mapy], rn2_on_display_rng);
        map_glyphinfo(mapx, mapy, g, 0U, &gi);
        return (int) gi.gm.tileidx;
    }

    g = levl[mapx][mapy].glyph;
    if (!_3ds_interesting_glyph(g))
        g = back_to_glyph(mapx, mapy);
    if (!_3ds_interesting_glyph(g))
        return -1;
    map_glyphinfo(mapx, mapy, g, 0U, &gi);
    return (int) gi.gm.tileidx;
}

/* Draw the map tile(s) for one visible map cell.  vcol/vrow are 0-based
   indices within the clipped map view (see wintty.c); mapx/mapy are the
   absolute map coordinates, needed for effects and for the underlay. */
void
_3ds_draw_tile(
    int vcol,
    int vrow,
    int mapx,
    int mapy,
    const glyph_info *glyphinfo,
    const glyph_info *bkglyphinfo)
{
    int tw, th, px, py, ox, oy, fgtile, bgtile, undertile;

    if (vcol < 0 || vrow < 0 || !_3ds_tiles_active())
        return;

    _3ds_tile_size(&tw, &th);

    /* In character mode the map keeps the console's top-left origin.  This
       matches the ASCII cursor and leaves any unused space at the bottom,
       just like the native 8x8 console layout. */
    if (!iflags.wc_tiled_map) {
        px = vcol * tw;
        py = _3DS_MAP_TOP + vrow * th;
        if (px + tw > _3DS_SCR_W
            || py + th > _3DS_SCR_H - _3DS_STATUS_H)
            return;
        _3ds_draw_ascii_cell(px, py, tw, th, glyphinfo, bkglyphinfo);
        if (flash_on && flash_phase && mapx == flash_mapx && mapy == flash_mapy)
            _3ds_flash_paint();
        _3ds_draw_cell_floaters(mapx, mapy, px, py, tw, th);
        _3ds_ascii_cursor_refresh(vcol, vrow);
        return;
    }

    _3ds_map_origin(&ox, &oy);
    px = ox + vcol * tw;
    py = oy + vrow * th;
    if (px + tw > _3DS_SCR_W || py + th > _3DS_SCR_H - _3DS_STATUS_H)
        return;

    /* Request background glyphs from the core the first time we draw; this
     * lets terrain show through transparent parts of monster/object tiles. */
    if (!iflags.use_background_glyph)
        iflags.use_background_glyph = TRUE;

    _3ds_blood_check_level();

    /* Start from black so nothing from the character console shows through. */
    _3ds_fill_block(px, py, tw, th, 0);

    bgtile = bkglyphinfo ? (int) bkglyphinfo->gm.tileidx : -1;
    if (bgtile >= 0 && bgtile < ntiles)
        _3ds_blit_block(px, py, bgtile, tw, th);

    /* floor stain, then the object or stair the creature is standing on,
       then the creature itself */
    _3ds_draw_blood(px, py, tw, th, mapx, mapy);

    undertile = _3ds_underlay_tile(mapx, mapy, glyphinfo);
    if (undertile >= 0 && undertile < ntiles && undertile != bgtile)
        _3ds_blit_block(px, py, undertile, tw, th);

    fgtile = glyphinfo ? (int) glyphinfo->gm.tileidx : -1;
    if (fgtile >= 0 && fgtile < ntiles)
        _3ds_blit_block(px, py, fgtile, tw, th);

    /* Remember what is on this cell so the look cursor can restore it. */
    if (vcol < _3DS_VIEW_MAX_COLS && vrow < _3DS_VIEW_MAX_ROWS) {
        cell_bg[vcol][vrow] = bgtile;
        cell_under[vcol][vrow] = undertile;
        cell_fg[vcol][vrow] = fgtile;
        cell_mapx[vcol][vrow] = mapx;
        cell_mapy[vcol][vrow] = mapy;
        cell_valid[vcol][vrow] = TRUE;
    }

    if (flash_on && flash_phase && mapx == flash_mapx && mapy == flash_mapy)
        _3ds_flash_paint();

    /* A redraw of the cell the look cursor is sitting on would paint over
       its frame; put the frame back. */
    if (cursor_shown && vcol == cursor_col && vrow == cursor_row) {
        cursor_shown = FALSE;
        _3ds_map_cursor(vcol, vrow, 1);
    }

    _3ds_draw_cell_floaters(mapx, mapy, px, py, tw, th);
}

#else /* !TILES_IN_GLYPHMAP */
/* Keep the link satisfied if tiles are not compiled in. */
void
_3ds_init_tiles(void)
{
}

void
_3ds_set_tileset(int which)
{
    (void) which;
}

boolean
_3ds_tiles_active(void)
{
    return FALSE;
}

void
_3ds_tile_size(int *w, int *h)
{
    *w = 0;
    *h = 0;
}

int
_3ds_map_view_cols(void)
{
    return 0;
}

int
_3ds_map_view_rows(void)
{
    return 0;
}

void
_3ds_clear_tiles(void)
{
}

void
_3ds_map_cursor(int vcol, int vrow, int show)
{
    _3ds_ascii_cursor(vcol, vrow, show);
}

void
_3ds_map_cursor_off(void)
{
    _3ds_ascii_cursor(-1, -1, 0);
}

void
_3ds_map_cursor_discard(void)
{
    _3ds_ascii_cursor_invalidate();
}

void
_3ds_draw_tile(
    int vcol,
    int vrow,
    int mapx,
    int mapy,
    const glyph_info *glyphinfo,
    const glyph_info *bkglyphinfo)
{
    (void) vcol;
    (void) vrow;
    (void) mapx;
    (void) mapy;
    (void) glyphinfo;
    (void) bkglyphinfo;
}

void
_3ds_hit_flash(int mapx, int mapy)
{
    (void) mapx;
    (void) mapy;
}

void
_3ds_blood_spawn(int vx, int vy, int dx, int dy)
{
    (void) vx;
    (void) vy;
    (void) dx;
    (void) dy;
}

void
_3ds_potion_shatter(struct obj *obj, int x, int y)
{
    (void) obj;
    (void) x;
    (void) y;
}

void
_3ds_floating_text(int x, int y, const char *text, u16 color)
{
    (void) x;
    (void) y;
    (void) text;
    (void) color;
}

void
_3ds_effects_tick(void)
{
}

void
_3ds_effects_discard(void)
{
}

void
_3ds_draw_icon(int px, int py, int size, int tileidx)
{
    (void) px;
    (void) py;
    (void) size;
    (void) tileidx;
}
#endif /* ?TILES_IN_GLYPHMAP */

/* --------------------------------------------------------------------- */
/* ASCII map cursor                                                      */
/*                                                                       */
/* The character console has no per-cell background colour, and its      */
/* cursor is invisible.  For look/glance commands we save the 8x8 pixel  */
/* cell under the cursor, paint its background yellow and redraw the     */
/* glyph pixels in black.  When the cursor moves (or getpos() ends) the  */
/* saved pixels are copied back verbatim, so the original colours are    */
/* restored exactly.  The map starts one 8-pixel character row below the */
/* message line (the tty map window's offy).                             */
/* --------------------------------------------------------------------- */

#define _3DS_ASCII_CELL 8
#define _3DS_ASCII_CELL_MAX 64
#define _3DS_ASCII_SCREEN_W 400
#define _3DS_ASCII_SCREEN_H 240
#define _3DS_ASCII_TOP_PX 8
#define _3DS_ASCII_CURSOR_COLOR 0xFFE0 /* bright yellow, RGB565 */

static u16 ascii_cell[_3DS_ASCII_CELL_MAX * _3DS_ASCII_CELL_MAX];
static int ascii_cell_w = _3DS_ASCII_CELL;
static int ascii_cell_h = _3DS_ASCII_CELL;
static int ascii_cursor_col = -1, ascii_cursor_row = -1;
static boolean ascii_cursor_shown = FALSE;

static void
_3ds_ascii_cell_size(int *w, int *h)
{
    int tw = iflags.wc_tile_width, th = iflags.wc_tile_height;

    /* When the native console path is in use the cells are exactly 8x8.
       Otherwise mirror _3ds_tile_size(), which is what _3ds_draw_tile()
       uses, so the saved cursor cell matches the drawn cell even if one
       dimension was left unset. */
    if (iflags.wc_tiled_map
        || ((tw <= 0 || tw == _3DS_ASCII_CELL)
            && (th <= 0 || th == _3DS_ASCII_CELL))) {
        *w = *h = _3DS_ASCII_CELL;
        return;
    }
    _3ds_tile_size(&tw, &th);
    if (tw < _3DS_ASCII_CELL)
        tw = _3DS_ASCII_CELL;
    if (th < _3DS_ASCII_CELL)
        th = _3DS_ASCII_CELL;
    if (tw > _3DS_ASCII_CELL_MAX)
        tw = _3DS_ASCII_CELL_MAX;
    if (th > _3DS_ASCII_CELL_MAX)
        th = _3DS_ASCII_CELL_MAX;
    *w = tw;
    *h = th;
}

static void
_3ds_ascii_cursor_invalidate(void)
{
    ascii_cursor_shown = FALSE;
    ascii_cursor_col = ascii_cursor_row = -1;
}

static void
_3ds_ascii_restore(void)
{
    u16 *fb = topConsole.frameBuffer;
    int cw, ch, x0, y0, x, y, i = 0;

    if (!fb || !ascii_cursor_shown)
        return;
    cw = ascii_cell_w;
    ch = ascii_cell_h;
    x0 = ascii_cursor_col * cw;
    y0 = _3DS_ASCII_TOP_PX + ascii_cursor_row * ch;
    for (y = 0; y < ch; y++)
        for (x = 0; x < cw; x++, i++) {
            int px = x0 + x, py = y0 + y;

            if ((unsigned) px < _3DS_ASCII_SCREEN_W
                && (unsigned) py < _3DS_ASCII_SCREEN_H)
                fb[px * _3DS_ASCII_SCREEN_H
                   + (_3DS_ASCII_SCREEN_H - 1 - py)] = ascii_cell[i];
        }
}

static void
_3ds_ascii_cursor(int vcol, int vrow, int show)
{
    u16 *fb = topConsole.frameBuffer;
    int cw, ch, x0, y0, x, y, i;

    if (!fb)
        return;

    if (ascii_cursor_shown) {
        if (show && vcol == ascii_cursor_col && vrow == ascii_cursor_row)
            return; /* the cursor did not move */
        _3ds_ascii_restore();
        _3ds_ascii_cursor_invalidate();
    }
    if (!show || vcol < 0 || vrow < 0)
        return;

    _3ds_ascii_cell_size(&cw, &ch);
    x0 = vcol * cw;
    y0 = _3DS_ASCII_TOP_PX + vrow * ch;
    if (x0 + cw > _3DS_ASCII_SCREEN_W
        || y0 + ch > _3DS_ASCII_SCREEN_H)
        return;

    ascii_cell_w = cw;
    ascii_cell_h = ch;

    /* remember the untouched cell */
    i = 0;
    for (y = 0; y < ch; y++)
        for (x = 0; x < cw; x++, i++) {
            int px = x0 + x, py = y0 + y;

            ascii_cell[i] = fb[px * _3DS_ASCII_SCREEN_H
                               + (_3DS_ASCII_SCREEN_H - 1 - py)];
        }

    /* Paint the background, then keep the glyph readable in black.  Find the
       cell's dominant colour first: it is the background for an ordinary
       cell and also for an inverse-video cell, whose "glyph" colour is the
       minority one. */
    {
        u16 c0 = ascii_cell[0], c1 = c0, bgcol;
        int n0 = 0, n1 = 0, j, n = cw * ch;

        /* A cell has at most two colours: the cell background and the glyph.
           Pick whichever covers more pixels; that is the colour to replace
           with the yellow highlight. */
        for (j = 1; j < n; j++)
            if (ascii_cell[j] != c0) {
                c1 = ascii_cell[j];
                break;
            }
        for (j = 0; j < n; j++) {
            if (ascii_cell[j] == c0)
                n0++;
            else if (ascii_cell[j] == c1)
                n1++;
        }
        bgcol = (n1 > n0) ? c1 : c0;

        i = 0;
        for (y = 0; y < ch; y++)
            for (x = 0; x < cw; x++, i++) {
                int px = x0 + x, py = y0 + y;

                fb[px * _3DS_ASCII_SCREEN_H
                   + (_3DS_ASCII_SCREEN_H - 1 - py)]
                    = (ascii_cell[i] == bgcol) ? _3DS_ASCII_CURSOR_COLOR
                                               : 0x0000;
            }
    }

    ascii_cursor_col = vcol;
    ascii_cursor_row = vrow;
    ascii_cursor_shown = TRUE;
}

/* The cell under the look cursor was just repainted; remember the new
   pixels and put the highlight back on top of them. */
static void
_3ds_ascii_cursor_refresh(int vcol, int vrow)
{
    if (ascii_cursor_shown && vcol == ascii_cursor_col
        && vrow == ascii_cursor_row) {
        _3ds_ascii_cursor_invalidate();
        _3ds_ascii_cursor(vcol, vrow, 1);
    }
}

#endif /* NINTENDO_3DS */

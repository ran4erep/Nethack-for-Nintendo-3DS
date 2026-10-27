/* NetHack 5.0	3dsart.c */
/* Copyright (c) Stichting Mathematisch Centrum, Amsterdam, 1985. */
/* NetHack may be freely redistributed.  See license for details. */

/*
 * Pixel renderer for the Nintendo 3DS touch screen.
 *
 * The touch screen is not drawn with the character console: every widget is
 * composited straight into the 320x240 RGB565 framebuffer, exactly the way
 * sys/3ds/3dstiles.c paints the top screen.  That gives real rounded corners,
 * gradients, drop shadows and antialiasing instead of coloured character
 * blocks.  Text is drawn with the very font the console uses, so the status
 * lines the core prints here stay in the same typeface.
 */

#include "hack.h"
#include "3dsi18n.h"
#include <3ds.h>
#include <math.h>

extern PrintConsole topConsole;
extern PrintConsole bottomConsole;

#define ART_W 320
#define ART_H 240

/* ---------------------------------------------------------------- */
/* colours                                                          */
/* ---------------------------------------------------------------- */

#define RGB(r, g, b) \
    ((u16) ((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3)))

#define C_CLEAR    0x0000
#define C_TEXT     RGB(236, 240, 246)

#define C_CAP_1    RGB(236, 240, 245) /* ordinary key cap, top            */
#define C_CAP_2    RGB(200, 208, 220) /* ordinary key cap, bottom         */
#define C_CAP_INK  RGB(24, 28, 34)
#define C_SPEC_1   RGB(64, 148, 214) /* special key cap, top             */
#define C_SPEC_2   RGB(38, 104, 160)
#define C_SPEC_INK RGB(244, 248, 252)
#define C_GO_1     RGB(78, 190, 118)
#define C_GO_2     RGB(44, 138, 80)
#define C_STOP_1   RGB(206, 78, 70)
#define C_STOP_2   RGB(152, 46, 40)

/* ---------------------------------------------------------------- */
/* framebuffer                                                      */
/* ---------------------------------------------------------------- */

static u16 *art_fb; /* valid only between art_open()/art_close() */

static void
art_open(void)
{
    art_fb = bottomConsole.frameBuffer;
}

static void
art_close(void)
{
    art_fb = NULL;
}

static void
art_px(int x, int y, u16 c)
{
    if ((unsigned) x < ART_W && (unsigned) y < ART_H)
        art_fb[x * ART_H + (ART_H - 1 - y)] = c;
}

static void
art_hline(int x, int y, int w, u16 c)
{
    int i;

    for (i = 0; i < w; i++)
        art_px(x + i, y, c);
}

static void
art_fill(int x, int y, int w, int h, u16 c)
{
    int i;

    for (i = 0; i < h; i++)
        art_hline(x, y + i, w, c);
}

/*
 * A snapshot of what sits behind the widgets.  Key caps are drawn with alpha,
 * so repainting one cap on top of itself would keep blending it over and over;
 * and repainting the whole screen on every touch is what made the keyboard
 * flicker.  Keeping a copy of the bare background lets a single cap be wiped
 * and redrawn on its own, and lets the keyboard be rolled in and out without
 * having to repaint the artwork.
 */
static u16 art_bg_cache[ART_W * ART_H]; /* the background alone            */
static int art_cache_status = -1;       /* status height the cache covers  */

/*
 * A screen row y lives at index (ART_H - 1 - y), so for a fixed x the rows
 * from `from_y` down are one contiguous run starting at x * ART_H.
 */
static void
art_rows_copy(u16 *dst, const u16 *src, int from_y)
{
    size_t n = (size_t) (ART_H - from_y) * sizeof(u16);
    int x;

    for (x = 0; x < ART_W; x++)
        memcpy(dst + x * ART_H, src + x * ART_H, n);
}

/* put one cap's worth of background back before it is redrawn */
static void
art_cache_restore(int x, int y, int w, int h)
{
    int i, j;

    for (i = 0; i < h; i++) {
        int py = y + i;

        if ((unsigned) py >= ART_H)
            continue;
        for (j = 0; j < w; j++) {
            int px = x + j;

            if ((unsigned) px >= ART_W)
                continue;
            art_fb[px * ART_H + (ART_H - 1 - py)]
                = art_bg_cache[px * ART_H + (ART_H - 1 - py)];
        }
    }
}

/* ---------------------------------------------------------------- */
/* colour maths                                                     */
/* ---------------------------------------------------------------- */

static u16
art_mix(u16 a, u16 b, int t) /* t = 0..256, 256 means "all b" */
{
    int ar = (a >> 11) & 31, ag = (a >> 5) & 63, ab = a & 31;
    int br = (b >> 11) & 31, bg = (b >> 5) & 63, bb = b & 31;

    return (u16) ((ar + (((br - ar) * t) >> 8)) << 11
                  | (ag + (((bg - ag) * t) >> 8)) << 5
                  | (ab + (((bb - ab) * t) >> 8)));
}

static u16
art_light(u16 c, int pct) /* pct > 100 brightens, < 100 darkens */
{
    int r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;

    r = r * pct / 100;
    g = g * pct / 100;
    b = b * pct / 100;
    if (r > 31) r = 31;
    if (g > 63) g = 63;
    if (b > 31) b = 31;
    return (u16) ((r << 11) | (g << 5) | b);
}

/* ---------------------------------------------------------------- */
/* optional artwork                                                 */
/*                                                                  */
/* sys/3ds/3dsart_data.c is generated by sys/3ds/mkart.ps1 from the */
/* optional png files in sys/3ds/art/.  When a piece is missing its */
/* pointer is NULL and the procedural artwork below is used.        */
/* ---------------------------------------------------------------- */

extern const int _3ds_art_bg_w, _3ds_art_bg_h;
extern const u16 *_3ds_art_bg_px;
extern const int _3ds_art_ico_w, _3ds_art_ico_h;
extern const u32 *_3ds_art_ico_play;
extern const u32 *_3ds_art_ico_gear;
extern const u32 *_3ds_art_ico_power;
extern const u32 *_3ds_art_ico_keyboard;

/*
 * Paint the background plate.  Everything at or above `from_y` is left
 * alone, which is what keeps the status lines the core prints at the top of
 * this screen from being wiped.
 */
static void
art_paint_bg(int from_y)
{
    int x, y;

    if (!_3ds_art_bg_px || _3ds_art_bg_w <= 0 || _3ds_art_bg_h <= 0) {
        art_fill(0, from_y, ART_W, ART_H - from_y, C_CLEAR);
        return;
    }
    for (y = from_y; y < ART_H; y++) {
        int sy = (y * _3ds_art_bg_h) / ART_H;
        const u16 *srow;

        if (sy >= _3ds_art_bg_h)
            sy = _3ds_art_bg_h - 1;
        srow = _3ds_art_bg_px + sy * _3ds_art_bg_w;
        for (x = 0; x < ART_W; x++) {
            int sx = (x * _3ds_art_bg_w) / ART_W;

            if (sx >= _3ds_art_bg_w)
                sx = _3ds_art_bg_w - 1;
            art_px(x, y, srow[sx]);
        }
    }
}

/*
 * Composite one RGBA icon (0xAARRGGBB) as a plain square picture centred on
 * (cx, cy) and scaled to a `size` x `size` box.  The artwork brings its own
 * transparent padding and is left square, so nothing is trimmed or reshaped.
 * When the source is finer than the box the source pixels are averaged rather
 * than sampled, which is what keeps a large drawing crisp once it is shrunk.
 */
static void
art_icon(const u32 *px, int cx, int cy, int size)
{
    int w = _3ds_art_ico_w, h = _3ds_art_ico_h;
    int x, y;

    if (!px || w <= 0 || h <= 0 || size <= 0)
        return;

    for (y = 0; y < size; y++) {
        int sy0 = (y * h) / size, sy1 = ((y + 1) * h) / size;
        int Y = cy - size / 2 + y;

        if ((unsigned) Y >= ART_H)
            continue;
        if (sy1 <= sy0)
            sy1 = sy0 + 1;
        for (x = 0; x < size; x++) {
            int sx0 = (x * w) / size, sx1 = ((x + 1) * w) / size;
            int X = cx - size / 2 + x;
            int sa = 0, sr = 0, sg = 0, sb = 0, n = 0, u, v, a;
            u16 c, *d;

            if ((unsigned) X >= ART_W)
                continue;
            if (sx1 <= sx0)
                sx1 = sx0 + 1;
            for (v = sy0; v < sy1; v++)
                for (u = sx0; u < sx1; u++) {
                    u32 p = px[v * w + u];

                    a = (int) (p >> 24) & 255;
                    n++;
                    sa += a;
                    sr += (int) ((p >> 16) & 255) * a;
                    sg += (int) ((p >> 8) & 255) * a;
                    sb += (int) (p & 255) * a;
                }
            if (!n || !sa)
                continue;
            /* average in premultiplied space so edges do not go dark */
            c = (u16) ((((sr / sa) >> 3) << 11) | (((sg / sa) >> 2) << 5)
                       | ((sb / sa) >> 3));
            a = sa / n;
            d = &art_fb[X * ART_H + (ART_H - 1 - Y)];
            *d = (a >= 255) ? c : art_mix(*d, c, a + 1);
        }
    }
}

/* ---------------------------------------------------------------- */
/* shapes                                                           */
/* ---------------------------------------------------------------- */

/*
 * A rounded rectangle painted with a vertical gradient from `ctop` to
 * `cbot`.  Corner pixels get coverage-based antialiasing, so the arcs stay
 * smooth instead of stepping.
 */
static void
art_round(int x, int y, int w, int h, int r, u16 ctop, u16 cbot, int alpha)
{
    float hw, hh, irx, iry;
    int i, j;

    if (w <= 0 || h <= 0)
        return;
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    if (r < 0) r = 0;

    hw = w * 0.5f;
    hh = h * 0.5f;
    irx = hw - (float) r;
    iry = hh - (float) r;

    for (i = 0; i < h; i++) {
        int py = y + i;
        u16 c = (h > 1) ? art_mix(ctop, cbot, (i * 256) / (h - 1)) : ctop;
        float qy = (float) i + 0.5f - hh;
        float ay = (qy < 0.f ? -qy : qy) - iry;

        if (py < 0 || py >= ART_H)
            continue;
        for (j = 0; j < w; j++) {
            int px = x + j, t;
            float qx, ax, d, cov;

            if (px < 0 || px >= ART_W)
                continue;
            if (ay <= 0.f) {
                qx = (float) j + 0.5f - hw;
                ax = (qx < 0.f ? -qx : qx) - irx;
                if (ax <= 0.f)
                    cov = 1.f;
                else {
                    d = sqrtf(ax * ax) - (float) r;
                    cov = 0.5f - d;
                }
            } else {
                qx = (float) j + 0.5f - hw;
                ax = (qx < 0.f ? -qx : qx) - irx;
                if (ax < 0.f) ax = 0.f;
                d = sqrtf(ax * ax + ay * ay) - (float) r;
                cov = 0.5f - d;
            }
            if (cov <= 0.f)
                continue;
            if (cov > 1.f)
                cov = 1.f;
            t = (int) (cov * (float) alpha);
            if (t >= 256)
                art_px(px, py, c);
            else if (t > 0)
                art_px(px, py, art_mix(art_fb[px * ART_H + (ART_H - 1 - py)],
                                       c, t));
        }
    }
}

/* A soft drop shadow: two progressively larger, fainter black discs. */
static void
art_shadow(int x, int y, int w, int h, int r)
{
    art_round(x - 1, y + 2, w + 2, h + 2, r + 1, C_CLEAR, C_CLEAR, 44);
    art_round(x - 2, y + 3, w + 4, h + 4, r + 2, C_CLEAR, C_CLEAR, 22);
}

/* ---------------------------------------------------------------- */
/* text, using the console's own glyphs                             */
/* ---------------------------------------------------------------- */

#define GLYPH_W 8
#define GLYPH_H 8

static void
art_copy(char *dst, const char *src, size_t max)
{
    size_t n = strlen(src);

    if (n > max - 1)
        n = max - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static void
art_text(int x, int y, const char *s, u16 col)
{
    const u8 *gfx = (const u8 *) bottomConsole.font.gfx;
    int off = (int) bottomConsole.font.asciiOffset;
    int num = (int) bottomConsole.font.numChars;

    if (!gfx || !s)
        return;
    for (; *s; s++, x += GLYPH_W) {
        int idx = (unsigned char) *s - off;
        const u8 *g;
        int i, j;

        if (idx < 0 || idx >= num)
            continue;
        g = gfx + idx * GLYPH_H;
        for (i = 0; i < GLYPH_H; i++) {
            u8 bits = g[i];

            if (!bits)
                continue;
            for (j = 0; j < GLYPH_W; j++)
                if (bits & (0x80 >> j))
                    art_px(x + j, y + i, col);
        }
    }
}

/*
 * The same glyph loop, but for any framebuffer (the top screen is 400 px
 * wide and has its own PrintConsole).  Both screens store a row y at
 * index x * 240 + (239 - y), so only the width has to be passed in.
 */
static void
art_text_fb(u16 *fb, int width, int x, int y, const char *s,
            const PrintConsole *pc, u16 col)
{
    const u8 *gfx;
    int off, num;

    if (!fb || !s || !pc)
        return;
    gfx = (const u8 *) pc->font.gfx;
    off = (int) pc->font.asciiOffset;
    num = (int) pc->font.numChars;
    if (!gfx)
        return;
    for (; *s; s++, x += GLYPH_W) {
        int idx = (unsigned char) *s - off;
        const u8 *g;
        int i, j;

        if (idx < 0 || idx >= num)
            continue;
        g = gfx + idx * GLYPH_H;
        for (i = 0; i < GLYPH_H; i++) {
            u8 bits = g[i];

            if (!bits)
                continue;
            for (j = 0; j < GLYPH_W; j++)
                if (bits & (0x80 >> j))
                    if ((unsigned) (x + j) < (unsigned) width
                        && (unsigned) (y + i) < ART_H)
                        fb[(x + j) * ART_H + (ART_H - 1 - (y + i))] = col;
        }
    }
}

/* Draw text into the top screen's framebuffer (used by the tile renderer
   for the floating combat numbers). */
void
_3ds_art_text_top(int x, int y, const char *s, u16 col)
{
    art_text_fb(topConsole.frameBuffer, 400, x, y, s, &topConsole, col);
}

static int
art_text_w(const char *s)
{
    return s ? (int) strlen(s) * GLYPH_W : 0;
}

/* the same glyphs blown up by a whole number, for headings */
static void
art_text_scaled(int x, int y, const char *s, u16 col, int scale)
{
    const u8 *gfx = (const u8 *) bottomConsole.font.gfx;
    int off = (int) bottomConsole.font.asciiOffset;
    int num = (int) bottomConsole.font.numChars;

    if (!gfx || !s || scale < 1)
        return;
    for (; *s; s++, x += GLYPH_W * scale) {
        int idx = (unsigned char) *s - off;
        const u8 *g;
        int i, j, k, l;

        if (idx < 0 || idx >= num)
            continue;
        g = gfx + idx * GLYPH_H;
        for (i = 0; i < GLYPH_H; i++) {
            u8 bits = g[i];

            if (!bits)
                continue;
            for (j = 0; j < GLYPH_W; j++) {
                if (!(bits & (0x80 >> j)))
                    continue;
                for (k = 0; k < scale; k++)
                    for (l = 0; l < scale; l++)
                        art_px(x + j * scale + l, y + i * scale + k, col);
            }
        }
    }
}

/*
 * Text with a contrasting border, so it stays readable straight on top of the
 * artwork instead of needing a plate painted behind it.
 */
static void
art_text_outline(int x, int y, const char *s, u16 col, u16 out, int scale)
{
    int dx, dy;

    for (dy = -scale; dy <= scale; dy++)
        for (dx = -scale; dx <= scale; dx++)
            if (dx || dy)
                art_text_scaled(x + dx, y + dy, s, out, scale);
    art_text_scaled(x, y, s, col, scale);
}

/* ---------------------------------------------------------------- */
/* little vector icons                                              */
/* ---------------------------------------------------------------- */

static void
art_icon_keyboard(int cx, int cy, int w, u16 col)
{
    int h = (w * 2) / 3;
    int x = cx - w / 2, y = cy - h / 2;
    int i, j;

    /* body */
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++)
            if (i == 0 || j == 0 || i == w - 1 || j == h - 1)
                art_px(x + i, y + j, col);
    /* key dots */
    for (j = 0; j < 2; j++)
        for (i = 0; i < 4; i++)
            art_px(x + 3 + i * ((w - 6) / 3), y + 2 + j * 3, col);
    /* space bar */
    art_hline(x + 3, y + h - 3, w - 6, col);
}

static void
art_icon_triangle(int cx, int cy, int s, u16 col)
{
    int i, half = s / 2;

    for (i = -half; i <= half; i++) {
        int w = half - (i < 0 ? -i : i);

        art_hline(cx - half, cy + i, w, col);
    }
}

static void
art_icon_gear(int cx, int cy, int r, u16 col)
{
    int dx, dy;

    for (dy = -r; dy <= r; dy++)
        for (dx = -r; dx <= r; dx++) {
            float d = sqrtf((float) (dx * dx + dy * dy));

            if (d >= (float) r * 0.45f && d <= (float) r * 0.78f)
                art_px(cx + dx, cy + dy, col);
        }
    art_fill(cx - 2, cy - r - 2, 4, 4, col);
    art_fill(cx - 2, cy + r - 1, 4, 4, col);
    art_fill(cx - r - 2, cy - 2, 4, 4, col);
    art_fill(cx + r - 1, cy - 2, 4, 4, col);
}

static void
art_icon_power(int cx, int cy, int r, u16 col)
{
    int dx, dy;

    for (dy = -r; dy <= r; dy++)
        for (dx = -r; dx <= r; dx++) {
            float d = sqrtf((float) (dx * dx + dy * dy));

            if (d <= (float) r && d >= (float) r - 2.2f) {
                if (dy >= 0 || (dx < -1 || dx > 1))
                    art_px(cx + dx, cy + dy, col);
            }
        }
    for (dy = -r - 1; dy <= 0; dy++)
        art_px(cx, cy + dy, col);
}

/* ---------------------------------------------------------------- */
/* the keyboard                                                     */
/* ---------------------------------------------------------------- */

enum {
    K_CHAR = K3D_CHAR,
    K_SHIFT = K3D_SHIFT,
    K_CTRL = K3D_CTRL,
    K_ALT = K3D_ALT,
    K_BACK = K3D_BACK,
    K_SPACE = K3D_SPACE,
    K_ENTER = K3D_ENTER,
    K_ESC = K3D_ESC,
    K_TAB = K3D_TAB,
    K_CAPS = K3D_CAPS,
    K_HOTBAR = K3D_HOTBAR,
    K_DIAG = K3D_DIAG /* diagonal-movement button */
};

/*
 * Keyboard metrics, tuned for the 320x240 touch panel: that screen is both
 * smaller and coarser than the 400x240 one above it, so the keys are kept
 * compact and are laid edge to edge rather than sitting in a wide margin.
 */
#define KB_ROWS   5
#define KB_MAXK   16
#define KB_KEY_H  21
#define KB_GAP    7  /* vertical gap between key rows                    */
#define KB_HGAP   1  /* horizontal gap between keys                      */
#define KB_SIDE   2  /* screen margin left and right of the key block    */
#define KB_BOTTOM 3  /* gap between the block and the bottom screen edge */
#define KB_BLOCK  (KB_ROWS * KB_KEY_H + (KB_ROWS - 1) * KB_GAP)

/*
 * How far a cap's drop shadow reaches below the cap.  The shadow is offset
 * downwards only, so it never reaches into the columns of the neighbouring
 * keys; that is what lets a single key be wiped and redrawn without clipping
 * a sliver off the key beside it.
 */
#define KB_DROP 5

/* natural key widths; the widest row is scaled to span the whole screen */
#define KB_NARROW 20
#define KB_WIDE   26
#define KB_CAPS   36 /* room for the four letters of "CAPS"      */
#define KB_SHIFT  61
#define KB_SPACE  251 /* only a starting point; see kb_build()    */
#define KB_ARM    43  /* the return key's arm, along the home row  */
#define KB_STEM   39  /* the return key's stem, in the row below   */
#define KB_HOT    64  /* a starting point for the hotbar toggle    */

/* the caps are a touch see through */
#define KB_CAP_ALPHA 240

/* height of the status strip the core prints at the top of this screen */
#define ART_STATUS_H ((iflags.wc2_statuslines >= 3) ? 24 : 16)

/* x/y of the toggle button (top right, right below the status strip) */
#define BTN_W 36
#define BTN_H 24
static int btn_x(void) { return 280; }
static int btn_y(void) { return ART_STATUS_H + 4; }

struct topbar_btn {
    short x, w;
    int id;
};

static const struct topbar_btn tb_buttons[10] = {
    { 3,   25, 1 },  /* Character */
    { 30,  25, 2 },  /* Zap */
    { 57,  25, 3 },  /* Cast */
    { 84,  25, 4 },  /* Fire */
    { 111, 25, 5 },  /* Glance */
    { 138, 25, 6 },  /* Throw */
    { 169, 25, 7 },  /* Diag Up-Left */
    { 196, 25, 8 },  /* Diag Up-Right */
    { 223, 25, 9 },  /* Diag Down-Left */
    { 250, 25, 10 }, /* Diag Down-Right */
};

struct kkey {
    short x, y, w;
    unsigned char kind;
    char ch;
    char label[8];
};

static int kb_kind_of(const char *, char);

/*
 * One row of the layout: the widths of the keys followed by their legends.
 * Legends are packed NUL separated so a single string describes a whole row,
 * and `count` says how many of them there are, so the walk never has to look
 * for a terminator that a malformed table would not have.
 *
 * The width slots are 0 for a normal key, 1 for a wide key, 2 for the space
 * bar, 3 for a shift key, 4/5 for the two halves of the return key, 6 for the
 * caps lock, which needs room for four letters, and 7 for the hotbar toggle,
 * whose width is worked out from the space bar at layout time.
 */
struct krow {
    const char *legend;
    const char *width;
    int count;
};

/*
 * The five key rows: a normal PC layout, so the one key that is not a plain
 * rectangle is the return key.  Legends are kept to three or four characters
 * so they still fit the cap once the row is scaled; the shift caps draw an
 * arrow instead.
 */
static const char kb_r0[] = "ESC\0" "1\0" "2\0" "3\0" "4\0" "5\0" "6\0" "7\0"
                            "8\0" "9\0" "0\0" "-\0" "=\0" "DEL";
static const char kb_r1[] = "TAB\0" "q\0" "w\0" "e\0" "r\0" "t\0" "y\0" "u\0"
                            "i\0" "o\0" "p\0" "[\0" "]\0" "\\";
static const char kb_r2[] = "CAPS\0" "a\0" "s\0" "d\0" "f\0" "g\0" "h\0" "j\0"
                            "k\0" "l\0" ";\0" "'\0" "RET";
static const char kb_r3[] = "SHIFT\0" "z\0" "x\0" "c\0" "v\0" "b\0" "n\0"
                            "m\0" ",\0" ".\0" "/\0" "RET";
static const char kb_r4[] = "CTRL\0" "ALT\0" "SPACE\0"
                            "DIAG\0" "DIAG\0" "DIAG\0" "DIAG";

static const char kb_w0[] = { 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1 };
static const char kb_w1[] = { 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
static const char kb_w2[] = { 6, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4 };
static const char kb_w3[] = { 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 5 };
static const char kb_w4[] = { 1, 1, 2, 0, 0, 0, 0 };

static const struct krow kb_rows[KB_ROWS] = {
    { kb_r0, kb_w0, 14 }, { kb_r1, kb_w1, 14 }, { kb_r2, kb_w2, 13 },
    { kb_r3, kb_w3, 12 }, { kb_r4, kb_w4, 7 }
};

static struct kkey kb_keys[KB_ROWS][KB_MAXK];
static int kb_count[KB_ROWS];
static int kb_ready = 0;

/*
 * The return key is the one cap drawn as a single shape rather than as a
 * rectangle, so its two halves are remembered here once the layout is known:
 * the arm sits on the home row and the stem drops into the row below it, and
 * the two share a right hand edge.
 */
static int kb_ent_x, kb_ent_y, kb_ent_w;  /* the arm                       */
static int kb_ent_sx, kb_ent_sw;          /* the stem, right edge aligned  */
static int kb_ent_h;                      /* arm top to stem bottom        */

/* the rectangle the quick command grid occupies, once the layout is known */
static int hot_x, hot_y, hot_w, hot_h;
static int hot_sy; /* the spare slots share the bottom row of keys */

/* the width a slot asks for */
static int
kb_slot_w(int slot)
{
    switch (slot) {
    case 7:
        return KB_HOT; /* replaced by whatever the space bar leaves */
    case 6:
        return KB_CAPS;
    case 5:
        return KB_STEM;
    case 4:
        return KB_ARM;
    case 3:
        return KB_SHIFT;
    case 2:
        return KB_SPACE;
    case 1:
        return KB_WIDE;
    default:
        return KB_NARROW;
    }
}

/*
 * Lay the keyboard out once.  The widest letter row decides the scale so the
 * keys span the screen from margin to margin, and the block is pinned to the
 * bottom edge.
 */
static void
kb_build(void)
{
    int r, i, n, total, maxnat = 0, avail, x, y;

    if (kb_ready)
        return;
    kb_ready = 1;

    /*
     * Work out what every key is first, but leave the bottom row out of the
     * scaling below: its space bar is not a fixed width, it is stretched to
     * sit under the letters c to m, so it can only be placed once the rest of
     * the board is known.
     */
    for (r = 0; r < KB_ROWS; r++) {
        const char *leg = kb_rows[r].legend;

        n = kb_rows[r].count;
        kb_count[r] = n;
        for (i = 0; i < n; i++) {
            kb_keys[r][i].ch = (char) leg[0];
            art_copy(kb_keys[r][i].label, leg, sizeof kb_keys[r][i].label);
            kb_keys[r][i].kind = (unsigned char) kb_kind_of(leg, leg[0]);
            kb_keys[r][i].w
                = (short) kb_slot_w((unsigned char) kb_rows[r].width[i]);
            leg += strlen(leg) + 1;
        }
    }

    /* pass one: the natural widths of the letter rows decide the scale */
    for (r = 0; r < KB_ROWS - 1; r++) {
        n = kb_count[r];
        total = (n - 1) * KB_HGAP;
        for (i = 0; i < n; i++)
            total += kb_keys[r][i].w;
        if (total > maxnat)
            maxnat = total;
    }

    /* pass two: scale every letter row by the same factor so they fill it */
    avail = ART_W - 2 * KB_SIDE;
    for (r = 0; r < KB_ROWS - 1; r++) {
        n = kb_count[r];
        total = 0;
        for (i = 0; i < n; i++) {
            kb_keys[r][i].w = (short) ((kb_keys[r][i].w * avail) / maxnat);
            total += kb_keys[r][i].w;
        }
        total += (n - 1) * KB_HGAP;
        x = (ART_W - total) / 2;
        for (i = 0; i < n; i++) {
            kb_keys[r][i].x = (short) x;
            x += kb_keys[r][i].w + KB_HGAP;
        }
    }

    /* pin the block to the bottom edge */
    y = ART_H - KB_BOTTOM - KB_BLOCK;
    for (r = 0; r < KB_ROWS; r++) {
        for (i = 0; i < kb_count[r]; i++)
            kb_keys[r][i].y = (short) y;
        y += KB_KEY_H + KB_GAP;
    }

    /*
     * Find the return key's two halves: the first one met is the arm, on the
     * home row, and the second is the stem, one row down.  The stem is nudged
     * so its right hand edge lines up with the arm's, which is what turns the
     * pair into a single L rather than two loose caps.
     */
    for (r = 0; r < KB_ROWS; r++)
        for (i = 0; i < kb_count[r]; i++)
            if (kb_keys[r][i].kind == K_ENTER) {
                if (!kb_ent_w) {
                    kb_ent_x = kb_keys[r][i].x;
                    kb_ent_y = kb_keys[r][i].y;
                    kb_ent_w = kb_keys[r][i].w;
                } else if (!kb_ent_sw) {
                    kb_ent_sw = kb_keys[r][i].w;
                    kb_ent_sx = kb_ent_x + kb_ent_w - kb_ent_sw;
                    kb_ent_h = kb_keys[r][i].y + KB_KEY_H - kb_ent_y;
                    kb_keys[r][i].x = (short) kb_ent_sx;
                }
            }

    /*
     * The bottom row.  The space bar sits exactly under the letters c to m,
     * and ctrl and alt share what is left of the row up to it.  The area to
     * the right of the space bar (where the hotbar toggle used to be) holds
     * the four diagonal-movement buttons in a row: up-left, up-right,
     * down-left, down-right.
     */
    {
        int row = KB_ROWS - 2; /* the shift row the space bar spans */
        int x0 = kb_keys[row][0].x, sx = x0, sr = ART_W, room, each;
        int ahx, arw, k;

        for (i = 0; i < kb_count[row]; i++) {
            int c = (unsigned char) kb_keys[row][i].ch;

            if (c == 'c')
                sx = kb_keys[row][i].x;
            else if (c == 'm')
                sr = kb_keys[row][i].x + kb_keys[row][i].w;
        }

        room = sx - KB_HGAP - x0;
        each = (room - KB_HGAP) / 2;
        kb_keys[KB_ROWS - 1][0].x = (short) x0;
        kb_keys[KB_ROWS - 1][0].w = (short) each;
        kb_keys[KB_ROWS - 1][1].x = (short) (x0 + each + KB_HGAP);
        kb_keys[KB_ROWS - 1][1].w = (short) each;
        kb_keys[KB_ROWS - 1][2].x = (short) sx;
        kb_keys[KB_ROWS - 1][2].w = (short) (sr - sx);

        /* the four diagonal buttons, filling the rest of the row */
        ahx = sr + KB_HGAP;
        arw = ((kb_ent_x + kb_ent_w) - ahx - 3 * KB_HGAP) / 4;
        for (k = 0; k < 4; k++) {
            kb_keys[KB_ROWS - 1][3 + k].x = (short) (ahx + k * (arw + KB_HGAP));
            kb_keys[KB_ROWS - 1][3 + k].w = (short) arw;
        }
    }

    /*
     * The command grid: it replaces the letter keys, so it covers what they
     * cover and reaches up to the keyboard toggle button.  The bottom row is
     * left alone, so the hotbar toggle stays where it was and can switch the
     * keyboard back.
     */
    hot_x = kb_keys[KB_ROWS - 2][0].x;
    hot_w = (kb_ent_x + kb_ent_w) - hot_x;
    hot_y = ART_STATUS_H + 32;
    hot_h = kb_keys[KB_ROWS - 1][0].y - KB_GAP - hot_y;
    hot_sy = kb_keys[KB_ROWS - 1][0].y;
}

/* what a key actually is, once the legend is known */
static int
kb_kind_of(const char *legend, char ch)
{
    if (!strcmp(legend, "SHIFT"))
        return K_SHIFT;
    if (!strcmp(legend, "CTRL") || !strcmp(legend, "CTL"))
        return K_CTRL;
    if (!strcmp(legend, "CAPS"))
        return K_CAPS;
    if (!strcmp(legend, "ALT"))
        return K_ALT;
    if (!strcmp(legend, "DEL"))
        return K_BACK;
    if (!strcmp(legend, "SPACE"))
        return K_SPACE;
    if (!strcmp(legend, "DIAG"))
        return K_DIAG;
    if (!strcmp(legend, "HOTBAR"))
        return K_HOTBAR;
    if (!strcmp(legend, "RET"))
        return K_ENTER;
    if (!strcmp(legend, "ESC"))
        return K_ESC;
    if (!strcmp(legend, "TAB"))
        return K_TAB;
    nhUse(ch);
    return K_CHAR;
}

/*
 * The caps shift turns into something else, and what they turn into.  A cap
 * always shows what tapping it is about to type, so the legends follow this.
 * shift_char() in 3dstty.c is the other half of the pair and its tables have
 * to agree with these.  Letters are not listed because shift and caps lock
 * both merely capitalise them, and caps lock on its own never reaches the
 * symbols, the way a real board behaves.
 */
static const char kbd_plain[] = "1234567890-=[]\\;',./";
static const char kbd_upper[] = "!@#$%^&*()_+{}|:\"<>?";

/* the arrow the shift caps carry, so no long word has to be squeezed in */
static void
kbd_arrow(int cx, int cy, u16 col)
{
    int i;

    for (i = 0; i < 5; i++)
        art_hline(cx - i, cy - 5 + i, 2 * i + 1, col);
    art_fill(cx - 1, cy, 3, 6, col);
}

/*
 * A soft drop shadow under a cap.  The offsets are downward only, so the
 * shadow stays inside the key's own columns and repainting one key cannot
 * clip a sliver off the key next to it.
 */
static void
kbd_shadow(int x, int y, int w, int h)
{
    art_round(x, y + 2, w, h, 5, C_CLEAR, C_CLEAR, 40);
    art_round(x, y + 4, w, h + 1, 5, C_CLEAR, C_CLEAR, 20);
}

/*
 * Coverage of a rounded rectangle at pixel (j, i), where (j, i) is measured
 * from the rectangle's top left corner.  This is the same signed distance
 * field art_round() uses, broken out so two of them can be unioned into one
 * shape.
 */
static float
kb_cov(int j, int i, int w, int h, int r)
{
    float hw, hh, irx, iry, ax, ay, d;

    if (r > w / 2)
        r = w / 2;
    if (r > h / 2)
        r = h / 2;
    if (r < 0)
        r = 0;

    hw = w * 0.5f;
    hh = h * 0.5f;
    irx = hw - (float) r;
    iry = hh - (float) r;
    ax = (float) j + 0.5f - hw;
    ay = (float) i + 0.5f - hh;
    if (ax < 0.f)
        ax = -ax;
    if (ay < 0.f)
        ay = -ay;
    ax -= irx;
    ay -= iry;
    if (ax < 0.f)
        ax = 0.f;
    if (ay < 0.f)
        ay = 0.f;
    d = sqrtf(ax * ax + ay * ay) - (float) r;
    d = 0.5f - d;
    if (d < 0.f)
        return 0.f;
    if (d > 1.f)
        return 1.f;
    return d;
}

/* the little "return" glyph the enter cap carries, pointing back to the left */
static void
kbd_ret(int cx, int cy, u16 col)
{
    int i;

    art_hline(cx - 5, cy, 10, col);      /* the shaft                 */
    art_fill(cx + 4, cy - 5, 2, 6, col); /* it turns up on the right  */
    for (i = 0; i < 5; i++) {            /* and comes back on the left */
        art_px(cx - 5 + i, cy - i, col);
        art_px(cx - 5 + i, cy + i, col);
    }
}

/*
 * Put the whole return key's footprint back before it is redrawn.  Two
 * rectangles rather than one: the arm overhangs the stem on its left, and a
 * single box around the pair would reach into the columns of the key that
 * sits under the overhang on the row below.
 */
static void
kbd_enter_restore(void)
{
    art_cache_restore(kb_ent_x, kb_ent_y, kb_ent_w, KB_KEY_H + KB_GAP);
    art_cache_restore(kb_ent_sx, kb_ent_y, kb_ent_sw, kb_ent_h + KB_DROP);
}

/*
 * The return key.  On a real board it is the one cap that is not a plain
 * rectangle: a wide arm along the home row with a narrower stem dropping into
 * the row below, both flush with the right hand edge.  The two rectangles are
 * unioned so the step reads as a single key and the seam between the rows is
 * filled in.
 */
static void
kbd_enter(void)
{
    int i, j, h = kb_ent_h;

    if (kb_ent_w <= 0 || h <= 0)
        return;

    /* the shadow follows the outline, so it is cast by both halves */
    kbd_shadow(kb_ent_x, kb_ent_y, kb_ent_w, KB_KEY_H);
    kbd_shadow(kb_ent_sx, kb_ent_y, kb_ent_sw, h);

    for (i = 0; i < h; i++) {
        int py = kb_ent_y + i;
        u16 c;

        if ((unsigned) py >= ART_H)
            continue;
        c = art_mix(C_GO_1, C_GO_2, (h > 1) ? (i * 256) / (h - 1) : 0);
        for (j = 0; j < kb_ent_w; j++) {
            int px = kb_ent_x + j;
            float cov, sc;

            if ((unsigned) px >= ART_W)
                continue;
            cov = (i < KB_KEY_H) ? kb_cov(j, i, kb_ent_w, KB_KEY_H, 5) : 0.f;
            sc = kb_cov(px - kb_ent_sx, i, kb_ent_sw, h, 5);
            if (sc > cov)
                cov = sc;
            if (cov <= 0.f)
                continue;
            if (cov >= 1.f)
                art_px(px, py, c);
            else
                art_px(px, py,
                       art_mix(art_fb[px * ART_H + (ART_H - 1 - py)], c,
                               (int) (cov * KB_CAP_ALPHA)));
        }
    }
    /* a one pixel highlight along the top edge gives the cap a bevel */
    art_hline(kb_ent_x + 4, kb_ent_y + 1, kb_ent_w - 8, art_light(C_GO_1, 118));
    /* the glyph sits in the middle of the whole L, not of its wide arm */
    kbd_ret(kb_ent_x + kb_ent_w / 2, kb_ent_y + kb_ent_h / 2, C_SPEC_INK);
}

static void
kbd_cap(int x, int y, int w, int h, int style, const char *label, int arrow)
{
    u16 t, b, ink;
    int lx, ly;

    switch (style) {
    case 1: /* special */
        t = C_SPEC_1, b = C_SPEC_2, ink = C_SPEC_INK;
        break;
    case 2: /* latched */
        t = C_GO_1, b = C_GO_2, ink = C_SPEC_INK;
        break;
    case 3: /* stop */
        t = C_STOP_1, b = C_STOP_2, ink = C_SPEC_INK;
        break;
    default: /* ordinary */
        t = C_CAP_1, b = C_CAP_2, ink = C_CAP_INK;
        break;
    }

    kbd_shadow(x, y, w, h);
    art_round(x, y, w, h, 5, t, b, KB_CAP_ALPHA);
    /* a one pixel highlight along the top edge gives the cap a bevel */
    art_hline(x + 4, y + 1, w - 8, art_light(t, 118));

    if (arrow)
        kbd_arrow(x + w / 2, y + h / 2, ink);
    else if (label && *label) {
        lx = x + (w - art_text_w(label)) / 2;
        ly = y + (h - GLYPH_H) / 2;
        art_text(lx, ly, label, ink);
    }
}

/*
 * Style and label for one cap.  Returns the style: 0 ordinary, 1 special,
 * 2 latched, 3 stop.  The label buffer must hold 8 bytes.  Shift and caps
 * lock turn the letters on the caps into capitals, the way a phone keyboard
 * does, and shift swaps the digits and punctuation for the symbols it would
 * type, so the board always shows what a tap will produce and not just what
 * the latch will do.
 */
static int
kbd_style(const struct kkey *k, int shift, int ctrl, int alt, int caps,
          char *label)
{
    int style = 0;

    art_copy(label, k->label, 8);
    switch (k->kind) {
    case K_SHIFT:
    case K_CTRL:
    case K_ALT:
        style = ((k->kind == K_SHIFT && shift) || (k->kind == K_CTRL && ctrl)
                 || (k->kind == K_ALT && alt))
                    ? 2
                    : 1;
        break;
    case K_CAPS:
        style = caps ? 2 : 1;
        break;
    case K_ENTER:
        style = 2;
        break;
    case K_BACK:
    case K_ESC:
    case K_TAB:
        style = 1;
        break;
    case K_CHAR: {
        const char *p;

        if (shift && (p = strchr(kbd_plain, k->ch)) != NULL) {
            /* the cap shows the symbol shift is about to type */
            label[0] = kbd_upper[p - kbd_plain];
            label[1] = '\0';
        } else if ((shift ^ caps) && label[0] >= 'a' && label[0] <= 'z') {
            label[0] = (char) toupper((unsigned char) label[0]);
        }
        break;
    }
    default:
        break;
    }
    if (k->kind == K_SPACE)
        label[0] = '\0';
    return style;
}

/*
 * Repaint a single key.  What used to be under the cap is put back first, so
 * tapping a key does not have to repaint the whole screen (which is what
 * made it flicker) and the translucent caps do not blend onto themselves.
 */

/* diagonal arrows are drawn by hot_arrow() (defined below) */
static void hot_arrow(int cx, int cy, int dx, int dy, int L, u16 col);

static void
kbd_one(int r, int i, int shift, int ctrl, int alt, int caps, int hot)
{
    const struct kkey *k = &kb_keys[r][i];
    char label[8];
    int style;

    if (k->kind == K_ENTER) {
        /* whichever half was touched, the same single L gets repainted */
        kbd_enter_restore();
        kbd_enter();
        return;
    }
    if (k->kind == K_DIAG) {
        /* the four diagonal-movement buttons, left to right: up-left,
           up-right, down-left, down-right */
        int g = i - 3;
        int dx = (g % 2) ? 1 : -1;
        int dy = (g / 2) ? 1 : -1;
        int L = (k->w / 2) - 2;

        if (L > 6)
            L = 6;
        if (L < 4)
            L = 4;
        art_cache_restore(k->x, k->y, k->w, KB_KEY_H + KB_DROP);
        kbd_cap(k->x, k->y, k->w, KB_KEY_H, 1, "", 0);
        hot_arrow(k->x + k->w / 2, k->y + KB_KEY_H / 2, dx, dy, L,
                  C_SPEC_INK);
        return;
    }
    if (k->kind == K_HOTBAR) {
        /* a toggle, so it reads as pressed while the grid is showing */
        art_cache_restore(k->x, k->y, k->w, KB_KEY_H + KB_DROP);
        kbd_cap(k->x, k->y, k->w, KB_KEY_H, hot ? 2 : 1, k->label, 0);
        return;
    }
    style = kbd_style(k, shift, ctrl, alt, caps, label);
    art_cache_restore(k->x, k->y, k->w, KB_KEY_H + KB_DROP);
    kbd_cap(k->x, k->y, k->w, KB_KEY_H, style, label, k->kind == K_SHIFT);
}

static void
kbd_keys(int shift, int ctrl, int alt, int caps, int hot)
{
    int r, i;

    for (r = 0; r < KB_ROWS; r++)
        for (i = 0; i < kb_count[r]; i++)
            kbd_one(r, i, shift, ctrl, alt, caps, hot);
}

/*
 * The quick commands.  Sorted for reading rather than for their keys, which
 * are never shown: a player looking for "quaff" should find it where the
 * alphabet says it is, not where the keyboard happens to be laid out.  Each
 * one sends the key that runs it, control characters written out in full.
 */
/* The hotbar toggle now shows a four-button diagonal-movement pad (up-left,
   up-right, down-left, down-right) instead of the old command grid. */
#define HOT_COLS  2
#define HOT_ROWS  2
#define HOT_GRID  (HOT_COLS * HOT_ROWS)
#define HOT_CMDS  HOT_GRID
#define HOT_SLOTS (HOT_GRID + 2)
#define HOT_GAPX  8
#define HOT_GAPY  8

static const struct hotcmd {
    const char *label;
    char key;
} hot_cmds[HOT_CMDS] = {
    { "", '7' }, /* up-left   */
    { "", '9' }, /* up-right  */
    { "", '1' }, /* down-left */
    { "", '3' }  /* down-right */
};

/* where one cell sits: the first eighteen in the grid, the last two in the
   bottom row, lined up under the first two columns of the grid above */
static void
hot_cell(int i, int *x, int *y, int *w, int *h)
{
    int cw = (hot_w - (HOT_COLS - 1) * HOT_GAPX) / HOT_COLS;
    int chh = (hot_h - (HOT_ROWS - 1) * HOT_GAPY) / HOT_ROWS;

    if (i >= HOT_GRID) {
        *w = cw;
        *h = KB_KEY_H;
        *x = hot_x + (i - HOT_GRID) * (cw + HOT_GAPX);
        *y = hot_sy;
        return;
    }
    *w = cw;
    *h = chh;
    *x = hot_x + (i % HOT_COLS) * (cw + HOT_GAPX);
    *y = hot_y + (i / HOT_COLS) * (chh + HOT_GAPY);
}

/* a diagonal arrow for the movement pad, pointing toward (dx, dy); L is
   the half-length of the shaft in pixels */
static void
hot_arrow(int cx, int cy, int dx, int dy, int L, u16 col)
{
    int i, k;
    int tx = cx + dx * L;
    int ty = cy + dy * L;
    int head = (L >= 6) ? 5 : 4;
    int nx = -dy;
    int ny = dx;

    /* the shaft: 2 pixels wide perpendicular to direction */
    for (i = -L; i <= L; i++) {
        int px = cx + dx * i;
        int py = cy + dy * i;
        art_px(px, py, col);
        art_px(px + nx, py + ny, col);
    }
    /* the arrowhead wings opening back from the tip */
    for (k = 0; k <= head; k++) {
        /* horizontal wing going backwards against dx */
        art_px(tx - dx * k, ty, col);
        art_px(tx - dx * k, ty - dy, col);
        /* vertical wing going backwards against dy */
        art_px(tx, ty - dy * k, col);
        art_px(tx - dx, ty - dy * k, col);
    }
}

/* User login profile avatar icon: circular head + curved shoulders */
static void
art_icon_character(int cx, int cy, u16 col)
{
    int dx, dy;

    /* Head: circle radius 2.6 at cy - 3 */
    for (dy = -3; dy <= 3; dy++) {
        for (dx = -3; dx <= 3; dx++) {
            if (dx * dx + dy * dy <= 7)
                art_px(cx + dx, cy - 3 + dy, col);
        }
    }
    /* Shoulders / body below head */
    art_hline(cx - 2, cy + 1, 5, col);
    art_hline(cx - 4, cy + 2, 9, col);
    art_hline(cx - 6, cy + 3, 13, col);
    art_hline(cx - 7, cy + 4, 15, col);
    art_hline(cx - 7, cy + 5, 15, col);
}

/* Magic wand icon: tilted shaft with sparks at the tip */
static void
art_icon_zap(int cx, int cy, u16 col)
{
    int i;

    /* Wand shaft: diagonal line from bottom-left to center-right */
    for (i = 0; i <= 8; i++) {
        int x = cx - 6 + i;
        int y = cy + 6 - i;
        art_px(x, y, col);
        art_px(x, y + 1, col);
    }
    /* Sparkle / star at wand tip (cx + 3, cy - 3) */
    art_px(cx + 3, cy - 6, col);
    art_px(cx + 3, cy - 5, col);
    art_px(cx + 3, cy - 4, col);
    art_px(cx + 3, cy - 2, col);
    art_px(cx + 3, cy - 1, col);
    art_px(cx + 1, cy - 3, col);
    art_px(cx + 2, cy - 3, col);
    art_px(cx + 4, cy - 3, col);
    art_px(cx + 5, cy - 3, col);
    /* Tiny sparkles */
    art_px(cx, cy - 5, col);
    art_px(cx + 6, cy - 5, col);
    art_px(cx + 5, cy, col);
}

/* Magic / spell cast icon: arcane star with radiant flares */
static void
art_icon_cast(int cx, int cy, u16 col)
{
    int i;

    /* Central core */
    art_fill(cx - 1, cy - 1, 3, 3, col);
    /* 4 cardinal rays */
    for (i = 2; i <= 6; i++) {
        art_px(cx, cy - i, col);
        art_px(cx, cy + i, col);
        art_px(cx - i, cy, col);
        art_px(cx + i, cy, col);
    }
    /* 4 diagonal rays */
    for (i = 2; i <= 4; i++) {
        art_px(cx + i, cy + i, col);
        art_px(cx - i, cy + i, col);
        art_px(cx + i, cy - i, col);
        art_px(cx - i, cy - i, col);
    }
    /* Outer mystic particles */
    art_px(cx + 5, cy + 5, col);
    art_px(cx - 5, cy + 5, col);
    art_px(cx + 5, cy - 5, col);
    art_px(cx - 5, cy - 5, col);
}

/* Abstract fire / shot icon: projectile head flying right with motion trails */
static void
art_icon_fire(int cx, int cy, u16 col)
{
    int i;

    /* Pointed projectile head pointing right */
    for (i = 0; i <= 4; i++) {
        art_px(cx + 5 - i, cy - i, col);
        art_px(cx + 5 - i, cy + i, col);
        art_px(cx + 4 - i, cy - i, col);
        art_px(cx + 4 - i, cy + i, col);
    }
    /* Center core */
    art_fill(cx - 1, cy - 1, 4, 3, col);
    /* Motion speed trails behind */
    art_hline(cx - 7, cy - 3, 5, col);
    art_hline(cx - 8, cy, 6, col);
    art_hline(cx - 7, cy + 3, 5, col);
}

/* Eye icon for glance */
static void
art_icon_glance(int cx, int cy, u16 col)
{
    int dx, dy;

    /* Upper eyelid */
    art_px(cx - 6, cy, col);     art_px(cx + 6, cy, col);
    art_px(cx - 5, cy - 1, col); art_px(cx + 5, cy - 1, col);
    art_px(cx - 4, cy - 2, col); art_px(cx + 4, cy - 2, col);
    art_px(cx - 3, cy - 3, col); art_px(cx + 3, cy - 3, col);
    art_px(cx - 2, cy - 4, col); art_px(cx + 2, cy - 4, col);
    art_px(cx - 1, cy - 4, col); art_px(cx + 1, cy - 4, col);
    art_px(cx, cy - 4, col);
    /* Lower eyelid */
    art_px(cx - 5, cy + 1, col); art_px(cx + 5, cy + 1, col);
    art_px(cx - 4, cy + 2, col); art_px(cx + 4, cy + 2, col);
    art_px(cx - 3, cy + 3, col); art_px(cx + 3, cy + 3, col);
    art_px(cx - 2, cy + 4, col); art_px(cx + 2, cy + 4, col);
    art_px(cx - 1, cy + 4, col); art_px(cx + 1, cy + 4, col);
    art_px(cx, cy + 4, col);
    /* Pupil / iris */
    for (dy = -2; dy <= 2; dy++) {
        for (dx = -2; dx <= 2; dx++) {
            if (dx * dx + dy * dy <= 4)
                art_px(cx + dx, cy + dy, col);
        }
    }
}

/* Abstract throw icon: ballistic throw trajectory arc with airborne projectile */
static void
art_icon_throw(int cx, int cy, u16 col)
{
    /* Parabolic throw arc */
    art_px(cx - 6, cy + 5, col); art_px(cx - 6, cy + 4, col);
    art_px(cx - 5, cy + 3, col); art_px(cx - 5, cy + 2, col);
    art_px(cx - 4, cy + 1, col); art_px(cx - 4, cy, col);
    art_px(cx - 3, cy - 1, col); art_px(cx - 3, cy - 2, col);
    art_px(cx - 2, cy - 3, col); art_px(cx - 1, cy - 4, col);
    art_px(cx, cy - 4, col);     art_px(cx + 1, cy - 4, col);
    art_px(cx + 2, cy - 3, col);
    /* Flying object at end of throw */
    art_px(cx + 5, cy - 4, col);
    art_px(cx + 4, cy - 3, col); art_px(cx + 5, cy - 3, col); art_px(cx + 6, cy - 3, col);
    art_px(cx + 3, cy - 2, col); art_px(cx + 4, cy - 2, col); art_px(cx + 5, cy - 2, col); art_px(cx + 6, cy - 2, col);
    art_px(cx + 4, cy - 1, col); art_px(cx + 5, cy - 1, col);
    art_px(cx + 5, cy, col);
}

/* the pad, drawn the same way as the keys so the two modes match */
static void
hot_draw(void)
{
    int i;

    for (i = 0; i < HOT_GRID; i++) {
        int x, y, w, h;

        hot_cell(i, &x, &y, &w, &h);
        kbd_shadow(x, y, w, h);
        art_round(x, y, w, h, 5, C_CAP_1, C_CAP_2, KB_CAP_ALPHA);
        art_hline(x + 4, y + 1, w - 8, art_light(C_CAP_1, 118));
        hot_arrow(x + w / 2, y + h / 2,
                  (i == 1 || i == 3) ? 1 : -1,
                  (i >= 2) ? 1 : -1, 11, C_CAP_INK);
    }
}

/* the round-ish toggle button in the top right corner */
static void
kbd_button(int visible)
{
    int x = btn_x(), y = btn_y();
    u16 t = visible ? C_STOP_1 : C_SPEC_1;
    u16 b = visible ? C_STOP_2 : C_SPEC_2;
    u16 ink = C_SPEC_INK;

    art_shadow(x, y, BTN_W, BTN_H, 5);
    art_round(x, y, BTN_W, BTN_H, 5, t, b, 256);
    art_hline(x + 4, y + 1, BTN_W - 8, art_light(t, 118));
    /* the icon alone says what the button does, so it sits in the middle */
    if (_3ds_art_ico_keyboard)
        art_icon(_3ds_art_ico_keyboard, x + BTN_W / 2, y + BTN_H / 2, 18);
    else
        art_icon_keyboard(x + BTN_W / 2, y + BTN_H / 2, 16, ink);
}

/* top toolbar row: action buttons, always-visible diagonal arrows, and toggle button */
static void
kbd_topbar(int visible)
{
    int y = btn_y();
    int i;
    u16 ink = C_SPEC_INK;

    for (i = 0; i < 10; i++) {
        int x = tb_buttons[i].x;
        int w = tb_buttons[i].w;
        int cx = x + w / 2;
        int cy = y + BTN_H / 2;

        art_shadow(x, y, w, BTN_H, 4);
        art_round(x, y, w, BTN_H, 4, C_SPEC_1, C_SPEC_2, 256);
        art_hline(x + 3, y + 1, w - 6, art_light(C_SPEC_1, 118));

        switch (i) {
        case 0: /* Character */
            art_icon_character(cx, cy, ink);
            break;
        case 1: /* Zap */
            art_icon_zap(cx, cy, ink);
            break;
        case 2: /* Cast */
            art_icon_cast(cx, cy, ink);
            break;
        case 3: /* Fire */
            art_icon_fire(cx, cy, ink);
            break;
        case 4: /* Glance */
            art_icon_glance(cx, cy, ink);
            break;
        case 5: /* Throw */
            art_icon_throw(cx, cy, ink);
            break;
        case 6: /* Diag Up-Left */
            hot_arrow(cx, cy, -1, -1, 6, ink);
            break;
        case 7: /* Diag Up-Right */
            hot_arrow(cx, cy, 1, -1, 6, ink);
            break;
        case 8: /* Diag Down-Left */
            hot_arrow(cx, cy, -1, 1, 6, ink);
            break;
        case 9: /* Diag Down-Right */
            hot_arrow(cx, cy, 1, 1, 6, ink);
            break;
        default:
            break;
        }
    }

    /* Toggle keyboard button */
    kbd_button(visible);
}

/* ---------------------------------------------------------------- */
/* public surface                                                   */
/* ---------------------------------------------------------------- */

int
_3ds_art_status_h(void)
{
    return ART_STATUS_H;
}

/*
 * The bottom line of the top screen is reserved for the hero summary (name,
 * hit points, power, gold).  The map never paints there, so a plain black
 * strip with white text is all it needs.  This does not touch the character
 * console's cursor: it writes pixels straight into the top framebuffer, the
 * same way the tile renderer does.
 *
 * As well as the text, a health bar is painted behind the name/title: it is
 * filled by the current hit-point fraction and coloured by how healthy the
 * hero is, so the player can read their health at a glance.
 */

static void
top_fill(u16 *fb, int px, int py, int w, int h, u16 color)
{
    int x, y;

    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++) {
            int pxx = px + x, pyy = py + y;

            if ((unsigned) pxx >= 400u || (unsigned) pyy >= (unsigned) ART_H)
                continue;
            fb[pxx * ART_H + (ART_H - 1 - pyy)] = color;
        }
}

/* Text with a one-pixel dark outline, so it stays readable on top of the
   health bar whatever colour the fill happens to be. */
static void
top_text_outline(u16 *fb, int x, int y, const char *s, u16 ink, u16 out)
{
    int dx, dy;

    for (dy = -1; dy <= 1; dy++)
        for (dx = -1; dx <= 1; dx++)
            if (dx || dy)
                art_text_fb(fb, 400, x + dx, y + dy, s, &topConsole, out);
    art_text_fb(fb, 400, x, y, s, &topConsole, ink);
}

/* Shared health classification used by both the top-screen status bar and
   the notification LED (3dsled.c).  The colour bands are the status bar's
   own: above half hit points is good (green), from a quarter up to half is
   wounded (amber), and a quarter or less is critical (red).  The breathing
   (pulse) flag marks the critical band so the LED can fade instead of
   showing a flat red.  Callers must not duplicate these thresholds. */
void
_3ds_health_color(int hp, int hpmax, u16 *color, boolean *pulse)
{
    int frac;

    *pulse = FALSE;
    if (hp < 0) {
        /* dead (u.uhp = -1 is the game-over marker): critical red,
           breathing */
        *color = C_STOP_1;
        *pulse = TRUE;
    } else if (hpmax > 0) {
        if (hp == 0) {
            /* unconscious: critical red, breathing */
            *color = C_STOP_1;
            *pulse = TRUE;
        } else {
            frac = (hp * 100) / hpmax;
            if (frac <= 25) {
                *color = C_STOP_1;           /* critical */
                *pulse = TRUE;
            } else if (frac <= 50) {
                *color = RGB(230, 170, 40);  /* wounded */
            } else {
                *color = C_GO_1;             /* healthy */
            }
        }
    } else {
        /* no hit-point pool established yet: healthy */
        *color = C_GO_1;
    }
}

void
_3ds_art_top_status(const char *title, const char *ess,
                    int hp, int hpmax, u16 col)
{
    u16 *fb = topConsole.frameBuffer;
    int x, y;
    int ty = ART_H - 8;
    int tw, fw;
    u16 barfill;
    boolean hb_pulse;

    if (!fb)
        return;
    for (y = ty; y < ART_H; y++)
        for (x = 0; x < 400; x++)
            fb[x * ART_H + (ART_H - 1 - y)] = C_CLEAR;

    tw = art_text_w(title);
    if (tw > 400)
        tw = 400;

    /* health bar: fill fraction of the title width, coloured by health.
       The colour bands come from _3ds_health_color() so the notification
       LED always agrees with the bar. */
    barfill = C_GO_1;
    _3ds_health_color(hp, hpmax, &barfill, &hb_pulse);
    if (hpmax > 0 && hp >= 0)
        fw = tw * ((hp * 100) / hpmax) / 100;
    else
        fw = tw;
    if (tw > 0)
        top_fill(fb, 0, ty, tw, 8, C_SPEC_2); /* dark gutter */
    if (fw > 0)
        top_fill(fb, 0, ty, fw, 8, barfill);

    top_text_outline(fb, 0, ty, title, C_TEXT, C_CLEAR);
    art_text_fb(fb, 400, tw, ty, ess, &topConsole, col);
}

/*
 * The complete status block on the touch screen.  The core still lays its
 * 40-column status lines out underneath (wintty.c draws them there), but at
 * 40 columns it has to drop most of the ability scores and the turn counter.
 * This rewrites the same strip with our own compact three-line layout, so
 * every field we care about is actually visible.  It writes pixels, not
 * console text, and must run after the core's status render.
 */
void
_3ds_art_bottom_status(const char *line1, const char *line2, const char *line3)
{
    u16 *fb = bottomConsole.frameBuffer;
    int x, y;

    if (!fb)
        return;
    for (y = 0; y < ART_STATUS_H; y++)
        for (x = 0; x < ART_W; x++)
            fb[x * ART_H + (ART_H - 1 - y)] = C_CLEAR;

    art_text_fb(fb, ART_W, 0, 0, line1, &bottomConsole, C_TEXT);
    if (ART_STATUS_H >= 16)
        art_text_fb(fb, ART_W, 0, 8, line2, &bottomConsole, C_TEXT);
    if (ART_STATUS_H >= 24)
        art_text_fb(fb, ART_W, 0, 16, line3, &bottomConsole, C_TEXT);
}

/* wipe the whole touch screen (used by the menus) */
void
_3ds_art_bg(void)
{
    art_open();
    art_paint_bg(0);
    art_close();
    /* the whole screen was repainted: the keyboard widgets are gone */
    _3ds_bottom_ui_invalidate();
}

/* ---------------------------------------------------------------- */
/* touch-screen message log                                          */
/*                                                                   */
/* The latest messages are kept in a small ring buffer and painted on */
/* the strip below the keyboard toggle button, so the player sees the */
/* log even while the map fills the top screen.  When the soft        */
/* keyboard is rolled out its keys are drawn after this, covering the */
/* strip; the log reappears when the keyboard is closed again.        */
/* ---------------------------------------------------------------- */

#define LOG_MAX 12 /* ring capacity; only what fits the screen is drawn */
#define LOG_TEXT_MAX 176
#define LOG_COLS 40
#define LOG_Y (ART_STATUS_H + 4 + BTN_H + 4) /* below the toggle button */
#define LOG_INK RGB(176, 188, 204)

static char log_msgs[LOG_MAX][LOG_TEXT_MAX + 1];
static int log_counts[LOG_MAX];
static int log_write = 0; /* next slot to write */
static int log_count = 0;

void
_3ds_log_add(const char *s)
{
    char buf[LOG_TEXT_MAX + 1];
    int i, n;

    if (!s || !*s)
        return;
    /* keep the pager prompt itself out of the log; it is not a message */
    if (!strncmp(s, "--More--", 8) || !strncmp(s, "\x97More\x97", 6))
        return;

    n = (int) strlen(s);
    if (n > LOG_TEXT_MAX)
        n = LOG_TEXT_MAX;
    for (i = 0; i < n; i++) {
        char c = s[i];
        if (c == '\n' || c == '\r' || c == '\033') {
            /* stop at the first break or escape sequence */
            n = i;
            break;
        }
        buf[i] = c;
    }
    while (n > 0 && (buf[n - 1] == ' ' || buf[n - 1] == '\t'))
        n--;
    if (n == 0)
        return;
    buf[n] = '\0';
    {
        int prev = (log_write - 1 + LOG_MAX) % LOG_MAX;

        if (log_count > 0 && !strcmp(log_msgs[prev], buf)) {
            /* identical to the newest message: bump its repeat count
               instead of adding yet another entry */
            log_counts[prev]++;
            _3ds_bottom_ui_invalidate();
            return;
        }
    }
    memcpy(log_msgs[log_write], buf, (size_t) n);
    log_msgs[log_write][n] = '\0';
    log_counts[log_write] = 1;
    log_write = (log_write + 1) % LOG_MAX;
    if (log_count < LOG_MAX)
        log_count++;
    /* the strip has new content; the widgets are repainted on the next
       input poll, which also redraws the log */
    _3ds_bottom_ui_invalidate();
}

/* Wrap msg at whitespace so every line fits LOG_COLS characters.  Lines
   are written to out (up to outcap), returns how many were filled. */
static int
log_wrap(const char *msg, char (*out)[LOG_COLS + 1], int outcap)
{
    int nout = 0;
    const char *p = msg ? msg : "";

    while (*p && nout < outcap) {
        const char *start, *brk, *last_space;
        int col = 0;

        /* skip leading whitespace between wrapped lines */
        while (*p == ' ')
            p++;
        if (!*p)
            break;
        start = p;
        last_space = NULL;
        /* scan up to LOG_COLS characters, remembering the last space so a
           line breaks at a whole-word boundary when possible */
        while (*p && col < LOG_COLS) {
            if (*p == ' ')
                last_space = p;
            col++;
            p++;
        }
        if (!*p) {
            brk = p; /* the rest fits on this line */
        } else if (last_space && last_space > start) {
            brk = last_space; /* break after the last whole word */
        } else {
            brk = start + LOG_COLS; /* no space: hard-break mid-word */
        }
        {
            int len = (int) (brk - start);

            memcpy(out[nout], start, (size_t) len);
            out[nout][len] = '\0';
            nout++;
        }
        p = brk;
    }
    return nout;
}

/* Draw the log strip: newest message on top, older ones below, wrapping to
   the bottom edge of the screen.  Called with an open art session. */
static void
art_log_draw(void)
{
    int maxrows = (ART_H - LOG_Y) / 8;
    int row = 0;
    int k;

    for (k = 0; k < log_count && row < maxrows; k++) {
        int idx = (log_write - 1 - k + LOG_MAX) % LOG_MAX;
        char disp[LOG_TEXT_MAX + 16];
        char lines[(LOG_TEXT_MAX / LOG_COLS) + 2][LOG_COLS + 1];
        int nlines, li;

        if (log_counts[idx] > 1)
            Snprintf(disp, sizeof disp, "%s (x%d)",
                     log_msgs[idx], log_counts[idx]);
        else
            Strcpy(disp, log_msgs[idx]);
        nlines = log_wrap(disp, lines,
                          (int) (sizeof lines / sizeof lines[0]));
        for (li = 0; li < nlines && row < maxrows; li++, row++)
            art_text_fb(art_fb, ART_W, 2, LOG_Y + row * 8, lines[li],
                        &bottomConsole, LOG_INK);
    }
}

void
_3ds_art_log(void)
{
    art_open();
    art_log_draw();
    art_close();
}

void
_3ds_art_present(void)
{
    gfxFlushBuffers();
    gfxSwapBuffers();
}

/*
 * Redraw the soft keyboard and the toggle button.  The bare background is
 * snapshotted once, so the keys can later be wiped and repainted one at a
 * time (a cap is translucent; drawing it over itself would darken it) and the
 * whole keyboard can be rolled away without repainting the artwork.
 */
void
_3ds_art_keyboard(int visible, int shift, int ctrl, int alt, int caps, int hot)
{
    int i;

    kb_build();
    art_open();
    if (art_cache_status != ART_STATUS_H) {
        art_paint_bg(ART_STATUS_H);
        art_rows_copy(art_bg_cache, art_fb, ART_STATUS_H);
        art_cache_status = ART_STATUS_H;
    } else {
        /* wipe the caps by putting the bare background back */
        art_rows_copy(art_fb, art_bg_cache, ART_STATUS_H);
    }
    /* the message log lives below the toggle button; it is drawn first so
       the keyboard (when rolled out) sits on top of it */
    art_log_draw();
    if (visible) {
        if (hot) {
            /* the whole grid, plus the two slots in the bottom row; the
               toggle keeps its place so the keyboard can be called back */
            hot_draw();
            for (i = 0; i < kb_count[KB_ROWS - 1]; i++)
                if (kb_keys[KB_ROWS - 1][i].kind == K_HOTBAR)
                    kbd_one(KB_ROWS - 1, i, shift, ctrl, alt, caps, 1);
        } else {
            kbd_keys(shift, ctrl, alt, caps, 0);
        }
    }
    kbd_topbar(visible);
    art_close();
}

/*
 * A key was tapped.  Every cap is repainted, because shift and caps lock
 * change the letters printed on all of them and not just on the key that was
 * pressed.  The artwork underneath is left alone, so this does not blit the
 * whole screen the way a full redraw would, and each cap puts its own patch
 * of background back first rather than blending over itself.
 */
void
_3ds_art_kbd_tap(int shift, int ctrl, int alt, int caps)
{
    kb_build();
    art_open();
    kbd_keys(shift, ctrl, alt, caps, 0);
    art_close();
}

/*
 * Hit test the movement pad.  On a hit the character that moves the hero
 * diagonally (a number-pad digit) is stored through the out pointer and 1
 * is returned.
 */
int
_3ds_art_hot_hit(int px, int py, int *ch)
{
    int i;

    kb_build();
    for (i = 0; i < HOT_GRID; i++) {
        int x, y, w, h;

        hot_cell(i, &x, &y, &w, &h);
        if (px >= x && px < x + w && py >= y && py < y + h) {
            *ch = (unsigned char) hot_cmds[i].key;
            return 1;
        }
    }
    return 0;
}

int
_3ds_art_kbd_button_hit(int px, int py)
{
    return px >= btn_x() && px < btn_x() + BTN_W && py >= btn_y()
           && py < btn_y() + BTN_H;
}

int
_3ds_art_topbar_hit(int px, int py, int *act)
{
    int y = btn_y();
    int i;

    if (py < y || py >= y + BTN_H)
        return 0;

    /* keyboard toggle button */
    if (px >= btn_x() && px < btn_x() + BTN_W) {
        if (act)
            *act = 1005;
        return 1;
    }

    for (i = 0; i < 10; i++) {
        int bx = tb_buttons[i].x;
        int bw = tb_buttons[i].w;
        if (px >= bx && px < bx + bw) {
            if (act)
                *act = tb_buttons[i].id;
            return 1;
        }
    }
    return 0;
}

/*
 * Hit test the keyboard.  On success the key's row, index, action code and
 * character are stored through the out pointers and 1 is returned.
 */
int
_3ds_art_kbd_hit(int px, int py, int *row, int *idx, int *kind, int *ch)
{
    int r, i;

    kb_build();
    for (r = 0; r < KB_ROWS; r++)
        for (i = 0; i < kb_count[r]; i++) {
            const struct kkey *k = &kb_keys[r][i];
            int ky = k->y, kh = KB_KEY_H;

            /*
             * The return key's stem is drawn across the gap between rows, so
             * its hit box is stretched up over the gap to match; the arm keeps
             * its own row so it cannot steal the tail of the key beside it.
             */
            if (k->kind == K_ENTER && k->y != kb_ent_y) {
                ky = kb_ent_y + KB_KEY_H;
                kh = kb_ent_h - KB_KEY_H;
            }
            if (py >= ky && py < ky + kh && px >= k->x && px < k->x + k->w) {
                *row = r;
                *idx = i;
                *kind = k->kind;
                *ch = (unsigned char) k->ch;
                return 1;
            }
        }
    return 0;
}

/* ---------------------------------------------------------------- */
/* main menu tiles                                                  */
/* ---------------------------------------------------------------- */

#define MENU_W 248
#define MENU_H 44
#define MENU_GAP 12
#define MENU_ICON 32 /* icons are drawn as plain square artwork   */
#define MENU_TEXT 2  /* labels are blown up so the border reads   */
#define MENU_GAPX 20 /* space between an icon and its caption     */

static int
menu_y(int i)
{
    int total = 3 * MENU_H + 2 * MENU_GAP;

    return (ART_H - total) / 2 + i * (MENU_H + MENU_GAP);
}

static int
menu_x(void)
{
    return (ART_W - MENU_W) / 2;
}

static const char *
menu_text(int i)
{
    static const char *default_menu_label[3] = { "PLAY", "OPTIONS", "EXIT" };
    if (i >= 0 && i < 3)
        return nh_tr(default_menu_label[i]);
    return "";
}

/*
 * Left edge shared by every entry: the widest caption decides it, so the
 * icons line up in a column and the whole block sits in the middle of the
 * screen instead of being pushed against one side.
 */
static int
menu_x0(void)
{
    int i, best = 0, w;

    for (i = 0; i < 3; i++) {
        w = (int) strlen(menu_text(i)) * GLYPH_W * MENU_TEXT;
        if (w > best)
            best = w;
    }
    return (ART_W - (MENU_ICON + MENU_GAPX + best)) / 2;
}

/*
 * A menu entry is nothing but its icon and its caption painted straight on to
 * the picture: no plate, no frame, no outline box.  The caption carries a dark
 * border of its own so it stays readable over whatever is behind it.
 */
static void
menu_tile(int i, int icon)
{
    int x = menu_x0(), y = menu_y(i);
    int cy = y + MENU_H / 2;
    int icx = x + MENU_ICON / 2;
    int lx = x + MENU_ICON + MENU_GAPX;
    const u32 *art = (icon == 0) ? _3ds_art_ico_play
                    : (icon == 1) ? _3ds_art_ico_gear
                                  : _3ds_art_ico_power;

    if (art) {
        art_icon(art, icx, cy, MENU_ICON);
    } else {
        switch (icon) {
        case 0:
            art_icon_triangle(icx, cy, 14, C_TEXT);
            break;
        case 1:
            art_icon_gear(icx, cy, 12, C_TEXT);
            break;
        default:
            art_icon_power(icx, cy, 12, C_TEXT);
            break;
        }
    }

    art_text_outline(lx, cy - GLYPH_H, menu_text(i), C_TEXT, C_CLEAR,
                     MENU_TEXT);
}

void
_3ds_art_menu(void)
{
    art_open();
    art_paint_bg(0);
    menu_tile(0, 0);
    menu_tile(1, 1);
    menu_tile(2, 2);
    art_close();
    /* the menu took the whole screen over: the game widgets are gone */
    _3ds_bottom_ui_invalidate();
}

int
_3ds_art_menu_hit(int px, int py)
{
    int i;

    for (i = 0; i < 3; i++) {
        int y = menu_y(i);

        if (px >= menu_x() && px < menu_x() + MENU_W && py >= y
            && py < y + MENU_H)
            return i;
    }
    return -1;
}

/* ---------------------------------------------------------------- */
/* background only, for the screens whose controls all live above   */
/* ---------------------------------------------------------------- */

/* 3dsart.c */

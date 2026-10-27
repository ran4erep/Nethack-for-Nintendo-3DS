/* NetHack 5.0	3dsui.c */
/* Copyright (c) Stichting Mathematisch Centrum, Amsterdam, 1985. */
/* NetHack may be freely redistributed.  See license for details. */
/*
 * Nintendo 3DS user interface.
 *
 * This module implements the "front end" chrome for the 3DS port:
 *    - an animated main menu (top screen logo, bottom screen buttons),
 *    - a settings screen (collapsible 3DS UI and NetHack option groups),
 *    - an in-game pause menu (START button),
 *    - a custom, windowed character-creation screen.
 *
 * Everything here draws straight into the libctru text consoles so it
 * is usable both before NetHack's window system exists (main menu) and
 * while the game is running (pause menu / character selection).
 */

#include "hack.h"
#include "3dsi18n.h"
#include "wintty.h"
#include <3ds.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

extern PrintConsole topConsole;
extern PrintConsole bottomConsole;

/* menu loop control, defined in 3dsmain.c */
extern int _3ds_menu_loop;
extern void _3ds_return_to_menu(void);

/* the tty window port supplies this; used to repaint after pauses */
extern void _3ds_reconfigure_display(void);

/* the tile renderer, defined in 3dstiles.c */
extern void _3ds_set_tileset(int);

/* the action pad, defined in 3dstty.c */
extern void _3ds_init_bottom_ui(void);

/* save machinery (src/save.c) */
extern int dosave0(void);

/* status fields, filled in by the core (src/botl.c) and the tty port */
extern char *status_vals[MAXBLSTATS];
extern boolean status_activefields[MAXBLSTATS];

#define TOP_COLS 50
#define TOP_ROWS 30
#define BOT_COLS 40
#define BOT_ROWS 30

#define CFG_PATH "sdmc:/3ds/nethack/config/3dsui.cfg"
#define NAME_MAXLEN 32
#define DEF_TILE 16
#define MIN_TILE 8
#define MAX_TILE 64

#define COL_RESET "\x1b[0m"
#define COL_DIM   "\x1b[0;37m"
#define COL_HI    "\x1b[1;37m"
#define COL_SEL   "\x1b[1;33m"
#define COL_TITLE "\x1b[1;36m"
#define COL_OK    "\x1b[1;32m"
#define COL_WARN  "\x1b[1;31m"

/* The windowed screens (settings, pause, character creation, the opening
   story) are drawn in plain white: one colour, no decoration, so that the
   only thing standing out is the cursor. */
#define COL_UI    "\x1b[1;37m"
#define COL_CURS  "\x1b[7;37m"

/* ---------------------------------------------------------------- */
/* persistent settings                                              */
/* ---------------------------------------------------------------- */

static struct {
    boolean loaded;
    boolean tiled_map;
    int tile_w, tile_h; /* graphical tile size */
    int char_w, char_h; /* character-cell size in ASCII mode */
    int tileset;        /* 0 = built-in, 1 = PixelHack */
    int camera;         /* 0 = scroll near the edges, 1 = centred */
    int more_confirm;   /* 1 = stop at --More--, 0 = keep going */
    int tutorial_mode;  /* 0 = ask each game, 1 = always, 2 = never */
    int showexp;
    int weaponstatus, armorstatus, terrainstatus;
    int pickup, autoopen, safe_pet, confirm, tips;
    int cmdassist, mention_walls;
    int hilite_pet, hilite_pile;
    int use_inverse;
    int sortloot;       /* 0 = none, 1 = loot, 2 = full */
    int showdamage;
    int led;            /* 1 = health LED on, 0 = off */
    int master_volume;  /* 0..100, scales everything */
    int music_volume;   /* 0..100, relative to master */
    int sfx_volume;     /* 0..100, relative to master */
    int language;       /* 0 = English, 1 = Russian */
    char name[NAME_MAXLEN];
} U;

static void
ui_strtrim(char *s)
{
    size_t n = strlen(s);

    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r' || s[n - 1] == ' '))
        s[--n] = '\0';
}

void
_3ds_settings_load(void)
{
    FILE *f;
    char line[160];

    if (U.loaded)
        return;
    U.loaded = TRUE;
    U.tiled_map = TRUE;
    U.tile_w = DEF_TILE;
    U.tile_h = DEF_TILE;
    U.char_w = 8;
    U.char_h = 8;
    U.tileset = 0;
    U.camera = 0;
    U.more_confirm = 1;
    U.tutorial_mode = 0;
    U.showexp = 0;
    U.weaponstatus = U.armorstatus = U.terrainstatus = 0;
    U.pickup = 0;
    U.autoopen = 1;
    U.safe_pet = 1;
    U.confirm = 1;
    U.tips = 1;
    U.cmdassist = 1;
    U.mention_walls = 0;
    U.hilite_pet = U.hilite_pile = 0;
    U.use_inverse = 0;
    U.sortloot = 0;
    U.showdamage = 0;
    U.led = 1;
    U.master_volume = 100;
    U.music_volume = 80;
    U.sfx_volume = 100;
    U.language = 0;
    U.name[0] = '\0';

    f = fopen(CFG_PATH, "rb");
    if (!f) {
        /* no file yet: the defaults are already in U, but the audio engine
           still needs to hear about them */
        if (U.language != 1)
        U.language = 0;
    _3ds_i18n_set_language(U.language);
    _3ds_snd_set_volumes(U.master_volume, U.music_volume, U.sfx_volume);
        return;
    }
    while (fgets(line, sizeof line, f)) {
        char *eq = strchr(line, '=');
        char *val;

        if (!eq)
            continue;
        *eq = '\0';
        val = eq + 1;
        ui_strtrim(line);
        ui_strtrim(val);
        if (!strcmp(line, "tiled_map"))
            U.tiled_map = (atoi(val) != 0) ? TRUE : FALSE;
        else if (!strcmp(line, "tile_width"))
            U.tile_w = atoi(val);
        else if (!strcmp(line, "tile_height"))
            U.tile_h = atoi(val);
        else if (!strcmp(line, "char_width"))
            U.char_w = atoi(val);
        else if (!strcmp(line, "char_height"))
            U.char_h = atoi(val);
        else if (!strcmp(line, "tileset"))
            U.tileset = atoi(val);
        else if (!strcmp(line, "camera"))
            U.camera = atoi(val);
        else if (!strcmp(line, "more_confirm"))
            U.more_confirm = atoi(val);
        else if (!strcmp(line, "tutorial_mode"))
            U.tutorial_mode = atoi(val);
        else if (!strcmp(line, "showexp"))
            U.showexp = atoi(val);
        else if (!strcmp(line, "weaponstatus"))
            U.weaponstatus = atoi(val);
        else if (!strcmp(line, "armorstatus"))
            U.armorstatus = atoi(val);
        else if (!strcmp(line, "terrainstatus"))
            U.terrainstatus = atoi(val);
        else if (!strcmp(line, "autopickup"))
            U.pickup = atoi(val);
        else if (!strcmp(line, "autoopen"))
            U.autoopen = atoi(val);
        else if (!strcmp(line, "safe_pet"))
            U.safe_pet = atoi(val);
        else if (!strcmp(line, "confirm"))
            U.confirm = atoi(val);
        else if (!strcmp(line, "tips"))
            U.tips = atoi(val);
        else if (!strcmp(line, "cmdassist"))
            U.cmdassist = atoi(val);
        else if (!strcmp(line, "mention_walls"))
            U.mention_walls = atoi(val);
        else if (!strcmp(line, "hilite_pet"))
            U.hilite_pet = atoi(val);
        else if (!strcmp(line, "hilite_pile"))
            U.hilite_pile = atoi(val);
        else if (!strcmp(line, "use_inverse"))
            U.use_inverse = atoi(val);
        else if (!strcmp(line, "sortloot"))
            U.sortloot = atoi(val);
        else if (!strcmp(line, "showdamage"))
            U.showdamage = atoi(val);
        else if (!strcmp(line, "led"))
            U.led = atoi(val);
        else if (!strcmp(line, "master_volume"))
            U.master_volume = atoi(val);
        else if (!strcmp(line, "music_volume"))
            U.music_volume = atoi(val);
        else if (!strcmp(line, "sfx_volume"))
            U.sfx_volume = atoi(val);
        else if (!strcmp(line, "language")) {
            U.language = atoi(val);
            _3ds_i18n_set_language(U.language);
        }
        else if (!strcmp(line, "name")) {
            copynchars(U.name, val, NAME_MAXLEN - 1);
            U.name[NAME_MAXLEN - 1] = '\0';
        }
    }
    fclose(f);

    if (U.tile_w < MIN_TILE || U.tile_w > MAX_TILE)
        U.tile_w = DEF_TILE;
    if (U.tile_h < MIN_TILE || U.tile_h > MAX_TILE)
        U.tile_h = DEF_TILE;
    if (U.char_w < MIN_TILE || U.char_w > MAX_TILE)
        U.char_w = 8;
    if (U.char_h < MIN_TILE || U.char_h > MAX_TILE)
        U.char_h = 8;
    if (U.tileset != 1)
        U.tileset = 0;
    if (U.camera != 1)
        U.camera = 0;
    if (U.more_confirm != 0)
        U.more_confirm = 1;
    if (U.tutorial_mode < 0 || U.tutorial_mode > 2)
        U.tutorial_mode = 0;
    if (U.sortloot < 0 || U.sortloot > 2)
        U.sortloot = 0;
    U.showexp = U.showexp ? 1 : 0;
    U.weaponstatus = U.weaponstatus ? 1 : 0;
    U.armorstatus = U.armorstatus ? 1 : 0;
    U.terrainstatus = U.terrainstatus ? 1 : 0;
    U.pickup = U.pickup ? 1 : 0;
    U.autoopen = U.autoopen ? 1 : 0;
    U.safe_pet = U.safe_pet ? 1 : 0;
    U.confirm = U.confirm ? 1 : 0;
    U.tips = U.tips ? 1 : 0;
    U.cmdassist = U.cmdassist ? 1 : 0;
    U.mention_walls = U.mention_walls ? 1 : 0;
    U.hilite_pet = U.hilite_pet ? 1 : 0;
    U.hilite_pile = U.hilite_pile ? 1 : 0;
    U.use_inverse = U.use_inverse ? 1 : 0;
    U.showdamage = U.showdamage ? 1 : 0;
    U.led = U.led ? 1 : 0;
    if (U.master_volume < 0 || U.master_volume > 100)
        U.master_volume = 100;
    if (U.music_volume < 0 || U.music_volume > 100)
        U.music_volume = 80;
    if (U.sfx_volume < 0 || U.sfx_volume > 100)
        U.sfx_volume = 100;
    if (U.language != 1)
        U.language = 0;
    _3ds_i18n_set_language(U.language);
    _3ds_snd_set_volumes(U.master_volume, U.music_volume, U.sfx_volume);
}

void
_3ds_settings_save(void)
{
    FILE *f;

    _3ds_settings_load();
    /* U may already be loaded, in which case load() returns early; apply
       the current values here so a volume change is audible immediately. */
    if (U.language != 1)
        U.language = 0;
    _3ds_i18n_set_language(U.language);
    _3ds_snd_set_volumes(U.master_volume, U.music_volume, U.sfx_volume);
    f = fopen(CFG_PATH, "wb");
    if (!f)
        return;
    fprintf(f, "tiled_map=%d\n", U.tiled_map ? 1 : 0);
    fprintf(f, "tile_width=%d\n", U.tile_w);
    fprintf(f, "tile_height=%d\n", U.tile_h);
    fprintf(f, "char_width=%d\n", U.char_w);
    fprintf(f, "char_height=%d\n", U.char_h);
    fprintf(f, "tileset=%d\n", U.tileset);
    fprintf(f, "camera=%d\n", U.camera);
    fprintf(f, "more_confirm=%d\n", U.more_confirm);
    fprintf(f, "tutorial_mode=%d\n", U.tutorial_mode);
    fprintf(f, "showexp=%d\n", U.showexp);
    fprintf(f, "weaponstatus=%d\n", U.weaponstatus);
    fprintf(f, "armorstatus=%d\n", U.armorstatus);
    fprintf(f, "terrainstatus=%d\n", U.terrainstatus);
    fprintf(f, "autopickup=%d\n", U.pickup);
    fprintf(f, "autoopen=%d\n", U.autoopen);
    fprintf(f, "safe_pet=%d\n", U.safe_pet);
    fprintf(f, "confirm=%d\n", U.confirm);
    fprintf(f, "tips=%d\n", U.tips);
    fprintf(f, "cmdassist=%d\n", U.cmdassist);
    fprintf(f, "mention_walls=%d\n", U.mention_walls);
    fprintf(f, "hilite_pet=%d\n", U.hilite_pet);
    fprintf(f, "hilite_pile=%d\n", U.hilite_pile);
    fprintf(f, "use_inverse=%d\n", U.use_inverse);
    fprintf(f, "sortloot=%d\n", U.sortloot);
    fprintf(f, "showdamage=%d\n", U.showdamage);
    fprintf(f, "led=%d\n", U.led);
    fprintf(f, "master_volume=%d\n", U.master_volume);
    fprintf(f, "music_volume=%d\n", U.music_volume);
    fprintf(f, "sfx_volume=%d\n", U.sfx_volume);
    fprintf(f, "language=%d\n", U.language);
    fprintf(f, "name=%s\n", U.name);
    fclose(f);
}

/* 3dsled.c asks whether the health-LED feature is switched on. */
int
_3ds_led_enabled(void)
{
    return U.led ? 1 : 0;
}

/* Push the stored NetHack game options into the core's flag state.  These
   are the options a player would otherwise have to edit into nethackrc;
   the options screen exposes the useful subset. */
static void
apply_nh_options(void)
{
    flags.showexp = U.showexp ? TRUE : FALSE;
    flags.weaponstatus = U.weaponstatus ? TRUE : FALSE;
    flags.armorstatus = U.armorstatus ? TRUE : FALSE;
    flags.terrainstatus = U.terrainstatus ? TRUE : FALSE;
    flags.pickup = U.pickup ? TRUE : FALSE;
    flags.autoopen = U.autoopen ? TRUE : FALSE;
    flags.safe_dog = U.safe_pet ? TRUE : FALSE;
    flags.confirm = U.confirm ? TRUE : FALSE;
    flags.tips = U.tips ? TRUE : FALSE;
    flags.mention_walls = U.mention_walls ? TRUE : FALSE;
    flags.sortloot = (U.sortloot == 2) ? 'f' : (U.sortloot == 1) ? 'l' : 'n';
    iflags.cmdassist = U.cmdassist ? TRUE : FALSE;
    iflags.wc_hilite_pet = U.hilite_pet ? TRUE : FALSE;
    if (iflags.wc_hilite_pet && !iflags.wc2_petattr)
        iflags.wc2_petattr = ATR_INVERSE;
    iflags.hilite_pile = U.hilite_pile ? TRUE : FALSE;
    iflags.use_inverse = U.use_inverse ? TRUE : FALSE;
    iflags.showdamage = U.showdamage ? TRUE : FALSE;

    /* The status options are normally applied by parseoptions(), which also
       reassesses the active status fields.  We set the flags directly, so do
       the same here; otherwise toggling them mid-game would not show up until
       the next level change.  It is safe to skip before the status window has
       been created (main menu, before the first game). */
    if (gb.blinit) {
        if (flags.terrainstatus)
            classify_terrain();
        status_initialize(REASSESS_ONLY);
        disp.botl = TRUE;
    }
}

/* Push the stored 3DS choices into NetHack's option state.  Called from
   pcmain() after initoptions() so the menu always wins over the config
   file (the config file stays the place for every *other* option). */
void
_3ds_apply_settings(void)
{
    _3ds_settings_load();
    iflags.wc_tiled_map = U.tiled_map ? TRUE : FALSE;
    iflags.wc_ascii_map = U.tiled_map ? FALSE : TRUE;
    if (U.tiled_map) {
        iflags.wc_tile_width = U.tile_w;
        iflags.wc_tile_height = U.tile_h;
    } else {
        iflags.wc_tile_width = U.char_w;
        iflags.wc_tile_height = U.char_h;
    }
    iflags.use_background_glyph = iflags.wc_tiled_map;
    _3ds_set_tileset(U.tileset);
    apply_nh_options();

    /* The 3DS has no number pad, so the D-pad and the soft keyboard's
       digit row stand in for one: 1/3 are the down diagonals, 7/9 the up
       diagonals, and so on.  Force this here because an existing SD-card
       config file is not overwritten on upgrade. */
    iflags.num_pad = TRUE;
    iflags.num_pad_mode = 0;
    reset_commands(FALSE);

    /* The turn counter is one of the fields the compact touch-screen status
       layout always shows, so make sure the core actually fills it in even
       when an older config file turned the 'time' option off. */
    flags.time = TRUE;

    /* Three status lines (24 px): two for numbers and one for the hero's
       current status, with the rest of the touch screen left for the
       soft keyboard. */
    iflags.wc2_statuslines = 3;
}

/* Camera policy for the clipped map view (wintty.c):
 *   0 - the traditional tty behaviour, scrolling only when the hero comes
 *       close to an edge of the visible area;
 *   1 - recentre on the hero every time the view has to move.
 * The view dimensions themselves always come from the current tile size. */
int
_3ds_camera_centered(void)
{
    _3ds_settings_load();
    return U.camera == 1;
}

/* TRUE when --More-- prompts should wait for a key; FALSE keeps the message
   log moving without asking for confirmation. */
boolean
_3ds_more_confirm(void)
{
    _3ds_settings_load();
    return U.more_confirm ? TRUE : FALSE;
}

/* 0 = ask before each game, 1 = always enter the tutorial, 2 = never. */
int
_3ds_tutorial_mode(void)
{
    _3ds_settings_load();
    return U.tutorial_mode;
}

/* One status field, with the leading separator space removed.  The strings
   live in status_vals[] and already carry their own labels (" Dx:12",
   " AC:0", ...), so the compact layout does not have to rebuild them. */
static const char *
status_text(int idx)
{
    const char *s;

    if (idx < 0 || idx >= MAXBLSTATS || !status_activefields[idx])
        return "";
    s = status_vals[idx];
    if (!s)
        return "";
    while (*s == ' ')
        s++;
    return s;
}

/* Append a token, separating it from the previous one with one space. */
static void
status_add(char *buf, size_t size, const char *text)
{
    size_t len = strlen(buf);

    if (!text || !*text || len + 1 >= size)
        return;
    while (*text == ' ')
        text++;
    if (!*text)
        return;
    if (len > 0 && buf[len - 1] != ' ' && len + 1 < size)
        buf[len++] = ' ';
    while (*text && len + 1 < size)
        buf[len++] = *text++;
    buf[len] = '\0';
}

/* Append without a separating space (experience is "/0" after "Xp:1"). */
static void
status_add_raw(char *buf, size_t size, const char *text)
{
    size_t len = strlen(buf);

    if (!text || !*text)
        return;
    while (*text == ' ')
        text++;
    while (*text && len + 1 < size)
        buf[len++] = *text++;
    buf[len] = '\0';
}

static void
copy_trimmed(char *dst, size_t size, const char *src)
{
    size_t len;

    if (!src)
        src = "";
    while (*src == ' ')
        src++;
    len = strlen(src);
    while (len > 0 && src[len - 1] == ' ')
        len--;
    if (len >= size)
        len = size - 1;
    memcpy(dst, src, len);
    dst[len] = '\0';
}

/*
 * Refresh both status displays.
 *
 * Top screen: one reserved 8-pixel line at the very bottom, carrying the
 * hero's name and rank title plus the vital numbers (HP, power, gold).
 *
 * Touch screen: a compact three-line block.  The core's own 40-column layout
 * has to drop fields at this width, so we overwrite that strip with our own
 * layout: all six ability scores on the first line; alignment, AC, experience,
 * turn count, HD and finally the dungeon level on the second; and the hero's
 * current status (hunger, conditions, encumbrance, equipment) on the third.
 * Nothing shown on the top line is repeated here.
 *
 * Both are painted straight into the framebuffers, so this must run after
 * the core has rendered its status (see render_status() in wintty.c).
 */
void
_3ds_status_panel(void)
{
    char title[64], ess[64];
    char line1[41], line2[41], line3[41];
    char conditions[64];
    char name[PL_NSIZ + 1];
    const char *xp, *exp;
    int hp, hpmax, pw, pwmax, avail, tlen;
    long gold;
    u16 color = 0xFFFF; /* white */

    if (!svp.plname[0])
        return; /* not in a game yet */

    copynchars(name, svp.plname, sizeof name - 1);
    name[sizeof name - 1] = '\0';
    name[0] = highc(name[0]);

    /* "<name> the <rank>" comes straight from the core's title field.  If it
       has not been filled in yet, fall back to the bare name. */
    copy_trimmed(title, sizeof title, status_text(BL_TITLE));
    if (!title[0])
        copy_trimmed(title, sizeof title, name);
#ifdef NINTENDO_3DS
    /* translate the "<name> the <rank>" title token-by-token */
    if (_3ds_i18n_get_language() == NH_LANG_RU && title[0]) {
        char tbuf[64], out[64];
        char *tok;
        boolean first = TRUE;

        Strcpy(tbuf, title);
        out[0] = '\0';
        for (tok = strtok(tbuf, " "); tok;
             tok = strtok((char *) 0, " ")) {
            if (!strcmpi(tok, "the"))
                continue;
            if (!first)
                Strcat(out, " ");
            first = FALSE;
            Strcat(out, nh_tr(tok));
        }
        if (!first)
            Strcpy(title, out);
    }
#endif

    hp = Upolyd ? u.mh : u.uhp;
    hpmax = Upolyd ? u.mhmax : u.uhpmax;
    if (hp < 0)
        hp = 0;
    if (hpmax < 0)
        hpmax = 0;
    pw = u.uen;
    pwmax = u.uenmax;
    gold = money_cnt(gi.invent);
    if (gold < 0L)
        gold = 0L;

    /* turn the whole top line red when the hero is in real trouble */
    if (hp == 0 || (hpmax > 0 && hp * 3 <= hpmax))
        color = 0xF800;

    /* Keep the top line inside the 50 characters that fit on the screen. */
    Snprintf(ess, sizeof ess, " HP%d/%d Pw%d/%d %s %d $%ld",
             hp, hpmax, pw, pwmax, nh_tr("Level:"), (int) u.ulevel, gold);
    avail = 50 - (int) strlen(ess);
    if (avail < 0)
        avail = 0;
    tlen = (int) strlen(title);
    if (tlen > avail)
        title[avail] = '\0';
    _3ds_art_top_status(title, ess, hp, hpmax, color);

    /* Compact full status for the touch screen. */
    line1[0] = line2[0] = line3[0] = '\0';
    status_add(line1, sizeof line1, status_text(BL_STR));
    status_add(line1, sizeof line1, status_text(BL_DX));
    status_add(line1, sizeof line1, status_text(BL_CO));
    status_add(line1, sizeof line1, status_text(BL_IN));
    status_add(line1, sizeof line1, status_text(BL_WI));
    status_add(line1, sizeof line1, status_text(BL_CH));

    status_add(line2, sizeof line2, status_text(BL_ALIGN));
    status_add(line2, sizeof line2, status_text(BL_AC));
    xp = status_text(BL_XP);
    exp = status_text(BL_EXP);
    if (*xp) {
        status_add(line2, sizeof line2, xp);
        status_add_raw(line2, sizeof line2, exp);
    }
    /* the turn counter is more important than the hit-dice field */
    status_add(line2, sizeof line2, status_text(BL_TIME));
    status_add(line2, sizeof line2, status_text(BL_HD));
    status_add(line2, sizeof line2, status_text(BL_LEVELDESC));

    /* third line: the hero's current condition, not more numbers */
    {
        char hbuf[32];
        copy_trimmed(hbuf, sizeof hbuf, status_text(BL_HUNGER));
        if (hbuf[0])
            status_add(line3, sizeof line3, nh_tr(hbuf));
    }
    _3ds_status_conditions(conditions, sizeof conditions);
    status_add(line3, sizeof line3, conditions);
    {
        char cbuf[32];
        copy_trimmed(cbuf, sizeof cbuf, status_text(BL_CAP));
        if (cbuf[0])
            status_add(line3, sizeof line3, nh_tr(cbuf));
    }
    status_add(line3, sizeof line3, status_text(BL_WEAPON));
    status_add(line3, sizeof line3, status_text(BL_ARMOR));
    status_add(line3, sizeof line3, status_text(BL_TERRAIN));
    status_add(line3, sizeof line3, status_text(BL_SCORE));

    _3ds_art_bottom_status(line1, line2, line3);
}

/* Show a read-only multi-line text popup (menu rows are unselectable, so
   the list is shown as a page that closes with A/B).  Used for textual
   discoveries (engravings, headstones, ...) and item information. */
void
_3ds_popup_text(const char *title, const char *text)
{
    winid win;
    anything any;
    char buf[BUFSZ];
    const char *p = text ? text : "";
    menu_item *dummy = (menu_item *) 0;

    /* A textual discovery (an engraving, a headstone) is reported while the
       hero is stepping onto the square.  Redraw the map first so the hero is
       shown standing on the tile before the popup opens; otherwise the window
       appears while the hero still looks like it is on the previous square. */
    if (iflags.window_inited && u.ux) {
        extern boolean _3ds_tiles_active(void);

        if (_3ds_tiles_active()) {
            d_level uz0_save = u.uz0;

            assign_level(&u.uz0, &u.uz);
            redraw_map(TRUE);
            u.uz0 = uz0_save;
        } else {
            docrt();
        }
        consoleSelect(&topConsole);
        gfxFlushBuffers();
        gfxSwapBuffers();
    }

    win = create_nhwindow(NHW_MENU);
    start_menu(win, MENU_BEHAVE_STANDARD);
    any = cg.zeroany; /* text rows are not selectable */

    while (*p) {
        int n = 0;

        while (*p && *p != '\n' && n < (int) sizeof buf - 1)
            buf[n++] = *p++;
        if (*p == '\n')
            p++;
        buf[n] = '\0';
        add_menu(win, &nul_glyphinfo, &any, '\0', 0, ATR_NONE,
                 NO_COLOR, buf, MENU_ITEMFLAGS_NONE);
    }

    end_menu(win, title);
    (void) select_menu(win, PICK_NONE, &dummy);
    if (dummy)
        free((genericptr_t) dummy);
    destroy_nhwindow(win);
}

/* ---------------------------------------------------------------- */
/* low level drawing helpers                                        */
/* ---------------------------------------------------------------- */

static void top_goto(int x, int y) { printf("\x1b[%d;%dH", y + 1, x + 1); }
static void bot_goto(int x, int y) { printf("\x1b[%d;%dH", y + 1, x + 1); }

static void center_into(char *dst, int width, const char *s);

/* One whole row, padded out to the right hand edge.  The windowed screens
   are repainted without ever being cleared, so every line has to be written
   in full or the tail of the previous frame would still be showing. */
static void
top_row(int y, int x, const char *col, const char *s)
{
    char buf[TOP_COLS + 1];
    int n = TOP_COLS - x, len;

    if (y < 0 || y >= TOP_ROWS || x < 0 || n <= 0)
        return;
    len = s ? (int) strlen(s) : 0;
    if (len > n)
        len = n;
    if (len > 0)
        memcpy(buf, s, (size_t) len);
    memset(buf + len, ' ', (size_t) (n - len));
    buf[n] = '\0';
    top_goto(x, y);
    if (col)
        printf("%s", col);
    printf("%s%s", buf, COL_RESET);
}

static void
top_puts(int x, int y, const char *col, const char *s)
{
    if (y < 0 || y >= TOP_ROWS || !s)
        return;
    if (x < 0)
        x = 0;
    top_goto(x, y);
    if (col)
        printf("%s", col);
    printf("%s%s", s, COL_RESET);
}

static void
top_center(int y, const char *col, const char *s)
{
    char buf[TOP_COLS + 1];

    center_into(buf, TOP_COLS, s);
    top_row(y, 0, col, buf);
}

static void
bot_puts(int x, int y, const char *col, const char *s)
{
    if (y < 0 || y >= BOT_ROWS || !s)
        return;
    if (x < 0)
        x = 0;
    bot_goto(x, y);
    if (col)
        printf("%s", col);
    printf("%s%s", s, COL_RESET);
}

static void
bot_center(int y, const char *col, const char *s)
{
    int len = (int) strlen(s);
    bot_puts((BOT_COLS - len) / 2, y, col, s);
}

static void
top_box(int x, int y, int w, int h, const char *col)
{
    char blank[TOP_COLS + 1];
    int i;

    top_goto(x, y);
    printf("%s+", col);
    for (i = 0; i < w - 2; i++)
        putchar('-');
    printf("+%s", COL_RESET);
    for (i = 1; i < h - 1; i++) {
        /* the sides and the empty middle, so that whatever a menu drew
           inside the frame before is wiped by the frame itself */
        memset(blank, ' ', (size_t) (w - 2));
        blank[w - 2] = '\0';
        top_goto(x, y + i);
        printf("%s|%s|%s", col, blank, COL_RESET);
    }
    top_goto(x, y + h - 1);
    printf("%s+", col);
    for (i = 0; i < w - 2; i++)
        putchar('-');
    printf("+%s", COL_RESET);
}

/* ------------------------------------------------------------------ */
/* atomic menu compositing                                             */
/*                                                                     */
/* The 3DS renders single-buffered (gfxSetDoubleBuffering off), so a   */
/* multi-step repaint of a menu frame (box, rows, icons, soft keyboard) */
/* is visible on screen as it happens: tearing and flicker while the   */
/* cursor moves, with half-finished frames and leftover pixels b        */
/* artifacts.  Menus therefore compose each frame into scratch          */
/* framebuffers first and blit the finished frame to the real screens   */
/* in one pass, so a frame is never half-drawn.                        */
/* ------------------------------------------------------------------ */

#define MENU_SCR_TOP_PX (400 * 240)
#define MENU_SCR_BOT_PX (320 * 240)
static u16 menu_scratch_top[MENU_SCR_TOP_PX];
static u16 menu_scratch_bot[MENU_SCR_BOT_PX];
static u16 *menu_real_top = NULL, *menu_real_bot = NULL;

static void
menu_frame_begin(void)
{
    menu_real_top = topConsole.frameBuffer;
    menu_real_bot = bottomConsole.frameBuffer;
    if (!menu_real_top || !menu_real_bot)
        return;
    memcpy(menu_scratch_top, menu_real_top, sizeof menu_scratch_top);
    memcpy(menu_scratch_bot, menu_real_bot, sizeof menu_scratch_bot);
    topConsole.frameBuffer = menu_scratch_top;
    bottomConsole.frameBuffer = menu_scratch_bot;
}

static void
menu_frame_end(void)
{
    if (!menu_real_top || !menu_real_bot)
        return;
    memcpy(menu_real_top, menu_scratch_top, sizeof menu_scratch_top);
    memcpy(menu_real_bot, menu_scratch_bot, sizeof menu_scratch_bot);
    topConsole.frameBuffer = menu_real_top;
    bottomConsole.frameBuffer = menu_real_bot;
}

static void
center_into(char *dst, int width, const char *s)
{
    int len = (int) strlen(s), pad, i;

    if (len > width)
        len = width;
    pad = (width - len) / 2;
    for (i = 0; i < pad; i++)
        dst[i] = ' ';
    if (len > 0)
        memcpy(dst + pad, s, (size_t) len);
    for (i = pad + len; i < width; i++)
        dst[i] = ' ';
    dst[width] = '\0';
}

/* a touch friendly button drawn on the bottom screen */
static void
bot_button(int y, const char *label, int selected)
{
    char mid[24];
    const char *col = selected ? "\x1b[1;30;46m" : "\x1b[0;37m";
    int i;

    center_into(mid, 20, label);
    bot_goto(9, y);
    printf("%s+", col);
    for (i = 0; i < 20; i++)
        putchar('-');
    printf("+%s", COL_RESET);
    bot_goto(9, y + 1);
    printf("%s|%s|%s", col, mid, COL_RESET);
    bot_goto(9, y + 2);
    printf("%s+", col);
    for (i = 0; i < 20; i++)
        putchar('-');
    printf("+%s", COL_RESET);
}

/* buttons start at row 4 and are 4 rows apart (3 rows of button + 1
   blank); the touch panel is 320x240 so 32 pixels per console row. */
#define BOT_BTN_TOP_PX 32
#define BOT_BTN_STEP_PX 32

static int
bot_hit(int px, int py, int nbuttons)
{
    int idx;

    (void) px;
    if (py < BOT_BTN_TOP_PX)
        return -1;
    idx = (py - BOT_BTN_TOP_PX) / BOT_BTN_STEP_PX;
    if (idx < 0 || idx >= nbuttons)
        return -1;
    /* reject the gap just below each button */
    if ((py - BOT_BTN_TOP_PX) % BOT_BTN_STEP_PX >= 24)
        return -1;
    return idx;
}

/* ---------------------------------------------------------------- */
/* animated ASCII logo                                               */
/* ---------------------------------------------------------------- */

static const char *
bgglyph(char c, int row)
{
    switch (c) {
    case 'N': { static const char *r[5] = { "#   #", "##  #", "# # #", "#  ##", "#   #" }; return r[row]; }
    case 'E': { static const char *r[5] = { "#####", "#    ", "#### ", "#    ", "#####" }; return r[row]; }
    case 'T': { static const char *r[5] = { "#####", "  #  ", "  #  ", "  #  ", "  #  " }; return r[row]; }
    case 'H': { static const char *r[5] = { "#   #", "#   #", "#####", "#   #", "#   #" }; return r[row]; }
    case 'A': { static const char *r[5] = { " ### ", "#   #", "#####", "#   #", "#   #" }; return r[row]; }
    case 'C': { static const char *r[5] = { " ####", "#    ", "#    ", "#    ", " ####" }; return r[row]; }
    case 'K': { static const char *r[5] = { "#   #", "#  # ", "###  ", "#  # ", "#   #" }; return r[row]; }
    case '3': { static const char *r[5] = { "#### ", "    #", " ### ", "    #", "#### " }; return r[row]; }
    case 'D': { static const char *r[5] = { "#### ", "#   #", "#   #", "#   #", "#### " }; return r[row]; }
    case 'S': { static const char *r[5] = { " ####", "#    ", " ### ", "    #", "#### " }; return r[row]; }
    default:  { static const char *r[5] = { "    ", "    ", "    ", "    ", "    " }; return r[row]; }
    }
}

static const char *rainbow[7] = {
    "\x1b[1;31m", "\x1b[1;33m", "\x1b[1;32m", "\x1b[1;36m",
    "\x1b[1;34m", "\x1b[1;35m", "\x1b[1;37m"
};

/* build a 5 row banner for a word into out (>= 80 chars) */
static void
build_banner(const char *word, int row, char *out)
{
    int x = 0, i;

    for (i = 0; word[i]; i++) {
        const char *g = bgglyph(word[i], row);
        size_t gl = strlen(g);

        memcpy(out + x, g, gl);
        x += (int) gl;
        if (word[i + 1])
            out[x++] = ' ';
    }
    out[x] = '\0';
}

/* The banner is built once as a grid of pixels, then a travelling wave
   shifts every column up and down to make it ripple like a flag. */
static char banner_grid[5][80];
static int banner_w = 0;
static const int wave_tab[8] = { 0, 1, 2, 1, 0, -1, -2, -1 };

static void
banner_build(void)
{
    const char *word = "NETHACK";
    int x = 0, i, r;

    if (banner_w)
        return;
    for (i = 0; word[i]; i++) {
        for (r = 0; r < 5; r++) {
            const char *g = bgglyph(word[i], r);
            memcpy(&banner_grid[r][x], g, 5);
        }
        x += 5;
        if (word[i + 1]) {
            for (r = 0; r < 5; r++)
                banner_grid[r][x] = ' ';
            x++;
        }
    }
    banner_w = x;
}

static void
draw_title(int frame)
{
    char line[80];
    int r, c;

    banner_build();
    for (r = 0; r < 7; r++) {
        int len = 0;

        for (c = 0; c < banner_w; c++) {
            int off = wave_tab[((c / 2) + (frame / 10)) & 7];
            int src = (r - 2) - off;

            line[len++] = (src >= 0 && src < 5) ? banner_grid[src][c] : ' ';
        }
        line[len] = '\0';
        top_puts((TOP_COLS - banner_w) / 2, 2 + r, COL_HI, line);
    }
}

static void
put_at(char *buf, int idx, char c)
{
    if (idx >= 0 && idx < TOP_COLS)
        buf[idx] = c;
}

/* ---------------------------------------------------------------- */
/* loading screen                                                   */
/* ---------------------------------------------------------------- */

/* The logo held still.  It sits exactly where the menu will draw it, but
   without the ripple: the animation belongs to the menu, not to loading. */
static void
draw_title_still(void)
{
    char line[81];
    int r;

    banner_build();
    for (r = 0; r < 5; r++) {
        memcpy(line, banner_grid[r], (size_t) banner_w);
        line[banner_w] = '\0';
        top_puts((TOP_COLS - banner_w) / 2, 2 + r, COL_HI, line);
    }
}

#define LOAD_ROW (TOP_ROWS - 2) /* the bar along the bottom of the screen */
#define LOAD_LEN 40

/* the last figure drawn, so the chunked copy below only repaints the bar when
   it really has moved */
static int load_last = -1;

/* Put the loading screen up: the logo, an empty bar underneath it, and the
   artwork on the bottom screen, so that starting the game is not a long
   black stare at nothing. */
void
_3ds_loading_start(void)
{
    consoleSelect(&topConsole);
    printf("\x1b[2J\x1b[H");
    draw_title_still();
    load_last = -1;
    _3ds_art_bg();
    _3ds_art_present();
}

/* Move the bar on.  Only its own row is rewritten, so this can be called
   from inside a copy loop without the logo flickering. */
void
_3ds_loading(int pct)
{
    char bar[LOAD_LEN + 1];
    int i, fill;

    if (pct < 0)
        pct = 0;
    if (pct > 100)
        pct = 100;
    if (pct == load_last)
        return;
    load_last = pct;

    fill = (LOAD_LEN * pct + 50) / 100;
    for (i = 0; i < LOAD_LEN; i++)
        bar[i] = (i < fill) ? '#' : '.';
    bar[LOAD_LEN] = '\0';

    consoleSelect(&topConsole);
    top_row(LOAD_ROW, (TOP_COLS - LOAD_LEN) / 2, COL_HI, bar);
    gfxFlushBuffers();
}

/* ---------------------------------------------------------------- */
/* save / load progress                                             */
/* ---------------------------------------------------------------- */

/* The start-up splash bar does double duty: it reports the progress of a
   game save and of a game load, with a caption above it.  src/sfstruct.c
   feeds _3ds_io_tick() the number of bytes that really moved to or from the
   SD card, so the bar follows work actually done instead of a timer. */
#define IO_MSG_ROW (LOAD_ROW - 2) /* the "Saving" / "Loading" caption */

static int io_active = 0;
static int io_mode = _3DS_IO_READ;
static unsigned long io_done = 0, io_total = 0;
static int io_bar = -1;
static int io_guessed = 0; /* io_total is a makeweight, not a real size */
static char io_label[24] = ""; /* "Saving" / "Loading" */

/* Size of the savefile we are about to read or write.  0 when there is
   none to measure (the very first save of a game). */
static unsigned long
io_estimate(void)
{
    const char *fq_save;
    struct stat st;

    if (!gs.SAVEF[0])
        return 0UL;
    fq_save = fqname(gs.SAVEF, SAVEPREFIX, 0);
    if (!fq_save || !*fq_save)
        return 0UL;
    if (stat(fq_save, &st) == 0 && st.st_size > 0)
        return (unsigned long) st.st_size;
    /* the file functions fall back to the path without the device prefix */
    if (strncmp(fq_save, "sdmc:", 5) == 0
        && stat(fq_save + 5, &st) == 0 && st.st_size > 0)
        return (unsigned long) st.st_size;
    return 0UL;
}

/* Repaint the whole loading screen: the logo (which whatever else is drawing
   may have cleared over since the last update), the caption and the bar.
   Saves and restores do a good deal of drawing of their own, so the progress
   display has to be able to rebuild itself. */
static void
io_paint(const char *label, int pct)
{
    PrintConsole *prev = consoleSelect(&topConsole);

    draw_title_still();
    top_center(IO_MSG_ROW, COL_HI, label ? label : "");
    (void) consoleSelect(prev);
    _3ds_loading(pct); /* draws the bar and flushes the screen */
}

/* Put the progress screen up and start counting.  mode is one of _3DS_IO_*:
   a save only counts card writes, a load counts both directions because it
   reads the savefile and then writes every level back out as a level file. */
void
_3ds_io_begin(const char *label, int mode)
{
    unsigned long size = io_estimate();

    /* armed only once the screen is up: see _3ds_io_abandon() */
    io_active = 0;
    io_mode = mode;
    io_done = 0;
    io_bar = 0;
    io_total = (mode == _3DS_IO_BOTH) ? (2UL * size) : size;
    io_guessed = (io_total == 0);
    (void) snprintf(io_label, sizeof io_label, "%s", label ? label : "");

    _3ds_loading_start(); /* logo, bottom screen, empty bar */
    io_paint(io_label, 0);
    io_active = 1;
}

void
_3ds_io_tick(unsigned long bytes, int writing)
{
    unsigned long pct;

    if (!io_active || bytes == 0)
        return;
    if (io_mode != _3DS_IO_BOTH
        && (io_mode == _3DS_IO_WRITE) != (writing != 0))
        return;

    io_done += bytes;
    if (io_total == 0)
        io_total = 512UL * 1024UL; /* nothing to measure; keep creeping */
    pct = io_done / (io_total / 100UL + 1UL);
    if (io_guessed && pct >= 95UL) {
        /* the size was only a guess and the work has outgrown it; stretch it
           so the bar keeps moving instead of sticking near the end */
        io_total = io_done + io_total / 8UL + 65536UL;
        pct = io_done / (io_total / 100UL + 1UL);
    }
    if (pct > 99UL)
        pct = 99UL; /* 100% is reserved for the end of the operation */
    if ((int) pct != io_bar) {
        io_bar = (int) pct;
        io_paint(io_label, io_bar);
    }
}

void
_3ds_io_end(void)
{
    PrintConsole *prev;

    if (!io_active)
        return;
    io_active = 0;

    /* take the caption and the bar down again; the caller paints whatever
       belongs on the screen next over the top of this */
    prev = consoleSelect(&topConsole);
    top_row(IO_MSG_ROW, 0, COL_HI, "");
    top_row(IO_MSG_ROW + 1, 0, COL_HI, "");
    top_row(LOAD_ROW, 0, COL_HI, "");
    (void) consoleSelect(prev);
    gfxFlushBuffers();
    load_last = -1; /* the splash functions may be used again */
}

/* The game has cleared the screen to put its own up (which is how a load
   hands over from the splash to the game): the progress display has served
   its purpose, so drop it.  Without this the next I/O tick would paint the
   logo and the bar straight back over the screen the game has just drawn. */
void
_3ds_io_abandon(void)
{
    io_active = 0;
    load_last = -1;
}

/* ---------------------------------------------------------------- */
/* main menu                                                        */
/* ---------------------------------------------------------------- */


static void
draw_main_static(void)
{
    top_center(10, COL_HI, nh_tr("f o r   N i n t e n d o   3 D S"));
    top_center(12, COL_DIM, "Port by ran4erep, 2026");

    /* the game's own credits sit along the bottom edge of the screen */
    top_center(TOP_ROWS - 2, COL_DIM, "NetHack 5.0");
    top_center(TOP_ROWS - 1, COL_DIM, "(c) 1985-2026 NetHack Development Team");
}

static void
draw_main_buttons(void)
{
    _3ds_art_menu();
    _3ds_art_present();
}

/* the three home-menu tiles are purely touch: no cursor, no D-pad */
static int
main_menu_hit(u16 px, u16 py)
{
    return _3ds_art_menu_hit((int) px, (int) py);
}

static void
draw_main_screen(int frame)
{
    consoleSelect(&topConsole);
    printf("\x1b[2J\x1b[H");
    draw_main_static();
    draw_title(frame);
    gfxFlushBuffers();
    draw_main_buttons();
}

int
_3ds_main_menu(void)
{
    int frame = 0, result = -1;

    /* Not playing: the notification LED stays off. */
    _3ds_led_off();

    _3ds_settings_load();
    /* The menu track also covers character creation, which happens after
       the menu returns but before the first turn (see moveloop()). */
    _3ds_music_play("menu");
    _3ds_kbd_active(0);
    draw_main_screen(frame);

    while (result < 0) {
        u32 k;

        /* menus keep the LED off */
        _3ds_led_off();

        gspWaitForVBlank();
        if (!aptMainLoop()) {
            result = 0;
            break;
        }
        hidScanInput();
        k = hidKeysDown();

        /* START is kept as a shortcut; the tiles are touch only */
        if (k & KEY_START)
            result = 1;
        if (k & KEY_TOUCH) {
            touchPosition t;
            int hit;

            hidTouchRead(&t);
            hit = main_menu_hit(t.px, t.py);
            if (hit == 0)
                result = 1;
            else if (hit == 1) {
                _3ds_options_screen(0);
                draw_main_screen(frame);
            } else if (hit == 2)
                result = 0;
        }

        frame++;
        consoleSelect(&topConsole);
        draw_title(frame);
        gfxFlushBuffers();
    }

    consoleSelect(&topConsole);
    return (result == 1) ? 1 : 0;
}

/* ---------------------------------------------------------------- */
/* settings screen                                                  */
/* ---------------------------------------------------------------- */

static const int size_presets[] = { 8, 10, 12, 14, 16, 18, 20, 24,
                                    28, 32, 36, 40, 48, 56, 64 };
#define NSIZES ((int) (sizeof size_presets / sizeof size_presets[0]))

static int
size_step(int cur, int dir)
{
    int i, best = 0;

    for (i = 1; i < NSIZES; i++)
        if (abs(size_presets[i] - cur) < abs(size_presets[best] - cur))
            best = i;
    if (dir > 0) {
        if (best < NSIZES - 1)
            best++;
    } else {
        if (best > 0)
            best--;
    }
    return size_presets[best];
}

/* A calm backdrop for the screens whose controls all live on the top
   screen: the touch panel just shows the artwork, with no widget and no
   version stamp of its own. */
static void
draw_idle_panel(void)
{
    _3ds_art_bg();
    _3ds_art_present();
}

/* a horizontal rule that joins the outer frame's left and right edges */
static void
frame_sep(int y, const char *col)
{
    int i;

    top_goto(0, y);
    printf("%s+", col);
    for (i = 0; i < TOP_COLS - 2; i++)
        putchar('-');
    printf("+%s", COL_RESET);
}

/* ---------------------------------------------------------------- */
/* controller-friendly menus                                        */
/*                                                                  */
/* The tty port's menus are driven by typing an item's accelerator  */
/* letter, which needs the soft keyboard on a 3DS.  This routine      */
/* intercepts every NHW_MENU window and runs it as a scrollable       */
/* cursor list instead: D-pad moves, A chooses (or toggles), START    */
/* confirms a multiple-selection list and B cancels.  Inventory rows  */
/* also carry their object's tile icon.                               */
/* ---------------------------------------------------------------- */

#define MENU_MAX_ITEMS 256 /* safety cap for the pointer array */
#define MENU_ROW_MAX 23
/* Minimum frame height for read-only text windows (discoveries, fortunes,
   item info): short texts still fill a comfortable page instead of a tiny
   strip at the centre of the screen. */
#define MENU_PAGER_MIN_H 18

/* Move the cursor `count` selectable rows from `from` in direction `dir`,
   skipping headings and other rows which can't be selected. */
static int
menu_step(int from, int dir, int count, int nitems, const unsigned char *sel)
{
    int i = from;

    while (count-- > 0) {
        int guard = nitems;

        do {
            i += dir;
            if (i < 0)
                i = nitems - 1;
            else if (i >= nitems)
                i = 0;
        } while (!sel[i] && --guard > 0);
        if (!sel[i])
            return from;
    }
    return i;
}

/* Hold-to-repeat state for menu navigation (D-pad and L/R paging). */
static int menu_up_timer = 0, menu_dn_timer = 0;
static int menu_pu_timer = 0, menu_pd_timer = 0;

/* Fire once on the press, then repeatedly while the button is held, with
   an initial delay and a faster repeat rate in the same spirit as the map
   movement in tgetch(). */
static int
menu_repeat(u32 kDown, u32 kHeld, u32 mask, int *timer)
{
    if (kDown & mask) {
        *timer = 16;
        return 1;
    }
    if (kHeld & mask) {
        if (*timer > 0)
            --*timer;
        else {
            *timer = 6;
            return 1;
        }
    } else {
        *timer = 0;
    }
    return 0;
}

/* Wrapped display lines for read-only (text) windows.  A long line is
   broken at word boundaries into PAGE_W-character rows so it stays inside
   the frame instead of running off the screen edge. */
#define PAGE_W 46
#define PAGE_MAX 2048
static char page_store[PAGE_MAX][PAGE_W + 1];
static int page_count = 0;

static void
pager_wrap(const char *s)
{
    const char *p = s ? s : "";

    if (!*p) {
        if (page_count < PAGE_MAX) {
            page_store[page_count][0] = '\0';
            page_count++;
        }
        return;
    }
    while (*p) {
        int len, cut, sp = -1, k;

        if (page_count >= PAGE_MAX)
            return;
        len = (int) strlen(p);
        cut = (len < PAGE_W) ? len : PAGE_W;
        if (len > PAGE_W) {
            for (k = 0; k < cut; k++)
                if (p[k] == ' ')
                    sp = k;
            if (sp > 0)
                cut = sp;
        }
        memcpy(page_store[page_count], p, (size_t) cut);
        page_store[page_count][cut] = '\0';
        page_count++;
        p += cut;
        while (*p == ' ')
            p++;
    }
}

/* A title forced by the core for the next menu window (the item-actions
   menu), so it doesn't depend on the prompt heuristics below. */
static const char *menu_title_override = NULL;

void
_3ds_menu_title_override(const char *t)
{
    menu_title_override = t;
}

int
_3ds_menu(int window, void *arg)
{
    struct WinDesc *cw = (struct WinDesc *) arg;
    tty_menu_item *curr, *items[MENU_MAX_ITEMS], *prompt = NULL;
    unsigned char selectable[MENU_MAX_ITEMS];
    int nitems = 0, nsel = 0, cursor = 0, top = 0, done = 0, cancelled = 0;
    int pager = 0;
    int npage = 0;
    int y0, h, maxvis, i, row;
    char title[TOP_COLS + 1];
    int last_touch_row = -1;
    /* A read-only page (or any menu) can open as a side effect of a D-pad
       move (stepping onto an engraving / headstone).  Remember the nav keys
       that were already held when the window opened and ignore them until
       they are released, so the window does not appear to scroll itself by
       one row from the direction that just moved the hero. */
    boolean nav_first_poll = TRUE;
    u32 nav_suppress = 0;
    boolean any_glyph = FALSE;
    /* marquee scrolling state for the row under the cursor */
    int anim_idx = -1, anim_off = 0, anim_dir = 1, anim_timer = 0,
        anim_max = 0;
    boolean anim_dirty = FALSE;
#define MENU_TEXT_COLS 43
#define ANIM_PAUSE 30     /* frames to rest at start (~0.5 s) */
#define ANIM_PAUSE_END 36 /* frames to rest at far end (~0.6 s) */
#define ANIM_STEP 3       /* frames per character step (~1/20 s) */

    (void) window;
    if (!cw || !cw->mlist)
        return 0;

    for (curr = cw->mlist; curr; curr = curr->next) {
        if (curr->glyphinfo.glyph != NO_GLYPH)
            any_glyph = TRUE;
        if (curr->identifier.a_void)
            break;
    }

    /* tty_end_menu() prepends the prompt and then an empty spacer line
       ahead of the menu items.  Recognize that pair so the real game
       prompt ("What do you want to drop?", ...) can be used as the window
       title instead of a hardcoded word. */
    if (cw->mlist && !cw->mlist->identifier.a_void
        && cw->mlist->str && *cw->mlist->str
        && cw->mlist->next && !cw->mlist->next->identifier.a_void
        && cw->mlist->next->str && !*cw->mlist->next->str) {
        prompt = cw->mlist;
    }

    for (curr = cw->mlist; curr; curr = curr->next) {
        if (curr == prompt)
            continue; /* the prompt is rendered as the window title */
        if (prompt && curr == prompt->next)
            continue; /* the empty spacer the core inserts after the prompt */
        if (curr == cw->mlist && curr->str && !*curr->str)
            continue; /* (legacy) empty leading spacer when there's no prompt */
        if (nitems < MENU_MAX_ITEMS) {
            selectable[nitems] = (curr->identifier.a_void != NULL);
            if (selectable[nitems])
                ++nsel;
            items[nitems++] = curr;
        }
    }
    if (cw->how < PICK_NONE || cw->how > PICK_ANY)
        cw->how = PICK_NONE;
    /* With nothing selectable this is really a text window; show every
       line as read-only content instead of an empty list. */
    pager = (nsel == 0);

    /* Flatten and word-wrap the text-window content into display rows
       that fit inside the frame. */
    page_count = 0;
    if (pager) {
        for (i = 0; i < nitems; i++)
            pager_wrap(items[i]->str);
    }
    npage = page_count;

    if (menu_title_override)
        Snprintf(title, sizeof title, "%s", menu_title_override);
    else if (prompt && !strncmp(prompt->str, "Do what with ", 13))
        Snprintf(title, sizeof title, "Context menu");
    else if (prompt)
        Snprintf(title, sizeof title, "%s", prompt->str);
    else if (!pager && any_glyph)
        Snprintf(title, sizeof title, "Inventory");
    else
        Snprintf(title, sizeof title, "%s",
                 pager ? "Information"
                       : (cw->how == PICK_ANY ? "Choose items" : "Menu"));

    for (i = 0; i < nitems; i++)
        if (selectable[i]) {
            cursor = i;
            break;
        }

    {
        int nrows = pager ? npage : nitems;

        h = nrows < MENU_ROW_MAX ? nrows : MENU_ROW_MAX;
    }
    if (h < 1)
        h = 1;
    h += 5; /* title, separator, help, top and bottom border */
    /* Read-only text windows get a generous minimum size so short texts (a
       one-line engraving or headstone) fill a comfortable page instead of a
       small box. */
    if (pager && h < MENU_PAGER_MIN_H)
        h = MENU_PAGER_MIN_H;
    if (h > TOP_ROWS)
        h = TOP_ROWS;
    y0 = (TOP_ROWS - h) / 2;
    if (y0 < 0)
        y0 = 0;
    maxvis = h - 5;

    /* Keep the soft keyboard live while the menu is up: its return and
       escape keys act as A/B, and the toggle button stays usable. */
    _3ds_kbd_active(1);
    _3ds_effects_discard();

    while (!done) {
        char line[TOP_COLS + 1];

        /* compose the whole frame off-screen, then blit it in one pass */
        menu_frame_begin();
        consoleSelect(&topConsole);
        top_box(0, y0, TOP_COLS, h, COL_UI);

        /* title, shortened to fit inside the frame; window titles are
           drawn white, matching the inventory, not cyan */
        {
            char tbuf[TOP_COLS - 1];

            Snprintf(tbuf, sizeof tbuf, "%.*s", TOP_COLS - 4, nh_tr(title));
            top_center(y0 + 1, COL_UI, tbuf);
        }
        frame_sep(y0 + 2, COL_UI);

        if (!pager) {
            if (cursor < top)
                top = cursor;
            if (cursor >= top + maxvis)
                top = cursor - maxvis + 1;
        }
        if (top < 0)
            top = 0;
        if (pager) {
            if (npage > maxvis && top > npage - maxvis)
                top = npage - maxvis;
        } else {
            if (nitems > maxvis ? top > nitems - maxvis : top > 0)
                top = (nitems > maxvis) ? nitems - maxvis : 0;
        }

        /* Pick the marquee scroll state for the row under the cursor: a
           selectable row whose text overflows the MENU_TEXT_COLS text area
           scrolls left to its end, pauses, and scrolls back, forever. */
        {
            const char *txt = NULL;
            int len = 0;

            if (!pager && nsel > 0 && cursor >= 0 && cursor < nitems
                && selectable[cursor])
                txt = items[cursor]->str;
            if (txt)
                len = (int) strlen(txt);

            if (anim_idx != cursor) {
                anim_idx = cursor;
                anim_off = 0;
                anim_dir = 1;
                anim_timer = ANIM_PAUSE;
            }

            if (!txt || len <= MENU_TEXT_COLS) {
                anim_max = 0;
                anim_off = 0;
            } else {
                anim_max = len - MENU_TEXT_COLS;
            }
        }

        for (row = 0; row < maxvis; row++) {
            int idx = top + row;
            tty_menu_item *it;

            if (pager) {
                if (idx >= npage)
                    break;
                top_goto(1, y0 + 3 + row);
                printf("%s%-48.48s%s", COL_UI, page_store[idx], COL_RESET);
                continue;
            }
            if (idx >= nitems)
                break;
            it = items[idx];

            if (!selectable[idx]) {
                /* heading or separator: never carries the cursor */
                top_goto(1, y0 + 3 + row);
                printf("%s%-48.48s%s", COL_DIM, it->str ? it->str : "",
                       COL_RESET);
            } else {
                Snprintf(line, sizeof line, "%c ",
                         it->selected ? '*' : ' ');
                if (idx == cursor && anim_max > 0) {
                    /* marquee: ping-pong sliding window of the text */
                    int j, slen = (int) strlen(it->str);

                    for (j = 0; j < MENU_TEXT_COLS; j++) {
                        int src = anim_off + j;

                        line[2 + j] = (src >= 0 && src < slen) ? it->str[src] : ' ';
                    }
                    line[2 + MENU_TEXT_COLS] = '\0';
                } else {
                    Snprintf(line + 2, sizeof line - 2, "%.*s",
                             MENU_TEXT_COLS,
                             it->str ? it->str : "");
                }
                top_goto(1, y0 + 3 + row);
                printf("%s%-45.45s%s",
                       (idx == cursor) ? COL_CURS : COL_UI, line,
                       COL_RESET);

                /* Item icon in the right margin, clear of the text.
                   Inventory entries carry a glyph; headings and plain text
                   entries do not. */
                if (it->glyphinfo.glyph != NO_GLYPH)
                    _3ds_draw_icon(46 * 8, (y0 + 3 + row) * 8, 8,
                                   (int) it->glyphinfo.gm.tileidx);
            }
        }

        /* no on-screen control hints; the menu is self-explanatory on a
           3DS (D-pad moves, A/START chooses, B cancels) */

        /* paint the soft keyboard/log under the menu; the single present
           below shows both screens together */
        _3ds_kbd_draw();
        menu_frame_end();

        gfxFlushBuffers();
        gfxSwapBuffers();

        /* Poll directly: the tty input layer would translate the D-pad
           into vi keys and A into Enter, and this menu needs the raw
           buttons for its own cursor.  The soft keyboard stays live so
           its return/escape keys act as A/B, and the D-pad repeats while
           held. */
        {
            u32 kDown, kHeld;
            int up, dn, pu, pd;

            anim_dirty = FALSE;
            for (;;) {
                gspWaitForVBlank();
                if (!aptMainLoop()) {
                    cancelled = 1;
                    done = 1;
                    break;
                }
                hidScanInput();
                kDown = hidKeysDown();
                kHeld = hidKeysHeld();
                if (nav_first_poll) {
                    nav_first_poll = FALSE;
                    /* keys held (not freshly pressed) when the window
                       opened; e.g. the direction that walked the hero onto
                       the tile that reported a discovery */
                    nav_suppress = kHeld & ~kDown
                                   & (KEY_UP | KEY_DOWN | KEY_LEFT
                                      | KEY_RIGHT | KEY_L | KEY_R);
                }
                if (kDown
                    || (kHeld & (KEY_UP | KEY_DOWN | KEY_LEFT
                                 | KEY_RIGHT | KEY_L | KEY_R)))
                    break;
                /* nothing pressed: run the selected row's marquee */
                if (anim_max > 0) {
                    if (--anim_timer <= 0) {
                        anim_off += anim_dir;
                        if (anim_dir > 0 && anim_off >= anim_max) {
                            anim_off = anim_max;
                            anim_dir = -1;
                            anim_timer = ANIM_PAUSE_END;
                        } else if (anim_dir < 0 && anim_off <= 0) {
                            anim_off = 0;
                            anim_dir = 1;
                            anim_timer = ANIM_PAUSE;
                        } else {
                            anim_timer = ANIM_STEP;
                        }
                        anim_dirty = TRUE;
                        break; /* recompose the frame with the new offset */
                    }
                }
            }
            if (done)
                break;
            if (anim_dirty)
                continue; /* the outer loop redraws, then waits again */

            /* Ignore nav buttons that were already held when the window
               opened until they are released, so a stale directional from
               the move that opened this window cannot scroll a read-only
               page.  Once released, normal navigation resumes. */
            if (nav_suppress) {
                u32 still = kHeld & nav_suppress;

                if (!still)
                    nav_suppress = 0; /* all released: resume normal */
                kHeld &= ~nav_suppress;
                kDown &= ~nav_suppress;
            }

            /* Touch screen: the soft keyboard's return/escape act as A/B,
               the toggle button flips the board, and a tap elsewhere on the
               list still moves/activates the cursor. */
            if (kDown & KEY_TOUCH) {
                touchPosition t;
                int r, idx, kind = 0, ch = 0;
                int trow;

                hidTouchRead(&t);
                if (_3ds_art_kbd_button_hit(t.px, t.py)) {
                    _3ds_kbd_toggle(); /* flip visibility, no own present */
                } else if (_3ds_kbd_visible()
                           && _3ds_art_kbd_hit(t.px, t.py, &r, &idx,
                                                &kind, &ch)) {
                    if (kind == K3D_ENTER)
                        kDown |= KEY_A;
                    else if (kind == K3D_ESC)
                        kDown |= KEY_B;
                    /* letter/shift/etc keys are ignored inside menus */
                } else {
                    /* Map the tapped row onto a list row.  A first tap
                       moves the cursor there, a second tap on the same row
                       activates it; a read-only page scrolls a page. */
                    trow = (int) (t.py / 8) - (y0 + 3);
                    if (pager) {
                        if (trow >= 0 && trow < maxvis)
                            top += (trow < maxvis / 2) ? -maxvis : maxvis;
                    } else if (trow >= 0 && trow < maxvis
                               && top + trow < nitems
                               && selectable[top + trow]) {
                        trow += top;
                        if (cursor == trow && last_touch_row == trow)
                            kDown |= KEY_A;
                        else {
                            cursor = trow;
                            last_touch_row = trow;
                        }
                    }
                }
            }

            up = menu_repeat(kDown, kHeld, KEY_UP | KEY_LEFT,
                             &menu_up_timer);
            dn = menu_repeat(kDown, kHeld, KEY_DOWN | KEY_RIGHT,
                             &menu_dn_timer);
            pu = menu_repeat(kDown, kHeld, KEY_L, &menu_pu_timer);
            pd = menu_repeat(kDown, kHeld, KEY_R, &menu_pd_timer);

            if (pager) {
                if (up)
                    --top;
                else if (dn)
                    ++top;
                else if (pu)
                    top -= maxvis;
                else if (pd)
                    top += maxvis;
                else if (kDown & (KEY_A | KEY_B | KEY_START))
                    done = 1;
            } else if (up) {
                cursor = menu_step(cursor, -1, 1, nitems, selectable);
            } else if (pu) {
                cursor = menu_step(cursor, -1, maxvis, nitems, selectable);
            } else if (dn) {
                cursor = menu_step(cursor, 1, 1, nitems, selectable);
            } else if (pd) {
                cursor = menu_step(cursor, 1, maxvis, nitems, selectable);
            } else if (kDown & (KEY_A | KEY_START)) {
                if (nitems == 0) {
                    done = 1;
                } else if (cw->how == PICK_NONE) {
                    done = 1;
                } else if (cw->how == PICK_ONE) {
                    for (i = 0; i < nitems; i++)
                        items[i]->selected = FALSE;
                    items[cursor]->selected = TRUE;
                    items[cursor]->count = -1L;
                    done = 1;
                } else { /* PICK_ANY */
                    if (kDown & KEY_START) {
                        done = 1;
                    } else {
                        tty_menu_item *it = items[cursor];

                        it->selected = !it->selected;
                        it->count = -1L;
                        cursor = menu_step(cursor, 1, 1, nitems, selectable);
                    }
                }
            } else if (kDown & KEY_B) {
                for (i = 0; i < nitems; i++) {
                    items[i]->selected = FALSE;
                    items[i]->count = -1L;
                }
                cancelled = 1;
                done = 1;
            }
        }
    }

    if (cancelled)
        cw->flags |= WIN_CANCELLED;
    else
        cw->flags &= ~WIN_CANCELLED;
    if (ttyDisplay) {
        ttyDisplay->curx = ttyDisplay->cury = 0;
        ttyDisplay->toplin = TOPLINE_EMPTY;
    }
    consoleSelect(&topConsole);
    /* The frame is centred and taller than the list itself (title,
       separator and help lines add five rows), so tty's eraser - which only
       blanks cw->maxrow rows - leaves the top border in the message line
       and the bottom border in the status line.  Blank every row the frame
       used; the map redraw that follows the menu puts the map back and the
       status panel is drawn again here. */
    {
        int y;

        for (y = y0; y < y0 + h && y < TOP_ROWS; y++)
            top_row(y, 0, COL_UI, "");
    }
    _3ds_status_panel();
    consoleSelect(&topConsole);
    return 1;
}

/* one row inside the frame; the whole row reverse-videos when selected */
static void
frame_row(int y, int selected, const char *label, const char *value)
{
    char buf[64];

    snprintf(buf, sizeof buf, " %-16.16s %-28.28s", label, value);
    top_goto(1, y);
    printf("%s%-48.48s%s", selected ? COL_CURS : COL_UI, buf, COL_RESET);
}

static void frame_text_row(int, int, const char *);

/* One entry in the options screen.  Categories are headers which can be
   collapsed; everything else is an option row. */
enum {
    OID_CAT_3DS = 1,
    OID_CAT_SND,
    OID_CAT_NH,
    OID_LANGUAGE,
    OID_MAP,
    OID_SIZE_W,
    OID_SIZE_H,
    OID_CAMERA,
    OID_TILESET,
    OID_MORE,
    OID_LED,
    OID_TUTORIAL,
    OID_SHOWEXP,
    OID_WEAPON,
    OID_ARMOR,
    OID_TERRAIN,
    OID_PICKUP,
    OID_AUTOOPEN,
    OID_SAFEPET,
    OID_CONFIRM,
    OID_TIPS,
    OID_CMDASSIST,
    OID_MENTION,
    OID_INVERSE,
    OID_HILITEPET,
    OID_HILITEPILE,
    OID_SORTLOOT,
    OID_SHOWDAMAGE,
    OID_VOL_MASTER,
    OID_VOL_MUSIC,
    OID_VOL_SFX
};

static const char *const onoff_names[] = { "Off", "On" };
static const char *const lang_names[] = { "English", "\xD0\xF3\xF1\xF1\xEA\xE8\xE9" };
static const char *const map_names[] = { "Tiles", "Symbols" };
static const char *const camera_names[] = { "Scroll", "Centered" };
static const char *const tileset_names[] = { "Default", "PixelHack" };
static const char *const more_names[] = { "Press Enter", "Auto" };
static const char *const tutorial_names[] = {
    "Ask each game", "Always", "Never"
};
static const char *const sortloot_names[] = { "None", "Loot", "Full" };
static const char *const volume_names[] = {
    "0%", "10%", "20%", "30%", "40%", "50%",
    "60%", "70%", "80%", "90%", "100%"
};

static int
opt_is_category(int id)
{
    return id == OID_CAT_3DS || id == OID_CAT_SND || id == OID_CAT_NH;
}

static int
opt_category_index(int id)
{
    if (id == OID_CAT_3DS)
        return 0;
    return (id == OID_CAT_SND) ? 1 : 2;
}

/* Storage slot for a volume row, or NULL. */
static int *
opt_volume_field(int id)
{
    if (id == OID_VOL_MASTER)
        return &U.master_volume;
    if (id == OID_VOL_MUSIC)
        return &U.music_volume;
    if (id == OID_VOL_SFX)
        return &U.sfx_volume;
    return (int *) 0;
}

static const char *
opt_label(int id)
{
    switch (id) {
    case OID_CAT_3DS:
        return nh_tr("3DS UI settings");
    case OID_CAT_SND:
        return nh_tr("Sound");
    case OID_CAT_NH:
        return nh_tr("NetHack settings");
    case OID_LANGUAGE:
        return nh_tr("Language");
    case OID_MAP:
        return nh_tr("Map display");
    case OID_SIZE_W:
        return nh_tr(U.tiled_map ? "Tile width" : "Symbol width");
    case OID_SIZE_H:
        return nh_tr(U.tiled_map ? "Tile height" : "Symbol height");
    case OID_CAMERA:
        return nh_tr("Camera");
    case OID_TILESET:
        return nh_tr("Tileset");
    case OID_MORE:
        return nh_tr("More prompts");
    case OID_LED:
        return nh_tr("Health LED");
    case OID_TUTORIAL:
        return nh_tr("Tutorial");
    case OID_SHOWEXP:
        return nh_tr("Show XP points");
    case OID_WEAPON:
        return nh_tr("Weapon status");
    case OID_ARMOR:
        return nh_tr("Armor status");
    case OID_TERRAIN:
        return nh_tr("Terrain status");
    case OID_PICKUP:
        return nh_tr("Autopickup");
    case OID_AUTOOPEN:
        return nh_tr("Autoopen doors");
    case OID_SAFEPET:
        return nh_tr("Safe pet");
    case OID_CONFIRM:
        return nh_tr("Confirm attacks");
    case OID_TIPS:
        return nh_tr("Show hints");
    case OID_CMDASSIST:
        return nh_tr("Command assist");
    case OID_MENTION:
        return nh_tr("Mention walls");
    case OID_INVERSE:
        return nh_tr("Inverse video");
    case OID_HILITEPET:
        return nh_tr("Highlight pets");
    case OID_HILITEPILE:
        return nh_tr("Highlight piles");
    case OID_SORTLOOT:
        return nh_tr("Sort loot");
    case OID_SHOWDAMAGE:
        return nh_tr("Show damage");
    case OID_VOL_MASTER:
        return nh_tr("Master volume");
    case OID_VOL_MUSIC:
        return nh_tr("Music volume");
    case OID_VOL_SFX:
        return nh_tr("Sound volume");
    }
    return "";
}

/* Rows which are simple yes/no options.  Returns the storage slot or NULL. */
static int *
opt_flag_field(int id)
{
    switch (id) {
    case OID_SHOWEXP:
        return &U.showexp;
    case OID_WEAPON:
        return &U.weaponstatus;
    case OID_ARMOR:
        return &U.armorstatus;
    case OID_TERRAIN:
        return &U.terrainstatus;
    case OID_PICKUP:
        return &U.pickup;
    case OID_AUTOOPEN:
        return &U.autoopen;
    case OID_SAFEPET:
        return &U.safe_pet;
    case OID_CONFIRM:
        return &U.confirm;
    case OID_LED:
        return &U.led;
    case OID_TIPS:
        return &U.tips;
    case OID_CMDASSIST:
        return &U.cmdassist;
    case OID_MENTION:
        return &U.mention_walls;
    case OID_INVERSE:
        return &U.use_inverse;
    case OID_HILITEPET:
        return &U.hilite_pet;
    case OID_HILITEPILE:
        return &U.hilite_pile;
    case OID_SHOWDAMAGE:
        return &U.showdamage;
    }
    return (int *) 0;
}

static int
opt_choice_names(int id, const char *const **names)
{
    switch (id) {
    case OID_LANGUAGE:
        *names = lang_names;
        return 2;
    case OID_MAP:
        *names = map_names;
        return 2;
    case OID_CAMERA:
        *names = camera_names;
        return 2;
    case OID_TILESET:
        *names = tileset_names;
        return 2;
    case OID_MORE:
        *names = more_names;
        return 2;
    case OID_TUTORIAL:
        *names = tutorial_names;
        return 3;
    case OID_SORTLOOT:
        *names = sortloot_names;
        return 3;
    case OID_VOL_MASTER:
    case OID_VOL_MUSIC:
    case OID_VOL_SFX:
        *names = volume_names;
        return (int) (sizeof volume_names / sizeof volume_names[0]);
    default:
        *names = onoff_names;
        return 2;
    }
}

static int
opt_choice_index(int id)
{
    int *p, *vp;

    vp = opt_volume_field(id);
    if (vp) {
        int v = *vp;

        if (v < 0)
            v = 0;
        if (v > 100)
            v = 100;
        return (v + 5) / 10;
    }
    switch (id) {
    case OID_LANGUAGE:
        return U.language ? 1 : 0;
    case OID_MAP:
    case OID_CAMERA:
        return U.camera ? 1 : 0;
    case OID_TILESET:
        return U.tileset ? 1 : 0;
    case OID_MORE:
        return U.more_confirm ? 0 : 1;
    case OID_TUTORIAL:
        return U.tutorial_mode;
    case OID_SORTLOOT:
        return U.sortloot;
    }
    p = opt_flag_field(id);
    return p ? (*p ? 1 : 0) : 0;
}

static void
opt_set_choice(int id, int idx)
{
    int *p, *vp;

    vp = opt_volume_field(id);
    if (vp) {
        *vp = idx * 10;
        if (*vp < 0)
            *vp = 0;
        if (*vp > 100)
            *vp = 100;
        apply_nh_options();
        return;
    }
    switch (id) {
    case OID_LANGUAGE:
        U.language = (idx == 1) ? 1 : 0;
        _3ds_i18n_set_language(U.language);
        break;
    case OID_MAP:
        U.tiled_map = (idx == 0) ? TRUE : FALSE;
        break;
    case OID_CAMERA:
        U.camera = (idx == 1) ? 1 : 0;
        break;
    case OID_TILESET:
        U.tileset = (idx == 1) ? 1 : 0;
        break;
    case OID_MORE:
        U.more_confirm = (idx == 0) ? 1 : 0;
        break;
    case OID_TUTORIAL:
        U.tutorial_mode = idx;
        break;
    case OID_SORTLOOT:
        U.sortloot = idx;
        break;
    default:
        p = opt_flag_field(id);
        if (p)
            *p = idx ? 1 : 0;
        break;
    }
    apply_nh_options();
}

static void
opt_value_text(int id, char *buf, size_t bufsz)
{
    const char *const *names;
    int n, idx, *vp;

    vp = opt_volume_field(id);
    if (vp) {
        snprintf(buf, bufsz, "%d%%", *vp);
        return;
    }
    if (id == OID_SIZE_W) {
        snprintf(buf, bufsz, "%d px", U.tiled_map ? U.tile_w : U.char_w);
        return;
    }
    if (id == OID_SIZE_H) {
        snprintf(buf, bufsz, "%d px", U.tiled_map ? U.tile_h : U.char_h);
        return;
    }
    n = opt_choice_names(id, &names);
    idx = opt_choice_index(id);
    if (idx < 0 || idx >= n)
        idx = 0;
    snprintf(buf, bufsz, "%s", nh_tr(names[idx]));
}

static int *
opt_size_field(int id)
{
    if (id == OID_SIZE_W)
        return U.tiled_map ? &U.tile_w : &U.char_w;
    return U.tiled_map ? &U.tile_h : &U.char_h;
}

/* Build the visible rows.  Categories hide their children when collapsed;
   the tileset row only exists in graphical-tile mode. */
static int
build_opt_rows(int *rows, const int cat_open[3])
{
    int n = 0;

    rows[n++] = OID_CAT_3DS;
    if (cat_open[0]) {
        rows[n++] = OID_LANGUAGE;
        rows[n++] = OID_MAP;
        rows[n++] = OID_SIZE_W;
        rows[n++] = OID_SIZE_H;
        rows[n++] = OID_CAMERA;
        if (U.tiled_map)
            rows[n++] = OID_TILESET;
        rows[n++] = OID_MORE;
        rows[n++] = OID_LED;
    }
    rows[n++] = OID_CAT_SND;
    if (cat_open[1]) {
        rows[n++] = OID_VOL_MASTER;
        rows[n++] = OID_VOL_MUSIC;
        rows[n++] = OID_VOL_SFX;
    }
    rows[n++] = OID_CAT_NH;
    if (cat_open[2]) {
        rows[n++] = OID_TUTORIAL;
        rows[n++] = OID_TIPS;
        rows[n++] = OID_CMDASSIST;
        rows[n++] = OID_SHOWEXP;
        rows[n++] = OID_WEAPON;
        rows[n++] = OID_ARMOR;
        rows[n++] = OID_TERRAIN;
        rows[n++] = OID_PICKUP;
        rows[n++] = OID_AUTOOPEN;
        rows[n++] = OID_SAFEPET;
        rows[n++] = OID_CONFIRM;
        rows[n++] = OID_MENTION;
        rows[n++] = OID_INVERSE;
        rows[n++] = OID_HILITEPET;
        rows[n++] = OID_HILITEPILE;
        rows[n++] = OID_SORTLOOT;
        rows[n++] = OID_SHOWDAMAGE;
    }
    return n;
}

/* Rows 3..27 are the visible list; the footer lives on row 28.  All three
   categories open no longer fit in one screen, so the caller supplies the
   index of the first visible row. */
#define OPT_VISROWS 25

static void
draw_opt_screen(int sel, int ingame, const int cat_open[3],
                const int *rows, int nrows, int top)
{
    char label[40], value[40];
    const char *hdr = ingame ? nh_tr("SETTINGS") : nh_tr("OPTIONS");
    int i, y = 3, last = top + OPT_VISROWS;

    if (last > nrows)
        last = nrows;

    consoleSelect(&topConsole);
    top_box(0, 0, TOP_COLS, 30, COL_UI);
    top_center(1, COL_UI, hdr);
    frame_sep(2, COL_UI);

    for (i = top; i < last; i++, y++) {
        int id = rows[i];

        if (opt_is_category(id)) {
            snprintf(label, sizeof label, "[%c] %s",
                     cat_open[opt_category_index(id)] ? '-' : '+',
                     opt_label(id));
            frame_text_row(y, i == sel, label);
        } else {
            snprintf(label, sizeof label, " %s", opt_label(id));
            opt_value_text(id, value, sizeof value);
            frame_row(y, i == sel, label, value);
        }
    }
    /* Only draw the separator when the list does not already reach row 27
       (which would make it overwrite the last visible row). */
    if (y <= 27)
        frame_sep(27, COL_UI);
    top_center(28, COL_UI, nh_tr("A change  L/R adjust  B back"));
}

/* A context menu anchored below an option row, like the character-creation
   dropdowns.  It flips upwards when there is no room below. */
static void
draw_opt_dropdown(int row, const char *title, const char *const *names,
                  int idx, int n)
{
    char buf[64], titlebuf[32];
    int x = 19, w = 30;
    int y = row + 1, h = n + 2, tl, i;

    if (y + h > 29)
        y = 29 - h;
    if (y < 1)
        y = 1;

    top_box(x, y, w, h, COL_UI);
    snprintf(titlebuf, sizeof titlebuf, "[ %s ]", title);
    tl = (int) strlen(titlebuf);
    top_goto(x + 1 + (w - 2 - tl) / 2, y);
    printf("%s%s%s", COL_UI, titlebuf, COL_RESET);

    for (i = 0; i < n; i++) {
        snprintf(buf, sizeof buf, " %-24.24s", nh_tr(names[i]));
        top_goto(x + 1, y + 1 + i);
        printf("%s%-26.26s%s", i == idx ? COL_CURS : COL_UI, buf,
               COL_RESET);
    }
}

void
_3ds_options_screen(int ingame)
{
    int sel = 0, done = 0, top = 0, cat_open[3] = { 0, 0, 0 };
    int rows[40], nrows, dd = 0, dd_idx = 0, dd_n = 0;
    int dd_id = 0, dd_row = 0;
    const char *const *dd_names = NULL;

    _3ds_settings_load();
    _3ds_kbd_active(0);
    draw_idle_panel();

    for (;;) {
        u32 k;

        /* Leaving via B sets done and continues so the row/composition
           code below is skipped; break right here, before the inner wait
           loop, or the screen would stay up until another key is pressed. */
        if (done)
            break;

        /* settings screen: LED off */
        _3ds_led_off();

        nrows = build_opt_rows(rows, cat_open);
        if (sel < 0)
            sel = 0;
        if (sel >= nrows)
            sel = nrows - 1;

        /* keep the selection inside the 25-row viewport */
        if (sel < top)
            top = sel;
        if (sel >= top + OPT_VISROWS)
            top = sel - OPT_VISROWS + 1;
        if (top > nrows - OPT_VISROWS)
            top = (nrows > OPT_VISROWS) ? nrows - OPT_VISROWS : 0;
        if (top < 0)
            top = 0;

        draw_opt_screen(sel, ingame, cat_open, rows, nrows, top);
        if (dd)
            draw_opt_dropdown(dd_row, opt_label(dd_id), dd_names,
                              dd_idx, dd_n);
        gfxFlushBuffers();

        do {
            gspWaitForVBlank();
            if (!aptMainLoop()) {
                done = 1;
                break;
            }
            hidScanInput();
            k = hidKeysDown();
        } while (k == 0);
        if (done)
            break;

        if (dd) {
            if (k & KEY_DOWN) {
                dd_idx = (dd_idx + 1) % dd_n;
            } else if (k & KEY_UP) {
                dd_idx = (dd_idx + dd_n - 1) % dd_n;
            } else if (k & KEY_A) {
                opt_set_choice(dd_id, dd_idx);
                dd = 0;
            } else if (k & KEY_B) {
                dd = 0;
            }
            continue;
        }

        if (k & KEY_DOWN) {
            sel = (sel + 1) % nrows;
            continue;
        }
        if (k & KEY_UP) {
            sel = (sel + nrows - 1) % nrows;
            continue;
        }
        if (k & KEY_B) {
            done = 1;
            continue;
        }
        if (k & KEY_A) {
            int id = rows[sel];

            if (opt_is_category(id)) {
                int ci = opt_category_index(id);

                cat_open[ci] = !cat_open[ci];
            } else if (id == OID_SIZE_W || id == OID_SIZE_H) {
                int *p = opt_size_field(id);

                *p = size_step(*p, 1);
            } else {
                dd_n = opt_choice_names(id, &dd_names);
                dd_idx = opt_choice_index(id);
                if (dd_idx < 0 || dd_idx >= dd_n)
                    dd_idx = 0;
                dd_id = id;
                dd_row = 3 + sel - top;
                dd = 1;
            }
            continue;
        }
        if (k & (KEY_LEFT | KEY_RIGHT)) {
            int id = rows[sel];
            int *vp = opt_volume_field(id);

            if (id == OID_SIZE_W || id == OID_SIZE_H) {
                int *p = opt_size_field(id);

                *p = size_step(*p, (k & KEY_RIGHT) ? 1 : -1);
            } else if (vp) {
                /* 5% steps, clamped: wrapping 100 down to 0 would be a
                   nasty surprise while adjusting the volume */
                *vp += (k & KEY_RIGHT) ? 5 : -5;
                if (*vp < 0)
                    *vp = 0;
                if (*vp > 100)
                    *vp = 100;
            }
            continue;
        }
    }

    /* Persist every change made on this visit.  Settings are no longer
       written on each keystroke (the synchronous SD-card write froze the
       screen mid-adjustment), so flush once on exit. */
    _3ds_settings_save();

    if (ingame) {
        iflags.wc_tiled_map = U.tiled_map ? TRUE : FALSE;
        iflags.wc_ascii_map = U.tiled_map ? FALSE : TRUE;
        if (U.tiled_map) {
            iflags.wc_tile_width = U.tile_w;
            iflags.wc_tile_height = U.tile_h;
        } else {
            iflags.wc_tile_width = U.char_w;
            iflags.wc_tile_height = U.char_h;
        }
        iflags.use_background_glyph = iflags.wc_tiled_map;
        _3ds_set_tileset(U.tileset);
        _3ds_reconfigure_display();
    }
    consoleSelect(&topConsole);
}

/* ---------------------------------------------------------------- */
/* pause menu                                                       */
/* ---------------------------------------------------------------- */

/* a full width text row inside the frame */
static void
frame_text_row(int y, int selected, const char *text)
{
    char buf[64];

    snprintf(buf, sizeof buf, " %-46.46s", text);
    top_goto(1, y);
    printf("%s%-48.48s%s", selected ? COL_CURS : COL_UI, buf, COL_RESET);
}

static void
draw_pause_screen(int sel)
{
    static const char *item[3] = {
        "Continue playing",
        "Settings",
        "Save and return to main menu"
    };
    int i;

    consoleSelect(&topConsole);
    top_box(0, 0, TOP_COLS, 30, COL_UI);
    top_center(1, COL_UI, nh_tr("P A U S E D"));
    frame_sep(2, COL_UI);
    for (i = 0; i < 3; i++)
        frame_text_row(4 + i * 2, sel == i, nh_tr(item[i]));

    gfxFlushBuffers();
}

static void
ui_save_and_exit_to_menu(void)
{
    clear_nhwindow(WIN_MESSAGE);
    if (dosave0()) {
        program_state.savefile_completed++;
        u.uhp = -1; /* universal "game is over" indicator */
        exit_nhwindows("Be seeing you...");
        nh_terminate(EXIT_SUCCESS); /* longjmps back to the main menu */
    } else {
        /* the save failed and we are staying in the game; the progress
           screen replaced both displays, so put the game's back */
        _3ds_init_bottom_ui();
        _3ds_reconfigure_display();
        docrt();
    }
}

void
_3ds_pause_menu(void)
{
    int sel = 0, done = 0;

    /* there is nothing to pause or save until a game is under way (the
       name is being asked, or a character is being rolled) */
    if (!program_state.something_worth_saving)
        return;

    _3ds_kbd_active(0);
    draw_idle_panel();
    draw_pause_screen(sel);

    while (!done) {
        u32 k;

        /* pause menu: LED off */
        _3ds_led_off();

        gspWaitForVBlank();
        if (!aptMainLoop()) {
            _3ds_menu_loop = 0;
            nethack_exit(EXIT_SUCCESS);
        }
        hidScanInput();
        k = hidKeysDown();

        if (k & KEY_DOWN) {
            sel = (sel + 1) % 3;
            draw_pause_screen(sel);
        }
        if (k & KEY_UP) {
            sel = (sel + 2) % 3;
            draw_pause_screen(sel);
        }
        if ((k & KEY_A) || (k & KEY_START)) {
            if (sel == 0)
                done = 1;
            else if (sel == 1) {
                _3ds_options_screen(1);
                _3ds_kbd_active(0);
                draw_pause_screen(sel);
            } else
                ui_save_and_exit_to_menu();
        }
        if (k & KEY_B)
            done = 1;
    }

    /* swallow the key release so we don't immediately re-enter the menu */
    while (aptMainLoop()) {
        gspWaitForVBlank();
        hidScanInput();
        if (hidKeysHeld() == 0)
            break;
    }

    /* wipe the menu and repaint the game; the bottom panel first so the
       status the core is about to draw is not painted over */
    _3ds_init_bottom_ui();
    _3ds_reconfigure_display();
    consoleSelect(&topConsole);
}

/* ---------------------------------------------------------------- */
/* character creation                                               */
/* ---------------------------------------------------------------- */

static int
role_count(void)
{
    int n = 0;

    while (roles[n].name.m)
        n++;
    return n;
}

static int
race_count(void)
{
    int n = 0;

    while (races[n].noun)
        n++;
    return n;
}

static void
fix_dependents(void)
{
    if (flags.initrace >= 0
        && !ok_race(flags.initrole, flags.initrace, flags.initgend,
                    flags.initalign))
        flags.initrace = ROLE_NONE;
    if (flags.initgend >= 0
        && !ok_gend(flags.initrole, flags.initrace, flags.initgend,
                    flags.initalign))
        flags.initgend = ROLE_NONE;
    if (flags.initalign >= 0
        && !ok_align(flags.initrole, flags.initrace, flags.initgend,
                     flags.initalign))
        flags.initalign = ROLE_NONE;
}

static void
cycle_field(int field, int dir)
{
    int i, n, cur;
    static int cand[64];

    cand[0] = ROLE_NONE; /* "Random" cycles first */
    n = 1;
    if (field == 1) { /* role */
        for (i = 0; i < role_count() && n < 64; i++)
            if (ok_role(i, flags.initrace, flags.initgend, flags.initalign))
                cand[n++] = i;
    } else if (field == 2) { /* race */
        for (i = 0; i < race_count() && n < 64; i++)
            if (ok_race(flags.initrole, i, flags.initgend, flags.initalign))
                cand[n++] = i;
    } else if (field == 3) { /* gender */
        for (i = 0; i < ROLE_GENDERS; i++)
            if (ok_gend(flags.initrole, flags.initrace, i, flags.initalign))
                cand[n++] = i;
    } else if (field == 4) { /* alignment */
        for (i = 0; i < ROLE_ALIGNS; i++)
            if (ok_align(flags.initrole, flags.initrace, flags.initgend, i))
                cand[n++] = i;
    } else {
        return;
    }

    cur = -1;
    for (i = 0; i < n; i++)
        if (cand[i] == (field == 1 ? flags.initrole
                        : field == 2 ? flags.initrace
                        : field == 3 ? flags.initgend
                                     : flags.initalign)) {
            cur = i;
            break;
        }
    if (cur < 0)
        cur = 0;
    else
        cur = (cur + dir + n) % n;

    if (field == 1)
        flags.initrole = cand[cur];
    else if (field == 2)
        flags.initrace = cand[cur];
    else if (field == 3)
        flags.initgend = cand[cur];
    else
        flags.initalign = cand[cur];

    fix_dependents();
}

static void
randomize_character(void)
{
    flags.initrole = randrole(FALSE);
    flags.initrace = randrace(flags.initrole);
    flags.initgend = randgend(flags.initrole, flags.initrace);
    flags.initalign = randalign(flags.initrole, flags.initrace);
}

static const char *
role_name(void)
{
    return (flags.initrole >= 0) ? nh_tr(roles[flags.initrole].name.m) : nh_tr("Random");
}

static const char *
race_name(void)
{
    return (flags.initrace >= 0) ? nh_tr(races[flags.initrace].noun) : nh_tr("Random");
}

static const char *
gend_name(void)
{
    return (flags.initgend >= 0) ? nh_tr(genders[flags.initgend].adj) : nh_tr("Random");
}

static const char *
align_name(void)
{
    return (flags.initalign >= 0) ? nh_tr(aligns[flags.initalign].adj) : nh_tr("Random");
}

static void
ui_strcat_s(char *dst, int dstsz, const char *s)
{
    size_t d = strlen(dst);

    if (d < (size_t) dstsz - 1)
        strncat(dst, s, (size_t) dstsz - 1 - d);
}

static void
build_summary(char *out, int outsz)
{
    int n = 0, i;
    char parts[4][40];

    if (flags.initalign >= 0 && flags.initalign < ROLE_ALIGNS)
        snprintf(parts[n++], sizeof parts[0], "%s", nh_tr(aligns[flags.initalign].adj));
    if (flags.initgend >= 0 && flags.initgend < ROLE_GENDERS)
        snprintf(parts[n++], sizeof parts[0], "%s", nh_tr(genders[flags.initgend].adj));
    if (flags.initrace >= 0)
        snprintf(parts[n++], sizeof parts[0], "%s", nh_tr(races[flags.initrace].noun));
    if (flags.initrole >= 0)
        snprintf(parts[n++], sizeof parts[0], "%s", nh_tr(roles[flags.initrole].name.m));

    out[0] = '\0';
    if (n == 0) {
        snprintf(out, (size_t) outsz, "%s", nh_tr("a completely random adventurer"));
        return;
    }
    for (i = 0; i < n; i++) {
        if (i)
            ui_strcat_s(out, outsz, " ");
        ui_strcat_s(out, outsz, parts[i]);
    }
}

/* candidate values for a character field; ROLE_NONE ("Random") comes first */
static int
field_choices(int field, int *cand)
{
    int i, n = 0;

    cand[n++] = ROLE_NONE; /* "Random" sits at the top of the dropdown */
    if (field == 1) {
        for (i = 0; i < role_count() && n < 64; i++)
            if (ok_role(i, flags.initrace, flags.initgend, flags.initalign))
                cand[n++] = i;
    } else if (field == 2) {
        for (i = 0; i < race_count() && n < 64; i++)
            if (ok_race(flags.initrole, i, flags.initgend, flags.initalign))
                cand[n++] = i;
    } else if (field == 3) {
        for (i = 0; i < ROLE_GENDERS; i++)
            if (ok_gend(flags.initrole, flags.initrace, i, flags.initalign))
                cand[n++] = i;
    } else {
        for (i = 0; i < ROLE_ALIGNS; i++)
            if (ok_align(flags.initrole, flags.initrace, flags.initgend, i))
                cand[n++] = i;
    }
    return n;
}

static int
field_value(int field)
{
    return field == 1 ? flags.initrole
           : field == 2 ? flags.initrace
           : field == 3 ? flags.initgend
                        : flags.initalign;
}

static void
field_set(int field, int v)
{
    if (field == 1)
        flags.initrole = v;
    else if (field == 2)
        flags.initrace = v;
    else if (field == 3)
        flags.initgend = v;
    else
        flags.initalign = v;
    fix_dependents();
}

static const char *
field_choice_name(int field, int v)
{
    if (v == ROLE_NONE)
        return nh_tr("Random");
    if (field == 1)
        return nh_tr(roles[v].name.m);
    if (field == 2)
        return nh_tr(races[v].noun);
    if (field == 3)
        return nh_tr(genders[v].adj);
    return nh_tr(aligns[v].adj);
}

/* field is 1..4 here, matching the dropdown that hangs off each row */
static void
draw_char_screen(int field, const char *name)
{
    static const char *labels[4] = {
        "Role", "Race", "Gender", "Alignment"
    };
    const char *values[4];
    char summary[120], desc[80];
    int i;

    values[0] = role_name();
    values[1] = race_name();
    values[2] = gend_name();
    values[3] = align_name();

    consoleSelect(&topConsole);
    top_box(0, 0, TOP_COLS, 30, COL_UI);
    top_center(1, COL_UI, nh_tr("C R E A T E   A   C H A R A C T E R"));
    frame_sep(2, COL_UI);
    /* the name was asked for before this screen was reached */
    frame_row(3, FALSE, nh_tr("Name"), name);
    for (i = 0; i < 4; i++)
        frame_row(4 + i, field == i + 1, nh_tr(labels[i]), values[i]);
    frame_sep(9, COL_UI);

    build_summary(desc, sizeof desc);
    if (name[0])
        snprintf(summary, sizeof summary, "%s, the %s", name, desc);
    else
        snprintf(summary, sizeof summary, "%s", desc);
    top_center(10, COL_UI, summary);
    frame_sep(12, COL_UI);

    top_center(28, COL_UI, nh_tr("A change  X random  B back  Y rename  START go"));
}

/* The choices of a field as a context menu anchored to that field's row:
   it hangs off the value column, right below the item it belongs to,
   the way a popup menu works in an operating system. */
static void
draw_dropdown(int field, int idx, int n, const int *cand)
{
    static const char *titles[5] = {
        "", "Role", "Race", "Gender", "Alignment"
    };
    char title[24];
    int row = 3 + field; /* the row frame_row() drew this field on */
    int x = 19, w = 30;  /* x = the value column of frame_row()    */
    int y = row + 1, h = n + 2, tl, i;

    if (y + h > 29)
        y = 29 - h; /* flip upwards when it would fall off the screen */
    if (y < 1)
        y = 1;

    top_box(x, y, w, h, COL_UI);

    snprintf(title, sizeof title, "[ %s ]", nh_tr(titles[field]));
    tl = (int) strlen(title);
    top_goto(x + 1 + (w - 2 - tl) / 2, y);
    printf("%s%s%s", COL_UI, title, COL_RESET);

    for (i = 0; i < n; i++) {
        char buf[64];

        snprintf(buf, sizeof buf, " %-24.24s",
                 field_choice_name(field, cand[i]));
        top_goto(x + 1, y + 1 + i);
        printf("%s%-26.26s%s", i == idx ? COL_CURS : COL_UI, buf,
               COL_RESET);
    }
}

/* ---------------------------------------------------------------- */
/* the hero's name                                                  */
/*                                                                  */
/* NetHack looks for a saved game under the name it is handed, so   */
/* the name is asked for before anything else: a name with a game   */
/* behind it is loaded as it stands, and only a name without one    */
/* goes on to roll a character.  The typing is done on the console's */
/* own keyboard; the top screen only shows the question and the     */
/* name as it stands.                                              */
/* ---------------------------------------------------------------- */

#define NAME_ROW 15     /* the field sits in the middle of the screen */
#define NAME_FIELD_W 34 /* a whole name fits, with a space either side */

/* The console's built-in keyboard over the bottom screen.  Returns 1 when
   the player confirmed and 0 when they backed out. */
static int
ui_input_text_impl(const char *hint, const char *initial, char *out,
                   int outsz, boolean must_fill)
{
    static SwkbdState sw;
    SwkbdButton btn;

    swkbdInit(&sw, SWKBD_TYPE_NORMAL, 2, outsz - 1);
    if (must_fill)
        swkbdSetValidation(&sw, SWKBD_NOTEMPTY_NOTBLANK, 0, 0);
    swkbdSetHintText(&sw, hint);
    swkbdSetInitialText(&sw, initial ? initial : "");
    btn = swkbdInputText(&sw, out, (size_t) outsz);
    return (btn == SWKBD_BUTTON_CONFIRM);
}

static int
ui_input_text(const char *hint, const char *initial, char *out, int outsz)
{
    return ui_input_text_impl(hint, initial, out, outsz, TRUE);
}

/* Public entry point for in-game text prompts (call, engrave, ...): opens
   the system keyboard and allows an empty reply.  Returns 1 on confirm and
   0 when the player backed out. */
int
_3ds_input_text(const char *hint, const char *initial, char *out, int outsz)
{
    return ui_input_text_impl(hint, initial, out, outsz, FALSE);
}

/* In-game text input handler used by win_getlin() (the tty port): shows a
   framed prompt window on the top screen and opens the system keyboard
   dialog for the line itself, the same pair used for naming the hero.
   Cancelling writes an escape to bufp. */
void
_3ds_getlin(const char *query, char *bufp)
{
    char answer[BUFSZ];
    int ok, y;

    _3ds_prompt_enter();
    _3ds_kbd_active(0); /* the typing is the system keyboard's job */

    /* a framed window with the question, like the character-creation
       screen, so the player sees what is being asked; the swkbd dialog
       then overlays the touch screen for the actual typing */
    consoleSelect(&topConsole);
    top_box(0, 12, TOP_COLS, 6, COL_UI);
    frame_sep(13, COL_UI);
    if (query && *query)
        top_center(14, COL_UI, nh_tr(query));
    top_row(15, 0, COL_UI, "");
    gfxFlushBuffers();

    ok = _3ds_input_text(nh_tr(query), "", answer, (int) sizeof answer);

    /* Erase the prompt window.  The box is painted over the map, so blank
       the rows it used and then restore the map tiles underneath: the
       caller does not always redraw the map (call/engrave change no visible
       tile), so without this the wiped window stays on screen. */
    for (y = 12; y < 18; y++)
        top_row(y, 0, COL_UI, "");
    if (iflags.window_inited && u.ux) {
        extern boolean _3ds_tiles_active(void);

        if (_3ds_tiles_active()) {
            d_level uz0_save = u.uz0;

            assign_level(&u.uz0, &u.uz);
            redraw_map(TRUE);
            u.uz0 = uz0_save;
        } else {
            docrt();
        }
    }
    _3ds_kbd_active(1);
    _3ds_bottom_ui_invalidate();
    _3ds_prompt_leave();

    if (!ok) {
        bufp[0] = '\033';
        bufp[1] = '\0';
    } else {
        Strcpy(bufp, answer);
    }
}

static void
draw_name_screen(const char *name)
{
    char field[NAME_FIELD_W + 2];
    int x = (TOP_COLS - NAME_FIELD_W) / 2;

    consoleSelect(&topConsole);
    top_box(0, 0, TOP_COLS, 30, COL_UI);
    top_center(1, COL_UI, nh_tr("W H O   A R E   Y O U ?"));
    frame_sep(2, COL_UI);

    /* the name being typed, in a field of its own in the middle */
    snprintf(field, sizeof field, " %-*.*s ", NAME_FIELD_W - 2,
             NAME_FIELD_W - 2, name);
    top_goto(x, NAME_ROW);
    printf("%s%s%s", COL_CURS, field, COL_RESET);

    gfxFlushBuffers();
}

/* Ask for a name: the question on the top screen, the console's own
   keyboard for the typing.  Returns 1 when the player confirmed and 0 when
   they backed out, in which case the name is left as it was found. */
static int
ask_name_prompt(char *name, int bufsz)
{
    char answer[NAME_MAXLEN];

    _3ds_kbd_active(0); /* the typing is the system keyboard's job */
    draw_name_screen(name);

    answer[0] = '\0';
    if (!ui_input_text(nh_tr("Who are you?"), name, answer, (int) sizeof answer)
        || !answer[0])
        return 0; /* backed out: keep the name it had */

    copynchars(name, answer, bufsz - 1);
    name[bufsz - 1] = '\0';
    draw_name_screen(name); /* show what they settled on */
    draw_idle_panel();
    consoleSelect(&topConsole);
    return 1;
}

/* returns 1 when a character was chosen, 0 when the player backed out */
static int
_3ds_char_creation(void)
{
    int field = 1, result = 0; /* field 1..4: role, race, gender, alignment */
    int dropdown = 0, dd_idx = 0, dd_n = 0;
    int cand[80];
    int i;

    _3ds_settings_load();

    fix_dependents();
    _3ds_kbd_active(0);
    draw_idle_panel();

    for (;;) {
        u32 k;

        draw_char_screen(field, svp.plname);
        if (dropdown) {
            dd_n = field_choices(field, cand);
            if (dd_idx >= dd_n)
                dd_idx = dd_n - 1;
            draw_dropdown(field, dd_idx, dd_n, cand);
        }
        gfxFlushBuffers();

        do {
            gspWaitForVBlank();
            if (!aptMainLoop())
                return 0;
            hidScanInput();
            k = hidKeysDown();
        } while (k == 0);

        if (k & KEY_START) {
            result = 1;
            break;
        }
        if (k & KEY_X) { /* roll a random character */
            randomize_character();
            dropdown = 0;
            continue;
        }
        if (k & KEY_Y) { /* change the name without losing the choices */
            if (ask_name_prompt(svp.plname, (int) sizeof svp.plname)) {
                copynchars(U.name, svp.plname, NAME_MAXLEN - 1);
                U.name[NAME_MAXLEN - 1] = '\0';
            }
            continue;
        }
        if (k & KEY_B) {
            if (dropdown)
                dropdown = 0;
            else
                break;
            continue;
        }
        if (k & KEY_DOWN) {
            if (dropdown)
                dd_idx = (dd_idx + 1) % dd_n;
            else
                field = (field % 4) + 1;
            continue;
        }
        if (k & KEY_UP) {
            if (dropdown)
                dd_idx = (dd_idx + dd_n - 1) % dd_n;
            else
                field = ((field + 2) % 4) + 1;
            continue;
        }
        if (k & KEY_LEFT) {
            if (!dropdown)
                cycle_field(field, -1);
            continue;
        }
        if (k & KEY_RIGHT) {
            if (!dropdown)
                cycle_field(field, 1);
            continue;
        }
        if (k & KEY_A) {
            if (dropdown) {
                field_set(field, cand[dd_idx]);
                dropdown = 0;
            } else {
                dd_n = field_choices(field, cand);
                dd_idx = 0;
                for (i = 0; i < dd_n; i++)
                    if (cand[i] == field_value(field)) {
                        dd_idx = i;
                        break;
                    }
                dropdown = 1;
            }
            continue;
        }
    }

    if (result) {
        /* resolve anything left as "Random" so role_init() has nothing
           surprising to do, then hand the choices over to NetHack */
        if (!validrole(flags.initrole))
            flags.initrole = randrole(FALSE);
        if (!validrace(flags.initrole, flags.initrace))
            flags.initrace = randrace(flags.initrole);
        if (!validgend(flags.initrole, flags.initrace, flags.initgend))
            flags.initgend = randgend(flags.initrole, flags.initrace);
        if (!validalign(flags.initrole, flags.initrace, flags.initalign))
            flags.initalign = randalign(flags.initrole, flags.initrace);
        if (flags.initgend >= 0 && flags.initgend < ROLE_GENDERS)
            flags.female = (flags.initgend == 1) ? TRUE : FALSE;

        /* the name was settled before this screen was reached */
        copynchars(U.name, svp.plname, NAME_MAXLEN - 1);
        U.name[NAME_MAXLEN - 1] = '\0';
        return 1;
    }
    return 0;
}

/* ---------------------------------------------------------------- */
/* tty window-port hooks                                            */
/* ---------------------------------------------------------------- */

/* NetHack asks the interface for a name before it decides whether there is
   a saved game to restore, so this is a real question: the name is typed on
   the console's own keyboard, and a save under it is what stops the
   character setup below from being reached at all. */
void
_3ds_askname(void)
{
    char namebuf[NAME_MAXLEN];

    _3ds_settings_load();

    /* start from the name that was used last time */
    if (U.name[0])
        copynchars(namebuf, U.name, NAME_MAXLEN - 1);
    else
        copynchars(namebuf, "Hero", NAME_MAXLEN - 1);
    namebuf[NAME_MAXLEN - 1] = '\0';

    (void) ask_name_prompt(namebuf, (int) sizeof namebuf);
    if (!namebuf[0]) /* an empty name would name no save file at all */
        copynchars(namebuf, U.name[0] ? U.name : "Hero", NAME_MAXLEN - 1);
    namebuf[NAME_MAXLEN - 1] = '\0';

    copynchars(svp.plname, namebuf, (int) sizeof svp.plname - 1);
    svp.plname[sizeof svp.plname - 1] = '\0';
    copynchars(U.name, namebuf, NAME_MAXLEN - 1);
    U.name[NAME_MAXLEN - 1] = '\0';
}

/* Helpers for the story shown on the intro screen.  The narrative is the
   same "legacy" text the game itself prints via dat/quest.lua
   (questtext.common.legacy); we reproduce it here so the 3DS screen can
   show it before the core has initialized the hero, substituting the deity
   name, the god's title and the role's starting rank for the format codes
   the real text uses (%d, %G and %r). */

static const char *
intro_deity_name(void)
{
    const struct Role *rl;
    const char *god = NULL;
    int align_val;

    if (flags.initrole < 0 || flags.initrole >= role_count())
        return "your deity";
    rl = &roles[flags.initrole];

    align_val = (flags.initalign >= 0 && flags.initalign < ROLE_ALIGNS)
                    ? aligns[flags.initalign].value
                    : A_NONE;

    switch (align_val) {
    case A_LAWFUL:
        god = rl->lgod;
        break;
    case A_NEUTRAL:
        god = rl->ngod;
        break;
    case A_CHAOTIC:
        god = rl->cgod;
        break;
    default:
        break;
    }
    if (!god || !*god) {
        if (rl->lgod && *rl->lgod)
            god = rl->lgod;
        else if (rl->ngod && *rl->ngod)
            god = rl->ngod;
        else
            god = rl->cgod;
    }
    if (!god || !*god)
        return "your deity";
    if (*god == '_')
        ++god; /* a leading '_' marks the deity as a goddess */
    return god;
}

static const char *
intro_god_title(void)
{
    const struct Role *rl;
    const char *god = NULL;
    int align_val;

    if (flags.initrole < 0 || flags.initrole >= role_count())
        return "god";
    rl = &roles[flags.initrole];

    align_val = (flags.initalign >= 0 && flags.initalign < ROLE_ALIGNS)
                    ? aligns[flags.initalign].value
                    : A_NONE;

    switch (align_val) {
    case A_LAWFUL:
        god = rl->lgod;
        break;
    case A_NEUTRAL:
        god = rl->ngod;
        break;
    case A_CHAOTIC:
        god = rl->cgod;
        break;
    default:
        break;
    }
    if (!god || !*god) {
        if (rl->lgod && *rl->lgod)
            god = rl->lgod;
        else if (rl->ngod && *rl->ngod)
            god = rl->ngod;
        else
            god = rl->cgod;
    }
    return (god && *god == '_') ? "goddess" : "god";
}

/* wrap a multi-line string into lines at most `width' characters wide,
   not splitting words across rows; returns the number of lines written */
static int
wrap_story(const char *in, char lines[][64], int maxlines, int width)
{
    char buf[BUFSZ];
    int n = 0;

    while (*in && n < maxlines) {
        int len = 0;

        while (in[len] && in[len] != '\n' && (int) sizeof buf > len + 1)
            len++;
        memcpy(buf, in, (size_t) len);
        buf[len] = '\0';
        in += len;
        if (*in == '\n')
            in++;
        if (len > 0 && buf[len - 1] == '\r')
            buf[--len] = '\0';

        while (len > 0 && n < maxlines) {
            int take;

            while (len > 0 && buf[0] == ' ') { /* drop indent on wraps */
                memmove(buf, buf + 1, (size_t) (len - 1) + 1);
                len--;
            }
            take = (len > width) ? width : len;
            if (len > width) { /* keep whole words together */
                int brk = take;
                while (brk > 0 && buf[brk] != ' ')
                    brk--;
                if (brk > 0)
                    take = brk;
            }
            memcpy(lines[n], buf, (size_t) take);
            lines[n][take] = '\0';
            n++;
            memmove(buf, buf + take, (size_t) (len - take) + 1);
            len -= take;
        }
    }
    return n;
}

/* The little story the player sees after rolling a character, plus the
   question about the tutorial level (unless the settings have already
   answered it for good).  phase 0 shows the story, phase 1 the tutorial
   question. */
static void
draw_intro_screen(int phase, int sel, int tutorial_mode)
{
    consoleSelect(&topConsole);
    top_box(0, 0, TOP_COLS, 30, COL_UI);
    top_center(1, COL_UI, nh_tr("T H E   D U N G E O N S   O F   D O O M"));
    frame_sep(2, COL_UI);

    if (phase == 0) {
        char story[1024];
        char lines[25][64];
        const char *deity, *title, *rank;
        int nlines, i, y;

        deity = intro_deity_name();
        title = intro_god_title();
        rank = (flags.initrole >= 0 && flags.initrole < role_count())
                   ? rank_of(1, roles[flags.initrole].mnum, flags.female)
                   : "rookie";

        if (_3ds_i18n_get_language() == NH_LANG_RU) {
            char story_utf8[1024];
            const char *ru_title = (!strcmp(title, "goddess")) ? "богиня" : "бог";
            const char *ru_rank = nh_tr(rank);

            Snprintf(story_utf8, sizeof story_utf8,
                     "Так гласит Книга %s:\n"
                     "\n"
                     "    После Сотворения мира жестокий бог Молох\n"
                     "    восстал против власти Создателя Мардука.\n"
                     "    Молох похитил величайший из артефактов богов —\n"
                     "    Амулет Йендора, и укрыл его в темных глубинах\n"
                     "    Геенны, где теперь скрывается и выжидает час.\n"
                     "\n"
                     "Ваш %s %s стремится овладеть Амулетом,\n"
                     "дабы обрести власть над другими богами.\n"
                     "\n"
                     "Вам, чье призвание — %s, предначертано от\n"
                     "рождения стать орудием воли %s. Вам суждено\n"
                     "добыть Амулет или пасть в бою. Час судьбы настал!\n"
                     "Во имя всех нас: ступайте же смело с именем %s!\n",
                     deity, ru_title, deity, ru_rank, deity, deity);
            _3ds_utf8_to_cp1251(story, story_utf8, sizeof story);
        } else {
            Snprintf(story, sizeof story,
                     "It is written in the Book of %s:\n"
                     "\n"
                     "    After the Creation, the cruel god Moloch rebelled\n"
                     "    against the authority of Marduk the Creator.\n"
                     "    Moloch stole from Marduk the most powerful of all\n"
                     "    the artifacts of the gods, the Amulet of Yendor,\n"
                     "    and he hid it in the dark cavities of Gehennom, the\n"
                     "    Under World, where he now lurks, and bides his time.\n"
                     "\n"
                     "Your %s %s seeks to possess the Amulet, and with it\n"
                     "to gain deserved ascendance over the other gods.\n"
                     "\n"
                     "You, a newly trained %s, have been heralded\n"
                     "from birth as the instrument of %s.  You are destined\n"
                     "to recover the Amulet for your deity, or die in the\n"
                     "attempt.  Your hour of destiny has come.  For the sake\n"
                     "of us all:  Go bravely with %s!\n",
                     deity, title, deity, rank, deity, deity);
        }

        nlines = wrap_story(story, lines, 25, TOP_COLS - 2);
        y = 3;
        for (i = 0; i < nlines && y < 26; i++, y++)
            top_puts(1, y, COL_UI, lines[i]);
        top_center(27, COL_DIM, nh_tr("Press A to continue"));
    } else {
        if (tutorial_mode == 0) {
            top_center(15, COL_UI, nh_tr("Will you learn the ways first?"));
            frame_text_row(18, sel == 0, nh_tr("Yes, show me the tutorial"));
            frame_text_row(19, sel == 1, nh_tr("No, straight into the dungeon"));
        } else {
            top_center(15, COL_UI, tutorial_mode == 1
                                       ? nh_tr("The tutorial is always enabled.")
                                       : nh_tr("The tutorial is disabled."));
            top_center(18, COL_UI, nh_tr("Press A to begin."));
        }
    }

    gfxFlushBuffers();
}

static void
_3ds_intro_screen(void)
{
    int sel = 0, done = 0, answer = 0;
    int tutorial_mode = _3ds_tutorial_mode();
    int phase = 0;

    _3ds_kbd_active(0);
    draw_idle_panel();
    draw_intro_screen(phase, sel, tutorial_mode);

    while (!done) {
        u32 k;

        gspWaitForVBlank();
        if (!aptMainLoop())
            break;
        hidScanInput();
        k = hidKeysDown();

        if (phase == 0) {
            if (k & (KEY_A | KEY_START | KEY_B)) {
                phase = 1;
                draw_intro_screen(phase, sel, tutorial_mode);
            }
            continue;
        }
        if (tutorial_mode == 0 && (k & (KEY_UP | KEY_DOWN))) {
            sel = !sel;
            draw_intro_screen(phase, sel, tutorial_mode);
        }
        if (k & KEY_A) {
            answer = (tutorial_mode == 0) ? (sel == 0)
                                          : (tutorial_mode == 1);
            done = 1;
        }
        if (k & KEY_B) {
            answer = (tutorial_mode == 0) ? 0 : (tutorial_mode == 1);
            done = 1;
        }
    }
    nh_tutorial_choice(answer ? TRUE : FALSE);
}

void
_3ds_player_selection(void)
{
    if (!_3ds_char_creation())
        nh_terminate(EXIT_SUCCESS); /* back to the main menu */
    _3ds_intro_screen();
    _3ds_init_bottom_ui(); /* hand the bottom screen back to the game */
}

/* ---------------------------------------------------------------- */
/* yes/no dialog                                                    */
/* ---------------------------------------------------------------- */

/* A small window with Yes/No buttons, used for every y/n question so the
   answer is a deliberate button press instead of a stray key.  The top
   console is single buffered, so the pixels under the window can be saved
   and put back afterwards. */
#define YN_ROWS 7
#define YN_TOP 10 /* first console row of the window */
static u16 yn_saved[YN_ROWS * 8][TOP_COLS * 8];

static void
yn_save_area(void)
{
    u16 *fb = topConsole.frameBuffer;
    int x, y;

    if (!fb)
        return;
    for (y = 0; y < YN_ROWS * 8; y++)
        for (x = 0; x < TOP_COLS * 8; x++)
            yn_saved[y][x]
                = fb[x * 240 + (239 - (YN_TOP * 8 + y))];
}

static void
yn_restore_area(void)
{
    u16 *fb = topConsole.frameBuffer;
    int x, y;

    if (!fb)
        return;
    for (y = 0; y < YN_ROWS * 8; y++)
        for (x = 0; x < TOP_COLS * 8; x++)
            fb[x * 240 + (239 - (YN_TOP * 8 + y))]
                = yn_saved[y][x];
}

static void
yn_draw(const char *query, boolean has_y, boolean has_n, int sel)
{
    char line[TOP_COLS - 6];

    consoleSelect(&topConsole);
    top_box(0, YN_TOP, TOP_COLS, YN_ROWS, COL_UI);

    Snprintf(line, sizeof line, "%.*s", (int) sizeof line - 1, nh_tr(query));
    top_center(YN_TOP + 1, COL_UI, line);

    if (has_y && has_n) {
        top_puts(13, YN_TOP + 3, (sel == 0) ? COL_CURS : COL_UI, nh_tr(" Yes "));
        top_puts(30, YN_TOP + 3, (sel == 1) ? COL_CURS : COL_UI, nh_tr(" No  "));
    } else if (has_y) {
        top_puts(21, YN_TOP + 3, COL_CURS, nh_tr(" Yes "));
    } else {
        top_puts(21, YN_TOP + 3, COL_CURS, nh_tr(" No  "));
    }
    top_center(YN_TOP + YN_ROWS - 2, COL_DIM, nh_tr("A select   B cancel"));
    gfxFlushBuffers();
    gfxSwapBuffers();
}

/* Returns 'y' or 'n' when it handled the question, 0 when resp allows more
   than a plain yes/no (the caller then falls back to the text prompt). */
char
_3ds_yn_dialog(const char *query, const char *resp, char def)
{
    char allowed[40], *e;
    boolean has_y = FALSE, has_n = FALSE;
    int i, sel, done = 0, nbtn;
    char res = 0;

    if (!query || !resp || !*resp)
        return 0;
    for (e = allowed, i = 0; *resp && i < (int) sizeof allowed - 1;
         resp++, i++) {
        if (*resp == '\033')
            break;
        /* 'a' (e.g. "alphabetical order" for the vanquished list) is close
           enough to a plain yes/no that we still show the Yes/No dialog;
           anything else (digits '#' or a richer choice set) falls back to
           the ordinary text prompt. */
        if (*resp != 'y' && *resp != 'n' && *resp != 'q'
            && *resp != 'a' && *resp != ' ')
            return 0; /* some other answer is possible */
        *e++ = *resp;
    }
    *e = '\0';
    has_y = (strchr(allowed, 'y') != 0);
    has_n = (strchr(allowed, 'n') != 0);
    if (!has_y && !has_n)
        return 0;
    if (def && def != 'y' && def != 'n' && def != 'q')
        def = has_n ? 'n' : 'y';

    nbtn = (has_y && has_n) ? 2 : 1;
    sel = (nbtn == 2 && def == 'n') ? 1 : 0;

    yn_save_area();
    yn_draw(query, has_y, has_n, sel);

    while (!done) {
        u32 k;

        gspWaitForVBlank();
        if (!aptMainLoop())
            break;
        hidScanInput();
        k = hidKeysDown();

        if (k & KEY_TOUCH) {
            /* the player may tap 'y' or 'n' on the touch keyboard */
            touchPosition t;
            int ch;

            hidTouchRead(&t);
            (void) _3ds_bottom_touch(t.px, t.py);
            ch = _3ds_take_queued_key();
            if (ch == 'y' && has_y) {
                res = 'y';
                done = 1;
            } else if (ch == 'n' && has_n) {
                res = 'n';
                done = 1;
            } else if (ch == '\033' || ch == 'q') {
                res = strchr(allowed, 'q') ? 'q' : (has_n ? 'n' : 'y');
                done = 1;
            }
            continue;
        }
        if (nbtn == 2 && (k & (KEY_LEFT | KEY_RIGHT | KEY_UP | KEY_DOWN))) {
            sel = !sel;
            yn_draw(query, has_y, has_n, sel);
            continue;
        }
        if (k & (KEY_A | KEY_START)) {
            res = (nbtn == 1) ? (has_y ? 'y' : 'n')
                              : ((sel == 0) ? 'y' : 'n');
            done = 1;
        } else if (k & KEY_B) {
            /* cancel: the answer the core would use for <esc> */
            if (strchr(allowed, 'q'))
                res = 'q';
            else if (def)
                res = def;
            else
                res = has_n ? 'n' : 'y';
            done = 1;
        }
    }

    yn_restore_area();
    consoleSelect(&topConsole);
    /* park the console cursor at the start of the message line: the game
       writes its top line from there */
    printf("\x1b[1;1H%s", COL_RESET);
    gfxFlushBuffers();
    gfxSwapBuffers();
    /* swallow the button release so the answer isn't seen again by the next
       read; only the answering buttons are waited for, the circle pad must
       not be able to keep this loop spinning */
    while (aptMainLoop()) {
        gspWaitForVBlank();
        hidScanInput();
        if (!(hidKeysHeld() & (KEY_A | KEY_B | KEY_START | KEY_TOUCH)))
            break;
    }
    return res;
}

/* ---------------------------------------------------------------- */
/* kick dialog                                                      */
/* ---------------------------------------------------------------- */

#define KICK_ROWS 8
#define KICK_TOP 9

static void
kick_draw(const char *title, int sel, int cnt)
{
    char line[TOP_COLS - 6];
    char cntbuf[32];
    const char *rows[3];

    rows[0] = nh_tr("Kick once");
    Snprintf(cntbuf, sizeof cntbuf, nh_tr("Kick %d times"), cnt);
    rows[1] = cntbuf;
    rows[2] = nh_tr("Kick until broken");

    consoleSelect(&topConsole);
    top_box(0, KICK_TOP, TOP_COLS, KICK_ROWS, COL_UI);
    Snprintf(line, sizeof line, "%.*s", (int) sizeof line - 1, nh_tr(title));
    top_center(KICK_TOP + 1, COL_UI, line);
    top_puts(4, KICK_TOP + 3, (sel == 0) ? COL_CURS : COL_UI, rows[0]);
    top_puts(4, KICK_TOP + 4, (sel == 1) ? COL_CURS : COL_UI, rows[1]);
    top_puts(4, KICK_TOP + 5, (sel == 2) ? COL_CURS : COL_UI, rows[2]);
    top_center(KICK_TOP + KICK_ROWS - 2, COL_DIM,
               nh_tr("Up/Down choose   Left/Right count   A ok   B cancel"));
    gfxFlushBuffers();
    gfxSwapBuffers();
}

/* Choose how many kicks to perform: once, a count selected with left/right,
   or until the door breaks (a generous fixed series of kicks).  Returns 1
   when confirmed (fills *count_out and *until_out), 0 on cancel. */
int
_3ds_kick_dialog(int *count_out, boolean *until_out)
{
    static u16 kick_saved[KICK_ROWS * 8][TOP_COLS * 8];
    u16 *fb = topConsole.frameBuffer;
    int x, y, sel = 0, cnt = 3, done = 0, cancelled = 0;

    if (fb) {
        for (y = 0; y < KICK_ROWS * 8; y++)
            for (x = 0; x < TOP_COLS * 8; x++)
                kick_saved[y][x]
                    = fb[x * 240 + (239 - (KICK_TOP * 8 + y))];
    }
    kick_draw("Kick", sel, cnt);

    while (!done) {
        u32 k;

        gspWaitForVBlank();
        if (!aptMainLoop()) {
            cancelled = 1;
            break;
        }
        hidScanInput();
        k = hidKeysDown();

        if (k & KEY_UP) {
            sel = (sel + 2) % 3;
            kick_draw("Kick", sel, cnt);
        } else if (k & KEY_DOWN) {
            sel = (sel + 1) % 3;
            kick_draw("Kick", sel, cnt);
        } else if (k & (KEY_LEFT | KEY_RIGHT)) {
            if (sel == 1) {
                cnt += (k & KEY_RIGHT) ? 1 : -1;
                if (cnt < 2)
                    cnt = 2;
                if (cnt > 9)
                    cnt = 9;
                kick_draw("Kick", sel, cnt);
            }
        } else if (k & KEY_A) {
            done = 1;
        } else if (k & KEY_B) {
            cancelled = 1;
            done = 1;
        }
    }

    if (fb) {
        for (y = 0; y < KICK_ROWS * 8; y++)
            for (x = 0; x < TOP_COLS * 8; x++)
                fb[x * 240 + (239 - (KICK_TOP * 8 + y))]
                    = kick_saved[y][x];
    }
    consoleSelect(&topConsole);
    printf("\x1b[1;1H%s", COL_RESET);
    gfxFlushBuffers();
    gfxSwapBuffers();

    if (cancelled)
        return 0;
    *until_out = (sel == 2);
    *count_out = (sel == 1) ? cnt : 1;
    return 1;
}

/* 3dsui.c */

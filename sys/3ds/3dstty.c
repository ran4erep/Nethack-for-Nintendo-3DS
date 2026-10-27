/* NetHack 5.0	3dstty.c */
/* Copyright (c) Stichting Mathematisch Centrum, Amsterdam, 1985. */
/* NetHack may be freely redistributed.  See license for details. */

#include "hack.h"
#include "wintty.h"
#include <3ds.h>

char erase_char = '\b';
char kill_char = 21; /* Ctrl-U */

extern PrintConsole topConsole;
extern PrintConsole bottomConsole;

/* touch-screen message log (3dsart.c) */
extern void _3ds_log_add(const char *);

/* ---------------------------------------------------------------- */
/* console cursor tracking for the touch screen                     */
/*                                                                  */
/* The core keeps a single cursor position for the whole display    */
/* (ttyDisplay->curx/cury), but the status window is drawn on the   */
/* other screen now.  We therefore keep a second position for that  */
/* screen and swap them in and out around every status update, so   */
/* the relative cursor motion the core likes to use stays correct   */
/* on both screens.                                                 */
/* ---------------------------------------------------------------- */

static int bot_curx = 0, bot_cury = 0; /* 0-based, this screen */

/* Bottom screen line cursor (1-based ANSI). */
static void
bot_goto(int x, int y)
{
    printf("\x1b[%d;%dH", y + 1, x + 1);
}

/* Park this screen's cursor at the top left corner. */
static void
bot_home(void)
{
    consoleSelect(&bottomConsole);
    bot_goto(0, 0);
    bot_curx = bot_cury = 0;
    consoleSelect(&topConsole);
}

void
_3ds_status_console(int on)
{
    static int top_x = 0, top_y = 0;

    if (!ttyDisplay)
        return;
    if (on) {
        top_x = ttyDisplay->curx;
        top_y = ttyDisplay->cury;
        consoleSelect(&bottomConsole);
        ttyDisplay->curx = (short) bot_curx;
        ttyDisplay->cury = (short) bot_cury;
    } else {
        bot_curx = ttyDisplay->curx;
        bot_cury = ttyDisplay->cury;
        consoleSelect(&topConsole);
        ttyDisplay->curx = (short) top_x;
        ttyDisplay->cury = (short) top_y;
    }
}

/* width of the touch screen in characters; the core draws its status
   window here instead of on the top screen */
int
_3ds_bottom_cols(void)
{
    return 40;
}

#define KEY_BUF_SIZE 128
static char key_buf[KEY_BUF_SIZE];
static int key_buf_head = 0;
static int key_buf_tail = 0;

/* ------------------------------------------------------------------ */
/* input trace (debug aid)                                             */
/*                                                                     */
/* When the marker file sdmc:/3ds/nethack/config/debug.txt exists, every key  */
/* the input layer returns is appended to sdmc:/3ds/nethack/config/keytrace.log */
/* with the context that decides how it is interpreted (input state,    */
/* getpos cursor, message-line state).  Normal play is unaffected: no   */
/* marker file, no logging.                                             */
/* ------------------------------------------------------------------ */

static FILE *trace_fp = NULL;
static unsigned trace_seq = 0;
static boolean trace_checked = FALSE; /* marker file probe done once */

static void
trace_open(void)
{
    FILE *marker;

    if (trace_checked)
        return;
    trace_checked = TRUE;
    marker = fopen("sdmc:/3ds/nethack/config/debug.txt", "rb");
    if (marker) {
        fclose(marker);
        trace_fp = fopen("sdmc:/3ds/nethack/config/keytrace.log", "ab");
        if (trace_fp)
            fprintf(trace_fp, "--- trace start ---\n");
    }
}

static void
trace_key(int ret, const char *where)
{
    trace_open();
    if (!trace_fp)
        return;
    fprintf(trace_fp, "%u ret=0x%02x '%c' where=%s in_parse=%d input=%d",
            trace_seq++, (unsigned) ret & 0xff,
            (ret >= 32 && ret < 127) ? (char) ret : '.', where,
            iflags.in_parse ? 1 : 0, (int) program_state.input_state);
    if (ttyDisplay)
        fprintf(trace_fp, " getx=%d toplin=%d lastwin=%d",
                (int) gg.getposx, (int) ttyDisplay->toplin,
                (int) ttyDisplay->lastwin);
    fputc('\n', trace_fp);
    fflush(trace_fp);
}

void enable_ctrlP(void) {}
void disable_ctrlP(void) {}
void tty_utf8graphics_fixup(void) {}

void g_pututf8(uint8 *utf8str)
{
    while (*utf8str) {
        (void) putchar(*utf8str++);
    }
}

void chdrive(const char *str)
{
    (void) str;
}

void error(const char *s, ...)
{
    va_list ap;
    va_start(ap, s);
    vfprintf(stderr, s, ap);
    va_end(ap);
    fprintf(stderr, "\n");
    /* a fatal error must really terminate, not bounce back to the menu */
    _3ds_menu_loop = 0;
    nethack_exit(EXIT_FAILURE);
}

int strncmpi(const char *s1, const char *s2, int n)
{
    if (n < 0)
        return strcasecmp(s1, s2);
    return strncasecmp(s1, s2, (size_t) n);
}

int kbhit(void)
{
    if (key_buf_head != key_buf_tail)
        return 1;
    hidScanInput();
    return (hidKeysDown() != 0);
}

static void queue_char(char c)
{
    int next = (key_buf_tail + 1) % KEY_BUF_SIZE;
    if (next != key_buf_head) {
        key_buf[key_buf_tail] = c;
        key_buf_tail = next;
    }
}

void gettty(void)
{
    erase_char = '\b';
    kill_char = 21;
    iflags.cbreak = TRUE;
}

void settty(const char *s)
{
    term_end_screen();
    if (s)
        raw_print(s);
}

void setftty(void)
{
    term_start_screen();
}

void get_scr_size(void)
{
    CO = 50; /* 400 pixels / 8 px per char */
    LI = 30; /* 240 pixels / 8 px per char */
}

void flushout(void)
{
    (void) fflush(stdout);
    gfxFlushBuffers();
}

/* ---------------------------------------------------------------- */
/* soft keyboard state                                              */
/* ---------------------------------------------------------------- */

static int kbd_visible = 0; /* keyboard rolled out?             */
static int kbd_allowed = 0; /* may the player use it right now? */
static int kbd_shift = 0;   /* shift the next character         */
static int kbd_ctrl = 0;    /* control the next character       */
static int kbd_alt = 0;     /* ESC prefix the next character    */
static int kbd_caps = 0;    /* caps lock, which latches         */
static int kbd_hot = 0;     /* the quick command grid showing?  */
static int bot_dirty = 1;   /* touch screen needs its widgets back? */

/* ---------------------------------------------------------------- */
/* prompt state                                                     */
/*                                                                   */
/* The 3DS hides ordinary messages from the top line (the touch-screen */
/* log shows them), but questions and input prompts must stay visible. */
/* The tty prompt functions bracket their input with these calls so    */
/* the top-line suppression knows not to erase a live question.        */
/* ---------------------------------------------------------------- */

static int _3ds_prompt_depth = 0;

void
_3ds_prompt_enter(void)
{
    _3ds_prompt_depth++;
}

void
_3ds_prompt_leave(void)
{
    if (_3ds_prompt_depth > 0)
        _3ds_prompt_depth--;
}

boolean
_3ds_message_prompt_active(void)
{
    return _3ds_prompt_depth > 0;
}

/* Auto-open the soft keyboard for text input prompts. */
void
_3ds_kbd_open_for_input(void)
{
    kbd_visible = 1;
    kbd_hot = 0;
    bot_dirty = 1;
}

/* Something has painted over the whole touch screen: the menu artwork, the
   save/load splash, or a console clear.  The button and the rolled out
   keyboard are blitted straight into the framebuffer, so any of those leaves
   them gone until they are drawn again.  This flags the loss; tgetch() puts
   the widgets back before it starts polling for input. */
void
_3ds_bottom_ui_invalidate(void)
{
    bot_dirty = 1;
}

static void
kbd_redraw(void)
{
    _3ds_art_keyboard(kbd_visible, kbd_shift, kbd_ctrl, kbd_alt, kbd_caps,
                      kbd_hot);
    bot_home();
    _3ds_art_present();
    bot_dirty = 0;
}

/* Menus call this to take the keyboard out of play; tgetch() switches it
   back on while it is polling for input.  The menu is about to repaint the
   touch screen, so the widgets have to be redrawn once play resumes.
   Whether the player had the keyboard rolled out is the player's choice,
   not the menu's: remember it so that resuming the game brings the same
   keyboard (and its toggle button) straight back. */
void
_3ds_kbd_active(int on)
{
    kbd_allowed = on;
    if (!on)
        bot_dirty = 1;
}

/* whether the soft keyboard is currently rolled out (the menu uses this to
   decide if a touch belongs to a key rather than to the list) */
int
_3ds_kbd_visible(void)
{
    return kbd_visible;
}

/* Draw the keyboard onto the bottom framebuffer without presenting.  Menus
   manage their own frame and call this, then present once after all drawing,
   so the keyboard must not trigger its own present. */
void
_3ds_kbd_draw(void)
{
    _3ds_art_keyboard(kbd_visible, kbd_shift, kbd_ctrl, kbd_alt,
                      kbd_caps, kbd_hot);
    bot_home();
}

/* Flip the keyboard's rolled-out state (used by the menu, which draws its
   own frame and therefore must not go through _3ds_bottom_touch(), whose
   kbd_redraw() would present mid-frame). */
void
_3ds_kbd_toggle(void)
{
    kbd_visible = !kbd_visible;
    kbd_hot = 0;
    _3ds_kbd_draw();
}

/* the character a key produces when shift is latched */
static char
shift_char(char c)
{
    static const char plain[] = "1234567890-=[]\\;',./";
    static const char upper[] = "!@#$%^&*()_+{}|:\"<>?";
    const char *p;

    if (c >= 'a' && c <= 'z')
        return (char) toupper((unsigned char) c);
    if ((p = strchr(plain, c)) != NULL)
        return upper[p - plain];
    return c;
}

/* A tapped key: queue whatever it types and repaint the caps. */
static void
kbd_press(int kind, int ch)
{
    char out = 0;

    switch (kind) {
    case K3D_SHIFT:
        kbd_shift = !kbd_shift;
        break;
    case K3D_CAPS:
        kbd_caps = !kbd_caps;
        kbd_shift = 0; /* turning caps on drops a latched shift */
        break;
    case K3D_CTRL:
        kbd_ctrl = !kbd_ctrl;
        break;
    case K3D_ALT:
        kbd_alt = !kbd_alt;
        break;
    case K3D_ESC:
        queue_char('\033');
        break;
    case K3D_TAB:
        queue_char('\t');
        break;
    case K3D_BACK:
        queue_char('\b');
        break;
    case K3D_SPACE:
        queue_char(' ');
        break;
    case K3D_ENTER:
        queue_char('\n');
        break;
    default: {
        int letter = (ch >= 'a' && ch <= 'z');

        /* caps lock capitals letters only, and shift undoes it, the way a
           real board works; the other symbols follow shift alone */
        if ((letter && (kbd_shift ^ kbd_caps)) || (!letter && kbd_shift))
            out = shift_char((char) ch);
        else
            out = (char) ch;
        break;
    }
    }

    if (out) {
        if (kbd_ctrl) {
            /* control keys: Ctrl-A is 1, Ctrl-Z is 26 */
            char c = (char) toupper((unsigned char) out);

            queue_char((c >= 'A' && c <= 'Z') ? (char) (c - 'A' + 1) : out);
        } else if (kbd_alt) {
            /* NetHack represents Meta (Alt) as the key with its high bit
               set (M(c) == 0x80 | c).  Queue that single character instead
               of an ESC prefix: altmeta is off on this port, so an ESC
               would be read as cancel and the following letter alone would
               be executed (Alt-d would drop instead of dip). */
            queue_char((char) (0x80 | (unsigned char) out));
        } else {
            queue_char(out);
        }
    }

    /* the modifiers stick to exactly one key; caps lock is not one of them */
    if (kind != K3D_SHIFT && kind != K3D_CTRL && kind != K3D_ALT)
        kbd_shift = kbd_ctrl = kbd_alt = 0;

    /* a latch changes the letter on every cap, so they all get repainted */
    _3ds_art_kbd_tap(kbd_shift, kbd_ctrl, kbd_alt, kbd_caps);
    _3ds_art_present();
}

/* ---------------------------------------------------------------- */
/* touch handling                                                   */
/* ---------------------------------------------------------------- */

/* movement key for a direction, defined below */
static char dir_key_mode(int dx, int dy, int numpad);

int
_3ds_bottom_touch(u16 px, u16 py)
{
    int kind = 0, ch = 0, row = -1, idx = -1;
    int tb_act = 0;

    if (!kbd_allowed)
        return 0;

    if (_3ds_art_topbar_hit(px, py, &tb_act)) {
        if (tb_act == 1005) {
            kbd_visible = !kbd_visible;
            kbd_redraw();
            return 1;
        }
        switch (tb_act) {
        case 1: /* Character (Attributes) */
            queue_char(C('x'));
            return 1;
        case 2: /* Zap wand */
            queue_char('z');
            return 1;
        case 3: /* Cast spell */
            queue_char('Z');
            return 1;
        case 4: /* Fire */
            queue_char('f');
            return 1;
        case 5: /* Glance */
            queue_char(';');
            return 1;
        case 6: /* Throw */
            queue_char('t');
            return 1;
        case 7: /* Diag Up-Left */
            queue_char(dir_key_mode(-1, -1, iflags.num_pad));
            return 1;
        case 8: /* Diag Up-Right */
            queue_char(dir_key_mode(1, -1, iflags.num_pad));
            return 1;
        case 9: /* Diag Down-Left */
            queue_char(dir_key_mode(-1, 1, iflags.num_pad));
            return 1;
        case 10: /* Diag Down-Right */
            queue_char(dir_key_mode(1, 1, iflags.num_pad));
            return 1;
        default:
            return 1;
        }
    }
    if (kbd_visible) {
        if (kbd_hot) {
            int cmd = 0;

            /* the grid is tested first: it covers the letter keys */
            if (_3ds_art_hot_hit(px, py, &cmd)) {
                if (cmd) /* a slot with no command yet just swallows the tap */
                    queue_char((char) cmd);
            } else if (_3ds_art_kbd_hit(px, py, &row, &idx, &kind, &ch)
                       && kind == K3D_HOTBAR) {
                kbd_hot = 0;
                kbd_redraw();
            }
            return 1;
        }
        if (_3ds_art_kbd_hit(px, py, &row, &idx, &kind, &ch)) {
            if (kind == K3D_HOTBAR) {
                kbd_hot = 1;
                kbd_redraw();
            } else if (kind == K3D_DIAG) {
                /* a diagonal-movement button: queue the number-pad digit
                   for that diagonal.  Four buttons sit in the bottom row
                   to the right of the space bar, left to right: up-left,
                   up-right, down-left, down-right. */
                int g = idx - 3;
                int dx = (g % 2) ? 1 : -1;
                int dy = (g / 2) ? 1 : -1;

                queue_char(dir_key_mode(dx, dy, iflags.num_pad));
            } else {
                kbd_press(kind, ch);
            }
        }
        return 1;
    }
    return 0;
}

void
_3ds_init_bottom_ui(void)
{
    _3ds_art_keyboard(kbd_visible, kbd_shift, kbd_ctrl, kbd_alt, kbd_caps,
                      kbd_hot);
    bot_home();
    _3ds_art_present();
    bot_dirty = 0;
}

/*
 * A finger is still on a key.  Only characters (and delete) repeat; modifier
 * and mode keys must not, and neither must return, which would confirm the
 * same prompt over and over.
 */
int
_3ds_bottom_touch_hold(u16 px, u16 py)
{
    int row, idx, kind = 0, ch = 0;
    int tb_act = 0;

    if (!kbd_allowed)
        return 0;

    if (_3ds_art_topbar_hit(px, py, &tb_act)) {
        switch (tb_act) {
        case 7: /* Diag Up-Left */
            queue_char(dir_key_mode(-1, -1, iflags.num_pad));
            return 1;
        case 8: /* Diag Up-Right */
            queue_char(dir_key_mode(1, -1, iflags.num_pad));
            return 1;
        case 9: /* Diag Down-Left */
            queue_char(dir_key_mode(-1, 1, iflags.num_pad));
            return 1;
        case 10: /* Diag Down-Right */
            queue_char(dir_key_mode(1, 1, iflags.num_pad));
            return 1;
        default:
            return 0;
        }
    }

    if (!kbd_visible || kbd_hot)
        return 0;
    if (!_3ds_art_kbd_hit(px, py, &row, &idx, &kind, &ch))
        return 0;
    if (kind == K3D_CHAR || kind == K3D_BACK) {
        kbd_press(kind, ch);
        return 1;
    }
    if (kind == K3D_DIAG) {
        int g = idx - 3;
        int dx = (g % 2) ? 1 : -1;
        int dy = (g / 2) ? 1 : -1;
        queue_char(dir_key_mode(dx, dy, iflags.num_pad));
        return 1;
    }
    return 0;
}

void
_3ds_clear_all_consoles(void)
{
    /* Clear both the top console (map/messages) and bottom console (status).
       term_clear_screen() emits ANSI escape sequences to stdout, which go to
       the currently selected PrintConsole. */
    consoleSelect(&topConsole);
    term_clear_screen();
    consoleSelect(&bottomConsole);
    term_clear_screen();
    consoleSelect(&topConsole);
    /* the bottom console was wiped; the widgets have to be drawn again */
    bot_dirty = 1;
}

/* The movement key for a direction.  numpad selects digit movement keys;
   otherwise the traditional hjklyubn letters are used. */
static char
dir_key_mode(int dx, int dy, int numpad)
{
    static const char letters[3][3] = {
        { 'y', 'k', 'u' }, { 'h', '.', 'l' }, { 'b', 'j', 'n' }
    };
    static const char digits[3][3] = {
        { '7', '8', '9' }, { '4', '5', '6' }, { '1', '2', '3' }
    };

    if (dx < -1)
        dx = -1;
    if (dx > 1)
        dx = 1;
    if (dy < -1)
        dy = -1;
    if (dy > 1)
        dy = 1;
    return (numpad ? digits : letters)[dy + 1][dx + 1];
}

static char
dir_key(int dx, int dy)
{
    return dir_key_mode(dx, dy, iflags.num_pad);
}

/* Queue a movement.  Run is the G prefix, which works for both digit and
   letter movement keys (the uppercase letter / meta-digit alternatives do
   not, because altmeta is off by default). */
static void
queue_dir(int dx, int dy, int run, int numpad)
{
    char key = dir_key_mode(dx, dy, numpad);

    if (!key)
        return;
    if (run)
        queue_char('G');
    queue_char(key);
}

static int
pop_queued(void)
{
    if (key_buf_head != key_buf_tail) {
        char c = key_buf[key_buf_head];

        key_buf_head = (key_buf_head + 1) % KEY_BUF_SIZE;
        return (unsigned char) c;
    }
    return -1;
}

/* Return the next queued character, fixing the getpos Enter case.  The
   soft keyboard queues '\n' for its return key (and '\r' for legacy), but
   while a map cursor is active Enter must confirm the tile under the cursor
   the same way A does ('.').  Otherwise the queued Enter falls into
   getpos()'s quit handling and the naming attempt ends with "Done." and
   no name prompt. */
static int
pop_queued_input(void)
{
    int c = pop_queued();

    if (c >= 0 && (program_state.input_state == getposInp || gg.getposx > 0)
        && (c == '\n' || c == '\r'))
        c = '.';
    if (c >= 0)
        trace_key(c, "pop");
    return c;
}

/* Used by the y/n dialog, which polls the buttons itself: the player may
   also tap an answer on the touch keyboard. */
int
_3ds_take_queued_key(void)
{
    return pop_queued();
}

int tgetch(void)
{
    static int dpad_dx = 0, dpad_dy = 0; /* current held direction */
    static int dpad_stable = 0;           /* frames this combination has held */
    static int dpad_repeat = 0;           /* frames until the next repeat */
    static boolean dpad_armed = FALSE;    /* combination qualified to emit? */
    static int cpad_timer = 0, cpad_dx = 0, cpad_dy = 0;
    static int touch_timer = 0, touch_px = -100, touch_py = -100;
    static int y_timer = 0;

    /* First check if we have queued characters from keyboard or macros */
    if (key_buf_head != key_buf_tail)
        return pop_queued_input();

    /* Something repainted the touch screen while the game was busy (the
       loading splash of a save or load, the menu artwork, a console clear),
       which wipes the keyboard toggle button even though its hit box stays
       where it was.  Put the widgets back before polling for input. */
    if (bot_dirty)
        _3ds_init_bottom_ui();

    /* Flush any pending output to screen before waiting for input */
    gfxFlushBuffers();
    gfxSwapBuffers();

    /* Wait for user input */
    while (aptMainLoop()) {
        _3ds_kbd_active(1);

        /* A menu (the pause screen in particular) or a console clear can
           repaint the touch screen at any time, including from inside this
           loop.  Putting the widgets back only before the loop would miss
           the repaints that happen while we are already polling, so check
           every pass. */
        if (bot_dirty)
            _3ds_init_bottom_ui();

        /* Animate and expire hit flashes / spurting blood even while the
           game is waiting for a key. */
        _3ds_effects_tick();

        /* Keep the notification LED in sync with current health. */
        _3ds_led_tick();

        /* Release sound buffers whose effects have finished playing. */
        _3ds_snd_tick();

        gspWaitForVBlank();
        hidScanInput();

        u32 kDown = hidKeysDown();
        u32 kHeld = hidKeysHeld();

        boolean run = (kHeld & KEY_L) != 0;

        /* Face buttons and system buttons act once per press. */
        if (kDown & KEY_START) {
            /* pause menu; returns only when the player chooses to resume */
            _3ds_pause_menu();
            gfxFlushBuffers();
            gfxSwapBuffers();
            continue;
        }
        if (kDown & KEY_A) {
            /* While a look/glance/target cursor is active, A confirms the
               tile under the cursor (the same as '.').  This is checked
               before the --More-- pager so that pressing A while a map
               cursor is up can never fall through to Enter. */
            if (program_state.input_state == getposInp || gg.getposx > 0) {
                trace_key('.', "A-getpos");
                return '.';
            }
            /* While a direction prompt ("In what direction?") is up, A
               selects the hero's own tile (the same as '.'). */
            if (program_state.input_state == getdirInp) {
                trace_key('.', "A-getdir");
                return '.';
            }
            /* While a --More-- pager is waiting, A confirms it (space). */
            if (ttyDisplay && ttyDisplay->toplin == TOPLINE_NEED_MORE) {
                trace_key(' ', "A-more");
                return ' ';
            }
            /* On the map, A is the "what can I do here?" key: it opens a
               context menu for the hero's tile and the ring around it.
               Everywhere else (prompts, --More--) it keeps its usual
               accept/confirm meaning; getpos cursors and direction
               questions are handled by the branches above.

               Deciding this from ttyDisplay->lastwin does not work: any
               message (such as "You see a chest here." right after a step)
               leaves lastwin at WIN_MESSAGE, so A would fall through to
               Enter and the core reported "Unknown command '^J'."  The
               core's command parser sets iflags.in_parse for the duration
               of reading a hero command, which is exactly the case where A
               should open the menu; prompts and menus are read outside it. */
            if (iflags.in_parse && ttyDisplay
                && program_state.input_state != getdirInp
                && gg.getposx == 0) {
                trace_key(M('h'), "A-ctxmenu");
                return M('h');
            }
            trace_key('\n', "A-enter");
            return '\n';
        }
        if (kDown & KEY_B) {
            /* B on the map swaps the wielded and secondary weapons; every
               where else it keeps its cancel meaning.  Prompt/menu reads
               don't go through here (menus poll their own buttons), so the
               only contexts seen here are the hero-command read and the
               direction/object prompts, where ESC must stay a cancel. */
            if (iflags.in_parse
                && (!ttyDisplay || ttyDisplay->toplin != TOPLINE_NEED_MORE)) {
                trace_key('x', "B-swap");
                return 'x';
            }
            trace_key('\033', "B");
            return '\033';
        }
        if (kDown & KEY_X) {
            trace_key('i', "X");
            return 'i';
        }
        /* Y searches (like the letter 's'), and repeats while held. */
        if (kDown & KEY_Y) {
            y_timer = 16; /* initial delay ~260 ms */
            trace_key('s', "Y");
            return 's';
        }
        if (kHeld & KEY_Y) {
            if (y_timer == 0) {
                y_timer = 6; /* repeat rate ~100 ms */
                trace_key('s', "Y-held");
                return 's';
            }
            if (y_timer > 0)
                y_timer--;
        } else {
            y_timer = 0;
        }
        if (kDown & KEY_R) {
            /* R: kick (^D), replacing the old eat binding */
            trace_key('\004', "R-kick");
            return '\004';
        }
        if (kDown & KEY_SELECT) {
            trace_key('?', "SELECT");
            return '?';
        }

        /* Touch screen: keys repeat while a finger stays on them. */
        if (kHeld & KEY_TOUCH) {
            touchPosition touch;

            hidTouchRead(&touch);
            if (kDown & KEY_TOUCH) {
                touch_px = touch.px;
                touch_py = touch.py;
                touch_timer = 16; /* initial delay ~260 ms */
                (void) _3ds_bottom_touch(touch.px, touch.py);
            } else if (abs((int) touch.px - touch_px) > 8
                       || abs((int) touch.py - touch_py) > 8) {
                /* slid onto another key: treat it as a fresh press */
                touch_px = touch.px;
                touch_py = touch.py;
                touch_timer = 16;
                (void) _3ds_bottom_touch(touch.px, touch.py);
            } else if (touch_timer == 0) {
                touch_timer = 6; /* repeat rate ~100 ms */
                (void) _3ds_bottom_touch_hold(touch.px, touch.py);
            } else {
                touch_timer--;
            }
            if (key_buf_head != key_buf_tail)
                return pop_queued_input();
        } else {
            touch_timer = 0;
            touch_px = touch_py = -100;
        }

        /* Circle pad: pan the map view.  While a look/glance command is
           active it moves the cursor instead, so the two never fight. */
        {
            circlePosition cpos;
            int cdx = 0, cdy = 0;

            hidCircleRead(&cpos);
            /* cpos.dy is positive when the pad is pushed up, while screen
               rows grow downwards, so flip it into map direction space. */
            if (cpos.dx > 45 && cpos.dy > 45) {
                cdx = 1;
                cdy = -1;
            } else if (cpos.dx < -45 && cpos.dy > 45) {
                cdx = -1;
                cdy = -1;
            } else if (cpos.dx > 45 && cpos.dy < -45) {
                cdx = 1;
                cdy = 1;
            } else if (cpos.dx < -45 && cpos.dy < -45) {
                cdx = -1;
                cdy = 1;
            } else if (cpos.dy > 65) {
                cdy = -1;
            } else if (cpos.dy < -65) {
                cdy = 1;
            } else if (cpos.dx > 65) {
                cdx = 1;
            } else if (cpos.dx < -65) {
                cdx = -1;
            }

            if (cdx || cdy) {
                boolean cursor_mode = (gg.getposx > 0);
                /* A hero command (as opposed to a prompt or a menu) owns
                   the map, whatever ttyDisplay->lastwin happens to say; a
                   message on the top line must not stop the view from
                   scrolling. */
                boolean map_input = iflags.in_parse;
                boolean dir_input =
                    (program_state.input_state == getdirInp);

                if (cdx != cpad_dx || cdy != cpad_dy) {
                    cpad_dx = cdx;
                    cpad_dy = cdy;
                    cpad_timer = 12; /* initial delay ~200 ms */
                } else if (cpad_timer == 0) {
                    cpad_timer = 5; /* repeat rate ~80 ms */
                } else {
                    cpad_timer--;
                    cdx = cdy = 0;
                }
                if (cdx || cdy) {
                    if (cursor_mode)
                        return dir_key(cdx, cdy);
                    /* "In what direction?" prompts (kick, throw, zap, ...)
                       are not map input and are not a menu either.  Feed
                       them a direction key instead of ignoring the pad. */
                    if (dir_input)
                        return dir_key(cdx, cdy);
                    if (!map_input) {
                        /* a menu or prompt owns the input; leave the map
                           behind it alone */
                        cpad_timer = 0;
                        cpad_dx = cpad_dy = 0;
                    } else {
                        _3ds_scroll_view(cdx, cdy);
                        gfxFlushBuffers();
                        gfxSwapBuffers();
                        continue;
                    }
                }
            } else {
                cpad_timer = 0;
                cpad_dx = cpad_dy = 0;
            }
        }

        /* D-pad: the hero's movement, and it repeats while held.  A held
           combination must stay stable for a few frames before it is acted
           on, so two perpendicular keys landing one frame apart merge into a
           single diagonal move instead of two orthogonal steps, and a
           momentary glitch in the scan never produces a spurious extra step. */
        {
            int dx = 0, dy = 0;
            boolean emit = FALSE;
            boolean cursor_mode = (gg.getposx > 0);
            boolean map_input = iflags.in_parse;
            boolean dir_input =
                (program_state.input_state == getdirInp);

            if (kHeld & KEY_DUP)
                dy = -1;
            if (kHeld & KEY_DDOWN)
                dy = 1;
            if (kHeld & KEY_DLEFT)
                dx = -1;
            if (kHeld & KEY_DRIGHT)
                dx = 1;

            if (dx || dy) {
                if (dx == dpad_dx && dy == dpad_dy) {
                    if (dpad_stable < 1000)
                        dpad_stable++;
                } else {
                    /* the held combination changed (possibly a half-formed
                       diagonal): restart the stability count and wait for it
                       to settle before moving */
                    dpad_dx = dx;
                    dpad_dy = dy;
                    dpad_stable = 1;
                    dpad_armed = FALSE;
                    dpad_repeat = 0;
                }

                /* a combination must persist for 3 frames (~50 ms) before it
                   is accepted, so diagonals are detected reliably */
                if (!dpad_armed && dpad_stable >= 3) {
                    dpad_armed = TRUE;
                    emit = TRUE;
                    dpad_repeat = 16; /* initial delay ~260 ms */
                } else if (dpad_armed) {
                    if (dpad_repeat > 0) {
                        --dpad_repeat;
                    } else {
                        emit = TRUE;
                        dpad_repeat = 6; /* repeat rate ~100 ms */
                    }
                }

                if (emit) {
                    /* menus keep their vi-key navigation; on the map and at
                       a direction prompt use the movement key set the core
                       is bound to (number-pad digits on this port) */
                    queue_dir(dpad_dx, dpad_dy,
                              run && map_input && !cursor_mode,
                              (map_input || dir_input || cursor_mode)
                                  ? iflags.num_pad : 0);
                    if (key_buf_head != key_buf_tail)
                        return pop_queued_input();
                }
            } else {
                dpad_dx = dpad_dy = 0;
                dpad_stable = 0;
                dpad_armed = FALSE;
                dpad_repeat = 0;
            }
        }

        gfxFlushBuffers();
        gfxSwapBuffers();
    }

    nethack_exit(EXIT_SUCCESS);
    return '\033';
}

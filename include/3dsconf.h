/* NetHack 5.0	3dsconf.h */
/* Copyright (c) Stichting Mathematisch Centrum, Amsterdam, 1985. */
/* NetHack may be freely redistributed.  See license for details. */

#ifndef _3DSCONF_H
#define _3DSCONF_H

#define MICRO /* single-user handheld system */
#define CHDIR
#define NOCWD_ASSUMPTIONS
#define DLB
#define DLBFILE "nhdat"
#undef DLBFILE2

/* Sound effects through the Nintendo 3DS CSND service (sys/3ds/3dssnd.c).
   This hooks the port into NetHack's built-in soundlib framework so that
   Soundeffect()/SoundAchievement() and the port's own _3ds_sfx() calls all
   go through the same playback engine. */
#define SND_LIB_3DS

/* sndprocs.h normally derives SND_LIB_INTEGRATED from SND_LIB_3DS, but
   global.h pulls in optlist.h before any header includes sndprocs.h, so the
   'sounds' option would be compiled as "off by default" (and described as
   plain "use sounds").  Defining the shortcut here, next to the soundlib
   choice and before optlist.h is parsed, makes the option default to On. */
#define SND_LIB_INTEGRATED

#define NO_SIGNAL
#undef NO_TERMS
#define NO_TERMCAP_HEADERS
#define ANSI_DEFAULT
#define CLIPPING
#undef RANDOM
#define Rand() random()
#define SELF_RECOVER
#define STRNCMPI
int strncmpi(const char *, const char *, int);
int kbhit(void);
void chdrive(const char *);
void error(const char *, ...);

#ifdef HACKDIR
#undef HACKDIR
#endif
#define HACKDIR "sdmc:/3ds/nethack"

#define CONFIG_FILE "nethackrc"

#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>

#ifndef FCMASK
#define FCMASK 0666
#endif
#ifndef O_BINARY
#define O_BINARY 0
#endif

#ifndef PATHLEN
#define PATHLEN 256
#endif
#ifndef FILENAME
#define FILENAME 80
#endif

#ifndef MICRO_H
#include "micro.h"
#endif

#ifdef SYSCF
#undef SYSCF
#endif
#ifdef SYSCF_FILE
#undef SYSCF_FILE
#endif

/* Disable INSURANCE checkpoint saves on 3DS.
   save_currentstate() writes the full game state to SD card via
   unbuffered I/O which takes ~58 seconds on 3DS hardware, causing
   a hard freeze (Home button unresponsive) after every level change
   and at game start.  Normal Save (S command) still works fine.

   The underlying slowness (one SD card transaction per savefile field)
   is fixed in src/sfstruct.c by buffering structlevel file I/O in RAM;
   see _3ds_iobuf_*() below.  INSURANCE stays off anyway: its checkpoint
   save is redundant now that the ordinary level save is cheap. */
#ifdef INSURANCE
#undef INSURANCE
#endif


/* 3DS-specific function declarations */
void _3ds_init(void);
void _3ds_exit(void);
int tgetch(void);
void gettty(void);
void settty(const char *);
void setftty(void);
void get_scr_size(void);
void flushout(void);

/* 3DS graphical front end (sys/3ds/3dsui.c) */
extern int _3ds_menu_loop;
void _3ds_return_to_menu(void);
void _3ds_settings_load(void);
void _3ds_settings_save(void);
void _3ds_apply_settings(void);
int _3ds_camera_centered(void);
boolean _3ds_more_confirm(void);
int _3ds_tutorial_mode(void);
void _3ds_scroll_view(int, int);
int _3ds_main_menu(void);
void _3ds_options_screen(int);
void _3ds_pause_menu(void);
void _3ds_player_selection(void);
void _3ds_askname(void);
void _3ds_reconfigure_display(void);
void _3ds_loading_start(void);
void _3ds_loading(int);
void _3ds_status_panel(void);
void _3ds_status_conditions(char *, size_t);

/* 3DS tile renderer (sys/3ds/3dstiles.c) */
void _3ds_init_tiles(void);
void _3ds_set_tileset(int);
void _3ds_draw_icon(int, int, int, int);
void _3ds_hit_flash(int, int);
void _3ds_blood_spawn(int, int, int, int);
struct obj;
void _3ds_potion_shatter(struct obj *, int, int);
void _3ds_floating_text(int, int, const char *, unsigned short);
void _3ds_art_text_top(int, int, const char *, unsigned short);
void _3ds_effects_tick(void);
void _3ds_effects_discard(void);
void _3ds_map_view_offset(int *, int *);

/* 3DS graphical menus (sys/3ds/3dsui.c); they take over NHW_MENU windows.
   The second argument is a struct WinDesc * (kept as void * here because
   this header is included before the tty window header). */
int _3ds_menu(int, void *);
char _3ds_yn_dialog(const char *, const char *, char);

/* save/load progress, drawn with the same bar as the start-up splash
   (3dsui.c).  _3ds_io_tick() is fed by src/sfstruct.c with the number of
   bytes that actually reached (or came from) the SD card. */
#define _3DS_IO_READ  0
#define _3DS_IO_WRITE 1
#define _3DS_IO_BOTH  2
void _3ds_io_begin(const char *, int);
void _3ds_io_tick(unsigned long, int);
void _3ds_io_end(void);
void _3ds_io_abandon(void);

/* RAM buffering of structlevel save/level/bones files (sfstruct.c) */
void _3ds_iobuf_reset(int);

/* RAM-backed level files (3dslevelmem.c).  The linker routes write/read/
   lseek/close for these descriptors to memory instead of the SD card, so
   changing floors never touches storage. */
int _3ds_levelmem_create(int);
int _3ds_levelmem_open(int);
void _3ds_levelmem_delete(int);
boolean _3ds_levelmem_isfd(int);

/* Notification-LED health indicator (3dsled.c) */
void _3ds_led_init(void);
void _3ds_led_off(void);
void _3ds_led_tick(void);
void _3ds_led_exit(void);
int _3ds_led_enabled(void);

/* Sound effects through CSND (3dssnd.c).  _3ds_sfx() is the port's own
   trigger for gameplay sounds that have no soundlib seid; it honours
   iflags.sounds and deafness exactly like Soundeffect() does.
   _3ds_sfx_always() skips the deafness check, for the few player-perspective
   sounds (footsteps, level-up) that should play even while Deaf.  Both are
   no-ops when no sound pack was built in. */
void _3ds_sfx(const char *key, int volume);
void _3ds_sfx_always(const char *key, int volume);
/* Central hook for the "You hear..." message sounds (youhear.c); called from
   You_hear() in pline.c after the message is formatted. */
void _3ds_youhear_sfx(const char *msg);
/* Suppress the pickup jingle while a context-menu action (quaff) is picking
   its item up; the action's own sound is the feedback.  dopickup() clears
   the flag when the pickup command finishes. */
void _3ds_sfx_pickup_silent(int on);
int _3ds_sfx_pickup_is_silent(void);
void _3ds_snd_init(void);
void _3ds_snd_tick(void);
void _3ds_snd_exit(void);
/* Volumes are percentages (0..100): master scales everything, music and
   sfx scale their own group.  The music setting applies to the gameplay
   loop; the menu track always plays at the master level.
   _3ds_music_play() starts a looping track ("menu" or "game") and keeps
   it if it is already playing. */
void _3ds_snd_set_volumes(int master, int music, int sfx);
void _3ds_music_play(const char *name);
void _3ds_music_stop(void);

/* soft keyboard key kinds, shared with sys/3ds/3dstty.c */
#define K3D_CHAR  0
#define K3D_SHIFT 1
#define K3D_CTRL  2
#define K3D_ALT   3
#define K3D_BACK  4
#define K3D_SPACE 5
#define K3D_ENTER 6
#define K3D_ESC   7
#define K3D_TAB   8
#define K3D_CAPS  9
#define K3D_HOTBAR 10
#define K3D_DIAG   11 /* diagonal-movement button on the keyboard */

/* 3DS bottom screen: soft keyboard and touch handling (3dstty.c) */
int _3ds_bottom_touch(unsigned short, unsigned short);
int _3ds_bottom_touch_hold(unsigned short, unsigned short);
int _3ds_take_queued_key(void);
void _3ds_kbd_active(int);
int _3ds_kbd_visible(void);
void _3ds_kbd_draw(void);
void _3ds_kbd_toggle(void);
int _3ds_bottom_cols(void);
void _3ds_init_bottom_ui(void);
void _3ds_clear_all_consoles(void);
void _3ds_bottom_ui_invalidate(void);
/* override the title of the next menu window (item actions) */
void _3ds_menu_title_override(const char *);
int _3ds_input_text(const char *, const char *, char *, int);
void _3ds_prompt_enter(void);
void _3ds_prompt_leave(void);

/* 3DS bottom screen pixel renderer (3dsart.c) */
int _3ds_art_status_h(void);
void _3ds_art_top_status(const char *, const char *, int, int, unsigned short);
void _3ds_art_bottom_status(const char *, const char *, const char *);
void _3ds_art_bg(void);
void _3ds_art_present(void);
void _3ds_art_keyboard(int, int, int, int, int, int);
void _3ds_art_kbd_tap(int, int, int, int);
int _3ds_art_kbd_button_hit(int, int);
int _3ds_art_topbar_hit(int, int, int *);
int _3ds_art_kbd_hit(int, int, int *, int *, int *, int *);
int _3ds_art_hot_hit(int, int, int *);
void _3ds_art_menu(void);
int _3ds_art_menu_hit(int, int);

#endif /* _3DSCONF_H */

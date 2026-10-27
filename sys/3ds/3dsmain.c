/* NetHack 5.0	3dsmain.c */
/* Copyright (c) Stichting Mathematisch Centrum, Amsterdam, 1985. */
/* NetHack may be freely redistributed.  See license for details. */

#include "hack.h"
#include "3dsfont.h"
#include "3dsi18n.h"
#include <3ds.h>
#include <3ds/archive.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <setjmp.h>

/* Allocate 2MB stack for main thread (NetHack + Lua require generous stack) */
u32 __stacksize__ = 2 * 1024 * 1024;

PrintConsole topConsole;
PrintConsole bottomConsole;

extern void _3ds_init_bottom_ui(void);
extern void _3ds_init_tiles(void);
extern boolean pcmain(int, char **);

/* Set while NetHack is running so that nethack_exit() (sys/share/pcsys.c)
   can return to the front end's main menu instead of killing the process. */
int _3ds_menu_loop = 0;

/* Where a finished game jumps back to; see nethack_exit(). */
jmp_buf _3ds_menu_jb;

void
_3ds_return_to_menu(void)
{
    longjmp(_3ds_menu_jb, 1);
}

void _3ds_exit(void)
{
    _3ds_led_exit();
    _3ds_snd_exit();
    romfsExit();
    gfxExit();
}

static void _3ds_ensure_dir(const char *path)
{
    char tmp[256];
    char *p = NULL;
    size_t len;

    snprintf(tmp, sizeof(tmp), "%s", path);
    len = strlen(tmp);
    if (len == 0 || len >= sizeof(tmp)) return;
    if (tmp[len - 1] == '/')
        tmp[len - 1] = 0;

    /* Skip drive prefix if present (e.g. "sdmc:/") */
    char *start = strstr(tmp, ":/");
    if (start)
        p = start + 2;
    else if (tmp[0] == '/')
        p = tmp + 1;
    else
        p = tmp;

    while (*p) {
        if (*p == '/') {
            *p = 0;
            mkdir(tmp, 0777);
            *p = '/';
        }
        p++;
    }
    mkdir(tmp, 0777);
}

/* Copy a bundled file from RomFS to the SD card if not already present.
   subdir is the destination subdirectory under sdmc:/3ds/nethack/ (or
   "" for the top level); the RomFS source always sits at its root. */
static void _3ds_copy_from_romfs(const char *subdir, const char *name)
{
    char sdpath[256];
    char rompath[256];
    FILE *check;
    FILE *src;
    FILE *dst;

    if (subdir && *subdir)
        snprintf(sdpath, sizeof(sdpath), "sdmc:/3ds/nethack/%s/%s",
                 subdir, name);
    else
        snprintf(sdpath, sizeof(sdpath), "sdmc:/3ds/nethack/%s", name);
    snprintf(rompath, sizeof(rompath), "romfs:/%s", name);

    check = fopen(sdpath, "rb");
    if (check) {
        fclose(check);
        return;
    }

    src = fopen(rompath, "rb");
    if (!src)
        return;
    dst = fopen(sdpath, "wb");
    if (dst) {
        char buf[16384];
        size_t n;
        while ((n = fread(buf, 1, sizeof(buf), src)) > 0) {
            fwrite(buf, 1, n, dst);
        }
        fclose(dst);
    }
    fclose(src);
}

/* Remove every plain file in a directory.  Used for the transient level-file
   directory, which must be empty whenever a game starts (any file there is
   an orphan from an aborted session), and for the cleanup of legacy files. */
static void _3ds_wipe_dir(const char *dir)
{
    DIR *d;
    struct dirent *e;
    struct stat st;
    char path[512];

    d = opendir(dir);
    if (!d)
        return;
    while ((e = readdir(d)) != 0) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
            continue;
        snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
        if (stat(path, &st) == 0 && S_ISREG(st.st_mode))
            unlink(path);
    }
    closedir(d);
}

/* Remove unfinished bones temp files ("*.bn") from the bones directory.
   A fresh bones file is written under a temporary name and renamed once it
   is complete; any .bn left behind belongs to an aborted write. */
static void _3ds_wipe_bones_temp(void)
{
    DIR *d;
    struct dirent *e;
    struct stat st;
    char path[512];
    size_t len;

    d = opendir("sdmc:/3ds/nethack/bones");
    if (!d)
        return;
    while ((e = readdir(d)) != 0) {
        len = strlen(e->d_name);
        if (len < 4 || strcmp(e->d_name + len - 3, ".bn") != 0)
            continue;
        snprintf(path, sizeof(path), "sdmc:/3ds/nethack/bones/%s",
                 e->d_name);
        if (stat(path, &st) == 0 && S_ISREG(st.st_mode))
            unlink(path);
    }
    closedir(d);
}

/* Does this file name fit the lock/level-file scheme used by NetHack on
   the 3DS?  pcmain() builds gl.lock from the player name plus ".99" and
   regularize() turns the dot into an underscore, so the per-game files
   look like "Hero1_99.0", "Hero_99.7", and the bones temp file
   "Hero_99.bn".  Older builds of this port dropped all of those straight
   into the nethack root; they are stale (no game is running) and are
   swept away on startup. */
static boolean _3ds_is_stale_lockname(const char *name)
{
    int a = -1, b = -1, n = -1;

    if (sscanf(name, "%*[A-Za-z0-9]_%d.%d%n", &a, &b, &n) == 2
        && n == (int) strlen(name))
        return TRUE;
    n = -1;
    if (sscanf(name, "%*[A-Za-z0-9]_%d.bn%n", &a, &n) == 1
        && n == (int) strlen(name))
        return TRUE;
    return FALSE;
}

/* One-time migration: move files that previous versions of the port left in
   the nethack root into the subdirectory they now belong in.  If the
   destination already exists (fresh copy, or the user created one), the old
   root copy is simply discarded. */
static void _3ds_migrate_file(const char *src, const char *dst)
{
    FILE *in, *out;
    char buf[16384];
    size_t n;

    out = fopen(dst, "rb");
    if (out) {
        fclose(out);
        unlink(src); /* destination already exists; the root copy is moot */
        return;
    }
    in = fopen(src, "rb");
    if (!in)
        return;
    out = fopen(dst, "wb");
    if (!out) {
        fclose(in);
        return;
    }
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0)
        fwrite(buf, 1, n, out);
    fclose(out);
    fclose(in);
    unlink(src);
}

/* Sweep the top-level folder for files left behind by older builds (the
   lock/level files described above).  Everything NetHack needs at the top
   level is either a directory or nhdat, so nothing useful is touched. */
static void _3ds_cleanup_stale_files(void)
{
    DIR *d;
    struct dirent *e;
    struct stat st;
    char path[512];

    d = opendir("sdmc:/3ds/nethack");
    if (!d)
        return;
    while ((e = readdir(d)) != 0) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
            continue;
        if (!_3ds_is_stale_lockname(e->d_name))
            continue;
        snprintf(path, sizeof(path), "sdmc:/3ds/nethack/%s", e->d_name);
        if (stat(path, &st) == 0 && S_ISREG(st.st_mode))
            unlink(path);
    }
    closedir(d);
}

int main(int argc, char *argv[])
{
    /* Initialize 3DS subsystems */
    gfxInitDefault();
    /* Notification-LED health indicator; does nothing if the mcuHwc
       service is unavailable. */
    _3ds_led_init();
    /* Disable double buffering so text console writes directly to screen buffer
     * without alternating frame buffer flicker or glyph disappearance. */
    gfxSetDoubleBuffering(GFX_TOP, false);
    gfxSetDoubleBuffering(GFX_BOTTOM, false);
    consoleInit(GFX_TOP, &topConsole);
    consoleInit(GFX_BOTTOM, &bottomConsole);
    _3ds_font_init();
    _3ds_i18n_init();

    /* the logo stands in for a black screen while the card is read */
    _3ds_loading_start();

    archiveMountSdmc();
    romfsInit();
    _3ds_loading(4);

    /* Ensure directory structure on SD card using explicit sdmc: paths */
    _3ds_ensure_dir("sdmc:/3ds/nethack/save");
    _3ds_ensure_dir("sdmc:/3ds/nethack/bones");
    _3ds_ensure_dir("sdmc:/3ds/nethack/levels");
    /* optional: drop custom tiles.bmp / PixelHack.png here */
    _3ds_ensure_dir("sdmc:/3ds/nethack/tiles");
    _3ds_ensure_dir("sdmc:/3ds/nethack/config");
    /* optional: drop custom effect WAVs here, see sys/3ds/README.md */
    _3ds_ensure_dir("sdmc:/3ds/nethack/sounds");
    /* optional: drop a custom menu.wav here (overrides the built-in loop) */
    _3ds_ensure_dir("sdmc:/3ds/nethack/music");

    /* Change current working directory to NetHack home on SD card */
    if (chdir("sdmc:/3ds/nethack") != 0) {
        chdir("/3ds/nethack");
    }
    _3ds_loading(8);

    /* One-time migration of files that older builds kept in the root. */
    _3ds_migrate_file("sdmc:/3ds/nethack/NetHack.cnf",
                      "sdmc:/3ds/nethack/config/NetHack.cnf");
    _3ds_migrate_file("sdmc:/3ds/nethack/3dsui.cfg",
                      "sdmc:/3ds/nethack/config/3dsui.cfg");
    _3ds_migrate_file("sdmc:/3ds/nethack/tiles.bmp",
                      "sdmc:/3ds/nethack/tiles/tiles.bmp");
    _3ds_migrate_file("sdmc:/3ds/nethack/PixelHack.png",
                      "sdmc:/3ds/nethack/tiles/PixelHack.png");
    _3ds_migrate_file("sdmc:/3ds/nethack/record",
                      "sdmc:/3ds/nethack/config/record");

    /* The level-file directory is transient: anything in it belongs to a
       session that ended without cleaning up.  Wipe it, and also remove
       the legacy lock/level leftovers at the top level, so the nethack
       folder never turns into a dumping ground.  A small folder is also a
       fast folder: the 3DS FAT layer re-scans directory entries for every
       open/create/delete, so a root cluttered with hundreds of stale files
       makes the bursts of file I/O at character creation and game start
       take seconds. */
    _3ds_wipe_dir("sdmc:/3ds/nethack/levels");
    _3ds_wipe_bones_temp();
    _3ds_cleanup_stale_files();
    _3ds_loading(10);

    /* Copy bundled nhdat from RomFS to SD card when the SD copy is missing or
       out of date (size differs from the bundled archive). */
    {
        struct stat sd_st, rom_st;
        int sd_ok = (stat("sdmc:/3ds/nethack/nhdat", &sd_st) == 0);
        int rom_ok = (stat("romfs:/nhdat", &rom_st) == 0);

        if (!sd_ok || (rom_ok && sd_st.st_size != rom_st.st_size)) {
            FILE *src = fopen("romfs:/nhdat", "rb");
            if (src) {
                FILE *dst = fopen("sdmc:/3ds/nethack/nhdat", "wb");
                long total = rom_ok ? (long) rom_st.st_size : 0, done = 0;

                if (!dst)
                    dst = fopen("/3ds/nethack/nhdat", "wb");
                if (dst) {
                    char buf[16384];
                    size_t n;
                    while ((n = fread(buf, 1, sizeof(buf), src)) > 0) {
                        fwrite(buf, 1, n, dst);
                        done += (long) n;
                        if (total > 0)
                            _3ds_loading(8 + (int) (78 * done / total));
                    }
                    fclose(dst);
                }
                fclose(src);
            }
        }
    }
    _3ds_loading(88);

    /* Tiles, effects and music are read straight from RomFS.  The SD
       folders are only consulted when the player drops a replacement in
       them, so nothing large is unpacked on startup.  Only the sample
       config is copied, because it is meant to be edited. */
    _3ds_copy_from_romfs("config", "NetHack.cnf");
    _3ds_loading(94);

    /* Verify SD card write access */
    int test_fd = open("sdmc:/3ds/nethack/test.tmp", O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (test_fd >= 0) {
        close(test_fd);
        unlink("sdmc:/3ds/nethack/test.tmp");
    }

    /* Pre-load the graphical tileset (no-op if tiles are unavailable) */
    _3ds_init_tiles();

    /* Sound effects through CSND (no-op if the service is unavailable). */
    _3ds_snd_init();

    _3ds_settings_load();
    _3ds_loading(100);

    /* hold the finished bar for a moment; loading is quick once the data is
       on the card, and a splash that flashes past is worse than none */
    for (int i = 0; i < 30 && aptMainLoop(); i++)
        gspWaitForVBlank();

    /* longjmp target: a finished game (death / save / quit) returns here so
       the front end can show the main menu again. */
    setjmp(_3ds_menu_jb);

    for (;;) {
        if (!_3ds_main_menu())
            break; /* the player chose EXIT */

        /* the logo sees the player through the load, then the game takes
           the screens over */
        _3ds_init_bottom_ui();
        _3ds_loading_start();

        _3ds_menu_loop = 1;
        early_init(argc, argv);
        _3ds_loading(25);
        boolean resuming = pcmain(argc, argv);
        moveloop(resuming);
        /* reached only if the game ended without calling nethack_exit() */
        _3ds_menu_loop = 0;
    }

    /* EXIT from the front end's main menu (or the HOME button closing the
       app) must not go through nethack_exit(): there may never have been a
       game, so windowprocs is still empty and wait_synch() would call a NULL
       nhgetch pointer.  Even after a game, msexit() would print
       "Hit <Enter> to continue."; there is nothing to tear down here. */
    _3ds_exit();
    exit(EXIT_SUCCESS);
    return 0;
}

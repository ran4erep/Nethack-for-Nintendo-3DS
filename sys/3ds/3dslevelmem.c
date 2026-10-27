/* NetHack 5.0	3dslevelmem.c */
/* Copyright (c) Stichting Mathematisch Centrum, Amsterdam, 1985. */
/* NetHack may be freely redistributed.  See license for details. */

/*
 * Nintendo 3DS: RAM-backed level files.
 *
 * Level files are the freezes the player sees when descending or ascending.
 * Every time the hero leaves a dungeon level NetHack writes a level file and
 * reads it back on return, and on the 3DS the SD card is slow and its FAT
 * layer re-scans a directory on every open/create/delete.  The port already
 * batches those per-field writes (see b3_wbuf in sfstruct.c), but the data
 * still ends up on the card, which is the slow part.
 *
 * So we stop using the card for level files entirely.  Level data lives in
 * RAM for the whole session, and the only time it reaches the SD card is
 * inside the single save file when the player saves (which is how levels are
 * persisted anyway).  Changing floors never touches the card.
 *
 * The engine believes it is talking to a real file descriptor.  The linker
 * is asked (LDFLAGS --wrap=...) to route every reference to write/read/
 * lseek/close made by this program through the __wrap_* versions below.
 * Those serve a reserved, never-used descriptor range backed by memory and
 * pass every other descriptor straight through to the OS.  Level files are
 * handed one of these descriptors by create_levelfile()/open_levelfile() in
 * src/files.c instead of being opened on the card.
 */

#include "hack.h"

#ifndef NINTENDO_3DS
#error "3dslevelmem.c is only for the Nintendo 3DS port"
#endif

#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

/* Reserved descriptor range.  Real fds on the 3DS are tiny sequential
   integers; nothing the OS returns ever lands here, so collisions are
   impossible. */
#define LM_BASE 0x7F000000
#define LM_MAX  96

struct lm_file {
    boolean used;
    int ledger;
    unsigned char *data;
    size_t cap;
    size_t len;
    long pos;
};

static struct lm_file lm[LM_MAX];

/* The real OS calls, provided by the linker's --wrap mechanism. */
extern ssize_t __real_write(int, const void *, size_t);
extern ssize_t __real_read(int, void *, size_t);
extern off_t __real_lseek(int, off_t, int);
extern int __real_close(int);

static int
lm_isfd(int fd)
{
    return (fd >= LM_BASE && fd < LM_BASE + LM_MAX);
}

static struct lm_file *
lm_get(int fd)
{
    if (!lm_isfd(fd))
        return 0;
    return &lm[fd - LM_BASE];
}

/* Is fd one of the RAM-backed level descriptors? (used by bufon()) */
boolean
_3ds_levelmem_isfd(int fd)
{
    struct lm_file *m = lm_get(fd);

    return m && m->used;
}

/* Reset / (re)create the RAM copy for a ledger, mimicking O_TRUNC. */
int
_3ds_levelmem_create(int ledger)
{
    int i;
    struct lm_file *m = 0;

    for (i = 0; i < LM_MAX; i++) {
        if (lm[i].used && lm[i].ledger == ledger) {
            m = &lm[i];
            break;
        }
    }
    if (!m) {
        for (i = 0; i < LM_MAX; i++)
            if (!lm[i].used) {
                m = &lm[i];
                break;
            }
    }
    if (!m)
        return -1;
    if (!m->used) {
        m->used = TRUE;
        m->ledger = ledger;
        m->cap = 0;
        m->data = 0;
    }
    m->len = 0;
    m->pos = 0;
    return LM_BASE + (int) (m - lm);
}

/* Open an existing RAM copy for a ledger; -1 if it does not exist. */
int
_3ds_levelmem_open(int ledger)
{
    int i;

    for (i = 0; i < LM_MAX; i++)
        if (lm[i].used && lm[i].ledger == ledger) {
            lm[i].pos = 0;
            return LM_BASE + i;
        }
    return -1;
}

/* Drop the RAM copy for a ledger. */
void
_3ds_levelmem_delete(int ledger)
{
    int i;

    for (i = 0; i < LM_MAX; i++)
        if (lm[i].used && lm[i].ledger == ledger) {
            free(lm[i].data);
            memset(&lm[i], 0, sizeof lm[i]);
            return;
        }
}

ssize_t
__wrap_write(int fd, const void *buf, size_t count)
{
    struct lm_file *m = lm_get(fd);
    size_t need;

    if (!m || !m->used)
        return __real_write(fd, buf, count);

    need = (size_t) m->pos + count;
    if (need > m->cap) {
        size_t ncap = m->cap ? m->cap : 4096;
        unsigned char *nd;

        while (ncap < need)
            ncap <<= 1;
        nd = (unsigned char *) realloc(m->data, ncap);
        if (!nd)
            return -1;
        m->data = nd;
        m->cap = ncap;
    }
    memcpy(m->data + m->pos, buf, count);
    m->pos += (long) count;
    if (m->pos > (long) m->len)
        m->len = (size_t) m->pos;
    return (ssize_t) count;
}

ssize_t
__wrap_read(int fd, void *buf, size_t count)
{
    struct lm_file *m = lm_get(fd);
    size_t avail, n;

    if (!m || !m->used)
        return __real_read(fd, buf, count);

    if (m->pos >= (long) m->len)
        return 0;
    avail = m->len - (size_t) m->pos;
    n = count < avail ? count : avail;
    memcpy(buf, m->data + m->pos, n);
    m->pos += (long) n;
    return (ssize_t) n;
}

off_t
__wrap_lseek(int fd, off_t offset, int whence)
{
    struct lm_file *m = lm_get(fd);
    long np;

    if (!m || !m->used)
        return __real_lseek(fd, offset, whence);

    switch (whence) {
    case SEEK_SET:
        np = offset;
        break;
    case SEEK_CUR:
        np = m->pos + offset;
        break;
    case SEEK_END:
        np = (long) m->len + offset;
        break;
    default:
        return -1;
    }
    if (np < 0)
        return -1;
    m->pos = np;
    return np;
}

int
__wrap_close(int fd)
{
    struct lm_file *m = lm_get(fd);

    if (!m || !m->used)
        return __real_close(fd);

    free(m->data);
    memset(m, 0, sizeof *m);
    return 0;
}

/* NetHack 5.0	sfstruct.c	$NHDT-Date: 1781973066 2026/06/20 16:31:06 $  $NHDT-Branch: NetHack-5.0 $:$NHDT-Revision: 1.28 $ */
/* Copyright (c) Stichting Mathematisch Centrum, Amsterdam, 1985. */
/*-Copyright (c) Michael Allison, 2025. */
/* NetHack may be freely redistributed.  See license for details. */

#include "hack.h"
#include "sfprocs.h"

/* #define SFLOGGING */          /* debugging */

staticfn void sfstruct_read_error(void);

/* historical full struct savings */

#ifdef SAVEFILE_DEBUGGING
#if defined(__GNUC__)
#define DEBUGFORMATSTR64 "%s %s %ld %ld %d\n"
#elif defined(_MSC_VER)
#define DEBUGFORMATSTR64 "%s %s %lld %ld %d\n"
#endif
#endif

#ifdef SFLOGGING
staticfn void logging_finish(void);
#endif

#define SFO_BODY(dt) \
{                                                                                   \
    bwrite(nhfp->fd, (genericptr_t) d_##dt, sizeof *d_##dt);                        \
}

#define SFI_BODY(dt) \
{                                                                                   \
    if (nhfp->eof) {                                                                \
       sfstruct_read_error();                                                       \
    }                                                                               \
    mread(nhfp->fd, (genericptr_t) d_##dt, sizeof *d_##dt);                         \
    if (restoreinfo.mread_flags == -1)                                              \
        nhfp->eof = TRUE;                                                           \
}

#define SF_A(dtyp) \
void historical_sfo_##dtyp(NHFILE *, dtyp *d_##dtyp, const char *);                 \
void historical_sfi_##dtyp(NHFILE *, dtyp *d_##dtyp, const char *);                 \
                                                                                    \
void historical_sfo_##dtyp(NHFILE *nhfp, dtyp *d_##dtyp,                            \
                           const char *myname UNUSED)                               \
    SFO_BODY(dtyp)                                                                  \
                                                                                    \
void historical_sfi_##dtyp(NHFILE *nhfp, dtyp *d_##dtyp,                            \
                  const char *myname UNUSED)                                        \
    SFI_BODY(dtyp)

#define SFO_CBODY(dt)                                                               \
    {                                                                               \
        norm_ptrs_##dt(d_##dt);                                            \
        bwrite(nhfp->fd, (genericptr_t) d_##dt, sizeof *d_##dt);                    \
    }

#define SFI_CBODY(dt)                                                               \
    {                                                                               \
        if (nhfp->eof) {                                                            \
            sfstruct_read_error();                                                  \
        }                                                                           \
        mread(nhfp->fd, (genericptr_t) d_##dt, sizeof *d_##dt);                     \
        norm_ptrs_##dt(d_##dt);                                            \
        if (restoreinfo.mread_flags == -1)                                          \
            nhfp->eof = TRUE;                                                       \
    }

#define SF_C(keyw, dtyp) \
void historical_sfo_##dtyp(NHFILE *, keyw dtyp *d_##dtyp,                           \
                           const char *);                                           \
void historical_sfi_##dtyp(NHFILE *, keyw dtyp *d_##dtyp,                           \
                           const char *);                                           \
extern void norm_ptrs_##dtyp(keyw dtyp *d_##dtyp);                         \
                                                                                    \
void historical_sfo_##dtyp(NHFILE *nhfp, keyw dtyp *d_##dtyp,                       \
                           const char *myname UNUSED)                               \
    SFO_CBODY(dtyp)                                                                 \
                                                                                    \
void historical_sfi_##dtyp(NHFILE *nhfp, keyw dtyp *d_##dtyp,                       \
                           const char *myname UNUSED)                               \
    SFI_CBODY(dtyp)

#define SF_X(xxx, dtyp) \
void historical_sfo_##dtyp(NHFILE *, xxx *d_##dtyp, const char *, int);             \
void historical_sfi_##dtyp(NHFILE *, xxx *d_##dtyp, const char *, int);             \
                                                                                    \
void historical_sfo_##dtyp(NHFILE *nhfp, xxx *d_##dtyp,                             \
                           const char *myname UNUSED, int bflen UNUSED)             \
    SFO_BODY(dtyp)                                                                  \
                                                                                    \
void historical_sfi_##dtyp(NHFILE *nhfp, xxx *d_##dtyp,                             \
                           const char *myname UNUSED, int bflen UNUSED)             \
    SFI_BODY(dtyp)


#include "sfmacros.h"
SF_C(struct, version_info)

void historical_sfo_char(NHFILE *, char *d_char, const char *, int);
void historical_sfi_char(NHFILE *, char *d_char, const char *, int);

void
historical_sfo_char(NHFILE *nhfp, char *d_char,
                    const char *myname UNUSED, int cnt)
{
    bwrite(nhfp->fd, (genericptr_t) d_char, cnt * sizeof (char));
}

void
historical_sfi_char(NHFILE *nhfp, char *d_char,
                    const char *myname UNUSED, int cnt)
{
    mread(nhfp->fd, (genericptr_t) d_char, cnt * sizeof (char));
    if (restoreinfo.mread_flags == -1)
        nhfp->eof = TRUE;
}
//extern void sfo_genericptr(NHFILE *, void **, const char *);
//extern void sfi_genericptr(NHFILE *, void **, const char *);
//extern void sfo_x_genericptr(NHFILE *, void **, const char *);
//extern void sfi_x_genericptr(NHFILE *, void **, const char *);

void historical_sfo_genericptr_t(NHFILE *, genericptr_t *d_genericptr_t,
                                 const char *);
void historical_sfi_genericptr_t(NHFILE *, genericptr_t *d_genericptr_t,
                                 const char *);
void
historical_sfo_genericptr_t(NHFILE *nhfp, genericptr_t *d_genericptr_t,
                            const char *myname UNUSED)
{
    bwrite(nhfp->fd, (genericptr_t) d_genericptr_t, sizeof *d_genericptr_t);
}
void
historical_sfi_genericptr_t(NHFILE *nhfp, genericptr_t *d_genericptr_t,
                            const char *myname UNUSED)
{
    if (nhfp->eof) {
        sfstruct_read_error();
    }
    mread(nhfp->fd, (genericptr_t) d_genericptr_t, sizeof *d_genericptr_t);
    if (restoreinfo.mread_flags == -1)
        nhfp->eof = TRUE;
}

SF_X(uint8_t, bitfield)

struct sf_structlevel_procs historical_sfo_procs = {
    "",
    /* sf */
    {
        historical_sfo_arti_info,
        historical_sfo_nhrect,
        historical_sfo_branch,
        historical_sfo_bubble,
        historical_sfo_cemetery,
        historical_sfo_context_info,
        historical_sfo_nhcoord,
        historical_sfo_damage,
        historical_sfo_dest_area,
        historical_sfo_dgn_topology,
        historical_sfo_dungeon,
        historical_sfo_d_level,
        historical_sfo_ebones,
        historical_sfo_edog,
        historical_sfo_egd,
        historical_sfo_emin,
        historical_sfo_engr,
        historical_sfo_epri,
        historical_sfo_eshk,
        historical_sfo_fe,
        historical_sfo_flag,
        historical_sfo_fruit,
        historical_sfo_gamelog_line,
        historical_sfo_kinfo,
        historical_sfo_levelflags,
        historical_sfo_ls_t,
        historical_sfo_linfo,
        historical_sfo_mapseen_feat,
        historical_sfo_mapseen_flags,
        historical_sfo_mapseen_rooms,
        historical_sfo_mkroom,
        historical_sfo_monst,
        historical_sfo_mvitals,
        historical_sfo_obj,
        historical_sfo_objclass,
        historical_sfo_q_score,
        historical_sfo_rm,
        historical_sfo_spell,
        historical_sfo_stairway,
        historical_sfo_s_level,
        historical_sfo_trap,
        historical_sfo_version_info,
        historical_sfo_you,
        historical_sfo_any,
        historical_sfo_aligntyp,
        historical_sfo_boolean,
        historical_sfo_coordxy,
        historical_sfo_genericptr_t,
        historical_sfo_int,
        historical_sfo_int16,
        historical_sfo_int32,
        historical_sfo_int64,
        historical_sfo_long,
        historical_sfo_schar,
        historical_sfo_short,
        historical_sfo_size_t,
        historical_sfo_time_t,
        historical_sfo_uchar,
        historical_sfo_uint16,
        historical_sfo_uint32,
        historical_sfo_uint64,
        historical_sfo_ulong,
        historical_sfo_unsigned,
        historical_sfo_ushort,
        historical_sfo_xint16,
        historical_sfo_xint8,
        historical_sfo_char,
        historical_sfo_bitfield,
#ifdef DEMO_UPLIFTS
        historical_sfo_mystruct,
        historical_sfo_mystruct_rev0,
#endif
    }
};

struct sf_structlevel_procs historical_sfi_procs = {
    "",
    /* sf */
    {
        historical_sfi_arti_info,
        historical_sfi_nhrect,
        historical_sfi_branch,
        historical_sfi_bubble,
        historical_sfi_cemetery,
        historical_sfi_context_info,
        historical_sfi_nhcoord,
        historical_sfi_damage,
        historical_sfi_dest_area,
        historical_sfi_dgn_topology,
        historical_sfi_dungeon,
        historical_sfi_d_level,
        historical_sfi_ebones,
        historical_sfi_edog,
        historical_sfi_egd,
        historical_sfi_emin,
        historical_sfi_engr,
        historical_sfi_epri,
        historical_sfi_eshk,
        historical_sfi_fe,
        historical_sfi_flag,
        historical_sfi_fruit,
        historical_sfi_gamelog_line,
        historical_sfi_kinfo,
        historical_sfi_levelflags,
        historical_sfi_ls_t,
        historical_sfi_linfo,
        historical_sfi_mapseen_feat,
        historical_sfi_mapseen_flags,
        historical_sfi_mapseen_rooms,
        historical_sfi_mkroom,
        historical_sfi_monst,
        historical_sfi_mvitals,
        historical_sfi_obj,
        historical_sfi_objclass,
        historical_sfi_q_score,
        historical_sfi_rm,
        historical_sfi_spell,
        historical_sfi_stairway,
        historical_sfi_s_level,
        historical_sfi_trap,
        historical_sfi_version_info,
        historical_sfi_you,
        historical_sfi_any,

        historical_sfi_aligntyp,
        historical_sfi_boolean,
        historical_sfi_coordxy,
        historical_sfi_genericptr_t,
        historical_sfi_int,
        historical_sfi_int16,
        historical_sfi_int32,
        historical_sfi_int64,
        historical_sfi_long,
        historical_sfi_schar,
        historical_sfi_short,
        historical_sfi_size_t,
        historical_sfi_time_t,
        historical_sfi_uchar,
        historical_sfi_uint16,
        historical_sfi_uint32,
        historical_sfi_uint64,
        historical_sfi_ulong,
        historical_sfi_unsigned,
        historical_sfi_ushort,
        historical_sfi_xint16,
        historical_sfi_xint8,
        historical_sfi_char,
        historical_sfi_bitfield,
#ifdef DEMO_UPLIFTS
        historical_sfi_mystruct,
        historical_sfi_mystruct_rev0,
#endif
    }
};

/*
 * The historical bwrite() and mread() functions follow
 */

#ifdef SAVEFILE_DEBUGGING
static long floc = 0L;
#endif

/*
 * historical structlevel savefile writing and reading routines follow.
 * These were moved here from save.c and restore.c between 3.6.3 and 5.0.0,.
 */

staticfn int getidx(int, int);

#if defined(UNIX) || defined(WIN32) || defined(SFSTRUCT_BUFFERING)
#define USE_BUFFERING
#endif

struct restore_info restoreinfo = {
    "externalcomp", 0,
};

#define MAXFD 5
#ifdef SFSTRUCT_BUFFERING
#define BWBUFSZ 16384
#endif
enum {NOFLG = 0, NOSLOT = 1};
static int bw_sticky[MAXFD] = {-1,-1,-1,-1,-1};
static int bw_buffered[MAXFD] = {0,0,0,0,0};
#ifdef USE_BUFFERING
static FILE *bw_FILE[MAXFD] = {0,0,0,0,0};
#endif

#ifdef NINTENDO_3DS
/*
 * Nintendo 3DS: the SD card is pathologically slow when a save file is
 * written or read one field at a time.  A field is typically 1-8 bytes and
 * a savefile or level file holds tens of thousands of them, so leaving the
 * card do one transaction per field made a level save take the better part
 * of a minute and a game save take several.  Buffer both directions in RAM
 * and hand the card large blocks instead.
 *
 * Writes honour the bufon()/bufoff() protocol (bflush() pushes the buffer
 * out, so bufoff() and bclose() keep the "plain write()" passages that the
 * save code depends on in the right order).  Every place that writes a
 * sizeable amount of structlevel data calls store_version() or bufon()
 * first -- savegame, level files, bones files and restlevelfile() -- so all
 * of the bulk traffic ends up buffered.
 *
 * The read-ahead buffer is not trusted blindly: each read asks the file
 * where it is (lseek(SEEK_CUR) is served from the devoptab's in-memory
 * offset, so it is free).  While a block is buffered the file itself sits at
 * the end of that block -- the refill hands the whole thing to one
 * FSFILE_Read, which moves the offset by however much it returned -- and
 * that is the condition under which the block may still be used.  Reading a
 * different file in the meantime moves the file we were reading back to the
 * first byte never handed out, so nothing is skipped.  A rewind or a close
 * is reported through _3ds_iobuf_reset().
 */
#define B3_WBUFSZ 16384
#define B3_RBUFSZ 32768

static unsigned char b3_wbuf[MAXFD][B3_WBUFSZ];
static unsigned b3_wlen[MAXFD] = { 0, 0, 0, 0, 0 };

static int b3_rfd = -1;             /* fd the read-ahead buffer belongs to */
static unsigned char b3_rbuf[B3_RBUFSZ];
static unsigned b3_rpos = 0, b3_rlen = 0;
static long b3_rbase = 0;           /* file offset of b3_rbuf[0] */

/* Forget any read-ahead data cached for fd (called when a file is rewound
   or closed).  Nothing depends on this being called: every read verifies
   the file offset before trusting the buffer. */
void
_3ds_iobuf_reset(int fd)
{
    if (fd == b3_rfd) {
        b3_rfd = -1;
        b3_rpos = b3_rlen = 0;
    }
}

/* push one file's write buffer out to the card */
staticfn void
b3_wflush(int idx)
{
    unsigned n;

    if (idx < 0)
        return;
    n = b3_wlen[idx];
    if (n > 0) {
        b3_wlen[idx] = 0;
        if ((long) write(bw_sticky[idx], (genericptr_t) b3_wbuf[idx],
                         (int) n) != (long) n)
            panic("flush of savefile failed!");
        _3ds_io_tick((unsigned long) n, 1);
    }
}

/* read len bytes, serving them from a read-ahead buffer filled with large
   card reads; returns how many bytes were actually obtained */
staticfn unsigned
b3_rread(int fd, unsigned char *dst, unsigned len)
{
    unsigned got = 0;
    long cur;

    /* The devoptab for sdmc keeps the file offset in RAM, so this costs
       nothing.  A buffered block is in step with the file only while the
       file sits at the end of it; anything else means the position has been
       moved behind our back and the block can no longer be trusted. */
    cur = (long) lseek(fd, (off_t) 0, SEEK_CUR);
    if (fd != b3_rfd || cur < 0L || cur != b3_rbase + (long) b3_rlen) {
        if (b3_rfd >= 0 && b3_rfd != fd) {
            /* another file is being read now.  Put the one whose block we
               are dropping back to the first byte never handed out, so the
               page it had already told the card to hand over is not lost. */
            (void) lseek(b3_rfd, (off_t) (b3_rbase + (long) b3_rpos),
                         SEEK_SET);
        }
        b3_rfd = -1;
        b3_rpos = b3_rlen = 0;
    }

    while (got < len) {
        unsigned avail = (fd == b3_rfd) ? (b3_rlen - b3_rpos) : 0;
        long r;

        if (avail == 0) {
            if (len - got >= B3_RBUFSZ) {
                /* a request larger than the buffer; read it straight */
                b3_rfd = -1;
                b3_rpos = b3_rlen = 0;
                r = (long) read(fd, (genericptr_t) (dst + got),
                                (size_t) (len - got));
                if (r <= 0)
                    break;
                _3ds_io_tick((unsigned long) r, 0);
                got += (unsigned) r;
                continue;
            }
            b3_rbase = (long) lseek(fd, (off_t) 0, SEEK_CUR);
            if (b3_rbase < 0L)
                b3_rbase = 0L;
            r = (long) read(fd, (genericptr_t) b3_rbuf, (size_t) B3_RBUFSZ);
            b3_rfd = fd;
            b3_rpos = 0;
            b3_rlen = (r > 0) ? (unsigned) r : 0;
            avail = b3_rlen;
            if (avail == 0) {
                b3_rfd = -1;
                break;          /* EOF or error; mread() reports it */
            }
            _3ds_io_tick((unsigned long) avail, 0);
        }
        if (avail > len - got)
            avail = len - got;
        (void) memcpy(dst + got, b3_rbuf + b3_rpos, (size_t) avail);
        b3_rpos += avail;
        got += avail;
    }
    return got;
}
#endif /* NINTENDO_3DS */

#ifdef SFLOGGING
static FILE *ofp[20] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                         0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    char ofnamebuf[80];
    static long ocnt = 0L;

    static FILE *ifp[20] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                             0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    char ifnamebuf[80];
    static long icnt = 0L;
#endif


/*
 * Presumably, the fdopen() to allow use of stdio fwrite()
 * over write() was done for performance or functionality
 * reasons to help some particular platform long ago.
 *
 * There have been some issues being encountered with the
 * implementation due to having an individual set of
 * tracking variables, even though there were nested
 * sets of open fd (like INSURANCE).
 *
 * This uses an individual tracking entry for each fd
 * being used.
 *
 * Some notes:
 *
 * Once buffered IO (stdio) has been enabled on the file
 * associated with a descriptor via fdopen():
 *
 *    1. If you use bufoff and bufon to try to toggle the
 *       use of write vs fwrite; the code just tracks which
 *       routine is to be called through the tracking
 *       variables and acts accordingly.
 *             bw_sticky[]    -  used to find the index number for
 *                               the fd that is stored in it, or -1
 *                               if it is a free slot.
 *             bw_buffered[]  -  indicator that buffered IO routines
 *                               are available for use.
 *             bw_FILE[]      -  the non-zero FILE * for use in calling
 *                               fwrite() when bw_buffered[] is also
 *                               non-zero.
 *
 *    2. It is illegal to call close(fd) after fdopen(), you
 *       must always use fclose() on the FILE * from
 *       that point on, so care must be taken to never call
 *       close(fd) on the underlying fd or bad things will
 *       happen.
 */

staticfn int
getidx(int fd, int flg)
{
    int i, retval = -1;

    for (i = 0; i < MAXFD; ++i)
        if (bw_sticky[i] == fd)
            return i;
    if (flg == NOSLOT)
        return retval;
    for (i = 0; i < MAXFD; ++i)
        if (bw_sticky[i] < 0) {
            bw_sticky[i] = fd;
            retval = i;
            break;
        }
    return retval;
}

/* Let caller know that bclose() should handle it (TRUE) */
boolean
close_check(int fd)
{
    int idx = getidx(fd, NOSLOT);
    boolean retval = FALSE;

    if (idx >= 0)
        retval = TRUE;
    return retval;
}

void
bufon(int fd)
{
    int idx = getidx(fd, NOFLG);

    if (idx >= 0) {
        bw_sticky[idx] = fd;
#ifdef NINTENDO_3DS
        /* RAM-backed level descriptors have no OS file behind them, so the
           stdio buffering fdopen() would try to attach is meaningless.
           Mark them buffered and leave bw_FILE[] NULL. */
        if (_3ds_levelmem_isfd(fd)) {
            bw_buffered[idx] = 1;
            return;
        }
#endif
#ifdef USE_BUFFERING
        if (bw_buffered[idx])
            panic("buffering already enabled");
        if (!bw_FILE[idx]) {
            /* "b": MiNTlib defaults stdio to text mode (LF -> CRLF) */
            if ((bw_FILE[idx] = fdopen(fd, "wb")) == 0)
                panic("buffering of file %d failed", fd);
#ifdef SFSTRUCT_BUFFERING
            (void) setvbuf(bw_FILE[idx], (char *) 0, _IOFBF,
                           (size_t) BWBUFSZ);
#endif
        }
        bw_buffered[idx] = (bw_FILE[idx] != 0);
#else
        bw_buffered[idx] = 1;
#endif
    }
}

void
bufoff(int fd)
{
    int idx = getidx(fd, NOFLG);

    if (idx >= 0) {
        bflush(fd);
        bw_buffered[idx] = 0;     /* just a flag that says "use write(fd)" */
    }
}

void
bclose(int fd)
{
    int idx = getidx(fd, NOSLOT);

    bufoff(fd);     /* sets bw_buffered[idx] = 0 */
    if (idx >= 0) {
#ifdef USE_BUFFERING
        if (bw_FILE[idx]) {
            (void) fclose(bw_FILE[idx]);
            bw_FILE[idx] = 0;
        } else
#endif
            close(fd);
        /* return the idx to the pool */
        bw_sticky[idx] = -1;
    }
#ifdef SFLOGGING
    if (fd >= 0 && fd <= SIZE(ofp)) {
        if (ofp[fd]) {
            fclose(ofp[fd]);
            ofp[fd] = 0;
        } else if (ifp[fd]) {
            fclose(ifp[fd]);
            ifp[fd] = 0;
        }
    }
#endif
    return;
}

void
bflush(int fd)
{
    int idx = getidx(fd, NOFLG);

    if (idx >= 0) {
#ifdef NINTENDO_3DS
        b3_wflush(idx);
#endif
#ifdef USE_BUFFERING
        if (bw_FILE[idx]) {
           if (fflush(bw_FILE[idx]) == EOF)
               panic("flush of savefile failed!");
        }
#endif
    }
    return;
}

void
bwrite(int fd, const genericptr_t loc, unsigned num)
{
    boolean failed;
    int idx = getidx(fd, NOFLG);

#ifdef SFLOGGING
    if (fd >= 0 && fd < SIZE(ofp)) {
        if (!ofp[fd]) {
            Snprintf(ofnamebuf, sizeof ofnamebuf, "bwrite_%02d.log", fd);
            ofp[fd] = fopen(ofnamebuf, "w");
        }
        if (ofp[fd]) {
            fprintf(ofp[fd], "%08ld, %08ld, %d\n", ocnt,
                    ftell(ofp[fd]), num);
            ocnt++;
        }
    }
#endif

    if (idx >= 0) {
        if (num == 0) {
            /* nothing to do; we need a special case to exit early
               because glibc fwrite doesn't give reliable
               success/failure indication when writing 0 bytes */
            return;
        }

#ifdef NINTENDO_3DS
        if (bw_buffered[idx]) {
            if (num > B3_WBUFSZ) {
                /* bigger than the buffer; push what we have and hand
                   this one straight to the card */
                b3_wflush(idx);
                failed = ((long) write(fd, loc, (int) num) != (long) num);
                if (!failed)
                    _3ds_io_tick((unsigned long) num, 1);
            } else {
                if (num > B3_WBUFSZ - b3_wlen[idx])
                    b3_wflush(idx);
                (void) memcpy(&b3_wbuf[idx][b3_wlen[idx]], loc, (size_t) num);
                b3_wlen[idx] += num;
                failed = FALSE;
            }
        } else
#endif
#ifdef USE_BUFFERING
        if (bw_buffered[idx] && bw_FILE[idx]) {
            failed = (fwrite(loc, (int) num, 1, bw_FILE[idx]) != 1);
        } else
#endif /* UNIX */
        {
            /* lint wants 3rd arg of write to be an int; lint -p an unsigned */
#if defined(BSD) || defined(ULTRIX) || defined(WIN32) || defined(_MSC_VER)
            failed = ((long) write(fd, loc, (int) num) != (long) num);
#else /* e.g. SYSV, __TURBOC__ */
            failed = ((long) write(fd, loc, num) != (long) num);
#endif
        }
        if (failed) {
#if defined(HANGUPHANDLING)
            if (program_state.done_hup)
                nh_terminate(EXIT_FAILURE);
            else
#endif
                panic("cannot write %u bytes to file #%d", num, fd);
        }
    } else
        impossible("fd not in list (%d)?", fd);
}

/*  ===================================================== */

void
mread(int fd, genericptr_t buf, unsigned len)
{

#ifdef SFLOGGING
    if (fd >= 0 && fd < 9) {
        if (!ifp[fd]) {
            Snprintf(ifnamebuf, sizeof ifnamebuf, "mread_%02d.log", fd);
            ifp[fd] = fopen(ifnamebuf, "w");
        }
        if (ifp[fd]) {
            fprintf(ifp[fd], "%08ld, %08ld, %d\n", icnt,
                    ftell(ifp[fd]), (int) len);
            icnt++;
        }
    }
#endif

#if defined(BSD) || defined(ULTRIX) || defined(WIN32)
#define readLenType int
#else /* e.g. SYSV, __TURBOC__ */
#define readLenType unsigned
#endif
    readLenType rlen;
        /* Not perfect, but we don't have ssize_t available. */
#ifdef NINTENDO_3DS
    rlen = (readLenType) b3_rread(fd, (unsigned char *) buf, len);
#else
    rlen = (readLenType) read(fd, buf, (readLenType) len);
#endif
    if ((readLenType) rlen != (readLenType) len) {
        if ((restoreinfo.mread_flags == 1) /* means "return anyway" */
            || (program_state.reading_bonesfile == 1)) {
            restoreinfo.mread_flags = -1;
            return;
        } else {
#ifndef SFCTOOL
            pline("Read %d instead of %u bytes.", (int) rlen, len);
            display_nhwindow(WIN_MESSAGE, TRUE); /* flush before error() */
            if (program_state.restoring) {
                (void) nhclose(fd);
                (void) delete_savefile();
                error("Error restoring old game.");
            }
            panic("Error reading level file.");
#else
            printf("Read %d instead of %u bytes.\n", (int) rlen, len);
#endif
        }
    }
}

staticfn void
sfstruct_read_error(void)
{
    /* problem */;
}

#ifdef SFLOGGING
staticfn void
logging_finish(void)
{
    int i;

    for (i = 0; i < SIZE(ofp); ++i) {
        if (ofp[i]) {
            fclose(ofp[i]);
            ofp[i] = 0;
        }
    }
    ocnt = 0L;

    for (i = 0; i < SIZE(ifp); ++i) {
        if (ifp[i]) {
            fclose(ifp[i]);
            ifp[i] = 0;
        }
    }
    icnt = 0L;
}
#endif  /* SFLOGGING */

#undef SF_X
#undef SF_C
#undef SF_A

/* end of sfstruct.c */


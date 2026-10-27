/* NetHack 5.0	3dssnd.c */
/* Copyright (c) Stichting Mathematisch Centrum, Amsterdam, 1985. */
/* NetHack may be freely redistributed.  See license for details. */
/*
 * Nintendo 3DS sound effects, using libctru's CSND service.
 *
 * This is the port's soundlib for NetHack 5's built-in sound interface
 * (include/sndprocs.h, doc/sound.txt).  Everything the core emits through
 * Soundeffect()/SoundAchievement() lands in sound3ds_procs below; the few
 * gameplay sounds that have no soundlib id yet (footsteps, melee hits, the
 * hero picking something up, ...) call the port's own _3ds_sfx() directly
 * from the core files, guarded by #ifdef NINTENDO_3DS.
 *
 * Sounds come from a small sound pack laid out as
 *
 *      sounds/<key>/0.wav      base
 *      sounds/<key>/1.wav      variation 1
 *      sounds/<key>/2.wav      ...
 *
 * and are looked up at playback time, in this order:
 *
 *      sdmc:/3ds/nethack/sounds/<key>/<n>.wav   (player's own, on SD card)
 *      /3ds/nethack/sounds/<key>/<n>.wav
 *      romfs:/sounds/<key>/<n>.wav              (packed into the build)
 *
 * so the pack can be replaced without rebuilding.  sys/3ds/build_sounds.py
 * turns soundpack/manifest.json into the romfs tree and into the generated
 * key table (build3ds/sndpack.h); see sys/3ds/README.md.
 *
 * Why CSND rather than NDSP: csnd:SND is already in sys/3ds/nethack.rsf and
 * CSND needs no dspfirm.cdc dump.  The service is deprecated but works.
 *
 * Playback model: a small pool of CSND channels.  Effects are decoded once
 * into a RAM cache (up to 4 MB) and stay there for the lifetime of the
 * process, so repeated footsteps and combat do not touch storage.  A play
 * hands the cached buffer to the hardware, and the per-frame
 * _3ds_snd_tick() releases the channel once csndIsPlaying() says it has
 * finished.  If the cache cap is reached, later sounds are still loaded on
 * demand and freed when their channel ends.
 *
 * The main menu (and character creation, which runs before the first turn)
 * and the gameplay loop that starts when the first turn begins are the two
 * looping background tracks.  Music is a longer PCM WAV:
 *
 *      sdmc:/3ds/nethack/music/<name>.wav      player's own
 *      /3ds/nethack/music/<name>.wav
 *      romfs:/music/<name>.wav                 built in
 *
 * The last CSND channel is reserved for the track and loops in hardware, so
 * the music needs no per-frame refill; the remaining channels play effects.
 * The bundled build writes a ~45 second, 22050 Hz mono menu loop and a
 * gameplay loop from soundpack/loop.mp3, each about 2 MB; sys/3ds/
 * build_sounds.py cuts and crossfades the source MP3s.
 */

#include "hack.h"
#include <3ds.h>
#include <3ds/services/csnd.h>
#include "sndpack.h"

#ifdef SND_LIB_3DS

/* How many sounds may overlap.  Effects layer on top of each other freely
   and only steal the oldest slot when every channel is busy; twelve gives a
   rapid burst of kicks or hits room to ring out at once.  CSND has sixteen
   channels, so the remaining ones stay untouched. */
#define SND3DS_MAXCH 12

/* A single decoded effect WAV is dropped if it is larger than this.  The
   bundled pack is well under it; this is just a guard against a rogue file
   filling the linear heap. */
#define SND3DS_MAXBYTES (512 * 1024)

/* Music is much longer than an effect; the generated menu and gameplay
   loops are under 2 MB, so leave room for a slightly longer replacement. */
#define SND3DS_MUSIC_MAXBYTES (3 * 1024 * 1024)
#define SND3DS_MUSIC_NAMELEN 32

static boolean snd_inited = FALSE;
static boolean snd_attempted = FALSE;
static int snd_nchan = 0;       /* usable CSND channels we found */
static int snd_chnum[SND3DS_MAXCH]; /* their hardware channel numbers */
static int snd_music_slot = -1; /* channel pool slot held by the music */
static char snd_music_wanted[SND3DS_MUSIC_NAMELEN]; /* requested track */

/* Volumes from the settings screen, 0.0 .. 1.0.  Effects are scaled by
   master * sfx.  Music is scaled by master * music for the gameplay loop;
   the menu track always plays at the master level. */
static float snd_master = 1.0f;
static float snd_music = 0.8f;
static float snd_sfx = 1.0f;

/* Context-menu actions such as quaff pick an item up as a side effect; the
   action's own sound is the useful feedback, so the pickup jingle is
   suppressed for the duration of that pickup command. */
static int snd_pickup_silent = 0;

struct snd3ds_slot {
    void *buf;      /* linearAlloc'd PCM data, live until the sound ends */
    u32 size;
    u32 rate;
    u64 started;
    const char *key; /* pack key; static lifetime, used to stop related cues */
    boolean busy;
    boolean seen;   /* csndIsPlaying() has reported this channel as playing */
    boolean cached; /* buf belongs to the effect cache; do not free it here */
};
static struct snd3ds_slot snd_slot[SND3DS_MAXCH];

/* Decoded effects live in RAM for the whole process.  The built-in pack is
   about 1.5 MB; the cap leaves room for a few custom files without letting a
   rogue pack eat the linear heap.  Entries are never evicted: if the cap is
   reached, later sounds simply fall back to loading from storage. */
#define SND3DS_CACHE_MAXBYTES (4 * 1024 * 1024)
#define SND3DS_CACHE_MAXENTRIES 128

struct snd3ds_cache_entry {
    int key_index;
    int pick;
    void *buf;
    u32 size;
    u32 rate;
};
static struct snd3ds_cache_entry snd_cache[SND3DS_CACHE_MAXENTRIES];
static int snd_cache_count = 0;
static u32 snd_cache_bytes = 0;

/* CPU ticks per second, as in 3dsled.c. */
#define SND3DS_TICKS_PER_SEC 268111800ULL

/* Own PRNG: never touch the game's random() for picking a variation, or
   replays and bones files would diverge. */
static u32 snd_seed = 0;

static u32
snd3ds_rand(u32 n)
{
    if (n <= 1)
        return 0;
    if (!snd_seed)
        snd_seed = (u32) svcGetSystemTick() | 1u;
    /* xorshift32 */
    snd_seed ^= snd_seed << 13;
    snd_seed ^= snd_seed >> 17;
    snd_seed ^= snd_seed << 5;
    return snd_seed % n;
}

/* ------------------------------------------------------------------ */
/* Key lookup and file probing                                         */
/* ------------------------------------------------------------------ */

static int
snd3ds_find_key(const char *key)
{
    int i;

    if (!key || !*key)
        return -1;
    for (i = 0; i < sndpack_num_keys; i++)
        if (!strcmp(sndpack_keys[i].key, key))
            return i;
    return -1; /* key not in the pack: stay silent */
}

static u32
rd_u32(const unsigned char *p)
{
    return (u32) p[0] | ((u32) p[1] << 8) | ((u32) p[2] << 16)
           | ((u32) p[3] << 24);
}

static u32
rd_u16(const unsigned char *p)
{
    return (u32) p[0] | ((u32) p[1] << 8);
}

/* Read one of the pack's WAVs into a fresh linearAlloc buffer.  Only plain
   PCM, mono, 16-bit is accepted, which is what build_sounds.py writes.
   On success the caller receives the buffer, its size and the sample rate
   in *buf, *size and *rate, and owns *buf. */
static boolean
snd3ds_load_wav(FILE *f, void **buf, u32 *size, u32 *rate, u32 maxbytes)
{
    unsigned char b[16];
    u32 csize, fmt = 0, chans = 0, bits = 0, srate = 0;
    long dataoff = -1;
    u32 datasize = 0;
    void *p;

    *buf = 0;
    *size = 0;
    *rate = 0;

    if (fread(b, 1, 12, f) != 12 || memcmp(b, "RIFF", 4) || memcmp(b + 8, "WAVE", 4))
        return FALSE;

    while (fread(b, 1, 8, f) == 8) {
        csize = rd_u32(b + 4);
        if (!memcmp(b, "fmt ", 4)) {
            unsigned char fb[16];

            if (csize < 16 || fread(fb, 1, 16, f) != 16)
                return FALSE;
            fmt = rd_u16(fb);
            chans = rd_u16(fb + 2);
            srate = rd_u32(fb + 4);
            bits = rd_u16(fb + 14);
            if (csize > 16)
                fseek(f, (long) (csize - 16), SEEK_CUR);
        } else if (!memcmp(b, "data", 4)) {
            dataoff = ftell(f);
            datasize = csize;
            break;
        } else {
            fseek(f, (long) (csize + (csize & 1)), SEEK_CUR);
        }
    }

    if (dataoff < 0 || fmt != 1 /* PCM */ || chans != 1 || bits != 16
        || datasize == 0 || datasize > maxbytes)
        return FALSE;

    if (fseek(f, dataoff, SEEK_SET) != 0)
        return FALSE;
    p = linearAlloc(datasize);
    if (!p)
        return FALSE;
    if (fread(p, 1, datasize, f) != datasize) {
        linearFree(p);
        return FALSE;
    }

    /* Reject an all-but-silent file.  A stale/corrupt SD override used to
       win over the working RomFS copy; treating it as unloadable lets the
       lookup continue to the next location instead of muting the cue. */
    {
        const unsigned char *pcm = (const unsigned char *) p;
        u32 i, peak = 0;

        for (i = 0; i + 1 < datasize; i += 2) {
            int v = (int) (signed short) (pcm[i]
                                          | ((unsigned) pcm[i + 1] << 8));

            if (v < 0)
                v = -v;
            if ((u32) v > peak)
                peak = (u32) v;
        }
        if (peak < 8) {
            linearFree(p);
            return FALSE;
        }
    }

    *buf = p;
    /* CSND addresses sound data in 32-bit words; dropping at most one
       trailing PCM16 sample keeps odd-length files safe. */
    *size = (datasize >= 4) ? (datasize & ~3u) : datasize;
    *rate = srate ? srate : 22050;
    return TRUE;
}

/* Try the SD override first, then the plain mount path, then romfs.  A file
   that opens but is not a usable (silent, corrupt, wrong-format) WAV does
   not stop the search: the next location is tried, so a stale SD file can
   no longer shadow the working RomFS copy.  On success *pathout names the
   file that was actually decoded. */
static boolean
snd3ds_load_key(const char *key, int which, void **buf, u32 *size, u32 *rate,
                char *pathout, size_t pathlen)
{
    static const char *const prefix[] = {
        "sdmc:/3ds/nethack/sounds",
        "/3ds/nethack/sounds",
        "romfs:/sounds",
    };
    char path[PATHLEN];
    FILE *f;
    int i;

    for (i = 0; i < SIZE(prefix); i++) {
        Snprintf(path, sizeof path, "%s/%s/%d.wav", prefix[i], key, which);
        f = fopen(path, "rb");
        if (!f)
            continue;
        if (snd3ds_load_wav(f, buf, size, rate, SND3DS_MAXBYTES)) {
            fclose(f);
            if (pathout && pathlen)
                Snprintf(pathout, pathlen, "%s", path);
            return TRUE;
        }
        fclose(f);
    }
    return FALSE;
}

/* ------------------------------------------------------------------ */
/* Effect cache                                                        */
/* ------------------------------------------------------------------ */

static struct snd3ds_cache_entry *
snd3ds_cache_find(int ki, int pick)
{
    int i;

    for (i = 0; i < snd_cache_count; i++)
        if (snd_cache[i].key_index == ki && snd_cache[i].pick == pick)
            return &snd_cache[i];
    return (struct snd3ds_cache_entry *) 0;
}

/* Adopt a freshly decoded buffer.  Returns NULL when the entry or byte cap
   has been reached; the caller then still owns (and must free) buf. */
static struct snd3ds_cache_entry *
snd3ds_cache_add(int ki, int pick, void *buf, u32 size, u32 rate)
{
    struct snd3ds_cache_entry *ce;

    if (snd_cache_count >= SND3DS_CACHE_MAXENTRIES
        || snd_cache_bytes + size > SND3DS_CACHE_MAXBYTES)
        return (struct snd3ds_cache_entry *) 0;
    ce = &snd_cache[snd_cache_count++];
    ce->key_index = ki;
    ce->pick = pick;
    ce->buf = buf;
    ce->size = size;
    ce->rate = rate;
    snd_cache_bytes += size;
    return ce;
}

/* Decode the whole pack once, so fast repeated actions (walking with the
   run key held down, combat) never touch storage.  Missing variations are
   skipped; playback falls back to the base sound exactly as before. */
static void
snd3ds_cache_preload(void)
{
    char path[PATHLEN];
    void *buf;
    u32 size, rate;
    int ki, pick;

    for (ki = 0; ki < sndpack_num_keys; ki++) {
        const char *key = sndpack_keys[ki].key;

        for (pick = 0; pick <= (int) sndpack_keys[ki].count; pick++) {
            if (snd3ds_cache_find(ki, pick))
                continue;
            if (!snd3ds_load_key(key, pick, &buf, &size, &rate, path,
                                 sizeof path))
                continue;
            if (!snd3ds_cache_add(ki, pick, buf, size, rate))
                linearFree(buf);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Channel pool                                                        */
/* ------------------------------------------------------------------ */

/* The last channel is reserved for music, so effects overlap on one fewer
   channel than CSND gave us.  With the usual twelve that leaves eleven. */
static int
snd3ds_sfx_nchan(void)
{
    return (snd_nchan > 1) ? snd_nchan - 1 : snd_nchan;
}

static void
snd3ds_free_slot(int i)
{
    if (!snd_slot[i].busy)
        return;
    CSND_SetPlayStateR((u32) snd_chnum[i], 0);
    csndExecCmds(true);
    /* Cached effect buffers stay in RAM; only on-demand buffers are owned
       by the slot. */
    if (snd_slot[i].buf && !snd_slot[i].cached)
        linearFree(snd_slot[i].buf);
    snd_slot[i].buf = 0;
    snd_slot[i].size = 0;
    snd_slot[i].key = 0;
    snd_slot[i].cached = FALSE;
    snd_slot[i].busy = FALSE;
}

static void
snd3ds_play(const char *key, int volume)
{
    int ki, count, pick, slot, i;
    const struct sndpack_key *kp;
    struct snd3ds_cache_entry *ce;
    void *buf;
    u32 size, rate;
    float gain;
    boolean cached = FALSE, loaded = FALSE;
    char path[PATHLEN];

    if (!snd_inited || snd_nchan == 0)
        return;

    ki = snd3ds_find_key(key);
    if (ki < 0)
        return;
    kp = &sndpack_keys[ki];
    count = (int) kp->count;         /* number of variations, 0 = base only */
    pick = (int) snd3ds_rand((u32) count + 1);

    path[0] = '\0';
    ce = snd3ds_cache_find(ki, pick);
    if (ce) {
        buf = ce->buf;
        size = ce->size;
        rate = ce->rate;
        cached = TRUE;
        Snprintf(path, sizeof path, "cache:%s/%d", key, pick);
    } else {
        loaded = snd3ds_load_key(key, pick, &buf, &size, &rate, path,
                                 sizeof path);
        if (!loaded && pick != 0) {
            /* the picked variation is missing or unusable (e.g. only the base
               was dropped onto the SD card): fall back to the base sound */
            pick = 0;
            ce = snd3ds_cache_find(ki, pick);
            if (ce) {
                buf = ce->buf;
                size = ce->size;
                rate = ce->rate;
                cached = TRUE;
                loaded = TRUE;
                Snprintf(path, sizeof path, "cache:%s/%d", key, pick);
            } else {
                loaded = snd3ds_load_key(key, pick, &buf, &size, &rate, path,
                                         sizeof path);
            }
        }
        if (!loaded)
            return;
        if (!cached && snd3ds_cache_add(ki, pick, buf, size, rate))
            cached = TRUE;
    }

    gain = ((float) (volume <= 0 ? 100 : volume) / 100.0f) * kp->vol[pick]
           * snd_master * snd_sfx;
    if (gain > 1.0f)
        gain = 1.0f;
    if (gain <= 0.0f) {
        if (!cached)
            linearFree(buf);
        return;
    }

    /* Sounds are allowed to overlap: the smash rings out on top of any
       kick that is still playing, and the oldest effect is only cut off
       when every channel is busy. */

    /* CSND's DSP reads physical memory, so the samples have to be written
       back out of the CPU cache first.  Without this the console plays
       silence (or whatever was in memory before). */
    (void) GSPGPU_FlushDataCache(buf, size);

    /* claim a free effect channel, else stop the oldest one */
    slot = -1;
    for (i = 0; i < snd3ds_sfx_nchan(); i++)
        if (!snd_slot[i].busy) {
            slot = i;
            break;
        }
    if (slot < 0) {
        slot = 0;
        for (i = 1; i < snd3ds_sfx_nchan(); i++)
            if (snd_slot[i].started < snd_slot[slot].started)
                slot = i;
        snd3ds_free_slot(slot);
    }

    snd_slot[slot].buf = buf;
    snd_slot[slot].size = size;
    snd_slot[slot].rate = rate;
    snd_slot[slot].started = svcGetSystemTick();
    snd_slot[slot].key = key;
    snd_slot[slot].seen = FALSE;
    snd_slot[slot].cached = cached;
    snd_slot[slot].busy = TRUE;

    (void) csndPlaySound(snd_chnum[slot],
                          SOUND_ENABLE | SOUND_ONE_SHOT | SOUND_FORMAT_16BIT,
                          rate, gain, 0.0f, buf, buf, size);
}

/* ------------------------------------------------------------------ */
/* Background music                                                    */
/* ------------------------------------------------------------------ */

static FILE *
snd3ds_open_music(const char *name, char *pathout, size_t pathlen)
{
    static const char *const prefix[] = {
        "sdmc:/3ds/nethack/music",
        "/3ds/nethack/music",
        "romfs:/music",
    };
    char path[PATHLEN];
    FILE *f;
    int i;

    for (i = 0; i < SIZE(prefix); i++) {
        Snprintf(path, sizeof path, "%s/%s.wav", prefix[i], name);
        f = fopen(path, "rb");
        if (f) {
            if (pathout && pathlen)
                Snprintf(pathout, pathlen, "%s", path);
            return f;
        }
    }
    return (FILE *) 0;
}

/* The menu track always plays at the master volume; the music setting only
   applies to the gameplay loop.  snd_music_wanted holds the requested track
   ("menu" or "game"). */
static float
snd3ds_music_gain(void)
{
    if (!strcmp(snd_music_wanted, "menu"))
        return snd_master;
    return snd_master * snd_music;
}

/* Load a track and hand it to the hardware in normal-loop mode.  The whole
   buffer is the loop: csndPlaySound() is told that playback starts at the
   buffer and the loop point is the same address, so after the first pass it
   restarts from the beginning without any CPU involvement. */
static void
snd3ds_music_start(const char *name)
{
    FILE *f;
    void *buf;
    u32 size, rate;
    float gain;
    char path[PATHLEN];
    Result rc;
    int slot;

    if (!snd_inited || snd_nchan < 2)
        return;

    slot = snd_nchan - 1;
    if (snd_music_slot >= 0)
        snd3ds_free_slot(snd_music_slot);
    snd_music_slot = -1;

    gain = snd3ds_music_gain();
    if (gain <= 0.0f)
        return; /* wanted, but muted; the volume setter will start it */

    path[0] = '\0';
    f = snd3ds_open_music(name, path, sizeof path);
    if (!f)
        return;
    if (!snd3ds_load_wav(f, &buf, &size, &rate, SND3DS_MUSIC_MAXBYTES)) {
        fclose(f);
        return;
    }
    fclose(f);

    (void) GSPGPU_FlushDataCache(buf, size);

    snd_slot[slot].buf = buf;
    snd_slot[slot].size = size;
    snd_slot[slot].rate = rate;
    snd_slot[slot].started = svcGetSystemTick();
    snd_slot[slot].key = 0; /* music is not an effect key */
    snd_slot[slot].seen = FALSE;
    snd_slot[slot].cached = FALSE; /* music is not part of the effect cache */
    snd_slot[slot].busy = TRUE;

    rc = csndPlaySound(snd_chnum[slot],
                       SOUND_ENABLE | SOUND_REPEAT | SOUND_FORMAT_16BIT,
                       rate, gain, 0.0f, buf, buf, size);
    if (R_FAILED(rc)) {
        snd3ds_free_slot(slot);
        return;
    }
    snd_music_slot = slot;
}

/* ------------------------------------------------------------------ */
/* Public entry points                                                 */
/* ------------------------------------------------------------------ */

/* Start (or keep) a track ("menu" for the front end, "game" for the
   gameplay loop).  Called each time the front end enters the main menu and
   when the first turn begins; the same track is not restarted, so
   character creation, which runs between the menu and the first turn, keeps
   the menu music going. */
void
_3ds_music_play(const char *name)
{
    if (!name || !*name || strlen(name) >= sizeof snd_music_wanted)
        return;

    /* The game shuts the soundlib down when a game ends (see end.c), so
       CSND may be closed by the time the front end shows the menu again.
       Initialization is idempotent and cheap when it is already up. */
    if (!snd_inited)
        _3ds_snd_init();
    if (!snd_inited)
        return;

    if (snd_music_slot >= 0 && snd_slot[snd_music_slot].busy
        && !strcmp(snd_music_wanted, name))
        return; /* already playing this track */

    if (strcmp(snd_music_wanted, name)) {
        _3ds_music_stop();
        Snprintf(snd_music_wanted, sizeof snd_music_wanted, "%s", name);
    }
    snd3ds_music_start(name);
}

void
_3ds_music_stop(void)
{
    snd_music_wanted[0] = '\0';
    if (!snd_inited || snd_music_slot < 0)
        return;
    snd3ds_free_slot(snd_music_slot);
    snd_music_slot = -1;
}

/* Volumes are percentages, 0..100.  Safe to call before _3ds_snd_init():
   the values are only stored until there is a channel to apply them to. */
void
_3ds_snd_set_volumes(int master, int music, int sfx)
{
    snd_master = (float) (master < 0 ? 0 : (master > 100 ? 100 : master))
                 / 100.0f;
    snd_music = (float) (music < 0 ? 0 : (music > 100 ? 100 : music))
                / 100.0f;
    snd_sfx = (float) (sfx < 0 ? 0 : (sfx > 100 ? 100 : sfx)) / 100.0f;

    if (!snd_inited)
        return;

    /* Change the volume of the track that is already looping, without
       restarting it.  CSND_SetVol() only queues the command, so execute it
       now instead of waiting for the next play. */
    if (snd_music_slot >= 0) {
        CSND_SetVol((u32) snd_chnum[snd_music_slot],
                    CSND_VOL(snd3ds_music_gain(), 0.0f), 0);
        csndExecCmds(true);
    } else if (snd_music_wanted[0] && snd3ds_music_gain() > 0.0f) {
        /* requested while muted (or before CSND was ready) */
        snd3ds_music_start(snd_music_wanted);
    }
}

void
_3ds_snd_init(void)
{
    int i, n = 0;
    Result rc;

    if (snd_inited || snd_attempted)
        return;
    snd_attempted = TRUE;

    /* CSND is a plain service; on the rare unit where it cannot be opened
       every call below becomes a no-op and the game stays silent. */
    rc = csndInit();
    if (R_FAILED(rc))
        return;
    snd_inited = TRUE;

    for (i = 0; i < CSND_NUM_CHANNELS && n < SND3DS_MAXCH; i++)
        if (csndChannels & (1u << i))
            snd_chnum[n++] = i;
    /* Be robust if the mask is still empty (some libctru builds only fill
       it after the first CSND update): use the low channels instead of
       staying silent. */
    if (n == 0) {
        for (i = 0; i < SND3DS_MAXCH; i++)
            snd_chnum[n++] = i;
    }
    snd_nchan = n;
    if (snd_nchan == 0) {
        csndExit();
        snd_inited = FALSE;
        return;
    }
    for (i = 0; i < SND3DS_MAXCH; i++) {
        snd_slot[i].busy = FALSE;
        snd_slot[i].buf = 0;
        snd_slot[i].key = 0;
        snd_slot[i].cached = FALSE;
    }
    snd_music_slot = -1;

    /* Decode every effect once.  The cache survives _3ds_snd_exit(), so this
       normally runs only for the first init of the process; returning to the
       menu or starting another game reuses the same RAM. */
    if (snd_cache_count == 0)
        snd3ds_cache_preload();
}

void
_3ds_snd_exit(void)
{
    int i;

    if (!snd_inited)
        return;
    _3ds_music_stop();
    for (i = 0; i < snd_nchan; i++)
        snd3ds_free_slot(i);
    csndExit();
    snd_inited = FALSE;
    snd_attempted = FALSE;
    snd_nchan = 0;
    /* snd_cache is deliberately not freed: the decoded effects are reused
       when the front end reopens CSND for the menu or the next game.  The
       OS reclaims them at process exit. */
}

/* Called once per rendered frame while waiting for input, next to the LED
   and hit-flash ticks; releases buffers whose sound has finished. */
void
_3ds_snd_tick(void)
{
    int i;
    u64 now;

    if (!snd_inited)
        return;
    now = svcGetSystemTick();
    /* the music slot is managed by _3ds_music_play/_stop, not by this tick */
    for (i = 0; i < snd3ds_sfx_nchan(); i++) {
        u8 playing = 0;
        u64 elapsed_ms, dur_ms;

        if (!snd_slot[i].busy)
            continue;
        elapsed_ms = (now - snd_slot[i].started) * 1000ULL
                     / SND3DS_TICKS_PER_SEC;
        /* 16-bit mono: bytes / (2 * rate) seconds */
        dur_ms = snd_slot[i].rate
                     ? ((u64) snd_slot[i].size * 500ULL / snd_slot[i].rate)
                     : 0ULL;

        if (R_FAILED(csndIsPlaying((u32) snd_chnum[i], &playing))) {
            /* cannot query: fall back to the sound's known length */
            if (elapsed_ms > dur_ms + 500ULL)
                snd3ds_free_slot(i);
        } else if (playing) {
            snd_slot[i].seen = TRUE;
            /* never hold a buffer forever if the status gets stuck */
            if (elapsed_ms > dur_ms + 2000ULL)
                snd3ds_free_slot(i);
        } else if (snd_slot[i].seen) {
            /* it was playing and has now stopped */
            snd3ds_free_slot(i);
        } else if (elapsed_ms > dur_ms + 500ULL) {
            /* the channel never reported itself as playing: either the
               sound was rejected or it finished between two queries.
               Holding it for its full length costs little and avoids
               cutting an effect short if the status lags. */
            snd3ds_free_slot(i);
        }
    }

    /* Guard the looping music.  The track is a hardware loop and should
       always be running; if CSND reports its reserved channel as silent,
       restart it so the background music does not die mid-game.  If a
       reload fails the slot is released and this check is naturally
       skipped afterwards. */
    if (snd_music_slot >= 0 && snd_slot[snd_music_slot].busy) {
        u8 playing = 0;

        if (R_SUCCEEDED(csndIsPlaying((u32) snd_chnum[snd_music_slot],
                                      &playing))
            && !playing)
            snd3ds_music_start(snd_music_wanted);
    }
}

/* Trigger for gameplay sounds that have no soundlib id (see the #ifdef
   NINTENDO_3DS hooks in src/).  Honours the sound option and deafness. */
void
_3ds_sfx(const char *key, int volume)
{
    if (!iflags.sounds || Deaf)
        return;
    snd3ds_play(key, volume);
}

/* Same, but played even while the hero is Deaf, for the few cues that are
   the player's own feedback rather than the hero's hearing (footsteps,
   level-up). */
void
_3ds_sfx_always(const char *key, int volume)
{
    if (!iflags.sounds)
        return;
    snd3ds_play(key, volume);
}

/* Context-menu quaff picks the potion up first; that pickup is only a
   mechanical step, so its jingle would mask the quaff sound.  The flag is
   set by cmd.c before the queued pickup and cleared by dopickup(). */
void
_3ds_sfx_pickup_silent(int on)
{
    snd_pickup_silent = on ? 1 : 0;
}

int
_3ds_sfx_pickup_is_silent(void)
{
    return snd_pickup_silent != 0;
}

/* ------------------------------------------------------------------ */
/* soundlib glue                                                       */
/* ------------------------------------------------------------------ */

static void sound3ds_init_nhsound(void);
static void sound3ds_exit_nhsound(const char *);
static void sound3ds_achievement(schar, schar, int32_t);
static void sound3ds_soundeffect(char *, int32_t, int32_t);
static void sound3ds_hero_playnotes(int32_t, const char *, int32_t);
static void sound3ds_play_usersound(char *, int32_t, int32_t);
static void sound3ds_ambience(int32_t, int32_t, int32_t);
static void sound3ds_verbal(char *, int32_t, int32_t, int32_t, int32_t);

struct sound_procs sound3ds_procs = {
    SOUNDID(sound3ds),
    SOUND_TRIGGER_SOUNDEFFECTS | SOUND_TRIGGER_ACHIEVEMENTS,
    sound3ds_init_nhsound,
    sound3ds_exit_nhsound,
    sound3ds_achievement,
    sound3ds_soundeffect,
    sound3ds_hero_playnotes,
    sound3ds_play_usersound,
    sound3ds_ambience,
    sound3ds_verbal,
};

/* Core sound effects that have a natural equivalent in the pack.  The rest
   of the pack keys are played from the port's own hooks. */
static const struct {
    int seid;
    const char *key;
} snd3ds_seid_map[] = {
    { se_door_unlock_and_open, "unlock" },
    { se_door_crash_open, "door-smash" },
    { se_crash_door, "door-smash" },
    { se_kick_door_it_shatters, "door-smash" },
    /* Both successful break outcomes use the smash cue.  door-kick is
       reserved for a kick that did not break the door. */
    { se_kick_door_it_crashes_open, "door-smash" },
    { se_splash, "splash" },
    { se_loud_splash, "splash" },
    { se_faint_splashing, "splash" },
    { se_distant_thunder, "thunder" },
    { se_guards_footsteps, "footsteps" },
};

static void
sound3ds_init_nhsound(void)
{
    _3ds_snd_init();
}

static void
sound3ds_exit_nhsound(const char *reason UNUSED)
{
    _3ds_snd_exit();
}

static void
sound3ds_achievement(schar ach1, schar ach2, int32_t avals UNUSED)
{
    if (ach1 != 0) /* ordinary achievements have no sounds in this pack */
        return;
    if ((int) ach2 == sa2_xplevelup)
        _3ds_sfx_always("level-up", 100);
}

static void
sound3ds_soundeffect(char *desc UNUSED, int32_t seid, int32_t volume)
{
    int i;

    for (i = 0; i < SIZE(snd3ds_seid_map); i++)
        if (snd3ds_seid_map[i].seid == (int) seid) {
            /* The pack is normalized at build time; playing the core's
               per-call volume here would make some cues (faint splashes,
               kicks, unlocks) quieter than the rest.  Keep every effect at
               the pack level. */
            snd3ds_play(snd3ds_seid_map[i].key, 100);
            return;
        }
}

static void
sound3ds_hero_playnotes(int32_t instrument UNUSED, const char *str UNUSED,
                        int32_t volume UNUSED)
{
}

static void
sound3ds_play_usersound(char *filename UNUSED, int32_t volume UNUSED,
                        int32_t idx UNUSED)
{
}

static void
sound3ds_ambience(int32_t ambience_action UNUSED, int32_t ambienceid UNUSED,
                  int32_t proximity UNUSED)
{
}

static void
sound3ds_verbal(char *text UNUSED, int32_t gender UNUSED, int32_t tone UNUSED,
                int32_t vol UNUSED, int32_t moreinfo UNUSED)
{
}

#endif /* SND_LIB_3DS */

/*3dssnd.c*/

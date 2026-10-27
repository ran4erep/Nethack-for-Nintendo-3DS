/* NetHack 5.0	3dsled.c */
/* Copyright (c) Stichting Mathematisch Centrum, Amsterdam, 1985. */
/* NetHack may be freely redistributed.  See license for details. */
/*
 * Nintendo 3DS notification-LED health indicator.
 *
 * The notification LED (the one at the right edge of the top screen, next
 * to the cameras) normally stays OFF.  It lights only while a game is
 * actually being played, and only to report the hero's hit points:
 *
 * The LED mirrors the status bar's health bands (see _3ds_health_color()
 * in 3dsart.c), so it always agrees with the bar painted on the top
 * screen.  Above half hit points is solid green, from a quarter to half
 * is solid amber, a quarter or less is breathing red, and death
 * (u.uhp <= 0) is breathing red too.
 *
 * When the hero is polymorphed, the monster form's hit points (u.mh) are
 * used, exactly like the status panel does.
 *
 * In every menu - the game's main menu, pause, settings, character
 * creation - and after exiting to the console's HOME menu the LED is
 * turned back off (the HOME menu suspends the app, and the app restores
 * the LED when it notices it is leaving the foreground).
 *
 * The "breathing" (fading from zero to full brightness and back) is done
 * in software: the MCU's pattern timings are underspecified, and patterns
 * that play too fast read as a dim, barely-changing glow.  The game's
 * per-frame tick instead computes the desired brightness from the wall
 * clock and applies it as a plain solid pattern, so the fade is slow,
 * unmistakable, and reaches a true 0 and a true 255 every cycle.
 *
 * The LED is an RGB unit behind the MCU.  We only ever talk to it through
 * libctru's MCUHWC_SetInfoLedPattern(), which validates the pattern against
 * the hardware protocol, so this is safe: do NOT write to the MCU's I2C
 * registers directly, malformed patterns there have irreversibly bricked
 * consoles on old firmware.
 *
 * The system still owns this same LED for its own events (StreetPass /
 * SpotPass notifications, low-battery warning).  Those briefly override our
 * pattern and then hand the LED back; that is expected and unavoidable.
 *
 * The feature can be turned off from the 3DS UI settings screen (Health LED)
 * or by setting "led=0" in sdmc:/3ds/nethack/config/3dsui.cfg.
 */

#include "hack.h"
#include <3ds.h>
#include <3ds/services/mcuhwc.h>
#include <math.h>
#include <string.h>

/* Whether mcuHwc is initialized and usable. */
static boolean led_inited = FALSE;

/* The pattern currently applied to the LED.  Kept so the per-frame tick
   only talks to the MCU when the state actually changed. */
static boolean cur_on;      /* TRUE = any non-off pattern is applied */
static u8 cur_r, cur_g, cur_b;
static boolean cur_pulse;   /* TRUE = the breathing animation is running */
static u8 cur_bright;       /* brightness of the last applied pulse frame */
static double pulse_phase;  /* phase of the breathing cycle, radians */
static u64 pulse_last_tick; /* CPU tick of the last pulse update */

/* One full breathing cycle; rise and fall are each half of this. */
#define PULSE_PERIOD_MS 3200.0
#define PULSE_STEP 8 /* brightness quantisation: writes only every ~2 frames */

/* Send a pattern to the MCU.  The result is deliberately ignored: on the
   few units where the notification LED is not reachable (or the system is
   momentarily holding it for its own event) there is nothing we can do
   about it, and the game must go on. */
static void
led_apply(const InfoLedPattern *pat)
{
    (void) MCUHWC_SetInfoLedPattern(pat);
}

/* Plain solid colour: all 32 steps set to the same RGB value, repeated
   forever.  Does not touch the module's state bookkeeping. */
static void
led_apply_solid(u8 r, u8 g, u8 b)
{
    InfoLedPattern pat;
    int i;

    memset(&pat, 0, sizeof pat);
    pat.delay = 0x10;      /* 1 second per step; all steps equal -> static */
    pat.loopDelay = 0x00;  /* repeat forever */
    for (i = 0; i < 32; i++) {
        pat.redPattern[i] = r;
        pat.greenPattern[i] = g;
        pat.bluePattern[i] = b;
    }
    led_apply(&pat);
}

/* Solid colour with bookkeeping: the LED now shows this exact colour. */
static void
led_solid(u8 r, u8 g, u8 b)
{
    led_apply_solid(r, g, b);
    cur_on = TRUE;
    cur_r = r;
    cur_g = g;
    cur_b = b;
    cur_pulse = FALSE;
    cur_bright = 0;
}

/* Turn the LED completely off (its normal resting state). */
static void
led_off(void)
{
    InfoLedPattern pat;

    if (!cur_on && !cur_pulse)
        return;
    memset(&pat, 0, sizeof pat);
    pat.delay = 0x10;
    pat.loopDelay = 0x00;
    led_apply(&pat);
    cur_on = FALSE;
    cur_r = cur_g = cur_b = 0;
    cur_pulse = FALSE;
    cur_bright = 0;
}

/* Advance the breathing animation and write the new brightness.  Called
   from the per-frame tick while the red pulse is active. */
static void
led_breath(u8 r, u8 g, u8 b)
{
    static const double PI = 3.14159265358979323846;
    const u64 ticks_per_sec = 268111800ULL;
    u64 now = svcGetSystemTick();
    double dt_ms, k;
    u8 br;

    if (pulse_last_tick == 0) {
        /* (re)start of a breath: begin dark and rise */
        pulse_last_tick = now;
        pulse_phase = 0.0;
        cur_bright = 0xff; /* force the first frame to be written */
    }

    dt_ms = (double) (now - pulse_last_tick)
            * (1000.0 / (double) ticks_per_sec);
    pulse_last_tick = now;

    pulse_phase += dt_ms * (2.0 * PI / PULSE_PERIOD_MS);
    if (pulse_phase > 2.0 * PI)
        pulse_phase -= 2.0 * PI;

    /* 0..1, ends of the cycle are exactly 0 (LED fully off) */
    k = (1.0 - cos(pulse_phase)) / 2.0;
    br = (u8) (k * 255.0);
    br = (u8) ((br / PULSE_STEP) * PULSE_STEP);
    if (br != cur_bright) {
        cur_bright = br;
        led_apply_solid((u8) (r * br / 255), (u8) (g * br / 255),
                        (u8) (b * br / 255));
    }
}

void
_3ds_led_init(void)
{
    if (led_inited)
        return;
    /* mcuHwc is a plain service: when it cannot be opened (very old
       homebrew entry points) keep the LED off and make every other call
       here a no-op. */
    if (R_FAILED(mcuHwcInit()))
        return;
    led_inited = TRUE;
    cur_on = TRUE; /* force the first led_off() to write */
    led_off();
}

void
_3ds_led_off(void)
{
    if (led_inited)
        led_off();
}

void
_3ds_led_exit(void)
{
    if (!led_inited)
        return;
    led_off();
    mcuHwcExit();
    led_inited = FALSE;
}

/* Called once per rendered frame while the game is waiting for input. */
void
_3ds_led_tick(void)
{
    /* Shared health-band logic from 3dsart.c; see _3ds_health_color(). */
    extern void _3ds_health_color(int, int, u16 *, boolean *);
    u8 r, g, b;
    boolean pulse;
    int hp, hpmax;

    if (!led_inited)
        return;

    /* Feature switched off in the settings -> LED off. */
    if (!_3ds_led_enabled()) {
        led_off();
        return;
    }

    /* No game under way yet (main menu, character creation, rolling a
       character): nothing to say, keep the LED off. */
    if (!program_state.something_worth_saving) {
        led_off();
        return;
    }

    hp = Upolyd ? u.mh : u.uhp;
    hpmax = Upolyd ? u.mhmax : u.uhpmax;
    if (hpmax <= 0) {
        /* stats not established yet */
        led_off();
        return;
    }

    /* Pick the colour and the breathing flag from the same health logic
       the top status bar uses (_3ds_health_color in 3dsart.c), instead of
       this file's own hardcoded bands.  The bar's colour is RGB565, so
       stretch it back out to byte-sized channels for the LED. */
    {
        u16 lcol;
        boolean lpulse;

        _3ds_health_color(hp, hpmax, &lcol, &lpulse);
        r = (u8) (((lcol >> 11) & 0x1f) << 3);
        g = (u8) (((lcol >> 5) & 0x3f) << 2);
        b = (u8) ((lcol & 0x1f) << 3);
        pulse = lpulse;
    }

    if (pulse) {
        if (cur_pulse && cur_r == r && cur_g == g && cur_b == b) {
            led_breath(r, g, b);
        } else {
            cur_r = r;
            cur_g = g;
            cur_b = b;
            cur_pulse = TRUE;
            pulse_last_tick = 0; /* start this breath from dark */
            led_breath(r, g, b);
        }
    } else {
        if (cur_pulse || !cur_on
            || r != cur_r || g != cur_g || b != cur_b)
            led_solid(r, g, b);
    }
}

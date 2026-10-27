/* NetHack 5.0	youhear.c
 * Central hook for the "You hear..." message sounds.
 *
 * Generated from soundpack/new/you-hear/ by .tools/gen_youhear_c.py.
 * The map is checked case-insensitively against the formatted text of
 * every You_hear() message; the first matching pattern plays its pack
 * key.  Order matters: put specific patterns before broad ones.
 */
#include "hack.h"
#include "sndpack.h"

#if defined(SND_LIB_3DS)

static const struct {
    const char *pat;
    const char *key;
} youhear_map[] = {
    { "cwnannwn", "cwnannwn-howling" },
    { "banshee", "banshee-wailing" },
    { "some noises", "noises-in-distance" },
    { "zap", "wand-zap" },
    { "chugging", "potion-chugging" },
    { "monster behind", "monster-behind-boulder" },
    { "rumble of distant thunder", "thunder" },
    { "studio audience applaud", "studio-audience-applause" },
    { "howling at the moon", "werewolf-howl" },
    { "slurping", "gelatinous-cube-slurp" },
    { "crunching", "metallivore-crunch" },
    { "crashing rock", "dwarf-digging-rock" },
    { "crash through the ground", "hole-crash-ground" },
    { "a blast", "gas-spore-blast" },
    { "crashing sound", "door-destroyed-crash" },
    { "rushing noise", "vortex-rushing" },
    { "splat", "ochre-jelly-splat" },
    { "a thud", "armor-thud" },
    { "clank", "shield-clank" },
    { "cracking sound", "armor-cracking" },
    { "ripping", "shirt-ripping" },
    { "chewing", "chewing-lizard" },
    { "a cough", "cough-breath" },
    { "gobbling", "ghoul-gobble" },
    { "sloshing", "glob-slosh" },
    { "slow drip", "slow-drip" },
    { "gurgling", "sink-gurgle" },
    { "dishes being washed", "dishes-washed" },
    { "water falling on coins", "water-on-coins" },
    { "bubbling water", "fountain-bubbling" },
    { "soda fountain", "soda-fountain" },
    { "tumbler", "tumblers-click" },
    { "gear", "gears-turn" },
    { "rumbling in the distance", "boulder-trap-rumble" },
    { "bowling", "bowling" },
    { "rumbling stop", "rumbling-stop" },
    { "loud crash", "boulder-crash" },
    { "distant explosion", "trapped-door-explosion" },
    { "explosion in the distance", "landmine-explosion" },
    { "squeaking", "squeaky-board" },
    { "confused bear", "bear-web-roar" },
    { "angry bear", "bear-trap-roar" },
    { "deafening roar", "deafening-roar" },
    { "distant howling", "magic-trap-howl" },
    { "moon howling", "moon-howl" },
    { "loud click", "arrow-trap-click" },
    { "soft click", "dart-trap-click" },
    { "twang", "trapdoor-twang" },
    { "seal barking", "seal-bark" },
    { "elephant stepping", "elephant-peanut" },
    { "doolittle", "doctor-doolittle" },
    { "someone searching", "searching-vault" },
    { "scrooge", "scrooge" },
    { "quarterback", "quarterback-play" },
    { "snoring snakes", "oracle-snoring" },
    { "strange wind", "oracle-wind" },
    { "convulsive ravings", "oracle-ravings" },
    { "woodchucks", "oracle-woodchuck" },
    { "zot", "oracle-zot" },
    { "courtly conversation", "throne-conversation" },
    { "sceptre pounded", "throne-sceptre" },
    { "off with", "throne-off-with-head" },
    { "beruthiel", "queen-cats" },
    { "praising", "temple-praising" },
    { "beseeching", "temple-beseeching" },
    { "sacrifice", "temple-sacrifice" },
    { "donations", "temple-donations" },
    { "low buzzing", "beehive-buzzing" },
    { "angry drone", "beehive-drone" },
    { "bees in your bonnet", "bees-bonnet" },
    { "nonexistent", "bees-nonexistent-bonnet" },
    { "unnaturally quiet", "graveyard-quiet" },
    { "on the back of your", "graveyard-hair-neck" },
    { "seems to stand up", "graveyard-hair-head" },
    { "blades being honed", "barracks-blades" },
    { "loud snoring", "barracks-snoring" },
    { "dice being thrown", "barracks-dice" },
    { "macarthur", "general-macarthur" },
    { "mosquitoes", "swamp-mosquitoes" },
    { "marsh gas", "swamp-marsh-gas" },
    { "donald duck", "donald-duck" },
    { "shoplifters", "shop-shoplifters" },
    { "cash register", "shop-cash-register" },
    { "neiman", "neiman-marcus" },
    { "something shatter", "item-shatter" },
    { "crumbling", "boulder-crumbling" },
    { "maniacal laughter close by", "scare-monster-laugh-near" },
    { "maniacal laughter in the distance", "scare-monster-laugh-far" },
    { "sad wailing close by", "scare-monster-wail-near" },
    { "sad wailing in the distance", "scare-monster-wail-far" },
    { "muffled shatter", "container-muffled-shatter" },
    { "muffled cracking", "container-muffled-crack" },
    { "hellfire", "hellfire-crackling" },
    { "heart beat", "heartbeat" },
    { "typing", "faint-typing" },
    { "hollow sound", "secret-door-hollow" },
    { "dead, jim", "its-dead-jim" },
    { "reveille", "bugle-reveille" },
    { "battle gear", "battle-gear-rattle" },
    { "hissing", "moat-hissing-gas" },
    { "soft crackling", "moat-soft-crackling" },
    { "crackling sound", "plane-water-crackling" },
};

static int
stristr(const char *hay, const char *needle)
{
    int i, j;

    for (i = 0; hay[i]; i++) {
        for (j = 0; needle[j]; j++) {
            unsigned char c = (unsigned char) hay[i + j];

            if (c >= 'A' && c <= 'Z')
                c = (unsigned char) (c + ('a' - 'A'));
            if (c != (unsigned char) needle[j])
                break;
        }
        if (!needle[j])
            return 1;
    }
    return 0;
}

/* Called from You_hear() in pline.c after the message is formatted.
   Plays the pack cue matching the message, if any. */
void
_3ds_youhear_sfx(const char *msg)
{
    size_t i;

    if (!msg || !*msg)
        return;
    if (!iflags.sounds || Deaf)
        return;
    for (i = 0; i < SIZE(youhear_map); i++)
        if (stristr(msg, youhear_map[i].pat)) {
            _3ds_sfx(youhear_map[i].key, 100);
            return;
        }
}

#endif /* SND_LIB_3DS */

/*youhear.c*/

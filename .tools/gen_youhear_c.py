#!/usr/bin/env python3
"""Generate sys/3ds/youhear.c mapping table from you-hear message patterns."""
import os

# (lowercase substring pattern, pack key)
MAP = [
    # --- You (death omens) ---
    ("cwnannwn", "cwnannwn-howling"),
    ("banshee", "banshee-wailing"),
    # --- Monsters: general ---
    ("some noises", "noises-in-distance"),
    ("zap", "wand-zap"),
    ("chugging", "potion-chugging"),
    ("monster behind", "monster-behind-boulder"),
    # --- Pets ---
    ("rumble of distant thunder", "thunder"),
    ("studio audience applaud", "studio-audience-applause"),
    # --- Werecreatures ---
    ("howling at the moon", "werewolf-howl"),
    # --- Other monsters ---
    ("slurping", "gelatinous-cube-slurp"),
    ("crunching", "metallivore-crunch"),
    ("crashing rock", "dwarf-digging-rock"),
    ("crash through the ground", "hole-crash-ground"),
    ("a blast", "gas-spore-blast"),
    ("crashing sound", "door-destroyed-crash"),
    ("rushing noise", "vortex-rushing"),
    ("splat", "ochre-jelly-splat"),
    ("a thud", "armor-thud"),
    ("clank", "shield-clank"),
    ("cracking sound", "armor-cracking"),
    ("ripping", "shirt-ripping"),
    ("chewing", "chewing-lizard"),
    ("a cough", "cough-breath"),
    ("gobbling", "ghoul-gobble"),
    ("sloshing", "glob-slosh"),
    # --- Dungeon features ---
    ("slow drip", "slow-drip"),
    ("gurgling", "sink-gurgle"),
    ("dishes being washed", "dishes-washed"),
    ("water falling on coins", "water-on-coins"),
    ("bubbling water", "fountain-bubbling"),
    ("soda fountain", "soda-fountain"),
    ("tumbler", "tumblers-click"),
    ("gear", "gears-turn"),
    # --- Traps ---
    ("rumbling in the distance", "boulder-trap-rumble"),
    ("bowling", "bowling"),
    ("rumbling stop", "rumbling-stop"),
    ("loud crash", "boulder-crash"),
    ("distant explosion", "trapped-door-explosion"),
    ("explosion in the distance", "landmine-explosion"),
    ("squeaking", "squeaky-board"),
    ("confused bear", "bear-web-roar"),
    ("angry bear", "bear-trap-roar"),
    ("deafening roar", "deafening-roar"),
    ("distant howling", "magic-trap-howl"),
    ("moon howling", "moon-howl"),
    ("loud click", "arrow-trap-click"),
    ("soft click", "dart-trap-click"),
    ("twang", "trapdoor-twang"),
    # --- Special rooms ---
    ("seal barking", "seal-bark"),
    ("elephant stepping", "elephant-peanut"),
    ("doolittle", "doctor-doolittle"),
    ("someone searching", "searching-vault"),
    ("scrooge", "scrooge"),
    ("quarterback", "quarterback-play"),
    ("snoring snakes", "oracle-snoring"),
    ("strange wind", "oracle-wind"),
    ("convulsive ravings", "oracle-ravings"),
    ("woodchucks", "oracle-woodchuck"),
    ("zot", "oracle-zot"),
    ("courtly conversation", "throne-conversation"),
    ("sceptre pounded", "throne-sceptre"),
    ("off with", "throne-off-with-head"),
    ("beruthiel", "queen-cats"),
    ("praising", "temple-praising"),
    ("beseeching", "temple-beseeching"),
    ("sacrifice", "temple-sacrifice"),
    ("donations", "temple-donations"),
    ("low buzzing", "beehive-buzzing"),
    ("angry drone", "beehive-drone"),
    ("bees in your bonnet", "bees-bonnet"),
    ("nonexistent", "bees-nonexistent-bonnet"),
    ("unnaturally quiet", "graveyard-quiet"),
    ("on the back of your", "graveyard-hair-neck"),
    ("seems to stand up", "graveyard-hair-head"),
    ("blades being honed", "barracks-blades"),
    ("loud snoring", "barracks-snoring"),
    ("dice being thrown", "barracks-dice"),
    ("macarthur", "general-macarthur"),
    ("mosquitoes", "swamp-mosquitoes"),
    ("marsh gas", "swamp-marsh-gas"),
    ("donald duck", "donald-duck"),
    ("shoplifters", "shop-shoplifters"),
    ("cash register", "shop-cash-register"),
    ("neiman", "neiman-marcus"),
    # --- Items ---
    ("something shatter", "item-shatter"),
    ("crumbling", "boulder-crumbling"),
    # --- Scroll of scare monster ---
    ("maniacal laughter close by", "scare-monster-laugh-near"),
    ("maniacal laughter in the distance", "scare-monster-laugh-far"),
    ("sad wailing close by", "scare-monster-wail-near"),
    ("sad wailing in the distance", "scare-monster-wail-far"),
    # --- Container ---
    ("muffled shatter", "container-muffled-shatter"),
    ("muffled cracking", "container-muffled-crack"),
    # --- Stethoscope ---
    ("hellfire", "hellfire-crackling"),
    ("heart beat", "heartbeat"),
    ("typing", "faint-typing"),
    ("hollow sound", "secret-door-hollow"),
    ("dead, jim", "its-dead-jim"),
    # --- Bugle ---
    ("reveille", "bugle-reveille"),
    ("battle gear", "battle-gear-rattle"),
    # --- Dungeon environment ---
    ("hissing", "moat-hissing-gas"),
    ("soft crackling", "moat-soft-crackling"),
    ("crackling sound", "plane-water-crackling"),
]

BASE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(BASE, "..", "sys", "3ds", "youhear.c")
OUT = os.path.normpath(OUT)

lines = []
lines.append("/* NetHack 5.0	youhear.c")
lines.append(" * Central hook for the \"You hear...\" message sounds.")
lines.append(" *")
lines.append(" * Generated from soundpack/new/you-hear/ by .tools/gen_youhear_c.py.")
lines.append(" * The map is checked case-insensitively against the formatted text of")
lines.append(" * every You_hear() message; the first matching pattern plays its pack")
lines.append(" * key.  Order matters: put specific patterns before broad ones.")
lines.append(" */")
lines.append("#include \"hack.h\"")
lines.append("#include \"sndpack.h\"")
lines.append("")
lines.append("#if defined(SND_LIB_3DS)")
lines.append("")
lines.append("static const struct {")
lines.append("    const char *pat;")
lines.append("    const char *key;")
lines.append("} youhear_map[] = {")
for pat, key in MAP:
    lines.append('    { "%s", "%s" },' % (pat, key))
lines.append("};")
lines.append("")
lines.append("static int")
lines.append("stristr(const char *hay, const char *needle)")
lines.append("{")
lines.append("    int i, j;")
lines.append("")
lines.append("    for (i = 0; hay[i]; i++) {")
lines.append("        for (j = 0; needle[j]; j++) {")
lines.append("            unsigned char c = (unsigned char) hay[i + j];")
lines.append("")
lines.append("            if (c >= 'A' && c <= 'Z')")
lines.append("                c = (unsigned char) (c + ('a' - 'A'));")
lines.append("            if (c != (unsigned char) needle[j])")
lines.append("                break;")
lines.append("        }")
lines.append("        if (!needle[j])")
lines.append("            return 1;")
lines.append("    }")
lines.append("    return 0;")
lines.append("}")
lines.append("")
lines.append("/* Called from You_hear() in pline.c after the message is formatted.")
lines.append("   Plays the pack cue matching the message, if any. */")
lines.append("void")
lines.append("_3ds_youhear_sfx(const char *msg)")
lines.append("{")
lines.append("    size_t i;")
lines.append("")
lines.append("    if (!msg || !*msg)")
lines.append("        return;")
lines.append("    if (!iflags.sounds || Deaf)")
lines.append("        return;")
lines.append("    for (i = 0; i < SIZE(youhear_map); i++)")
lines.append("        if (stristr(msg, youhear_map[i].pat)) {")
lines.append("            _3ds_sfx(youhear_map[i].key, 100);")
lines.append("            return;")
lines.append("        }")
lines.append("}")
lines.append("")
lines.append("#endif /* SND_LIB_3DS */")
lines.append("")
lines.append("/*youhear.c*/")

with open(OUT, "w", encoding="utf-8", newline="\n") as f:
    f.write("\n".join(lines) + "\n")
print("wrote", OUT, "entries:", len(MAP))

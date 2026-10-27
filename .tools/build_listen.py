#!/usr/bin/env python3
"""Build a listen page for the generated you-hear sounds + engraving/footsteps."""
import json, os, html

BASE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(BASE)
YOU = os.path.join(ROOT, "soundpack", "new", "you-hear")
OUT_HTML = os.path.join(YOU, "listen.html")

CATS = [
    ("Death omens", ["cwnannwn-howling", "banshee-wailing"]),
    ("Monsters - general", ["noises-in-distance", "wand-zap", "potion-chugging",
                             "monster-behind-boulder"]),
    ("Pets", ["pet-thunder", "studio-audience-applause"]),
    ("Werecreatures", ["werewolf-howl"]),
    ("Other monsters", ["gelatinous-cube-slurp", "metallivore-crunch",
                        "dwarf-digging-rock", "hole-crash-ground", "gas-spore-blast",
                        "door-destroyed-crash", "vortex-rushing",
                        "ochre-jelly-splat", "armor-thud", "shield-clank",
                        "armor-cracking", "shirt-ripping", "chewing-lizard",
                        "cough-breath", "ghoul-gobble", "glob-slosh"]),
    ("Dungeon features", ["slow-drip", "sink-gurgle", "dishes-washed",
                          "water-on-coins", "fountain-bubbling", "soda-fountain",
                          "tumblers-click", "gears-turn"]),
    ("Traps", ["boulder-trap-rumble", "bowling", "rumbling-stop", "boulder-crash",
               "trapped-door-explosion", "landmine-explosion", "squeaky-board",
               "bear-web-roar", "bear-trap-roar", "deafening-roar",
               "magic-trap-howl", "moon-howl", "arrow-trap-click", "dart-trap-click",
               "trapdoor-twang"]),
    ("Special rooms", ["seal-bark", "elephant-peanut", "doctor-doolittle",
                       "searching-vault", "scrooge", "quarterback-play",
                       "oracle-snoring", "oracle-wind", "oracle-ravings",
                       "oracle-woodchuck", "oracle-zot", "throne-conversation",
                       "throne-sceptre", "throne-off-with-head", "queen-cats",
                       "temple-praising", "temple-beseeching",
                       "temple-sacrifice", "temple-donations",
                       "beehive-buzzing", "beehive-drone", "bees-bonnet",
                       "bees-nonexistent-bonnet", "graveyard-quiet",
                       "graveyard-hair-neck", "graveyard-hair-head",
                       "barracks-blades", "barracks-snoring", "barracks-dice",
                       "general-macarthur", "swamp-mosquitoes", "swamp-marsh-gas",
                       "donald-duck", "shop-shoplifters", "shop-cash-register",
                       "neiman-marcus"]),
    ("Items", ["item-shatter", "boulder-crumbling"]),
    ("Scroll of scare monster", ["scare-monster-laugh-near",
                                 "scare-monster-laugh-far",
                                 "scare-monster-wail-near",
                                 "scare-monster-wail-far"]),
    ("Container", ["container-muffled-shatter", "container-muffled-crack"]),
    ("Stethoscope", ["heartbeat", "faint-typing", "secret-door-hollow",
                     "its-dead-jim", "hellfire-crackling"]),
    ("Bugle", ["bugle-reveille", "battle-gear-rattle"]),
    ("Dungeon environment", ["moat-hissing-gas", "moat-soft-crackling",
                             "plane-water-crackling"]),
]

with open(os.path.join(YOU, "manifest.json"), encoding="utf-8") as f:
    manifest = json.load(f)

def slug_map():
    m = {}
    for cat, slugs in CATS:
        for s in slugs:
            m[s] = cat
    return m
SMAP = slug_map()

B = []
B.append("<!DOCTYPE html><html lang='en'><head><meta charset='utf-8'>")
B.append("<title>NetHack 3DS - You hear sounds</title>")
B.append("<style>")
B.append("body{font-family:system-ui,sans-serif;margin:24px;background:#1a1a2e;color:#eee;max-width:900px;margin:0 auto;padding:24px}")
B.append("h1{color:#e0e0ff}h2{color:#9aa6ff;margin-top:36px;border-bottom:1px solid #333;padding-bottom:4px}")
B.append(".item{background:#24243e;border:1px solid #333;border-radius:8px;padding:8px 12px;margin:6px 0;display:flex;align-items:center;gap:12px;flex-wrap:wrap}")
B.append(".label{flex:1;min-width:260px}")
B.append(".prompt{color:#aaa;font-size:12px;width:100%}")
B.append("audio{height:32px}")
B.append(".missing{color:#ff7b7b;font-size:12px}")
B.append(".note{color:#8f8;font-size:13px}")
B.append(".excl{background:#3a2a2a}")
B.append("</style></head><body>")
B.append("<h1>NetHack 3DS — «You hear…» sound pack</h1>")
B.append("<p class='note'>Прослушайте каждый звук. Пропущенные/проваленные отмечены. "
         "Если что-то не нравится — скажите slug, перегенерирую.</p>")

def item_block(slug, label, prompt, status_extra=""):
    mp3 = slug + "/0.mp3"
    exists = os.path.isfile(os.path.join(YOU, slug, "0.mp3"))
    B.append("<div class='item'>")
    B.append("<audio controls preload='none' src='%s'></audio>" % html.escape(mp3))
    B.append("<div class='label'>%s</div>" % html.escape(label))
    if not exists:
        B.append("<div class='missing'>⚠ нет файла</div>")
    if prompt:
        B.append("<div class='prompt'>prompt: %s</div>" % html.escape(prompt))
    B.append("</div>")

# Special curated items first
B.append("<h2>Уже было / отдельные ключи</h2>")
for slug, label, prompt, rel in [
    ("engraving", "engraving — царапанье по полу (новый ключ)", "", "../engraving/0.mp3"),
    ("footsteps", "footsteps — патруль стражи (переделан)", "Степающий ритмичный шаг сапог по камню, патруль в коридоре",
     "../footsteps/0.mp3"),
]:
    exists = os.path.isfile(os.path.join(os.path.dirname(YOU), rel[3:]))
    B.append("<div class='item'>")
    B.append("<audio controls preload='none' src='%s'></audio>" % html.escape(rel))
    B.append("<div class='label'>%s</div>" % html.escape(label))
    if not exists:
        B.append("<div class='missing'>⚠ нет файла</div>")
    B.append("</div>")

# Excluded: already exist as pack keys
B.append("<div class='item excl'><div class='label'>（пропущено — уже есть в soundpack/new): "
         "«a door open»→door-opens, «a door crash open»→door-smash, «a door unlock and open»→unlock, "
         "«someone counting money»→counting-coins, «splashing of a naiad»→splash, "
         "«barely hear faint splashing»→splash</div></div>")

for cat, slugs in CATS:
    B.append("<h2>%s</h2>" % html.escape(cat))
    for s in slugs:
        e = manifest.get(s)
        if not e:
            B.append("<div class='item'><div class='label'>%s</div>"
                     "<div class='missing'>⚠ нет в манифесте</div></div>" % html.escape(s))
            continue
        item_block(s, e.get("message", s), e.get("prompt", ""))

# Uncat'ed leftovers
left = [s for s in manifest if SMAP.get(s) is None]
if left:
    B.append("<h2>Прочее</h2>")
    for s in left:
        e = manifest[s]
        item_block(s, e.get("message", s), e.get("prompt", ""))

B.append("</body></html>")
with open(OUT_HTML, "w", encoding="utf-8") as f:
    f.write("\n".join(B))
print("wrote", OUT_HTML)

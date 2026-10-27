#!/usr/bin/env python3
"""Generate ElevenLabs sound effects for NetHack "You hear..." messages.

Each message becomes a subfolder under soundpack/new/you-hear/<slug>/0.mp3.
Outputs a manifest.json describing slug -> message / prompt / status.

API: POST https://api.elevenlabs.io/v1/sound-generation
Key: read from the skill's API_key.txt (or ELEVENLABS_API_KEY env).
"""
import json, os, sys, time, urllib.request, urllib.error

BASE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(BASE)
OUT = os.path.join(ROOT, "soundpack", "new", "you-hear")

KEY = os.environ.get("ELEVENLABS_API_KEY")
if not KEY:
    kp = os.path.join(ROOT, ".opencode", "skills", "sound-effects", "API_key.txt")
    with open(kp) as f:
        KEY = f.read().strip()

URL = "https://api.elevenlabs.io/v1/sound-generation"
MODEL = "eleven_text_to_sound_v2"
INFLUENCE = 0.3
DEFAULT_DUR = 2.0
HEADERS = {
    "xi-api-key": KEY,
    "Content-Type": "application/json",
    "Accept": "audio/mpeg",
}

# Each entry: (slug, message_label, prompt, duration_seconds)
ENTRIES = [
    # --- You (death omens) ---
    ("cwnannwn-howling", "You hear the howling of the CwnAnnwn...",
     "Eerie ghostly hounds howling in the far distance, chilling and otherworldly", 3.0),
    ("banshee-wailing", "You hear the wailing of the Banshee...",
     "A banshee's high, mournful wail echoing through a cavern, ominous and thin", 3.0),
    # --- Monsters: general ---
    ("noises-in-distance", "You hear some noises <in the distance>.",
     "Faint muffled scuffling and clatter from far away down a corridor", 2.0),
    ("wand-zap", "You hear a <nearby/distant> zap.",
     "A crisp electric zap crackle, like a magical wand discharge, with a faint buzz", 1.5),
    ("potion-chugging", "You hear a chugging sound.",
     "A greedy glugging, chugging drinking sound, liquid being gulped down", 1.5),
    ("monster-behind-boulder", "You hear a monster behind the boulder. Perhaps that's why you cannot move it.",
     "A heavy grinding push against stone followed by a low threatening growl", 2.0),
    # --- Pets ---
    ("pet-thunder", "You hear the rumble of distant thunder...",
     "Deep low rumble of thunder rolling across the distance, distant and ominous", 3.0),
    ("studio-audience-applause", "You hear the studio audience applaud!",
     "A burst of enthusiastic crowd applause and cheering", 2.0),
    # --- Werecreatures ---
    ("werewolf-howl", "You hear a <jackal/wolf> howling at the moon.",
     "A lone wolf howling long and mournfully at the moon in the night", 3.0),
    # --- Other monsters ---
    ("gelatinous-cube-slurp", "You hear a slurping sound.",
     "A wet, obscene slurping and squelching sound as something absorbs objects", 2.0),
    ("metallivore-crunch", "You hear a crunching sound.",
     "A hard metallic crunching and grinding, like biting into iron", 1.8),
    ("dwarf-digging-rock", "You hear crashing rock.",
     "Heavy rock being chiseled and crumbling, picks striking stone, rocks tumbling", 2.0),
    ("hole-crash-ground", "You hear something crash through the ground.",
     "A heavy object falling and crashing through crumbling earth and rock into a hole", 2.0),
    ("gas-spore-blast", "You hear a blast.",
     "A dull explosive blast with a rush of air and sharp crack", 1.5),
    ("door-destroyed-crash", "You hear a crashing sound.",
     "Wood and debris crashing down and splintering, a collapsing door", 1.8),
    ("vortex-rushing", "You hear a rushing noise nearby.",
     "A rushing whirlwind of air, a swirling wind gust whooshing past", 2.0),
    ("ochre-jelly-splat", "You hear a splat nearby.",
     "A wet squishy splat, like soft jelly slapping against stone", 1.5),
    ("armor-thud", "You hear a thud.",
     "A heavy dull thud as something solid drops to the floor", 1.2),
    ("shield-clank", "You hear a clank.",
     "A metallic clank, metal objects clattering and ringing on stone", 1.5),
    ("armor-cracking", "You hear a cracking sound.",
     "Metal cracking and splitting with a sharp strained creak", 1.5),
    ("shirt-ripping", "You hear a ripping sound.",
     "Cloth tearing and ripping apart with a long shred", 1.5),
    ("chewing-lizard", "You hear chewing.",
     "Slow wet chewing and munching, crunching through flesh", 1.8),
    ("cough-breath", "You hear a cough.",
     "A sudden dry strangled cough, a choked breath", 1.2),
    ("ghoul-gobble", "You hear an awful gobbling noise.",
     "Horrid gobbling and gagging as something devours rotten meat", 2.0),
    ("glob-slosh", "You hear a faint sloshing sound.",
     "A soft faint sloshing of liquid, like gel merging and shifting", 2.0),
    # --- Dungeon features ---
    ("slow-drip", "You hear a slow drip.",
     "A single water droplet dripping into a puddle, echoing in a cavern", 2.0),
    ("sink-gurgle", "You hear a gurgling noise.",
     "Water gurgling and glugging down a pipe or drain", 2.0),
    ("dishes-washed", "You hear dishes being washed!",
     "Clinking dishes and running water, someone washing up", 2.0),
    ("water-on-coins", "You hear water falling on coins.",
     "Water trickling and dripping onto a pile of metal coins", 2.0),
    ("fountain-bubbling", "You hear bubbling water.",
     "Bubbling and burbling fountain water flowing and splashing", 2.0),
    ("soda-fountain", "You hear a soda fountain!",
     "Fizzy carbonated soda dispensing with bubbling and hissing", 2.0),
    ("tumblers-click", "You hear <number> tumbler(s) click.",
     "A precise mechanical click of tumblers falling into place in a lock", 1.8),
    ("gears-turn", "You hear <number> gear(s) turn.",
     "Heavy metal gears grinding and ratcheting as they turn", 2.0),
    # --- Traps ---
    ("boulder-trap-rumble", "You hear rumbling in the distance.",
     "Distant low rumbling of a great heavy boulder rolling", 2.5),
    ("bowling", "You hear someone bowling.",
     "A heavy ball rolling and clattering, then pins struck, echoing", 2.5),
    ("rumbling-stop", "You hear a rumbling stop abruptly.",
     "A rolling rumble suddenly stops with a dull thud and silence", 2.0),
    ("boulder-crash", "You hear a loud crash!",
     "Two massive boulders colliding with a deep loud crash and rubble", 1.8),
    ("trapped-door-explosion", "You hear a distant explosion.",
     "A muffled distant explosion, a boom echoing from far away", 2.0),
    ("landmine-explosion", "Kaablamm! You hear an explosion in the distance.",
     "A loud distant explosion with a heavy boom and shockwave", 2.0),
    ("squeaky-board", "You hear a distant squeaking sound.",
     "A distant high-pitched squeak, like a rusty hinge or floorboard", 1.8),
    ("bear-web-roar", "You hear the roaring of a confused bear!",
     "A confused angry bear roaring and thrashing, muffled struggles", 2.5),
    ("bear-trap-roar", "You hear the roaring of an angry bear!",
     "A furious bear roaring in pain, snarling and thrashing", 2.5),
    ("deafening-roar", "You hear a deafening roar!",
     "An enormous deafening roar, a massive beast-like bellow", 2.0),
    ("magic-trap-howl", "You hear distant howling.",
     "Distant wind howling down a tunnel, eerie and hollow", 2.5),
    ("moon-howl", "You hear the moon howling at you.",
     "A weird high howling, laughter-like, echoing strangely", 2.0),
    ("arrow-trap-click", "You hear a loud click!",
     "A sharp loud mechanical click, a mechanism engaging", 1.0),
    ("dart-trap-click", "You hear a soft click!",
     "A soft quiet click, a small mechanism engaging", 1.0),
    ("trapdoor-twang", "You hear a twang followed by a thud.",
     "A metallic twang or spring snap, then a dull thud", 1.5),
    # --- Special rooms ---
    ("seal-bark", "You hear a sound reminiscent of a seal barking.",
     "A weird barking honk like a seal barking, echoing in a chamber", 1.8),
    ("elephant-peanut", "You hear a sound reminiscent of an elephant stepping on a peanut.",
     "A heavy crunching stomp, like a giant foot crushing a peanut", 1.5),
    ("doctor-doolittle", "You hear Doctor Doolittle!",
     "A cheerful crowd of animal noises: chatter, barks and chirps mixed", 2.0),
    ("searching-vault", "You hear someone searching.",
     "Rummaging and shuffling sounds, someone searching around", 2.0),
    ("scrooge", "You hear Ebenezer Scrooge!",
     "A grumpy miser counting coins with sour grumbling", 2.0),
    ("quarterback-play", "You hear the quarterback calling the play.",
     "A brisk voice calling out rapid numbers in a rhythmic cadence", 2.0),
    ("oracle-snoring", "You hear snoring snakes.",
     "Odd sibilant hissing snores, many snakes sleeping", 2.5),
    ("oracle-wind", "You hear a strange wind.",
     "A strange hollow wind whistling through unseen cracks", 2.5),
    ("oracle-ravings", "You hear convulsive ravings.",
     "Muffled frantic rambling and muttering, incoherent ravings", 2.0),
    ("oracle-woodchuck", "You hear someone say \"No more woodchucks!\"",
     "A gruff voice muttering mid a firm declaration", 1.5),
    ("oracle-zot", "You hear a loud ZOT!",
     "A sudden loud magical ZOT, a burst of arcane energy", 1.5),
    ("throne-conversation", "You hear the tones of courtly conversation.",
     "Polite refined chatter and murmured courtly conversation", 2.0),
    ("throne-sceptre", "You hear a sceptre pounded in judgment.",
     "A heavy wooden staff or sceptre pounding on stone, commanding", 1.5),
    ("throne-off-with-head", "Someone shouts \"Off with his/her head!\"",
     "A shrill commanding voice shouting a sentence, dramatic", 1.5),
    ("queen-cats", "You hear Queen Beruthiel's cats!",
     "Many angry cats hissing, yowling and meowing together", 2.5),
    ("temple-praising", "You hear someone praising <deity>.",
     "A reverent chanting voice praising the deity, echoing in a temple", 2.5),
    ("temple-beseeching", "You hear someone beseeching <deity>.",
     "A desperate pleading voice praying and beseeching", 2.5),
    ("temple-sacrifice", "You hear an animal carcass being offered in sacrifice.",
     "A heavy fleshy thud of a carcass being placed on an altar", 1.8),
    ("temple-donations", "You hear a strident plea for donations.",
     "A loud insistent wheedling voice pleading for coins", 2.0),
    ("beehive-buzzing", "You hear a low buzzing.",
     "A low deep buzzing hum of a large swarm of bees", 3.0),
    ("beehive-drone", "You hear an angry drone.",
     "An angry agitated buzzing drone, a disturbed beehive", 3.0),
    ("bees-bonnet", "You hear bees in your bonnet!",
     "Bees buzzing frantically and angrily all around your head", 2.5),
    ("bees-nonexistent-bonnet", "You hear bees in your (nonexistent) bonnet!",
     "Bees buzzing around your ears, frantic and close", 2.5),
    ("graveyard-quiet", "You suddenly realize it is unnaturally quiet.",
     "A sudden uncanny, unnaturally deep silence, no sound at all", 2.0),
    ("graveyard-hair-neck", "The <hair> on the back of your <neck> stands up.",
     "A subtle eerie chill, a low creepy shiver tone", 2.0),
    ("graveyard-hair-head", "The <hair> on your <head> seems to stand up.",
     "A weird unsettling whispery chill, hairs raising", 2.0),
    ("barracks-blades", "You hear blades being honed.",
     "Steel being sharpened, a whetstone scraping a blade rhythmically", 2.5),
    ("barracks-snoring", "You hear loud snoring.",
     "Loud heavy snoring from many sleeping soldiers", 3.0),
    ("barracks-dice", "You hear dice being thrown.",
     "Dice rattling on a wooden table, rolling and clicking", 1.5),
    ("general-macarthur", "You hear General MacArthur!",
     "A booming authoritative military voice barking orders", 1.5),
    ("swamp-mosquitoes", "You hear mosquitoes!",
     "A high thin whine of mosquitoes buzzing close by", 2.0),
    ("swamp-marsh-gas", "You smell marsh gas!",
     "A low bubbling gurgle of gas rising from murky water", 2.0),
    ("donald-duck", "You hear Donald Duck!",
     "An angry quacking cartoonish voice ranting in gibberish", 2.0),
    ("shop-shoplifters", "You hear someone cursing shoplifters.",
     "An irritated grumbling voice muttering curses under its breath", 1.8),
    ("shop-cash-register", "You hear the chime of a cash register.",
     "A classic cash register chime with a definitive ding", 1.0),
    ("neiman-marcus", "You hear Neiman and Marcus arguing!",
     "Two pompous voices arguing over prices, rapid back and forth", 2.0),
    # --- Items ---
    ("item-shatter", "You hear something shatter!",
     "Glass or ceramic shattering into pieces with a sharp crash", 1.5),
    ("boulder-crumbling", "You hear a crumbling sound.",
     "Stone crumbling and grinding, a boulder breaking apart", 2.0),
    # --- Scroll ---
    ("scare-monster-laugh-near", "You hear maniacal laughter close by.",
     "A close maniacal high-pitched cackling laughter", 2.0),
    ("scare-monster-laugh-far", "You hear maniacal laughter in the distance.",
     "Maniacal cackling laughter echoing from far away", 2.5),
    ("scare-monster-wail-near", "You hear sad wailing close by.",
     "A close sad mournful wailing, weeping and keening", 2.5),
    ("scare-monster-wail-far", "You hear sad wailing in the distance.",
     "Sad mournful wailing drifting in from far off", 3.0),
    # --- Container ---
    ("container-muffled-shatter", "You hear a muffled shatter.",
     "A muffled shattering of glass inside a container, dulled", 1.5),
    ("container-muffled-crack", "You hear a muffled cracking.",
     "A muffled cracking of an eggshell inside a container", 1.5),
    # --- Stethoscope ---
    ("hellfire-crackling", "You hear the crackling of hellfire.",
     "Deep ominous crackling of fire with a sinister low roar", 2.5),
    ("heartbeat", "You hear your heart beat.",
     "A slow strong heartbeat, thumping and deep", 2.0),
    ("faint-typing", "You hear a faint typing noise.",
     "Faint rapid clicking and tapping, like typing on a keyboard", 2.0),
    ("secret-door-hollow", "You hear a hollow sound. This must be a secret <door/passage>!",
     "A hollow knocking on a wall, empty and echoing behind it", 1.5),
    ("its-dead-jim", "You hear a voice say, \"It's dead, Jim.\"",
     "A calm matter-of-fact voice speaking a short sentence", 1.5),
    # --- Bugle ---
    ("bugle-reveille", "You hear a bugle playing reveille!",
     "A bright clear bugle playing a military reveille call", 3.0),
    ("battle-gear-rattle", "You hear the rattle of battle gear being readied.",
     "Metal armor and weapons rattling and clinking as troops arm up", 2.5),
    # --- Dungeon environment ---
    ("moat-hissing-gas", "You hear hissing gas.",
     "A sharp hissing of steam, water boiling and evaporating", 2.0),
    ("moat-soft-crackling", "You hear a soft crackling.",
     "Soft gentle crackling of ice forming and freezing", 2.0),
    ("plane-water-crackling", "You hear a crackling sound.",
     "A sharp crackling of water freezing solid with ice snaps", 2.0),
]


def generate(slug, prompt, duration):
    payload = {
        "text": prompt,
        "model_id": MODEL,
        "prompt_influence": INFLUENCE,
        "duration_seconds": duration,
    }
    req = urllib.request.Request(URL, data=json.dumps(payload).encode(),
                                 headers=HEADERS, method="POST")
    with urllib.request.urlopen(req, timeout=120) as resp:
        return resp.read()


def main():
    only = sys.argv[1:] if len(sys.argv) > 1 else None
    os.makedirs(OUT, exist_ok=True)
    manifest = {}
    fail = []
    for slug, label, prompt, duration in ENTRIES:
        if only and slug not in only:
            continue
        path = os.path.join(OUT, slug, "0.mp3")
        os.makedirs(os.path.dirname(path), exist_ok=True)
        if os.path.isfile(path) and os.path.getsize(path) > 1000:
            print("skip", slug)
            manifest[slug] = {"message": label, "prompt": prompt,
                              "duration": duration, "status": "existing"}
            continue
        ok = False
        for attempt in range(4):
            try:
                data = generate(slug, prompt, duration)
                with open(path, "wb") as f:
                    f.write(data)
                print("ok", slug, len(data))
                manifest[slug] = {"message": label, "prompt": prompt,
                                  "duration": duration, "status": "ok",
                                  "bytes": len(data)}
                ok = True
                break
            except urllib.error.HTTPError as e:
                body = e.read().decode(errors="replace")[:200]
                print("HTTP", e.code, slug, body)
                if e.code == 429:
                    time.sleep(5 * (attempt + 1))
                else:
                    break
            except Exception as e:
                print("ERR", slug, repr(e))
                time.sleep(3)
        if not ok:
            manifest[slug] = {"message": label, "prompt": prompt,
                              "duration": duration, "status": "failed"}
            fail.append(slug)
    with open(os.path.join(OUT, "manifest.json"), "w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=2, ensure_ascii=False)

    # Rework the guard-patrol footsteps sound (existing "footsteps" key).
    fs_slug = "footsteps"
    fs_path = os.path.join(ROOT, "soundpack", "new", fs_slug, "0.mp3")
    if not only or fs_slug in only:
        os.makedirs(os.path.dirname(fs_path), exist_ok=True)
        ok = False
        for attempt in range(4):
            try:
                data = generate(
                    fs_slug,
                    "Steady rhythmic heavy booted footsteps marching on stone corridors, a guard on patrol, medium distance",
                    3.0)
                with open(fs_path, "wb") as f:
                    f.write(data)
                print("ok footsteps", len(data))
                ok = True
                break
            except urllib.error.HTTPError as e:
                print("HTTP", e.code, fs_slug, e.read().decode(errors="replace")[:200])
                if e.code == 429:
                    time.sleep(5 * (attempt + 1))
                else:
                    break
            except Exception as e:
                print("ERR", fs_slug, repr(e))
                time.sleep(3)

    print("DONE ok=%d failed=%d" % (len(manifest) - len(fail), len(fail)))
    if fail:
        print("failed:", fail)


if __name__ == "__main__":
    main()

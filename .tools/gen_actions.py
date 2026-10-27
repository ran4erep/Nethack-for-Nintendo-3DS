#!/usr/bin/env python3
"""Generate pray / loot / dualwield action sounds into soundpack/new/<key>/0.mp3."""
import json, os, time, urllib.request, urllib.error

BASE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(BASE)
KEY = os.environ.get("ELEVENLABS_API_KEY")
if not KEY:
    kp = os.path.join(ROOT, ".opencode", "skills", "sound-effects", "API_key.txt")
    with open(kp) as f:
        KEY = f.read().strip()

URL = "https://api.elevenlabs.io/v1/sound-generation"
HEADERS = {"xi-api-key": KEY, "Content-Type": "application/json", "Accept": "audio/mpeg"}

ACTIONS = [
    ("pray", "Prayer",
     "A soft reverent prayer, low chanting and murmurs in a quiet temple, solemn and calm",
     3.0),
    ("loot", "Loot",
     "Rummaging through a container and lifting its lid, items shifting and clinking, quick shuffling",
     2.5),
    ("dualwield", "Dual wield",
     "Two weapons drawn and readied, metallic swish of blades crossing, a crisp combat flourish",
     2.0),
]


def generate(text, duration):
    payload = {"text": text, "model_id": "eleven_text_to_sound_v2",
               "prompt_influence": 0.3, "duration_seconds": duration}
    req = urllib.request.Request(URL, data=json.dumps(payload).encode(),
                                 headers=HEADERS, method="POST")
    with urllib.request.urlopen(req, timeout=120) as resp:
        return resp.read()


for slug, label, prompt, dur in ACTIONS:
    out = os.path.join(ROOT, "soundpack", "new", slug)
    os.makedirs(out, exist_ok=True)
    path = os.path.join(out, "0.mp3")
    ok = False
    for attempt in range(4):
        try:
            data = generate(prompt, dur)
            with open(path, "wb") as f:
                f.write(data)
            print("ok", slug, len(data))
            ok = True
            break
        except urllib.error.HTTPError as e:
            print("HTTP", e.code, slug, e.read().decode(errors="replace")[:200])
            if e.code == 429:
                time.sleep(5 * (attempt + 1))
            else:
                break
        except Exception as e:
            print("ERR", slug, repr(e))
            time.sleep(3)
    if not ok:
        print("FAILED", slug)

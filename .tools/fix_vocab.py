#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Repair leaked placeholders and add missing vocabulary translations
(colors, verbs, role/gender/race adjectives, greetings, rank titles)."""

import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import translate_messages as tm
import autotranslate

TSV = tm.DEFAULT_TSV
PLACEHOLDER_RE = autotranslate.PLACEHOLDER_RE


def translate_robust(text):
    """Translate while protecting %-directives with [[i]] markers."""
    dirs = []
    def rep(m):
        dirs.append(m.group(0))
        return "[[%d]]" % (len(dirs) - 1)
    protected = PLACEHOLDER_RE.sub(rep, text)
    ru = autotranslate.translate_line(protected)
    if ru is None:
        return None
    for i, d in enumerate(dirs):
        tok = "[[%d]]" % i
        ru = ru.replace(tok, d)
    return re.sub(r"\s+", " ", ru).strip()


def main():
    rows = tm.load_tsv(TSV)

    # ---- Part A: repair leaked << >> rows ----
    leak = [k for k, v in rows.items() if '<<' in v or '>>' in v]
    print("leaked rows:", len(leak), flush=True)
    repaired = 0
    for k in leak:
        nv = translate_robust(k)
        if nv and tm.placeholder_count(nv) == tm.placeholder_count(k):
            rows[k] = nv
            repaired += 1
        time.sleep(0.1)
    print("repaired:", repaired, flush=True)

    # ---- tense fix: «Вы услышите» -> «Вы услышали» ----
    tense = 0
    for k in rows:
        if rows[k].startswith("Вы услышите "):
            rows[k] = "Вы услышали " + rows[k][len("Вы услышите "):]
            tense += 1
    print("tense fixed:", tense, flush=True)

    # ---- Part B: vocabulary to add ----
    vocab = []
    # greetings (from Hello())
    vocab += ["Hello", "Salutations", "Aloha", "Velkommen",
              "Konnichi wa", "Irasshaimase", "Hallo"]
    # verbs used as arguments in message templates
    vocab += ["see", "feel", "sense", "hear", "smell", "touch"]
    # gem / object color descriptors
    vocab += ["blue", "red", "yellow", "green", "black", "white", "orange",
              "purple", "violet", "pink", "tan", "gray", "grey", "brown",
              "clear", "silver", "gold", "topaz", "emerald", "ruby",
              "sapphire", "amethyst", "obsidian", "opal", "jade", "pearl",
              "beryl", "agate", "garnet", "diamond", "zircon", "fluorite",
              "jasper", "onyx", "jet", "turquoise", "aquamarine"]
    # gender / race / alignment adjectives used in the character description
    vocab += ["male", "female", "human", "dwarvish", "elven", "gnomish",
              "orcish", "lawful", "neutral", "chaotic", "adrift",
              "dwarf", "elf", "gnome", "orc"]
    # rank titles & role names from role.c
    role_src = open(os.path.join(tm.ROOT, "src", "role.c"),
                    encoding="utf-8", errors="replace").read()
    vocab += re.findall(r'\{\s*"([^"]+)"\s*,\s*0\s*\}', role_src)

    need = []
    seen = set()
    for w in vocab:
        w = w.strip()
        if not w or w in rows or w in seen:
            continue
        seen.add(w)
        need.append(w)
    print("vocab to translate:", len(need), flush=True)

    added = 0
    for w in need:
        ru = autotranslate.translate_line(w)
        if ru and ru.lower() != w.lower():
            rows[w] = ru
            added += 1
        time.sleep(0.08)
    print("vocab added:", added, flush=True)

    with open(TSV, "w", encoding="utf-8") as f:
        f.write("# EN\tRU — заполни RU-колонку, затем запусти:\n")
        f.write("# python .tools/translate_messages.py --apply\n")
        for k in sorted(rows):
            f.write(f"{k}\t{rows[k]}\n")
    print("tsv written", flush=True)


if __name__ == "__main__":
    main()

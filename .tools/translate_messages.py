#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
translate_messages.py — find untranslated NetHack game messages and add
Russian translations for the 3DS port.

The 3DS port translates text with exact-match dictionaries in
sys/3ds/3dsi18n.c (main "dictionary[]") plus the item/monster name
dictionaries (3dsitems.h / 3dsmonsters.h).  This tool follows that exact
pattern: it extracts every player-facing message template from src/*.c,
rebuilds the full dictionary key (including the "You "/"Your "/"The "/
"There "/"You feel "/... prefixes that pline.c prepends at runtime),
skips strings that are already translated, then merges in translations from
a TSV table (EN<TAB>RU).  It can emit a C snippet or apply the new entries
straight into 3dsi18n.c.

Translation table: .tools/messages_ru.tsv (UTF-8, "#" comments).
Rows whose Russian cell is empty or whose %-placeholders do not match the
English template are left in pending_ru.tsv instead of being applied.

Usage:
    python .tools/translate_messages.py            # report + write pending
    python .tools/translate_messages.py --apply    # insert entries into C
    python .tools/translate_messages.py --tsv custom.tsv
"""

import argparse
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC_DIR = os.path.join(ROOT, "src")
I18N_C = os.path.join(ROOT, "sys", "3ds", "3dsi18n.c")
ITEMS_H = os.path.join(ROOT, "sys", "3ds", "3dsitems.h")
MONSTERS_H = os.path.join(ROOT, "sys", "3ds", "3dsmonsters.h")
DEFAULT_TSV = os.path.join(ROOT, ".tools", "messages_ru.tsv")
PENDING_TSV = os.path.join(ROOT, ".tools", "pending_ru.tsv")
REVIEW_TSV = os.path.join(ROOT, ".tools", "review_ru.tsv")
ENTRIES_C = os.path.join(ROOT, ".tools", "new_message_entries.c")

# pline.c prepends these prefixes before the caller's literal reaches vpline().
PREFIXES = {
    "You": ["You "],
    "Your": ["Your "],
    "There": ["There "],
    "pline_The": ["The "],
    "You_feel": ["You feel ", "You dream that you feel "],
    "You_cant": ["You can't "],
    "You_hear": ["You hear ", "You barely hear ", "You dream that you hear "],
    "You_see": ["You see ", "You sense ", "You dream that you see "],
    "You_dream": ["You dream "],
    "verbalize": ['"'],
}
NO_PREFIX = {
    "pline", "pline_dir", "pline_xy", "pline_mon", "explanation",
    "Norep", "quickmsg", "msmsg", "mvitalize",
}

CALL_RE = re.compile(r'\b([A-Za-z_][A-Za-z0-9_]*)\s*\(\s*"((?:[^"\\]|\\.)*)"',
                     re.DOTALL)
PLACEHOLDER_RE = re.compile(r'%(?:[-+ #0-9.*]*[hlLzjt]*[diuoxXfFeEgGscpn%])')


def c_unescape(s):
    """Turn a C string literal body into the logical string (\\" -> ",
    \\\\ -> \\, \\n -> newline, \\t -> tab)."""
    out = []
    i, n = 0, len(s)
    esc = {'n': '\n', 't': '\t', 'r': '\r', '\\': '\\', '"': '"',
           "'": "'", '0': '\0'}
    while i < n:
        if s[i] == '\\' and i + 1 < n and s[i + 1] in esc:
            out.append(esc[s[i + 1]])
            i += 2
        else:
            out.append(s[i])
            i += 1
    return ''.join(out)


def c_escape(s):
    return s.replace('\\', '\\\\').replace('"', '\\"')

MESSAGE_FUNCS = set(PREFIXES) | NO_PREFIX

# Standalone words/terms that are looked up through nh_tr() as arguments or
# labels (status line, object colour descriptors, verbs, greetings, ranks).
GLOSSARY = [
    "Hello", "Salutations", "Aloha", "Velkommen", "Konnichi wa",
    "Irasshaimase", "Hallo",
    "see", "feel", "sense", "hear", "smell", "touch",
    "blue", "red", "yellow", "green", "black", "white", "orange",
    "purple", "violet", "pink", "tan", "gray", "grey", "brown",
    "clear", "silver", "gold", "topaz", "emerald", "ruby",
    "sapphire", "amethyst", "obsidian", "opal", "jade", "pearl",
    "beryl", "agate", "garnet", "diamond", "zircon", "fluorite",
    "jasper", "onyx", "jet", "turquoise", "aquamarine",
    "male", "female", "human", "dwarvish", "elven", "gnomish",
    "orcish", "lawful", "neutral", "chaotic", "adrift",
    "Level:", "Level", "XP", "AC", "Gold", "HP", "Power",
    "an explosion", "some noises", " in the distance",
    "a door being smashed", "a door exploding", "feel the shock of",
]


def strip_comments(text):
    """Remove C comments, leaving strings intact."""
    out = []
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if c == '/' and i + 1 < n and text[i + 1] == '*':
            j = text.find('*/', i + 2)
            i = n if j == -1 else j + 2
            continue
        if c == '/' and i + 1 < n and text[i + 1] == '/':
            j = text.find('\n', i)
            i = n if j == -1 else j
            continue
        if c == '"':
            out.append(c)
            i += 1
            while i < n:
                if text[i] == '\\':
                    out.append(text[i:i + 2])
                    i += 2
                    continue
                if text[i] == '"':
                    out.append('"')
                    i += 1
                    break
                out.append(text[i])
                i += 1
            continue
        out.append(c)
        i += 1
    return ''.join(out)


def extract_keys():
    """Return Counter of full dictionary keys (template, with prefixes)."""
    from collections import Counter
    keys = Counter()
    for name in sorted(os.listdir(SRC_DIR)):
        if not name.endswith('.c'):
            continue
        text = strip_comments(open(os.path.join(SRC_DIR, name),
                                   encoding='utf-8', errors='replace').read())
        for m in CALL_RE.finditer(text):
            fn, lit = m.group(1), c_unescape(m.group(2))
            if fn in NO_PREFIX:
                full = [lit]
            elif fn in PREFIXES:
                full = [p + lit for p in PREFIXES[fn]]
                if fn == 'verbalize':
                    full = [k + '"' for k in full]
            else:
                continue
            for k in full:
                if '\n' in k:
                    continue
                keys[k] += 1
    for w in GLOSSARY:
        keys[w] += 1
    # rank titles / role names from role.c (e.g. { "Novice", 0 },)
    rolef = os.path.join(ROOT, "src", "role.c")
    if os.path.exists(rolef):
        for w in re.findall(r'\{\s*"([^"]+)"\s*,\s*0\s*\}',
                            open(rolef, encoding="utf-8", errors="replace").read()):
            keys[w] += 1
    return keys


def load_existing():
    """Return {English_key: ru_utf8} already present in the 3DS dictionaries."""
    existing = {}
    pat = re.compile(r'\{\s*"((?:[^"\\]|\\.)*)"\s*,\s*"((?:[^"\\]|\\.)*)"')
    for path in (I18N_C, ITEMS_H, MONSTERS_H):
        if not os.path.exists(path):
            continue
        for line in open(path, encoding='utf-8', errors='replace'):
            m = pat.search(line)
            if m:
                existing[c_unescape(m.group(1))] = c_unescape(m.group(2))
    return existing


def load_tsv(path):
    rows = {}
    if not os.path.exists(path):
        return rows
    for ln, line in enumerate(open(path, encoding='utf-8'), 1):
        line = line.rstrip('\n').rstrip('\r')
        if not line or line.startswith('#'):
            continue
        if '\t' not in line:
            print(f"warn: {path}:{ln}: no TAB, skipped")
            continue
        en, ru = line.split('\t', 1)
        rows[en] = ru.strip()
    return rows


def placeholder_count(s):
    return len(PLACEHOLDER_RE.findall(s))


def write_tsv(path, rows):
    with open(path, 'w', encoding='utf-8') as f:
        f.write("# EN\tRU — fill the RU column, then re-run\n")
        f.write("# python .tools/translate_messages.py --apply\n")
        for en, count in rows:
            f.write(f"{en}\t\n")


DREAM_RE = re.compile(r'^You dream that you (feel|hear|see) (.*)$')
BARELY_RE = re.compile(r'^You barely hear (.*)$')


def derive_variants(table, existing_ru, missing_keys):
    """Auto-generate RU for "You dream that you feel/hear/see X" and
    "You barely hear X" variants from already-translated bases."""
    rows = {}
    for k in missing_keys:
        m = DREAM_RE.match(k)
        if m:
            verb, rest = m.group(1), m.group(2)
            base = 'You %s %s' % (verb, rest)
            ru = table.get(base) or existing_ru.get(base)
            if ru and not ru.startswith('Тебе снится'):
                rows[k] = 'Тебе снится, что ' + ru[:1].lower() + ru[1:]
            continue
        m = BARELY_RE.match(k)
        if m:
            base = 'You hear ' + m.group(1)
            ru = table.get(base) or existing_ru.get(base)
            if ru:
                if ru.startswith('Ты слышишь '):
                    rows[k] = 'Ты едва слышишь ' + ru[len('Ты слышишь '):]
                else:
                    rows[k] = 'Ты едва ' + ru[:1].lower() + ru[1:]
    return rows


def append_tsv_rows(path, rows):
    """rows: dict en->ru, appended (idempotent)."""
    have = set(load_tsv(path))
    added = 0
    with open(path, 'a', encoding='utf-8') as f:
        for en, ru in rows.items():
            if en in have:
                continue
            f.write(f"{en}\t{ru}\n")
            added += 1
    return added


def make_c_entries(translated):
    """translated: list of (en, ru) sorted by frequency. Return C text."""
    lines = []
    for en, ru in translated:
        lines.append(f'    {{ "{c_escape(en)}", "{c_escape(ru)}", "" }},')
    return '\n'.join(lines)


def refresh_to_c(table):
    """Update existing dictionary entries whose translation changed in the TSV."""
    pat = re.compile(r'(\{\s*"((?:[^"\\]|\\.)*)"\s*,\s*"((?:[^"\\]|\\.)*)"\s*,\s*""\s*\},)')
    with open(I18N_C, encoding='utf-8') as f:
        lines = f.read().splitlines()
    changed = 0
    for i, ln in enumerate(lines):
        m = pat.search(ln)
        if not m:
            continue
        en = c_unescape(m.group(2))
        if en not in table:
            continue
        ru = table[en]
        if not ru or ru == en:
            continue
        if placeholder_count(en) != placeholder_count(ru):
            continue
        old_ru = c_unescape(m.group(3))
        if old_ru == ru:
            continue
        lines[i] = ln.replace(m.group(1), '    { "%s", "%s", "" },' % (c_escape(en), c_escape(ru)))
        changed += 1
    if changed:
        with open(I18N_C, 'w', encoding='utf-8') as f:
            f.write('\n'.join(lines) + '\n')
    return changed


def apply_to_c(entries_text):
    with open(I18N_C, encoding='utf-8') as f:
        lines = f.read().splitlines()
    start = None
    for i, ln in enumerate(lines):
        if 'static struct tr_item dictionary[] = {' in ln:
            start = i
            break
    if start is None:
        sys.exit("fatal: dictionary[] not found in 3dsi18n.c")
    end = None
    for j in range(start + 1, len(lines)):
        if lines[j].strip() == '};':
            end = j
            break
    if end is None:
        sys.exit("fatal: end of dictionary[] not found")
    block = lines[start + 1:end]
    block.extend(entries_text.splitlines())
    new_lines = lines[:start + 1] + block + lines[end:]
    with open(I18N_C, 'w', encoding='utf-8') as f:
        f.write('\n'.join(new_lines) + '\n')


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--apply', action='store_true',
                    help='write new entries into 3dsi18n.c')
    ap.add_argument('--autofill', action='store_true',
                    help='fill every remaining key with a placeholder row so '
                         'nothing stays in pending (identity rows are not '
                         'inserted into the C dictionary)')
    ap.add_argument('--refresh', action='store_true',
                    help='update existing dictionary entries whose translations '
                         'changed in the TSV')
    ap.add_argument('--apply-all', action='store_true',
                    help='insert every translated TSV row (including words and '
                         'wiki-listed messages that are not direct call sites)')
    ap.add_argument('--tsv', default=DEFAULT_TSV)
    args = ap.parse_args()

    keys = extract_keys()
    existing_ru = load_existing()
    existing = set(existing_ru)
    table = load_tsv(args.tsv)
    # all keys not yet present in the C dictionaries (they may already have a
    # row in the TSV — including autofilled identity rows that need upgrading)
    missing = {k: c for k, c in keys.items() if k not in existing}

    # 1) auto-derive "You dream…"/"You barely hear…" variants from bases
    derived = derive_variants(table, existing_ru, missing)
    if derived:
        n = append_tsv_rows(args.tsv, derived)
        table = load_tsv(args.tsv)
        print(f"derived variant translations: {n}")

    # 2) optional identity autofill so nothing remains without a TSV row
    autofilled = 0
    if args.autofill:
        auto = {k: k for k in missing if k not in table}
        if auto:
            autofilled = append_tsv_rows(args.tsv, auto)
            table = load_tsv(args.tsv)

    translated = []   # (en, ru) ready to insert
    invalid = []      # (en, ru, reason)
    pending = []      # (en, count) still untranslated

    for en, count in sorted(missing.items(), key=lambda kv: (-kv[1], kv[0])):
        ru = table.get(en)
        if not ru or ru == en:      # empty or identity rows are not applied
            pending.append((en, count))
            continue
        if placeholder_count(en) != placeholder_count(ru):
            invalid.append((en, ru, "placeholder mismatch"))
            continue
        translated.append((en, ru))

    print(f"message keys found:        {len(keys)}")
    print(f"already translated:        {len(keys) - len(missing)}")
    print(f"untranslated in source:    {len(missing)}")
    print(f"  translations available:  {len(translated)}")
    print(f"  skipped (bad/empty):     {len(invalid)}")
    print(f"  still pending:           {len(pending)}")
    if autofilled:
        print(f"  autofilled (identity):   {autofilled}")

    # pending_ru.tsv = keys with NO row in the TSV at all (real backlog)
    rowless = [(en, c) for en, c in pending if en not in table]
    write_tsv(PENDING_TSV, rowless)
    # review_ru.tsv = keys whose TSV row is still only an identity placeholder.
    # Pure format/debug skeletons (e.g. "%s %d;", "set_wall_state: …") are
    # not player-facing and cannot be translated without breaking printf(),
    # so they are excluded from the review list.
    SKIP_KEYS = {
        '%s', '%s %s', '%s %s %s', '%s!', '%s!!', '%s.', '%s,', '%s:',
        '%s...', '%s %s.', '%s %s!', '%s %s %s!', '%s %s %s.',
        '%s %s...', '%s%s%s%s', '%s %d;',
        '%s %ld %s %s %s%s."', '"%d0%d %ld%d1 0%d%d0"%s',
        'set_wall_state: %s @ (%d,%d) %s%s%s%s',
    }
    review = [(en, c) for en, c in pending if table.get(en) == en
              and en not in SKIP_KEYS]
    with open(REVIEW_TSV, 'w', encoding='utf-8') as f:
        f.write("# EN\tRU — identity placeholders, fill with real translations\n")
        for en, c in review:
            f.write(f"{en}\t\n")

    if translated:
        entries = make_c_entries(translated)
        with open(ENTRIES_C, 'w', encoding='utf-8') as f:
            f.write(entries + '\n')
        print(f"wrote {ENTRIES_C} ({len(translated)} entries)")
        if args.apply:
            apply_to_c(entries)
            print(f"applied {len(translated)} entries into {I18N_C}")
    else:
        print("nothing new to add")

    if args.refresh:
        n = refresh_to_c(table)
        if n:
            print(f"refreshed {n} existing entries in {I18N_C}")

    if args.apply_all:
        extra = []
        for en, ru in table.items():
            if en in existing:
                continue
            if not ru or ru == en:
                continue
            if placeholder_count(en) != placeholder_count(ru):
                continue
            extra.append((en, ru))
        if extra:
            entries = make_c_entries(extra)
            apply_to_c(entries)
            print(f"applied {len(extra)} extra table entries into {I18N_C}")

    if rowless:
        print(f"untranslated list written to {PENDING_TSV} ({len(rowless)} rows)")

    if invalid:
        print("invalid rows (placeholders mismatch):")
        for en, ru, why in invalid[:10]:
            print(f"  {en!r} -> {ru!r} ({why})")


if __name__ == '__main__':
    main()

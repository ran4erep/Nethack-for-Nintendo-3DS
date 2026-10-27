#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
autotranslate.py — translate the remaining identity (untranslated) rows in
messages_ru.tsv via the free Google Translate web endpoint, then write the
results back into the TSV.  Run translate_messages.py --apply afterwards.

Usage:
    python .tools/autotranslate.py [--limit N] [--dry]
"""

import argparse
import json
import os
import re
import sys
import time
import urllib.parse
import urllib.request

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import translate_messages as tm

PLACEHOLDER_RE = re.compile(r'%(?:[-+ #0-9.*]*[hlLzjt]*[diuoxXfFeEgGscpn%])')

UA = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36"
URL = "https://translate.googleapis.com/translate_a/single?client=gtx&sl=en&tl=ru&dt=t&q="


def translate_line(text, tries=3):
    q = urllib.parse.quote(text)
    for attempt in range(tries):
        try:
            req = urllib.request.Request(URL + q, headers={"User-Agent": UA})
            with urllib.request.urlopen(req, timeout=20) as r:
                data = json.loads(r.read().decode("utf-8"))
            parts = []
            for seg in data[0]:
                if seg[0]:
                    parts.append(seg[0])
            return "".join(parts)
        except Exception:
            if attempt == tries - 1:
                return None
            time.sleep(1.5 * (attempt + 1))


def protect(s):
    dirs = []
    def rep(m):
        dirs.append(m.group(0))
        return "<<%d>>" % (len(dirs) - 1)
    protected = PLACEHOLDER_RE.sub(rep, s)
    return protected, dirs


def restore(s, dirs):
    for i, d in enumerate(dirs):
        tok = "<<%d>>" % i
        if tok in s:
            s = s.replace(tok, d)
        else:
            # translator dropped/merged the token: re-append in order
            s = s + d
    return re.sub(r"\s+", " ", s).strip()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--limit", type=int, default=0)
    ap.add_argument("--dry", action="store_true")
    ap.add_argument("--tsv", default=tm.DEFAULT_TSV)
    args = ap.parse_args()

    keys = tm.extract_keys()
    existing_ru = tm.load_existing()
    table = tm.load_tsv(args.tsv)
    targets = []
    for k in keys:
        if k in existing_ru:
            continue
        if table.get(k) == k:      # identity placeholder only
            targets.append(k)
    targets.sort()
    if args.limit:
        targets = targets[:args.limit]
    print("remaining identity rows to translate:", len(targets), flush=True)

    done = 0
    failed = []
    for idx, en in enumerate(targets):
        protected, dirs = protect(en)
        ru = translate_line(protected)
        if ru is None:
            failed.append(en)
            continue
        ru = restore(ru, dirs)
        if not ru or tm.placeholder_count(ru) != len(dirs):
            # translator mangled placeholders: keep identity rather than break
            failed.append(en)
            continue
        table[en] = ru
        done += 1
        if (idx + 1) % 150 == 0:
            print(f"  {idx+1}/{len(targets)}", flush=True)
        time.sleep(0.15)

    print("translated:", done, " failed:", len(failed), flush=True)
    if not args.dry and done:
        with open(args.tsv, "w", encoding="utf-8") as f:
            f.write("# EN\tRU — заполни RU-колонку, затем запусти:\n")
            f.write("# python .tools/translate_messages.py --apply\n")
            for en in sorted(table):
                f.write(f"{en}\t{table[en]}\n")
        print("tsv updated", flush=True)
    if failed:
        with open(tm.REVIEW_TSV, "w", encoding="utf-8") as f:
            f.write("# still untranslated after autotranslate\n")
            for en in failed:
                f.write(f"{en}\t\n")
        print("left", len(failed), "rows in review", flush=True)


if __name__ == "__main__":
    main()

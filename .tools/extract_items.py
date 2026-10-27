import re, os

with open('include/objects.h', 'r', encoding='utf-8', errors='ignore') as f:
    lines = f.readlines()

items = []
# Patterns:
# PROJECTILE("arrow", NoDes, ...)
# OBJECT(OBJ("strange object", NoDes), ...)
# DRGN_ARMR("gray dragon scale mail", ...)
# FOOD("meatball", ...)
for line in lines:
    m = re.search(r'^\s*(?:OBJECT\s*\(\s*OBJ\s*\(\s*"([^"]+)"\s*,\s*(NoDes|"([^"]*)")|([A-Z_]+)\s*\(\s*"([^"]+)"(?:\s*,\s*(NoDes|"([^"]*)"))?)', line)
    if m:
        if m.group(1):
            name = m.group(1)
            desc = m.group(3) if m.group(3) else ''
        else:
            name = m.group(5)
            desc = m.group(7) if m.group(7) else ''
        items.append((name, desc))

print(f"Total extracted: {len(items)}")
with open('.tools/extracted_items.txt', 'w', encoding='utf-8') as out:
    for n, d in items:
        out.write(f"{n}\t{d}\n")

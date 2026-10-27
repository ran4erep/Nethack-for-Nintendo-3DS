import sys
sys.path.insert(0, ".tools")
from gen_monsters_header import MONSTER_TRANSLATIONS

with open(".tools/monsters_list.txt", "r", encoding="utf-8") as f:
    monsters = [line.strip() for line in f if line.strip()]

missing = [m for m in monsters if m not in MONSTER_TRANSLATIONS]
print(f"Total from monsters_list: {len(monsters)}")
print(f"Missing count: {len(missing)}")
if missing:
    for m in missing:
        print(f'    "{m}": "",')

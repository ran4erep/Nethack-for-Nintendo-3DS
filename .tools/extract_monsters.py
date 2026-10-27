import re

with open("include/monsters.h", "r", encoding="utf-8", errors="ignore") as f:
    content = f.read()

names = []
for m in re.finditer(r'NAM\("([^"]+)"\)', content):
    names.append(m.group(1))

for m in re.finditer(r'NAMS\("([^"]*)",\s*"([^"]*)",\s*"([^"]*)"\)', content):
    for g in m.groups():
        if g:
            names.append(g)

unique_names = sorted(list(set(names)))
print(f"Total entries: {len(names)}, Unique names: {len(unique_names)}")
with open(".tools/monsters_list.txt", "w", encoding="utf-8") as out:
    for n in unique_names:
        out.write(n + "\n")

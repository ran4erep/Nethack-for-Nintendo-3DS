import re

with open("include/monsters.h", "r", encoding="utf-8", errors="ignore") as f:
    lines = f.readlines()

current_section = "General"
monsters_by_section = {}

for line in lines:
    sec_match = re.match(r'^\s*/\*\s*([^*]+?)\s*\*/\s*$', line)
    if sec_match and not line.strip().startswith("/* NetHack"):
        s = sec_match.group(1).strip()
        if len(s) < 30 and not s.startswith("Rule") and not s.startswith("TODO") and not s.startswith("name"):
            current_section = s
    m_match = re.search(r'NAM\("([^"]+)"\)', line)
    if m_match:
        name = m_match.group(1)
        monsters_by_section.setdefault(current_section, []).append(name)
    m2_match = re.search(r'NAMS\("([^"]*)",\s*"([^"]*)",\s*"([^"]*)"\)', line)
    if m2_match:
        for g in m2_match.groups():
            if g:
                monsters_by_section.setdefault(current_section, []).append(g)

for sec, mlist in monsters_by_section.items():
    print(f"[{sec}] ({len(mlist)}): {mlist}")

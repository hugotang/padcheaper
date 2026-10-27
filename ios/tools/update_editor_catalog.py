#!/usr/bin/env python3
"""Extract only public card facts for the offline formation editor.

Use downloaded mon_ja.json and skill_ja.json; never execute upstream scripts.
The runtime independently validates every value against the loaded game catalog.
"""
import argparse
import hashlib
import json
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument("directory", type=Path)
parser.add_argument("--revision", required=True)
args = parser.parse_args()
cards_path = args.directory / "mon_ja.json"
skills_path = args.directory / "skill_ja.json"
cards = json.loads(cards_path.read_text(encoding="utf-8"))
skills = json.loads(skills_path.read_text(encoding="utf-8"))
rows = []
for card in cards:
    if not card or not card.get("enabled") or not 0 < card["id"] <= 65435:
        continue
    skill_id = card.get("activeSkillId", 0)
    skill = skills[skill_id] if 0 <= skill_id < len(skills) else {}
    rows.append([card["id"], card["name"], card.get("attrs", []),
                 120 if card.get("limitBreakIncr", 0) > 0 else card.get("maxLevel", 1),
                 max(1, skill.get("maxLevel", 1)), card.get("awakenings", []),
                 card.get("superAwakenings", []), card.get("syncAwakening", 0),
                 bool(card.get("canAssist")), 8 if card.get("is8Latent") else 6,
                 " ".join(card.get("altName", []))[:400]])
output = {"source": "https://github.com/Mapaler/PADDashFormation", "revision": args.revision,
          "region": "ja", "numbering": "encyclopedia", "card_sha256": hashlib.sha256(cards_path.read_bytes()).hexdigest(),
          "skill_sha256": hashlib.sha256(skills_path.read_bytes()).hexdigest(), "cards": rows}
path = Path("ios/editor/catalog.json")
path.parent.mkdir(parents=True, exist_ok=True)
path.write_text(json.dumps(output, ensure_ascii=False, separators=(",", ":")) + "\n", encoding="utf-8")
print(f"Saved {len(rows)} card records to {path}")

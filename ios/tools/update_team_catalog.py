#!/usr/bin/env python3
"""Extract pinned public facts for the independently implemented solo model.

Reads JSON only. Run from the repository root with the same inputs/revision as
update_editor_catalog.py; generated runtime data never executes upstream code.
"""
import argparse
import hashlib
import json
from pathlib import Path


def generate(directory, revision, editor):
    paths = [directory / "mon_ja.json", directory / "skill_ja.json"]
    digests = [hashlib.sha256(p.read_bytes()).hexdigest() for p in paths]
    if (editor["revision"] != revision or editor["card_sha256"] != digests[0]
            or editor["skill_sha256"] != digests[1]):
        raise ValueError("Team and editor catalogs must use identical pinned inputs")
    cards, skills = [json.loads(p.read_text(encoding="utf-8")) for p in paths]
    rows, records = [], {}

    def collect(index, ancestors=()):
        if not index:
            return
        if index in ancestors or len(ancestors) >= 12:
            raise ValueError("Cyclic or excessive leader skill references")
        if index in records:
            return
        skill = skills[index]
        assert skill["id"] == index and isinstance(skill["params"], list)
        assert all(type(p) is int for p in skill["params"])
        records[index] = [index, skill["type"], skill["params"]]
        if skill["type"] == 138:
            for child in skill["params"]:
                collect(child, (*ancestors, index))

    for row in editor["cards"]:
        card = cards[row[0]]
        assert card["id"] == row[0] and card["enabled"]
        curves = [[card[key][k] for k in ("min", "max", "scale")]
                  for key in ("hp", "atk", "rcv")]
        # Names, attributes and awakenings already live in the editor catalog.
        rows.append([card["id"], card["types"], card["maxLevel"],
                     card.get("limitBreakIncr", 0), *curves,
                     card.get("leaderSkillId", 0), card.get("henshinTo", []),
                     card.get("rarity", 0), card.get("collabId", 0)])
        collect(card.get("leaderSkillId", 0))
    ids = {r[0] for r in rows}
    assert all(target in ids for row in rows for target in row[8])
    return dict(schema=1, source=editor["source"], revision=revision,
                region="ja", numbering="encyclopedia", card_sha256=digests[0],
                skill_sha256=digests[1], cards=rows,
                skills=[records[i] for i in sorted(records)])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--revision", required=True)
    parser.add_argument("--output", type=Path, default=Path("ios/editor/team-catalog.json"))
    args = parser.parse_args()
    editor = json.loads(Path("ios/editor/catalog.json").read_text(encoding="utf-8"))
    result = generate(args.directory, args.revision, editor)
    args.output.write_text(json.dumps(result, ensure_ascii=False, separators=(",", ":")) + "\n",
                           encoding="utf-8")
    print(f"Solo facts: {len(result['cards'])} cards, {len(result['skills'])} leader records")


if __name__ == "__main__":
    main()

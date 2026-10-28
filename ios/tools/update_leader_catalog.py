#!/usr/bin/env python3
"""Extract a conservative board-goal index from public PAD facts.

Read downloaded JSON only; never execute upstream code. Parameter meanings were
reviewed in PADDashFormation's script-skill-parser.js at the recorded revision.
This is not a leader damage/activation simulator. Unknown effects block automatic
planning instead of being guessed from a localized description.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path


PASSIVE = {11, 12, 13, 15, 16, 17, 22, 23, 24, 26, 28, 29, 30, 31, 33, 36,
           40, 41, 45, 46, 48, 49, 53, 54, 62, 63, 64, 65, 67, 69, 73, 75,
           76, 77, 79, 105, 106, 107, 108, 111, 114, 121, 129, 136, 137,
           148, 162, 163, 178, 185, 186, 197, 213, 254, 264, 265}
HP = {14, 38, 39, 43, 44, 94, 95, 96, 97, 122, 123, 130, 131, 139, 183}
COMPOSITION = {125, 175, 203, 217, 229, 245}
AWAKENINGS_1 = [27, 48, 60, 78, 126, 22, 23, 24, 25, 26, 79, 80, 81,
                 0, 0, 73, 74, 75, 76, 77, 20, 82, 133, 134, 135]


def goal(kind, colors=0, count=1, length=0):
    if not 1 <= count <= 14 or not 0 <= colors <= 63:
        raise ValueError("unsupported goal bounds")
    if kind != "combo" and not colors:
        raise ValueError("unsupported orb colors")
    if kind == "colors" and count > colors.bit_count():
        raise ValueError("invalid color threshold")
    if kind == "match" and not 3 <= length <= 42:
        raise ValueError("unsupported connected length")
    return dict(kind=kind, colors=colors, count=count, length=length)


def merge_goals(goals):
    merged = {}
    for item in goals:
        key = item["kind"], item["colors"], item["length"]
        if key not in merged or item["count"] > merged[key]["count"]:
            merged[key] = item
    return list(merged.values())


def awakening_goal(awakening):
    shape = {27: "four", 48: "square", 60: "l", 78: "cross", 126: "t"}
    if awakening in shape:
        return goal(shape[awakening], 31)
    if 22 <= awakening <= 26:
        return goal("row", 1 << (awakening - 22))
    if 149 <= awakening <= 153:
        return goal("column", 1 << (awakening - 149))
    if 79 <= awakening <= 81:
        return goal("colors", 31, awakening - 76)
    if 73 <= awakening <= 77:
        return goal("match", 1 << (awakening - 73), 2, 3)
    if awakening == 20:
        return goal("row", 32)
    raise ValueError("unmodeled awakening trigger")


def profile_for(skill_id, skills):
    goals, warnings, unsupported = [], set(), set()

    def visit(index, ancestors):
        if index in ancestors or len(ancestors) >= 12 or not 0 < index < len(skills):
            unsupported.add("技能引用無效或循環")
            return
        skill = skills[index]
        if not isinstance(skill, dict) or skill.get("id") != index:
            unsupported.add("技能資料缺失")
            return
        kind, params = skill.get("type"), skill.get("params")
        if not isinstance(params, list) or len(params) > 16 or any(type(p) is not int for p in params):
            unsupported.add("技能參數格式未支援")
            return
        p = params + [0] * (16 - len(params))
        try:
            if kind == 138:
                if not params:
                    raise ValueError("empty composite")
                for child in params:
                    visit(child, ancestors | {index})
            elif kind in PASSIVE:
                pass
            elif kind in HP:
                warnings.add("HP 條件未評估")
            elif kind in COMPOSITION:
                warnings.add("隊員組成條件未評估")
            elif kind in {100, 133, 270}:
                warnings.add("技能使用條件未評估")
            elif kind == 198:
                warnings.add("回復量條件未評估")
            elif kind in {66, 98, 103, 104, 166, 169, 223}:
                goals.append(goal("combo", count=p[0]))
            elif kind in {61, 165, 170, 194, 199}:
                goals.append(goal("colors", p[0], p[1]))
                warnings.add("同時攻擊的隊員屬性與封鎖狀態未評估")
            elif kind in {109, 119, 159, 167, 182, 200, 219}:
                goals.append(goal("match", p[0], 1, p[1]))
            elif kind == 235:
                if p[1] and not p[2]:
                    goals.append(goal("match", p[0], 1, p[1]))
                elif not p[1] and p[2] == 4:
                    goals.append(goal("four", p[0]))
                else:
                    raise ValueError("unmodeled exact length")
            elif kind in {124, 164, 171, 201, 206}:
                n = 5 if kind in {124, 206} else 4
                masks = [v for v in p[:n] if v]
                minimum = p[n]
                if not masks or not 1 <= minimum <= len(masks) or any(v > 63 or v < 1 or v.bit_count() != 1 for v in masks):
                    raise ValueError("unmodeled alternatives")
                if len(set(masks)) == 1:
                    goals.append(goal("match", masks[0], minimum, 3))
                elif minimum == len(masks):
                    goals.extend(goal("match", mask, count, 3) for mask, count in Counter(masks).items())
                else:
                    raise ValueError("unmodeled color-group alternatives")
            elif kind in {151, 209}:
                goals.append(goal("cross", 32))
            elif kind == 210:
                goals.append(goal("cross", p[0]))
            elif kind == 157:
                mask = 0
                for attr, multiplier in zip(p[:6:2], p[1:6:2]):
                    if multiplier:
                        if not 0 <= attr < 6:
                            raise ValueError("unmodeled cross color")
                        mask |= 1 << attr
                goals.append(goal("cross", mask))
            elif kind in {193, 220, 221}:
                goals.append(goal("l", p[0]))
            elif kind == 149:
                goals.append(goal("four", 32))
            elif kind in {271, 280, 285}:
                if kind == 280:
                    awakenings = [p[0]]
                else:
                    mapping = AWAKENINGS_1 if kind == 271 else list(range(149, 154))
                    if not 0 < p[0] < (1 << len(mapping)):
                        raise ValueError("unmodeled awakening mask")
                    awakenings = [v for bit, v in enumerate(mapping) if p[0] & (1 << bit)]
                # All selected awakening triggers must be satisfied together.
                additions = [awakening_goal(v) for v in awakenings]
                goals.extend(additions)
                warnings.add("只規劃覺醒所需珠型；隊伍是否具有有效覺醒、攻擊屬性與封鎖狀態未評估")
            else:
                raise ValueError("unknown effect")
        except (ValueError, TypeError, IndexError):
            unsupported.add(f"尚未支援的盤面規則（技能類型 {kind}）")

    if skill_id:
        visit(skill_id, set())
    goals = merge_goals(goals)
    if len(goals) > 6:
        unsupported.add("盤面條件超過 6 項")
    return dict(goals=goals if not unsupported else [], warnings=sorted(warnings),
                unsupported=sorted(unsupported))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--revision", required=True)
    parser.add_argument("--output", type=Path, default=Path("ios/data/leader-orbs.json"))
    args = parser.parse_args()
    card_bytes = (args.directory / "mon_ja.json").read_bytes()
    skill_bytes = (args.directory / "skill_ja.json").read_bytes()
    cards, skills = json.loads(card_bytes), json.loads(skill_bytes)
    index = {str(card["id"]): {"name": card["name"], "skill": card.get("leaderSkillId", 0)}
             for card in cards if card and card.get("enabled") and 0 < card["id"] <= 65435}
    profiles = {str(skill): profile_for(skill, skills) for skill in sorted({row["skill"] for row in index.values()})}
    output = dict(schema=1, source="https://github.com/Mapaler/PADDashFormation", revision=args.revision,
                  region="ja", numbering="encyclopedia", card_sha256=hashlib.sha256(card_bytes).hexdigest(),
                  skill_sha256=hashlib.sha256(skill_bytes).hexdigest(), cards=index, skills=profiles)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(output, ensure_ascii=False, separators=(",", ":")) + "\n", encoding="utf-8")
    print(f"Saved {len(index)} cards and {len(profiles)} leader profiles to {args.output}")


if __name__ == "__main__":
    main()

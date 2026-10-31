"""Authoring fixtures; executed only with an authorized ios/test.sh run."""
import importlib.util
import copy
import json
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("leader_catalog", ROOT / "ios/tools/update_leader_catalog.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


def skills(*entries):
    return [None] + [dict(id=i + 1, type=kind, params=params, description="not used for parsing")
                     for i, (kind, params) in enumerate(entries)]


class LeaderCatalogTests(unittest.TestCase):
    def test_fixed_any_orb_is_an_explicit_six_color_subset(self):
        for kind, params, expected in (
                (119, [1023, 6, 2000], module.goal("match", 63, 1, 6)),
                (193, [1023, 2000], module.goal("l", 63)),
                (220, [1023, 1], module.goal("l", 63)),
                (235, [1023, 0, 4, 200], module.goal("four", 63))):
            source = skills((kind, params))
            fixed = module.profile_for(1, source, fixed=True)
            self.assertEqual(fixed["goals"], [expected])
            self.assertFalse(fixed["unsupported"])
            self.assertTrue(any("任意珠種" in w for w in fixed["warnings"]))
            self.assertTrue(module.profile_for(1, source)["unsupported"])
        for mask in (64, 128, 384, 1024, 1025, -1):
            self.assertTrue(module.profile_for(1, skills((119, [mask, 6, 200])), fixed=True)["unsupported"])
        self.assertTrue(module.profile_for(1, skills((61, [1023, 3, 200])), fixed=True)["unsupported"])

    def test_fixed_exact_length_never_becomes_at_least(self):
        for length in (3, 5, 6, 42):
            result = module.profile_for(1, skills((235, [1023, 0, length, 200])), fixed=True)
            self.assertEqual(result["goals"], [module.goal("exact", 63, 1, length)])
            self.assertTrue(any("首批消除" in w for w in result["warnings"]))
            self.assertFalse(result["unsupported"])
        for params in ([1023, 0, 2, 200], [1023, 0, 43, 200], [1023, 3, 5, 200], [1023, 0, 0, 200]):
            self.assertTrue(module.profile_for(1, skills((235, params)), fixed=True)["unsupported"])
        result = module.profile_for(1, skills((235, [1023, 3, 0, 150])), fixed=True)
        self.assertEqual(result["goals"], [module.goal("match", 63, 1, 3)])

    def test_fixed_facts_build_merge_and_real_examples(self):
        base = json.loads((ROOT / "ios/data/leader-orbs.json").read_text(encoding="utf-8"))
        facts = json.loads((ROOT / "ios/data/fixed-leader-skills.json").read_text(encoding="utf-8"))
        original_base, original_facts = copy.deepcopy(base), copy.deepcopy(facts)
        merged = module.merge_fixed_profiles(base, facts)
        self.assertEqual(base, original_base)
        self.assertEqual(facts, original_facts)
        self.assertEqual(len(facts["cards"]), 117)
        self.assertEqual(len(facts["skills"]), 325)
        self.assertEqual(len(merged["skills"]), len(base["skills"]) + 113)
        for key, profile in base["skills"].items():
            self.assertEqual(merged["skills"][key], profile)
        self.assertEqual(merged["skills"]["35997"]["goals"], [module.goal("combo", count=6)])
        self.assertEqual(merged["skills"]["36017"]["goals"], [module.goal("l", 63), module.goal("combo", count=6)])
        self.assertEqual(merged["skills"]["36024"]["goals"], [module.goal("colors", 31, 3), module.goal("exact", 63, 1, 5)])
        self.assertNotIn("36861", merged["skills"], "avatar active skill is not a leader profile")
        fixed4 = [row for row in facts["cards"] if row["catalog_id"] // 1000000 == 9]
        self.assertEqual(len(fixed4), 54)
        for row in fixed4:
            profile = merged["skills"][str(row["skill"])]
            self.assertFalse(profile["unsupported"], row)
            self.assertTrue(profile["goals"], row)
        for profile in merged["skills"].values():
            self.assertLessEqual(len(profile["goals"]), 6)
            if profile["unsupported"]:
                self.assertEqual(profile["goals"], [])
            for item in profile["goals"]:
                self.assertEqual(item, module.goal(**item))

    def test_fixed_facts_reject_mismatch_and_incomplete_dependencies(self):
        base = json.loads((ROOT / "ios/data/leader-orbs.json").read_text(encoding="utf-8"))
        source = json.loads((ROOT / "ios/data/fixed-leader-skills.json").read_text(encoding="utf-8"))
        def rejected(change):
            facts = copy.deepcopy(source)
            change(facts)
            with self.assertRaises(ValueError):
                module.merge_fixed_profiles(base, facts)
        rejected(lambda f: f.update(revision="different"))
        rejected(lambda f: f.update(skill_sha256="0" * 64))
        rejected(lambda f: f.update(raw_card_sha256="missing"))
        rejected(lambda f: f["cards"].append(f["cards"][0]))
        rejected(lambda f: f["cards"][0].update(catalog_id=10000000))
        rejected(lambda f: f["cards"][0].update(catalog_id=9009950))
        rejected(lambda f: f["skills"].append(f["skills"][0]))
        rejected(lambda f: f["skills"].pop())
        rejected(lambda f: f["skills"].append(dict(id=120000, type=11, params=[])))
        rejected(lambda f: f["skills"][0].update(params=[True]))
        rejected(lambda f: next(r for r in f["skills"] if r["type"] == 138).update(params=[120000]))
        def cycle(f):
            row = next(r for r in f["skills"] if r["type"] == 138)
            row["params"] = [row["id"]]
        rejected(cycle)

    def test_raw_fixed_fact_selection_preserves_native_ids(self):
        def card(native, name, skill):
            row = [0] * 27
            row[0], row[1], row[26] = native, name, skill
            return row
        source = skills((138, [2]), (169, [6, 1800, 25]), (71, [0, 2, 3, -1]))
        raw = [card(8388, "ordinary", 1), card(9008388, "fixed", 1), card(10000000, "avatar", 3),
               card(9004839, "already indexed", 4), card(9009950, "reserved gap", 1)]
        cards, records = module.fixed_skill_facts(raw, source, {0, 4})
        self.assertEqual(cards, [dict(catalog_id=9008388, name="fixed", skill=1)])
        self.assertEqual(records, [{k: source[i][k] for k in ("id", "type", "params")} for i in (1, 2)])

    def test_conjunction_and_awakening_mapping(self):
        source = skills((138, [2, 3]), (151, [0, 0, 83]), (271, [1, 10000, 0, 3, 20000000]))
        result = module.profile_for(1, source)
        self.assertEqual(result["unsupported"], [])
        self.assertEqual(result["goals"], [module.goal("cross", 32), module.goal("four", 31)])
        self.assertTrue(any("有效覺醒" in s for s in result["warnings"]))

    def test_unknown_and_poison_are_not_ignored(self):
        for unknown in ((119, [384, 6, 350]), (999, []), (101, [4, 200]), (235, [1, 0, 6, 200])):
            source = skills((138, [2, 3]), (66, [4, 200]), unknown)
            result = module.profile_for(1, source)
            self.assertEqual(result["goals"], [])
            self.assertTrue(result["unsupported"])

    def test_malformed_and_recursive_skills(self):
        for source in (skills((138, [1])), skills((138, [2])), skills((61, [31, True, 900])),
                       skills((271, [1 << 30])), skills((285, [32])), skills((138, []))):
            self.assertTrue(module.profile_for(1, source)["unsupported"])

    def test_repeated_color_groups_and_minimum_threshold(self):
        source = skills((124, [4, 4, 4, 0, 0, 2, 500, 100]))
        self.assertEqual(module.profile_for(1, source)["goals"], [module.goal("match", 4, 2, 3)])
        source = skills((61, [31, 4, 900, 100, 1]))
        self.assertEqual(module.profile_for(1, source)["goals"], [module.goal("colors", 31, 4)])

    def test_non_board_conditions_are_visible(self):
        source = skills((138, [2, 3, 4]), (61, [31, 4, 900]), (133, [31, 0, 400, 400]), (123, [50, 31, 0, 200]))
        result = module.profile_for(1, source)
        self.assertFalse(result["unsupported"])
        self.assertIn("HP 條件未評估", result["warnings"])
        self.assertIn("技能使用條件未評估", result["warnings"])
        self.assertEqual(result["goals"], [module.goal("colors", 31, 4)])

    def test_pinned_index_provenance_and_bounds(self):
        data = json.loads((ROOT / "ios/data/leader-orbs.json").read_text(encoding="utf-8"))
        editor = json.loads((ROOT / "ios/editor/catalog.json").read_text(encoding="utf-8"))
        for key in ("revision", "card_sha256", "skill_sha256", "region", "numbering"):
            self.assertEqual(data[key], editor[key])
        for profile in data["skills"].values():
            self.assertLessEqual(len(profile["goals"]), 6)
            if profile["unsupported"]:
                self.assertEqual(profile["goals"], [])
            for item in profile["goals"]:
                self.assertEqual(item, module.goal(**item))
        column = data["skills"][str(data["cards"]["14242"]["skill"])]["goals"]
        self.assertEqual(column, [module.goal("column", 4)])


if __name__ == "__main__":
    unittest.main()

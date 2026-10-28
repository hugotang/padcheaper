"""Authoring fixtures; executed only with an authorized ios/test.sh run."""
import importlib.util
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

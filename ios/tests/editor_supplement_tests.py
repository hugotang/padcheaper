"""Reject tampered, incomplete or differently scoped public catalog snapshots."""
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from editor_supplement import DEFAULT_MANIFEST, load_supplement


class SupplementTests(unittest.TestCase):
    def test_pinned_inventory(self):
        cards, provenance = load_supplement()
        self.assertEqual([c["num"] for c in cards], list(range(14354, 14443)))
        self.assertEqual(provenance["records"], 89)

    def test_rejects_corruption(self):
        self.check_rejected(lambda source, manifest: source["mons"].pop(), rehash=False)

    def test_rejects_incomplete_inventory(self):
        self.check_rejected(lambda source, manifest: source["mons"].pop(0))

    def test_rejects_other_region(self):
        self.check_rejected(lambda source, manifest: source["mons"][0]["mon"].update(region="na"))

    def test_rejects_unknown_transform_direction(self):
        self.check_rejected(lambda source, manifest: source["mons"][0]["mon"].update(transforms=[14442, 14441, 14440]))

    def check_rejected(self, mutate, rehash=True):
        manifest = json.loads(DEFAULT_MANIFEST.read_text(encoding="utf-8"))
        source = json.loads((DEFAULT_MANIFEST.parent / manifest["file"]).read_text(encoding="utf-8"))
        mutate(source, manifest)
        raw = json.dumps(source, ensure_ascii=False).encode()
        if rehash:
            manifest["sha256"] = hashlib.sha256(raw).hexdigest()
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / manifest["file"]).write_bytes(raw)
            path = root / "manifest.json"
            path.write_text(json.dumps(manifest), encoding="utf-8")
            with self.assertRaises(AssertionError):
                load_supplement(path)


if __name__ == "__main__":
    unittest.main()

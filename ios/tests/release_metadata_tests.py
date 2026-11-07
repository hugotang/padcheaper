#!/usr/bin/env python3
"""Release metadata contracts; run only with an explicit build/test request."""
import copy
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import release_metadata as release


class ReleaseMetadataTests(unittest.TestCase):
    def setUp(self):
        self.manifest = {
            "schema": 1, "dylib_version": "1.2.3-orbs", "minimum_ios": "15.0",
            "architecture": "arm64", "platform": "iPhoneOS", "signing": "fixture signature",
            "mode": "diagnostic", "summary_features": {"Current feature": "sample"},
            "features": {"sample": "captured source description", "unlisted": "still exported"},
        }

    def test_flat_metadata_and_compiler_override(self):
        metadata = release.build_metadata(self.manifest, "a" * 40, b"synthetic fixture", "16.2")
        self.assertEqual(metadata["schema"], 1)
        self.assertEqual(metadata["minimum_ios"], "16.2")
        self.assertEqual(metadata["sample"], "captured source description")
        self.assertEqual(metadata["unlisted"], "still exported")
        self.assertIs(metadata["device_verified"], False)
        self.assertEqual(metadata["sha256"], hashlib.sha256(b"synthetic fixture").hexdigest())
        release.verify_release_snapshot(metadata, self.manifest)

    def test_summary_uses_captured_descriptions_without_claiming_tests(self):
        metadata = release.build_metadata(self.manifest, "a" * 40, b"fixture", "16.2")
        self.manifest["features"]["sample"] = "later source edit"
        summary = release.render_summary(metadata)
        self.assertIn("captured source description", summary)
        self.assertNotIn("later source edit", summary)
        self.assertNotIn("still exported", summary)
        self.assertIn("minimum iOS 16.2", summary)
        self.assertIn("device_verified=false", summary)
        self.assertNotIn("passed", summary)

    def test_invalid_or_reserved_metadata_is_rejected(self):
        for change in (
            {"schema": True}, {"dylib_version": '../bad"version'}, {"minimum_ios": "$(command)"},
            {"summary_features": {"Missing": "missing"}}, {"features": {"commit": "override"}},
            {"device_verified": True},
        ):
            with self.subTest(change=change), self.assertRaises(ValueError):
                release.validate_release({**self.manifest, **change})
        with self.assertRaises(ValueError):
            json.loads('{"schema":1,"schema":2}', object_pairs_hook=release.unique_object)

    def test_artifact_inconsistency_is_rejected(self):
        baseline = release.build_metadata(self.manifest, "a" * 40, b"fixture", "15.0")
        for key, value in (("dylib_version", "1.2.4-orbs"), ("sample", "different"),
                           ("release_summary", {}), ("device_verified", True)):
            with self.subTest(key=key), self.assertRaises(ValueError):
                release.verify_release_snapshot({**baseline, key: value}, self.manifest)

    def test_finalize_captures_manifest_and_preserves_source_notes(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            header = root / "generated.h"
            header.write_text(release.version_header(self.manifest), encoding="utf-8")
            artifact = root / "artifact"
            artifact.mkdir()
            (artifact / "PADCheaper.dylib").write_bytes(b"metadata fixture, not a real binary")
            source = root / "source.md"
            source.write_text("[note](docs/example.md)", encoding="utf-8")
            (artifact / "README.md").write_text(source.read_text(), encoding="utf-8")
            with patch.object(release, "HEADER", header), patch.object(release.subprocess, "check_output", return_value="b" * 40):
                release.finalize(artifact, self.manifest, "16.0")
            metadata = release.read_json(artifact / "build-info.json")
            captured = release.read_json(artifact / "release.json")
            release.verify_release_snapshot(metadata, captured)
            self.assertEqual(captured, self.manifest)
            self.assertEqual(metadata["commit"], "b" * 40)
            self.assertEqual(source.read_text(), "[note](docs/example.md)")
            self.assertEqual((artifact / "README.md").read_text(), "[note](example.md)")
            self.assertEqual((artifact / "SHA256SUMS").read_text(), f'{metadata["sha256"]}  PADCheaper.dylib\n')

    def test_changed_version_header_blocks_finalization_before_writes(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            header = root / "generated.h"
            changed = copy.deepcopy(self.manifest)
            changed["dylib_version"] = "1.2.4-orbs"
            header.write_text(release.version_header(changed), encoding="utf-8")
            with patch.object(release, "HEADER", header), self.assertRaises(ValueError):
                release.finalize(root, self.manifest, "15.0")
            self.assertFalse((root / "build-info.json").exists())

    def test_artifact_target_links_keep_historical_evidence_separate(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            notes = root / "targets.md"
            notes.write_text("[current](../targets/pad-23.6.1.json)\n"
                             "[current battle](../targets/battle-23.6.1.json)\n"
                             "[current orbs](../targets/orbs-23.6.1.json)\n"
                             "[old](../targets/pad-23.6.0.json)\n"
                             "[old battle](../targets/battle-23.6.0.json)\n"
                             "[old orbs](../targets/orbs-23.6.0.json)\n"
                             "[migration](docs/address-update-23.6.1.md)\n"
                             "[evidence](../research/address-update-23.6.1.json)\n"
                             "[badges](../editor/badge-catalog.json)\n"
                             "[artwork](../editor/assets/badge.png)\n"
                             "[source](../src/PHDataTarget.h)\n"
                             "[test](../tests/orb_shape_guidance_tests.cpp)\n", encoding="utf-8")
            release.rewrite_artifact_links(root, "a" * 40)
            self.assertEqual(notes.read_text(encoding="utf-8"),
                             "[current](target.json)\n[current battle](battle-target.json)\n"
                             "[current orbs](orb-target.json)\n[old](targets/pad-23.6.0.json)\n"
                             "[old battle](targets/battle-23.6.0.json)\n"
                             "[old orbs](targets/orbs-23.6.0.json)\n"
                             "[migration](address-update-23.6.1.md)\n"
                             "[evidence](research/address-update-23.6.1.json)\n"
                             "[badges](editor-source/badge-catalog.json)\n"
                             "[artwork](third_party/PADDashFormation/badge.png)\n"
                             f"[source](https://github.com/hugotang/padcheaper/blob/{'a' * 40}/ios/src/PHDataTarget.h)\n"
                             f"[test](https://github.com/hugotang/padcheaper/blob/{'a' * 40}/ios/tests/orb_shape_guidance_tests.cpp)\n")


if __name__ == "__main__":
    unittest.main()

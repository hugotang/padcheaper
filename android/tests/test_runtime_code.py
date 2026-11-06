"""Offline runtime-capture fixtures; execute only after an explicit build/test request."""

import copy
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import collect_runtime_code as runtime


class RuntimeCodeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        # Synthetic bytes only. The fixture-specific header hash is substituted
        # only when testing a complete collection; no PAD bytes are in this test.
        header = bytearray(runtime.HEADER_BYTES)
        header[:6] = b"\x7fELF\x02\x01"
        header[18:20] = (183).to_bytes(2, "little")
        cls.header_hash = hashlib.sha256(header).hexdigest()
        cls.code = bytes(header) + bytes(runtime.CODE_BYTES - len(header))
        cls.code_hash = hashlib.sha256(cls.code).hexdigest()

    def setUp(self):
        self.expected = {"sourceFingerprint": "a" * 64, "moduleVersion": "0.1.0-probe",
                         "gamePackage": "jp.gungho.pad", "gameVersion": "23.6.0", "gameVersionCode": 230600,
                         "gameApkSha256": runtime.GAME_APK_SHA256,
                         "gameLibrarySha256": runtime.GAME_LIBRARY_SHA256,
                         "payloadSha256": {"x86_64": "b" * 64}, "apkSha256": "c" * 64}
        self.process = {"pid": 2448, "startTicks": "1234567", "bootId": "test-boot", "abi": "x86_64",
                        "uid": 110123}
        self.report = {"schema": 1, "captureKind": "runtime-code", "gamePackage": "jp.gungho.pad",
                       "moduleSourceFingerprint": "a" * 64, "moduleVersion": "0.1.0-probe", "pid": 2448,
                       "processIdentityAvailable": True, "processStartTicks": "1234567", "bootId": "test-boot",
                       "capturedElapsedMillis": 1000, "captureFinishedElapsedMillis": 1500,
                       "captureStatus": "complete", "packageIdentityMatched": True,
                       "gameVersion": "23.6.0", "gameVersionCode": 230600, "libraryArtifactMatched": True,
                       "nativePayloadLoaded": True, "nativePayloadAbi": "x86_64", "nativePayloadSha256": "b" * 64,
                       "frameDispatchPrepared": True, "foreground": True, "frameStatus": "observed",
                       "threadChanged": False, "foregroundFrames": 12, "frames": 20, "frameAgeMillis": 10,
                       "frameThread": 22, "captureStartThread": 22,
                       "foregroundGeneration": 3, "captureStartGeneration": 3,
                       "runtimeCode": {"status": "code-captured", "capturedBytes": 17295152,
                                       "fileOffset": 0, "virtualAddress": 0, "elfMachine": 183,
                                       "stableRead": True, "mappingsStable": True},
                       "codeFile": "code-2448-12345678-abcd-4321-890a-123456789abc.bin",
                       "codeSha256": self.code_hash,
                       "runtimeLayoutVerified": False, "boardReadEnabled": False,
                       "actionsEnabled": False, "deviceVerified": False}

    def issues(self, **changes):
        return runtime.assess_runtime_code({**self.report, **changes}, self.expected, self.process, 2000)

    def fake_adb(self, mutation=None):
        case = self

        class FakeAdb:
            binary_read = False
            reads = None

            def __init__(self):
                self.reads = []

            def main_pid(self):
                return case.process["pid"]

            def process(self, pid):
                value = copy.deepcopy(case.process)
                if self.binary_read and mutation == "process":
                    value["startTicks"] = "7654321"
                return value

            def installed_module_hash(self, user):
                case.assertEqual(1, user)
                if self.binary_read and mutation == "module":
                    return "d" * 64
                return case.expected["apkSha256"]

            def root_file(self, path, limit):
                self.reads.append((path, limit))
                prefix = "/data/user/1/jp.gungho.pad/files/padcheaper/"
                case.assertTrue(path.startswith(prefix))
                if path.endswith(".json"):
                    case.assertEqual(prefix + "code-2448.json", path)
                    case.assertEqual(256 * 1024, limit)
                    report = copy.deepcopy(case.report)
                    if self.binary_read and mutation == "report":
                        report["captureStatus"] = "in-progress"
                    return json.dumps(report).encode()
                case.assertEqual(prefix + case.report["codeFile"], path)
                case.assertEqual(17295152, limit)
                self.binary_read = True
                if mutation == "truncated":
                    return case.code[:-1]
                if mutation == "overflow":
                    raise ValueError("ADB response exceeds size bound")
                return case.code

            def elapsed_ms(self):
                if self.binary_read and mutation == "clock-unavailable":
                    raise OSError("final-device-clock-unavailable")
                if self.binary_read and mutation == "clock-invalid":
                    return True
                if self.binary_read and mutation == "clock-later":
                    return 2500
                return 302000 if self.binary_read and mutation == "expired" else 2000

        return FakeAdb()

    def collect(self, adb, max_age_ms=runtime.MAX_AGE_MS):
        with patch.object(runtime, "HEADER_SHA256", self.header_hash):
            return runtime.collect_runtime_code(adb, self.expected, max_age_ms)

    def test_only_fixed_range_stable_foreground_evidence_is_accepted(self):
        self.assertEqual([], self.issues())
        for changes in ({"fileOffset": 1}, {"virtualAddress": 1}, {"capturedBytes": 17295151},
                        {"elfMachine": 62}, {"capturedBytes": True}):
            self.assertIn("runtime-code-range-mismatch", self.issues(runtimeCode={**self.report["runtimeCode"], **changes}))
        for field in ("stableRead", "mappingsStable"):
            self.assertIn("runtime-code-not-stable",
                          self.issues(runtimeCode={**self.report["runtimeCode"], field: False}))
        self.assertIn("capture-foreground-generation-changed-or-invalid", self.issues(foregroundGeneration=4))
        self.assertIn("capture-frame-thread-changed-or-invalid", self.issues(frameThread=23))
        self.assertIn("frames-not-current", self.issues(foreground=False))
        self.assertIn("frame-thread-changed", self.issues(threadChanged=True))

    def test_wrong_identity_or_capability_is_rejected(self):
        for field, value, issue in (
                ("schema", True, "report-schema-mismatch"),
                ("captureKind", "probe", "capture-kind-mismatch"),
                ("pid", 2449, "process-pid-mismatch"),
                ("processStartTicks", "new-start", "process-start-mismatch"),
                ("bootId", "new-boot", "boot-mismatch"),
                ("moduleSourceFingerprint", "d" * 64, "module-source-mismatch"),
                ("nativePayloadSha256", "d" * 64, "payload-hash-mismatch"),
                ("nativePayloadAbi", "arm64-v8a", "payload-process-abi-mismatch"),
                ("captureStatus", "in-progress", "capture-failed")):
            self.assertIn(issue, self.issues(**{field: value}))
        for field in ("runtimeLayoutVerified", "boardReadEnabled", "actionsEnabled", "deviceVerified"):
            self.assertIn("unexpected-capability:" + field, self.issues(**{field: True}))

    def test_time_bounds_and_wrong_field_types_are_rejected(self):
        for value in (-1, 2001, True, "1000", None):
            self.assertIn("report-expired-or-invalid-time", self.issues(capturedElapsedMillis=value))
        for value in (999, 2001, True, "1500", None):
            self.assertIn("capture-duration-invalid", self.issues(captureFinishedElapsedMillis=value))
        report = {**self.report, "captureFinishedElapsedMillis": 31001}
        self.assertIn("capture-duration-invalid",
                      runtime.assess_runtime_code(report, self.expected, self.process, 32000))
        self.assertIn("report-expired-or-invalid-time",
                      runtime.assess_runtime_code(self.report, self.expected, self.process, 302000))
        for value in (None, "code-captured", []):
            self.assertIn("runtime-code-evidence-missing", self.issues(runtimeCode=value))
        self.assertIn("capture-foreground-generation-changed-or-invalid", self.issues(captureStartGeneration=True))

    def test_invalid_binary_filename_is_never_read(self):
        for filename in ("../code.bin", "/data/local/tmp/code.bin", "code-2449-12345678-abcd-4321-890a-123456789abc.bin",
                         "code-2448-12345678-abcd-4321-890a-123456789abc.bin/extra", None):
            with self.subTest(filename=filename):
                self.report["codeFile"] = filename
                adb = self.fake_adb()
                result, data = self.collect(adb)
                self.assertIn("runtime-code-filename-invalid", result["issues"])
                self.assertIsNone(data)
                self.assertFalse(adb.binary_read)

    def test_binary_hash_size_and_header_are_independently_checked(self):
        with patch.object(runtime, "HEADER_SHA256", self.header_hash):
            self.assertEqual([], runtime.assess_code_bytes(self.code, self.report))
            self.assertIn("runtime-code-size-mismatch", runtime.assess_code_bytes(self.code[:-1], self.report))
            self.assertIn("runtime-code-content-hash-mismatch",
                          runtime.assess_code_bytes(self.code, {**self.report, "codeSha256": "0" * 64}))
            altered = b"X" + self.code[1:]
            self.assertIn("runtime-code-header-mismatch",
                          runtime.assess_code_bytes(altered, {**self.report, "codeSha256": hashlib.sha256(altered).hexdigest()}))

    def test_success_keeps_unpacked_and_game_action_status_unverified(self):
        result, data = self.collect(self.fake_adb())
        self.assertEqual("runtime-code-collected", result["status"])
        self.assertEqual([], result["issues"])
        self.assertEqual(self.code, data)
        for field in ("unpackedCodeVerified", "runtimeLayoutVerified", "deviceVerified", "boardReadEnabled", "actionsEnabled"):
            self.assertIs(False, result[field])

    def test_archived_clock_is_actual_final_device_reading_and_policy(self):
        result, data = self.collect(self.fake_adb("clock-later"), 2000)
        self.assertEqual("runtime-code-collected", result["status"])
        self.assertIsNotNone(data)
        self.assertEqual(2500, result["finalDeviceElapsedMillis"])
        self.assertEqual(2000, result["maxAgeMillisUsed"])
        self.assertNotEqual(result["report"]["captureFinishedElapsedMillis"], result["finalDeviceElapsedMillis"])
        self.assertEqual([], runtime.assess_runtime_code(result["report"], self.expected, result["process"],
                                                        result["finalDeviceElapsedMillis"], result["maxAgeMillisUsed"]))
        # A final reading which crosses the limit must be recorded as a failure,
        # not replaced by the earlier successful clock observation.
        expired, data = self.collect(self.fake_adb("expired"))
        self.assertEqual(302000, expired["finalDeviceElapsedMillis"])
        self.assertEqual(runtime.MAX_AGE_MS, expired["maxAgeMillisUsed"])
        self.assertIn("report-expired-or-invalid-time", expired["issues"])
        self.assertIsNone(data)

    def test_missing_final_clock_or_failed_identity_recheck_cannot_publish_clock_evidence(self):
        for mutation, issue in (("clock-unavailable", "final-device-clock-unavailable"),
                                ("clock-invalid", "final-device-elapsed-invalid"),
                                ("process", "process-changed-during-collection"),
                                ("module", "module-changed-during-collection"),
                                ("report", "report-changed-during-collection")):
            with self.subTest(mutation=mutation):
                result, data = self.collect(self.fake_adb(mutation))
                self.assertNotEqual("runtime-code-collected", result["status"])
                self.assertIn(issue, result["issues"])
                self.assertNotIn("finalDeviceElapsedMillis", result)
                self.assertNotIn("maxAgeMillisUsed", result)
                self.assertIsNone(data)

    def test_identity_changes_after_binary_read_discard_accepted_bytes(self):
        for mutation, issue in (("process", "process-changed-during-collection"),
                                ("module", "module-changed-during-collection"),
                                ("report", "report-changed-during-collection"),
                                ("expired", "report-expired-or-invalid-time"),
                                ("truncated", "runtime-code-size-mismatch"),
                                ("overflow", "ADB response exceeds size bound")):
            with self.subTest(mutation=mutation):
                adb = self.fake_adb(mutation)
                result, data = self.collect(adb)
                self.assertTrue(adb.binary_read)
                self.assertNotEqual("runtime-code-collected", result["status"])
                self.assertIn(issue, result["issues"])
                self.assertIsNone(data)

    def test_wrong_installed_apk_or_target_does_not_read_binary(self):
        adb = self.fake_adb()
        adb.installed_module_hash = lambda user: "d" * 64
        result, data = self.collect(adb)
        self.assertIn("installed-module-apk-mismatch", result["issues"])
        self.assertFalse(adb.binary_read)
        self.assertIsNone(data)
        self.expected["gameLibrarySha256"] = "d" * 64
        result, data = self.collect(self.fake_adb())
        self.assertIn("unsupported-runtime-code-target", result["issues"])
        self.assertIsNone(data)

    def test_publication_is_exclusive_and_failures_publish_no_binary(self):
        with tempfile.TemporaryDirectory() as temporary:
            output = runtime.reserve_output(Path(temporary) / "capture")
            with self.assertRaises(FileExistsError):
                runtime.reserve_output(output)
            result, data = self.collect(self.fake_adb())
            original_rename = Path.rename

            def inspect_rename(source, destination):
                self.assertFalse(destination.exists())
                self.assertEqual({"report.json", self.report["codeFile"]}, {p.name for p in source.iterdir()})
                return original_rename(source, destination)

            with patch.object(Path, "rename", inspect_rename):
                published = runtime.publish_result(output, result, data)
            self.assertEqual(self.code, (published / self.report["codeFile"]).read_bytes())
            archived = json.loads((published / "report.json").read_text())
            self.assertEqual("runtime-code-collected", archived["status"])
            self.assertEqual(result["finalDeviceElapsedMillis"], archived["finalDeviceElapsedMillis"])
            self.assertEqual(result["maxAgeMillisUsed"], archived["maxAgeMillisUsed"])
            with self.assertRaises(FileExistsError):
                runtime.publish_result(output, result, data)
            failed = runtime.reserve_output(Path(temporary) / "failed")
            result, data = self.collect(self.fake_adb("process"))
            published = runtime.publish_result(failed, result, data)
            self.assertEqual(["report.json"], [p.name for p in published.iterdir()])


if __name__ == "__main__":
    unittest.main()

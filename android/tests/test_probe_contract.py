"""Pure fixtures, no ADB/device use. Run only with an explicit build/test request."""

import copy
import json
from pathlib import Path
import sys
import unittest
import tempfile
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from collect_probe import Adb, collect
from probe_contract import MODULE, apk_identity, assess_report, process_start_ticks, read_json


class ProbeContractTests(unittest.TestCase):
    def setUp(self):
        self.expected = {"sourceFingerprint": "a" * 64, "moduleVersion": "0.1.0-probe",
                         "gameVersion": "23.6.0", "gameVersionCode": 230600,
                         "payloadSha256": {"x86_64": "b" * 64}, "apkSha256": "c" * 64}
        self.process = {"pid": 2448, "startTicks": "1234567", "bootId": "test-boot", "abi": "x86_64", "uid": 10123}
        self.report = {"schema": 3, "gamePackage": "jp.gungho.pad", "moduleSourceFingerprint": "a" * 64,
                       "moduleVersion": "0.1.0-probe", "pid": 2448, "processIdentityAvailable": True,
                       "processStartTicks": "1234567", "bootId": "test-boot", "capturedElapsedMillis": 1000,
                       "captureStatus": "complete", "packageIdentityMatched": True,
                       "gameVersion": "23.6.0", "gameVersionCode": 230600, "libraryArtifactMatched": True,
                       "nativePayloadLoaded": True, "nativePayloadAbi": "x86_64", "nativePayloadSha256": "b" * 64,
                       "frameDispatchPrepared": True, "foreground": True, "frameStatus": "observed",
                       "threadChanged": False, "foregroundFrames": 12, "frames": 20, "frameAgeMillis": 10,
                       "frameThread": 22, "library": {"status": "arm64-image-observed", "elfMachine": 183},
                       "runtimeLayoutVerified": False, "boardReadEnabled": False, "actionsEnabled": False,
                       "deviceVerified": False}

    def issues(self, **changes):
        return assess_report({**self.report, **changes}, self.expected, self.process, 2000, 300000)

    def test_entry_evidence_does_not_authorize_actions(self):
        self.assertEqual([], self.issues())
        self.assertIn("unexpected-capability:actionsEnabled", self.issues(actionsEnabled=True))
        self.assertIn("unexpected-capability:deviceVerified", self.issues(deviceVerified=True))

    def test_pid_reuse_and_new_build_with_same_version_are_rejected(self):
        self.assertIn("process-start-mismatch", self.issues(processStartTicks="7654321"))
        self.assertIn("boot-mismatch", self.issues(bootId="other-boot"))
        self.assertIn("module-source-mismatch", self.issues(moduleSourceFingerprint="d" * 64))
        self.assertIn("payload-hash-mismatch", self.issues(nativePayloadSha256="e" * 64))

    def test_expired_future_and_missing_evidence_are_not_success(self):
        for time in (-400000, 3000, True):
            self.assertIn("report-expired-or-invalid-time", self.issues(capturedElapsedMillis=time))
        self.assertIn("process-identity-unavailable", self.issues(processIdentityAvailable=False))
        self.assertIn("capture-failed", self.issues(captureStatus="failed"))
        self.assertIn("frames-not-current", self.issues(frameStatus="stale"))
        self.assertIn("frames-not-current", self.issues(foreground=False))
        self.assertIn("insufficient-frame-evidence", self.issues(foregroundFrames=1))
        self.assertIn("library-image-unverified", self.issues(library={"status": "memory-read-unavailable"}))
        self.assertIn("library-image-unverified", self.issues(library="not-an-object"))

    def test_invalid_json_and_process_stat_are_rejected(self):
        with self.assertRaises(ValueError):
            read_json('{"schema":3,"schema":2}')
        with self.assertRaises(ValueError):
            read_json('{"fps":NaN}')
        fields = " ".join(str(n) for n in range(4, 22))
        self.assertEqual("1234567", process_start_ticks(f"2448 (pad ) worker) S {fields} 1234567 0"))
        with self.assertRaises(ValueError):
            process_start_ticks("2448 (pad) S 1 2")

    def test_apk_payload_cpu_is_checked_instead_of_trusting_its_folder(self):
        # Fixture ZIP only, never an installable APK or Android build.
        identity = {**self.expected, "schema": 1, "packageName": MODULE, "gamePackage": "jp.gungho.pad",
                    "gameApkSha256": "f" * 64, "gameLibrarySha256": "e" * 64}

        def archive(path, x86_machine):
            with zipfile.ZipFile(path, "w") as output:
                output.writestr("assets/padcheaper-build.json", json.dumps(identity))
                output.writestr("META-INF/xposed/java_init.list", MODULE + ".PadModule\n")
                output.writestr("META-INF/xposed/scope.list", "jp.gungho.pad\n")
                output.writestr("META-INF/xposed/module.prop",
                                "minApiVersion=102\ntargetApiVersion=102\nstaticScope=true\nautoHotReload=false\n")
                for abi, machine in (("arm64-v8a", 183), ("x86_64", x86_machine)):
                    header = bytearray(20)
                    header[:6] = b"\x7fELF\x02\x01"
                    header[18:20] = machine.to_bytes(2, "little")
                    output.writestr(f"lib/{abi}/libpadcheaper_runtime.so", header)

        with tempfile.TemporaryDirectory() as temporary:
            valid, invalid = Path(temporary) / "valid.zip", Path(temporary) / "invalid.zip"
            archive(valid, 62)
            self.assertEqual({"x86_64", "arm64-v8a"}, set(apk_identity(valid)["payloadSha256"]))
            archive(invalid, 183)
            with self.assertRaisesRegex(ValueError, "payload CPU differs"):
                apk_identity(invalid)

    def test_restart_during_collection_is_incomplete(self):
        case = self

        class FakeAdb:
            reads = 0

            def main_pid(self):
                return 2448

            def process(self, pid):
                self.reads += 1
                value = copy.deepcopy(case.process)
                if self.reads > 1:
                    value["startTicks"] = "7654321"
                return value

            def installed_module_hash(self, user):
                return case.expected["apkSha256"]

            def root_file(self, path, limit):
                return json.dumps(case.report).encode()

            def run(self, *args):
                return b""

        result = collect(FakeAdb(), self.expected, 300000)
        self.assertEqual("incomplete", result["status"])
        self.assertIn("process-changed-during-collection", result["issues"])
        self.assertFalse(result["deviceVerified"])

    def test_same_name_child_is_not_a_second_main(self):
        class ProcessAdb(Adb):
            def __init__(self, parents, uids=None, parent_name=b"zygote64"):
                self.parents = parents
                self.uids = uids or {pid: 10062 for pid in parents}
                self.parent_name = parent_name

            def run(self, *args, **kwargs):
                return " ".join(str(pid) for pid in reversed(self.parents)).encode()

            def root_file(self, path, limit):
                pid = int(path.split("/")[2])
                if path.endswith("/cmdline"):
                    return (b"jp.gungho.pad" if pid in self.parents else self.parent_name) + b"\0"
                uid = self.uids[pid]
                return f"PPid:\t{self.parents[pid]}\nUid:\t{uid}\t{uid}\t{uid}\t{uid}\n".encode()

        self.assertEqual(7978, ProcessAdb({7978: 682, 8106: 7978, 8200: 8106}).main_pid())
        for tree in ({7978: 682, 8106: 682}, {7978: 8106, 8106: 7978}):
            with self.assertRaises(ValueError):
                ProcessAdb(tree).main_pid()
        with self.assertRaises(ValueError):
            ProcessAdb({7978: 682, 8106: 7978}, {7978: 10062, 8106: 110062}).main_pid()
        with self.assertRaises(ValueError):
            ProcessAdb({8106: 1}, parent_name=b"init").main_pid()


if __name__ == "__main__":
    unittest.main()

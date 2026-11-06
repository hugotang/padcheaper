"""Synthetic offline fixtures only. Execute only after an explicit build/test request."""

import copy
from contextlib import ExitStack
import hashlib
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest import mock
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import analyze_runtime_code as analyzer
import collect_runtime_code as collector
from probe_contract import MODULE, apk_identity


def digest(data):
    return hashlib.sha256(data).hexdigest()


class AnalyzeRuntimeCodeTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        # Synthetic PT_LOAD plus section table: no original game bytes are used.
        self.code_size = 3 * 4096 + 17
        original = bytearray(self.code_size + 4096)
        original[:6] = b"\x7fELF\x02\x01"
        struct.pack_into("<H", original, 18, 183)
        struct.pack_into("<QQ", original, 32, 64, self.code_size + 1024)
        struct.pack_into("<HHHHH", original, 54, 56, 8, 64, 5, 4)
        struct.pack_into("<IIQQQQQQ", original, 120,
                         1, 5, 0, 0, 0, self.code_size, self.code_size, 16384)
        names = b"\0.text\0.data\0.bss\0.shstrtab\0"
        names_offset = self.code_size + 512
        original[names_offset:names_offset + len(names)] = names
        section_rows = [
            (0, 0, 0, 0, 0, 0, 0, 0, 0, 0),
            (1, 1, 6, 1024, 1024, 64, 0, 0, 16, 0),
            (7, 1, 3, 32768, self.code_size + 128, 32, 0, 0, 8, 0),
            (13, 8, 3, 32800, len(original) + 100, 128, 0, 0, 8, 0),
            (18, 3, 0, 0, names_offset, len(names), 0, 0, 1, 0),
        ]
        for index, row in enumerate(section_rows):
            struct.pack_into("<IIQQQQIIQQ", original, self.code_size + 1024 + index * 64, *row)
        self.original = bytes(original)
        self.original_path = self.root / "original-fixture.so"
        self.original_path.write_bytes(self.original)
        captured = bytearray(self.original[:self.code_size])
        captured[1024] ^= 1
        self.captured = bytes(captured)
        constants = {"CODE_BYTES": self.code_size, "GAME_LIBRARY_BYTES": len(self.original),
                     "GAME_LIBRARY_SHA256": digest(self.original), "HEADER_BYTES": 512,
                     "HEADER_SHA256": digest(self.original[:512])}
        patches = ExitStack()
        self.addCleanup(patches.close)
        for module in (analyzer, collector):
            patches.enter_context(mock.patch.multiple(module, **constants))
        self.apk = self.root / "module-fixture.zip"
        identity = {"schema": 1, "packageName": MODULE, "gamePackage": "jp.gungho.pad",
                    "moduleVersion": "0.1.0-probe", "sourceFingerprint": "a" * 64,
                    "gameVersion": "23.6.0", "gameVersionCode": 230600,
                    "gameApkSha256": collector.GAME_APK_SHA256,
                    "gameLibrarySha256": digest(self.original)}
        with zipfile.ZipFile(self.apk, "w") as archive:
            archive.writestr("assets/padcheaper-build.json", json.dumps(identity))
            archive.writestr("META-INF/xposed/java_init.list", MODULE + ".PadModule\n")
            archive.writestr("META-INF/xposed/scope.list", "jp.gungho.pad\n")
            archive.writestr("META-INF/xposed/module.prop",
                             "minApiVersion=102\ntargetApiVersion=102\nstaticScope=true\nautoHotReload=false\n")
            for abi, machine in (("x86_64", 62), ("arm64-v8a", 183)):
                header = bytearray(20)
                header[:6] = b"\x7fELF\x02\x01"
                struct.pack_into("<H", header, 18, machine)
                archive.writestr(f"lib/{abi}/libpadcheaper_runtime.so", header)
        self.expected = apk_identity(self.apk)
        self.process = {"pid": 2448, "uid": 10062, "abi": "x86_64", "startTicks": "12345",
                        "bootId": "11111111-2222-3333-4444-555555555555"}
        self.report = {"schema": 1, "captureKind": "runtime-code", "gamePackage": "jp.gungho.pad",
                       "moduleSourceFingerprint": "a" * 64, "moduleVersion": "0.1.0-probe", "pid": 2448,
                       "processIdentityAvailable": True, "processStartTicks": "12345",
                       "bootId": self.process["bootId"], "capturedElapsedMillis": 1000,
                       "captureFinishedElapsedMillis": 1100, "captureStatus": "complete",
                       "packageIdentityMatched": True, "gameVersion": "23.6.0", "gameVersionCode": 230600,
                       "libraryArtifactMatched": True, "nativePayloadLoaded": True, "nativePayloadAbi": "x86_64",
                       "nativePayloadSha256": self.expected["payloadSha256"]["x86_64"],
                       "frameDispatchPrepared": True, "foreground": True, "frameStatus": "observed",
                       "threadChanged": False, "foregroundFrames": 20, "frames": 40, "frameAgeMillis": 10,
                       "frameThread": 22, "captureStartThread": 22, "foregroundGeneration": 3,
                       "captureStartGeneration": 3, "runtimeCode": {"status": "code-captured",
                           "capturedBytes": self.code_size, "fileOffset": 0, "virtualAddress": 0,
                           "elfMachine": 183, "stableRead": True, "mappingsStable": True},
                       "codeFile": "code-2448-11111111-2222-3333-4444-555555555555.bin",
                       "codeSha256": digest(self.captured)}
        flags = {name: False for name in analyzer.CAPABILITY_FIELDS}
        self.report.update(flags)
        self.envelope = {"schema": 1, "status": "runtime-code-collected", "issues": [],
                         "expectedBuild": self.expected, "installedModuleApkSha256": self.expected["apkSha256"],
                         "captureContract": {"capturedBytes": self.code_size, "fileOffset": 0,
                             "virtualAddress": 0, "gameLibraryBytes": len(self.original),
                             "gameLibrarySha256": digest(self.original), "headerBytes": 512,
                             "headerSha256": digest(self.original[:512])},
                         "process": self.process, "finalDeviceElapsedMillis": 1200, "maxAgeMillisUsed": 300000,
                         "report": self.report, "acceptedCodeFile": self.report["codeFile"],
                         "acceptedCodeSha256": self.report["codeSha256"], "unpackedCodeVerified": False, **flags}

    def result_directory(self):
        directory = self.root / "capture" / "result"
        directory.mkdir(parents=True)
        (directory / "report.json").write_text(json.dumps(self.envelope), encoding="utf-8")
        (directory / self.report["codeFile"]).write_bytes(self.captured)
        return directory

    def test_archive_uses_recorded_device_clock_and_rejects_missing_or_expired_clock(self):
        with mock.patch.object(analyzer, "assess_runtime_code", wraps=collector.assess_runtime_code) as assess:
            with mock.patch("time.time", side_effect=AssertionError("wall clock must not be read")):
                analyzer.validate_envelope(self.envelope, self.expected)
            self.assertEqual((1200, 300000), assess.call_args.args[3:])
        for value in (None, True, -1, 999, 301001):
            with self.subTest(clock=value), self.assertRaises(ValueError):
                analyzer.validate_envelope({**self.envelope, "finalDeviceElapsedMillis": value}, self.expected)
        missing = copy.deepcopy(self.envelope)
        del missing["maxAgeMillisUsed"]
        with self.assertRaisesRegex(ValueError, "archived-device-clock"):
            analyzer.validate_envelope(missing, self.expected)

    def test_archive_rejects_capabilities_process_filename_and_apk_identity_changes(self):
        changes = [(("actionsEnabled",), 0), (("unpackedCodeVerified",), True),
                   (("report", "boardReadEnabled"), True), (("process", "pid"), True),
                   (("process", "startTicks"), "0"), (("process", "bootId"), "not-a-boot-id"),
                   (("report", "processStartTicks"), "54321"),
                   (("report", "codeFile"), "../outside.bin"),
                   (("report", "foregroundGeneration"), 4),
                   (("installedModuleApkSha256",), "d" * 64),
                   (("expectedBuild", "apkSha256"), "d" * 64)]
        for path, value in changes:
            changed = copy.deepcopy(self.envelope)
            target = changed
            for key in path[:-1]:
                target = target[key]
            target[path[-1]] = value
            with self.subTest(path=path), self.assertRaises(ValueError):
                analyzer.validate_envelope(changed, self.expected)

    def test_comparison_ranges_are_end_exclusive_and_include_the_final_partial_page(self):
        original = bytes(self.code_size)
        changed = bytearray(original)
        for offset in (10, 4096, 8191, self.code_size - 1):
            changed[offset] = 1
        result = analyzer.compare_bytes(bytes(changed), original)
        self.assertEqual(4, result["changedBytes"])
        self.assertEqual(3, result["changedPages"])
        self.assertEqual([{"start": 0, "endExclusive": 8192, "changedBytes": 3},
                          {"start": 12288, "endExclusive": self.code_size, "changedBytes": 1}],
                         result["changedPageSpans"])
        self.assertEqual([], analyzer.compare_bytes(original, original)["changedPageSpans"])
        with self.assertRaisesRegex(ValueError, "comparison-length"):
            analyzer.compare_bytes(bytes(changed[:-1]), original)

    def test_only_captured_original_sections_receive_runtime_hashes(self):
        sections = {s["name"]: s for s in analyzer.original_sections(self.original)}
        self.assertEqual({".text", ".data", ".bss"}, set(sections))
        self.assertTrue(sections[".text"]["fullyCaptured"])
        self.assertFalse(sections[".data"]["fullyCaptured"])
        self.assertFalse(sections[".bss"]["fullyCaptured"])
        result = analyzer.analyze(self.envelope, self.expected, self.captured, self.original)
        for section in result["originalSections"]:
            self.assertEqual(section["fullyCaptured"], "capturedSha256" in section)
            if section["name"] == ".text":
                self.assertFalse(section["identicalToOriginal"])
        self.assertFalse(result["archivedCollection"]["currentProcessRechecked"])
        self.assertTrue(all(result[name] is False for name in (*analyzer.CAPABILITY_FIELDS, "unpackedCodeVerified")))

    def test_wrong_original_capture_or_report_hash_is_refused(self):
        original = bytearray(self.original)
        original[-1] ^= 1
        captured = bytearray(self.captured)
        captured[-1] ^= 1
        changed_report = copy.deepcopy(self.envelope)
        changed_report["report"]["codeSha256"] = "d" * 64
        changed_report["acceptedCodeSha256"] = "d" * 64
        cases = [(self.envelope, self.captured, bytes(original), "original-library-identity"),
                 (self.envelope, bytes(captured), self.original, "content-hash-mismatch"),
                 (changed_report, self.captured, self.original, "content-hash-mismatch")]
        for envelope, code, library, reason in cases:
            with self.subTest(reason=reason), self.assertRaisesRegex(ValueError, reason):
                analyzer.analyze(envelope, self.expected, code, library)

    def test_load_and_publish_preserve_owned_bytes_and_checksum_every_output(self):
        directory = self.result_directory()
        report_bytes = (directory / "report.json").read_bytes()
        analysis, code, envelope_bytes = analyzer.load_inputs(directory, self.apk, self.original_path)
        self.assertEqual((self.captured, report_bytes), (code, envelope_bytes))
        self.assertEqual(digest(report_bytes), analysis["collectorReportSha256"])
        published = analyzer.publish_analysis(self.root / "analysis", analysis, code, envelope_bytes)
        self.assertEqual(self.captured, (published / "runtime-code.bin").read_bytes())
        self.assertEqual(report_bytes, (published / "collector-report.json").read_bytes())
        self.assertEqual(report_bytes, (directory / "report.json").read_bytes())
        loader = json.loads((published / "ida-load.json").read_text())
        self.assertEqual((0, self.code_size, "AArch64"),
                         (loader["loadVirtualAddress"], loader["endExclusive"], loader["architecture"]))
        self.assertFalse(loader["initializationExecuted"])
        checksums = (published / "SHA256SUMS").read_text().splitlines()
        self.assertEqual(5, len(checksums))
        for line in checksums:
            expected, name = line.split("  ", 1)
            self.assertEqual(expected, digest((published / name).read_bytes()))

    def test_load_rechecks_report_and_apk_after_reading_owned_byte_snapshots(self):
        directory = self.result_directory()
        report_path = directory / "report.json"
        original_report = report_path.read_bytes()
        real_read = analyzer.bounded_file

        def change_report(path, *args, **kwargs):
            result = real_read(path, *args, **kwargs)
            if Path(path) == self.original_path:
                report_path.write_bytes(original_report + b"\n")
            return result

        with mock.patch.object(analyzer, "bounded_file", side_effect=change_report):
            with self.assertRaisesRegex(ValueError, "report-changed-during-analysis"):
                analyzer.load_inputs(directory, self.apk, self.original_path)
        report_path.write_bytes(original_report)
        with mock.patch.object(analyzer, "apk_identity", side_effect=[self.expected, {**self.expected, "apkSha256": "e" * 64}]):
            with self.assertRaisesRegex(ValueError, "module-apk-changed-during-analysis"):
                analyzer.load_inputs(directory, self.apk, self.original_path)

    def test_output_is_exclusive_and_failed_writes_never_expose_result(self):
        analysis = analyzer.analyze(self.envelope, self.expected, self.captured, self.original)
        encoded = json.dumps(self.envelope).encode()
        output = self.root / "analysis"
        final = analyzer.publish_analysis(output, analysis, self.captured, encoded)
        before = {p.name: p.read_bytes() for p in final.iterdir()}
        with self.assertRaises(FileExistsError):
            analyzer.publish_analysis(output, analysis, b"replacement", encoded)
        self.assertEqual(before, {p.name: p.read_bytes() for p in final.iterdir()})
        failed = self.root / "failed-analysis"
        with mock.patch.object(analyzer.os, "fsync", side_effect=OSError("synthetic disk failure")):
            with self.assertRaisesRegex(OSError, "synthetic disk failure"):
                analyzer.publish_analysis(failed, analysis, self.captured, encoded)
        self.assertFalse((failed / "result").exists())

    def test_binary_and_report_symlinks_cannot_escape_the_result_directory(self):
        directory = self.result_directory()
        for name, reason in ((self.report["codeFile"], "capture-file-outside"), ("report.json", "report-file-outside")):
            inside = directory / name
            data = inside.read_bytes()
            outside = self.root / ("external-" + name)
            outside.write_bytes(data)
            inside.unlink()
            try:
                inside.symlink_to(outside)
            except (OSError, NotImplementedError) as error:
                inside.write_bytes(data)
                self.skipTest(f"symlink creation unavailable: {error}")
            try:
                with self.subTest(name=name), self.assertRaisesRegex(ValueError, reason):
                    analyzer.load_inputs(directory, self.apk, self.original_path)
            finally:
                inside.unlink()
                inside.write_bytes(data)


if __name__ == "__main__":
    unittest.main()

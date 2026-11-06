"""Collect one explicit runtime-code capture. Reads module files only; never captures target memory."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import time
import uuid
import zipfile

from collect_probe import Adb
from probe_contract import MAX_REPORT, PACKAGE, apk_identity, read_json


# This contract covers only the first, non-writable PT_LOAD in the verified
# PAD 23.6.0 library. These are metadata hashes, not shipped game bytes.
CODE_BYTES = 17295152
GAME_LIBRARY_BYTES = 30735496
HEADER_BYTES = 512
HEADER_SHA256 = "4d1fef531ae14a6d2c12b368b02ced94cab841d365a17554ceb94c09e430f55e"
GAME_LIBRARY_SHA256 = "fec9fbc55d3a9b7945817420475a8c03fa608ecc4605535e373ca2d0fc738a8a"
GAME_APK_SHA256 = "4c86b832ac41d29325402cb3ad4cedcde40abf585319c75a89c60b15200ef70b"
MAX_AGE_MS = 300000
MAX_CAPTURE_MS = 30000
UUID_PATTERN = r"[0-9a-f]{8}(?:-[0-9a-f]{4}){3}-[0-9a-f]{12}"


def target_matches(expected):
    return (expected.get("gamePackage") == PACKAGE and
            expected.get("gameVersion") == "23.6.0" and
            type(expected.get("gameVersionCode")) is int and expected["gameVersionCode"] == 230600 and
            expected.get("gameApkSha256") == GAME_APK_SHA256 and
            expected.get("gameLibrarySha256") == GAME_LIBRARY_SHA256)


def assess_runtime_code(report, expected, process, now_elapsed_ms, max_age_ms=MAX_AGE_MS):
    """Validate runtime-capture evidence directly; never synthesize a probe report."""
    issues = []

    def require(condition, reason):
        if not condition:
            issues.append(reason)

    def integer(value):
        return type(value) is int

    require(target_matches(expected), "unsupported-runtime-code-target")
    require(integer(report.get("schema")) and report["schema"] == 1, "report-schema-mismatch")
    require(report.get("captureKind") == "runtime-code", "capture-kind-mismatch")
    require(report.get("gamePackage") == PACKAGE, "game-package-mismatch")
    require(report.get("moduleSourceFingerprint") == expected["sourceFingerprint"], "module-source-mismatch")
    require(report.get("moduleVersion") == expected["moduleVersion"], "module-version-mismatch")
    require(integer(report.get("pid")) and report["pid"] == process["pid"], "process-pid-mismatch")
    require(report.get("processIdentityAvailable") is True, "process-identity-unavailable")
    require(report.get("processStartTicks") == process["startTicks"], "process-start-mismatch")
    require(report.get("bootId") == process["bootId"], "boot-mismatch")
    started, finished = report.get("capturedElapsedMillis"), report.get("captureFinishedElapsedMillis")
    require(integer(now_elapsed_ms) and integer(max_age_ms) and 0 < max_age_ms <= MAX_AGE_MS and
            integer(started) and started >= 0 and 0 <= now_elapsed_ms - started <= max_age_ms,
            "report-expired-or-invalid-time")
    require(integer(started) and started >= 0 and integer(finished) and integer(now_elapsed_ms) and
            started <= finished <= now_elapsed_ms and finished - started <= MAX_CAPTURE_MS,
            "capture-duration-invalid")
    require(report.get("captureStatus") == "complete", "capture-failed")
    require(report.get("packageIdentityMatched") is True, "game-artifact-unverified")
    require(report.get("gameVersion") == expected["gameVersion"] and
            integer(report.get("gameVersionCode")) and report["gameVersionCode"] == expected["gameVersionCode"],
            "game-version-mismatch")
    require(report.get("libraryArtifactMatched") is True, "game-library-unverified")
    require(report.get("nativePayloadLoaded") is True, "payload-not-loaded")
    require(report.get("nativePayloadAbi") == process["abi"], "payload-process-abi-mismatch")
    require(report.get("nativePayloadSha256") == expected["payloadSha256"].get(process["abi"]),
            "payload-hash-mismatch")
    require(report.get("frameDispatchPrepared") is True, "frame-dispatch-unprepared")
    require(report.get("foreground") is True and report.get("frameStatus") == "observed", "frames-not-current")
    require(report.get("threadChanged") is False, "frame-thread-changed")
    frames, total = report.get("foregroundFrames"), report.get("frames")
    require(integer(frames) and frames >= 2 and integer(total) and total >= frames, "insufficient-frame-evidence")
    age = report.get("frameAgeMillis")
    require(integer(age) and 0 <= age <= 2000, "frame-age-invalid")
    thread, start_thread = report.get("frameThread"), report.get("captureStartThread")
    require(integer(thread) and thread > 0 and integer(start_thread) and start_thread == thread,
            "capture-frame-thread-changed-or-invalid")
    generation, start_generation = report.get("foregroundGeneration"), report.get("captureStartGeneration")
    require(integer(generation) and generation > 0 and integer(start_generation) and start_generation == generation,
            "capture-foreground-generation-changed-or-invalid")
    runtime = report.get("runtimeCode")
    require(isinstance(runtime, dict), "runtime-code-evidence-missing")
    if isinstance(runtime, dict):
        require(runtime.get("status") == "code-captured", "runtime-code-not-captured")
        require(integer(runtime.get("capturedBytes")) and runtime["capturedBytes"] == CODE_BYTES and
                integer(runtime.get("fileOffset")) and runtime["fileOffset"] == 0 and
                integer(runtime.get("virtualAddress")) and runtime["virtualAddress"] == 0 and
                integer(runtime.get("elfMachine")) and runtime["elfMachine"] == 183,
                "runtime-code-range-mismatch")
        require(runtime.get("stableRead") is True and runtime.get("mappingsStable") is True,
                "runtime-code-not-stable")
    filename = report.get("codeFile")
    require(isinstance(filename, str) and
            re.fullmatch(rf"code-{process['pid']}-{UUID_PATTERN}\.bin", filename) is not None,
            "runtime-code-filename-invalid")
    digest = report.get("codeSha256")
    require(isinstance(digest, str) and re.fullmatch(r"[0-9a-f]{64}", digest) is not None,
            "runtime-code-hash-invalid")
    for field in ("runtimeLayoutVerified", "boardReadEnabled", "actionsEnabled", "deviceVerified"):
        require(report.get(field) is False, f"unexpected-capability:{field}")
    return issues


def assess_code_bytes(data, report):
    issues = []
    if len(data) != CODE_BYTES:
        issues.append("runtime-code-size-mismatch")
    if hashlib.sha256(data).hexdigest() != report["codeSha256"]:
        issues.append("runtime-code-content-hash-mismatch")
    if hashlib.sha256(data[:HEADER_BYTES]).hexdigest() != HEADER_SHA256:
        issues.append("runtime-code-header-mismatch")
    return issues


def collect_runtime_code(adb, expected, max_age_ms=MAX_AGE_MS):
    """Return (evidence, accepted bytes or None); recheck identity after the binary read."""
    result = {"schema": 1, "collectedAtMillis": int(time.time() * 1000), "status": "incomplete",
              "expectedBuild": expected, "issues": [], "deviceVerified": False,
              "runtimeLayoutVerified": False, "boardReadEnabled": False, "actionsEnabled": False,
              "unpackedCodeVerified": False,
              "captureContract": {"capturedBytes": CODE_BYTES, "fileOffset": 0, "virtualAddress": 0,
                                  "gameLibraryBytes": GAME_LIBRARY_BYTES, "gameLibrarySha256": GAME_LIBRARY_SHA256,
                                  "headerBytes": HEADER_BYTES, "headerSha256": HEADER_SHA256},
              "contentMeaning": "Stable captured bytes only; executable decoding and layout are unverified."}
    accepted = None
    try:
        if not target_matches(expected):
            raise ValueError("unsupported-runtime-code-target")
        pid = adb.main_pid()
        before = adb.process(pid)
        result["process"] = before
        user = before["uid"] // 100000
        installed_hash = adb.installed_module_hash(user)
        result["installedModuleApkSha256"] = installed_hash
        if installed_hash != expected["apkSha256"]:
            result["issues"].append("installed-module-apk-mismatch")
        # Only this tool's module-produced files are read. Never read game heap,
        # arbitrary caller-supplied paths, or a filename taken from an old PID.
        remote = f"/data/user/{user}/{PACKAGE}/files/padcheaper"
        report_path = f"{remote}/code-{pid}.json"
        report_bytes = adb.root_file(report_path, MAX_REPORT)
        report = read_json(report_bytes)
        result["report"] = report
        result["issues"].extend(assess_runtime_code(report, expected, before, adb.elapsed_ms(), max_age_ms))
        if not result["issues"]:
            # root_file asks head for one extra byte and rejects overflow.
            data = adb.root_file(f"{remote}/{report['codeFile']}", CODE_BYTES)
            result["issues"].extend(assess_code_bytes(data, report))
            if not result["issues"]:
                accepted = data
        if adb.root_file(report_path, MAX_REPORT) != report_bytes:
            raise ValueError("report-changed-during-collection")
        after = adb.process(adb.main_pid())
        if before != after:
            raise ValueError("process-changed-during-collection")
        if adb.installed_module_hash(user) != installed_hash:
            raise ValueError("module-changed-during-collection")
        # The capture may expire while the binary transfers. Assess again using
        # the final device elapsed clock before accepting any output bytes. Save
        # this exact reading and policy so archived evidence can be revalidated
        # at collection time without inventing a device clock from wall time.
        final_elapsed_ms = adb.elapsed_ms()
        if type(final_elapsed_ms) is not int or final_elapsed_ms < 0:
            raise ValueError("final-device-elapsed-invalid")
        result["finalDeviceElapsedMillis"] = final_elapsed_ms
        result["maxAgeMillisUsed"] = max_age_ms
        for issue in assess_runtime_code(report, expected, after, final_elapsed_ms, max_age_ms):
            if issue not in result["issues"]:
                result["issues"].append(issue)
        result["status"] = "needs-attention" if result["issues"] else "runtime-code-collected"
    except (ValueError, OSError, subprocess.SubprocessError, KeyError, IndexError, UnicodeError) as error:
        result["issues"].append(str(error))
    if result["status"] != "runtime-code-collected":
        accepted = None
    return result, accepted


def reserve_output(path):
    """Reserve the requested new directory without replacing any existing output."""
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.mkdir()
    return path


def publish_result(output, result, data):
    """Atomically expose result/report.json plus its binary inside a reserved output directory."""
    output = Path(output)
    if (result["status"] == "runtime-code-collected") != (data is not None):
        raise ValueError("accepted-report-and-binary-must-be-published-together")
    final = output / "result"
    if final.exists():
        raise FileExistsError("result already exists; choose a new output directory")
    staging = output / (".pending-" + uuid.uuid4().hex)
    staging.mkdir()
    if data is not None:
        name = result["report"]["codeFile"]
        if not re.fullmatch(rf"code-{result['process']['pid']}-{UUID_PATTERN}\.bin", name):
            raise ValueError("runtime-code-filename-invalid")
        with (staging / name).open("xb") as stream:
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        result = {**result, "acceptedCodeFile": name, "acceptedCodeSha256": hashlib.sha256(data).hexdigest()}
    with (staging / "report.json").open("x", encoding="utf-8") as stream:
        json.dump(result, stream, ensure_ascii=False, indent=2)
        stream.write("\n")
        stream.flush()
        os.fsync(stream.fileno())
    # output was exclusively reserved by this invocation. A directory rename
    # exposes the complete pair at once; interrupted writes stay in .pending-*.
    staging.rename(final)
    return final


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--adb", default="adb", help="path to adb executable")
    parser.add_argument("--serial", required=True)
    parser.add_argument("--apk", required=True, type=Path, help="exact installed module APK")
    parser.add_argument("--out", required=True, type=Path, help="new directory; atomically publishes result/ inside it")
    parser.add_argument("--max-age-seconds", type=int, default=300)
    args = parser.parse_args()
    if not re.fullmatch(r"[A-Za-z0-9_.:-]+", args.serial) or not 1 <= args.max_age_seconds <= 300:
        parser.error("invalid serial or maximum report age (1-300 seconds)")
    try:
        expected = apk_identity(args.apk)
        if not target_matches(expected):
            raise ValueError("unsupported-runtime-code-target")
        output = reserve_output(args.out)
    except (OSError, ValueError, KeyError, zipfile.BadZipFile) as error:
        parser.error(str(error))
    result, data = collect_runtime_code(Adb(args.adb, args.serial), expected, args.max_age_seconds * 1000)
    result["serial"] = args.serial
    try:
        published = publish_result(output, result, data)
    except (OSError, ValueError, KeyError) as error:
        print(f"Output not published: {error}")
        return 1
    print(result["status"])
    print(f"Evidence: {published / 'report.json'}")
    for issue in result["issues"]:
        print("- " + issue)
    return 0 if result["status"] == "runtime-code-collected" else 1


if __name__ == "__main__":
    raise SystemExit(main())

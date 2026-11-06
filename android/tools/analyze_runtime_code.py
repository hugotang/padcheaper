"""Offline verification and byte comparison of an exported runtime-code result.

No ADB, execution, disassembly, ELF reconstruction, or automatic layout approval.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import stat
import struct
import uuid
import zipfile

from collect_runtime_code import (CODE_BYTES, GAME_LIBRARY_BYTES, GAME_LIBRARY_SHA256,
                                  HEADER_BYTES, HEADER_SHA256, MAX_AGE_MS,
                                  UUID_PATTERN, assess_code_bytes, assess_runtime_code,
                                  target_matches)
from probe_contract import ABIS, MAX_REPORT, apk_identity, read_json

PAGE_BYTES = 4096
MAX_SECTIONS = 256
CAPABILITY_FIELDS = ("deviceVerified", "runtimeLayoutVerified", "boardReadEnabled", "actionsEnabled")


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def bounded_file(path, limit, exact=None):
    path = Path(path)
    with path.open("rb") as stream:
        before = os.fstat(stream.fileno())
        if not stat.S_ISREG(before.st_mode) or not 0 < before.st_size <= limit:
            raise ValueError(f"input-not-bounded-regular-file:{path.name}")
        if exact is not None and before.st_size != exact:
            raise ValueError(f"input-size-mismatch:{path.name}")
        data = stream.read(limit + 1)
        after = os.fstat(stream.fileno())
    if len(data) != before.st_size or (before.st_size, before.st_mtime_ns) != (after.st_size, after.st_mtime_ns):
        raise ValueError(f"input-changed-during-read:{path.name}")
    return data


def require(condition, reason):
    if not condition:
        raise ValueError(reason)


def validate_envelope(envelope, expected):
    """Validate archived evidence against its recorded final device clock, never today's clock."""
    require(type(envelope.get("schema")) is int and envelope["schema"] == 1, "unsupported-envelope-schema")
    require(envelope.get("status") == "runtime-code-collected" and envelope.get("issues") == [],
            "capture-envelope-not-accepted")
    require(envelope.get("expectedBuild") == expected, "capture-apk-identity-mismatch")
    require(envelope.get("installedModuleApkSha256") == expected["apkSha256"], "installed-apk-hash-mismatch")
    require(target_matches(expected), "unsupported-runtime-code-target")
    require(all(envelope.get(field) is False for field in CAPABILITY_FIELDS) and
            envelope.get("unpackedCodeVerified") is False, "unexpected-envelope-capability")
    contract = envelope.get("captureContract")
    required_contract = {"capturedBytes": CODE_BYTES, "fileOffset": 0, "virtualAddress": 0,
                         "gameLibraryBytes": GAME_LIBRARY_BYTES, "gameLibrarySha256": GAME_LIBRARY_SHA256,
                         "headerBytes": HEADER_BYTES, "headerSha256": HEADER_SHA256}
    require(isinstance(contract, dict) and all(type(contract.get(k)) is type(v) and contract[k] == v
            for k, v in required_contract.items()), "capture-contract-mismatch")
    process = envelope.get("process")
    require(isinstance(process, dict), "missing-process-evidence")
    require(type(process.get("pid")) is int and 0 < process["pid"] <= 2**31 - 1 and
            type(process.get("uid")) is int and 10000 <= process["uid"] <= 2**31 - 1 and
            process.get("abi") in ABIS, "invalid-process-evidence")
    ticks, boot = process.get("startTicks"), process.get("bootId")
    require(isinstance(ticks, str) and re.fullmatch(r"[1-9][0-9]{0,18}", ticks) is not None and
            int(ticks) <= 2**63 - 1, "invalid-process-start-ticks")
    require(isinstance(boot, str) and re.fullmatch(UUID_PATTERN, boot) is not None, "invalid-boot-identity")
    final_clock, age = envelope.get("finalDeviceElapsedMillis"), envelope.get("maxAgeMillisUsed")
    require(type(final_clock) is int and final_clock >= 0 and type(age) is int and 0 < age <= MAX_AGE_MS,
            "missing-or-invalid-archived-device-clock")
    report = envelope.get("report")
    require(isinstance(report, dict), "missing-runtime-report")
    issues = assess_runtime_code(report, expected, process, final_clock, age)
    require(not issues, "archived-report-invalid:" + ",".join(issues))
    require(envelope.get("acceptedCodeFile") == report["codeFile"] and
            envelope.get("acceptedCodeSha256") == report["codeSha256"], "accepted-file-identity-mismatch")
    return report


def original_sections(original):
    """Read bounded ELF metadata from the exact original, not the incomplete capture."""
    require(len(original) == GAME_LIBRARY_BYTES and sha256(original) == GAME_LIBRARY_SHA256,
            "original-library-identity-mismatch")
    require(sha256(original[:HEADER_BYTES]) == HEADER_SHA256, "original-header-mismatch")
    phoff = struct.unpack_from("<Q", original, 32)[0]
    phentsize, phnum = struct.unpack_from("<HH", original, 54)
    require((phoff, phentsize, phnum) == (64, 56, 8), "original-program-table-mismatch")
    program = struct.unpack_from("<IIQQQQQQ", original, phoff + phentsize)
    require(program == (1, 5, 0, 0, 0, CODE_BYTES, CODE_BYTES, 16384), "original-captured-segment-mismatch")
    shoff = struct.unpack_from("<Q", original, 40)[0]
    shentsize, shnum, shstrndx = struct.unpack_from("<HHH", original, 58)
    require(shentsize == 64 and 0 < shnum <= MAX_SECTIONS and shstrndx < shnum and
            shoff + shnum * shentsize <= len(original), "invalid-original-section-table")
    raw = [struct.unpack_from("<IIQQQQIIQQ", original, shoff + i * shentsize) for i in range(shnum)]
    strings = raw[shstrndx]
    require(strings[1] == 3 and 0 < strings[5] <= 65536 and strings[4] + strings[5] <= len(original),
            "invalid-original-section-names")
    names = original[strings[4]:strings[4] + strings[5]]
    sections = []
    for item in raw:
        name_offset, kind, flags, address, offset, size = item[:6]
        require(name_offset < len(names), "invalid-original-section-name-offset")
        end = names.find(b"\0", name_offset)
        require(end >= name_offset and end - name_offset <= 256, "invalid-original-section-name")
        name = names[name_offset:end].decode("ascii")
        if not flags & 2 or not size:  # Only allocated sections; the rest stay original-file metadata.
            continue
        if kind != 8:  # SHT_NOBITS has no file payload.
            require(offset <= len(original) and size <= len(original) - offset, "invalid-original-section-extent")
        captured = kind != 8 and address == offset and offset < CODE_BYTES and size <= CODE_BYTES - offset
        sections.append({"name": name, "type": kind, "elfFlags": flags,
                         "fileOffset": offset, "virtualAddress": address, "bytes": size,
                         "fullyCaptured": captured, "metadataSource": "exact-original-ELF-section-table"})
    return sections


def compare_bytes(captured, original_segment):
    require(len(captured) == len(original_segment) == CODE_BYTES, "comparison-length-mismatch")
    changed_bytes, changed_pages, spans = 0, 0, []
    for offset in range(0, CODE_BYTES, PAGE_BYTES):
        end = min(offset + PAGE_BYTES, CODE_BYTES)
        left, right = captured[offset:end], original_segment[offset:end]
        if left == right:
            continue
        count = sum(a != b for a, b in zip(left, right))
        changed_bytes += count
        changed_pages += 1
        if spans and spans[-1]["endExclusive"] == offset:
            spans[-1]["endExclusive"] = end
            spans[-1]["changedBytes"] += count
        else:
            spans.append({"start": offset, "endExclusive": end, "changedBytes": count})
    return {"capturedSha256": sha256(captured), "originalSegmentSha256": sha256(original_segment),
            "identicalToOriginalSegment": changed_bytes == 0, "changedBytes": changed_bytes,
            "pageBytes": PAGE_BYTES, "changedPages": changed_pages,
            "changedPageSpans": spans,
            "spanMeaning": "Page-aligned candidate ranges, end exclusive; not functions or exact change boundaries."}


def analyze(envelope, expected, captured, original):
    report = validate_envelope(envelope, expected)
    issues = assess_code_bytes(captured, report)
    require(not issues, "captured-bytes-invalid:" + ",".join(issues))
    sections = original_sections(original)
    for section in sections:
        if section["fullyCaptured"]:
            offset, size = section["fileOffset"], section["bytes"]
            first, last = offset, offset + size
            section["capturedSha256"] = sha256(captured[first:last])
            section["originalSha256"] = sha256(original[first:last])
            section["identicalToOriginal"] = captured[first:last] == original[first:last]
    comparison = compare_bytes(captured, original[:CODE_BYTES])
    return {"schema": 1, "status": "offline-code-comparison-ready",
            "sourceFingerprint": expected["sourceFingerprint"], "moduleApkSha256": expected["apkSha256"],
            "originalLibrarySha256": GAME_LIBRARY_SHA256, "captureSha256": report["codeSha256"],
            "archivedCollection": {"finalDeviceElapsedMillis": envelope["finalDeviceElapsedMillis"],
                                   "maxAgeMillisUsed": envelope["maxAgeMillisUsed"],
                                   "pid": envelope["process"]["pid"], "currentProcessRechecked": False},
            "comparison": comparison, "originalSections": sections,
            "interpretation": "Matching or changed bytes alone cannot prove successful unpacking or a board layout.",
            "deviceVerified": False, "runtimeLayoutVerified": False, "boardReadEnabled": False,
            "actionsEnabled": False, "unpackedCodeVerified": False}


def load_inputs(result_directory, module_apk, original_library):
    directory = Path(result_directory).resolve(strict=True)
    require(directory.is_dir() and directory.name == "result", "expected-collector-result-directory")
    report_path = directory / "report.json"
    require(report_path.resolve(strict=True).parent == directory, "report-file-outside-result-directory")
    envelope_bytes = bounded_file(report_path, MAX_REPORT)
    envelope = read_json(envelope_bytes)
    expected = apk_identity(module_apk)
    report = validate_envelope(envelope, expected)
    binary = directory / report["codeFile"]
    require(binary.resolve(strict=True).parent == directory, "capture-file-outside-result-directory")
    captured = bounded_file(binary, CODE_BYTES, CODE_BYTES)
    original = bounded_file(original_library, GAME_LIBRARY_BYTES, GAME_LIBRARY_BYTES)
    analysis = analyze(envelope, expected, captured, original)
    # Re-read provenance after potentially slow reads; all output comes from these owned byte snapshots.
    require(report_path.resolve(strict=True).parent == directory and
            bounded_file(report_path, MAX_REPORT) == envelope_bytes, "report-changed-during-analysis")
    require(apk_identity(module_apk) == expected, "module-apk-changed-during-analysis")
    analysis["collectorReportSha256"] = sha256(envelope_bytes)
    return analysis, captured, envelope_bytes


def publish_analysis(output, analysis, captured, envelope_bytes):
    output = Path(output)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.mkdir()  # Exclusive reservation; never overwrite a prior analysis or delivery.
    staging = output / (".pending-" + uuid.uuid4().hex)
    staging.mkdir()
    loader = {"schema": 1, "file": "runtime-code.bin", "sha256": sha256(captured),
              "loader": "Binary (raw), forced; do not auto-detect ELF", "architecture": "AArch64",
              "addressBits": 64, "byteOrder": "little", "fileOffset": 0, "loadVirtualAddress": 0,
              "lengthBytes": CODE_BYTES, "endExclusive": CODE_BYTES, "initialSegmentKind": "data/unknown",
              "addressMeaning": "ELF-relative virtual address, not a live process address",
              "uncapturedRanges": "Outside [0, 0x107e730) remains unknown, including writable PT_LOAD and BSS",
              "initializationExecuted": False, "layoutVerified": False}
    notes = """# Android runtime code comparison

This folder contains an exact copy of the accepted captured bytes and offline comparison metadata.
No new device check, native execution, disassembly or board-layout approval was performed.

## IDA import

Force the Binary/raw loader despite the ELF magic. Select AArch64, 64-bit, little-endian,
file offset 0, load address 0, length 0x107e730. Keep the initial segment data/unknown.
Before further analysis, verify IDA preserved the exact byte hash and extent in ida-load.json.
The local IDA tool's 64-bit raw-import behavior must be verified at the next authorized test.

The extent includes ELF metadata, read-only data, unwind tables, and code. Mark instructions and
functions only where independently supported. analysis.json section labels come from the exact
original ELF, not from runtime symbol recovery. Outside the captured extent remains unknown;
do not load the missing writable/BSS ranges, zero-fill them, or copy them from the original file.

changedPageSpans are byte-comparison candidates, not recovered functions. Unchanged bytes do not
prove failed unpacking, and changed bytes do not prove successful unpacking. Never load this
partial image as a complete ELF, modify the original capture, or execute it.

Files: analysis.json, ida-load.json, runtime-code.bin, collector-report.json, SHA256SUMS.
Checksums establish content consistency, not authenticity of an untrusted source.
"""
    contents = {"runtime-code.bin": captured, "collector-report.json": envelope_bytes,
                "analysis.json": (json.dumps(analysis, indent=2, ensure_ascii=False) + "\n").encode("utf-8"),
                "ida-load.json": (json.dumps(loader, indent=2) + "\n").encode("utf-8"),
                "README.md": notes.encode("utf-8")}
    contents["SHA256SUMS"] = "".join(f"{sha256(data)}  {name}\n" for name, data in sorted(contents.items())).encode("ascii")
    for name, data in contents.items():
        with (staging / name).open("xb") as stream:
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
    final = output / "result"
    staging.rename(final)
    return final


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--capture", required=True, type=Path, help="collector result/ directory")
    parser.add_argument("--apk", required=True, type=Path, help="exact module APK used for that capture")
    parser.add_argument("--original-library", required=True, type=Path, help="exact original libpad.so")
    parser.add_argument("--out", required=True, type=Path, help="new analysis directory, outside the capture")
    args = parser.parse_args()
    try:
        capture_path = args.capture.resolve(strict=True)
        output_path = args.out.resolve()
        require(not output_path.is_relative_to(capture_path), "analysis-output-must-be-outside-capture")
        analysis, captured, envelope = load_inputs(args.capture, args.apk, args.original_library)
        destination = publish_analysis(args.out, analysis, captured, envelope)
    except (OSError, ValueError, KeyError, TypeError, struct.error, zipfile.BadZipFile) as error:
        print(f"Analysis not published: {error}")
        return 1
    print(analysis["status"])
    print(f"Changed bytes: {analysis['comparison']['changedBytes']}; changed pages: {analysis['comparison']['changedPages']}")
    print(f"Analysis: {destination / 'analysis.json'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

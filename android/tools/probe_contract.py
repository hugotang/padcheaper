"""APK/report identity checks. These checks never authorize game actions."""

import hashlib
import io
import json
from pathlib import Path
import re
import zipfile

PACKAGE = "jp.gungho.pad"
MODULE = "io.github.hugotang.padcheaper"
MAX_REPORT = 256 * 1024
ABIS = {"x86_64": 62, "arm64-v8a": 183}


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f"duplicate JSON key: {key}")
        result[key] = value
    return result


def read_json(data):
    def invalid_constant(value):
        raise ValueError(f"non-finite JSON number: {value}")

    result = json.loads(data, object_pairs_hook=unique_object, parse_constant=invalid_constant)
    if not isinstance(result, dict):
        raise ValueError("expected JSON object")
    return result


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def elf_abi(header):
    if len(header) < 20 or header[:6] != b"\x7fELF\x02\x01":
        raise ValueError("unsupported ELF header")
    machine = int.from_bytes(header[18:20], "little")
    for abi, expected in ABIS.items():
        if machine == expected:
            return abi
    raise ValueError("unsupported ELF machine")


def process_start_ticks(stat):
    end = stat.rfind(")")
    if end < 0 or stat[end + 1:end + 2] != " ":
        raise ValueError("invalid process stat")
    fields = stat[end + 2:].split()
    if len(fields) < 20 or not re.fullmatch(r"[0-9]+", fields[19]):
        raise ValueError("missing process starttime")
    ticks = int(fields[19])
    if not 0 < ticks <= 2**63 - 1:
        raise ValueError("invalid process starttime")
    return str(ticks)


def apk_identity(path):
    path = Path(path)
    if not 0 < path.stat().st_size <= 128 * 1024 * 1024:
        raise ValueError("module APK exceeds size bound")
    apk = path.read_bytes()
    with zipfile.ZipFile(io.BytesIO(apk)) as archive:
        names = archive.namelist()
        if len(names) != len(set(names)):
            raise ValueError("duplicate APK entry")

        def entry(name, limit):
            info = archive.getinfo(name)
            if not 0 < info.file_size <= limit:
                raise ValueError(f"APK entry exceeds size bound: {name}")
            with archive.open(info) as stream:
                data = stream.read(limit + 1)
            if len(data) != info.file_size:
                raise ValueError(f"APK entry length differs: {name}")
            return data

        identity = read_json(entry("assets/padcheaper-build.json", MAX_REPORT))
        if type(identity.get("schema")) is not int or identity["schema"] != 1 or \
                identity.get("packageName") != MODULE or identity.get("gamePackage") != PACKAGE:
            raise ValueError("unexpected module build identity")
        for field in ("sourceFingerprint", "gameApkSha256", "gameLibrarySha256"):
            if not isinstance(identity.get(field), str) or not re.fullmatch(r"[0-9a-f]{64}", identity[field]):
                raise ValueError(f"invalid build identity: {field}")
        for field in ("moduleVersion", "gameVersion"):
            if not isinstance(identity.get(field), str) or not identity[field]:
                raise ValueError(f"missing build identity: {field}")
        if type(identity.get("gameVersionCode")) is not int or identity["gameVersionCode"] <= 0:
            raise ValueError("invalid game version code")
        if entry("META-INF/xposed/java_init.list", 4096).decode().strip() != MODULE + ".PadModule":
            raise ValueError("module Java entry differs")
        if entry("META-INF/xposed/scope.list", 4096).decode().strip() != PACKAGE:
            raise ValueError("module scope differs")
        properties = {}
        for line in entry("META-INF/xposed/module.prop", 4096).decode().splitlines():
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            key, separator, value = line.partition("=")
            key, value = key.strip(), value.strip()
            if not separator or key in properties:
                raise ValueError("invalid module properties")
            properties[key] = value
        if any(properties.get(key) != value for key, value in {
                "minApiVersion": "102", "targetApiVersion": "102", "staticScope": "true", "autoHotReload": "false"}.items()):
            raise ValueError("module framework requirements differ")
        payloads = {}
        for abi in ABIS:
            payload = entry(f"lib/{abi}/libpadcheaper_runtime.so", 64 * 1024 * 1024)
            if elf_abi(payload) != abi:
                raise ValueError(f"payload CPU differs from directory: {abi}")
            payloads[abi] = sha256(payload)
        return {**identity, "apkSha256": sha256(apk), "payloadSha256": payloads}


def assess_report(report, expected, process, now_elapsed_ms, max_age_ms):
    """Return explicit issues; an empty result means entry evidence only, never device verification."""
    issues = []

    def require(condition, reason):
        if not condition:
            issues.append(reason)

    def integer(value):
        return type(value) is int

    require(integer(report.get("schema")) and report["schema"] == 3, "report-schema-mismatch")
    require(report.get("gamePackage") == PACKAGE, "game-package-mismatch")
    require(report.get("moduleSourceFingerprint") == expected["sourceFingerprint"], "module-source-mismatch")
    require(report.get("moduleVersion") == expected["moduleVersion"], "module-version-mismatch")
    require(integer(report.get("pid")) and report["pid"] == process["pid"], "process-pid-mismatch")
    require(report.get("processIdentityAvailable") is True, "process-identity-unavailable")
    require(report.get("processStartTicks") == process["startTicks"], "process-start-mismatch")
    require(report.get("bootId") == process["bootId"], "boot-mismatch")
    captured = report.get("capturedElapsedMillis")
    require(integer(captured) and 0 <= now_elapsed_ms - captured <= max_age_ms, "report-expired-or-invalid-time")
    require(report.get("captureStatus") == "complete", "capture-failed")
    require(report.get("packageIdentityMatched") is True, "game-artifact-unverified")
    require(report.get("gameVersion") == expected["gameVersion"] and
            integer(report.get("gameVersionCode")) and report["gameVersionCode"] == expected["gameVersionCode"],
            "game-version-mismatch")
    require(report.get("libraryArtifactMatched") is True, "game-library-unverified")
    require(report.get("nativePayloadLoaded") is True, "payload-not-loaded")
    abi = report.get("nativePayloadAbi")
    require(abi == process["abi"], "payload-process-abi-mismatch")
    require(report.get("nativePayloadSha256") == expected["payloadSha256"].get(process["abi"]), "payload-hash-mismatch")
    require(report.get("frameDispatchPrepared") is True, "frame-dispatch-unprepared")
    require(report.get("foreground") is True and report.get("frameStatus") == "observed", "frames-not-current")
    require(report.get("threadChanged") is False, "frame-thread-changed")
    frames, total = report.get("foregroundFrames"), report.get("frames")
    require(integer(frames) and frames >= 2 and integer(total) and total >= frames, "insufficient-frame-evidence")
    age = report.get("frameAgeMillis")
    require(integer(age) and 0 <= age <= 2000, "frame-age-invalid")
    require(integer(report.get("frameThread")) and report["frameThread"] > 0, "frame-thread-invalid")
    library = report.get("library")
    require(isinstance(library, dict) and library.get("status") == "arm64-image-observed" and
            type(library.get("elfMachine")) is int and library["elfMachine"] == 183, "library-image-unverified")
    for field in ("runtimeLayoutVerified", "boardReadEnabled", "actionsEnabled", "deviceVerified"):
        require(report.get(field) is False, f"unexpected-capability:{field}")
    return issues

"""Read one current game's probe via ADB. Never installs, restarts, taps, writes or changes root settings."""

import argparse
from decimal import Decimal
import json
from pathlib import Path
import re
import shlex
import subprocess
import tempfile
import time
import zipfile

from probe_contract import (MAX_REPORT, MODULE, PACKAGE, apk_identity, assess_report,
                            elf_abi, process_start_ticks, read_json)


class Adb:
    def __init__(self, executable, serial):
        self.prefix = [executable, "-s", serial]

    def run(self, *arguments, limit=MAX_REPORT):
        # All commands are finite and read-only. Tempfiles bound in-memory capture;
        # proc/report reads are bounded on-device by head as well.
        with tempfile.TemporaryFile() as output, tempfile.TemporaryFile() as error:
            result = subprocess.run(self.prefix + list(arguments), stdout=output, stderr=error,
                                    timeout=15, check=False)
            output.seek(0)
            data = output.read(limit + 1)
            error.seek(0)
            diagnostic = error.read(4096).decode("utf-8", errors="replace").strip()
        if len(data) > limit:
            raise ValueError("ADB response exceeds size bound")
        if result.returncode:
            raise ValueError(f"ADB read failed ({result.returncode}): {diagnostic or data.decode('utf-8', errors='replace')[:500]}")
        return data

    def root_command(self, command, limit=MAX_REPORT):
        return self.run("exec-out", "su -c " + shlex.quote(command), limit=limit)

    def root_file(self, path, limit=8192):
        # Read one extra byte so truncation is detected, rather than parsing a prefix.
        return self.root_command(f"head -c {limit + 1} {shlex.quote(path)}", limit=limit)

    def main_pid(self):
        # pidof's no-process exit status is normalized; other errors still fail parsing.
        raw = self.run("shell", f"pidof {PACKAGE} || true", limit=4096).decode().strip()
        pids = raw.split()
        if not 1 <= len(pids) <= 16 or any(not p.isdigit() or int(p) <= 0 for p in pids):
            raise ValueError("main-process-unavailable-or-ambiguous")
        candidates = set(map(int, pids))
        parents, uids = {}, {}
        for pid in candidates:
            if self.root_file(f"/proc/{pid}/cmdline", 4096).split(b"\0", 1)[0].decode() != PACKAGE:
                raise ValueError("process-name-mismatch")
            status = self.root_file(f"/proc/{pid}/status", 16384).decode()
            parent = re.search(r"^PPid:\s+(\d+)\s*$", status, re.MULTILINE)
            uid = re.search(r"^Uid:\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s*$", status, re.MULTILINE)
            if not parent or not uid or len(set(uid.groups())) != 1:
                raise ValueError("invalid-process-tree")
            parents[pid], uids[pid] = int(parent[1]), int(uid[1])
        roots = [pid for pid in candidates if parents[pid] not in candidates]
        if len(roots) != 1 or len(set(uids.values())) != 1:
            raise ValueError("main-process-unavailable-or-ambiguous")
        main = roots[0]
        parent_name = self.root_file(f"/proc/{parents[main]}/cmdline", 4096).split(b"\0", 1)[0]
        if parent_name not in (b"zygote", b"zygote64", b"zygote32"):
            raise ValueError("main-process-parent-unverified")
        # MuMu/game forks retain the main process name. Accept descendants only;
        # never pick a PID by list order or by an old report's filename.
        for pid in candidates:
            seen = set()
            while pid != main:
                if pid in seen or pid not in parents:
                    raise ValueError("invalid-process-tree")
                seen.add(pid)
                pid = parents[pid]
        return main

    def process(self, pid):
        if self.root_file(f"/proc/{pid}/cmdline", 4096).split(b"\0", 1)[0].decode() != PACKAGE:
            raise ValueError("process-name-mismatch")
        ticks = process_start_ticks(self.root_file(f"/proc/{pid}/stat").decode())
        boot = self.root_file("/proc/sys/kernel/random/boot_id").decode().strip().lower()
        if not re.fullmatch(r"[0-9a-f]{8}(?:-[0-9a-f]{4}){3}-[0-9a-f]{12}", boot):
            raise ValueError("invalid-boot-identity")
        status = self.root_file(f"/proc/{pid}/status", 16384).decode()
        uid_match = re.search(r"^Uid:\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s*$", status, re.MULTILINE)
        if not uid_match or len(set(uid_match.groups())) != 1 or int(uid_match[1]) < 10000:
            raise ValueError("unexpected-process-uid")
        # exe is an executable, so deliberately request exactly the first 20 bytes.
        header = self.root_command(f"head -c 20 /proc/{pid}/exe", limit=20)
        return {"pid": pid, "startTicks": ticks, "bootId": boot,
                "uid": int(uid_match[1]), "abi": elf_abi(header)}

    def elapsed_ms(self):
        raw = self.root_file("/proc/uptime").decode().split()[0]
        if not re.fullmatch(r"[0-9]+\.[0-9]+", raw):
            raise ValueError("invalid-device-uptime")
        return int(Decimal(raw) * 1000)

    def installed_module_hash(self, user):
        raw = self.run("shell", "pm", "path", "--user", str(user), MODULE, limit=8192).decode().splitlines()
        paths = [line.removeprefix("package:") for line in raw if line.startswith("package:")]
        if len(paths) != 1 or not paths[0].startswith("/data/app/") or not paths[0].endswith("/base.apk"):
            raise ValueError("installed-module-unavailable-or-split")
        value = self.root_command("sha256sum " + shlex.quote(paths[0]), limit=8192).decode().split()[0]
        if not re.fullmatch(r"[0-9a-f]{64}", value):
            raise ValueError("invalid-installed-module-hash")
        return value


def collect(adb, expected, max_age_ms):
    result = {"schema": 1, "collectedAtMillis": int(time.time() * 1000), "status": "incomplete",
              "expectedBuild": expected, "issues": [], "deviceVerified": False,
              "boardReadEnabled": False, "actionsEnabled": False}
    pid = None
    try:
        pid = adb.main_pid()
        before = adb.process(pid)
        result["process"] = before
        user = before["uid"] // 100000
        installed_hash = adb.installed_module_hash(user)
        result["installedModuleApkSha256"] = installed_hash
        if installed_hash != expected["apkSha256"]:
            result["issues"].append("installed-module-apk-mismatch")
        # Android multi-user directory comes from this process's UID, never a user-0 constant.
        remote = f"/data/user/{user}/{PACKAGE}/files/padcheaper/probe-{pid}.json"
        report = read_json(adb.root_file(remote, MAX_REPORT))
        result["report"] = report
        after_pid = adb.main_pid()
        after = adb.process(after_pid)
        if before != after:
            raise ValueError("process-changed-during-collection")
        if adb.installed_module_hash(user) != installed_hash:
            raise ValueError("module-changed-during-collection")
        result["issues"].extend(assess_report(report, expected, after, adb.elapsed_ms(), max_age_ms))
        result["status"] = "needs-attention" if result["issues"] else "entry-evidence-collected"
    except (ValueError, OSError, subprocess.SubprocessError, KeyError, IndexError, UnicodeError) as error:
        result["issues"].append(str(error))
    if pid is not None:
        try:
            result["entryLog"] = adb.run("logcat", "-d", "-t", "100", "--pid", str(pid),
                                         "PADCheaper:I", "*:S").decode("utf-8", errors="replace")
        except (ValueError, OSError, subprocess.SubprocessError) as error:
            result["entryLogError"] = str(error)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--adb", default="adb", help="path to adb executable")
    parser.add_argument("--serial", required=True)
    parser.add_argument("--apk", required=True, type=Path, help="exact local module APK being verified")
    parser.add_argument("--out", required=True, type=Path, help="new JSON output file; never overwritten")
    parser.add_argument("--max-age-seconds", type=int, default=300)
    args = parser.parse_args()
    if not re.fullmatch(r"[A-Za-z0-9_.:-]+", args.serial) or not 1 <= args.max_age_seconds <= 3600:
        parser.error("invalid serial or maximum report age")
    if args.out.exists():
        parser.error("output already exists; choose a new file")
    try:
        expected = apk_identity(args.apk)
    except (OSError, ValueError, KeyError, zipfile.BadZipFile) as error:
        parser.error(str(error))
    result = collect(Adb(args.adb, args.serial), expected, args.max_age_seconds * 1000)
    result["serial"] = args.serial
    args.out.parent.mkdir(parents=True, exist_ok=True)
    with args.out.open("x", encoding="utf-8") as output:
        json.dump(result, output, ensure_ascii=False, indent=2)
        output.write("\n")
    print(result["status"])
    for issue in result["issues"]:
        print("- " + issue)
    return 0 if result["status"] == "entry-evidence-collected" else 1


if __name__ == "__main__":
    raise SystemExit(main())

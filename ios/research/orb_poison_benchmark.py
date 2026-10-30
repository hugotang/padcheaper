"""Interleave strict/heart-four-enabled policy on exact sanitized device boards.

Use an already-built orb_poison_probe during an authorized build/test batch.
This measures search and independently validates routes; it does not measure
the team's real recovery, poison damage, iPhone timing or native acceptance.
"""
import argparse
import hashlib
import json
from pathlib import Path
import platform
import statistics
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def source_hash(path):
    return hashlib.sha256(path.read_bytes().replace(b"\r\n", b"\n")).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--probe", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--repeats", type=int, default=3)
    args = parser.parse_args()
    if args.output.exists() or not args.probe.is_file() or not 1 <= args.repeats <= 10:
        parser.error("require an existing probe and new output; repeats must be 1..10")
    fixture_path = ROOT / "ios/research/orb-poison-20261030.json"
    cases = json.loads(fixture_path.read_text(encoding="utf-8-sig"))["cases"]
    rows = []
    executable = str(args.probe.resolve())
    for repeat in range(args.repeats):
        for index, case in enumerate(cases, 1):
            board = case["board"]
            assert case["goals"] == [{"colors": 8, "count": 1, "kind": "t", "length": 0},
                                      {"colors": 32, "count": 1, "kind": "four", "length": 0}]
            thorns, search = case["thorns"], case["search"]
            assert thorns["known"] or not any(board["thornPercent"])
            order = (False, True) if (repeat + index) % 2 else (True, False)
            for allow in order:
                fields = [index, board["width"], board["height"], board["spinnerMask"], board["spinnerColors"],
                          int(thorns["known"]), thorns["hp"], thorns["maximumHP"], thorns["pendingDamage"],
                          int(search["thornComboPriority"]), int(allow), search["stepLimit"], search["budgetMS"],
                          board["tapeMask"], board["cloudMask"], board["fixedStart"], board["unmatchableColors"],
                          board["minErase"], board["minMatch"]]
                for key in ("types", "flags", "enhancementBits", "blindTurns", "thornPercent"):
                    assert len(board[key]) == board["width"] * board["height"]
                    fields.extend(board[key])
                process = subprocess.run([executable], input=" ".join(map(str, fields)) + "\n", cwd=ROOT,
                                         text=True, capture_output=True, timeout=15)
                if process.returncode:
                    raise RuntimeError(f"{case['id']}/{allow}: {process.returncode}\n{process.stdout}\n{process.stderr}")
                row = json.loads(process.stdout)
                assert row["valid"] and row["allowPoisonWithHeartFour"] == allow, row
                rows.append({"case": case["id"], "repeat": repeat,
                             "policy": "heart-four" if allow else "strict", **row})
            print(f"Compared {case['id']} repeat {repeat + 1}/{args.repeats}", flush=True)
    summary = {}
    for case in cases:
        for policy in ("strict", "heart-four"):
            subset = [row for row in rows if row["case"] == case["id"] and row["policy"] == policy]
            summary[f"{case['id']}/{policy}"] = {
                "runs": len(subset), "solved": sum(row["status"] == "solved" for row in subset),
                "oracle_valid": sum(row["valid"] for row in subset), "statuses": [row["status"] for row in subset],
                "combos": [row["combos"] for row in subset], "steps": [row["steps"] for row in subset],
                "poison_erased": [row["poisonErased"] for row in subset],
                "contacts": [row["thornContacts"] for row in subset], "damage": [row["thornDamage"] for row in subset],
                "median_ms": round(statistics.median(row["wall_ms"] for row in subset), 3)}
    report = {"schema": 1, "date": "2026-10-30", "platform": platform.platform(), "machine": platform.machine(),
              "source_sha256_lf": {name: source_hash(ROOT / name) for name in
                                   ("ios/src/PHOrbSolver.cpp", "ios/src/PHOrbSolver.h", "ios/src/PHOrbModel.h",
                                    "ios/research/orb_poison_probe.cpp", "ios/research/orb_poison_benchmark.py")},
              "fixture_sha256": hashlib.sha256(fixture_path.read_bytes()).hexdigest(),
              "probe_sha256": hashlib.sha256(args.probe.read_bytes()).hexdigest(),
              "scope": "Same solver and recorded boards; only allowPoisonWithHeartFour differs. All shared phases and original thorn HP checked. Desktop search timing only; native execution and poison healing not measured.",
              "device_verified": False, "summary": summary, "runs": rows}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8", newline="\n")
    for key, result in summary.items():
        print(key, "solved", result["solved"], "/", result["runs"], "combo", result["combos"],
              "poison", result["poison_erased"], "median_ms", result["median_ms"])


if __name__ == "__main__":
    main()

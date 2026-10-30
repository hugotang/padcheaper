"""Interleave already-built old/new probes using exact stopped-board HP.

The earlier execution histories do not supply their turn's HP; they are kept
as device observations and are never assigned the latest stopped-board HP.
Only run during an explicitly authorized build/test batch.
"""
import argparse
import hashlib
import json
from pathlib import Path
import platform
import statistics
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def normalized_hash(path):
    return hashlib.sha256(path.read_bytes().replace(b"\r\n", b"\n")).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", type=Path, required=True)
    parser.add_argument("--candidate", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--baseline-commit", default="4807a75c00edc7c7e4c843e1c3ee5be12f1797f3")
    parser.add_argument("--repeats", type=int, default=3)
    args = parser.parse_args()
    if args.output.exists() or not 1 <= args.repeats <= 10:
        parser.error("preserve earlier reports; repeats must be 1..10")
    fixture_path = ROOT / "ios/research/orb-shape-thorn-20261030.json"
    fixtures = json.loads(fixture_path.read_text(encoding="utf-8-sig"))
    inputs = []
    for turn, stopped in enumerate(fixtures["stopped"], 1):
        board = stopped["trace"]["board"]
        assert not board["tapeMask"] and not board["fixedStart"] and not board["unmatchableColors"]
        assert board["minErase"] in (0, 3) and not board["minMatch"] and all(c < 6 for c in board["types"])
        inputs.append({"id": stopped["id"], "turn": turn, "width": board["width"], "height": board["height"],
                       "spinnerMask": board["spinnerMask"], "spinnerColors": board["spinnerColors"],
                       "hpKnown": stopped["hpKnown"], "hp": stopped["hp"], "maximumHP": stopped["maximumHP"],
                       "pendingDamage": stopped["pendingDamage"], "priority": stopped["trace"]["thornComboPriority"],
                       "types": board["types"], "thornPercent": board["thornPercent"]})
    binaries = {version: executable.resolve() for version, executable in
                (("baseline", args.baseline), ("candidate", args.candidate))}
    if not all(path.is_file() for path in binaries.values()):
        parser.error("missing already-built probe")
    rows = []
    for repeat in range(args.repeats):
        for index, case in enumerate(inputs):
            fields = [case[k] for k in ("turn", "width", "height", "spinnerMask", "spinnerColors", "hpKnown",
                                       "hp", "maximumHP", "pendingDamage", "priority")]
            fields = [int(value) for value in fields] + case["types"] + case["thornPercent"]
            order = ("baseline", "candidate") if (repeat + index) % 2 == 0 else ("candidate", "baseline")
            for version in order:
                process = subprocess.run([str(binaries[version])], input=" ".join(map(str, fields)) + "\n",
                                         cwd=ROOT, text=True, capture_output=True, timeout=15)
                if process.returncode:
                    raise RuntimeError(f"{version}/{case['id']}: {process.returncode}\n{process.stdout}\n{process.stderr}")
                row = json.loads(process.stdout)
                assert row["valid"], row
                rows.append({"version": version, "case": case["id"], "repeat": repeat, **row})
            print(f"Compared {case['id']} repeat {repeat + 1}/{args.repeats}", flush=True)
    summary = {}
    for version in binaries:
        for case in inputs:
            subset = [row for row in rows if row["version"] == version and row["case"] == case["id"]]
            summary[f"{version}/{case['id']}"] = {
                "runs": len(subset), "solved": sum(row["status"] == "solved" for row in subset),
                "oracle_valid": sum(row["valid"] for row in subset), "statuses": [row["status"] for row in subset],
                "median_ms": round(statistics.median(row["wall_ms"] for row in subset), 3),
                "steps": [row["steps"] for row in subset], "combos": [row["combos"] for row in subset],
                "contacts": [row["thornContacts"] for row in subset], "damage": [row["thornDamage"] for row in subset]}
    old_source = subprocess.check_output(["git", "show", args.baseline_commit + ":ios/src/PHOrbSolver.cpp"], cwd=ROOT)
    report = {"schema": 1, "date": "2026-10-30", "platform": platform.platform(), "machine": platform.machine(),
              "baseline_commit": args.baseline_commit,
              "baseline_source_sha256": hashlib.sha256(old_source.replace(b"\r\n", b"\n")).hexdigest(),
              "candidate_source_sha256": normalized_hash(ROOT / "ios/src/PHOrbSolver.cpp"),
              "probe_sha256": normalized_hash(Path(__file__).with_name("orb_recorded_shape_probe.cpp")),
              "oracle_source_sha256": normalized_hash(Path(__file__).with_name("orb_shape_probe.cpp")),
              "fixture_sha256": normalized_hash(fixture_path),
              "binaries": {version: {"name": path.name, "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}
                           for version, path in binaries.items()},
              "scope": "Exact 0.5.9 stopped boards with each log's current/max/pending HP; GCC/G++ -O2, thorough "
                       "48 steps/512 beam/800 ms; independent all-phase runs/groups/gravity and moving-thorn oracle. "
                       "Earlier execution HP is unknown. Desktop timings do not establish iPhone performance.",
              "summary": summary, "inputs": inputs, "measurements": rows}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("x", encoding="utf-8") as output:
        metadata = {k: v for k, v in report.items() if k not in ("inputs", "measurements")}
        output.write(json.dumps(metadata, ensure_ascii=False, indent=2)[:-2] + ",\n")
        for key in ("inputs", "measurements"):
            output.write(f'  "{key}": [\n')
            output.write(",\n".join("    " + json.dumps(row, ensure_ascii=False, separators=(",", ":")) for row in report[key]))
            output.write("\n  ]" + (",\n" if key == "inputs" else "\n}\n"))
    print(json.dumps(summary, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()

"""Compare already-built orb_shape_probe executables on identical basic boards.

python ios/research/orb_shape_benchmark.py --baseline OLD --candidate NEW --output REPORT
Does not build, read the game, send input or modify delivered binaries.
Run only as part of an explicitly authorized build/test batch.
"""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import statistics
import subprocess


def cases(fixture):
    yield {"name": "IMG_0407", "width": fixture["width"], "height": fixture["height"],
           "cells": "".join(str(cell) for row in fixture["rows"] for cell in row), "step_cap": None}
    # A representative short native-time cap, not the missing screenshot timer.
    yield {"name": "IMG_0407_cap24", "width": fixture["width"], "height": fixture["height"],
           "cells": "".join(str(cell) for row in fixture["rows"] for cell in row), "step_cap": 24}
    for width, height in ((6, 5), (7, 6), (5, 4)):
        size = width * height
        for hearts in (4, 8, 12):
            for seed in (11, 29, 83, 137):
                state = seed
                lights = min(11, size - hearts - 3)
                cells = [5] * hearts + [3] * lights
                background = (0, 1, 2, 4)
                cells += [background[i % 4] for i in range(size - len(cells))]
                # Explicit Fisher-Yates PRNG, reproducible across Python releases.
                for i in range(size - 1, 0, -1):
                    state = (1664525 * state + 1013904223) & 0xFFFFFFFF
                    j = state % (i + 1)
                    cells[i], cells[j] = cells[j], cells[i]
                yield {"name": f"{width}x{height}_heart{hearts}_seed{seed}", "width": width,
                       "height": height, "cells": "".join(map(str, cells)), "step_cap": None}


def summarize(rows):
    result = {}
    for version in ("baseline", "candidate"):
        for mode in ("match", "four"):
            for quality in ("fast", "balanced", "thorough"):
                subset = [row for row in rows if row["version"] == version and row["mode"] == mode
                          and row["quality"] == quality and not row["case"].startswith("IMG_0407")]
                elapsed = sorted(row["wall_ms"] for row in subset)
                if not elapsed:
                    continue
                result[f"{version}/{mode}/{quality}"] = {
                    "cases": len(subset), "solved": sum(row["goals_met"] for row in subset),
                    "oracle_valid": sum(row["valid"] for row in subset),
                    "median_ms": round(statistics.median(elapsed), 3),
                    "p95_ms": elapsed[math.ceil(len(elapsed) * 0.95) - 1], "max_ms": max(elapsed),
                    "deadlines": sum(row["deadline"] for row in subset),
                }
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", type=Path, required=True)
    parser.add_argument("--candidate", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    fixture = json.loads((root / "ios/research/orb-shape-0407.json").read_text(encoding="utf-8-sig"))
    dataset = list(cases(fixture))
    binaries = {name: path.resolve() for name, path in (("baseline", args.baseline), ("candidate", args.candidate))}
    for path in binaries.values():
        if not path.is_file():
            parser.error(f"missing probe executable: {path}")
    if args.output.exists():
        parser.error("output already exists; preserve earlier measurements")
    rows = []
    for index, case in enumerate(dataset):
        for mode in ("match", "four"):
            for quality in ("fast", "balanced", "thorough"):
                # Interleave old/new so long batches do not bias one version.
                order = ("baseline", "candidate") if index % 2 == 0 else ("candidate", "baseline")
                for version in order:
                    command = [str(binaries[version]), str(case["width"]), str(case["height"]),
                               case["cells"], mode, quality]
                    if case["step_cap"]:
                        command.append(str(case["step_cap"]))
                    process = subprocess.run(command, cwd=root, env=os.environ.copy(),
                                             capture_output=True, text=True, timeout=15)
                    if process.returncode:
                        raise RuntimeError(f"{version}/{case['name']}/{mode}/{quality}: "
                                           f"{process.returncode}\n{process.stdout}\n{process.stderr}")
                    row = json.loads(process.stdout)
                    if not row["valid"]:
                        raise RuntimeError(f"independent oracle rejected {command}")
                    if version == "candidate" and row["route"] and row["goal_hits"][1] != row["oracle"]["heart"]:
                        raise RuntimeError("candidate undercounted an actual heart goal")
                    row.update(version=version, case=case["name"])
                    rows.append(row)
        print(f"Compared {index + 1}/{len(dataset)}: {case['name']}", flush=True)
    report = {
        "schema": 1, "platform": platform.platform(), "machine": platform.machine(),
        "scope": "Ordinary screenshot transcription and synthetic basic boards; no native timer/effects or device performance claim.",
        "baseline_commit": "2c3362d7a928d64e83837dc0f92d9505d0da8a38",
        "candidate_source_sha256": hashlib.sha256((root / "ios/src/PHOrbSolver.cpp").read_bytes()).hexdigest(),
        "probe_source_sha256": hashlib.sha256((root / "ios/research/orb_shape_probe.cpp").read_bytes()).hexdigest(),
        "binaries": {name: {"name": path.name, "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}
                     for name, path in binaries.items()},
        "cases": dataset, "summary": summarize(rows), "measurements": rows,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("x", encoding="utf-8") as output:
        # Keep each reproducible input and measurement on one line so a
        # retained benchmark does not obscure source changes in code review.
        metadata = {key: value for key, value in report.items() if key not in ("cases", "measurements")}
        output.write(json.dumps(metadata, ensure_ascii=False, indent=2)[:-2] + ",\n")
        for key in ("cases", "measurements"):
            output.write(f'  "{key}": [\n')
            output.write(",\n".join("    " + json.dumps(row, ensure_ascii=False, separators=(",", ":")) for row in report[key]))
            output.write("\n  ]" + (",\n" if key == "cases" else "\n}\n"))
    print(json.dumps({"summary": report["summary"], "screenshot": [row for row in rows
                      if row["case"].startswith("IMG_0407")]}, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()

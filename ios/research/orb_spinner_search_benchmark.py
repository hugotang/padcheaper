"""Compare already-built recorded-shape probes on sanitized 0.5.13 boards.

Run only during an explicitly authorized build/test batch. Build the same
orb_recorded_shape_probe.cpp with each revision's solver/model/header; this
script does not compile or generate anything. Both probes use thorough's
original 800 ms / 48-step limits and an independent all-phase oracle.
"""
import argparse
import hashlib
import json
from pathlib import Path
import platform
import statistics
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", type=Path, required=True)
    parser.add_argument("--candidate", type=Path, required=True)
    parser.add_argument("--baseline-commit", required=True)
    parser.add_argument("--candidate-commit", required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--repeats", type=int, default=5)
    args = parser.parse_args()
    if args.output.exists() or not 1 <= args.repeats <= 10:
        parser.error("preserve earlier reports; repeats must be 1..10")
    for path in (args.baseline, args.candidate):
        if not path.is_file():
            parser.error(f"missing already-built probe: {path}")
    fixture = ROOT / "ios/research/orb-spinner-search-20261031.json"
    cases = json.loads(fixture.read_text(encoding="utf-8-sig"))["cases"]
    lines = []
    for turn, case in enumerate(cases, 1):
        board = case["board"]
        assert case["goals"] == [{"colors": 8, "count": 1, "kind": "t", "length": 0},
                                 {"colors": 32, "count": 1, "kind": "four", "length": 0}]
        assert not board["tapeMask"] and not board["cloudMask"] and not board["fixedStart"] and not board["minMatch"]
        assert board["minErase"] == 3 and not board["unmatchableColors"]
        assert all(flag in (0, 0x1000) for flag in board["flags"]) and not any(board["thornPercent"])
        assert board["spinnerColors"] == 63 and all(0 <= c < 6 for c in board["types"])
        assert len(board["types"]) == len(board["thornPercent"]) == len(board["flags"]) == len(board["blindTurns"]) == board["width"] * board["height"]
        assert case["reportedSearch"]["budgetMS"] == 800 and case["reportedSearch"]["stepLimit"] == 48
        # No recorded HP is required: these comparison boards contain no thorns.
        search = case["reportedSearch"]
        header = [turn, board["width"], board["height"], board["spinnerMask"], 63, 0, 0, 0, 0,
                  int(search.get("thornComboPriority", False)), int(search.get("allowPoisonWithHeartFour", False)),
                  int(search.get("diagonal", False))]
        lines.append(" ".join(map(str, header + board["types"] + board["thornPercent"] + board["flags"] + board["blindTurns"])))
    payload = "\n".join(lines) + "\n"
    report = {
        "schema": 1, "fixture": fixture.name, "fixture_sha256": hashlib.sha256(fixture.read_bytes()).hexdigest(),
        "limits": {"milliseconds": 800, "steps": 48, "beam": 512},
        "platform": platform.platform(), "repeats": args.repeats,
        "method": "Alternate baseline/candidate process order; compare each fixed board and independent all-phase validation.",
        "commits_supplied_by_operator": {"baseline": args.baseline_commit, "candidate": args.candidate_commit},
        "binary_sha256": {label: hashlib.sha256(path.read_bytes()).hexdigest()
                          for label, path in (("baseline", args.baseline), ("candidate", args.candidate))},
        "input_sha256": hashlib.sha256(payload.encode()).hexdigest(), "runs": [], "summary": [],
        "device_verified": False,
    }
    for repeat in range(args.repeats):
        order = [("baseline", args.baseline), ("candidate", args.candidate)]
        if repeat % 2:
            order.reverse()
        for label, executable in order:
            result = subprocess.run([str(executable.resolve()), "--policy-input"], input=payload, text=True,
                                    encoding="utf-8", capture_output=True, timeout=30, check=True)
            rows = [json.loads(line) for line in result.stdout.splitlines() if line.strip()]
            assert len(rows) == len(cases)
            for turn, (case, row) in enumerate(zip(cases, rows), 1):
                assert row["turn"] == turn and row["valid"], (label, case["id"], row)
                assert row["steps"] <= 48 and row["thornContacts"] == row["thornDamage"] == 0
                if label == "candidate" and row["path"]:
                    assert row["evaluationCalls"] is not None and row["evaluationCacheHits"] is not None
                    assert row["evaluationCalls"] + row["evaluationCacheHits"] == row["evaluated"]
                report["runs"].append({"revision": label, "repeat": repeat + 1, "case": case["id"], **row})
    for case in cases:
        for label in ("baseline", "candidate"):
            rows = [row for row in report["runs"] if row["case"] == case["id"] and row["revision"] == label]
            summary = {"case": case["id"], "revision": label,
                       "solved": sum(row["goals_met"] and row["hazards_safe"] for row in rows), "runs": len(rows)}
            for metric in ("wall_ms", "elapsedMS", "evaluated", "steps", "combos", "evaluationCalls", "evaluationCacheHits"):
                values = [row[metric] for row in rows if row.get(metric) is not None]
                summary["median_" + metric] = statistics.median(values) if values else None
            report["summary"].append(summary)
    with args.output.open("x", encoding="utf-8") as output:
        json.dump(report, output, ensure_ascii=False, indent=2)
        output.write("\n")
    print(json.dumps(report["summary"], ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()

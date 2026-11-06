"""Run an already-built local shape probe against recorded synthetic witnesses.

Only run when local compilation/simulation has been explicitly authorized.
Does not build, access a device, dispatch CI or modify a delivered artifact.
The witness is independently checked and is never supplied to the solver.
"""
import argparse
import hashlib
import json
from pathlib import Path
import platform
import subprocess

ROOT = Path(__file__).resolve().parents[2]
SHAPES = {
    "L": [(0, 0), (0, 1), (0, 2), (1, 2), (2, 2)],
    "Cross": [(1, 0), (0, 1), (1, 1), (2, 1), (1, 2)],
    "T": [(0, 0), (1, 0), (2, 0), (1, 1), (1, 2)],
    "Square": [(x, y) for y in range(3) for x in range(3)],
    "Four": [(x, 0) for x in range(4)],
}
KINDS = {"L": 3, "Cross": 4, "T": 5, "Square": 6, "Row": 7, "Column": 8, "Four": 9}


def normalized(points):
    x, y = min(p[0] for p in points), min(p[1] for p in points)
    return tuple(sorted((a-x, b-y) for a, b in points))


def rotations(shape):
    answer = set()
    for _ in range(4):
        answer.add(normalized(shape))
        shape = [(-y, x) for x, y in shape]
    return answer


def groups(cells, w, h):
    """Mark full horizontal/vertical runs, then connect marked same-color cells."""
    marked = set()
    for axis in (0, 1):
        lines, length = (h, w) if not axis else (w, h)
        for line in range(lines):
            def at(p):
                return line*w+p if not axis else p*w+line
            start = 0
            while start < length:
                end = start+1
                while end < length and cells[at(start)] == cells[at(end)]:
                    end += 1
                if cells[at(start)] >= 0 and end-start >= 3:
                    marked.update(at(p) for p in range(start, end))
                start = end
    answer = []
    while marked:
        first = min(marked)
        marked.remove(first)
        component, frontier = {first}, [first]
        while frontier:
            a = frontier.pop()
            for b in tuple(marked):
                if cells[a] == cells[b] and abs(a % w-b % w)+abs(a//w-b//w) == 1:
                    marked.remove(b)
                    component.add(b)
                    frontier.append(b)
        answer.append((cells[first], component))
    return answer


def shape_hit(component, shape, w, h):
    if shape == "Row":
        return any(all(y*w+x in component for x in range(w)) for y in range(h))
    if shape == "Column":
        return any(all(y*w+x in component for y in range(h)) for x in range(w))
    return normalized([(p % w, p//w) for p in component]) in rotations(SHAPES[shape])


def oracle(cells, w, h, goals):
    first = groups(cells, w, h)
    hits = [sum(c == color and shape_hit(g, shape, w, h) for c, g in first)
            for shape, color, _ in goals]
    cells = list(cells)
    combos = erased = 0
    while wave := groups(cells, w, h):
        combos += len(wave)
        for _, group in wave:
            erased += len(group)
            for p in group:
                cells[p] = -1
        for x in range(w):
            survivors = [cells[y*w+x] for y in range(h) if cells[y*w+x] >= 0]
            survivors = [-1]*(h-len(survivors))+survivors
            for y in range(h):
                cells[y*w+x] = survivors[y]
    return dict(hits=hits, combos=combos, erased=erased, firstCombos=len(first),
                goalsMet=all(v >= g[2] for v, g in zip(hits, goals)))


def replay(case, path, steps, diagonal, fixed_start):
    w, h = case["width"], case["height"]
    if not 2 <= len(path) <= steps+1 or any(type(p) is not int or not 0 <= p < w*h for p in path):
        raise ValueError(f"invalid path: {case['id']}")
    if fixed_start and path[0] != case["witness"][0]:
        raise ValueError(f"wrong fixed start: {case['id']}")
    cells = list(case["board"])
    for a, b in zip(path, path[1:]):
        dx, dy = abs(a % w-b % w), abs(a//w-b//w)
        if not (max(dx, dy) == 1 if diagonal else dx+dy == 1):
            raise ValueError(f"illegal step: {case['id']}")
        cells[a], cells[b] = cells[b], cells[a]
    return cells


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--probe", type=Path, required=True)
    parser.add_argument("--dataset", type=Path, default=Path(__file__).with_name("orb-shape-simulation-20261106.json"))
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--source-commit", required=True, help="Source revision used to build the probe; recorded, not inferred")
    parser.add_argument("--ids", help="Comma-separated case IDs, otherwise every recorded case")
    parser.add_argument("--beam", type=int, default=512)
    parser.add_argument("--budget", type=int, default=800)
    parser.add_argument("--steps", type=int, default=48)
    parser.add_argument("--recovery", type=int, choices=(0, 1), default=1)
    parser.add_argument("--backtracks", type=int, choices=(0, 1, 2), default=2)
    parser.add_argument("--diagonal", action="store_true")
    parser.add_argument("--fixed-start", action="store_true")
    args = parser.parse_args()
    if args.output.exists() or not args.probe.is_file() or not 1 <= args.beam <= 512 or not 1 <= args.budget <= 2500 or not 1 <= args.steps <= 48:
        parser.error("preserve existing output; require a built probe and valid solver limits")
    source = json.loads(args.dataset.read_text(encoding="utf-8"))
    cases = source["cases"]
    if args.ids:
        ids = set(args.ids.split(","))
        cases = [c for c in cases if c["id"] in ids]
        if {c["id"] for c in cases} != ids:
            parser.error("unknown case ID")
    if not cases or len({c["id"] for c in cases}) != len(cases):
        parser.error("empty dataset or duplicate ID")
    lines = []
    for c in cases:
        w, h = c["width"], c["height"]
        if (w, h) not in ((5, 4), (6, 5), (7, 6)) or len(c["board"]) != w*h or any(type(v) is not int or not 0 <= v < 6 for v in c["board"]):
            raise ValueError(f"invalid basic board: {c['id']}")
        witness = oracle(replay(c, c["witness"], args.steps, args.diagonal, args.fixed_start), w, h, c["goals"])
        if not witness["goalsMet"]:
            raise ValueError(f"witness does not meet all goals: {c['id']}")
        fields = [c["id"], w, h, len(c["goals"])]
        for shape, color, count in c["goals"]:
            fields.extend((KINDS[shape], 1 << color, count, 0))
        fields.extend(c["board"])
        fields.extend((c["witness"][0]+1 if args.fixed_start else 0, args.beam, args.budget, int(args.diagonal),
                       args.recovery, args.backtracks, args.steps))
        lines.append(" ".join(map(str, fields)))
    process = subprocess.run([str(args.probe.resolve())], input="\n".join(lines)+"\n", text=True,
                             capture_output=True, timeout=max(30, len(cases)*(args.budget/1000+1)), cwd=ROOT)
    if process.returncode:
        raise RuntimeError(f"probe exited {process.returncode}: {process.stderr}")
    rows = [json.loads(line) for line in process.stdout.splitlines()]
    if [r["id"] for r in rows] != [c["id"] for c in cases]:
        raise ValueError("missing, reordered or duplicate probe results")
    summary = {}
    for c, r in zip(cases, rows):
        if not r["immutable"] or r["status"] not in (0, 1, 6):
            raise ValueError(f"unexpected solver status or source mutation: {c['id']}")
        if r["path"]:
            checked = oracle(replay(c, r["path"], args.steps, args.diagonal, args.fixed_start), c["width"], c["height"], c["goals"])
            if any(checked[key] != r[key] for key in ("hits", "combos", "erased", "goalsMet")) or not r["hazardsSafe"]:
                raise ValueError(f"independent oracle mismatch: {c['id']}")
            r["oracleFirstCombos"] = checked["firstCombos"]
        elif r["status"] != 6 or r["goalsMet"]:
            raise ValueError(f"unexpected missing route: {c['id']}")
        if (r["status"] == 0) != r["goalsMet"]:
            raise ValueError(f"status/goal mismatch: {c['id']}")
        stats = summary.setdefault(c["scenario"], dict(cases=0, solved=0, deadlines=0, maxMS=0, misses=[]))
        stats["cases"] += 1
        stats["solved"] += int(r["goalsMet"])
        stats["deadlines"] += int(r["deadline"])
        stats["maxMS"] = max(stats["maxMS"], r["elapsedMS"])
        if not r["goalsMet"]:
            stats["misses"].append(r["id"])
    report = dict(datasetSHA256=digest(args.dataset), probeSHA256=digest(args.probe), sourceCommit=args.source_commit,
                  platform=platform.platform(), parameters={k: v for k, v in vars(args).items() if k not in ("probe", "dataset", "output")},
                  cases=len(cases), solved=sum(r["goalsMet"] for r in rows), independentOracle="passed", summary=summary, rows=rows)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("x", encoding="utf-8") as stream:
        json.dump(report, stream, indent=2)
        stream.write("\n")
    print(json.dumps({k: report[k] for k in ("cases", "solved", "independentOracle", "summary")}, indent=2))


if __name__ == "__main__":
    main()

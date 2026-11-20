#!/usr/bin/env python3
"""Independent first-removal oracle for saved B4/B5 acceptance JSONL paths."""
import argparse
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def verify(rows, floor):
    if floor == 4:
        cases = {}
        for name in ('coral-b4-20261119.json', 'coral-b4-20261120.json'):
            corpus = json.loads((ROOT / 'ios/research' / name).read_text(encoding='utf-8'))
            for turn in corpus['turns']:
                cases[(corpus['runtimeVersion'], turn['turn'])] = (turn['startBoard'], False)
    else:
        corpus = json.loads((ROOT / 'ios/research/coral-b5-20261120.json').read_text(encoding='utf-8'))
        cases = {(c['version'], None): (c['board'], c['diagonal']) for c in corpus['cases']}
    assert len(rows) == 24
    seen = set()
    phases = 0
    for row in rows:
        key = (row['fixture'], row['turn'] if floor == 4 else None)
        test = (key, row['budgetMS'], row['trial'])
        assert test not in seen
        seen.add(test)
        assert row['budgetMS'] in (800, 2000) and row['trial'] in (0, 1, 2)
        assert row['solved'] and row['replayed'] and row['firstFourColors'] == 63
        board, diagonal = cases[key]
        cells = board['types'].copy()
        route = row['path']
        assert 2 <= len(route) <= 49 and row['steps'] == len(route) - 1
        w, h = board['width'], board['height']
        assert (w, h) == (7, 6)
        for cell in route:
            assert 0 <= cell < w*h and not ((board.get('tapeMask', 0) | board['spinnerMask']) & (1 << cell))
        for a, b in zip(route, route[1:]):
            dx, dy = abs(a % w - b % w), abs(a // w - b // w)
            assert max(dx, dy) == 1 and (diagonal or dx + dy == 1)
            cells[a], cells[b] = cells[b], cells[a]
        values = []
        for phase in range(6):
            layout = cells.copy()
            for i in range(w*h):
                if board['spinnerMask'] & (1 << i):
                    layout[i] = phase
            marked = set()
            for i in range(w*h):
                for stride, valid in ((1, i % w <= w-3), (w, i // w <= h-3)):
                    if valid and layout[i] == layout[i+stride] == layout[i+2*stride]:
                        marked.update((i, i+stride, i+2*stride))
            fours = combos = mask = 0
            while marked:
                i = marked.pop()
                color, group, todo = layout[i], {i}, [i]
                while todo:
                    j = todo.pop()
                    for k in (j-w, j+w, j-1, j+1):
                        if k in marked and abs(k % w-j % w) + abs(k // w-j // w) == 1 and layout[k] == color:
                            marked.remove(k)
                            group.add(k)
                            todo.append(k)
                combos += 1
                if len(group) == 4 and (len({k % w for k in group}) == 1 or len({k // w for k in group}) == 1):
                    mask |= 1 << color
                    fours += 1
            assert mask == 63
            values.append((mask, fours, combos))
            phases += 1
        assert min(v[1] for v in values) == row['firstFours']
        assert min(v[2] for v in values) == row['firstCombos']
    return {'floor': floor, 'routes': len(rows), 'phases': phases, 'passed': True,
            'scope': 'legal paths and exact first-removal groups; no damage or native timing'}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--floor', type=int, choices=(4, 5), required=True)
    parser.add_argument('jsonl', type=Path)
    args = parser.parse_args()
    print(json.dumps(verify([json.loads(s) for s in args.jsonl.read_text(encoding='utf-8').splitlines()], args.floor), indent=2))

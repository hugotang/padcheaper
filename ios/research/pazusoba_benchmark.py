"""Run an isolated upstream executable; independently replay its reported route.

Usage: python ios/research/pazusoba_benchmark.py path/to/pazusoba-core.exe
This does not read a game process or provide any touch input.
"""
import json
import re
import statistics
import subprocess
import sys
import time


def replay(board, start, route):
    width = {20: 5, 30: 6, 42: 7}[len(board)]
    result = list(board)
    current = start
    assert 0 <= current < len(board), "invalid start"
    for direction in route:
        r, c = divmod(current, width)
        dr, dc = {"U": (-1, 0), "D": (1, 0), "L": (0, -1), "R": (0, 1)}[direction]
        nr, nc = r + dr, c + dc
        assert 0 <= nr < len(board) // width and 0 <= nc < width, "route leaves board"
        next_cell = nr * width + nc
        result[current], result[next_cell] = result[next_cell], result[current]
        current = next_cell
    return "".join(result)


def combos(board):
    """Three-in-a-row plus connected matched cells; no random skyfall."""
    width = {20: 5, 30: 6, 42: 7}[len(board)]
    height = len(board) // width
    cells = list(board)
    total = 0
    for _ in range(len(cells)):
        marked = set()
        for r in range(height):
            for c in range(width):
                i = r * width + c
                if cells[i] is None:
                    continue
                if c + 2 < width and cells[i] == cells[i + 1] == cells[i + 2]:
                    marked.update((i, i + 1, i + 2))
                if r + 2 < height and cells[i] == cells[i + width] == cells[i + 2 * width]:
                    marked.update((i, i + width, i + 2 * width))
        if not marked:
            return total
        remaining = set(marked)
        while remaining:
            seed = remaining.pop()
            stack = [seed]
            total += 1
            while stack:
                cur = stack.pop()
                r, c = divmod(cur, width)
                for nr, nc in ((r - 1, c), (r + 1, c), (r, c - 1), (r, c + 1)):
                    j = nr * width + nc
                    if 0 <= nr < height and 0 <= nc < width and j in remaining and cells[j] == cells[cur]:
                        remaining.remove(j)
                        stack.append(j)
        for i in marked:
            cells[i] = None
        for c in range(width):
            values = [cells[r * width + c] for r in range(height) if cells[r * width + c] is not None]
            values = [None] * (height - len(values)) + values
            for r, value in enumerate(values):
                cells[r * width + c] = value
    raise AssertionError("cascade did not terminate")


exe = sys.argv[1]
records = []
for board in ("GLHLGGLBDHDDDHGHHRDRLDDLGLDDRG", "".join(("RBGLDHR", "BDGLHGB", "DRHLGDL", "HRBDHGL", "RBDGLHR", "BDGLHRB"))):
    assert len(board) in (30, 42)
    for _ in range(5):
        start = time.perf_counter()
        proc = subprocess.run([exe, board, "3", "25", "1000"], capture_output=True, text=True, timeout=15)
        elapsed = time.perf_counter() - start
        record = {"cells": len(board), "seconds_including_process_start": round(elapsed, 4), "exit_code": proc.returncode}
        try:
            assert proc.returncode == 0, proc.stderr
            begin, route = re.search(r"Route: \|(\d+)\| - ([UDLR]*)", proc.stdout).groups()
            reported_board = re.findall(r"Board: ([A-Z]+)", proc.stdout)[-1]
            reported_combo = int(re.search(r"Combo: (\d+)/", proc.stdout)[1])
            result = replay(board, int(begin), route)
            assert result == reported_board, "reported board differs from route replay"
            actual_combo = combos(result)
            assert actual_combo == reported_combo, "reported combo differs from independent elimination"
            record.update({"route_replayed": True, "steps": len(route), "combo": actual_combo})
        except (AssertionError, AttributeError, IndexError, KeyError) as error:
            record.update({"route_replayed": False, "error": str(error), "output": proc.stdout})
        records.append(record)
summary = {"source_commit": "493dd14746eb97f275704bef73baa0f8edcf61a8", "platform": "Windows x64 desktop",
           "depth": 25, "beam": 1000, "random_skyfall": False, "runs": records,
           "all_routes_replayed": all(r["route_replayed"] for r in records),
           "median_seconds": round(statistics.median(r["seconds_including_process_start"] for r in records), 4)}
print(json.dumps(summary, ensure_ascii=False, indent=2))

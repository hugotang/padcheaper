"""Compare recorded boards with independent path, thorn and all-phase oracles."""
import argparse
import hashlib
import json
from pathlib import Path
import statistics
import subprocess

ROOT = Path(__file__).resolve().parents[2]
BASELINE = '69a7b86deadb9bf06f5e87d376a451ff8aea53dc'


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--baseline', type=Path, required=True)
    parser.add_argument('--candidate', type=Path, required=True)
    parser.add_argument('--fixtures', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    fixtures = json.loads(args.fixtures.read_text(encoding='utf-8'))
    boards = [{'turn': r['turn'], 'board': r['startBoard']} for r in fixtures['executions']]
    boards.append({'turn': 7, 'board': fixtures['stopped']['board']})
    inputs = []
    for record in boards:
        b = record['board']
        assert not b['tapeMask'] and not b['fixedStart'] and not b['unmatchableColors']
        assert b['minErase'] in (0, 3) and b['minMatch'] == 0 and all(c < 6 for c in b['types'])
        inputs.append({'turn': record['turn'], **{k: b[k] for k in
            ('width', 'height', 'types', 'spinnerMask', 'spinnerColors', 'thornPercent')}})
    measurements = []
    for version, executable in [('baseline', args.baseline), ('candidate', args.candidate)]:
        cases = []
        for record in inputs:
            modes = [(False, False), (True, False)] + ([(True, True)] if version == 'candidate' else [])
            for known, priority in modes:
                for repeat in range(3 if record['turn'] != 7 else 1):
                    cases.append((record, known, priority, repeat))
        lines = []
        for b, known, priority, _ in cases:
            fields = [b['turn'], b['width'], b['height'], b['spinnerMask'], b['spinnerColors'],
                      int(known), 459648, 459648, 0, int(priority), *b['types'], *b['thornPercent']]
            lines.append(' '.join(map(str, fields)))
        completed = subprocess.run([str(executable.resolve())], input='\n'.join(lines), text=True, capture_output=True, check=True)
        rows = [json.loads(line) for line in completed.stdout.splitlines()]
        assert len(rows) == len(cases) and all(row['valid'] for row in rows)
        for row, (_, _, _, repeat) in zip(rows, cases):
            row.update(version=version, repeat=repeat)
        measurements.extend(rows)
    old = subprocess.check_output(['git', 'show', BASELINE + ':ios/src/PHOrbSolver.cpp'], cwd=ROOT)
    normalized = lambda path: path.read_bytes().replace(b'\r\n', b'\n')
    result = {'date': '2026-10-30', 'baseline_commit': BASELINE,
        'baseline_source_sha256': hashlib.sha256(old).hexdigest(),
        'candidate_source_sha256': hashlib.sha256(normalized(ROOT / 'ios/src/PHOrbSolver.cpp')).hexdigest(),
        'probe_sha256': hashlib.sha256(normalized(Path(__file__).with_name('orb_thorn_probe.cpp'))).hexdigest(),
        'scope': 'Windows x64, GCC/G++ -O2, thorough 48 steps/512 beam/800 ms; recorded board geometry/colors/spinners/thorns only. HP=maximum=459648, pending=0 for comparison, not a claim about every recorded turn. No native token/timer or device timing.',
        'inputs': inputs, 'measurements': measurements}
    args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    for version, known, priority in [('baseline', True, False), ('candidate', True, False), ('candidate', True, True)]:
        for turn in (4, 5, 6, 7):
            rows = [r for r in measurements if (r['version'],r['hpKnown'],r['priority'],r['turn']) == (version,known,priority,turn)]
            print(json.dumps({'version': version, 'turn': turn, 'priority': priority,
                'combos': [r['combos'] for r in rows], 'steps': [r['steps'] for r in rows],
                'contacts': [r['thornContacts'] for r in rows], 'medianMS': statistics.median(r['elapsedMS'] for r in rows)}))


if __name__ == '__main__':
    main()

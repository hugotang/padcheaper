"""Analyze sanitized 0.5.11 recordings; never build, run or import current iOS sources.

Runs/marked components/gravity and shapes are implemented independently. The
configuration proof deliberately ignores routes and hearts: if it is impossible
even under that relaxation, more gesture directions cannot repair that board.
"""
import copy
import json
from collections import Counter
from hashlib import sha256
from pathlib import Path


def neighbors(cell, width, height):
    x, y = cell % width, cell // width
    return [yy * width + xx for xx, yy in [(x - 1, y), (x + 1, y), (x, y - 1), (x, y + 1)]
            if 0 <= xx < width and 0 <= yy < height]


def phase_board(board, phase):
    result = copy.deepcopy(board)
    cycle = [c for c in range(10) if board['spinnerColors'] & (1 << c)]
    for i, color in enumerate(board['types']):
        if board['spinnerMask'] & (1 << i) and not board['flags'][i] & 0x800:
            result['types'][i] = cycle[(cycle.index(color) + phase) % len(cycle)]
    return result


def variants(board):
    return board['spinnerColors'].bit_count() if any(
        board['spinnerMask'] & (1 << i) and not board['flags'][i] & 0x800
        for i in range(len(board['types']))) else 1


def replay(board, path):
    result = copy.deepcopy(board)
    w, h = board['width'], board['height']
    blocked = board['spinnerMask'] | board['tapeMask']
    if not path:
        return None, 0
    assert not blocked & (1 << path[0])
    assert not board['fixedStart'] or path[0] + 1 == board['fixedStart']
    contacts = 0
    for a, b in zip(path, path[1:]):
        assert b in neighbors(a, w, h) and not blocked & (1 << b)
        contacts += bool(result['flags'][b] & 0x80000 and result['thornPercent'][b])
        for key in ['types', 'flags', 'enhancementBits', 'thornPercent', 'blindTurns']:
            result[key][a], result[key][b] = result[key][b], result[key][a]
        result['flags'][a] &= ~4
        result['flags'][b] &= ~4
    return result, contacts


def shape(group, cells, width, height, kind):
    if kind == 'row':
        return any(set(range(row * width, (row + 1) * width)) <= group for row in range(height))
    if kind == 'four':
        return len(group) == 4 and (len({i // width for i in group}) == 1 or len({i % width for i in group}) == 1)
    if len(group) != 5:
        return False
    min_x, min_y = min(i % width for i in group), min(i // width for i in group)
    local = {(i // width - min_y) * 3 + i % width - min_x for i in group}
    if max(i % width for i in group) - min_x != 2 or max(i // width for i in group) - min_y != 2:
        return False
    patterns = {'t': [{0, 1, 2, 4, 7}, {1, 4, 6, 7, 8}, {0, 3, 4, 5, 6}, {2, 3, 4, 5, 8}],
                'cross': [{1, 3, 4, 5, 7}]}
    return local in patterns[kind] and not any(
        cells[j] == cells[next(iter(group))] and j not in group
        for i in group for j in neighbors(i, width, height))


def evaluate(board, goals):
    w, h = board['width'], board['height']
    cells = board['types'][:]
    run = board['minErase'] or 3
    hits = [0] * len(goals)
    total = {'combos': 0, 'poisonErased': 0, 'mortalPoisonErased': 0, 'unmatchedBombs': 0, 'heartFours': 0}
    first = True
    witnesses = []
    while True:
        marked = set()
        for axis in range(2):
            for line in range(w if axis else h):
                length = h if axis else w
                indices = [i * w + line if axis else line * w + i for i in range(length)]
                begin = 0
                while begin < length:
                    end = begin + 1
                    while end < length and cells[indices[begin]] == cells[indices[end]]:
                        end += 1
                    color = cells[indices[begin]]
                    if color >= 0 and not board['unmatchableColors'] & (1 << color) and end - begin >= run:
                        marked.update(indices[begin:end])
                    begin = end
        groups = []
        remaining = marked.copy()
        while remaining:
            start = remaining.pop()
            group, todo = {start}, [start]
            while todo:
                for j in neighbors(todo.pop(), w, h):
                    if j in remaining and cells[j] == cells[start]:
                        remaining.remove(j)
                        group.add(j)
                        todo.append(j)
            if len(group) >= board['minMatch']:
                groups.append(group)
        removed = set().union(*groups) if groups else set()
        if first:
            total['unmatchedBombs'] = sum(c == 9 and i not in removed for i, c in enumerate(cells))
        if not removed:
            break
        total['combos'] += len(groups)
        for group in groups:
            color = cells[next(iter(group))]
            total['poisonErased'] += len(group) if color in [7, 8] else 0
            total['mortalPoisonErased'] += len(group) if color == 8 else 0
            if first and color == 5 and shape(group, cells, w, h, 'four'):
                total['heartFours'] += 1
            for index, goal in enumerate(goals):
                if not goal['colors'] & (1 << color):
                    continue
                met = len(group) >= goal['length'] if goal['kind'] == 'match' else (
                    first and shape(group, cells, w, h, goal['kind']))
                if met:
                    hits[index] += 1
                    if first:
                        witnesses.append({'goal': index, 'color': color, 'cells': sorted(group)})
        for i in removed:
            cells[i] = -1
        for x in range(w):
            survivors = [cells[y * w + x] for y in range(h) if cells[y * w + x] >= 0]
            column = [-1] * (h - len(survivors)) + survivors
            for y, color in enumerate(column):
                cells[y * w + x] = color
        first = False
    return {**total, 'goalHits': hits, 'firstWaveWitnesses': witnesses}


def compare_observed(predicted, observed):
    return any(phase_board(predicted, p) == observed for p in range(variants(predicted)))


def analyze_record(record, board_key, evaluation_key):
    board, path = record[board_key], record.get('path', [])
    fixed = Counter(c for i, c in enumerate(board['types']) if not board['spinnerMask'] & (1 << i))
    counts = [[phase_board(board, p)['types'].count(c) for c in range(6)] for p in range(variants(board))]
    result = {'fixedInventory': [fixed[c] for c in range(6)], 'minimumInventory': [min(a[c] for a in counts) for c in range(6)],
              'steps': max(0, len(path) - 1), 'spinnerPhases': len(counts)}
    if not path:
        return result
    predicted, contacts = replay(board, path)
    assert predicted == record['predictedBoard']
    evaluated = [evaluate(phase_board(predicted, p), record['goals']) for p in range(variants(predicted))]
    expected = record[evaluation_key]
    for key in ['combos', 'heartFours']:
        assert min(v[key] for v in evaluated) == expected[key], key
    for key in ['poisonErased', 'mortalPoisonErased', 'unmatchedBombs']:
        assert max(v[key] for v in evaluated) == expected[key], key
    hits = [min(v['goalHits'][i] for v in evaluated) for i in range(len(record['goals']))]
    assert hits == expected['goalHits']
    assert all(hits[i] >= goal['count'] for i, goal in enumerate(record['goals'])) == expected['goalsMet']
    result.update({'metadataReplayMatches': True, 'thornContacts': contacts, 'minimumCombos': min(v['combos'] for v in evaluated),
                   'goalHits': hits, 'evaluationMatches': True, 'phaseResults': evaluated})
    if 'observedBoard' in record:
        assert compare_observed(predicted, record['observedBoard'])
        result['observedMetadataAndSharedPhaseMatch'] = True
        result['observedTypeDifferences'] = [i for i, c in enumerate(predicted['types']) if c != record['observedBoard']['types'][i]]
    return result


def t_configuration_proof(board):
    w, h, n = board['width'], board['height'], len(board['types'])
    spinners = board['spinnerMask']
    fixed_mask = ((1 << n) - 1) & ~spinners
    available = sum(c == 3 for i, c in enumerate(board['types']) if not spinners & (1 << i))
    templates = []
    for y in range(h - 2):
        for x in range(w - 2):
            for local in [{0,1,2,4,7},{1,4,6,7,8},{0,3,4,5,6},{2,3,4,5,8}]:
                cells = {w * (y + i // 3) + x + i % 3 for i in local}
                border = set(j for i in cells for j in neighbors(i,w,h)) - cells
                templates.append((sum(1 << i for i in cells), sum(1 << i for i in border)))
    phases = []
    for phase in range(variants(board)):
        colors = phase_board(board,phase)['types']
        spinner_light = sum(1 << i for i, c in enumerate(colors) if spinners & (1 << i) and c == 3)
        viable = []
        for cells, border in templates:
            if cells & spinners & ~spinner_light or border & spinner_light:
                continue
            required, forbidden = cells & fixed_mask, border & fixed_mask
            if required.bit_count() <= available and (fixed_mask & ~forbidden).bit_count() >= available:
                viable.append((required,forbidden))
        phases.append(viable)
    states, frontier_sizes = {(0,0)}, []
    for choices in phases:
        next_states = set()
        for req, forb in states:
            for r, f in choices:
                if req & f or r & forb:
                    continue
                merged_r, merged_f = req | r, forb | f
                if merged_r.bit_count() <= available and (fixed_mask & ~merged_f).bit_count() >= available:
                    next_states.add((merged_r,merged_f))
        states = next_states
        frontier_sizes.append(len(states))
    nodes = 0
    def recursive(depth, required, forbidden):
        nonlocal nodes
        nodes += 1
        if depth == len(phases):
            return True
        for req, forb in phases[depth]:
            r, f = req | required, forb | forbidden
            if not r & f and r.bit_count() <= available and (fixed_mask & ~f).bit_count() >= available:
                if recursive(depth + 1,r,f):
                    return True
        return False
    found = recursive(0,0,0)
    assert found == bool(states)
    return {'model': 'arbitrary fixed-cell permutations; one independently placed conservative-border T per shared phase; no route, heart or move limit',
            'availableFixedLight': available, 'templateCount':len(templates),
            'templatesAvoidingSpinners':sum(not a & spinners for a,b in templates),
            'templatesAvoidingSpinnersAndBorder':sum(not (a | b) & spinners for a,b in templates),
            'viablePerPhase': [len(p) for p in phases], 'compatibleFrontierSizes':frontier_sizes,
            'recursiveNodes':nodes, 'configurationExists':found}


def main():
    root = Path(__file__).resolve().parent
    input_text = (root / 'orb-0511-logs-20261030.json').read_text(encoding='utf-8')
    source_text = Path(__file__).read_text(encoding='utf-8')
    inputs = json.loads(input_text)
    output = {'schema':1, 'scope':'Independent analysis of recorded 0.5.11 data; no verification of the unbuilt source batch',
              'provenance': {'encoding':'UTF-8 with normalized LF newlines',
                             'input_sha256':sha256(input_text.encode('utf-8')).hexdigest(),
                             'analysis_source_sha256':sha256(source_text.encode('utf-8')).hexdigest()}, 'logs':[]}
    for entry in inputs['logs']:
        result = {'id':entry['id'], 'source_sha256':entry['source_sha256'],
                  'trace':analyze_record(entry['trace'],'board','evaluation'), 'history':[]}
        for record in entry['history']:
            result['history'].append({'generation':record['generation'],'turn':record['turn'],
                                     **analyze_record(record,'startBoard','releaseEvaluation')})
        output['logs'].append(result)
    assert inputs['logs'][0]['trace'] == inputs['logs'][1]['trace']
    assert inputs['logs'][0]['history'] == inputs['logs'][1]['history']
    output['firstTwoRetainIdenticalTraceAndHistory'] = True
    output['firstBoardTConfigurationProof'] = t_configuration_proof(inputs['logs'][0]['trace']['board'])
    (root / 'orb-0511-log-analysis-20261030.json').write_text(json.dumps(output,ensure_ascii=False,indent=2) + '\n',encoding='utf-8')
    print(json.dumps({'logs':len(output['logs']), 'metadataAndEvaluationsMatch':True,
                      'firstBoardTConfigurationProof':output['firstBoardTConfigurationProof'],
                      'latestCross':output['logs'][-1]['trace']}, ensure_ascii=False))


if __name__ == '__main__':
    main()

#!/usr/bin/env python3
"""Author Coral's GameWith backbone and AppMedia B14/B15 skill route (2026-11-19).

This only writes JSON; it does not build, run tests, or dispatch workflows.
Skill IDs are constrained native current-stage IDs, not composite root IDs.
The v90 IDs are pinned in the template skill-reference.json; no runtime aliasing.
"""
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEST = ROOT / 'templates/coral-fixed-gamewith-585607'
SOURCE = 'https://xn--0ck4aw2h.gamewith.jp/article/show/585607'


def member(body, forms, assist=None, assist_skills=()):
    return dict(body=body, assist=assist,
                forms=[dict(number=n, skills=ids) for n, ids in forms],
                assistSkills=list(assist_skills))


def skill(slot, monster, active, *, after=0, source='body', optional=False,
          frequency='once', when='always', alternatives=(), consume=False, loop=False):
    a = dict(kind='skill', slot=slot, source=source, monster=monster, skill=active,
             afterMonster=after, optional=optional, frequency=frequency, when=when)
    if alternatives:
        a['alternatives'] = list(alternatives)
    if consume:
        a['consumeAssist'] = True
    if loop:
        a['loop'] = True
    return a


def atori(retry_skip=False):
    a = skill(1, 8477, 66706, frequency='each', alternatives=(6,))
    if retry_skip:
        # Only the second duplicate may be omitted on a retry, after this
        # puzzle's first Atori has a fresh native acknowledgement.
        a['optionalAfterFirst'] = True
    return a


def solomon(*, initial=False, optional=False, when='always'):
    return skill(5, 13828, 66716 if initial else 66718, optional=optional, when=when)


def diena(*, loop=False):
    return skill(2, 13876, 66724, loop=loop)


def keisy(*, loop=False, required=False):
    a = skill(3, 13745, 66740, loop=loop)
    if required:
        a['requireReady'] = True
    return a


def riku():
    return skill(4, 13878, 66729)


def target(part=False):
    # B7 uses the observed special Clammy catalog ID; see enemy-reference.json.
    return dict(kind='target', target='part' if part else 'enemy', monster=0 if part else 210524,
                optional=False, frequency='once')


def stage(name, actions=(), policy='six_colors_fours', *, until='puzzles',
          minimum=1, maximum=None, l=False, recovery=False, ms=0, absorb=False, retry_bypass=False,
          enemies=None, clear=(), hold=(), unmatchable=False):
    board = dict(policy=policy, assistRecoveryL=recovery, maxGestureMS=ms)
    if unmatchable:
        board['unmatchableRecovery'] = True
    if l:
        board['lColors'] = 13  # Fire, wood, light: verified guide and recovery carriers.
    if isinstance(until, str):
        until = dict(kind=until)
    result = dict(id=name, actions=list(actions), board=board,
                  repeat=dict(minimumPuzzles=minimum,
                              maximumPuzzles=maximum or minimum or 1, until=until))
    if absorb:
        result['absorbStall'] = True
    if retry_bypass:
        result['retryRequiresBypass'] = True
    if enemies is not None:
        result['requireEnemyState'] = enemies
    if clear:
        result['requireEffectsClear'] = list(clear)
    if hold:
        result['holdLoops'] = list(hold)
    return result


def exit_stage(name='finish', actions=None, **kw):
    # Extra turns are floor-specific; expired bypass or new enemy effects can
    # invalidate an otherwise identical attack (notably B5 and B10).
    return stage(name, [atori()] if actions is None else actions, until='floor_change', **kw)


def build():
    floors = []

    def floor(stages=None, ordinary=None, parts=None):
        variants = [dict(kind='any', stages=stages)] if stages else [
            dict(kind='ordinary', stages=ordinary), dict(kind='parts', stages=parts)]
        floors.append(dict(number=len(floors)+1, variants=variants))

    floor([
        stage('delay-and-haste', [skill(2, 13876, 66752, source='assist', consume=True),
                                 skill(4, 13878, 66755, source='assist', consume=True)], 'one_combo'),
        stage('charge', policy='one_combo'),
        stage('transform', [skill(5, 8476, 66700, after=8477),
                            skill(6, 8476, 66700, after=8477),
                            skill(3, 13744, 66735, after=13745)], 'one_combo'),
        stage('evolve-solomon', [solomon(initial=True), atori()]),
        stage('attack-2', [atori()]), stage('attack-3', [atori()]),
        exit_stage('buff-and-finish', [atori(), diena(), atori(True), solomon(), keisy()],
                   maximum=2, retry_bypass=True),
    ])
    floor([stage('six-triples', policy='six_colors_no_fours'), exit_stage(unmatchable=True)])
    floor([stage('survive-first-hit', [riku(), atori()]),
           exit_stage('expert-multicolor', policy='six_colors', ms=4500)])
    floor([stage('clear-absorption', [solomon(), keisy(), atori()]), exit_stage()])
    floor([stage('delay-1', [atori()]), stage('delay-2', [atori()]),
           stage('buff-without-fours', [solomon(), diena(), keisy(), atori()], 'six_colors_no_fours'),
           exit_stage()])
    floor([exit_stage()])
    pair = [dict(monster=200038), dict(monster=210524)]
    survivor = [dict(monster=200038, alive=False), dict(monster=210524, alive=True, maximumPercent=50)]
    covered = ('damage_absorb', 'attribute_absorb', 'damage_void')
    # Native facts authorize one follow-up, never a cached enemy action or a
    # guessed resurrection. Recheck targeting after the first board's skills.
    floor([stage('target-clammy', [target(), solomon(), atori(), target()], maximum=2,
                 until=dict(kind='enemy_state_or_floor_change', enemies=survivor), enemies=pair, clear=covered),
           exit_stage('finish-clammy', enemies=survivor, clear=covered)])
    floor(ordinary=[exit_stage('marine-snow', [keisy(loop=True), riku(), atori()])],
          parts=[stage('target-and-recover', [target(True), atori()], l=True, recovery=True),
                 stage('part-damage', [atori()]), exit_stage('break-part', [riku(), atori()])])
    floor(ordinary=[stage('nimue', [keisy(loop=True), atori()]), exit_stage()],
          parts=[stage('target-and-recover', [target(True), atori()], l=True, recovery=True),
                 stage('part-damage-1', [atori()]), stage('part-damage-2', [atori()]), exit_stage('break-part')])
    floor([exit_stage('haste-atori', [atori(), diena(), atori()])])
    floor([stage('super-resolve', [riku(), atori()]),
           stage('cleanse', [solomon(), atori()]), exit_stage()])
    floor([stage('recover-assists', policy='l_only', l=True, recovery=True),
           stage('break-shields', [riku(), atori()]),
           stage('reach-super-resolve', [atori()], minimum=0, maximum=2,
                 until=dict(kind='enemy_hp_at_most', percent=50)),
           exit_stage('cleanse-and-finish', [solomon(), atori()], maximum=2)])
    absorb = [solomon(optional=True, when='damage_absorb'), solomon(optional=True, when='attribute_absorb')]
    for a in absorb:
        a['frequency'] = 'each'
    # Reserve Keisy for B14's 5x4 change, then restore absorption bypass only
    # when it is actually uncovered. A native skill receipt alone is not enough:
    # the effects_clear predicate must observe its resulting bypass.
    floor([stage('reserve-keisy', [atori()], 'six_colors_no_fours', minimum=0, maximum=8, hold=(3,),
                 until=dict(kind='skill_ready', slot=3, monster=13745, skill=66740)),
           stage('restore-bypass', absorb+[atori()], 'six_colors_no_fours', minimum=0, maximum=8, hold=(3,),
                 until=dict(kind='effects_clear', effects=['damage_absorb', 'attribute_absorb'])),
           exit_stage('hp-buff-and-finish', [atori(), diena(loop=True), riku(), atori()], hold=(3,), clear=covered)])
    # AppMedia's Keisy resize preserves Solomon for B15. The explicit resize
    # cannot be skipped merely because Keisy's optional loop is already active.
    floor([stage('attack', [atori()], hold=(3,)),
           stage('resize-with-keisy', [keisy(loop=True, required=True), atori()]),
           stage('reserve-solomon', [atori()], 'six_colors_no_fours', minimum=0, maximum=8,
                 until=dict(kind='skill_ready', slot=5, monster=13828, skill=66718)),
           exit_stage(maximum=20)])
    finish_riku = riku()
    finish_riku['when'] = 'attack_ready'
    floor([stage('survive-first-hit', [solomon(when='unmatchable'), riku(), atori()], clear=('unmatchable',)),
           stage('reach-half', absorb+[atori()], until=dict(kind='enemy_hp_at_most', percent=50),
                 minimum=0, maximum=24, absorb=True),
           stage('recover-super-resolve', [atori()], l=True, recovery=True),
           stage('reach-quarter', absorb+[atori()], until=dict(kind='enemy_hp_at_most', percent=25),
                 minimum=0, maximum=24, absorb=True),
           exit_stage('finish-with-riku', absorb+[finish_riku, atori()], maximum=2, absorb=True)])
    members = [member(13828, [(13828, [66716, 66718])]),
               member(13876, [(13876, [66724])], 13619, [66752]),
               member(13744, [(13744, [66735]), (13745, [66740])]),
               member(13878, [(13878, [66729])], 13879, [66755]),
               member(8476, [(8476, [66700]), (8477, [66706])]),
               member(8476, [(8476, [66700]), (8477, [66706])])]
    return dict(format='padcheaper-fixed-plan', schema=1, target='jp.gungho.pad-ios-23.6.1-107553',
                profile=dict(id='coral-fixed-585607-v90-r5', name='珊瑚挑戰・固定 Atori・v90 B2 不可消回復',
                             team=[m['body'] for m in members], dungeon=7801, floor=1, source=SOURCE,
                             settings=dict(quality='thorough', mode='combo', goals=[], rules=[], assistRecoveryL=False),
                             fixedPlan=dict(schema=1, members=members, leaderSwap=5, floors=floors)))


if __name__ == '__main__':
    DEST.mkdir(parents=True, exist_ok=True)
    (DEST / 'fixed-plan.json').write_text(json.dumps(build(), ensure_ascii=False, indent=2)+'\n', encoding='utf-8', newline='\n')

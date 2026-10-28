#!/usr/bin/env python3
"""Generate the reviewed 23.6.0 orb/input layout, with independent code guards."""
import hashlib
import json
from pathlib import Path
import re


def generate(manifest):
    if manifest['schema'] != 1 or manifest['target_id'] != 'jp.gungho.pad-ios-23.6.0-106878':
        raise ValueError('Unsupported orb target')
    if manifest['scope'] != {'multiplayer': False, 'dimensions': [[6, 5], [7, 6], [5, 4]], 'min_erase': 3,
                             'continuous': True, 'input': 'scoped-private-manager',
                             'goal_modes': ['combo', 'manual', 'leaders'], 'max_goals': 6,
                             'shape_wave': 'first-removal-only', 'leader_planning': 'minimum-board-conditions-only',
                             'full_leader_activation': False, 'skill_rules': 'ordered-conjunctive-single-player',
                             'max_skill_rules': 12, 'skill_entry': 'native-confirmed-current-skill',
                             'enemy_conditions': ['damage_absorb', 'attribute_absorb', 'damage_void', 'combo_absorb'],
                             'skill_groups': 'round-robin-one-per-orb-turn', 'once_rules': 'current-battle-memory',
                             'reserved_skills': True, 'strategy_profiles': 64, 'profile_selection': 'explicit-start-only',
                             'locked_orbs': True, 'timed_spinners': False,
                             'obstacle_conditions': ['unmatchable', 'special_orbs', 'orb_effect', 'board_effect', 'spinner', 'locked', 'weakened'],
                             'survival_conditions': ['hp_at_most', 'awoken_bind', 'member_bind', 'skill_seal'],
                             'required_skills': 'confirmed-rule-or-group-before-route',
                             'hp_read': 'read-only-effective-cap', 'decision_reasons': True}:
        raise ValueError('Unreviewed orb scope')
    required = {'PHOrbInitRVA', 'PHOrbGeometryRVA', 'PHOrbNearestRVA', 'PHOrbPlayerInputRVA',
                'PHOrbSwapRVA', 'PHOrbInputUpdateRVA', 'PHOrbInputGetRVA', 'PHOrbInputTransformRVA',
                'PHOrbInputHistoryRVA', 'PHOrbFlagsRVA', 'PHOrbCanUseSkillRVA', 'PHOrbStartSkillRVA',
                'PHOrbSkillUpdateRVA', 'PHOrbBattleUpdateRVA', 'PHOrbEnemyDamageRVA', 'PHOrbMemberTapRVA',
                'PHOrbSkillEffectsRVA', 'PHOrbSpinnerSetRVA', 'PHOrbLockedRVA', 'PHOrbChangeRVA', 'PHOrbMatchRVA',
                'PHOrbMaximumHPRVA', 'PHOrbDungeonHPRVA', 'PHOrbDungeonOptionRVA', 'PHOrbDungeonOptionParseRVA',
                'PHOrbAwakeningCheckRVA'}
    if {g['name'] for g in manifest['guards']} != required or len(manifest['guards']) != len(required):
        raise ValueError('Incomplete orb guards')
    lines = ['// Generated from ios/targets/orbs-23.6.0.json. Do not edit.', '#pragma once',
             '#include <stdint.h>', '#include <stddef.h>']
    for name, value in manifest['fields'].items():
        number = int(value, 0)
        if not re.fullmatch(r'PHOrb[A-Za-z0-9]+', name) or not 0 < number < 0x3000000:
            raise ValueError('Invalid orb field')
        lines.append(f'static const uintptr_t {name} = 0x{number:x}u;')
    for guard in manifest['guards']:
        raw, rva = bytes.fromhex(guard['bytes']), int(guard['rva'], 0)
        if len(raw) != 64 or guard['length'] != 64 or not 0x4000 < rva < 0x1110000 or \
                int(guard['va'], 0) != rva + 0x100000000 or hashlib.sha256(raw).hexdigest() != guard['sha256']:
            raise ValueError('Invalid orb fingerprint')
        lines.append(f"static const uintptr_t {guard['name']} = 0x{rva:x}u;")
    lines += ['typedef struct { uintptr_t rva; uint8_t bytes[64]; } PHOrbTextGuard;',
              'static const PHOrbTextGuard PHOrbTextGuards[] = {']
    for guard in manifest['guards']:
        lines.append('    {0x%xu, {%s}},' % (int(guard['rva'], 0), ', '.join(f'0x{x:02x}' for x in bytes.fromhex(guard['bytes']))))
    lines += ['};', 'static const size_t PHOrbTextGuardCount = sizeof(PHOrbTextGuards) / sizeof(PHOrbTextGuards[0]);']
    return '\n'.join(lines) + '\n'


if __name__ == '__main__':
    manifest = json.loads(Path('ios/targets/orbs-23.6.0.json').read_text(encoding='utf-8'))
    output = Path('ios/generated/PHOrbGenerated.h')
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(generate(manifest), encoding='utf-8', newline='\n')
    print(f'Generated {output}')

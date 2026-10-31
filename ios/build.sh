#!/usr/bin/env bash
set -euo pipefail

repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_dir"
build_dir="${BUILD_DIR:-$repo_dir/build/ios}"
mkdir -p "$build_dir" ios/generated
python3 ios/tools/generate_target.py
python3 ios/tools/generate_battle.py
python3 ios/tools/generate_orbs.py
python3 ios/tools/generate_editor.py
python3 ios/tools/generate_leaders.py

sdk_path="$(xcrun --sdk iphoneos --show-sdk-path)"
clang_path="$(xcrun --sdk iphoneos --find clang)"
min_version="${IPHONEOS_DEPLOYMENT_TARGET:-15.0}"

for native in PHBattleNative PHTimer PHMusic PHOrbNative PHOrbSession; do
"$clang_path" -arch arm64 -isysroot "$sdk_path" \
    "-miphoneos-version-min=$min_version" -std=c++17 -fobjc-arc -fblocks -fexceptions \
    -O2 -Wall -Wextra -Werror -Wno-unused-parameter -Wno-unused-const-variable \
    -Iios/src -Iios/generated -c "ios/src/$native.mm" -o "$build_dir/$native.o"
done

"$clang_path" -arch arm64 -isysroot "$sdk_path" "-miphoneos-version-min=$min_version" \
    -std=c++17 -O2 -Wall -Wextra -Werror -Iios/src -c ios/src/PHOrbSolver.cpp -o "$build_dir/PHOrbSolver.o"

"$clang_path" -arch arm64 -isysroot "$sdk_path" \
    "-miphoneos-version-min=$min_version" -std=gnu11 -fobjc-arc -fblocks \
    -O2 -Wall -Wextra -Werror -Wno-unused-parameter -Wno-unused-const-variable \
    -dynamiclib -install_name @rpath/PADCheaper.dylib \
    -Iios/src -Iios/generated \
    ios/src/PHCodec.m ios/src/PHSaveState.m ios/src/PHDeckPersistence.m ios/src/PHUnlockRestoration.m ios/src/PHMemory.m ios/src/PHIdentity.m ios/src/PHConfiguration.m \
    ios/src/PHTarget.m ios/src/PHOverlay.m ios/src/PHRuntime.m ios/src/PHFormationController.m ios/src/PHFormationModel.m \
    ios/src/PHInputModel.c ios/src/PHBattleModel.c ios/src/PHBattlePreset.c ios/src/PHBattleSession.m ios/src/PHBadgeModel.c ios/src/PHBadges.m \
    ios/src/PHTimerModel.c ios/src/PHOrbModel.c ios/src/PHOrbController.m ios/src/PHOrbPlanning.m ios/src/PHOrbRules.m ios/src/PHOrbRuleEditor.m ios/src/PHOrbRecoveryEditor.m ios/src/PHOrbProfiles.m ios/src/PHOrbProfileEditor.m \
    "$build_dir/PHOrbNative.o" "$build_dir/PHOrbSession.o" "$build_dir/PHOrbSolver.o" \
    "$build_dir/PHBattleNative.o" "$build_dir/PHTimer.o" "$build_dir/PHMusic.o" -lc++ \
    -framework Foundation -framework CoreFoundation -framework UIKit -framework CoreGraphics -framework QuartzCore -framework WebKit -framework UniformTypeIdentifiers \
    -Wl,-dead_strip -o "$build_dir/PADCheaper.dylib"

# This ad-hoc signature is for artifact integrity, not an iPhone install identity.
# LiveContainer must sign the dylib using its configured device certificate.
codesign --force --sign - --timestamp=none "$build_dir/PADCheaper.dylib"
codesign --verify --strict "$build_dir/PADCheaper.dylib"
xcrun --sdk iphoneos lipo "$build_dir/PADCheaper.dylib" -verify_arch arm64

cp ios/config.example.json "$build_dir/config.example.json"
cp ios/README.md "$build_dir/README.md"
cp ios/docs/feasibility.md "$build_dir/feasibility.md"
cp ios/docs/runtime.md "$build_dir/runtime.md"
cp ios/docs/device-testing.md "$build_dir/device-testing.md"
cp ios/docs/monster-replacement.md "$build_dir/monster-replacement.md"
cp ios/docs/badge-timer.md "$build_dir/badge-timer.md"
cp ios/docs/battle-input.md "$build_dir/battle-input.md"
cp ios/docs/team-presets.md "$build_dir/team-presets.md"
cp ios/docs/formation.md "$build_dir/formation.md"
cp ios/docs/three-player-entry.md "$build_dir/three-player-entry.md"
cp ios/docs/entry-derived-state.md "$build_dir/entry-derived-state.md"
cp ios/docs/auto-orb-feasibility.md "$build_dir/auto-orb-feasibility.md"
cp ios/docs/auto-orbs.md "$build_dir/auto-orbs.md"
cp ios/docs/orb-goals.md "$build_dir/orb-goals.md"
cp ios/docs/orb-automation.md "$build_dir/orb-automation.md"
cp ios/docs/orb-strategies.md "$build_dir/orb-strategies.md"
cp ios/docs/orb-survival.md "$build_dir/orb-survival.md"
cp ios/docs/orb-recovery.md "$build_dir/orb-recovery.md"
cp ios/docs/orb-effects.md "$build_dir/orb-effects.md"
cp ios/docs/orb-shape-search.md "$build_dir/orb-shape-search.md"
cp ios/docs/orb-skill-selection.md "$build_dir/orb-skill-selection.md"
cp ios/docs/scripted-team-restore.md "$build_dir/scripted-team-restore.md"
cp ios/docs/release-0.5.8.md "$build_dir/release-0.5.8.md"
cp ios/docs/release-0.5.9.md "$build_dir/release-0.5.9.md"
cp ios/docs/release-0.5.10.md "$build_dir/release-0.5.10.md"
cp ios/docs/release-0.5.11.md "$build_dir/release-0.5.11.md"
cp ios/docs/release-0.5.12.md "$build_dir/release-0.5.12.md"
cp ios/docs/release-0.5.13.md "$build_dir/release-0.5.13.md"
cp ios/docs/orb-multiplayer-once.md ios/docs/orb-0511-log-analysis.md "$build_dir/"
cp ios/docs/orb-spinner-thorn-search.md "$build_dir/orb-spinner-thorn-search.md"
cp ios/docs/opening-resize-music.md "$build_dir/opening-resize-music.md"
cp ios/docs/persistence-music-sharing.md "$build_dir/persistence-music-sharing.md"
cp ios/docs/autocombo-review.md "$build_dir/autocombo-review.md"
cp ios/docs/ad-revival.md "$build_dir/ad-revival.md"
cp ios/docs/clear-dungeon-audit.md "$build_dir/clear-dungeon-audit.md"
cp ios/docs/release-0.5.7.md "$build_dir/release-0.5.7.md"
cp ios/docs/solver-research-2026-10-30.md "$build_dir/solver-research-2026-10-30.md"
cp ios/docs/release-0.5.6.md "$build_dir/release-0.5.6.md"
cp ios/docs/team-plan-config.md "$build_dir/team-plan-config.md"
mkdir -p "$build_dir/templates"
cp -R ios/templates/ishida-uryu-gamewith-574233 "$build_dir/templates/"
cp -R ios/templates/halloween-ciel-gamewith-579537 "$build_dir/templates/"
cp ios/docs/release-0.5.5.md "$build_dir/release-0.5.5.md"
cp ios/docs/release-0.5.4.md "$build_dir/release-0.5.4.md"
cp ios/docs/release-0.5.3.md "$build_dir/release-0.5.3.md"
cp ios/docs/uryu-byakuya-0.5.2.md "$build_dir/uryu-byakuya-0.5.2.md"
cp ios/docs/development.md "$build_dir/development.md"
cp ios/targets/orbs-23.6.0.json "$build_dir/orb-target.json"
mkdir -p "$build_dir/third_party/pazusoba"
cp ios/third_party/pazusoba/LICENSE ios/third_party/pazusoba/NOTICE.md "$build_dir/third_party/pazusoba/"
mkdir -p "$build_dir/research"
cp ios/research/pazusoba_probe.cpp ios/research/pazusoba_benchmark.py ios/research/pazusoba-results.json "$build_dir/research/"
cp ios/research/orb-shape-0407.json "$build_dir/research/"
cp ios/research/orb_shape_probe.cpp ios/research/orb_shape_benchmark.py ios/research/orb-shape-results.json "$build_dir/research/"
cp ios/research/orb-spinner-20261030.json ios/research/orb-shape-results-057.json "$build_dir/research/"
cp ios/research/orb_thorn_probe.cpp ios/research/orb_thorn_benchmark.py ios/research/orb-thorn-results-058.json "$build_dir/research/"
cp ios/research/orb-shape-thorn-20261030.json ios/research/orb-shape-thorn-feasibility-0510.json ios/research/orb-shape-thorn-results-0510.json ios/research/orb-shape-results-0510.json "$build_dir/research/"
cp ios/research/orb_recorded_shape_probe.cpp ios/research/orb_recorded_shape_benchmark.py ios/research/orb_lowhp_reachability_probe.cpp ios/research/orb_dense_heart_probe.cpp "$build_dir/research/"
cp ios/research/orb-poison-20261030.json ios/research/orb-poison-results-0511.json ios/research/orb_poison_probe.cpp ios/research/orb_poison_benchmark.py "$build_dir/research/"
cp ios/research/orb-multiplayer-20261031.json ios/research/halloween-ciel-579537-native.json "$build_dir/research/"
cp ios/research/orb-multiplayer-leaders-20261031.json ios/research/orb-multiplayer-next-board-20261031.json "$build_dir/research/"
cp ios/research/orb-multiplayer-leader-capacity-20261031.json "$build_dir/research/"
cp ios/data/fixed-leader-skills.json "$build_dir/research/"
cp ios/research/orb-0511-log-analysis-20261030.json ios/research/orb-0511-logs-20261030.json ios/research/orb_0511_log_analysis.py "$build_dir/research/"
cp ios/targets/battle-23.6.0.json "$build_dir/battle-target.json"
cp ios/targets/pad-23.6.0.json "$build_dir/target.json"
cp LICENSE "$build_dir/LICENSE"
rm "$build_dir/PHBattleNative.o" "$build_dir/PHTimer.o" "$build_dir/PHMusic.o" "$build_dir/PHOrbNative.o" "$build_dir/PHOrbSession.o" "$build_dir/PHOrbSolver.o"

python3 - "$build_dir" <<'PY'
import hashlib
import json
import re
from pathlib import Path
import subprocess
import sys

directory = Path(sys.argv[1])
binary = directory / "PADCheaper.dylib"
for notes in directory.glob("*.md"):
    content = notes.read_text(encoding="utf-8")
    for old, new in (("](docs/", "]("), ("](../research/", "](research/"),
                     ("](../templates/", "](templates/"), ("](../third_party/", "](third_party/"),
                     ("](../README.md", "](README.md"),
                     ("](../.github/workflows/ios-dylib.yml)",
                      "](https://github.com/hugotang/padcheaper/actions/workflows/ios-dylib.yml)")):
        content = content.replace(old, new)
    notes.write_text(content, encoding="utf-8")
for template_notes in (directory / "templates").glob("*/README.md"):
    template_notes.write_text(template_notes.read_text(encoding="utf-8").replace(
        "](../../docs/", "](../../"), encoding="utf-8")
sha = hashlib.sha256(binary.read_bytes()).hexdigest()
version = re.search(r'#define PH_RUNTIME_VERSION @"([^"]+)"',
                    Path("ios/src/PHOverlay.h").read_text()).group(1)
metadata = {
    "schema": 1,
    "commit": subprocess.check_output(["git", "rev-parse", "HEAD"], text=True).strip(),
    "architecture": "arm64",
    "dylib_version": version,
    "gui": "mobile team overview and member sheet; acknowledged atomic library saves; immutable applied-recipe matching; full-slot swap/copy, undo, recent cards; 16px inputs and pinch zoom; PADDashFormation import/export",
    "formation_save": "canonical write must succeed before publishing the automatic entry selection; failed or unacknowledged saves keep the editor open and retain drafts",
    "automatic_orbs": "single-player 6x5/7x6/5x4 continuous or once; 3/4/8-player explicit local once with Combo/manual/current native leader goals; local turn/view/source and current parameter checks, explicit 4P extra-skill helper handling; every route requires goals, native timing and swap/release acknowledgement; pending device verification",
    "multiplayer_next_board": "4P accepts the native retained submitted bit only with all other local-window checks; 3P/8P retain their existing flag masks; next gesture requires another explicit tap; device confirmation pending",
    "orb_readiness_diagnostics": "current session and runtime button gates plus up to 16 distinct multiplayer states observed by UI snapshots, retained after exit; metadata-only release keeps the last complete observed board",
    "team_plan_config": "versioned formation + ordered confirmed opening + rotation + board goals; confirmed consumeAssist and final puzzleAfter; accepted-use group intervals and startAfter persist within the same battle/strategy across stop/restart; atomic single-file config commit; source/skill constraints never force a native choice",
    "team_plan_templates": "Uryu preserves Byakuya on a sufficient first board with immediate shortage rescue; CIEL recommended plus independent single/double Kenpachi plans; single Kenpachi uses Unohana #14061 after a seven-step opening with mandatory three-skill supply and absorption rescue only; no universal seamless-skill or healing guarantee",
    "diagonal_orbs": "optional default-off adjacent eight-direction search, replay and native acknowledgement; matching stays horizontal/vertical; atomically saved with selected profile/team plan; device verification pending",
    "skill_window_recheck": "transient native closure waits up to three seconds and rechecks before activation and first touch; no once/group/opening receipt consumed while waiting; persistent restrictions and identity/input changes still stop",
    "profile_file_sharing": "selected ordinary profile or complete team plan as bounded JSON file; native share sheet and document picker; preview before atomic save; duplicate IDs refused; no account/API/master switches",
    "deck_persistence": "save_decks ID 58 native commit; exact queued raw record correlation recovers serializer-defaulted badges and respects confirmed explicit zero; account-bound per-field deferred restore with readback and cancellation",
    "unlock_restoration": "disabled cosmetic ownership and descriptor restoration retains failed work until readback; changed table, ID or native descriptor value revokes old lease",
    "uncleared_team_music": "optional default-off single-player named registered tracks; native ASCII case-insensitive prefix and first-match order; named-track diagnostics; read-only clear predicate; native temporary music override, boss selection and event/script priority",
    "opening_board_resize": "validated native resize interpolation preserves in-flight skill receipts; no geometry, planning, next skill or input during resizing; stable full board required to continue; original identity/input/multiplayer guards and bounded timeout retained",
    "skill_body_choice": "native same-evolution-family base/assist choice only; body still passes native cooldown, board and status conditions; source/stage/family rechecks; no forced readiness or charge writes",
    "thorn_combo_priority": "optional default-off, atomically saved with profile and team plan; goals/hazards then combos before thorn damage when enabled; exact first-wave heart four and verified surviving HP still mandatory",
    "normal_poison_with_heart_four": "optional default-off per strategy and full team plan; normal poison matches accepted only with an exact straight first-wave heart four in every shared spinner phase; goals and Combo precede accepted poison count; mortal poison forbidden and all bombs must clear first wave; user-confirmed team recovery, no healing or poison damage calculation",
    "thorn_zero_contact_search": "contact candidates can trigger a bounded zero-contact pass within the original deadline; independent replay fixture records comparison, not device timing",
    "clear_dungeon_audit": "read-only request/statistics analysis documented; no clear payload rewrite, audit clearing or local revival",
    "temporary_team_form": "validated native temporary identity preserves completed opening; original/generic actor skills pause, explicit current-monster rules may run; required rescue remains enforced; actual current leaders determine leader-mode goals",
    "orb_release_trace": "fresh actual-board evaluation before release with captured poison policy; bounded last-three accepted routes and search trace; independent mortal poison maxima and evaluated hazard failure context; no device first-wave confirmation",
    "automatic_skills": "up to 12 ordered conjunctive rules: member/current monster, run-relative turn interval, floor, orb shortage, and verified enemy absorb/void conditions; native current-skill confirmation with readiness gates; at most one skill per position per orb turn; no automatic dungeon entry or result-screen input",
    "skill_readiness_diagnostics": "refusals retain attempt-time accumulated charge, actual native base/assist IDs and cooldowns, protected stages and derived stage caches; idle polls call no getters; cooldown sufficiency never overrides native refusal; skill-stage/level/assist/use-count races invalidate the attempt",
    "skill_selection_reset": "only an idle member passing native readiness may reset a retained selection to the native default sentinel; exact ID and fresh snapshot checked, no charge writes",
    "floating_orb_shortcuts": "default collapsed 56px PC bubble; idle tap expands once/auto/collapse, second PC opens settings; busy PC immediately stops even folded; refresh/panel close retain folding; draggable shortcuts wrap with 44px minimum targets; current multiplayer readiness shares the button gate and retained stop cause stays in diagnostics/accessibility",
    "scripted_team_provider": "inspect native bounded bytecode from initial and all event roots; share empty records only when no party opcode exists; preserve native script and events; refuse malformed/unknown/running contexts",
    "team_restore_continuity": "ignore only native assist padding and unused skill bytes without an assist; retain per-slot restore failure reasons and retry unfinished leases; successful restores permit another team; uncertain writes block their original battle until an observed exit or validated different battle",
    "orb_frame_pacing": "65 ms target submission spacing includes native render time; every swap must still be acknowledged; unchanged four-second execution deadline and native time budget, with specific stop cause and elapsed time",
    "automatic_orb_input": "one began per route followed by held frames in the same private input bank; accepts tall iPhone layouts that defer selection; initial cancellation cause survives unowned/scene cleanup; start sent/accepted diagnostics; device repair pending",
    "automatic_skill_ready_shortcut": "rule editor sets every=1 while preserving all conditions, rotation group and floor reservation; checks before each orb turn only in continuous mode",
    "orb_strategies": "up to 64 saved profiles bind start-time team/dungeon; explicit-start matching rejects ambiguity; six rotating exclusive skill groups, battle-once rules and slot-wide floor reservations; saved profile and working settings commit atomically",
    "orb_survival": "read-only effective HP thresholds and temporary awakening/member bind or skill-seal conditions; required rules/groups need confirmed completion before a route while allowing other skills to remedy or haste; uncertain attempts never count as success; captured HP/player-state changes cancel the route; per-rule reasons shown; device verification pending",
    "orb_recovery": "optional continuous-only post-skill HP threshold adds one to three heart groups, at least three connected or exactly four in a straight line; conjoins base goals without overriding required skills; unknown HP, insufficient hearts or unmet goals stop; atomic profile persistence; no recovery-amount or damage estimate; device verification pending",
    "orb_obstacles": "cloud/blind/super-blind use actual types; tape/fixed start constrain every step; weak/locked/nail/combo metadata preserved; native unmatchable and removal rules applied; jammer ordinary combos, poison/mortal matches forbidden by default; optional normal poison policy requires first-wave heart four in all phases; mortal poison remains forbidden; all bombs must clear first wave; thorn target contacts require current HP above pending plus route damage upper bound and first-wave heart four, without crediting healing/resistance; unknown effects still block",
    "skill_orb_conditions": "native refusal may explain direct composite type-255 color counts; only actual attempt context retained; no bypass of native readiness; color-specific unmatchable rule uses ANY selected basic color, other predicates remain AND",
    "orb_spinners": "validate all six stored row masks before clipping to the current board after resize; offscreen cells do not block; custom types 0-9; avoid every visible spinner cell; all shared phases must meet goals and hazards; no timed color capture; raw read diagnostics retained; pending device verification",
    "orb_shape_search": "bitset runs/groups/gravity preserve exact four and conservative shape borders; spinner/thorn primary search focuses costly exact shapes and intermediate thorn repairs can complete heart fours later; final HP/damage/heart/hazard gates unchanged; phase-stable hints, missing-goal recovery, two discarded-frontier revisits and zero-contact pass share original time/path budgets; device verification pending",
    "automatic_orb_leaders": "read-only current leader/helper IDs; offline structured minimum board conditions; unknown rules block automatic planning; HP, skill use, awakening/attack-attribute validity, added combos and damage are not evaluated",
    "automatic_orbs_user_report": "user reported 0.5.0-orbs ordinary single-player 6x5 testing passed on 2026-10-28; this does not validate newer geometry or quality settings",
    "battle_replacement": "Single player only; up to six native parameter leases; body/weapon/training/awakenings; individual/all restore; device verification pending",
    "automatic_entry": "Single player only; selected saved team at fresh wave-zero opening; native Skill Boost and effective HP initialization; resumes and later waves excluded",
    "entry_recovery": "Multiplayer rebind removed; every mutation and rollback requires live single-player selectors; old intent is discarded without native writes",
    "entry_board_limit": "board dimensions and generated opening board remain from the native constructor; ordinary team refresh does not regenerate them",
    "multiplayer_write_policy": "Nonzero multi or modes 7/10/11 block team, skill, continuous, timer and BGM mutations regardless of filters; battle policy remains latched until exit. The sole exception is explicit local once for mode 7/11/10 selector 1: fresh ownership/view/clock guards, arm, one began and token-bound release; no packet edits or network pump/bank getters; multiplayer device synchronization unverified",
    "single_player_entry_report": "user confirmed automatic team application and Skill Boost working in 0.4.1-entry on 2026-10-28",
    "battle_filters": "All mode categories except multiplayer; source/multi zero defaults; editable mode/source GUI filters; legacy multi settings load but cannot override single-player restriction",
    "input_gate": "native raw/frame touch records and orb drag; phase/input/read failures reported separately; 0.3.2-battle",
    "badge_unlock": "user-reported unlock working in 0.3.0-battle; save/restart and account boundary checks pending",
    "timer_controls": "automatic flags only; explicit timed exit popup dismissal; device verification pending",
    "platform": "iPhoneOS",
    "minimum_ios": "15.0",
    "signing": "ad-hoc; use the existing Dopamine rootless injection setup, or re-sign in LiveContainer",
    "mode": "diagnostic by default; experimental writes opt in",
    "device_verified": False,
    "sha256": sha,
}
(directory / "build-info.json").write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8")
(directory / "SHA256SUMS").write_text(f"{sha}  PADCheaper.dylib\n", encoding="utf-8")
print(f"Built arm64 iPhoneOS dylib: {binary}")
PY

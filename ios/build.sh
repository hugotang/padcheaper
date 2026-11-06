#!/usr/bin/env bash
set -euo pipefail

repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_dir"
build_dir="${BUILD_DIR:-$repo_dir/build/ios}"
mkdir -p "$build_dir" ios/generated
python3 ios/tools/release_metadata.py header
python3 ios/tools/generate_target.py
python3 ios/tools/generate_battle.py
python3 ios/tools/generate_orbs.py
python3 ios/tools/generate_editor.py
python3 ios/tools/generate_leaders.py

sdk_path="$(xcrun --sdk iphoneos --show-sdk-path)"
clang_path="$(xcrun --sdk iphoneos --find clang)"
min_version="${IPHONEOS_DEPLOYMENT_TARGET:-$(python3 ios/tools/release_metadata.py minimum-ios)}"

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
    ios/src/PHTimerModel.c ios/src/PHOrbModel.c ios/src/PHOrbController.m ios/src/PHOrbPlanning.m ios/src/PHOrbRules.m ios/src/PHOrbRuleEditor.m ios/src/PHOrbRecoveryEditor.m ios/src/PHOrbProfiles.m ios/src/PHOrbEight.m ios/src/PHOrbProfileEditor.m ios/src/PHOrbDungeonReport.m \
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
cp ios/CHANGELOG.md "$build_dir/CHANGELOG.md"
cp ios/docs/feasibility.md "$build_dir/feasibility.md"
cp ios/docs/runtime.md "$build_dir/runtime.md"
cp ios/docs/device-testing.md "$build_dir/device-testing.md"
cp ios/docs/monster-replacement.md "$build_dir/monster-replacement.md"
cp ios/docs/badge-timer.md "$build_dir/badge-timer.md"
cp ios/docs/battle-input.md "$build_dir/battle-input.md"
cp ios/docs/team-presets.md "$build_dir/team-presets.md"
cp ios/docs/formation.md "$build_dir/formation.md"
# Every release note ships; new versions need no build.sh edit.
cp ios/docs/release-*.md "$build_dir/"
cp ios/docs/team-model.md ios/docs/team-dungeon.md "$build_dir/"
cp ios/docs/orb-eight-player-design.md "$build_dir/"
cp ios/docs/eight-player-b5-followup.md "$build_dir/"
cp ios/docs/eight-player-0522-review.md "$build_dir/"
cp ios/docs/eight-player-0523-review.md "$build_dir/"
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
cp ios/docs/dungeon-report.md "$build_dir/dungeon-report.md"
cp ios/docs/enemy-countermeasures.md "$build_dir/enemy-countermeasures.md"
cp ios/docs/combo-bonus.md "$build_dir/combo-bonus.md"
cp ios/docs/orb-multiplayer-once.md ios/docs/orb-0511-log-analysis.md "$build_dir/"
cp ios/docs/orb-spinner-thorn-search.md "$build_dir/orb-spinner-thorn-search.md"
cp ios/docs/opening-resize-music.md "$build_dir/opening-resize-music.md"
cp ios/docs/persistence-music-sharing.md "$build_dir/persistence-music-sharing.md"
cp ios/docs/autocombo-review.md "$build_dir/autocombo-review.md"
cp ios/docs/ad-revival.md "$build_dir/ad-revival.md"
cp ios/docs/clear-dungeon-audit.md "$build_dir/clear-dungeon-audit.md"
cp ios/docs/solver-research-2026-10-30.md "$build_dir/solver-research-2026-10-30.md"
cp ios/docs/solver-colors-2026-11-06.md ios/docs/solver-shapes-2026-11-06.md ios/docs/solver-shape-recovery-2026-11-06.md "$build_dir/"
cp ios/docs/solver-fishing-2026-11-06.md ios/docs/data-update-investigation-2026-11-06.md "$build_dir/"
cp ios/docs/solver-assist-fishing-2026-11-06.md ios/docs/formation-profile-persistence-2026-11-06.md "$build_dir/"
cp ios/docs/team-plan-config.md "$build_dir/team-plan-config.md"
mkdir -p "$build_dir/templates"
cp -R ios/templates/ishida-uryu-gamewith-574233 "$build_dir/templates/"
cp -R ios/templates/halloween-ciel-gamewith-579537 "$build_dir/templates/"
cp -R ios/templates/eight-player-thanks-2026 "$build_dir/templates/"
cp ios/docs/uryu-byakuya-0.5.2.md "$build_dir/uryu-byakuya-0.5.2.md"
cp ios/docs/development.md "$build_dir/development.md"
cp ios/docs/address-update-23.6.1.md "$build_dir/address-update-23.6.1.md"
cp ios/docs/member-state.md ios/docs/auto-session-state.md ios/docs/diagnostics-release.md "$build_dir/"
cp ios/docs/enemy-aware-automation.md "$build_dir/enemy-aware-automation.md"
cp ios/docs/battle-observation.md "$build_dir/battle-observation.md"
cp ios/docs/enemy-action-observation.md "$build_dir/enemy-action-observation.md"
cp ios/docs/enemy-selector-analysis.md "$build_dir/enemy-selector-analysis.md"
cp ios/docs/enemy-dispatch-analysis.md "$build_dir/enemy-dispatch-analysis.md"
cp ios/docs/enemy-damage-analysis.md "$build_dir/enemy-damage-analysis.md"
cp ios/docs/assist-recovery.md "$build_dir/assist-recovery.md"
cp ios/targets/orbs-23.6.1.json "$build_dir/orb-target.json"
mkdir -p "$build_dir/third_party/pazusoba"
cp ios/third_party/pazusoba/LICENSE ios/third_party/pazusoba/NOTICE.md "$build_dir/third_party/pazusoba/"
mkdir -p "$build_dir/third_party/PADDashFormation" "$build_dir/editor-source/assets"
cp ios/editor/assets/awoken.png ios/editor/assets/latent.png ios/editor/assets/NOTICE.md ios/editor/assets/PADDashFormation-LICENSE "$build_dir/third_party/PADDashFormation/"
cp ios/editor/*.js ios/editor/*.json ios/editor/editor.html "$build_dir/editor-source/"
cp ios/editor/assets/* "$build_dir/editor-source/assets/"
cp ios/tools/generate_editor.py ios/tools/update_team_catalog.py "$build_dir/editor-source/"
mkdir -p "$build_dir/tools"
cp ios/tools/compare_team_dungeon.js "$build_dir/tools/"
cp ios/editor/dungeon-guides.json "$build_dir/dungeon-guides.json"
mkdir -p "$build_dir/research"
cp ios/research/address-update-23.6.1.json "$build_dir/research/"
cp ios/research/orb-shape-simulation-20261106.json ios/research/orb-shape-results-20261106.json ios/research/orb-shape-recovery-results-20261106.json ios/research/orb-shape-holdout-20261106.json "$build_dir/research/"
cp ios/research/orb_shape_simulation.cpp ios/research/orb_shape_simulation.py ios/research/orb_shape_hint_audit.cpp "$build_dir/research/"
cp ios/research/orb_timed_fishing_probe.cpp "$build_dir/research/"
cp ios/research/solo-team-model-20261101.json "$build_dir/research/"
cp ios/research/eight-player-cup-20261102.json "$build_dir/research/"
cp ios/research/orb-auto-readiness-stop-20261102.json "$build_dir/research/"
cp ios/research/orb-eight-skill-resume-20261102.json "$build_dir/research/"
cp ios/research/team-skill-dungeon-20261101.json "$build_dir/research/"
cp ios/research/orb-enemy-selector-20261101.json ios/research/orb-enemy-dispatch-20261101.json ios/research/orb-enemy-damage-20261101.json "$build_dir/research/"
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
cp ios/research/orb-multiplayer-peer-exit-20261031.json "$build_dir/research/"
cp ios/research/orb-multiplayer-entry-20261031.json "$build_dir/research/"
cp ios/research/orb-multiplayer-solo-20261031.json ios/research/orb-spinner-search-20261031.json ios/research/orb_spinner_search_benchmark.py "$build_dir/research/"
cp ios/research/orb-dense-spinner-20261031.json "$build_dir/research/"
cp ios/research/orb-opening-read-20261101.json ios/research/orb-battle-observation-20261101.json "$build_dir/research/"
cp ios/research/orb-assist-recovery-20261101.json "$build_dir/research/"
cp ios/research/orb-enemy-actions-20261101.json "$build_dir/research/"
cp ios/data/fixed-leader-skills.json "$build_dir/research/"
cp ios/research/orb-0511-log-analysis-20261030.json ios/research/orb-0511-logs-20261030.json ios/research/orb_0511_log_analysis.py "$build_dir/research/"
cp ios/targets/battle-23.6.1.json "$build_dir/battle-target.json"
cp ios/targets/pad-23.6.1.json "$build_dir/target.json"
# Historical notes retain their original evidence; never redirect their links
# to the newly selected binary's descriptor.
mkdir -p "$build_dir/targets"
cp ios/targets/pad-23.6.0.json ios/targets/battle-23.6.0.json ios/targets/orbs-23.6.0.json "$build_dir/targets/"
cp LICENSE "$build_dir/LICENSE"
rm "$build_dir/PHBattleNative.o" "$build_dir/PHTimer.o" "$build_dir/PHMusic.o" "$build_dir/PHOrbNative.o" "$build_dir/PHOrbSession.o" "$build_dir/PHOrbSolver.o"

python3 ios/tools/release_metadata.py finalize "$build_dir" --minimum-ios "$min_version"

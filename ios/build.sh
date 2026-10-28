#!/usr/bin/env bash
set -euo pipefail

repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_dir"
build_dir="${BUILD_DIR:-$repo_dir/build/ios}"
mkdir -p "$build_dir" ios/generated
python3 ios/tools/generate_target.py
python3 ios/tools/generate_battle.py
python3 ios/tools/generate_editor.py

sdk_path="$(xcrun --sdk iphoneos --show-sdk-path)"
clang_path="$(xcrun --sdk iphoneos --find clang)"
min_version="${IPHONEOS_DEPLOYMENT_TARGET:-15.0}"

for native in PHBattleNative PHTimer; do
"$clang_path" -arch arm64 -isysroot "$sdk_path" \
    "-miphoneos-version-min=$min_version" -std=c++17 -fobjc-arc -fblocks -fexceptions \
    -O2 -Wall -Wextra -Werror -Wno-unused-parameter -Wno-unused-const-variable \
    -Iios/src -Iios/generated -c "ios/src/$native.mm" -o "$build_dir/$native.o"
done

"$clang_path" -arch arm64 -isysroot "$sdk_path" \
    "-miphoneos-version-min=$min_version" -std=gnu11 -fobjc-arc -fblocks \
    -O2 -Wall -Wextra -Werror -Wno-unused-parameter -Wno-unused-const-variable \
    -dynamiclib -install_name @rpath/PADCheaper.dylib \
    -Iios/src -Iios/generated \
    ios/src/PHCodec.m ios/src/PHSaveState.m ios/src/PHMemory.m ios/src/PHIdentity.m ios/src/PHConfiguration.m \
    ios/src/PHTarget.m ios/src/PHOverlay.m ios/src/PHRuntime.m ios/src/PHFormationController.m ios/src/PHFormationModel.m \
    ios/src/PHInputModel.c ios/src/PHBattleModel.c ios/src/PHBattlePreset.c ios/src/PHBattleSession.m ios/src/PHBadgeModel.c ios/src/PHBadges.m \
    ios/src/PHTimerModel.c "$build_dir/PHBattleNative.o" "$build_dir/PHTimer.o" -lc++ \
    -framework Foundation -framework CoreFoundation -framework UIKit -framework CoreGraphics -framework QuartzCore -framework WebKit \
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
mkdir -p "$build_dir/research"
cp ios/research/pazusoba_probe.cpp ios/research/pazusoba_benchmark.py ios/research/pazusoba-results.json "$build_dir/research/"
cp ios/targets/battle-23.6.0.json "$build_dir/battle-target.json"
cp ios/targets/pad-23.6.0.json "$build_dir/target.json"
cp LICENSE "$build_dir/LICENSE"
rm "$build_dir/PHBattleNative.o" "$build_dir/PHTimer.o"

python3 - "$build_dir" <<'PY'
import hashlib
import json
import re
from pathlib import Path
import subprocess
import sys

directory = Path(sys.argv[1])
readme = directory / "README.md"
readme.write_text(readme.read_text(encoding="utf-8").replace(
    "](docs/", "](").replace(
    "](../.github/workflows/ios-dylib.yml)",
    "](https://github.com/hugotang/padcheaper/actions/workflows/ios-dylib.yml)"), encoding="utf-8")
binary = directory / "PADCheaper.dylib"
research_notes = directory / "auto-orb-feasibility.md"
research_notes.write_text(research_notes.read_text(encoding="utf-8").replace(
    "](../research/", "](research/"), encoding="utf-8")
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
    "automatic_orbs": "feasibility research only; no solver or automatic touch execution is included in this dylib",
    "battle_replacement": "Single player only; up to six native parameter leases; body/weapon/training/awakenings; individual/all restore; device verification pending",
    "automatic_entry": "Single player only; selected saved team at fresh wave-zero opening; native Skill Boost and effective HP initialization; resumes and later waves excluded",
    "entry_recovery": "Multiplayer rebind removed; every mutation and rollback requires live single-player selectors; old intent is discarded without native writes",
    "entry_board_limit": "board dimensions and generated opening board remain from the native constructor; ordinary team refresh does not regenerate them",
    "multiplayer_write_policy": "Nonzero multi or mode 7 cannot be enabled by filters; detected multiplayer remains blocked until battle exit; metadata-only probes make no native calls",
    "single_player_entry_report": "user confirmed automatic team application and Skill Boost working in 0.4.1-entry on 2026-10-28",
    "battle_filters": "All mode categories except multiplayer; source/multi zero defaults; editable mode/source GUI filters; legacy multi settings load but cannot override single-player restriction",
    "input_gate": "native raw/frame touch records and orb drag; phase/input/read failures reported separately; 0.3.2-battle",
    "badge_unlock": "user-reported unlock working in 0.3.0-battle; save/restart and account boundary checks pending",
    "timer_controls": "automatic flags only; explicit timed exit popup dismissal; device verification pending",
    "platform": "iPhoneOS",
    "minimum_ios": "15.0",
    "signing": "ad-hoc; re-sign in LiveContainer",
    "mode": "diagnostic by default; experimental writes opt in",
    "device_verified": False,
    "sha256": sha,
}
(directory / "build-info.json").write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8")
(directory / "SHA256SUMS").write_text(f"{sha}  PADCheaper.dylib\n", encoding="utf-8")
print(f"Built arm64 iPhoneOS dylib: {binary}")
PY

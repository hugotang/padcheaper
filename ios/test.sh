#!/usr/bin/env bash
set -euo pipefail

repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_dir"
build_dir="${BUILD_DIR:-$repo_dir/build/ios-tests}"
mkdir -p "$build_dir"
sdk_path="$(xcrun --sdk macosx --show-sdk-path)"
clang_path="$(xcrun --sdk macosx --find clang)"
python3 ios/tools/generate_battle.py
python3 ios/tools/generate_orbs.py
python3 ios/tools/generate_editor.py
node ios/tests/formation_tests.js

"$clang_path" -isysroot "$sdk_path" -std=gnu11 -Wall -Wextra -Werror -Iios/src \
    ios/src/PHInputModel.c ios/tests/input_model_tests.c -o "$build_dir/input-model-tests"
"$build_dir/input-model-tests"

"$clang_path" -isysroot "$sdk_path" -std=gnu11 -fobjc-arc -fblocks -Wall -Wextra -Werror \
    -Wno-unused-const-variable -Iios/src -Iios/generated \
    ios/src/PHInputModel.c ios/tests/input_runtime_tests.m -framework Foundation -o "$build_dir/input-runtime-tests"
"$build_dir/input-runtime-tests"

"$clang_path" -isysroot "$sdk_path" -std=gnu11 -Wall -Wextra -Werror -Iios/src \
    ios/src/PHInputModel.c ios/src/PHBattleModel.c ios/tests/battle_model_tests.c -o "$build_dir/battle-model-tests"
"$build_dir/battle-model-tests"

"$clang_path" -isysroot "$sdk_path" -std=gnu11 -Wall -Wextra -Werror -Iios/src \
    ios/src/PHInputModel.c ios/src/PHBattleModel.c ios/src/PHBattlePreset.c ios/tests/battle_preset_tests.c -o "$build_dir/battle-preset-tests"
"$build_dir/battle-preset-tests"

for model in PHInputModel PHBattleModel PHBattlePreset PHOrbModel; do
    "$clang_path" -isysroot "$sdk_path" -std=gnu11 -Wall -Wextra -Werror -Iios/src \
        -c "ios/src/$model.c" -o "$build_dir/$model.o"
done
"$clang_path" -isysroot "$sdk_path" -std=c++17 -O2 -Wall -Wextra -Werror -Iios/src \
    -c ios/src/PHOrbSolver.cpp -o "$build_dir/PHOrbSolver.o"
"$clang_path" -isysroot "$sdk_path" -std=c++17 -O2 -Wall -Wextra -Werror -Iios/src \
    ios/tests/orb_solver_tests.cpp "$build_dir/PHOrbSolver.o" "$build_dir/PHOrbModel.o" \
    "$build_dir/PHBattleModel.o" "$build_dir/PHInputModel.o" -lc++ -o "$build_dir/orb-solver-tests"
"$build_dir/orb-solver-tests"
"$clang_path" -isysroot "$sdk_path" -std=c++17 -fobjc-arc -fblocks -fexceptions \
    -Wall -Wextra -Werror -Wno-unused-parameter -Wno-unused-const-variable -Iios/src -Iios/generated \
    ios/tests/orb_native_tests.mm "$build_dir/PHOrbModel.o" "$build_dir/PHBattleModel.o" "$build_dir/PHInputModel.o" \
    -lc++ -framework Foundation -framework CoreFoundation -o "$build_dir/orb-native-tests"
"$build_dir/orb-native-tests"
"$clang_path" -isysroot "$sdk_path" -std=c++17 -fobjc-arc -fblocks -fexceptions \
    -Wall -Wextra -Werror -Wno-unused-parameter -Iios/src \
    ios/src/PHOrbSession.mm ios/tests/orb_session_tests.mm "$build_dir/PHOrbSolver.o" "$build_dir/PHOrbModel.o" \
    "$build_dir/PHBattleModel.o" "$build_dir/PHInputModel.o" -lc++ -framework Foundation -o "$build_dir/orb-session-tests"
"$build_dir/orb-session-tests"
"$clang_path" -isysroot "$sdk_path" -std=c++17 -fobjc-arc -fblocks -fexceptions \
    -Wall -Wextra -Werror -Wno-unused-parameter -Wno-unused-const-variable -Iios/src -Iios/generated \
    ios/tests/battle_native_tests.mm "$build_dir/PHInputModel.o" "$build_dir/PHBattleModel.o" "$build_dir/PHBattlePreset.o" \
    -lc++ -framework Foundation -o "$build_dir/battle-native-tests"
"$build_dir/battle-native-tests"

"$clang_path" -isysroot "$sdk_path" -std=gnu11 -fobjc-arc -fblocks -Wall -Wextra -Werror -Iios/src \
    ios/src/PHInputModel.c ios/src/PHBattleModel.c ios/src/PHBattlePreset.c ios/src/PHFormationModel.m ios/tests/formation_model_tests.m \
    -framework Foundation -framework CoreFoundation -o "$build_dir/formation-model-tests"
"$build_dir/formation-model-tests"

"$clang_path" -isysroot "$sdk_path" -std=gnu11 -Wall -Wextra -Werror -Iios/src \
    ios/src/PHBadgeModel.c ios/tests/badge_model_tests.c -o "$build_dir/badge-model-tests"
"$build_dir/badge-model-tests"

"$clang_path" -isysroot "$sdk_path" -std=gnu11 -fobjc-arc -fblocks -Wall -Wextra -Werror \
    -Wno-unused-const-variable -Iios/src -Iios/generated \
    ios/src/PHBadgeModel.c ios/src/PHInputModel.c ios/src/PHBattleModel.c ios/src/PHBadges.m ios/tests/badge_runtime_tests.m \
    -framework Foundation -o "$build_dir/badge-runtime-tests"
"$build_dir/badge-runtime-tests"

"$clang_path" -isysroot "$sdk_path" -std=gnu11 -Wall -Wextra -Werror -Iios/src \
    ios/src/PHInputModel.c ios/src/PHTimerModel.c ios/tests/timer_model_tests.c -o "$build_dir/timer-model-tests"
"$build_dir/timer-model-tests"

"$clang_path" -isysroot "$sdk_path" -std=gnu11 -fobjc-arc -fblocks -Wall -Wextra -Werror -Iios/src \
    ios/src/PHInputModel.c ios/src/PHBattleModel.c ios/src/PHBattleSession.m ios/tests/battle_session_tests.m \
    -framework Foundation -o "$build_dir/battle-session-tests"
"$build_dir/battle-session-tests"
"$clang_path" -isysroot "$sdk_path" -std=gnu11 -fobjc-arc -fblocks \
    -Wall -Wextra -Werror -Iios/src \
    ios/src/PHCodec.m ios/tests/codec_tests.m \
    -framework Foundation -framework CoreFoundation \
    -o "$build_dir/codec-tests"
"$build_dir/codec-tests"

"$clang_path" -isysroot "$sdk_path" -std=gnu11 -fobjc-arc -fblocks \
    -Wall -Wextra -Werror -Iios/src \
    ios/src/PHIdentity.m ios/src/PHConfiguration.m ios/tests/configuration_tests.m \
    -framework Foundation -framework CoreFoundation \
    -o "$build_dir/configuration-tests"
"$build_dir/configuration-tests"

"$clang_path" -isysroot "$sdk_path" -std=gnu11 -fobjc-arc -fblocks \
    -Wall -Wextra -Werror -Iios/src \
    ios/src/PHSaveState.m ios/tests/save_state_tests.m \
    -framework Foundation \
    -o "$build_dir/save-state-tests"
"$build_dir/save-state-tests"

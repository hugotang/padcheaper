#!/usr/bin/env bash
set -euo pipefail

repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_dir"
build_dir="${BUILD_DIR:-$repo_dir/build/ios-tests}"
mkdir -p "$build_dir"
sdk_path="$(xcrun --sdk macosx --show-sdk-path)"
clang_path="$(xcrun --sdk macosx --find clang)"
python3 ios/tools/generate_battle.py

"$clang_path" -isysroot "$sdk_path" -std=gnu11 -Wall -Wextra -Werror -Iios/src \
    ios/src/PHBattleModel.c ios/tests/battle_model_tests.c -o "$build_dir/battle-model-tests"
"$build_dir/battle-model-tests"

"$clang_path" -isysroot "$sdk_path" -std=gnu11 -Wall -Wextra -Werror -Iios/src \
    ios/src/PHBadgeModel.c ios/tests/badge_model_tests.c -o "$build_dir/badge-model-tests"
"$build_dir/badge-model-tests"

"$clang_path" -isysroot "$sdk_path" -std=gnu11 -fobjc-arc -fblocks -Wall -Wextra -Werror \
    -Wno-unused-const-variable -Iios/src -Iios/generated \
    ios/src/PHBadgeModel.c ios/src/PHBattleModel.c ios/src/PHBadges.m ios/tests/badge_runtime_tests.m \
    -framework Foundation -o "$build_dir/badge-runtime-tests"
"$build_dir/badge-runtime-tests"

"$clang_path" -isysroot "$sdk_path" -std=gnu11 -Wall -Wextra -Werror -Iios/src \
    ios/src/PHTimerModel.c ios/tests/timer_model_tests.c -o "$build_dir/timer-model-tests"
"$build_dir/timer-model-tests"

"$clang_path" -isysroot "$sdk_path" -std=gnu11 -fobjc-arc -fblocks -Wall -Wextra -Werror -Iios/src \
    ios/src/PHBattleModel.c ios/src/PHBattleSession.m ios/tests/battle_session_tests.m \
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

#!/usr/bin/env bash
set -euo pipefail

test_dir="${BUILD_DIR:?Run the portable solver tests with BUILD_DIR first}"
sdk=$(xcrun --sdk macosx --show-sdk-path)
flags=(-isysroot "$sdk" -Wall -Wextra -Werror -Iios/src -Iios/generated)
python3 ios/tools/generate_target.py
python3 ios/tools/generate_leaders.py

objects=("$test_dir/solver.o" "$test_dir/PHOrbModel.o" "$test_dir/PHBattleModel.o" "$test_dir/PHInputModel.o")
xcrun clang "${flags[@]}" -std=gnu11 -c ios/src/PHBattlePreset.c -o "$test_dir/PHBattlePreset.o"
objects+=("$test_dir/PHBattlePreset.o")
for model in PHOrbDungeonReport PHOrbPlanning PHOrbRules PHOrbProfiles PHOrbEight PHFormationModel; do
    xcrun clang "${flags[@]}" -std=gnu11 -fobjc-arc -fblocks -c "ios/src/$model.m" -o "$test_dir/$model.o"
    objects+=("$test_dir/$model.o")
done
xcrun clang++ "${flags[@]}" -std=c++17 -fobjc-arc -fblocks -fexceptions -Wno-unused-parameter \
    ios/src/PHOrbSession.mm ios/tests/orb_session_tests.mm "${objects[@]}" \
    -framework Foundation -framework CoreFoundation -o "$test_dir/orb-session-tests"
"$test_dir/orb-session-tests"

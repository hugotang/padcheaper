#!/usr/bin/env bash
# Ordinary B4 acceptance gates the explicitly requested build. Optional
# fishing diagnostics have a different scope and cannot replace this check.
set -euo pipefail
repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$repo_dir"
test_dir="${BUILD_DIR:?Run the solver tests with BUILD_DIR first}"
solver_object="${CORAL_SOLVER_OBJECT:-$test_dir/solver.o}"
compiler="${CORAL_CXX:-g++}"
flags=(-std=c++17 -O2 -Wall -Wextra -Werror -Iios/src)
if [ "$(uname -s)" = Darwin ]; then
    flags+=(-isysroot "$(xcrun --sdk macosx --show-sdk-path)")
fi
"$compiler" "${flags[@]}" ios/research/coral_b4_six_fours_probe.cpp "$solver_object" \
    "$test_dir/PHOrbModel.o" "$test_dir/PHBattleModel.o" "$test_dir/PHInputModel.o" -o "$test_dir/coral-b4-probe"
probe_status=0
"$test_dir/coral-b4-probe" > "$test_dir/coral-b4-ordinary.jsonl" || probe_status=$?
cat "$test_dir/coral-b4-ordinary.jsonl"
if [ "$probe_status" -eq 2 ]; then
    echo "B4 ordinary acceptance NOT PASSED; the build must fail."
elif [ "$probe_status" -eq 0 ]; then
    python3 ios/tools/verify_coral_first_waves.py --floor 4 "$test_dir/coral-b4-ordinary.jsonl"
    echo "B4 ordinary acceptance passed; device damage and clear remain unverified."
else
    echo "B4 probe failed API/replay validation."
fi
exit "$probe_status"

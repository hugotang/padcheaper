#!/usr/bin/env bash
# Invoked by the explicit build/test suites, never by a source-only review.
# A missed historical B5 board is an acceptance failure, not a passing smoke
# test. Keep the JSONL even on failure; no device damage/clear claim follows.
set -euo pipefail
repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$repo_dir"
test_dir="${BUILD_DIR:?Run the solver tests with BUILD_DIR first}"
solver_object="${CORAL_SOLVER_OBJECT:-$test_dir/solver.o}"
compiler="${CORAL_CXX:-g++}"
"$compiler" -std=c++17 -O2 -Wall -Wextra -Werror -Iios/src \
    ios/research/coral_b5_six_fours_probe.cpp "$solver_object" "$test_dir/PHOrbModel.o" \
    "$test_dir/PHBattleModel.o" "$test_dir/PHInputModel.o" -o "$test_dir/coral-b5-probe"
probe_status=0
"$test_dir/coral-b5-probe" > "$test_dir/coral-b5-acceptance.jsonl" || probe_status=$?
cat "$test_dir/coral-b5-acceptance.jsonl"
if [ "$probe_status" -eq 2 ]; then
    echo "B5 recorded route acceptance NOT PASSED; this is not a repaired release."
elif [ "$probe_status" -eq 0 ]; then
    echo "B5 recorded route acceptance passed; mobile budget, damage and clear remain unverified."
else
    echo "B5 probe failed API/replay validation."
fi
exit "$probe_status"

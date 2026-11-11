#!/usr/bin/env bash
# Windows (MSYS2 mingw-w64) regression for the platform-independent C/C++
# parts: orb model, solver and the 8P inventory model. Objective-C sources
# still require macOS (ios/test.sh) or the manual GitHub Actions workflow.
#
#   bash ios/tools/windows-tests.sh            # build and run
#   BASELINE=<commit> bash ios/tools/windows-tests.sh
#       also builds the solver, its included sources and its tests from
#       <commit> and prints a summary diff, so a performance change can be
#       compared without mixing headers or model layouts from two revisions.
set -euo pipefail

repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$repo_dir"
export PATH="/c/msys64/mingw64/bin:$PATH"
build_dir="${BUILD_DIR:-$repo_dir/build/win-tests}"
mkdir -p "$build_dir"

for model in PHOrbModel PHBattleModel PHInputModel; do
    gcc -std=gnu11 -O2 -Wall -Wextra -Werror -Iios/src -c "ios/src/$model.c" -o "$build_dir/$model.o"
done
gcc -std=gnu11 -Wall -Wextra -Werror -Iios/src ios/tests/orb_eight_model_tests.c -o "$build_dir/orb-eight-model-tests.exe"
"$build_dir/orb-eight-model-tests.exe"

solver_tests() {
    local root="$1" out="$2"
    for model in PHOrbModel PHBattleModel PHInputModel; do
        gcc -std=gnu11 -O2 -Wall -Wextra -Werror "-I$root/src" -c "$root/src/$model.c" -o "$out-$model.o"
    done
    g++ -std=c++17 -O2 -Wall -Wextra -Werror "-I$root/src" -c "$root/src/PHOrbSolver.cpp" -o "$out.o"
    g++ -std=c++17 -O2 -Wall -Wextra -Werror "-I$root/src" "$root/tests/orb_solver_tests.cpp" "$out.o" \
        "$out-PHOrbModel.o" "$out-PHBattleModel.o" "$out-PHInputModel.o" -o "$out.exe"
    "$out.exe" > "$out.log" 2>&1 || { cat "$out.log"; return 1; }
    tail -1 "$out.log"
}
solver_tests ios "$build_dir/solver"

# Private scoring contract: the test includes the solver implementation so
# alternate hints can be checked without exporting implementation details.
g++ -std=c++17 -O2 -Wall -Wextra -Werror -Iios/src ios/tests/orb_shape_guidance_tests.cpp \
    "$build_dir/PHOrbModel.o" "$build_dir/PHBattleModel.o" "$build_dir/PHInputModel.o" -o "$build_dir/shape-guidance-tests.exe"
"$build_dir/shape-guidance-tests.exe"

# Public PHOrbSolve API-validity smoke test; macOS ios/test.sh runs the same
# file under ASan/UBSan. MinGW has no sanitizer runtime, so this is plain -O2.
g++ -std=c++17 -O2 -Wall -Wextra -Werror -Iios/src ios/tests/orb_solve_sanitized_tests.cpp "$build_dir/solver.o" \
    "$build_dir/PHOrbModel.o" "$build_dir/PHBattleModel.o" "$build_dir/PHInputModel.o" -o "$build_dir/solve-smoke-tests.exe"
"$build_dir/solve-smoke-tests.exe" > "$build_dir/solve-smoke-tests.log" 2>&1 || { cat "$build_dir/solve-smoke-tests.log"; exit 1; }
tail -1 "$build_dir/solve-smoke-tests.log"

if [ -n "${BASELINE:-}" ]; then
    # The solver includes PHOrb*.inc/.h and the tests include their own .inc
    # files; one file from the baseline would mix two revisions.
    baseline_dir="$(mktemp -d "$build_dir/baseline.XXXXXX")"
    git archive "$BASELINE" ios/src ios/tests | tar -x -C "$baseline_dir"
    solver_tests "$baseline_dir/ios" "$build_dir/solver-baseline"
    echo "Route summary differences versus $BASELINE (timing lines are expected to differ):"
    summary_status=0
    diff <(grep -E 'fixture|status=' "$build_dir/solver-baseline.log" | sed -E 's/(ms=|, )[0-9]+ ms|elapsed=[0-9]+ ms|ms=[0-9]+//g') \
         <(grep -E 'fixture|status=' "$build_dir/solver.log" | sed -E 's/(ms=|, )[0-9]+ ms|elapsed=[0-9]+ ms|ms=[0-9]+//g') \
        || summary_status=$?
    if [ "$summary_status" -eq 0 ]; then
        echo "identical printed summaries (not a full path comparison)"
    elif [ "$summary_status" -eq 1 ]; then
        echo "summary differences shown above; both regression suites passed"
    else
        exit "$summary_status"
    fi
fi

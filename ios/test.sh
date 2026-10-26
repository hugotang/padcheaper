#!/usr/bin/env bash
set -euo pipefail

repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_dir"
build_dir="${BUILD_DIR:-$repo_dir/build/ios-tests}"
mkdir -p "$build_dir"
sdk_path="$(xcrun --sdk macosx --show-sdk-path)"
clang_path="$(xcrun --sdk macosx --find clang)"
"$clang_path" -isysroot "$sdk_path" -std=gnu11 -fobjc-arc -fblocks \
    -Wall -Wextra -Werror -Iios/src \
    ios/src/PHCodec.m ios/tests/codec_tests.m \
    -framework Foundation -framework CoreFoundation \
    -o "$build_dir/codec-tests"
"$build_dir/codec-tests"

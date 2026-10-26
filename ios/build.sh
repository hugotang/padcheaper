#!/usr/bin/env bash
set -euo pipefail

repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_dir"
build_dir="${BUILD_DIR:-$repo_dir/build/ios}"
mkdir -p "$build_dir" ios/generated
python3 ios/tools/generate_target.py

sdk_path="$(xcrun --sdk iphoneos --show-sdk-path)"
clang_path="$(xcrun --sdk iphoneos --find clang)"
min_version="${IPHONEOS_DEPLOYMENT_TARGET:-15.0}"

"$clang_path" -arch arm64 -isysroot "$sdk_path" \
    "-miphoneos-version-min=$min_version" -std=gnu11 -fobjc-arc -fblocks \
    -O2 -Wall -Wextra -Werror -Wno-unused-parameter -Wno-unused-const-variable \
    -dynamiclib -install_name @rpath/PADCheaper.dylib \
    -Iios/src -Iios/generated \
    ios/src/PHCodec.m ios/src/PHMemory.m ios/src/PHIdentity.m ios/src/PHConfiguration.m \
    ios/src/PHTarget.m ios/src/PHOverlay.m ios/src/PHRuntime.m \
    -framework Foundation -framework CoreFoundation -framework UIKit -framework CoreGraphics -framework QuartzCore \
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
cp ios/targets/pad-23.6.0.json "$build_dir/target.json"
cp LICENSE "$build_dir/LICENSE"

python3 - "$build_dir" <<'PY'
import hashlib
import json
import re
from pathlib import Path
import subprocess
import sys

directory = Path(sys.argv[1])
binary = directory / "PADCheaper.dylib"
sha = hashlib.sha256(binary.read_bytes()).hexdigest()
version = re.search(r'#define PH_RUNTIME_VERSION @"([^"]+)"',
                    Path("ios/src/PHOverlay.h").read_text()).group(1)
metadata = {
    "schema": 1,
    "commit": subprocess.check_output(["git", "rev-parse", "HEAD"], text=True).strip(),
    "architecture": "arm64",
    "dylib_version": version,
    "gui": "draggable floating button; switches and load diagnostics",
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

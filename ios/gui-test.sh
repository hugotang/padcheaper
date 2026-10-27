#!/usr/bin/env bash
set -euxo pipefail

repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_dir"
build_dir="$repo_dir/build/ios-ui-tests"
app_dir="$build_dir/OverlayProbe.app"
mkdir -p "$app_dir/Frameworks"
python3 ios/tools/generate_target.py
python3 ios/tools/generate_battle.py
sdk_path="$(xcrun --sdk iphonesimulator --show-sdk-path)"
clang_path="$(xcrun --sdk iphonesimulator --find clang)"
architecture="$(uname -m)"
run_limited() {
    local timeout_seconds="$1"
    shift
    python3 - "$timeout_seconds" "$@" <<'PY'
import subprocess
import sys
try:
    result = subprocess.run(sys.argv[2:], timeout=int(sys.argv[1]))
except subprocess.TimeoutExpired:
    raise SystemExit("Simulator command timed out: " + " ".join(sys.argv[2:]))
raise SystemExit(result.returncode)
PY
}

for native in PHBattleNative PHTimer; do
"$clang_path" -arch "$architecture" -isysroot "$sdk_path" -mios-simulator-version-min=16.0 \
    -std=c++17 -fobjc-arc -fblocks -fexceptions -O2 -Wall -Wextra -Werror \
    -Wno-unused-parameter -Wno-unused-const-variable -Iios/src -Iios/generated \
    -c "ios/src/$native.mm" -o "$build_dir/$native.o"
done

"$clang_path" -arch "$architecture" -isysroot "$sdk_path" -mios-simulator-version-min=16.0 \
    -std=gnu11 -fobjc-arc -fblocks -O2 -Wall -Wextra -Werror \
    -Wno-unused-parameter -Wno-unused-const-variable -dynamiclib \
    -install_name @rpath/PADCheaperTest.dylib -Iios/src -Iios/generated \
    ios/src/PHCodec.m ios/src/PHSaveState.m ios/src/PHMemory.m ios/src/PHIdentity.m ios/src/PHConfiguration.m \
    ios/src/PHTarget.m ios/src/PHOverlay.m ios/src/PHRuntime.m \
    ios/src/PHBattleModel.c ios/src/PHBattleSession.m ios/src/PHBadgeModel.c ios/src/PHBadges.m \
    ios/src/PHTimerModel.c "$build_dir/PHBattleNative.o" "$build_dir/PHTimer.o" -lc++ \
    -framework Foundation -framework CoreFoundation -framework UIKit -framework CoreGraphics -framework QuartzCore \
    -o "$app_dir/Frameworks/PADCheaperTest.dylib"
"$clang_path" -arch "$architecture" -isysroot "$sdk_path" -mios-simulator-version-min=16.0 \
    -std=gnu11 -fobjc-arc -fblocks -Wall -Wextra -Werror -Wno-unused-parameter \
    -Iios/src ios/tests/overlay_probe.m -framework Foundation -framework UIKit -framework CoreGraphics -o "$app_dir/OverlayProbe"

python3 - "$app_dir" <<'PY'
from pathlib import Path
import plistlib
import sys

app = Path(sys.argv[1])
info = {
    "CFBundleIdentifier": "jp.gungho.pad",
    "CFBundleName": "PADCheaper GUI Probe",
    "CFBundleExecutable": "OverlayProbe",
    "CFBundlePackageType": "APPL",
    "CFBundleShortVersionString": "23.6.0",
    "CFBundleVersion": "106878",
    "MinimumOSVersion": "16.0",
    "LSRequiresIPhoneOS": True,
    "UIDeviceFamily": [1, 2],
    "UILaunchScreen": {},
    "UISupportedInterfaceOrientations": ["UIInterfaceOrientationPortrait", "UIInterfaceOrientationLandscapeLeft", "UIInterfaceOrientationLandscapeRight"],
    "UIApplicationSceneManifest": {
        "UIApplicationSupportsMultipleScenes": False,
        "UISceneConfigurations": {"UIWindowSceneSessionRoleApplication": [{
            "UISceneConfigurationName": "Default", "UISceneDelegateClassName": "PHProbeSceneDelegate",
        }]},
    },
}
(app / "Info.plist").write_bytes(plistlib.dumps(info))
PY
codesign --force --sign - --timestamp=none "$app_dir/Frameworks/PADCheaperTest.dylib"
codesign --force --sign - --timestamp=none "$app_dir"

runtime_id="$(xcrun simctl list runtimes --json | python3 -c '
import json, sys
available = [r for r in json.load(sys.stdin)["runtimes"] if r.get("isAvailable") and r["identifier"].startswith("com.apple.CoreSimulator.SimRuntime.iOS-")]
available.sort(key=lambda r: tuple(map(int, r["version"].split("."))))
print(available[-1]["identifier"])
')"
device_type="$(xcrun simctl list --json | python3 -c '
import json, sys
inventory = json.load(sys.stdin)
phones = [t for t in inventory["devicetypes"] if t.get("productFamily") == "iPhone"]
preferred = [t for t in phones if t["name"] in ("iPhone 17", "iPhone 16", "iPhone 15")]
print((preferred or phones)[-1]["identifier"])
')"
device_id="$(run_limited 45 xcrun simctl create 'PADCheaper GUI probe' "$device_type" "$runtime_id")"
cleanup() {
    run_limited 15 xcrun simctl shutdown "$device_id" >/dev/null 2>&1 || true
    run_limited 15 xcrun simctl delete "$device_id" >/dev/null 2>&1 || true
}
trap cleanup EXIT
run_limited 45 xcrun simctl boot "$device_id"
# Fresh hosted runners can spend over two minutes migrating simulator data.
run_limited 240 xcrun simctl bootstatus "$device_id" -b
xcrun simctl status_bar "$device_id" override --time '9:41' --batteryState charged --batteryLevel 100
run_probe() {
    local probe_app="$1" bundle_id="$2" prefix="$3"
    run_limited 45 xcrun simctl install "$device_id" "$probe_app"
    local container_dir report_path phase portrait_done=0 landscape_done=0
    container_dir="$(xcrun simctl get_app_container "$device_id" "$bundle_id" data)"
    report_path="$container_dir/Documents/overlay-probe.json"
    if ! run_limited 90 xcrun simctl launch "$device_id" "$bundle_id"; then
        # Cold simulator launch can outlive the client command. Keep a running
        # probe if it already produced evidence; otherwise retry launch once.
        if [ ! -f "$report_path" ]; then
            run_limited 60 xcrun simctl launch "$device_id" "$bundle_id"
        fi
    fi
    for attempt in $(seq 1 90); do
        if [ -f "$report_path" ]; then
            phase="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["phase"])' "$report_path")"
            if [ "$phase" = portrait ] && [ "$portrait_done" = 0 ]; then
                cp "$container_dir/Documents/$prefix-portrait.png" "$build_dir/"
                portrait_done=1
            fi
            if [ "$phase" = landscape ] && [ "$landscape_done" = 0 ]; then
                cp "$container_dir/Documents/$prefix-landscape.png" "$build_dir/"
                landscape_done=1
            fi
            if [ "$phase" = done ]; then
                cp "$report_path" "$build_dir/$prefix-probe.json"
                cp "$container_dir/Library/Application Support/PADHelper/status.json" "$build_dir/$prefix-status.json"
                cp "$container_dir"/Documents/*.png "$build_dir/"
                python3 - "$report_path" <<'PY'
import json
import sys
report = json.load(open(sys.argv[1]))
if not report["success"]:
    raise SystemExit("Overlay probe failed:\n" + "\n".join(report["failures"]))
print("Simulator overlay probe passed: early load, invalid target, persisted switches, API adoption, touch passthrough")
PY
                xcrun simctl terminate "$device_id" "$bundle_id"
                return 0
            fi
        fi
        sleep 1
    done
    if [ -f "$report_path" ]; then
        cp "$report_path" "$build_dir/$prefix-probe.json"
        cat "$report_path"
    fi
    return 1
}

run_probe "$app_dir" jp.gungho.pad overlay

# PAD itself uses the legacy AppDelegate window. A second install also models
# LC's rewritten bundle ID, while still exercising the exact same runtime.
legacy_app="$build_dir/LegacyProbe.app"
cp -R "$app_dir" "$legacy_app"
python3 - "$legacy_app" <<'PY'
from pathlib import Path
import plistlib
import sys

app = Path(sys.argv[1])
info = plistlib.loads((app / "Info.plist").read_bytes())
info.pop("UIApplicationSceneManifest")
info["CFBundleIdentifier"] = "com.example.PADCheaperLegacyProbe"
info["CFBundleName"] = "PADCheaper legacy window probe"
info["PHProbeLegacy"] = True
(app / "Info.plist").write_bytes(plistlib.dumps(info))
(app / "LCAppInfo.plist").write_bytes(plistlib.dumps({
    "doUseLCBundleId": True, "LCOrignalBundleIdentifier": "jp.gungho.pad",
}))
PY
codesign --force --sign - --timestamp=none "$legacy_app"
run_probe "$legacy_app" com.example.PADCheaperLegacyProbe legacy

# 共用求路核心

此處提供不依賴 Foundation／UIKit 的 CMake 入口；直接使用現有 `ios/src` 的 C／C++ 模型及求路器，避免兩份演算法分歧。尚未搬移實體檔案，iOS 0.5.35 的來源與建置入口不變。

此核心接受正規化盤面。`PHOrbModel.h`／`PHBattleModel.h` 仍包含 iOS 原生資料布局及旗標；Android reader 必須另行核對並轉成共同語意，不能拿現有 raw decoder 讀 Android 記憶體。

`PHOrbSession.mm`、技能規則／策略、UIKit 及記憶體 adapter 仍未跨平台化。CMake 入口已在 `0.1.0-probe` 集中編譯測試中建置 Android arm64／x86_64 核心；Windows GCC 15／Release `-O2` 的 `PH_BUILD_CORE_TESTS=ON` 既有求路器回歸亦已通過。Android 即時盤面尚未接入核心。

首次 GCC `-O3 -Werror` 建置在 `EvaluateStatic` 出現陣列界限警告；GNU C++ 的 Release 核心改用既有求路驗證採用的 `-O2`，沒有關閉警告。`-O3` 仍未驗證，不能視為本次通過配置。詳見 [0.1.0-probe 驗證紀錄](../android/docs/release-0.1.0-probe.md)。後續建置及測試仍依使用者明確「編譯測試」的批次流程執行。

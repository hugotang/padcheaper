# Android 0.1.0-probe：接入診斷驗證

2026-11-06，依使用者「編譯測試」完成前三批 Android 原型的集中驗證。此版只提供遊戲內接入診斷，尚不能讀盤、轉珠、放技能或換隊。

## 交付與安裝

- 獨立交付目錄：`E:/ipa/PADCheaper-Android-0.1.0-probe`，同名 ZIP。
- 安裝檔：`PADCheaper-Android-0.1.0-probe.apk`，debug 簽署的 universal APK，內含 arm64-v8a 與 x86_64 payload；套件 `io.github.hugotang.padcheaper`。
- 指定的 `emulator-5568` 已安裝本 APK，LSPosed 已啟用模組並選取日服 PAD。更新時安裝 APK 後完整關閉並重開遊戲；不支援原生 hot reload。
- 遊戲內「PC 診斷」→「擷取接入狀態」可取得紀錄。目標為已核對的日服 PAD 23.6.0／230600；其他來源 APK 即使版號相同，也需另行核對。

包內 `verification.json` 記錄來源提交與每份證據雜湊，`source-identity.json` 保存 114 份產品／建置輸入的指紋清單，`SHA256SUMS` 可核對包內所有檔案。交付重用已通過裝機的同一 APK，不因後續文件提交另建二進位。

交付已完成：來源提交 `5c61e85f52de885f9ea43a1b6119e7af44aea5f9`，40 份檔案校驗與 41 項 ZIP 逐檔／CRC 核對通過。ZIP 580,150 bytes，SHA-256 `0bdb49e57684f59a4c89a438316e6532d58d48549dd35f0bdf450a97639f4a2e`。本段為交付後補記，包內文件保留原快照；[交付收據](../research/verification-0.1.0-probe.json) 記錄最終數值。

| 身分 | 值 |
|---|---|
| APK bytes | 1,640,219 |
| APK SHA-256 | `f6bf4e3ee48c09353d02edcffe7e9278b5e5bbf765aab6ef1bf206c05bcab909` |
| 產品來源指紋 | `72698704498a06d38d53a8eeee0b89132f03ada3ce5a6012081a53b5e01033a1` |
| x86_64 payload SHA-256 | `babef7bc8484403ee4a82001c58a4ca01d9d8cd84de8a92b9255ad2c42f08633` |
| arm64-v8a payload SHA-256 | `f0fa500643c7297f4612fe8c08123f0e16938a1b13b930a6eebee5cd583f50d7` |

## 自動與裝機驗證

| 範圍 | 最終结果 |
|---|---|
| Gradle 8.13／AGP 8.12.0／SDK 36／NDK 27 | debug APK、兩種 ABI payload 與核心建置成功；API 102 為 compileOnly，未打包框架 API 類別 |
| Java | 12 案例，零失敗／錯誤／跳過 |
| Python | 7 案例，包括同名子程序、來源不符、PID 重用、過期及擷取中重開 |
| 共用求路器 | Windows GCC 15.2.0、Release `-O2`，既有回歸執行檔 1/1 通過，斷言開啟 |
| 原生解析器 | NDK x86_64 建置，模擬器執行 exit 0；包含合成真實映射的完整讀取路徑 |
| APK | 兩種 ABI、Xposed 入口／作用域／設定、來源身分、DEX 與 v2 簽章核對通過 |
| 首次擷取 | PID 8790，世代 1，來源／安裝 APK／載入 payload 一致，零 issues |
| 返回前景 | 相同 PID／start tick，世代 1→2，callback 恢復，零 issues |
| 完整重開 | PID 9883，新 start tick，世代回到 1，零 issues |

三份紀錄約 29.984–29.996 fps，影格年齡 64–109 ms，沒有觀察到執行緒變動。x86_64 程序中的自有 payload 成功觀察 ARM64 ELF header；`hasExecutableMapping=false` 只記錄映射事實，不能推論 ARM 函式可直接呼叫。

最終證據位於包內 `verification/`、`device/` 與 `java-tests/`，原始本機路徑為 `build/android/verification`、`build/android/device` 與 `build/android/app/test-results/testDebugUnitTest`。`apk-identity-final.json` 為本 APK 身分；早期 `apk-identity.json`、六案例 Python log 及早期裝機失敗不是最終版本驗證。包內 `history/` 保存關鍵失敗，方便追查修正。

## 此次修正與限制

裝機時原先開啟 `/proc/self/mem` 後遊戲程序退出，未留下可確認根因的 crash 報告。最終版改以 `process_vm_readv` 複製已核對映射範圍的 ELF header 20 bytes，原生案例及遊戲內擷取重測均通過。每個擷取階段先保存進度，程序中斷會留下未完成狀態。

遊戲可 fork 出同名子程序，原工具因多個 PID 而拒絕。最終版核對同 UID、唯一 zygote 根程序與其後代，並在讀取前後重新確認程序身分；歧義、重啟或模組更新仍拒絕。

最初 GCC `-O3 -Werror` 在求路器 EvaluateStatic 報陣列界限警告。共用 CMake 的 GNU C++ Release 核心採既有驗證使用的 `-O2` 後通過，沒有關閉警告；`-O3` 仍未驗證。

以下尚待驗證或實作：OpenGL／Vulkan 各自實測、同程序 Activity 重建、原生 arm64 裝置、載入失敗介面、完整子程序／多人隔離，以及 Android 執行期程式段、盤面 layout、模式／特殊珠／保護欄位與操作窗口。`deviceVerified`、`runtimeLayoutVerified`、`boardReadEnabled`、`actionsEnabled` 全部為 false。共用求路器已能建置，但未連接 Android 即時盤面。

下一階段先以 Android 專用證據定位只讀盤面，再接正規化盤面求路與明確的單人轉一次。詳見 [開發紀錄](development.md)；後续建置仍依明確「編譯測試」的批次流程。

# Android 0.1.1-probe：固定程式區段擷取與離線核對

2026-11-06，依使用者明確「編譯測試」驗證第四、五批 Android 來源。本版已建置並安裝至 `emulator-5568`，自動測試、接入診斷、固定區段擷取、離線分析、IDA raw 匯入與下列裝機檢查通過。**本輪驗證完成、待封裝**，尚未宣稱交付完成。

## 本版功能與安裝狀態

遊戲內「PC 診斷」新增「擷取程式資料」→「開始擷取」，明確手動收集已核對 libpad 第一個不可寫 PT_LOAD，固定 17,295,152 bytes。Java 核對原前景／執行緒／程序與時間，native 核對 header、映射及雙次內容；成功檔案由電腦端工具匯出並離線比較。詳細操作見 [擷取說明](runtime-code-capture.md) 與 [分析說明](runtime-code-analysis.md)。

本版仍不能讀盤、轉珠、放技能或換隊。APK 是 debug 簽署的 universal APK，套件 `io.github.hugotang.padcheaper`、versionCode 2，包含 arm64-v8a 及 x86_64 payload。本次實際執行的是 x86_64 程序／payload，由 Houdini 載入 ARM64 遊戲；更新 APK 後須完整重開 PAD，不支援 native hot reload。既有 0.1.0-probe 交付資料保持不變。

| 身分 | 值 |
|---|---|
| APK bytes | 1,757,211 |
| APK SHA-256 | `359bd6d5a1ac7aee8638ee8f40e13b8a872acc92b6b03240dac131b2a4f74834` |
| 產品來源指紋 | `08a5f75987f833921fac55b14b83cb88c683648d628badcb3ca01d9cb7151dcc` |
| x86_64 payload SHA-256 | `fc7fe8ebb6697764047175d8b6d652e091a94f0993b0aed260ba1c0cb1c28cc3` |
| arm64-v8a payload SHA-256 | `1f44fa99aec3509e28a6f71eac92de01376de1dde62fef8a8fa58754703b1809` |

## 已完成的驗證

| 範圍 | 結果 |
|---|---|
| Gradle 8.13／AGP 8.12.0／SDK 36／NDK 27 | debug APK、兩種 ABI payload 與核心建置成功 |
| Java | 13 個案例通過 |
| Python | 27 個案例通過，涵蓋接入、固定區段匯出、封存時鐘、離線分析、內容與路徑拒絕、原子發布 |
| 共用求路器 | Windows GCC 15.2.0／Release `-O2`，回歸執行檔 1/1 通過，斷言開啟 |
| 原生測試 | NDK x86_64 的 library inventory 與 runtime code capture 兩個執行檔在指定模擬器皆 exit 0，斷言開啟 |
| APK | v2 簽章通過；來源與已安裝 APK／實際載入 payload 相符 |
| 遊戲接入診斷 | `probe-entry-02.json` 為 `entry-evidence-collected`、零 issues，約 29.992 fps |
| 固定區段擷取 | `capture-01/result/report.json` 為 `runtime-code-collected`、零 issues，雙次內容與映射穩定 |
| 離線分析 | `offline-code-comparison-ready`；使用封存的最終裝置時鐘重驗，沒有重新宣稱當前程序已核對 |
| IDA raw 匯入 | `raw-import-verified`；AArch64／64-bit／little-endian，完整 byte hash、精確範圍與範圍外未映射均吻合 |
| 開始前取消 | 按「取消」前後檔案清單與原報告 hash 一致，沒有啟動擷取 |
| 返回前景／完整重開 | 同 PID 3896 的世代 1→2；重開後新 PID 5534／start tick 298109，世代回到 1，接入擷取均零 issues |
| 重複擷取與容量 | 三份完整 image 成功匯出且 hash 相同；第四次顯示容量已滿，原檔不變、無新暫存檔、程序仍正常 |
| 拒絕結果匯出 | 嚴格一秒時效、重開後尚無新擷取與容量拒絕均只匯出報告、不接受 binary |

測試與接入證據保存於 `build/android/verification-0.1.1/`。早期 `probe-entry.json` 因未取得可解析 JSON 為 `incomplete`，保留為歷史，不能當作成功；成功紀錄為 `probe-entry-02.json`。

## 實際擷取與差異

PID 3896、start tick 239598；擷取前後維持同一 boot／來源／APK／payload 與前景世代 1、執行緒 74。Java 記錄的完整請求為 135 ms，結束影格年齡 3 ms。電腦端匯出後再次核對同一程序及 APK，保存 `finalDeviceElapsedMillis=2650560` 與 `maxAgeMillisUsed=300000`。

完整重開後 PID 5534／start tick 298109 的第二、三次擷取分別為 132／147 ms，均通過匯出且與第一份完整 hash 相同。這些是指定模擬器、此次遊戲狀態的結果，不是跨設備耗時保證。

| 內容 | 值 |
|---|---|
| 固定範圍 | file offset／ELF-relative VA 0，end-exclusive `0x107e730` |
| 擷取 bytes | 17,295,152 |
| 擷取 SHA-256 | `43d41b01f7252f269bb3f9728688c12a6c0889a3bb51b71d09f0a81a09248109` |
| 原版對應段 SHA-256 | `70aa1dc509bfd109da293d898b37f034f034a8eab4c56e62434a31cf15412170` |
| 改變 bytes／頁數 | 12,457,407／3,443；每頁 4096 bytes，四段連續頁面候選範圍 |

原始擷取位於 `build/android/runtime-code-0.1.1/capture-01/result/`，離線結果位於 `build/android/runtime-analysis-0.1.1/capture-01/result/`。分析保存擷取的逐位元組複本、原 collector 報告、原版區段 metadata、hash、差異範圍及 raw IDA 匯入規格。未擷取的 RW／BSS／heap 保持未知，沒有補零或從原版填補。

IDA 強制 Binary/raw 匯入後，完整載入 hash 與擷取一致。loader 初始自動建立 CODE 與 22,526 個函式；這些未核對的分析已移除，停用自動分析並清除佇列，最終只有 `[0,0x107e730)` 唯讀 DATA、全部 unknown、零函式，範圍外未映射且沒有 RW／BSS。報告 `build/android/verification-0.1.1/ida-import.json` 記錄初始與最終狀態；資料庫已保存並關閉於 `build/android/ida-0.1.1/capture-01/runtime-code.bin.i64`。此結果只驗證匯入，不宣稱已恢復函式或完成解碼。

## 尚待完成與限制

本輪已完成上述檢查，交付目錄、來源提交及成品校驗待封裝後另記。`cancel-verification.json` 僅驗證開始前取消；`quota-verification.json` 驗證容量拒絕及失敗紀錄保存，不代表實際磁碟 I/O 故障注入。擷取途中切到背景、程序內 Activity 重建及實際保存 I/O 故障均尚未裝機實測。

指定模擬器上三份完整程式檔仍保留，共 51,885,456 bytes，容量已滿；三份均已匯出本機研究目錄，未清理裝置檔案。再次擷取前須先核對本機證據，再清理已匯出的裝置舊檔；原型不會自動刪除它們。

OpenGL／Vulkan 分別實測、程序內 Activity 重建、原生 arm64 裝置、完整載入失敗／多人隔離仍待驗證。GCC `-O3` 的既有陣列界限警告未在本輪另行解決，本輪通過核心配置為 `-O2`。

頁面差異與 raw 匯入均不證明解碼完成或盤面布局正確。`deviceVerified`、`runtimeLayoutVerified`、`boardReadEnabled`、`actionsEnabled`、`unpackedCodeVerified` 仍為 false；下一步需以 Android 專用證據核對 getter／writer、模式與特殊珠，逐格對照畫面後才接入求路與明確單人操作。iOS 產品來源、手動 CI 與既有交付不變。

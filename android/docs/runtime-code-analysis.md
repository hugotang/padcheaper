# Android 程式擷取的離線核對與 IDA 匯入

2026-11-06，`0.1.1-probe` 已完成新建置、自動測試、真遊戲固定程式區段擷取及交付，分析工具亦已成功核對該組證據並產生差異資料。IDA raw 匯入的 AArch64／64-bit／little-endian、完整 byte hash 與精確範圍核對通過；盤面 reader 仍未接通。詳見 [版本驗證紀錄](release-0.1.1-probe.md) 與 [交付收據](../research/verification-0.1.1-probe.json)。

## 輸入與來源核對

先依 [固定程式區段擷取](runtime-code-capture.md) 產生一組 `result/report.json` 和 UUID `.bin`。本工具完全離線，不會連接裝置、執行程式或呼叫遊戲。

```powershell
python android/tools/analyze_runtime_code.py --capture build/android/runtime-code/capture-01/result --apk build/android/app/outputs/apk/debug/app-debug.apk --original-library build/android-research/native/libpad.so --out build/android/runtime-analysis/capture-01
```

`--apk` 必須是該次擷取實際使用的模組 APK，不必是目前工作目錄的新來源。`--original-library` 必須是已核對的原版 libpad（30,735,496 bytes，SHA-256 `fec9fbc55d3a9b7945817420475a8c03fa608ecc4605535e373ca2d0fc738a8a`）。輸出放在擷取目錄之外的新工作目錄，既有輸出不覆寫。

分析前會核對：

- 封套必須是成功且無 issues；其 expectedBuild 與提供的 APK 全部相同，安裝 APK hash、來源及實際載入 payload 相符。
- 封套與內層報告的程序／開機／時間、前景世代／執行緒、固定讀取範圍及停用功能旗標成立。報告与 binary 路徑不能逃出 `result/`。
- collector 新增保存最後讀到的 `finalDeviceElapsedMillis` 及當次 `maxAgeMillisUsed`，離線重驗使用這兩個實際保存值，不使用分析當下的電腦時間或推算當時時鐘。缺少欄位的舊封套明確拒絕，需用新版 collector 重新匯出仍有效的擷取。
- binary 檔名／大小／完整 hash、精確 512-byte header，以及原版檔案的大小／hash 全部一致。讀取後重核對報告與 APK，輸出使用已持有的位元組快照。

這是已保存證據的內容一致性核對，不是數位簽署、來源真偽保證或重新查詢目前裝置；`currentProcessRechecked=false`。

## 產生的資料

成功後在新目錄的 `result/` 一次發布下列六個檔案；輸入錯誤時不產生分析，寫入中斷的 `.pending-*` 不屬於完整結果。

| 檔案 | 用途 |
|---|---|
| `runtime-code.bin` | 與有效擷取逐位元組相同的複本，沒有重建 ELF、修補或補零 |
| `collector-report.json` | 原封不動的 collector 報告，保留 hash 對應 |
| `analysis.json` | 當時時效重驗、hash、改變位元組／頁數、連續改變頁面範圍及原版區段標籤 |
| `ida-load.json` | AArch64 raw 匯入範圍與來源；規格本身不是 IDA 執行結果，需另附實際匯入核對 |
| `README.md` | 匯入與證據限制 |
| `SHA256SUMS` | 其餘五份檔案的內容 hash |

差異以 4096 bytes 為一頁，末頁依實際擷取末端截短。`changedPageSpans` 的 `endExclusive` 不包含末端；範圍是搜尋候選，不是函式邊界或每個位元組都改變的區間。整段及每個完整包含的原版 allocated section 都附 hash。

原版 section table 位於檔案 offset 30,733,560，超出擷取範圍，因此只能從已核對的原版讀取 metadata。每個區段都標明 `metadataSource=exact-original-ELF-section-table`；只有非 NOBITS、檔案 offset 與虛擬位址相同且整段位於擷取範圍內，才標 `fullyCaptured=true`。`.data.rel.ro`、`.got`、`.data` 和 `.bss` 等仍未擷取，不計算假的 runtime hash。

## IDA 匯入邊界

必須**強制 Binary/raw loader**；只改 `.bin` 副檔名不能阻止 ELF magic 觸發 ELF loader。選 AArch64、64-bit、little-endian，file offset＝0、load VA＝0、長度 `0x107e730`，研究起始狀態應為 data／unknown。

本次本地 IDA 的 raw 匯入已核對成功，但 loader 初始自動建立 CODE 區段與 22,526 個函式，不能視為已驗證指令。驗證時已停用自動分析、移除推測函式／指令並清除佇列；最終唯一區段為唯讀 DATA、全部位元組 unknown、零函式，完整 hash 不變且範圍外未映射，沒有建立 RW／BSS。報告為 `build/android/verification-0.1.1/ida-import.json`，資料庫已保存並關閉於 `build/android/ida-0.1.1/capture-01/runtime-code.bin.i64`。後續匯入須再次檢查實際 loader 狀態，不能只依 `ida-load.json` 規格假定已保留 unknown。

匯入後先核對範圍与原始 hash，再逐步標記有獨立證據的指令／函式。地址是 ELF-relative VA，不是當次程序中的絕對位址。header、唯讀資料與 unwind tables 都在擷取範圍，不能把整個區段標成已知指令。

原版 `.text` 範圍 `0x552960..0x104aedc` 僅是原版 metadata；不能因此假定其中全部已是正確 runtime 指令。對範圍外的 ADRP／全域引用，保留未知地址，不從原版檔案填補可寫段、不建立有內容的 BSS，也不把缺失值當零。

本次實際分析結果為 `offline-code-comparison-ready`：17,295,152 bytes 中有 12,457,407 bytes 與原版對應段不同，涉及 3,443 個 4096-byte 頁面、四段連續頁面候選範圍。擷取 hash 為 `43d41b01f7252f269bb3f9728688c12a6c0889a3bb51b71d09f0a81a09248109`；原版對應段 hash 為 `70aa1dc509bfd109da293d898b37f034f034a8eab4c56e62434a31cf15412170`。完整結果位於 `build/android/runtime-analysis-0.1.1/capture-01/result/analysis.json`。後續重開後的第二、三份有效擷取具有相同完整 hash；這不代表測遍所有遊戲狀態。

差異不等於解碼成功，相同也不等於解碼失敗。`unpackedCodeVerified`、`runtimeLayoutVerified`、`deviceVerified`、`boardReadEnabled`、`actionsEnabled` 全部保持 false。下一步以 IDA 核對盤面 getter／writer 及原生布局，然後逐格對照畫面。

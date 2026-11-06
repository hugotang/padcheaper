# Android 程式擷取的離線核對與 IDA 匯入

2026-11-06，第五批來源。分析工具與案例已寫，**尚未執行工具、測試或新建置**；第四批手動程式擷取也仍待編譯裝機。本頁是取得有效擷取後的流程，不代表目前已有 runtime image 或盤面 reader。

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
| `ida-load.json` | AArch64 raw 匯入範圍與來源；是待驗證的匯入規格，不是執行結果 |
| `README.md` | 匯入與證據限制 |
| `SHA256SUMS` | 其餘五份檔案的內容 hash |

差異以 4096 bytes 為一頁，末頁依實際擷取末端截短。`changedPageSpans` 的 `endExclusive` 不包含末端；範圍是搜尋候選，不是函式邊界或每個位元組都改變的區間。整段及每個完整包含的原版 allocated section 都附 hash。

原版 section table 位於檔案 offset 30,733,560，超出擷取範圍，因此只能從已核對的原版讀取 metadata。每個區段都標明 `metadataSource=exact-original-ELF-section-table`；只有非 NOBITS、檔案 offset 與虛擬位址相同且整段位於擷取範圍內，才標 `fullyCaptured=true`。`.data.rel.ro`、`.got`、`.data` 和 `.bss` 等仍未擷取，不計算假的 runtime hash。

## IDA 匯入邊界

必須**強制 Binary/raw loader**；只改 `.bin` 副檔名不能阻止 ELF magic 觸發 ELF loader。選 AArch64、64-bit、little-endian，file offset＝0、load VA＝0、長度 `0x107e730`，初始區段為 data／unknown。此規格在本地 IDA 工具的實際 64-bit 匯入行為，保留到下次授權測試確認。

匯入後先核對範圍与原始 hash，再逐步標記有獨立證據的指令／函式。地址是 ELF-relative VA，不是當次程序中的絕對位址。header、唯讀資料與 unwind tables 都在擷取範圍，不能把整個區段標成已知指令。

原版 `.text` 範圍 `0x552960..0x104aedc` 僅是原版 metadata；不能因此假定其中全部已是正確 runtime 指令。對範圍外的 ADRP／全域引用，保留未知地址，不從原版檔案填補可寫段、不建立有內容的 BSS，也不把缺失值當零。

差異不等於解碼成功，相同也不等於解碼失敗。`unpackedCodeVerified`、`runtimeLayoutVerified`、`deviceVerified`、`boardReadEnabled`、`actionsEnabled` 全部保持 false。下一步需要實際擷取、IDA 核對盤面 getter／writer 及原生布局，然後逐格對照畫面。

# Android 盤面研究：有限程式區段擷取

2026-11-06，接在已交付 0.1.0-probe 的入口驗證之後。本批只有來源與靜態檢查，**尚未建置、跑測試或安裝**；已交付 APK 沒有本頁的新按鈕。Android 盤面仍未定位。

## 為何先取得程式證據

原有接入擷取只核對 ELF header 20 bytes，足以辨認 ARM64 映像，不能證明盤面欄位或程式已解碼。原版 libpad 的 IDA 資料庫存在 25,986 個辨識函式，但未找到參考專案所列盤面方法名稱；函式數與反組譯輸出本身不是可靠 layout 證据。研究摘要见 [固定區段與來源](../research/runtime-code-20261106.json)。

參考 `E:/PADhelper` 的 `getBoardWidth`、`setBurBlockFlag`、`_checkFlood`、`calcBlocks`／`getNearestBlock` 可用於後續語意核對，不採用其位址作為本版 reader。其基址算法要求 executable mapping，與目前 Houdini `hasExecutableMapping=false` 不符；`sBLOCK+0x12` 的「類型」解讀亦與 iOS motion／settled 語意不同，需查 Android 實際讀寫者。

## 下次編譯後的操作

1. 收到明確「編譯測試」後建置、安裝新來源，完整重開指定遊戲。
2. 在「PC 診斷」選「擷取程式資料」→「開始擷取」，保持遊戲前景。原有「擷取接入狀態」仍只做有限 header 診斷。
3. 使用剛安裝的確切 APK，於工作目錄執行：

```powershell
python android/tools/collect_runtime_code.py --adb C:/Users/tangk/AppData/Local/Android/Sdk/platform-tools/adb.exe --serial emulator-5568 --apk build/android/app/outputs/apk/debug/app-debug.apk --out build/android/runtime-code/capture-01
```

輸出目錄每次必須是新名稱，且放在工作目錄，不修改 `E:/ipa` 已交付版本。成功時 `capture-01/result/` 同時出現 `report.json` 與 UUID 名稱的 `.bin`；失敗只发布附 issues 的報告，沒有接受的 binary。寫入中斷的 `.pending-*` 不是成功結果。

工具只讀模組產生的報告與 binary，依目前程序 UID 選擇 Android user，核對原版目標、已安裝 APK、來源／payload、PID／start tick／boot、影格、時效、長度與 hash。binary 傳輸後再次比對報告、程序、APK 與時間；不安裝、不重啟、不點按或讀任意記憶體位址。

## 固定讀取範圍

此目標原版 ELF64／ARM64 有八個 program headers。只讀第一個 PT_LOAD（index 1），file offset／vaddr 均為 0，filesz／memsz 均為 `0x107e730`，即 **17,295,152 bytes（約 16.5 MiB）**。它包含 ELF metadata、唯讀資料與程式碼，不是完整 libpad 檔案；後續可寫 PT_LOAD、BSS、heap、遊戲存檔及帳號資料均不在範圍內。

- 先核對精確 APK／原生庫雜湊與實際 payload CPU，再以原版前 512 bytes（ELF header 與 program headers）逐位元組核對。版本描述不同須重新研究，不能放寬範圍來讀取。
- 映射必須是同一路徑／device／inode 的 libpad，唯一 offset-zero 基址；整段覆蓋連續、可讀、不可寫，檔案 offset 與相對位址一致。Houdini 的 r--p 可接受，不以 executable 權限猜測基址。
- 透過 `process_vm_readv` 分塊最多 64 KiB 複製，只寫入呼叫端新建的空白、同 UID、regular、單連結 O_RDWR 暫存檔。沒有遊戲記憶體寫入、直接 ARM 函式呼叫、權限修改或 `/proc/self/mem` fallback。
- native 十秒限制；兩次完整讀取逐塊比對，兩次之間及結尾重新核對完整 libpad 映射，結尾重讀 header。這能排除觀察到的改變，不代表停住程序的原子快照，亦不保證已解碼。
- deadline 在每個有限區塊與映射讀取之間檢查，不能強行中斷正在阻塞的系統呼叫；總耗時與遊戲前景狀態仍由 Java 結束檢查決定能否接受。
- Java 要求原本前景世代與繪製執行緒、近期至少兩個 callback，完整請求最多三十秒。結束時用同一份不可變影格快照判斷與寫入報告；背景、重建、執行緒改變、讀取或保存失敗皆不接受結果。
- 先保存 `code-<pid>.json` 的 in-progress checkpoint，再擷取。完成 binary hash 及發布後，明確 sync 並回讀相同 JSON，成功才保留 binary；失敗清理本次檔案。獨立報告不覆蓋 `probe-<pid>.json`。
- 同時最多保留三份完整影像的容量，計入中斷暫存檔。達上限時明示需先匯出／清理舊程式擷取，模組不會自動刪除既有研究證據。

## 解讀與待驗證

`runtime-code-collected` 僅代表來源與固定區段擷取契約成立。`unpackedCodeVerified`／`runtimeLayoutVerified`／`boardReadEnabled`／`actionsEnabled`／`deviceVerified` 仍為 false；執行期程式、盤面布局與 native 操作窗口都必須另行核對。

新增 C++ 案例涵蓋 r--p 映射、缺口／重疊、不同 offset／路徑、可寫區段、錯誤 header、描述符限制、合成映射的雙次完整複製；Java 案例涵蓋原前景／執行緒與過期影格；Python 案例涵蓋來源、時效、檔名、內容、程序／APK／報告在传輸中改變與成對發布。**案例已寫，尚未執行**。

下次裝機須檢查擷取後程序仍存活、前背景／中斷拒絕、三份容量上限、失敗保存與匯出；若 header 或映射條件不符，保留原因再研究，不能默認調整成可写區段。取得證據後以 IDA 核對 singleton、impl、索引表／物件池、尺寸／方向、色號／motion／旗標與單人 phase，再逐格對照畫面，最後才接正規化 `PHOrbBoard`。

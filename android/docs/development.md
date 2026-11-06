# Android 開發與待驗證

## 2026-11-06：0.1.0-probe 集中編譯測試完成

- 延續使用者明確「編譯測試」的工作。上個聊天在整理交付時中斷；本次重新核對產品來源、最後 APK 與三份成功擷取紀錄完全一致，沒有變更已測產品來源或重跑已通過的套件。以下三批「尚未執行」是當時的歷史，最終結果以本節及 [版本驗證紀錄](release-0.1.0-probe.md) 為準。
- Gradle 8.13／AGP 8.12.0／SDK 36／NDK 27 完成 debug APK，包含 arm64-v8a、x86_64 原生 payload 與共用核心建置。API 102 維持 compileOnly，完整建置及裝機通過；沒有打包框架 API 類別，APK v2 簽章通過。
- Java 12 個案例、Python 7 個案例（含同名子程序辨識）通過。Windows GCC 15.2.0 的共用求路器回歸 1/1 通過，Android x86_64 原生解析器在 emulator-5568 執行成功，包含合成檔案映射的完整 header 讀取路徑。最初 GCC `-O3 -Werror` 在 EvaluateStatic 報陣列界限警告；GNU C++ Release 核心改用既有驗證的 `-O2` 後通過，沒有關閉警告，`-O3` 不列為已驗證。
- 裝機發現原生診斷開啟 `/proc/self/mem` 時程序退出，沒有留下可歸因的 crash 報告。改用核對映射範圍後的 `process_vm_readv`，只複製 ELF header 20 bytes，通過原生案例與完整裝機重測。擷取各階段先保存進度，程序中斷不再留下看似成功的舊結果。
- `pidof` 可回傳同名 fork 子程序。擷取工具改為核對同 UID、唯一由 zygote 啟動的根程序與完整後代關係，不依 PID 順序挑選；保留前後 PID／start tick／boot／ABI 身分核對，歧義或重開則拒絕。
- 最終 APK 已安裝並在現有 LSPosed 啟用日服 PAD。首次擷取、返回前景、完整重開三份結果均為 `entry-evidence-collected`、零 issues；前背景同一程序的世代由 1→2，重開後 PID／start tick 改變且世代回到 1。約 29.984–29.996 fps；x86_64 payload 成功觀察 ARM64 ELF header。這不是 native 操作窗口或讀盤驗證。
- APK SHA-256 `f6bf4e3ee48c09353d02edcffe7e9278b5e5bbf765aab6ef1bf206c05bcab909`，1,640,219 bytes；產品來源指紋 `72698704498a06d38d53a8eeee0b89132f03ada3ce5a6012081a53b5e01033a1`。早期 `apk-identity.json` 與 6 案例 Python log 保留為歷史，最終證據另存 `apk-identity-final.json` 及 `python-tests-final.log`。
- `deviceVerified`／`runtimeLayoutVerified`／`boardReadEnabled`／`actionsEnabled` 全部仍為 false。OpenGL／Vulkan 分別實測、程序內 Activity 重建、原生 arm64 裝置、載入失敗介面及多人程序隔離仍待驗證；沒有取得 Android 執行期程式段或盤面 layout。iOS 來源／CI／既有交付不變。

## 2026-11-06：第三批，裝機身分與只讀擷取

- 使用者要求「繼續」。新增建置時來源指紋與 APK 內 padcheaper-build.json；BuildConfig 使用相同識別。只納入產品／建置來源與保守的共用 C 系列相依，排除 log、遊戲二進位、文件及本機設定，統一換行以降低跨平台誤差。
- 紀錄升為 schema 3，新增 process start tick、boot ID、elapsed time 及實際載入 payload SHA-256。NativeLoader 回傳實際載入結果；無法讀取程序身分時明示未知並保留其他診斷，不宣稱可接受舊檔。
- 新增 [只讀擷取工具](probe-collection.md)：指定本機 APK／模擬器，核對已安裝 APK 雜湊、主程序前後身分、紀錄來源／時間／架構／影格及功能旗標，保留失敗與目前 PID 的有限入口 log。工具不安裝、重啟、寫裝置或變更 root 設定；本機輸出使用新檔不覆寫。
- 補寫 Java proc stat 解析與 Python 接入契約／APK 架構／擷取中程序重開等案例，**尚未執行**。Java 主程式／案例完成 parse＋type analysis；Python AST、Gradle Groovy 語法、JSON／XML／文件連結與差異空白檢查完成。未執行 Gradle 配置或 task，來源指紋產生與 assets 打包仍待正式建置。
- 本輪只讀查詢確認裝置連線、PAD 套件仍在，主程序未執行；沒有啟動遊戲。**三批均未編譯／跑測試／安裝，Android 盤面 layout 仍未驗證。** 下一步是明確「編譯測試」後完成集中建置、裝機診斷與執行期證據取得。

## 2026-11-06：第二批，逐幀分派與失敗診斷

- 使用者要求「繼續」。完成四份 DEX 的 frame caller 核對：OpenGL／Vulkan 共用 `h12.onDrawFrame(GL10)`，Vulkan 由 `l22.a()` 進入；方法簽名、code item 位置／長度／指令雜湊另存研究 JSON，未提交反組譯全文。詳見 [逐幀分派](frame-dispatch.md)。
- 入口新增 caller 簽名檢查，安裝 hook 後只對已核對的兩個 Java caller deoptimize。失敗解除本次 observers 並保留錯誤；未直接 hook 或呼叫 ARM native 函式，也未對 framework 全域 deoptimize。ART 覆蓋率與效能仍待裝置驗證。
- 前景世代新增獨立計數、影格新鮮度與平均頻率；沒有 callback、逾兩秒未更新、執行緒變更與背景會分別顯示。新增過期／新世代／執行緒變化案例，尚未執行。
- 將有限擷取移到 ProbeCapture，schema 2 同時保存成功／失敗階段與當次影格證據。修正前次成功紀錄可能掩蓋新失敗的問題；保存本身失敗明示舊檔可能殘留。執行緒啟動失敗解除擷取中狀態。
- NativeLoader 核對解壓後的 payload ELF 架構，與程序 ABI 不符即拒絕；補上空／截斷／未知 CPU／位元組序案例。
- Java 主程式與測試案例完成 parse／type analysis；JSON 重複鍵、DEX 指令雜湊、文件連結及差異檢查完成。沒有產生產品 class 或執行測試案例，本批未修改 C++ 來源。
- **未編譯／測試／安裝／變更 root 設定。** 只讀查詢時模擬器沒有 PAD 主程序，未重啟遊戲或取得 native 執行期 dump。盤面 layout／技能／輸入仍未接通，與第一批合併等待下次明確「編譯測試」。

## 2026-11-06：Root 接入研究與第一批原型

- 使用者授權開始 Android 版，指定 emulator-5568／KernelSU。選擇 LSPosed 作入口，沿用已安裝 Zygisk Next，不另建直接 Zygisk 模組。詳見 [接入方案](root-entry.md)。
- 已讀取模擬器 ABI／Android／root 模組／PAD 版本，取得 base 與 arm64 split APK 作本機分析。框架 DEX 確認 API 102；遊戲 DEX 確認 OnDrawFrame Java wrapper。原版 libpad 有加殼，未從它推導有效遊戲函式；嘗試只讀映射程式段時原程序已離開，未取得執行期 dump。沒有啟動、停止或操作遊戲。
- 新增 android Gradle 模組、arm64/x86_64 JNI payload、精確 APK／原生庫 hash gate、五個 Java frame/lifecycle observers、Activity 內診斷按鈕及有限手動擷取。子程序 PID 檢查、Activity 清理、前背景執行緒證據與原生 hot reload 拒絕分開處理。
- 原始碼審查補上 Activity／世代歸屬：旧畫面的延遲 pause／destroy 與 frame 不影響新畫面，彈窗在背景清理，擷取工作不持有 Activity 強參考。UI listener 另外核對 PID；observer 錯誤捕捉與原方法呼叫分開。
- 新增 core CMake 入口，直接使用同一份既有 C／C++ 求路來源；iOS 原始碼／建置未修改。Android 目前不讀 live board、不執行求路或任何戰鬥操作。
- 補寫 Java 版本／程序／ELF 架構／生命週期案例及 C++ maps／邊界案例，**尚未執行**。共用求路回歸可由 CMake 明確開啟，沒有新增自動工作流程。
- 完成靜態檢查：Java 主程式與 JUnit 案例使用 SDK 36／實際 API 102 jar 做 parse＋type analysis，未生成產品 class；新增 C++ runtime 與案例以 NDK 27 對 arm64／x86_64 做 `-fsyntax-only -Wall -Wextra -Werror`。另核對 JSON 重複鍵、XML、文件連結、shell 語法、wrapper 雜湊及差異空白。這些結果不代表測試案例通過或 native／GUI 已可運作。
- 核對 API 102 AAR 的 minCompileSdk=37，及官方 AGP 8.12.0 對 runtime classpath 執行 metadata 檢查的來源；保留 compileOnly＋SDK 36，不修改 AAR。尚未執行 Gradle 依賴解析、CMake 配置或產品建置，這部分相容性列入下次驗證。
- **尚未建置 APK／so／核心 archive，未跑測試套件、未安裝模組、未修改 root 設定或作用域。** 0.5.35 的 iOS CI 不驗證這批 Android 來源。下次明確「編譯測試」後按 android/README.md 執行。

## 下批接入工作

1. 補驗 OpenGL／Vulkan 分別覆蓋、程序內 Activity 重建、失敗介面、完整程序隔離及原生 arm64 裝置；雙 ABI 包裝、x86_64 載入、前背景與完整重開已通過本次接入驗證。
2. x86_64 payload 的有限 maps/header 讀取與約 30 fps callback 已取得證據；下一步需辨認實際渲染路徑與 native 操作窗口。不能靠 Java thread ID 的穩定性推論遊戲呼叫安全。
3. 取得本版 Android 的執行期程式證據，以 IDA 核對只讀盤面、尺寸、phase、單人模式、protected 欄位與特殊珠。另建 Android layout／fingerprint，不移用 iOS 或旧探針偏移。
4. 正規化盤面接共用求路器，再逐步驗證明確的單人轉一次、取消／放手；Auto／技能／換隊留在後續批次，8 人最後處理。

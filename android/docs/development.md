# Android 開發與待驗證

## 2026-11-06：共用形狀補搜優化，本機驗證完成

- 使用者繼續授權本機優化與編譯模擬，不打包、不啟動 CI。共用 `PHOrbSolver.cpp` 在無轉轉、非撈珠、非活動的形狀 recovery／saved frontier 改用不同引導，保留主搜尋尺度比較、既有 pass／beam／時間／步數額度與安全判定。詳見 [形狀補搜修正與驗證](../../ios/docs/solver-shape-recovery-2026-11-06.md)。重新評分有額外成本，不能宣稱所有情況性能不變。
- 原 828 盤的 48／128／120 壓力設定由 821／828 改善為 828／828，七個漏解全解、零 deadline；這不是產品 Fast。另一批在查詢 solver 前生成的新 360 盤為 359／360，仍有一個同色心 T＋十字未解且 deadline，另有一個達標但 deadline。Thorough 48／512／800 共 1,188／1,188 達標且零 deadline；獨立 oracle、最終 Windows model／solver 完整回歸與內部引導分數測試，以及 core CMake／CTest 2／2 通過。七個漏解已納入固定回歸，窄 beam 另重跑三輪 21／21 達標、零 deadline。構造資料不能當實戰成功率。
- Android core 引用這批已修改的共用來源，但本輪沒有重建 APK／Android ABI 核心或操作裝置；Windows 結果不驗證 Android 接入、ABI 或 iOS Session。手機／多人同步仍待驗證，既有 0.1.1-probe 和 `E:/ipa` 交付不變。以下「產品來源未改」及「尚未建置」保留為當時歷史，最新共用 C／C++ 本機結果以本節為準。

## 2026-11-06：共用 solver 本機形狀模擬完成

- 使用者授權本機編譯模擬，不打包、不啟動 CI。`084f2c7` Windows 共用 C/C++ model／solver 回歸通過，五色原盤與兩變體達到 5／5。828 個已知解形狀盤在 48／512／800 全過；窄 beam 有 7 個未逾時漏解，加時間無效，詳見 [形狀本機模擬](../../ios/docs/solver-shapes-2026-11-06.md)。結果是構造資料覆蓋，不能當實戰成功率。
- 本輪只新增研究工具、匿名盤面及分析證據，沒有改產品求路器。沒有重建 Android APK／ABI 核心，也未操作裝置；Windows 結果不驗證 Android ABI、即時接入或 iOS Session。既有 0.1.1-probe 與 `E:/ipa` 交付不變。下面「尚未建置」是上批歷史，共用 C/C++ 本機驗證以上述為準，其餘待驗證保留。

## 2026-11-06：共用五色 solver 新來源，尚未建置

- 使用者回到 solver 並要求優化 iOS 五色失敗 log。共用 `ios/src/PHOrbSolver.cpp` 新增無轉轉、非撈珠、非活動盤面的普通多色目標補搜、距離提示及缺色／布局多樣性，明確要求 `!board.spinnerMask && fishCell < 0`，所有轉轉都排除。iOS Session 另調整逾時重試與逐次統計；詳見 [五色求路來源紀錄](../../ios/docs/solver-colors-2026-11-06.md)。主搜尋排序／NearColor 不變，新增 ColorGuidance 才依首消限定選取已達色罩。一次 recovery／兩份 frontier 使普通無荊棘最多四輪，既有荊棘 no-contact 搜尋可能為第五輪；距離表、時鐘與尾部排序有成本，沒有宣稱性能不變。
- 本輪只有來源修改、案例、審查與語法檢查，**尚未建置產品、執行測試套件、派發 CI 或打包**。GCC C++17 `-Wall -Wextra -Werror -fsyntax-only` 對共用 Solver.cpp、完整 solver 測試來源及新重試 policy header 均 exit 0，沒有產生產品二進位或執行案例；Objective-C++ Session 尚待 macOS 編譯驗證。新增匿名三變體與獨立 oracle 是待跑驗收，不能當作已找到五色；下次明確「編譯測試」須重新執行跨平台共用 solver／效能與 iOS Session 回歸。
- Android core 直接引用該共用來源，因此下一次 APK／核心建置的來源指紋與待驗證範圍會改變；Android 即時盤面仍未接入 solver。已交付 0.1.1-probe 的 APK、驗證紀錄與 `E:/ipa` 交付檔保持不變，其通過結果不涵蓋這批新來源。

## 2026-11-06：0.1.1-probe 編譯測試及交付完成

- 使用者明確要求「編譯測試」，本輪合併第四批固定程式區段擷取與第五批離線分析。版本升至 0.1.1-probe／versionCode 2，詳見 [版本驗證紀錄](release-0.1.1-probe.md)。以下兩批的未建置描述保留為當時歷史；本節區分已通過與仍待完成項目。
- Gradle 8.13／AGP 8.12.0／SDK 36／NDK 27 的雙 ABI debug APK 建置成功；Java 13 個案例、Python 27 個案例、模擬器 x86_64 library inventory／runtime code capture 兩個原生執行檔，以及 Windows GCC 15.2.0／Release `-O2` 共用求路器回歸 1/1 全部通過。原生與核心執行檔斷言開啟，APK v2 簽章通過，證據在 `build/android/verification-0.1.1/`。
- APK 1,757,211 bytes，SHA-256 `359bd6d5a1ac7aee8638ee8f40e13b8a872acc92b6b03240dac131b2a4f74834`；產品來源指紋 `08a5f75987f833921fac55b14b83cb88c683648d628badcb3ca01d9cb7151dcc`。已安裝到 emulator-5568，成功接入紀錄 `probe-entry-02.json` 與此 APK／x86_64 payload 一致、零 issues，約 29.992 fps；早期 `probe-entry.json` 的 incomplete 不是成功驗證。
- 真遊戲固定區段擷取及匯出成功，`runtime-code-collected`、零 issues：17,295,152 bytes，SHA-256 `43d41b01f7252f269bb3f9728688c12a6c0889a3bb51b71d09f0a81a09248109`。PID 3896／start tick 239598，雙次讀取及映射穩定，完整 Java 請求 135 ms，匯出後程序／APK 重新核對一致。完整證據在 `build/android/runtime-code-0.1.1/capture-01/result/`。
- 離線分析成功重驗封存裝置時鐘與實際擷取，保持原始位元組；與原版對應區段相比，12,457,407 bytes、3,443 個頁面有變化，形成四段頁面候選範圍。結果在 `build/android/runtime-analysis-0.1.1/capture-01/result/`；差異不是解碼或盤面驗證。
- IDA raw 匯入核對通過，AArch64／64-bit／little-endian、完整 byte hash 與精確 `[0,0x107e730)` 一致。初始自動 CODE／22,526 函式未作為證據，已移除並停用自動分析，保存為唯一唯讀 DATA／全部 unknown／零函式，範圍外未映射、無 RW／BSS。初始／最終狀態見 `build/android/verification-0.1.1/ida-import.json`，資料庫已保存並關閉。
- 裝機開始前取消核對通過，原檔案清單／報告 hash 不變；返回前景同 PID 3896 的世代增至 2，完整重開後新 PID 5534／start tick 298109／世代 1，接入擷取正常。重開後第二、三份 runtime 擷取完整且 hash 與第一份相同，三份均已匯出；第四次介面顯示容量已滿、失敗報告成功保存／匯出、三份 binary／hash 不變且無新暫存檔，程序仍正常。
- 嚴格一秒時效、重開後尚未擷取與容量拒絕三份電腦端結果均不接受 binary。裝置三份檔案共 51,885,456 bytes 仍保留、容量已滿，未清理。開始前取消、前背景切換及容量拒絕不替代擷取途中背景中斷、程序內 Activity 重建及實際保存 I/O 故障的裝機驗證；後三項仍待執行。
- **本輪驗證與 0.1.1-probe 交付完成**；來源提交 `8b36733a135e869fbcaa565033eb93a2c402a066` 的 117 份產品輸入與 APK 指紋一致，成品位於 `E:/ipa/PADCheaper-Android-0.1.1-probe` 及同名 ZIP。70 份檔案校驗及 71 項 ZIP 的 CRC／逐檔 hash 全部吻合；ZIP 673,978 bytes，SHA-256 `7843b72567a859f0a1b1b4bdc0eff6be345534f1c3a8008b2de01f25f7baef82`，詳見 [交付收據](../research/verification-0.1.1-probe.json)。包內文件保持封裝前快照，本段為交付後補記，不修改成品或 0.1.0-probe／iOS 既有交付。
- `deviceVerified`／`runtimeLayoutVerified`／`boardReadEnabled`／`actionsEnabled`／`unpackedCodeVerified` 全部仍 false，Android 盤面與遊戲操作未接通。

## 2026-11-06：第五批，擷取後的離線核對與分析，待編譯測試

- 使用者要求「繼續」。新增 [離線分析](runtime-code-analysis.md)，輸入有效 collector 成對結果、該次模組 APK 及精確原版 libpad，重新核對來源／程序／hash／能力旗標，再列出頁面差異與原版區段對照。輸出保持擷取位元組不變，不重建不完整 ELF 或接受參考專案偏移。
- Collector 補存最終身分重核後的實際 `finalDeviceElapsedMillis`／`maxAgeMillisUsed`，並以同一值執行最後時效判斷。離線工具依保存的時鐘重驗，缺失資料拒絕，不假設舊封套仍有效或重新核對了目前裝置。
- 原版 section table 在擷取範圍外；分析只將其作已核對原版 metadata，區分實際完整擷取的區段與未擷取的 RW／BSS。差異範圍是頁面候選，原版 `.text` 標籤與位元組差異均不授予解碼／盤面驗證。
- 成功分析發布 raw 複本、原報告、hash／差異資料及 IDA 匯入規格，新的輸出與擷取目錄分開。規格明示強制 raw、AArch64 64-bit little-endian、僅 `[0,0x107e730)`，缺失區域未知；實際 IDA 匯入仍待授權測試。
- 原生／Java 擷取再次唯讀審查未發現新的阻擋問題，產品來源未變。本批沒有連接或操作裝置，也沒有取得 runtime image；第四、五批的工具與案例仍待執行，既有 0.1.0-probe 通過結果不驗證這兩批。
- 新增九組分析工具合成案例，涵蓋封存時鐘、身分／hash 不符、末頁與 end-exclusive 邊界、未擷取 RW／BSS、來源重讀變更、成組發布及路徑逃逸；collector 另加兩組最終時鐘案例，目前十一組。七份 Python 來源完成 AST，JSON 重複鍵、28 個文件連結及差異檢查通過；原版 ELF section table 位置／29 筆 metadata 已核對。**只做靜態檢查，沒有匯入或執行這些工具／案例，也未驗證 IDA raw 匯入。**

## 2026-11-06：第四批，盤面研究的固定程式區段擷取，待編譯測試

- 使用者要求「繼續」。先核對原版 ELF 與 IDA 資料庫；參考專案的盤面名稱／偏移沒有本版執行期證據，基址算法也不適用已觀察的 Houdini r--p 映射。新增 [程式區段擷取](runtime-code-capture.md) 與研究摘要，未移植未核對的 board offset。
- 診斷頁新增明確手動「擷取程式資料」，獨立 schema 1 報告及 UUID binary。限定原版第一個不可寫 PT_LOAD 的 17,295,152 bytes，精確 header／program headers、映射來源／完整範圍、双次讀取一致性與十秒 native 上限；Java 核對原前景／thread／程序與三十秒請求上限，暫存容量最多三份。
- 分階段保存，sync 並回讀精確 JSON 後才保留成功 binary；發布判斷與報告使用同一 terminal frame snapshot，失敗不保留本次可用影像。原 20-byte 接入診斷保持独立，沒有自動擷取、權限變更、遊戲狀態寫入或 ARM 函式呼叫。
- 新增只讀電腦端工具，核對目前程序、已安裝 APK、來源／payload、報告與 binary 的 header／hash；传輸後重核對，成對發布到新的本機目錄，失敗不接受 binary。新增 C++／Java／Python 案例待執行。
- 整合後 Java 主程式／案例的 parse＋type analysis 通過，未產生產品 class；NDK arm64／x86_64 對新 native helper、JNI 與案例的 syntax-only 通過。Python AST、JSON 重複鍵、24 個文件連結、精確 512-byte 原版 metadata 與差異空白檢查完成。新增 Python 9 個案例及 Java／C++ 案例均未執行；native 十秒限制是每個有限區塊間檢查 deadline，不宣稱可強行中斷阻塞的系統呼叫。
- 本輪只讀查詢時 PAD 主程序未執行；沒有啟動／停止遊戲、取得 runtime dump、安裝或修改 LSPosed。**本批尚未編譯／跑測試／裝機；0.1.0-probe 的通過紀錄不驗證本批來源，E:/ipa 交付檔保持不變。** Android 讀盤、求路接入、技能及操作仍未完成。

## 2026-11-06：0.1.0-probe 集中編譯測試完成

- 延續使用者明確「編譯測試」的工作。上個聊天在整理交付時中斷；本次重新核對產品來源、最後 APK 與三份成功擷取紀錄完全一致，沒有變更已測產品來源或重跑已通過的套件。以下三批「尚未執行」是當時的歷史，最終結果以本節及 [版本驗證紀錄](release-0.1.0-probe.md) 為準。
- Gradle 8.13／AGP 8.12.0／SDK 36／NDK 27 完成 debug APK，包含 arm64-v8a、x86_64 原生 payload 與共用核心建置。API 102 維持 compileOnly，完整建置及裝機通過；沒有打包框架 API 類別，APK v2 簽章通過。
- Java 12 個案例、Python 7 個案例（含同名子程序辨識）通過。Windows GCC 15.2.0 的共用求路器回歸 1/1 通過，Android x86_64 原生解析器在 emulator-5568 執行成功，包含合成檔案映射的完整 header 讀取路徑。最初 GCC `-O3 -Werror` 在 EvaluateStatic 報陣列界限警告；GNU C++ Release 核心改用既有驗證的 `-O2` 後通過，沒有關閉警告，`-O3` 不列為已驗證。
- 裝機發現原生診斷開啟 `/proc/self/mem` 時程序退出，沒有留下可歸因的 crash 報告。改用核對映射範圍後的 `process_vm_readv`，只複製 ELF header 20 bytes，通過原生案例與完整裝機重測。擷取各階段先保存進度，程序中斷不再留下看似成功的舊結果。
- `pidof` 可回傳同名 fork 子程序。擷取工具改為核對同 UID、唯一由 zygote 啟動的根程序與完整後代關係，不依 PID 順序挑選；保留前後 PID／start tick／boot／ABI 身分核對，歧義或重開則拒絕。
- 最終 APK 已安裝並在現有 LSPosed 啟用日服 PAD。首次擷取、返回前景、完整重開三份結果均為 `entry-evidence-collected`、零 issues；前背景同一程序的世代由 1→2，重開後 PID／start tick 改變且世代回到 1。約 29.984–29.996 fps；x86_64 payload 成功觀察 ARM64 ELF header。這不是 native 操作窗口或讀盤驗證。
- APK SHA-256 `f6bf4e3ee48c09353d02edcffe7e9278b5e5bbf765aab6ef1bf206c05bcab909`，1,640,219 bytes；產品來源指紋 `72698704498a06d38d53a8eeee0b89132f03ada3ce5a6012081a53b5e01033a1`。早期 `apk-identity.json` 與 6 案例 Python log 保留為歷史，最終證據另存 `apk-identity-final.json` 及 `python-tests-final.log`。
- `deviceVerified`／`runtimeLayoutVerified`／`boardReadEnabled`／`actionsEnabled` 全部仍為 false。OpenGL／Vulkan 分別實測、程序內 Activity 重建、原生 arm64 裝置、載入失敗介面及多人程序隔離仍待驗證；沒有取得 Android 執行期程式段或盤面 layout。iOS 來源／CI／既有交付不變。
- 已保存來源提交 `5c61e85f52de885f9ea43a1b6119e7af44aea5f9`，逐一確認 114 份提交內容與 APK 產品指紋一致；當前安裝 APK 雜湊亦再次吻合。已交付 `E:/ipa/PADCheaper-Android-0.1.0-probe` 及同名 ZIP，40 份檔案校驗、41 項 ZIP 逐檔與 CRC 核對通過。ZIP 580,150 bytes，SHA-256 `0bdb49e57684f59a4c89a438316e6532d58d48549dd35f0bdf450a97639f4a2e`。後續僅記錄完成結果，不變更包內文件快照或 APK，詳見 [交付收據](../research/verification-0.1.0-probe.json)。

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
3. 已取得本版 Android 固定不可寫區段的執行期程式證據，接著以 IDA 核對只讀盤面、尺寸、phase、單人模式、protected 欄位與特殊珠。另建 Android layout／fingerprint，不移用 iOS 或舊探針偏移。
4. 正規化盤面接共用求路器，再逐步驗證明確的單人轉一次、取消／放手；Auto／技能／換隊留在後續批次，8 人最後處理。

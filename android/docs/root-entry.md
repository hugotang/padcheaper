# Root 接入方案：選擇 LSPosed

2026-11-06。使用者要求開始 Android 版並比較 Zygisk／LSPosed，指定 emulator-5568、KernelSU。最初研究只讀取環境及建立來源；其後按明確「編譯測試」要求，已建置並安裝 `0.1.0-probe`，在現有 LSPosed 啟用 PAD 作用域。沒有安裝或升級 root 框架，最終結果見 [版本驗證紀錄](release-0.1.0-probe.md)。

## 決策

採用 **KernelSU → 現有 Zygisk Next → 現有 LSPosed → PADCheaper APK → JNI／C++**。Zygisk 是程序載入層，LSPosed 是建在其上的 ART hook 框架。LSPosed 能帶原生 C++，不代表只能處理 Java；直接 Zygisk 也不會自動讓模組跑在遊戲 frame thread。

| 項目 | LSPosed + 原生 payload | 直接 Zygisk 模組 |
|---|---|---|
| 目前環境 | 已安裝，frame/lifecycle 可以從 Java 接入 | 已有 Zygisk，但需自建載入、Java 橋接及管理入口 |
| 配置／介面 | 模組 APK、Activity 內按鈕，後續可用 WebView | 可做，但不是 Zygisk API 直接提供 |
| 求路核心 | JNI 載入同 CPU 的 C++，與框架分離 | 可載入 C++，需遵守 Zygisk 的 C++ runtime 隔離要求 |
| 原生呼叫窗口 | 必須驗證逐幀 callback、JNI 及來源 | 同樣需要單獨驗證 |
| x86_64 → ARM 轉譯 | Java wrapper 保留原本 JNI／Houdini 分派路徑 | x86_64 hook 不能直接跳到 ARM 函式位址 |
| 後續替換 | C++ 診斷不依賴 libxposed，可另加其他入口 | 若 LSPosed 路径在目標設備實測受阻，才重新評估 |

不採用「遊戲是原生所以必須 Zygisk」的判斷。此專案同時需要原生盤面、Java 生命週期與使用者介面；目前接入成本以 LSPosed 較低。

## 已取得的證據

- 模擬器 Android 15／API 35，ABI 清單 `x86_64,arm64-v8a,x86`，native bridge 為 libnb.so。PAD 的主程序 maps 同時出現 x86_64 LSPosed／ART、libhoudini.so 和 arm64/libpad.so。這是混合架構環境，不能以模擬器顯示的 SM-S9280 名稱當作 ARM 硬體。
- KernelSU root 可讀。Zygisk Next 1.4.2（789）與 LSPosed v2.1.0（7769）已安裝。框架內實際 `XposedInterface.LIB_API=102`，有無參數 XposedModule constructor、PackageReadyParam、HookBuilder、HotReloading callbacks；不僅依 module.prop 推斷 API。
- PAD 是 23.6.0／230600，split APK 含 arm64 libpad（30,735,496 bytes）。APK 與原生庫雜湊記錄於 target JSON；沒有提交遊戲二進位。
- DEX：`jp.gungho.pad.AppDelegate.OnDrawFrame()V` 是 public Java wrapper，僅 invoke-virtual 原生 `onDrawFrame()V` 一次再 return。onCreate/onResume/onPause/onDestroy 同樣存在。目前模擬器已觀察到約 30 fps 的 callback；沒有分別驗證 OpenGL／Vulkan 覆蓋率，也沒有量測實際 inline 狀態。
- 第二批核對四份 DEX：OpenGL／Vulkan 都經過 `h12.onDrawFrame(GL10)`，Vulkan 另經 `l22.a()`。已在精確 APK 篩選後加上這兩個呼叫者的 deoptimize，避免 wrapper 內聯繞過 hook。詳見 [逐幀證據與診斷](frame-dispatch.md)。
- 最初研究曾觀察到主程序及名為 gungho.pad:tab1 的衍生程序；後者 PPid 指向主程序。該批後段程序已不在，當時未重啟遊戲或取得原生執行期程式段。後續裝機另觀察到同名子程序，擷取工具已改以 UID／程序樹／zygote 父程序辨認唯一主程序，並通過相關離線案例。
- `0.1.0-probe` 的首次擷取、返回前景與完整重開紀錄均通過身分核對；x86_64 payload 已用有限的 `process_vm_readv` 讀取 ARM64 ELF header。這未取得 native 解碼後程式段或 Android 盤面布局。
- 本機 E:/PADhelper 的 `0a77821` 提供 LSPosed + 原生載入參考，但含單檔輪詢、遊戲寫入和舊版本偏移。本原型沒有複製其背景命令、直接 ARM 呼叫、網路及存檔寫入，也沒有把其註解的成功聲明當成本版驗證。

## 原型邊界

套件名稱、主程序與 base APK SHA 通過後才安裝五個 Java observers。每個 callback 重新比對 PID，排除 fork 後繼承的主程序狀態。原方法呼叫一次；逐幀只記錄計數、執行緒與 monotonic time。前背景變化獨立記錄，不授權戰鬥操作。

逐幀證據綁定目前 Activity 與前景世代；舊 Activity 的延遲 pause／destroy 或跨世代 frame 都不能覆蓋新畫面狀態。背景時關閉診斷彈窗，重建時移除舊按鈕；有限擷取工作只保留 Application context 及 Activity 弱參考。診斷按鈕自身也核對 PID，避免繼承的 UI listener 在衍生程序啟動擷取。

原生映像檢查必須由遊戲內按鈕明確要求，使用有限工作執行緒；磁碟 hash 和檔案 I/O 不放入 frame。紀錄保存在遊戲 Context 提供的私有目錄，不硬編 user 0。讀取不可用則顯示不可用，不更改 SELinux／ptrace 設定。

尚未核對 Android native layout／單人模式／資料防護編碼／讀盘時機，因此不讀 live board，更不呼叫技能或輸入。尤其 x86_64 payload 絕不可強轉 ARM 地址為 callable function。未來需要使用已驗證的 Java JNI 入口，或在正確 ARM 執行環境中的明確橋接。

## 外部來源（本輪核對）

- [Zygisk 官方 API／樣板](https://github.com/topjohnwu/zygisk-module-sample/tree/8ce26128f81baaed0b969aaf7f52f886b61af4ab)：API v5、specialization callbacks、JNI hook 與 companion 的層級；README 說明封存是唯讀發布方式，並非停止維護。官方提醒 Zygisk payload 不能直接混用一般 NDK STL 配置。
- [LSPosed 原始專案](https://github.com/LSPosed/LSPosed/tree/df74d83eb03a44cc6ad268841ac2ada28d077c77)：ART hooking，底層使用 LSPlant；其 latest release 仍為 1.9.2，不能當作這台 API 102 的版本依據。
- [libxposed API 102](https://github.com/libxposed/api/tree/79b75b49255a257d38f67bce2e928649dc2fc6c9)：package callbacks、protective exception semantics、hot reload 必須清理 native 狀態。使用 Maven 102.0.0，未包裝 API 類別。
- [AGP 8.12.0 官方來源](https://dl.google.com/dl/android/maven2/com/android/tools/build/gradle/8.12.0/gradle-8.12.0-sources.jar)：CheckAarMetadataTask 的依賴集合是 RUNTIME_CLASSPATH。API 102 AAR 的 minCompileSdk=37 已記錄；維持 compileOnly，SDK 36 的依賴解析、Java 測試與完整 APK 建置已通過，沒有修改 AAR metadata 或打包 API 實作。
- [Vector 2.2（JingMatrix LSPosed 分支）](https://github.com/JingMatrix/Vector/releases/tag/v2.2)：目前另一條 API 102 實作；名稱已改 Vector。此處僅做生態比較，沒有替使用者更新現有 LSPosed。
- [Android PADHelper 參考](https://github.com/befollowed/PADhelper/tree/0a77821df2d8b86184fc7946948c5282e0e62193)：本機參考的 commit。具體雜湊以 android/research/entry-20261106.json 為準。

採用某版本框架的公開支援範圍不代表本模組已通過裝置測試；需以指定模擬器的實際回歸為準。

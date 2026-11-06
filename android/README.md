# PADCheaper Android 接入原型

採用 **LSPosed／libxposed API 102 + JNI／C++**。root 接入以使用者現有 KernelSU → Zygisk Next → LSPosed 為基準；本專案產生模組 APK，不另裝直接 Zygisk 模組。

目前建置版本 `0.1.1-probe`，已完成雙 ABI APK 建置，並在 `emulator-5568` 安裝及啟用。除程序／APK 篩選、診斷按鈕、Java 逐幀觀察及 x86_64 payload 載入外，新增固定不可寫程式區段的明確手動擷取與離線核對；已成功取得 17,295,152 bytes 的 ARM64 runtime 程式研究資料。沒有讀盤、轉珠、技能、換隊或背景自動流程。

Java 13 個案例、Python 27 個案例、Windows 共用求路器回歸及模擬器 x86_64 的兩個原生測試執行檔全部通過。本版接入擷取、固定區段擷取、離線分析與 IDA raw 匯入的位元組／範圍核對亦已成功；開始前取消、返回前景、完整重開與三份容量上限檢查通過。擷取途中背景中斷、程序內 Activity 重建及實際保存 I/O 故障尚未裝機實測；OpenGL／Vulkan 分別覆蓋、arm64 真機與多人隔離亦待驗證。結果與 APK 身分見 [0.1.1-probe 驗證紀錄](docs/release-0.1.1-probe.md)。

第四、五批的 [固定程式區段擷取](docs/runtime-code-capture.md) 與 [離線來源核對、原版差異與 IDA 匯入規格](docs/runtime-code-analysis.md) 已納入本次集中編譯測試。位元組差異只提供研究候選，不代表 Android 盤面已定位。

最新來源已開始 [盤面局部布局研究](docs/board-layout-2026-11-06.md) 與 [純資料求路預覽](docs/board-preview.md)：已核對尺寸、指標表／物件池和部分欄位讀寫，並新增離線資料接口。新 C++ 來源及案例已通過 Windows GCC 與 NDK 27 arm64／x86_64 語法檢查，尚未建置產品或執行案例，也沒有 live 讀盤／JNI／UI 接入；下述 0.1.1-probe 通過與交付紀錄不驗證這批新來源。

0.1.1-probe 驗證與交付完成，成品位於 `E:/ipa/PADCheaper-Android-0.1.1-probe` 及同名 ZIP，詳見 [交付收據](research/verification-0.1.1-probe.json)。既有 [0.1.0-probe 交付](docs/release-0.1.0-probe.md) 保持不變，其 APK 不包含本版程式擷取功能。指定模擬器保留三份已匯出的程式檔，目前容量已滿；再次擷取前需先確認本機證據並清理裝置舊檔。

## 裝置與目標

- 使用者指定 `emulator-5568`，Android 15／API 35、KernelSU；已觀察 Zygisk Next 1.4.2 與 LSPosed 2.1.0 (7769)，其 framework.dex 含 libxposed API 102。
- 模擬器的 ART 是 x86_64，日服 PAD 23.6.0／230600 則載入 arm64 libpad，由 Houdini 轉譯。設定中的模型名稱不是 ARM CPU 的證據。
- `targets/pad-23.6.0.json` 固定此次 APK／libpad SHA-256。其他 APK、版本或布局一律不能被当成同一目標；對其他來源要重新驗證並明確新增描述。
- 模組已建置 x86_64／arm64-v8a payload；本次裝機只執行 x86_64 payload。載入時讀 `/proc/self/exe` 的 ELF machine，不能從遊戲的 primaryCpuAbi 或裝置支援 ABI 清單猜測。

詳細取捨與來源見 [接入方案](docs/root-entry.md)，未完成項目見 [開發紀錄](docs/development.md)。

## 編譯與驗證步驟

本版已按使用者明確的「編譯測試」要求完成建置、自動測試及上述裝機驗證；後續來源變更仍沿用相同批次流程。環境：JDK 17 以上、Gradle 8.13、AGP 8.12.0、Android SDK 36、NDK 27.0.12077973、CMake 3.22.1。Windows 用 `gradlew.bat`，其他平台用 `bash gradlew`。

1. 在 android 目錄執行 `gradlew.bat :app:testDebugUnitTest :app:assembleDebug`。
2. Linux 主機可配置 android/tests 的 CMake 並執行 CTest；本次以 NDK 建置 x86_64 測試執行檔並在指定模擬器執行。core 的 `PH_BUILD_CORE_TESTS=ON` 接入現有求路器回歸，本次在 Windows GCC 15／Release `-O2` 通過。於 repo 根目錄執行 `python -m unittest discover -s android/tests -p "test_*.py"`，驗證擷取工具的離線案例。
3. 核對 APK 裡包含兩種 ABI、META-INF/xposed 三份入口資料、assets/padcheaper-build.json，沒有打包 libxposed API 類別。
4. 在指定模擬器安裝 APK，透過現有 LSPosed 啟用作用域 `jp.gungho.pad`，完整重開遊戲。不得修改其他已安裝模組的作用域。
5. 在遊戲內點「PC 診斷」→「擷取接入狀態」。核對前背景、影格新鮮度／本次前景計數、繪製執行緒、原生 payload ABI、libpad machine，以及 `files/padcheaper/probe-<pid>.json`；分別驗證 OpenGL／Vulkan。[只讀擷取工具](docs/probe-collection.md) 會核對 schema 3 的來源／程序／時間及 payload 身分，逐幀語意見 [診斷說明](docs/frame-dispatch.md)。
6. 驗證重開／Activity 重建沒有重複按鈕，原生載入失敗保留清楚錯誤；子程序不得沿用主程序資格。這批不支援 native hot reload，更新需完整重開遊戲。

Gradle 不含安裝任務串接或自動 GitHub Actions；建置不會自行啟動遊戲。輸出位於 repo 的 build/android，沒有修改 E:/ipa 的 iOS 交付物。

API 102 的官方 AAR 標示 minCompileSdk 37；本專案只以 `compileOnly` 引用。已核對 AGP 8.12.0 原始碼，其 AAR metadata 檢查針對 runtime classpath。SDK 36 的 Gradle 依賴解析、Java 測試及完整 APK 建置已通過，APK 亦確認沒有打包 libxposed API 類別；沒有剝除或修改 AAR metadata。這是本版所用 API 與目前工具鏈的驗證結果。

Gradle wrapper 使用官方 8.13 檔案並固定 distribution checksum，來源及授權見 [第三方元件](THIRD_PARTY.md)。

## 診斷與共用核心

原生診斷只讀 `/proc/self/maps`，再以 `process_vm_readv` 讀取經映射範圍核對的 ELF header 20 bytes，不讀帳號資料／heap，不呼叫 ARM 遊戲函式，也不改變權限來補救讀取失敗。早期開啟 `/proc/self/mem` 令測試遊戲程序退出，已改用上述有限範圍讀取並完成重測。Houdini 下 ARM 段可能只有 r--p；有無可執行頁只記錄事實。

「擷取程式資料」是另一個明確手動操作，限定已核對 ELF 第一個不可寫 PT_LOAD，約 16.5 MiB；需前景／程序身分與映射／雙次內容一致，僅產生程式研究檔，仍不讀盤面。0.1.1-probe 已在指定模擬器完成擷取、電腦端匯出與離線核對。

`libraryArtifactMatched` 指磁碟上的 libpad 與此次版本一致；`arm64-image-observed` 只代表映射 ELF machine 可讀。兩者均不證明執行期解碼、盤面、模式或函式可呼叫。相應旗標固定 false。

安裝入口會驗證並處理已核對的兩個 Java 呼叫者，減少內聯繞過逐幀 hook 的風險；失敗則解除 observers 並記錄原因。目前模擬器已觀察到約 30 fps 的 callback，返回前景與完整重開後均可重新擷取；尚未分別切換 OpenGL／Vulkan 驗證覆蓋率。原生載入除 APK 路徑與雜湊外，另核對實際 payload ELF machine 與目前程序一致。

[core](../core/README.md) 直接建置現有 iOS C／C++ 求路來源，暫不搬檔，也不暴露到即時模組。待 Android 盤面／特殊珠語意及版本校驗完成，再接入正規化盤面。

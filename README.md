# PADCheaper — iOS LiveContainer dylib

這是 PADHelper 的 iOS 原型，目標為日服《パズル＆ドラゴンズ》23.6.0、iOS build **106878**、arm64，設計用於 LiveContainer 的無 JIT 載入方式。

GitHub Actions 會用 macOS／Xcode 編譯 **PADCheaper.dylib**，執行隊伍 JSON、LiveContainer 身分／設定及 iOS 模擬器 GUI 檢查，並上傳包含 dylib、SHA-256、設定範例及文件的 artifact。

**目前是實驗原型，預設僅診斷。** 已加入版本／UUID／指令校驗、Objective-C 攔截、皮膚／BGM 位圖與隊伍上傳／本地回蓋程式；尚未在 iPhone 驗證。徽章完整解鎖未實作。

**0.2.0-gui 起提供可拖動的 PC 浮動按鈕。** 點開即可查看載入狀態、停用原因及切換 Skin／BGM／背景計時修正；設定自動儲存。遊戲校驗失敗也會顯示面板；浮動按鈕出現代表 dylib 已載入，綠色及面板中的個別功能狀態才代表功能通過啟用條件。

- [編譯、安裝與設定](ios/README.md)
- [靜態分析與可行性](ios/docs/feasibility.md)
- [目前原型的時序與限制](ios/docs/runtime.md)
- [版本目標清單](ios/targets/pad-23.6.0.json)
- [Actions 編譯流程](https://github.com/hugotang/padcheaper/actions/workflows/ios-dylib.yml)

不需要上傳 IPA、IDA database 或簽署憑證到 GitHub。CI 產物使用 ad-hoc 簽章；安裝前須由 LiveContainer 使用裝置可用的憑證重新簽署。

Android 參考專案為 [PADHelper](https://github.com/befollowed/PADhelper)，授權見 [LICENSE](LICENSE)。此 repo 保留分析／程式碼，不附遊戲 executable 或音訊資源。

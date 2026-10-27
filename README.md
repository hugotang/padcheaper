# PADCheaper — iOS LiveContainer dylib

這是 PADHelper 的 iOS 原型，目標為日服《パズル＆ドラゴンズ》23.6.0、iOS build **106878**、arm64，設計用於 LiveContainer 的無 JIT 載入方式。

GitHub Actions 會用 macOS／Xcode 編譯 **PADCheaper.dylib**，執行隊伍 JSON、LiveContainer 身分／設定、戰鬥狀態、徽章原生佈局、計時策略及 iOS 模擬器 GUI 檢查，並上傳包含 dylib、SHA-256、設定範例及文件的 artifact。

**0.3.0-battle 是實驗原型，預設僅診斷。** 已加入版本／UUID／指令校驗、皮膚／BGM、有效隊伍徽章解鎖、單名戰鬥隊員替換／還原，以及計時旗標修正和手動關閉計時退出彈窗。皮膚／BGM／徽章使用隊伍上傳預設化與本地回蓋。2026-10-27 使用者回報 `0.2.0-gui` 在 iOS 27、自訂 LiveContainer、無 JIT 下 GUI 及 Skin／BGM 已實際生效；新版換怪、徽章、計時、儲存／重啟及帳號隔離仍待真機驗證，通關結算尚未驗證。

**PC 浮動按鈕可拖動，設定自動儲存。** 點開即可查看載入及個別功能狀態、切換功能、選擇戰鬥隊員位置 1–6、用數字鍵盤輸入目標圖鑑 ID，再套用或還原。計時退出彈窗須符合條件才可手動關閉。遊戲校驗失敗也會顯示面板；浮動按鈕出現代表 dylib 已載入，綠色及面板中的個別功能狀態才代表功能通過啟用條件。

**0.2.1-gui 加入儲存／伺服器接受／回蓋狀態。** 新儲存會取消舊回蓋，即使新選擇改回預設或本地保存失敗，舊回應也不再覆蓋新選擇。

- [編譯、安裝與設定](ios/README.md)
- [靜態分析與可行性](ios/docs/feasibility.md)
- [目前原型的時序與限制](ios/docs/runtime.md)
- [單名戰鬥隊員替換](ios/docs/monster-replacement.md)
- [徽章與計時控制](ios/docs/badge-timer.md)
- [真機回報與儲存／重啟測試](ios/docs/device-testing.md)
- [版本目標清單](ios/targets/pad-23.6.0.json)
- [Actions 編譯流程](https://github.com/hugotang/padcheaper/actions/workflows/ios-dylib.yml)

不需要上傳 IPA、IDA database 或簽署憑證到 GitHub。CI 產物使用 ad-hoc 簽章；安裝前須由 LiveContainer 使用裝置可用的憑證重新簽署。

Android 參考專案為 [PADHelper](https://github.com/befollowed/PADhelper)，授權見 [LICENSE](LICENSE)。此 repo 保留分析／程式碼，不附遊戲 executable 或音訊資源。

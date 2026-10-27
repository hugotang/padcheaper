# PADCheaper — iOS LiveContainer dylib

這是 PADHelper 的 iOS 原型，目標為日服《パズル＆ドラゴンズ》23.6.0、iOS build **106878**、arm64，設計用於 LiveContainer 的無 JIT 載入方式。

GitHub Actions 會用 macOS／Xcode 編譯 **PADCheaper.dylib**，執行隊伍 JSON、LiveContainer 身分／設定、戰鬥狀態、徽章原生佈局、計時策略及 iOS 模擬器 GUI 檢查，並上傳包含 dylib、SHA-256、設定範例及文件的 artifact。

**0.4.0-team 加入六人編隊與 GUI 入口設定，預設僅診斷。** 六個位置各有本體與武裝，可搜尋圖鑑、匯入／匯出 PADDashFormation 分享連結，設定等級、技能等級、普通／超／潛在覺醒和加值，再逐格或整隊套用、還原。圖鑑 #9900 起自動轉換為內部 ID；#14238 正確對應猫猫。皮膚／BGM／徽章沿用隊伍上傳預設化與本地回蓋，計時功能沿用原生觸控及旗標檢查。

2026-10-27 使用者已回報 GUI／Skin／BGM、徽章解鎖及先前單名替換可用。六人完整參數來源、計時、儲存／重啟、帳號隔離與通關結算仍需真機驗證。原生格式與限制見 [整隊與滿狀態](ios/docs/team-presets.md)。

**PC 面板的「入口設定」可即時修改 mode／source／multi。** mode 預設全部允許，source／multi 預設 0；三項各有「全部允許」與清單設定，自動保存到 config.json，每秒重載。過濾條件不改遊戲的實際數值；multi 1／4 使用多組隊伍的原生分支，需要另外適配，目前會顯示原因並停止操作。版本校驗、記憶體範圍、原生摘要及戰鬥階段仍須通過。

**0.2.1-gui 加入儲存／伺服器接受／回蓋狀態。** 新儲存會取消舊回蓋，即使新選擇改回預設或本地保存失敗，舊回應也不再覆蓋新選擇。

- [編譯、安裝與設定](ios/README.md)
- [靜態分析與可行性](ios/docs/feasibility.md)
- [目前原型的時序與限制](ios/docs/runtime.md)
- [單名戰鬥隊員替換](ios/docs/monster-replacement.md)
- [六人編隊與入口設定](ios/docs/formation.md)
- [徽章與計時控制](ios/docs/badge-timer.md)
- [真機回報與儲存／重啟測試](ios/docs/device-testing.md)
- [版本目標清單](ios/targets/pad-23.6.0.json)
- [Actions 編譯流程](https://github.com/hugotang/padcheaper/actions/workflows/ios-dylib.yml)

不需要上傳 IPA、IDA database 或簽署憑證到 GitHub。CI 產物使用 ad-hoc 簽章；安裝前須由 LiveContainer 使用裝置可用的憑證重新簽署。

Android 參考專案為 [PADHelper](https://github.com/befollowed/PADhelper)，授權見 [LICENSE](LICENSE)。此 repo 保留分析／程式碼，不附遊戲 executable 或音訊資源。

# PADCheaper — iOS LiveContainer dylib

這是 PADHelper 的 iOS 原型，目標為日服《パズル＆ドラゴンズ》23.6.0、iOS build **106878**、arm64，設計用於 LiveContainer 的無 JIT 載入方式。

GitHub Actions 會用 macOS／Xcode 編譯 **PADCheaper.dylib**，執行隊伍 JSON、LiveContainer 身分／設定、戰鬥狀態、徽章原生佈局、計時策略及 iOS 模擬器 GUI 檢查，並上傳包含 dylib、SHA-256、設定範例及文件的 artifact。

**0.4.2-entry 修正 mode 7／multi 1 三人模式的入場自動套隊。** 支援兩人或三人入場，在 phase 46 同步期間只處理本機席位的六名隊員並重算 SB，同一 frame 恢復原本顯示的隊伍。戰鬥中手動套用與還原須等自己的回合；詳見 [三人模式修正](ios/docs/three-player-entry.md)。保留 64 隊隊伍庫、六個位置的本體／武裝編輯、PADDashFormation 匯入／匯出及覺醒名稱。

使用者已回報 GUI／Skin／BGM、徽章解鎖及六人替換可用，並於 2026-10-28 確認單人入場自動套隊與 SB 成功。這次多人修正、計時、帳號隔離與通關結算仍需真機驗證。原生格式與限制見 [整隊與滿狀態](ios/docs/team-presets.md)。

**PC 面板的「入場自動套用隊伍」使用目前選中的儲存隊伍。** 先在地下城外開啟總開關、戰鬥隊員替換及此選項；新入場的開場動畫期間會一次套用整隊，透過遊戲原生流程重算 SB。戰鬥中途才開啟、續關或切樓層不會重新充能。手動套用仍保留目前充能。

**「入口設定」可即時修改 mode／source／multi。** mode 預設全部允許，source／multi 預設 0；三項各有「全部允許」與清單設定，自動保存到 config.json，每秒重載。multi 1／4 已加入原生多隊陣列適配，追蹤目前隊伍及助人的實際來源；畫面外隊員的還原會等到切回該隊。使用時須把 1、4 加入 multi 清單。版本、陣列、原生摘要及戰鬥階段仍須通過校驗。

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

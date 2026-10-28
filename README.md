# PADCheaper — iOS LiveContainer dylib

PADHelper 的 iOS 原型，目標為日服《パズル＆ドラゴンズ》23.6.0、iOS build **106878**、arm64，供 LiveContainer 無 JIT 載入。

**0.4.6-ui 改善手機編隊及儲存狀態。** 六人總覽搭配獨立角色編輯面板，提供本體／武裝切換、上一格／下一格、整格交換／複製、撤銷及最近使用角色。隊伍庫最多 64 隊，支援 PADDashFormation 匯入／匯出。

儲存成功必須收到原生檔案寫入確認。失敗保留草稿、不關閉編輯器，也不變更下次入場選擇。「本場已套用」核對實際接受的本體、武裝及培養設定，不會因切換隊伍庫而誤標。滿級／滿技直接顯示實際數字；保留雙指縮放。

**戰鬥與計時寫入僅限單人。** 非零 multi、mode 7 或本場曾偵測多人皆不可寫入；舊 allow-all 設定不能解除限制。多人仍可編隊、保存及分享。撤回原因及證據見 [多人限制](ios/docs/three-player-entry.md)。

單人新入場會使用已儲存的選取隊伍，於第一個符合條件的開場 frame 套用，重算原生 Skill Boost、初始化目前 HP 並刷新隊伍數值。中途開啟、續關及後續樓層不重新充能；已生成的開場盤面不重建。使用者確認過單人套隊與 SB，新版 HP、實際技能及結算仍需真機確認。

自動轉珠目前完成 [pazusoba/core 可行性研究](ios/docs/auto-orb-feasibility.md)，包含離線編譯、路徑重播與原碼問題的重現。此版 dylib 尚未加入求解器或自動輸入。

GitHub Actions 使用 macOS／Xcode 編譯、檢查 arm64 Mach-O 與簽章，執行模型／原生戰鬥／儲存及 iOS 模擬器 GUI 測試。CI 產物附 SHA-256、設定範例及文件。CI 不替代真機驗收。

- [編譯、安裝與設定](ios/README.md)
- [編隊、儲存與生效狀態](ios/docs/formation.md)
- [入場數值與時機](ios/docs/entry-derived-state.md)
- [真機驗收](ios/docs/device-testing.md)
- [靜態分析與可行性](ios/docs/feasibility.md)
- [原生輸入讀取](ios/docs/battle-input.md)
- [Actions 編譯流程](https://github.com/hugotang/padcheaper/actions/workflows/ios-dylib.yml)

不需要上傳 IPA、IDA database 或簽署憑證。安裝前完整退出 PAD，替換 dylib，再由 LiveContainer 使用裝置憑證重新簽署。

Android 參考專案為 [PADHelper](https://github.com/befollowed/PADhelper)，授權見 [LICENSE](LICENSE)。此 repo 不附遊戲 executable 或音訊資源。

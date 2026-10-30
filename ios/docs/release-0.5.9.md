# 0.5.9-orbs

修正開場第 5 步的 7×6 擴盤等待與未通關隊伍 BGM 查找。編譯、簽章及測試結果見 [開發紀錄](development.md)，新修正仍待 Dopamine rootless iPhone 實機驗收。

- 原生擴盤插值期間保留開場進度與已啟動技能，待新盤面完整穩定後接第 6 步；不在動畫中求路、施放下一技能或轉珠。
- BGM 使用原生的大小寫不敏感前綴比對與第一個註冊曲目順序，修正 `BGM_972` 無法對上 `bgm_972.caf` 的情況。
- 複製診斷新增改盤等待、讀取失敗原因及 BGM 所選／註冊名稱，方便驗證本次兩個問題。

同一份 dylib 保留 **Dopamine rootless 與 LiveContainer** 兩種載入方式，使用者確認兩者目前都能使用。本次問題在 Dopamine 直接注入時回報，兩項修正共用原生讀取及 frame hook。

2026-10-30，[CI 76](https://github.com/hugotang/padcheaper/actions/runs/41017566656) 的原生／session／求解器／規則／config、iPhoneOS arm64 編譯、嚴格簽章、瀏覽器及四份 Scene／舊式視窗啟動／重啟報告全部通過，GUI 零 failures。已檢視面板、BGM 開關、策略頁與分享／匯入畫面。

二進位來源 `4807a75c00edc7c7e4c843e1c3ee5be12f1797f3`，大小 4,916,176 bytes，SHA-256 `2bc114f6a458138c21b9eff02350c14593319a312a6c9eea501211564f10e108`；113 組指紋與原始 IPA 相符，config 範例及兩份雨龍整包保留 CI 原件。`device_verified=false`。

既有隊伍與 config 可沿用。先停止連續模式、完整關閉遊戲，再依使用環境替換 dylib，確保只載入一份版本：Dopamine 沿用原有 rootless 注入方式；LiveContainer 在原 PAD Tweak Folder 換入新版並重新簽署。重新啟動確認面板 `0.5.9-orbs`，完整步驟見 [README](../README.md)。此交付為 dylib。

從新單人關卡測試同一雨龍整包：確認開場 6／6，7×6 盤面穩定後才開始轉珠；再確認「未通關也使用隊伍 BGM」及遊戲道中／頭目曲目已選定，聽取播放結果。若仍中斷或未播放，複製新版完整診斷，包含 `Orb last read issue` 與 `BGM playback.selection`。`device_verified=false`，完整原生依據見 [修正分析](opening-resize-music.md)。

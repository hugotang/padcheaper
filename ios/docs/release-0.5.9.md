# 0.5.9-orbs

修正開場第 5 步的 7×6 擴盤等待與未通關隊伍 BGM 查找。編譯、簽章及測試結果見 [開發紀錄](development.md)，新修正仍待 Dopamine rootless iPhone 實機驗收。

- 原生擴盤插值期間保留開場進度與已啟動技能，待新盤面完整穩定後接第 6 步；不在動畫中求路、施放下一技能或轉珠。
- BGM 使用原生的大小寫不敏感前綴比對與第一個註冊曲目順序，修正 `BGM_972` 無法對上 `bgm_972.caf` 的情況。
- 複製診斷新增改盤等待、讀取失敗原因及 BGM 所選／註冊名稱，方便驗證本次兩個問題。

既有隊伍與 config 可沿用。先停止連續模式、完整關閉遊戲，透過原有 **Dopamine rootless 注入方式**替換 dylib，確保只載入一份版本，再啟動確認面板 `0.5.9-orbs`。這次不需要 LiveContainer；此交付為 dylib，並非 Theos deb 安裝包。

從新單人關卡測試同一雨龍整包：確認開場 6／6，7×6 盤面穩定後才開始轉珠；再確認「未通關也使用隊伍 BGM」及遊戲道中／頭目曲目已選定，聽取播放結果。若仍中斷或未播放，複製新版完整診斷，包含 `Orb last read issue` 與 `BGM playback.selection`。`device_verified=false`，完整原生依據見 [修正分析](opening-resize-music.md)。

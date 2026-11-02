# 0.5.20-orbs：首次 Auto 就緒與通關停止

修正 8 人倒數完成後「轉一次」可按、但首次「開始自動」仍停用的問題。展開 PC 快捷列或開啟轉珠面板後，會在原生畫面更新時每秒最多四次讀取就緒狀態；倒數結束、自己的盤面可操作且讀取校驗通過，即可直接按 Auto。這一步不求路、不開技、不拖珠，也不收集戰鬥報告。

單人與 8 人 Auto 現在直接辨認原生 Dungeon Clear／結算階段，立即取消後續求路與操作，不再等待下一盤逾時或全場計時歸零。通關後開始按鈕停用；下一場仍需明確按 Auto。普通換層、技能動畫及 8 人下一盤不會被當作通關。

3／4 人維持每次點擊轉一次；8 人繼續使用 [活動設定](../templates/eight-player-thanks-2026/README.md) 的本場原生技能／首消目標。沒有變更隊伍、計時或通關請求。原始程式的階段分派、通關分支與字節指紋列於 [分析紀錄](../research/orb-auto-readiness-stop-20261102.json)。

2026-11-02，`0.5.20-orbs` 已由 [CI 108](https://github.com/hugotang/padcheaper/actions/runs/41395842797) 通過完整模型／solver／原生／Session／規劃／規則／設定回歸、iPhoneOS arm64 編譯與嚴格簽章、編隊瀏覽器及 Scene／舊式視窗首次與重啟共四份 GUI。新增案例涵蓋首次展開 PC／轉珠面板、倒數後直接 Auto、無輸入的就緒觀察、通關時取消求路及手勢、晚到回呼不能操作結算、普通換層／下一盤繼續，以及新場不自行恢復。本次首次就緒及通關停止截圖已檢視；426 組指紋符合原始 IPA。GUI 使用真實 Session 與模擬原生 frame，不能代替 PAD 真機同步驗收。

二進位來源 `f6bc20142a4484ee8f0cf99a17303e19c0ab637e`；dylib 10,477,552 bytes，最低 iOS 15.0；SHA-256 `49b9da3d5af37221fcbc67a9ead634c6785f1e9b4581f231eff510461f1abcee`。交付目錄為 `E:/ipa/PADCheaper-0.5.20-orbs` 及同名 ZIP。二進位、metadata、manifests、活動範本及研究 JSON 保留同一 CI 原件，文件驗證結果另行提交。研究 JSON 保留源碼提交時的驗證狀態，本節及交付 verification.json 記錄最終結果。Windows 模型／solver 回歸通過，沒有新增效能比較或真機結果；`device_verified=false`。

更新時完全關閉遊戲後替換 dylib；LiveContainer 需重新簽署，設定及隊伍庫保留。正常遊玩時核對冷啟動倒數後 Auto、最後一層 Dungeon Clear 即停止，以及下一場不自行恢復。真機同步及精確計分仍待驗收。

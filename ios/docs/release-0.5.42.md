# 0.5.42-orbs：求路閃退、帳號與資料保全

日服 PAD 23.6.1／107553、arm64，最低 iOS 15。[CI 155](https://github.com/hugotang/padcheaper/actions/runs/42460893938) 已完成完整回歸、iPhoneOS 建置／簽章及模擬器 GUI，已交付本版，編譯來源固定為 `38342e45966f49d684d393798e6b868ac80c457f`；`device_verified=false`。

- 修正色彩候選分桶越界。兩次 0.5.41 求路閃退沒有原盤或現場索引，不宣稱已重現原始事故；詳見[調查](solver-beam-crash-2026-11-07.md)。
- 關閉 Skin／BGM 後，隊伍記憶體中未擁有的 ID 不再隨 `save_decks` 上傳。已擁有的選擇與從未啟用的功能不受影響。
- 隊伍庫無法載入時鎖定儲存與隊伍管理；會移除隊伍的儲存先寫 `formations.previous.json`。未通過驗證的 `config.json` 在第一次覆寫前另存為 `config.invalid-<epoch>.json`。兩者都要手動還原。設定值無效不再停用全部功能。
- 8 人倒數自動開始只適用於設定開啟時觀察到倒數的那一場；中途開啟或第一盤已手動轉過，該場不自動開始。仍是每場最多一次，停止後不重新啟動，不排隊或重新配對。
- 一般 8 人 Auto 之後，單人 Auto 會恢復 HP／異常的停止保護。
- 29 同色＋單轉轉的 T＋四連盤型，在搜尋前直接回報現有六次取色上限不足。
- 新增 [Himmel／傑拉德／因陀羅／三迦梨模板](../templates/himmel-gamewith-545672/README.md)，隊伍徽章仍需在遊戲內選擇。
- 手動測試新增 `PHOrbSolve` 的 ASan／UBSan 檢查。

沒有擴大多人權限：3／4 人 Auto、4／8 人取色、換隊、改計時或封包、自動配對均未開放。

## 驗證與交付

[CI 155](https://github.com/hugotang/padcheaper/actions/runs/42460893938) 通過 macOS 全部回歸（含 codec 上傳保護、Session 與 `PHOrbSolve` 的 ASan／UBSan）、iPhoneOS arm64 建置／嚴格簽章、瀏覽器編輯器測試及四份 iPhone GUI。Windows 本機核心回歸在同一來源通過；Android 兩種 ABI 的共用核心在 `58bb11d` 編譯通過，之後的提交未修改共用求路來源。

[CI 154](https://github.com/hugotang/padcheaper/actions/runs/42459189569) 曾在 Session 測試失敗 4 條斷言，原因是生命週期固定資料未經過原生倒數，不符合新的自動開始語意；未產生 dylib。改由固定資料經過倒數、斷言不放寬後由 CI 155 通過，失敗 log 已納入交付。

已交付 `E:/ipa/PADCheaper-0.5.42-orbs` 及同名 ZIP。379 份校驗清單、ZIP CRC／380 項通過。dylib 11,957,440 bytes，SHA-256 `b700485fdcde5d573d1da998b5e27d714cb3e0859fce1ef0737fcb1c866d70bf`；ZIP SHA-256 `f3ff0f148ee858c7a2764ced6e0f424a4eb66a6a59ca7049e240c951a9ba9dcb`。

上述為離線與 iPhone 模擬器驗證。仍待真機確認：關閉 Skin／BGM 後實際送出的隊伍內容與 BGM ID 對應、8 人倒數期間是否持續讀到倒數狀態、多人同步。兩次 0.5.41 閃退沒有原盤，未宣稱已重現原始事故。

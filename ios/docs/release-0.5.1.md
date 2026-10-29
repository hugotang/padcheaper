# PADCheaper 0.5.1-orbs

適用於日服 PAD `jp.gungho.pad` 23.6.0／iOS build 106878，arm64、最低 iOS 15。使用者的 iOS 27／自訂 LiveContainer／無 JIT 環境已有舊版 GUI、Skin／BGM 及普通單人 6×5 的通過回報；新版增加的功能仍需真機驗收。

## 本版內容

- 單人 6×5／7×6／5×4，快速／均衡／深入求路，形狀、多目標及部分隊長盤面條件。
- 開始連續轉珠或執行一次；最多 12 條技能規則，按充能、回合、樓層、缺珠及敵人效果判斷，施放完成後重讀。
- 六組技能輪替、本場一次、保留樓層，以及最多 64 份隊伍／關卡策略。
- HP／我方異常自救與必要技能確認；可選低血時要求 1–3 組心珠，至少三連或四顆直線。
- 目標／規則／策略／心珠設定保存失敗時保留草稿；數字鍵盤及關閉後遊戲焦點已經模擬器驗證。

自動功能限目前單人關卡。多人戰鬥寫入保持停用；未支援條件、狀態不確定或目標未達時停止。沒有完整隊長倍率、傷害、回復量或轉轉時序求解。

## 更新

1. 完整關閉 PAD。保留 guest 容器內 `Library/Application Support/PADHelper/` 的 config.json、隊伍庫及帳號檔。
2. 在 LiveContainer 的 Tweaks 移除舊版或重複 dylib，加入這個版本的 PADCheaper.dylib；若使用子資料夾，PAD 的 Tweak Folder 須指向相同資料夾。
3. 由 LiveContainer 使用有效憑證重新簽署 tweaks，再啟動 PAD。面板應顯示 `0.5.1-orbs`。
4. 開啟要使用的功能，先讀盤及預覽，再測「執行一次」和停止；連續／技能／策略最後逐項開啟。低血心珠在舊設定預設關閉。

`config.example.json` 是首次設定參考；更新時保留原設定。CI 的 ad-hoc 簽章需要 LiveContainer 重新簽署後才能在裝置載入。

## 驗證與來源

2026-10-29，[CI 53](https://github.com/hugotang/padcheaper/actions/runs/40748683671) 全部通過：原生／模型／規則／策略／HP／低血心珠及既有回歸、arm64 iPhoneOS 編譯、Mach-O／簽章、瀏覽器編輯器，以及 Scene／舊式視窗的首次啟動與重啟，共四份 GUI probe，零 failures。

- 二進位來源：`96f5b2ffbbdaea273dba887aa9d8f73ff546ee99`。
- dylib SHA-256：`08ec12903762558e9d994c0fae897b57aba58236ae6fbcf6c2bf2d39506042e4`。
- 原始 IPA 的 UUID、執行檔雜湊及 26 筆程式指紋已核對一致。
- 包內 `build-info.json` 保留 CI 二進位來源；`verification.json` 和 `verification/` 記錄交付核對，文件另補上最新結果。

CI 使用戰鬥資料替身，沒有執行實際 PAD。真機讀盤／技能／結算仍待回報；詳見 [驗收順序](device-testing.md) 與 [完整驗證記錄](development.md)。

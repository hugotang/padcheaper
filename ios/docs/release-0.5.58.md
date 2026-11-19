# 0.5.58-orbs：B4 攻擊條件驗證候選

本版尚未交付，也未解決兩張原盤的搜尋驗收。r6 能拒絕不符合攻略的低四消攻擊，但目前仍可能停在 B4。

B4 兩盤新增首消六色各一組四顆、完整敵人名單及新鮮效果覆蓋。條件目標依原生受保護的吸收色與回合數挑選木吸收敵人；已知不存在才略過，資料未知或改變即停止。保留原技能順序、兩盤上限及同場 consumed 防重播。詳見 [B4 分析](coral-b4-2026-11-19.md)。

使用者明確要求編譯測試後，兩張 0.5.57 原盤已作 800／2000ms、beam512、48 步離線搜尋，未找到通過全部轉轉色相的六色四消路線。這是「在目前搜尋條件下未找到」，不是盤面無解證明。Windows 回歸已通過；[CI 186](https://github.com/hugotang/padcheaper/actions/runs/44551918880) 在 `1c4003ecff14bbdb331a29ba7a2178e6d6dfb7bb` 完整通過 macOS 原生／Session、schema／solver、ASan/UBSan、iPhoneOS／簽章、瀏覽器及四份 iPhone GUI。macOS 同樣保留原盤驗收未過結果；詳見 [驗證紀錄](../research/verification-0.5.58.json) 與 [開發紀錄](development.md)。

0.5.57 不支援 r6 新欄位。此版本的 CI artifact 僅供驗證，未通過原盤驗收前不建立 E:/ipa 的正式交付資料夾或 ZIP。既有交付不改寫。`device_verified=false`，B4 目標／傷害、B5 技能銜接與整場通關皆未驗證。

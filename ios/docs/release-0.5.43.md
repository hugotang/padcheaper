# 0.5.43-orbs：Himmel 迦梨輪替

日服 PAD 23.6.1／107553、arm64，最低 iOS 15。[CI 156](https://github.com/hugotang/padcheaper/actions/runs/42472664547) 已完成完整回歸（含 Himmel 方案與 Session 測試）、iPhoneOS 建置／簽章及模擬器 GUI，已交付本版，編譯來源固定為 `c79f6789de95ae020f58645ac3b52de393eab205`；`device_verified=false`。

- [Himmel 模板](../templates/himmel-gamewith-545672/README.md)的三體迦梨供珠（規則 7–9）改為非必要。0.5.42 真機 log 顯示，輪替錯開、三體都未就緒時，Auto 在仍可消出六色的盤面上停止。現在會略過迦梨，照常以六色目標求路；消不出六色時仍會停止。
- 已匯入的策略不會自動變更，請重新匯入整包，或在策略編輯器把規則 7–9 改為非必要。
- 辛美爾三條敵方對策仍為必要。執行程式與求路器沒有變更；log 中三盤的離線重播結果與真機相同，詳見[開發紀錄](development.md)。

## 驗證與交付

已交付 `E:/ipa/PADCheaper-0.5.43-orbs` 及同名 ZIP。379 份校驗清單、ZIP CRC／380 項通過，交付內模板第 7–9 條確認為非必要。dylib 11,957,440 bytes，SHA-256 `5278783e83c54786287da6c4547496b63d92406d4ae4cc5828be57b123cd2e6f`；ZIP SHA-256 `8b56175ceaa4b0bf493f97042f3db3b8d39b61a3efe6a076cff2724dd9e2f8c8`。Windows 本機核心回歸在同一來源通過。真機上迦梨略過後的六色求路仍待確認。

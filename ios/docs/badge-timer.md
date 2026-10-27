# 徽章與計時原型

2026-10-27；日服 iOS PAD 23.6.0／106878，無 JIT LiveContainer。這些功能已加入 `0.3.0-battle`，仍需真機驗收。

## 徽章

`PHBadges` 讀取 `S+0xA0` 的參數表、`S+0x24E8` 的有界 count，並確認 `S+0xB0` 指向原生初始化的終點 `table+(count+1)*180`。ID n 的參數在 `table+(n-1)*180`，包含 11 個 16-byte 保護值及 +176 的相依徽章。保護值使用和原生 getter 相同的旋轉／XOR，但要求三份副本完全相符。

只解鎖 1–255 內自身 kind 非零、參數可讀且相依鏈無循環／越界的 ID。零值 placeholder 保持停用，不改原生參數。擁有位圖為 `S+0x52F314` 的 8 個 u32，XOR key 為 `0x9A69F65A`。追蹤本次新增 bits，停用或表失效時撤回；原生登入替換 ownership 後丟棄舊追蹤。

`save_decks` 的徽章欄位沿用現有 JSON 保存、預設化及交易回蓋。回蓋只針對隊伍身分匹配的預設值，並按各 bank 內索引 i 寫入 `id XOR uint8(15*i+0x61)`。若表或 ownership 已失效，停止學習該次徽章選擇，保留先前記錄。沒有修改編成持有卡片 ID 或整塊編輯結構。

證據：`decomp-10056c418.c`、`decomp-100453cf4.c`、`decomp-10056ca88.c`、`decomp-10056c3b8.c`，位於本機 `E:/ipa/analysis/pad_23.6.0`。編成快取、關閉功能後行為及重新登入保留仍須真機確認。

## 計時

依使用者指定的 [Android commit](https://github.com/befollowed/PADhelper/commit/13eb1e68db59e50a18c170db93784777616526db) 將自動旗標處理與手動撤窗分開，iOS 欄位及呼叫 ABI 另行核對：

| 項目 | iOS 位置 |
|---|---|
| flags | impl+0xAFEF0，u8 |
| 倒數參數 | *(impl+0xB0F20)，+12 的 signed u16 秒數須大於 0 |
| phase／pending | impl+0x92978／+0x92A00，16-byte 保護值 |
| 退出彈窗 | phase 58、pending 32、*(impl+0xBE9C8) 非零 |
| 暫停標記 | impl+0xB3592 |
| 原生 phase setter | H+0x570B54，(address, u64 value) |
| 原生撤窗 | H+0x61C610，(dialog in X0, float duration in S0) |

自動處理限一般單人 mode 0、multiplayer 0、非 scene 0x80386 且 phase 2／30。設 bit 6；只有 bit 5 同時存在才清 bit 5／1，其他 bits 保留。它不呼叫 phase setter，也不撤窗。hook 在原 drawFrame 前檢查，背景 resume 後強制檢查一次。

面板按鈕只排入一次手動命令，綁定帳號 epoch、地下城／入場 hash、戰鬥物件、倒數物件及彈窗指標。執行前重讀全部條件並檢查指令及記憶體範圍，限 3 秒內的同一個計時退出彈窗。原生關閉彈窗後清除指標／暫停標記，以原生 setter 將 pending 及 phase 設為 2。此選擇對應 `0x100589248` 的 pending=2 返回操作分支；一般提示窗的 pending=2 不會被按鈕提前關閉。

操作無法確認完成時停止後續寫入，不回寫已關閉的資源指標。此原型不支援所有計時模式；它不修改已用時間、上限或通關請求，不能保證撤窗後的結算結果。

證據：`decomp-100589130.c`、`decomp-100589248.c`、`disasm-100589248.txt`、`decomp-10061c610.c`、`decomp-10060d1f0.c`。新增呼叫的 64-byte 指令樣本位於 `battle-23.6.0.json`，由 IDA 匯出。

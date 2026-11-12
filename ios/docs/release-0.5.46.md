# 0.5.46-orbs：武裝失效後 Auto 讀取修正

日服 PAD 23.6.1／107553、arm64，最低 iOS 15。2026-11-12 已通過 [CI 160](https://github.com/hugotang/padcheaper/actions/runs/43170341092) 完整驗證並完成交付；來源固定為 `2e7c4c3608fcb7d204010c243a5018f678fa3d1c`，`device_verified=false`。

- 武裝失效時，原生遊戲可能只降低受保護蓄技、保留較高的舊副本，恢復武裝後差額也可能繼續存在。單人 Auto 改為容許此範圍內的差異，避免在補 L 或求路前誤停。
- 技能只使用真正的受保護蓄技值，仍經原生可用性、來源及操作窗口核對。低於目前值或超範圍的副本、損壞的 protected 值仍會拒絕。8P 與換隊的完全相等檢查不變。
- 讀取失敗新增資料類別與隊員位置；蓄技副本異常會記下兩個值，便於定位後續問題。

本輪已通過失效前後的原生記憶體案例、本體技能與冷卻判斷、補 L 雙讀、蓄技變化撤銷、8P 隔離及 Auto 等待→補 L→下一回合流程，以及既有完整回歸、ASan/UBSan、iPhoneOS 建置／簽章、瀏覽器及四份 iPhone GUI。Windows 同來源共用核心也通過；Windows smoke 沒有 sanitizer runtime。

原 log 未記錄失敗當下蓄技與副本，不能宣稱精確重播該次裝置事故。實機武裝失效、回復與後續回合仍待新版驗收。詳見[調查](orb-assist-charge-2026-11-12.md)與[開發紀錄](development.md)。

## 交付收據

交付 `E:/ipa/PADCheaper-0.5.46-orbs` 及同名 ZIP；394 份檔案雜湊、ZIP CRC／395 項及逐檔內容獨立核對通過，完整結果見[驗證收據](../research/verification-0.5.46.json)。dylib 11,957,856 bytes，SHA-256 `31172ac4ea352c751daabd763ef284d129e239ca171dda5f640f9c7384590e90`；ZIP SHA-256 `ab7ffb33fb9d52f7bfbd58c61840de2916c859e18ff28e55ccd99571e95938c4`。

包內文件是建置時的來源快照，完成結果以 `verification.json` 及 `verification/ci-run.json` 為準。交付檔案不覆寫；這份追加收據不改變已編譯來源。

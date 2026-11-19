# 0.5.57-orbs：珊瑚 B2 不可消回復

0.5.56 的裝置紀錄在 B2 半血後遇到一色不可消，Solomon 尚未就緒，六色求路因而停止。新版搭配 `coral-fixed-585607-v90-r5`，在 B2 收尾加入 AppMedia 的火／木四連回復機制，保留原技能順序與一盤上限。

此分支只支援單人 B2 的指定敵人、單一基本色不可消一回合與普通珠盤面。先讀取當前潛覺、有效二體攻擊與屬性，要求原生掃描的第一組為合資格火／木四顆消除，再以盤面既有珠落下補受阻顏色；其餘五色仍須首消。每個轉轉相位都須通過，這個分支停用 fishing。來源改變或結算後未觀察到不可消清除即停止；觀察清除不等於證明全由潛覺回復。詳見[原生依據與限制](unmatchable-recovery.md)。

安裝後完整關閉並重開遊戲，確認版本 `0.5.57-orbs`，再匯入並選用 [fixed-plan.json](../templates/coral-fixed-gamewith-585607/fixed-plan.json) 的「珊瑚挑戰・固定 Atori・v90 B2 不可消回復」。舊策略不會自動更新。請在自行開始的新場 B1、尚未施放技能或轉珠前啟動 Auto；已消耗且停止的同場不能重啟。

本文件是建置前快照：Windows 可攜回歸已通過；完整 macOS 原生／Session、ASan/UBSan、iPhoneOS arm64／簽章、瀏覽器與四份 iPhone GUI 的完成結果由交付包頂層 `verification.json` 及 `verification/` 記錄。真機回復時序、手機搜尋預算、傷害、耐久與全程通關仍未驗證，`device_verified=false`。

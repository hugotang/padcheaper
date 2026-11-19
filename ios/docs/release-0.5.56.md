# 0.5.56-orbs：珊瑚原生狀態分支

珊瑚 v90-r3 的兩份 B7 裝置紀錄均已完成兩盤原攻擊，但一敵已死、Clammy 恰為半血時便達到原收尾上限。v90-r4 先核對完整敵人名單 `200038/210524`，只有原生確認 `200038` 已死、`210524` 存活且 HP≤50%，才進入最多一盤補刀。原攻擊仍最多兩盤；正常換層可直接前進。吸收／無效未覆蓋、身分未知或改變均停止。

B12 到半血便直接施放 Solomon。B13 保留 Keisy，先取得原生吸收無效效果，再以 Atori→Diena→Riku→Atori 收尾。B14 首盤保留 Keisy、第二盤必要擴盤，其後有界存 Solomon；B15 開場有不可消時必要施放 Solomon，確認已清除才求路。B15 最後 Riku 階段仍最多兩盤，包含存技。這組 B14–B15 銜接採 AppMedia 路線；其餘比較及來源見[攻略與 log 審查](coral-plan-review-2026-11-19.md)。

診斷將每盤求路前、放手前、原生結算後的敵人身分／HP／存活／選取分開保存。結算資料綁定原執行紀錄，只寫一次，不會被停止後閒置讀盤或後續回合覆寫。沒有用 cached 敵方動作推定復活，也沒有模擬潛覺解不可消。

更新後完整關閉並重開遊戲，確認 `0.5.56-orbs`，在策略庫匯入並選用 [fixed-plan.json](../templates/coral-fixed-gamewith-585607/fixed-plan.json) 的「珊瑚挑戰・固定 Atori・v90 原生狀態分支」。新 ID `coral-fixed-585607-v90-r4` 與舊策略並存；舊策略不會自動改寫。須在自行開始的新場 B1、尚未施放技能或轉珠前啟動 Auto；已消耗且停止的同場不能重啟。

本文件為建置來源快照。Windows、macOS 原生／Session、ASan/UBSan、arm64／嚴格簽章、瀏覽器與 iPhone GUI 的實際結果，以同一建置 commit 的 CI 與交付 `verification.json` 為準。實機技能冷卻、傷害、耐久與 B1–B15 全程通關未驗證，`device_verified=false`。

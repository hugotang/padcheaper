# 珊瑚チャレンジ【固定チーム】Atori plan

[攻略來源](https://xn--0ck4aw2h.gamewith.jp/article/show/585607)，按 2026-11-16 的固定隊伍段落整理。

匯入檔：[fixed-plan.json](fixed-plan.json)。此 `padcheaper-fixed-plan` 格式需要 **0.5.49 或之後版本，0.5.48 不支援**。編譯測試結果見交付附帶的 `verification.json`；實機通關仍未驗證。在策略庫匯入並選用，進入遊戲提供的固定隊伍 B1，尚未施放技能或轉珠時，手動開始 Auto。

只操作目前單人戰鬥。不改隊、不入場、不接關、不重新挑戰。停止、背景切換、手動操作或未確認的技能啟動會停止流程；同一場已有原生行動的 plan 不可重新開始，以免重播技能。完成結算後結束。

## 隊伍與技能來源

開場六格：Solomon & Merna #13828、女僕 Diena #13876、Poppy #13744（變身 Keisy #13745）、管家 Riku #13878、Atori #8476、Atori #8476。第五格 Atori 變身時與第一格交換；之後 Solomon 在第五格，Atori 位於第一／六格。每次 Atori 行動按第一格、六格順序查詢原生就緒狀態，只施放其中一個已就緒的指定本體技能。

第二格的學園 Chronos 便當盒 #13619、第四格 Riku 蛋包飯 #13879，開場依序施放並確認消耗。其他四件未施放的武裝用 `assist: null` 表示在 B1 綁定原生來源；沒有猜填圖片中的武裝編號。綁定後任何來源變更都會停止。消失武裝與敵人的暫時武裝無效分開處理。

Solomon 的技能進化根 ID 是 63190，真正施放的第一／後續階段是 63191／63192。所有技能都核對當前本體／武裝與技能 ID，不把滿蓄武裝代作指定本體。

## 逐層流程

「攻擊」＝首消六色＋心四連，優先增加實際四顆消除組數。「存技」＝首消六色、既有盤面所有確定消除皆無四顆組。「L」＝火／木／光其中一色。未來天降不在求路器可保證的範圍。

| 樓層 | 流程 |
|---|---|
| B1 | Diena 武裝→Riku 武裝→1C；1C；第五格 Atori 變身→助人 Atori 變身→Keisy 變身→1C；Solomon 第一階段→Atori→攻擊；Atori 攻擊兩回；Atori→Diena→Atori→Solomon→Keisy→攻擊離層。 |
| B2 | 利用轉轉消六色、無四顆存一回；Atori 攻擊。 |
| B3 | Riku→Atori 攻擊；Atori→六色，整段操作最多 4.5 秒，對應攻略的五秒達人多色。 |
| B4 | Solomon→Keisy→Atori 攻擊；Atori 攻擊。 |
| B5 | Atori 攻擊兩回；Solomon→Diena→Keisy→Atori→六色無四顆；Atori 攻擊。 |
| B6 | Atori 攻擊。 |
| B7 | 指定 Clammy（圖鑑 #10424／原生 #10524）→Solomon→Atori 攻擊；Atori 攻擊。 |
| B8 一般 | Keisy→Riku→Atori 攻擊；開始 Keisy 原生就緒循環。 |
| B8 亂入 | 指定唯一存活部位→Atori→六色、心四連及 L；Atori 攻擊；Riku→Atori 攻擊。離層前需觀察部位破壞。 |
| B9 一般 | Keisy→Atori 攻擊；Atori 攻擊；開始／延續 Keisy 循環。 |
| B9 亂入 | 指定部位→Atori→六色、心四連及 L；Atori 攻擊三回。離層前需觀察部位破壞。 |
| B10 | Atori→Diena→Atori 攻擊。 |
| B11 | Riku→Atori 攻擊；Solomon→Atori 攻擊；Atori 攻擊。 |
| B12 | 只消一組 L；Riku→Atori 攻擊破盾；Atori 攻擊兩回；Solomon→Atori 攻擊，至多再一盤 Atori。 |
| B13 | 若 Solomon 尚未就緒，Atori 六色無四顆存技；就緒後 Riku→Atori→Diena→Atori 攻擊，開始 Diena 循環。 |
| B14 | Atori 攻擊；Solomon→Atori 攻擊；Atori 攻擊直到離層（有上限）。 |
| B15 | Riku→Atori 攻擊；攻擊至本體 HP≤50%；Atori→L、六色及心四連；攻擊至本體 HP≤25%；Riku→Atori 收尾。吸收未被我方效果覆蓋時，嘗試已就緒的 Solomon，否則以六色無四顆存技。 |

B8／B9 分支只在進層時選定，部位破壞後不會中途切成一般分支。B12 最後的兩盤共用同一個「Solomon 只施放一次」步驟。B13 永遠查詢 Solomon 的當前就緒狀態，不從 B12 的耗用回合數推算。

## 尚待驗證

- 實機固定版本的隊員來源、技能 ID、隊長交換與兩件消耗武裝。
- 敵人目標點選座標、下一幀選取回覆，及部位死亡到換層之間能否取得明確破壞回覆。
- 所有樓層傷害與回血能否符合攻略；本 plan 沒有傷害／回復模擬器。
- B3 五秒條件、帶轉轉的六色／L／四顆路線品質與真機幀率。

JSON 的 dungeon／floor 為 0，因為文章未提供原生關卡 ID，沒有猜填。靠手動選用與完整開場隊伍核對開始；如有此關實機診斷，可再將原生關卡 ID 固定。

產生器：[generate_coral_fixed_plan.py](../../tools/generate_coral_fixed_plan.py)；[來源與採用的處理方式](strategy.reference.json)、[靜態核對紀錄](validation.json)。不要把目前的靜態檢查或已撰寫的案例當作編譯／通關驗證。

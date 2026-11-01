# 敵人選招的條件分支分析

0.5.15 驗證更新：本頁已實作部分的模型／原生／session 案例與 iOS 編譯已由 [CI 90](https://github.com/hugotang/padcheaper/actions/runs/41283642862) 通過。整包另通過編隊瀏覽器及四份 GUI；GUI 覆蓋設定和模擬 L／充能文字，未逐層驗證敵人分析子層，真機仍待驗收。以下「尚未建置／執行」文字保留提交當時紀錄。完整下一擊／減傷／回血及耐久決策仍未完成，結果與限制見 [開發紀錄](development.md)。

2026-11-01，**源碼與案例已寫，尚未編譯、執行測試或真機驗證**。接續 [行動定義與目前 AI 狀態](enemy-action-observation.md)，本批分析普通單人行動列的條件分支，為 [敵人感知自動模式](enemy-aware-automation.md) 建立中間資料。0.5.14 不含本批。

後續已另加 [選招入口與條件檢查點](enemy-dispatch-analysis.md) 源碼及案例。每個候選可附獨立的 `postSelectionCheckpoint`，假設它由 fresh selector 返回後推演到招式準備之前；本頁的 row-zero 候選與 `aiStateProjected=false`／`nextEntryKnown=false` 含義不變。實際我方行動後狀態尚未確認。

## 分析的含義

`battleObservation.enemyActions.enemies[].selectorAnalysis` 回答：「假設原生選招器由第 1 列進入，使用本次 AI 旗標／計數／level／budget，敵方正規化 HP 百分比可為 0 至 100，哪些行動列可能被選到？」**這不是下一回合預測**。實際入口可能走快取、複合續招、特殊／override 或其他起點；我方行動和前一隻敵人也可能改變狀態。本批沒有證明這些前提在下一回合成立。

| 輸出 | 意義 |
| --- | --- |
| `conditionalWalkComplete=true` | 在上述假設內，支援的列分支已走完，沒有遇到未知或上限；不代表實際招式必定可用 |
| `candidateSet=conditional-conservative-superset` | 保守保留候選，可能多列入招式；沒有計算機率或 native feasibility |
| `candidateSet=partial-with-unknown-frontier` | 遇到未知控制、缺定義、非法跳轉、重複狀態或上限，剩餘部分未明；候選不能用來排除危險招式 |
| `nativeHPPercent` | 該候選的區間與兩端是否開放，例如 `(20,100]`；不以消珠前 HP 刪除低血招 |
| `flagsAtReturn`／`counterAtReturn` | 條件遍歷到選招返回時的值；尚未套用 dispatcher 的後續更新 |
| `noSelectionRetained` | 支援的分支可能沒有返回 action ID；**仍可能普通攻擊** |
| `actionSequences` | 可識別效果的定義順序；不是已證明會執行的事件清單 |

總層 `selectorAnalysis.status=ready` 只表示新指紋通過、可以嘗試分析；各敵人仍須分別看 `status`、`conditionalWalkComplete`、`issues`。新指紋失配時只撤下候選，保留可用的原始行動診斷。既有 `enemyForecastKnown`／`actionReachabilityKnown`／`damageBoundKnown`／`survivalKnown` 一律 false。`nextEntryKnown`／`aiStateProjected` 亦固定 false，不能據此延後必要解覺醒或授權耐久。

## 已建模的流程

純 C++ `PHEnemySelector.h` 使用本次已收集的圖與 64 列，不新增遊戲資料讀取。支援一次旗標立即返回、set／clear／OR／XOR、計數替換／增減、HP／level／unsigned counter 條件、兩種目的列基準和 pass 結束。旗標條件是任一位符合；加計數保留原生 unsigned `<=0x7ffffffe` 限制。HP 分支保留等號及開放端點，不以整數 HP 百分比近似。

有序加權模式保留主候選與最後的加權池；舊式兩輪模式第二輪從第 1 列再走，繼承第一輪的旗標／計數和 HP 區間。第二輪特別判斷的是 **action ID 36**，不是只看 opcode 36。opcode 47／49 的兩種模式差異與 signed cost 過濾已按原生分支處理。普通葉節點目前只核對 47／49／82／83／88／141 的選招流程，其餘保留未知；可選到某列不代表已理解該招傷害。

沒有呼叫 RNG、選招器、可用性函式或其他 native helper，也不臆測乘法溢位、取整與機率。在已核對的普通葉節點上保留「可能選取」及「可能跳過」，其他敵人的同類排斥也只作可能拒絕，因此結果可以含實際不會選到的候選。加權池保存列集合，同時按每次加入累加容量；即使重複同列也計入原生 64 格，超出後停止並標 unknown，不能用去重掩蓋原生越界前提。

每隻敵人最多 512 個狀態、256 組候選，整次最多 4096 步；多敵人共用步數預算。完整狀態去重包含 row、pass、旗標、計數、HP 區間及加權池／容量。重複狀態保守標為未完整，即使未證明它一定是無限迴圈。這些是輸出與遍歷上限，不是實機耗時保證。

## 複合招式及後續工作

opcode 83 只取第一個非正值之前的最多八個子 ID，保留重複及順序；巢狀不攤平，循環、缺定義、未知效果與八個全正 ID 都不能宣稱完整。對 88／141 保留「正的 +68 附帶攻擊 → 覺醒無效／武裝無效嘗試」，只列原始參數，不推斷有效回合或傷害。子 82 由原生複合準備流程跳過；根 82 轉成負選招結果，保留普通攻擊 fallback，兩者不可混為零傷害。

詳細頁顯示條件分支已分析／未完整的敵人數與候選組數，並保留下一招和耐久尚未確認的提示。舊資料隨 observation 失效撤下，沒有跨回合快取授權；3／4／8 人及 3 人單人續玩仍不取樣本套資料。

27 組新程式指紋獨立於 114 操作＋15 舊診斷＋45 L 政策＋32 行動診斷，原有指紋不變。它們保護本批引用的選招／跳轉／機率入口／fallback／事件先後片段；不是完整程式或動態資料驗證。下一次建置才產生更新的 header。

已寫 model／native／session 案例：旗標與 unsigned 計數、HP 相等和矛盾分支、兩輪繼承、ID 36／opcode 36 差異、成本與候選來源、未知／迴圈／容量、跨敵人步數、快取 ID 不混入、重複子招／82／未知複合、指紋失配隔離、資料撤下與多人排除。全部待執行；[真機要點](device-testing.md) 和 [原生研究與靜態核對](../research/orb-enemy-selector-20261101.json) 記錄範圍。

下一步先建立實際 dispatcher 入口與我方行動後 AI 狀態的證據，再處理完整傷害事件、減傷重算／到期、回復先後與後續技能可用性。之後才可接 L＋心四耐久一回合，或在武裝門檻前安排解覺醒技能。

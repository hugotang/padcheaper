# AutoCombo 可借鑑的求路設計

唯讀檢視 [taivsallen/AutoCombo](https://github.com/taivsallen/AutoCombo/tree/f1f736441adcb5a3826205241c20d196bf83e12b)，固定 commit `f1f736441adcb5a3826205241c20d196bf83e12b`。沒有執行上游程式或安裝其依賴。此版本沒有 LICENSE，GitHub license 欄位亦為 null；本版只研究方法，沒有將其程式複製進 PADCheaper。

最值得優先試驗的是**已有可行解後的有限縮路**，其次是排序快取及有獨立驗證的配對量測。不是直接把其他遊戲的評分函式換過來，也沒有證據需要先換神經網路。

| 方法 | 上游可查依據 | PAD 適配判斷 |
|---|---|---|
| 刪前段／尾段、迴圈、直接接合，再試兩步橋接 | [pathRefinement.js](https://github.com/taivsallen/AutoCombo/blob/f1f736441adcb5a3826205241c20d196bf83e12b/src/solver/pathRefinement.js) | 可作獨立實验。每個候選從不可變的原盤重播，要求真實目標不退、嚴格更短，沿用同一截止時間。PAD 還須重算荊棘接觸／HP、固定起點、封條、全轉轉相位、首消心四連與毒爆彈，不能只比較 Combo。 |
| 快取候選的排序向量，少建物件 | [solverCore.js](https://github.com/taivsallen/AutoCombo/blob/f1f736441adcb5a3826205241c20d196bf83e12b/src/solverCore.js)、[效能記錄](https://github.com/taivsallen/AutoCombo/blob/f1f736441adcb5a3826205241c20d196bf83e12b/reports/solver-next-20260909/README.md) | 先在本項目 profiling；現有 C++ 固定陣列／bitset 已避開許多 JS 配置成本，不能把上游百分比直接套用。 |
| 初始化、搜尋、縮路、收尾共用預算 | [performanceProfiles.js](https://github.com/taivsallen/AutoCombo/blob/f1f736441adcb5a3826205241c20d196bf83e12b/src/solver/performanceProfiles.js) | 應保留目前 120／350／800 ms 絕對期限。到期保留最後已驗證可行解，不能另加一段無上限縮路。 |
| 獨立重播、固定基準及保留盤配對 | [pathReplay.js](https://github.com/taivsallen/AutoCombo/blob/f1f736441adcb5a3826205241c20d196bf83e12b/src/solver/pathReplay.js)、同上效能記錄 | 值得沿用。分開報目標達成、合法性、同品質步數、時間尾端及退步盤；重複同一盤不能算多個獨立樣本。 |

原盤重播是縮路的關鍵：刪掉一段走法會改變沿途珠子的位置，即使幾何上接得上，也不能假設終盤相同。可先只收「全目標與首消／疊消 Combo 分組相同、危害不增加、嚴格縮短」的保守改良；有縮路能力不代表整個限時版本在每盤都更短，因為可用搜尋時間也被改變。

## 上游數據的範圍

上游記錄為 Web 核心、30 個盤面各三次，90 組配對、160 ms 預算：中位時間 133.001 → 110.547 ms，P95 147.795 → 142.140 ms；全需求達成率 30% → 30%，共同分組平均步數 11.489 → 11.489，最佳解品質勝／負／平 6／0／84。這些是**上游報告，沒有在本機重測**，亦不是 PAD 或 iPhone 結果。報告自己沒有宣稱平均縮步或全面最優。

此外，所檢視 commit 的 [App.jsx](https://github.com/taivsallen/AutoCombo/blob/f1f736441adcb5a3826205241c20d196bf83e12b/src/App.jsx) 有自己的 async beam 求解器，沒有引用 `refineSolverPath`；不能因獨立模組和歷史報告存在，就宣稱目前 UI 正在使用它。[Swift SolverEngine](https://github.com/taivsallen/AutoCombo/blob/f1f736441adcb5a3826205241c20d196bf83e12b/ios/AutoCombo/AutoCombo/Core/SolverEngine.swift) 也屬較簡化的 beam，許多需求靠分數表示，不能取代 PAD 的硬性目標檢查。架構文件中的 A*／IDA*／神經網路建議不是已交付功能證據。

## 本次採取的範圍

0.5.8 已有獨立的荊棘零接觸補搜、Combo 偏好與各次搜尋診斷，見 [求路比較](orb-shape-search.md)。本次沒有再擴張成 AutoCombo 縮路移植。後續可先以原盤重播的局部縮路做同預算 A/B，包含一般盤、T／四連、多轉轉、荊棘及已知可解難盤；確認目標達成率沒有被縮路預算拖低，再決定是否加入正式求解器。

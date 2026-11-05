# Auto 執行狀態與生命週期

2026-11-05；可維護性第二批，納入 [0.5.35](release-0.5.35.md)。編譯與驗證結果見 [開發紀錄](development.md)。

## 責任分工

| 檔案 | 負責內容 |
|---|---|
| [`PHOrbSessionState.h`](../src/PHOrbSessionState.h) | 不依賴 Foundation 的流程列舉、Auto 執行狀態、停止意圖判斷與 8 人倒數啟動紀錄。 |
| [`PHOrbAutoLifecycle.inc`](../src/PHOrbAutoLifecycle.inc) | 開始、停止、前背景中斷、等待穩定盤面及倒數自動啟動入口。 |
| [`PHOrbAutomationSession.inc`](../src/PHOrbAutomationSession.inc) | 單人策略綁定、開場／規則進度、等待技能／下一回合、原生幀中的技能施放。 |
| [`PHOrbEightSession.inc`](../src/PHOrbEightSession.inc) | 8 人固定隊伍、技能回執、同場接續與活動策略。 |
| [`PHOrbSession.mm`](../src/PHOrbSession.mm) | 原生幀調度、求路工作取消、預覽、輸入所有權與放手、設定及對外快照。 |

`.inc` 仍在同一個 `PHOrbSession` implementation 內編譯，沒有新增執行緒、物件代理或跨模組呼叫；這是依責任分檔，尚不是完全獨立的控制器。`beforeAutomationFrame:time:` 保留原來同步執行與提前返回的順序；本幀處理技能後，不會接著進入拖珠輸入。

## 單一狀態來源

`PHOrbSessionPhase` 取代內部文字狀態。預覽、轉一次與 Auto 共用同一份 phase；不另外保存 Auto phase 或文字鏡像。`PHOrbSessionPhaseName` 只在 UI／診斷輸出時轉回既有名稱，例如 `autoWaiting`、`skillWaiting`、`releasing`。既有 JSON 欄位與 UI 使用方式保持相容。

`PHOrbAutoRunState` 將模式、戰鬥身分、綁定狀態、回合／轉珠／技能計數、開始／等待時間及消除觀察集中管理。模式只有停止、單人、8 人；`active()` 與 `eight()` 從同一列舉推導，避免兩個布林值互相矛盾。這些只是 Session 帳目，不能授權原生技能或手勢。

## 重置與保留

| 事件 | 本次執行狀態 | 跨停止保留的證據 |
|---|---|---|
| 單人開始 | `beginSolo` 全部重設，從第 1 回合等待綁定。 | 開場、一次性技能及實際回合紀錄仍由既有身分／策略規則管理。 |
| 8 人新場 | 新場分支先 `resetProgress`；原生 arm 成功後才 `beginEight`。 | 新場分支重設該場技能、提交與準備回合紀錄。 |
| 8 人同場接續 | `beginEight` 更新綁定與等待時間，保留已確認計數及消除證據。 | 技能回執、序列、提交盤面及一次準備回合仍需原有規則重新核對。 |
| 停止 | `stop` 只撤銷模式；立即撤銷 engine 操作資格。 | 保留計數、原身分、已啟動技能與原有停止診斷，供同場接續／唯讀觀察。 |
| 設定重載／停止 | 不清除 `PHOrbEightAutoStartState`。 | 同場倒數自動開始的使用紀錄獨立於執行狀態。 |

倒數入口先確認設定、讀取、前景、總開關及沒有其他執行意圖，`claimCountdown` 再核對原生 8 人身分、倒數、本機操作窗口及尚未轉珠。自動嘗試與合資格的手動開始都在原生 arm 前消耗該場紀錄；因此原生拒絕後也不會因設定開關、停止或後續觀察自動重試。新場身分仍沿用 `PHOrbSameEightMatch`。

停止與拖珠清理分開：`Releasing` 仍算正在執行，讓已擁有的手勢完成放手；取消後的放手不算新意圖。重複前背景通知不覆寫原停止原因或增加取消次數。求路回呼仍需符合原 generation 和 phase，不能復活已停止的執行。

## 行為邊界與待驗證

沒有新增 3／4 人 Auto、換隊權限、封包／時計修改、排隊或重賽。8 人每次技能／手勢仍經原生本機操作窗口與固定來源核對。策略觸發條件、冷卻、技能回執、求路演算法、持珠與放手實作保持原有邏輯。

新增的純模型案例涵蓋重置／保留、狀態輸出相容、倒數條件與一次性使用；Session 案例涵蓋手動／倒數原生 arm 失敗、設定重載、下一場重啟及 Auto 持珠中停止。已接入現有 Windows／macOS 測試入口，納入 0.5.35 本輪驗證。

完整 Session 回歸涵蓋開場接續、技能窗口、改盤等待、逾時、延遲求路回呼、8 人同場技能回執與停止後放手；另需 iPhoneOS 編譯及 iPhone GUI。結果見開發紀錄；C++ 語法與原始碼搬移比對不等於 Objective-C++ 編譯或行為測試，真機多人同步亦待驗證。

第三批 [診斷序列化與版本／交付資料](diagnostics-release.md) 一同納入 0.5.35。

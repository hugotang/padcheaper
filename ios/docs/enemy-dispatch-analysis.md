# 敵人選招入口與條件檢查點

2026-11-01，**源碼與案例已寫，尚未編譯、執行測試或真機驗證**。接續 [條件選招分支](enemy-selector-analysis.md)，本批補齊部分原生排程入口及選招後的數值更新。0.5.14 不包含本批；完整 [敵人感知自動模式](enemy-aware-automation.md) 仍待實作。

## 這次新增的資料

`battleObservation.enemyActions.dispatchAnalysis` 表示獨立指紋與額外資料是否可用；各敵人的 `dispatchAnalysis.current` 回答：「假設現在呼叫排程器，而且其餘敵人或 hook 沒有先改變這份狀態，這隻敵人走哪個入口？」它不是下一回合必定使用的入口。

| 欄位 | 意義與限制 |
| --- | --- |
| `current.route` | 目前狀態下的條件入口：倒數未到、原生旗標阻擋、普通攻擊、複合續招、快取、特殊模式未明、override、另一起始列或第 1 列 |
| `conditionalRouteClassified` | 分類到了上述原生分支；不表示已證明會呼叫它，亦不表示已知該招執行／傷害 |
| `afterConditionalTick` | 假設回合更新函式確實到達，外部提早返回條件沒有阻止，且其餘捕獲值不變，套用一次倒數與快取更新 |
| `selectorAnalysis.candidates[].postSelectionCheckpoint` | 假設該候選已由 fresh selector 返回，計算準備招式之前的行動數、受保護計數、AI 點數與 packed compound 值 |
| `postSelectionCheckpoint.status=partial-prefix` | 已知計數和回充前綴，但缺少選中定義，不能把讀不到資料視為原生 lookup 失敗 |
| `postPlayerStateKnown`／`nextEntryKnown` | 固定 false；未模擬我方傷害、技能、敵人變階段或前一隻敵人的影響 |

原本行動表與 row-zero 分析保持各自含義。即使入口診斷顯示快取或倒數，原本的候選仍只是「假設第 1 列起步」的分析；本批不把它轉成實際下一招集合，也不因當前倒數大於 1 就宣稱下回合安全。`enemyForecastKnown`、`actionReachabilityKnown`、`damageBoundKnown`、`survivalKnown` 仍為 false。

## 已核對的時序

排程器 `0x10056262c` 先按快取 byte 的最低位，對非複合且 interpolation 兩個 float 相等的敵人把 selected 設成 -1。浮點比較保留 `+0 == -0`、NaN 不相等的原生語意。接著按槽位順序，檢查 parts、HP／生存旗標、signed countdown `<=0` 與 flag `0x8`；這個選招準備順序還不是完整承傷／效果順序。

排程前 hook byte 恰為 1 時可能改動狀態，本批停止深入分類。關卡指標為空或 `+24` 的 `0x80` 未設置時，原生直接保留普通攻擊；它不是「不行動」。通過後 dispatcher `0x100561c4c` 依序處理複合續招、`cache byte == 1 && selected >= 1`、特殊模式 predicate、普通兩種 selector。快取的 **bit test 與 exact-one test 不同**。特殊 marker `986027508` 命中時保留未明，不呼叫依賴全局狀態的 predicate；沒有命中才可排除該分支。

兩種 selector 都先處理 signed `enemy+2032 > 0` 的負返回、`+2336 != 0` 的 signed-short `+2352` override，再處理非負 special ID 與 flag 2。ordered special 可返回另一個參數 ID，故只分類、不捏造選中值；legacy 在這條分支返回 -1。

ordered 的 opcode180 redirect 從受保護 slot／action／byte 唯讀判別，不呼叫會清狀態的 helper。命中本敵人時，flag `0x2` 選 param6，否則 param7；值 1～64 表示另一個起始列，>64 為負返回，<=0 保留未知。這一批不從另一起始列重新執行 row walker；其他 slot 或已確認非180定義才可排除此 redirect。未知定義不視為未啟用。

回合更新 `0x1006061fc` 先清 cache byte，再經外部條件後，對非 parts、HP>0 或 survival byte bit0 的敵人，僅當 countdown>=1 才扣一次。這裡 HP0 的條件比排程器寬，兩者不可共用。loop state `525190` 跳過扣倒數但已清快取。函式有多種 caller／提早返回，**一次玩家轉珠必定等於一次 tick 尚未證明**。這裡不推演其他 status duration。

fresh selection 後先按 `(protectedCount >> 5) <= 0x752` 加一，等價於本範圍最多加至 60000；未能確認該計數時只留它為未知。uint32 行動數加一保留 wrap。AI budget 使用 unsigned16 當前值，加 signed16 回充，按照原生 signed CMP／CSEL 的次序 clamp 到 signed16 上限後存回 u16；上限負數亦保持原次序。正 ID opcode82 在這之後轉 -1，未扣成本；其他已知非負 ID 扣 record+64 的 **low u16** 並 wrap，再寫 `(uint32 ID << 9) | 0xf0`。這與 selector 用 signed32 cost 判斷的語意不同。

上述 checkpoint 停在普通／複合招式準備 helper **之前**，不是完成招式後狀態。ID0 可以指向 native table 的第0筆；live graph 未收錄時只輸出前綴，不能假定無招。負返回、空列與未選中依舊保留普通攻擊可能。

## 為甚麼尚不能預判耐久

`0x100563adc` 的我方傷害可啟動生存／special 分支、改 countdown；`0x1005639c8` 的 HP 門檻可掛上變階段狀態，`0x1005b3498` 隨後可能改 countdown、清 selected。玩家技能也有倒數寫入。因此觀察當下的旗標與倒數不保證在下一次選招前仍相同。源碼搜尋並未證明所有 AI 寫入已找到，不能用「暫未看到其他 writer」作不變性證據。

本批沒有放寬 required 解覺醒／不可消，沒有排程預放本體技能或 L＋心四耐久。下一步要處理我方行動後的 HP／生存／變階段分支，以及普通攻擊／複合準備的完整事件，再核對減傷與回血先後。

## 讀取與後續驗收

39 組指紋放在獨立 `PHOrbEnemyDispatchTextGuards`；原有 233 組 orb 指紋保留。新欄位為 cache／hook／special marker／loop state／redirect block；extra capture 另讀 dungeon flag、survival byte、interpolation 及 protected count。每遍最多 4096 bytes／64 次資料讀取，與原本 action graph 的預算分開。這是上限，不是效能驗證；程式指紋與既有 board／identity 覆核不計入這個資料子預算。

前後兩次捕獲比較原始 extra words、私有 pointer identity、redirect record 與解碼值；沿用整體入場／金鑰／phase／wave／moves／input／board 最後覆核。新讀取或新指紋失敗只撤下入口和 checkpoint；原本 action graph／selector 候選仍可保留。共用狀態改變則整組撤下。沒有把新資料放入每幀複製的 snapshot，也不新增原生函式呼叫、寫入或自動決策。

已寫 model／native／session 案例，涵蓋入口優先次序、倒數／HP0差異、signed／unsigned算術、82／缺定義、redirect、float 比較、單獨失配／讀取失敗、雙讀競態、舊資料撤下和全部多人排除。**案例尚未執行**，generated header 在下一次明確建置時才更新。詳見 [真機清單](device-testing.md) 與 [原生研究／靜態核對](../research/orb-enemy-dispatch-20261101.json)。

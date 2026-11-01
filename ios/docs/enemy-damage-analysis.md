# 敵人受傷後的根性與變階段檢查點

2026-11-01，**源碼與案例已寫，尚未編譯、執行測試或真機驗證**。接續 [敵人選招入口](enemy-dispatch-analysis.md)，本批建立受傷後局部欄位的值型模型和獨立唯讀診斷。0.5.14 不包含本批；完整 [敵人感知自動模式](enemy-aware-automation.md) 仍未完成。

## 目前可觀察的內容

`battleObservation.enemyActions.damageAnalysis` 記錄本組指紋及讀取狀態。每個敵人的 `damageAnalysis` 保存根性／超根性門檻、原生 HP 快照、存活／變階段旗標、額外 HP 模式及傷害累加到 override 的設定。快照 `enemy+120` 與目前 HP 是不同欄位，數值相同也不證明它會成為下一次攻擊的起點。

| 欄位 | 範圍與限制 |
| --- | --- |
| `snapshotHPCurrent`／`secondarySnapshotHPCurrent` | 原生目前保存的兩份 signed64 快照；不替換成當前 HP |
| `normalThresholdHP`／`superTriggerHP`／`superFloorHP` | 按原生浮點次序求出的門檻；超出已支援轉換範圍時為 null |
| `probes` | 三個獨立假設：有效傷害為 0、扣至 1 HP、扣至 0 HP；不是完整傷害分支集合 |
| `hpBeforeSurvival`／`phaseBeforeSurvival` | 扣 HP 與 HP 變階段檢查之後、根性保留之前的已知局部前綴 |
| `localFieldsKnown` | 在該假設下能算完模型所列欄位；不能當成完整回合已知 |
| `ifBatchSnapshotRoutineRuns` | 假設呼叫原生攻擊批次快照流程後，兩份快照、survival byte 與相關 flag 的局部結果 |
| `ifCleanupRoutineReachesEnemy` | 假設全體清理流程處理到此敵人的局部效果；不代表已模擬其他敵人 |
| `ifPhaseScanReachesEnemy` | 假設掃描走到此格且前面未有目標／提早返回，檢查 HP 旗標及原生 active byte 合計條件 |
| `ifSelectedTargetAnimationCompletes` | 假設此敵人確為原生目標且等待完成，計算 countdown／selected／phase 欄位 |

模型入口要求「傷害解析器已返回，而且允許進入 HP 結算」以及解析器之後的狀態。實際解析器仍未建模；診斷 probes 明示假設捕獲欄位未被它改動、吸收的 skip 未發生及外部 callback 不改模型欄位。正傷害遇到目前存在的 HP callback 時，直接保留已知前綴，後續為 unknown。

`scenarioCoverageComplete`、`postPlayerStateKnown`、`nextEntryKnown`、`enemyForecastKnown`、`actionReachabilityKnown`、`damageBoundKnown`、`survivalKnown` 固定 false。這些結果不傳回現有 selector、倒數推演、技能、救援或輸入決策。

## 已核對的原生次序

`0x100563adc` 先經特殊模式判定和 `0x100562f74` 傷害解析。解析器處理減傷／吸收／無效等，也能直接改 HP 或把傷害累加到 `enemy+2336` 的 override；因此不能以隊伍原始傷害直接扣血，也不能假定捕獲的下一招 override 原封不動。

普通 HP 分支在 native 64-bit subtraction 後作 signed clamp，再呼叫 HP 變階段 helper，然後才處理超根性和一般根性。額外 HP 模式（protected signed-byte `+2704 > 0`）另有路徑；除非明確的已解析攻擊 descriptor 表示繞過它，模型保留未知。原生函式完全不由診斷呼叫。

一般根性 `0x10056463c` 使用 u16 `+2892`，按 `double(百分比) / 100 * 最大HP` 計算門檻，要求保存快照 `+120 >= 門檻`。超根性 `0x1005645b0` 要求快照 **嚴格大於**觸發門檻，以及扣血後 HP **嚴格小於**保留量。觸發百分比在 `+2894`、保留百分比在 `+2896`，原生先乘最大 HP 再除 100；非零 u64 `+2904` 經 unsigned-to-double 取代兩者。兩種算法的運算次序保持分開。

門檻最後呼叫 `0x10004818c`，正數加 0.5 再截成 signed64。大整數轉 double 的精度損失保留；結果達到 `2^63` 或非有限時不執行 C++ 越界轉換，也不推測 ARM 的飽和值。HP 變階段則是 double 比例乘 100 **縮窄成 float**，再作原生的截整／加一；不能用整數比例比較替代接近邊界的結果。

已設 survival bit8 時會直接重設超根性 HP；已設 bit1 時，接著強制 HP1。原本致命的分支若 special ID 非負且關卡啟用敵人行動，會設 bit1、flag532 bit2、countdown0、HP1，再檢查一般根性。一般根性自身不改 countdown。immediate byte **恰為 1** 時會全體清理並重寫快照，後續一般根性比較使用重寫後的值。

`0x10060a55c` 清理只處理 active byte 恰為 1 的敵人。bit1 與 flag2 同時存在時整個敵人跳過，連 bit8 也保留；否則先清 bit1 再清 bit8。immediate **最低位為 0** 時分別加 flag `0x20`／`0x80`，每次清理後重新檢查 HP 變階段。這與上一段 exact-one 判斷不同。模型只計算局部數值效果，不模擬 animation 或跨敵人影響。

攻擊批次流程 `0x10060583c` 把 protected HP 複製到 `+120`／`+128`、清 survival 所在 dword 及 flag `0x80`。已找到 `0x10060a758` 在等待／清理後呼叫它；還不能從一次玩家轉珠推出呼叫次數或所有快照 writer，所以只提供獨立條件檢查點。

變階段除了 HP，`0x10060d1f0` 還把**全部敵人格的原始 unsigned active byte+1 相加，包含 parts**，與 signed16 `+2968` 比較。它不是 HP 百分比，也不把每格強制轉 bool。living nonparts、phase bit `0x20`／`2` 都未設且合計 `<=` 門檻，才加 `0x11`；exact immediate1 再加4。之後第一個已標記而未處理的敵人可以成為 phase19 目標。本批各格條件分析不宣稱實際目標。

假設目標 animation 等待完成，`0x1005b3498` 沒有 bit4 時把 countdown 設 0；有 bit4 時，count 分支取 signed16 `+2970`，HP 分支取 unsigned16 `+2914`。selected 清成0、phase 加8；其他 interpolation／動畫欄位與全域 phase19→20→2 時間線仍未建模。

## 讀取、隔離與待驗證

54 組新指紋放在獨立 `PHOrbEnemyDamageTextGuards`，原有 272 組 orb 保留。每遍額外資料最多 4096 bytes／64 reads；八格完整讀取用 1983 bytes／46 reads，不含程式指紋與既有共享盤面核對。這只是靜態上界，尚未量測效能。

額外資料完整雙讀比較，沿用入場、來源、key、phase、wave、moves、持珠／input 及最後盤面核對。只公開數值，不公開程序指標、protected words 或 keys。新診斷失配只撤下 `damageAnalysis`；共用身分／盤面失效則整組 observation 撤下。資料不加入每幀 snapshot，3／4／8 人及 3 人單人續玩都不啟用本組。

已寫 model／native／session 案例：根性等號、重複攻擊、phase 先於回復、immediate 差異、特殊 countdown、浮點邊界／越界、signed interval、讀取上限、指紋與競態隔離、撤下舊資料及多人排除。**未執行案例或 generator**；下一次明確要求建置時才更新 generated header。詳見 [原生研究與靜態核對](../research/orb-enemy-damage-20261101.json)、[待驗收清單](device-testing.md) 及 [開發紀錄](development.md)。

下一步要從實際攻擊序列建立解析器之後的狀態，連接選招／準備事件，再核對減傷有效期、回血先後與技能截止回合。現階段仍不能據此延後必需救援、防跳武裝或啟用 L＋心四耐久。

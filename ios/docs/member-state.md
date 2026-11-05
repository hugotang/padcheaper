# 隊員來源與有效狀態

2026-11-05；本批納入 [0.5.35](release-0.5.35.md)，適用 PAD 23.6.0／106878。編譯與驗證結果見 [開發紀錄](development.md)。

## 共用介面

[`PHBattleMemberState.h`](../src/PHBattleMemberState.h) 定義純 C 狀態及比較規則，不依賴 UIKit、遊戲記憶體或原生函式。[`PHBattleMemberReader.h`](../src/PHBattleMemberReader.h) 提供有界的唯讀適配器，讓戰鬥換隊的整塊快照與技能讀取的逐欄位記憶體存取使用同一套解碼。所有 protected form 欄位仍要求三份資料一致，失敗不發布部分結果。

| 欄位 | 意義與用途 |
|---|---|
| `sourceBodyID`／`sourceAssistID` | 原生保存的本體／武裝來源；用於配裝所有權、還原和同進化鏈二選一資格。武裝禁用或消耗時來源可能仍存在。 |
| `currentCatalogID` | 當前實際形態的圖鑑 ID；技能與能力核對以此為準。保留 32 位，避免截斷 8P 固定角色的特殊 ID。 |
| `effectiveAssistID` | 目前實際生效的武裝；零不代表原隊伍沒有配置武裝。 |
| `persistentFormID` | 持續變身後的本體 ID；沒有時底層本體為 `sourceBodyID`。 |
| `temporaryFormID`／`temporaryTurns` | 原生暫時變身及剩餘回合；正回合時必須與當前形態相符，才可推導規劃用底層身分。 |
| `PHBattleDungeonMemberRules` | 關卡規則指標、`+33` 原始旗標及讀取是否確認。`+32` 的技能／隊長規則與暫時敵方效果不混入此結構。 |

`PHBattlePlanningIdentity` 只維持已選策略的連續性，不授權施放底層角色技能。圖鑑有效性、8P 專用參數位置、成員來源席位、技能冷卻及 native can-use 仍由使用端原有邊界核對。暫時變身分支再次讀取完整身分，避免混用兩個形態的資料。

`known=false` 表示無法確認；已成功讀到空規則指標則是 `known=true, address=0`。原有技能邏輯允許沒有規則物件，換隊及 SB 初始化仍明確要求非空關卡物件。禁止用「無法讀取」推論「沒有禁用規則」。讀取函式回傳 false 時必須停止使用此次結果；輸出保持原值，以免寫入不完整資料。

## 武裝與關卡規則

- 武裝描述的 ID、覺醒／加值、等級、技能等級、variant、padding 使用具名索引；相關記憶體位置由 battle target 生成，生成器檢查本版版面。Orb／Battle 的關卡指標位置另有編譯期一致性斷言。
- `PHBattleNormalizeAssistDescriptor` 僅忽略原生 padding，以及 ID 為零時沒有意義的技能等級 word；其餘欄位維持精確核對。
- `PHBattleEffectiveAssistMatches` 在已確認的 Assist 禁用關卡要求有效描述是原生空武裝格式；普通關卡仍要求與保存配裝相同。來源 ID 的所有權檢查另行保留。
- `PHBattleEffectiveAssistAvailable` 統一技能與診斷的關卡禁用判斷；不取代 CD、同進化鏈、操作窗口或原生施放資格。
- `PHBattleAssistIsRetained` 統一 native `+472` 低位元組為 172 的含義；原始 word 仍保留於快照作狀態變更比較。不能據此推論武裝一定生效。
- 零有效武裝可能來自關卡禁用、暫時變身、敵方效果或技能消耗；只有已取得的規則／回合／retention 證據可解釋原因，不從零 ID 猜原因。

## 採用範圍與相容性

換隊成員 reader、配裝校驗、入場 SB、技能狀態／來源選擇、技能拒絕診斷、HP／覺醒狀態、武裝 L 回復、Combo 加算觀察和策略中的武裝 retention 判斷已使用共用定義。

既有 `PHBattleMember`／`PHOrbSkillMember` 的欄位作為相容視圖保留，由共用 decoder 填入，避免本批同時改動 Session、儲存格式與診斷 JSON。沒有增加另一份常駐身分快照。Leader 專用來源席位、8P 固定參數、動態覺醒和敵方效果保留各自的專用讀取與校驗。

後續兩批另行整理 [Auto 生命週期](auto-session-state.md) 與 [診斷／版本資料](diagnostics-release.md)，一同納入 0.5.35，保留各自可審查的行為邊界。

## 待驗證

新增純模型、Battle reader 及 Orb native 案例：一般／持續／暫時變身、8P 大 ID、來源與有效武裝不同、關卡禁用、無規則物件／不可讀規則、損壞 protected 欄位、短快照、完整讀取與零寫入，以及本體技能可用但武裝意圖被拒絕。沿用既有 Assist 禁用入場／SB／還原與多人限制案例。

案例及 iPhoneOS／macOS 編譯納入 0.5.35 本輪驗證，結果見開發紀錄。原生效果及多人同步仍需真機驗收；模型和語法檢查不能替代它們。

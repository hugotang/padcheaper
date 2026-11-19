# 隊伍＋開場＋循環＋盤面目標 config

0.5.6 起可在「自動轉珠 → 隊伍／關卡策略」貼入整包 JSON，按「驗證並預覽匯入」，檢查六格本體／武裝、開場順序、循環與目標，再按「儲存並選用」。這會一起保存隊伍和策略，保留其他隊伍。入場套隊沿用原本開關；按「開始自動轉」才開始技能流程。

附帶的 [雨龍四連版](../templates/ishida-uryu-gamewith-574233/team-plan.json) 及 [心 L 版](../templates/ishida-uryu-gamewith-574233/team-plan-heart-l.json) 擇一匯入。兩者的光 T 都要求首批恰好五顆；心四連和心 L 是兩種選擇。詳細配裝來源、數值假設見 [模板說明](../templates/ishida-uryu-gamewith-574233/README.md)。

另有 [GameWith 萬聖 CIEL 推薦隊／更木劍八 2 體](../templates/halloween-ciel-gamewith-579537/README.md)。其完整整包需要 0.5.12 的消耗武裝、先轉第一盤與保留施放間隔支援；已交付 0.5.11 請使用該目錄的編隊檔及手動開場後普通策略。

[GameWith Himmel／傑拉德／試煉因陀羅／三體闇迦梨推薦隊](../templates/himmel-gamewith-545672/README.md) 提供六格編隊、七步開場及五色＋心循環。首盤保留開場造盤，第二盤起第 5 → 4 → 6 格迦梨輪替；普通策略只供手動完成七步且首盤未轉時開始。使用目前 23.6.1 目標及既有格式，新模板的解析／Session／真機驗證仍待執行，詳見目錄內紀錄。

雨龍完整模板於 2026-10-31 加入首盤保留白哉：光／心足夠時第二盤才開始一般輪替，第一盤缺光或心仍可補珠。新版模板需要本批 `intervalMode`／`startAfter` 支援，0.5.11 不能直接匯入；[銜接理由及限制](../templates/ishida-uryu-gamewith-574233/README.md#首盤保留白哉與下一盤銜接)。

## 格式

2026-11-17 待編譯來源新增單人原生固定隊伍格式 `padcheaper-fixed-plan`，範本見[珊瑚固定隊伍](../templates/coral-fixed-gamewith-585607/README.md)。此格式保存六格來源、逐層技能與盤面政策，不建立或套用編隊，不選關、入場或重打。於「隊伍／關卡策略」匯入與分享；完整計畫須在 B1 未操作時手動開始 Auto。同場只要已發出技能、目標點選或拖珠，停止後便不能重新開始此固定計畫，避免重播或猜測進度。這與下方普通整包開場的恢復規則不同。**需要 0.5.49 或之後版本，0.5.48 不支援**。編譯測試結果以交付的 verification.json 為準，實機通關尚未驗證。

外包為 `format`、`schema: 1`、`target`、`profile`；`profile` 包含原有 `id/name/team/dungeon/floor/settings`，另有 `source` 與 `fixedPlan`，不得混入 `formation/opening/activity`。`settings.rules` 必須為空；六格 `team` 必須等於 `fixedPlan.members[].body`。地下城與子關卡 ID 仍使用原有配對規則，0 不限，不能將網站樓層號當成子關卡 ID。

`fixedPlan` 嚴格接受 `schema: 1`、六格 `members`、連續 B1 起的 `floors`，及可選 `leaderSwap: 2–5`。每格宣告 `body`、`assist`、`forms: [{number, skills: [精確本體技能 ID]}]` 與 `assistSkills`。首個形態必須為原本本體；變身與技能須列在來源角色的宣告中。`assist: 0` 表示無武裝；`null` 只允許未施放的武裝，開始時綁定實際來源，不能列 `assistSkills`。`leaderSwap` 表示該位置施放變身後與第一格交換，需原生回覆確認；後續 `slot` 是交換後的位置。原生來源、本體形態、武裝保留與技能窗口仍逐步核對。

每層為 `{number, variants}`；使用單一 `kind: "any"`，或同時明列 `ordinary` 與 `parts`。進層才選一次分支，部位破壞後保持已選分支。每分支的 `stages` 依序執行，每階段包含 `id/actions/board/repeat`，最多 24 個階段、24 個動作，整份最多 256 階段與 1024 動作。

技能動作為 `{kind:"skill", slot, source:"body"|"assist", monster, skill, afterMonster, optional, frequency:"once"|"each", when}`；`afterMonster: 0` 表示不變身。`when` 為 `always`、`attack_ready`（沒有未被我方 bypass 覆蓋的傷害／屬性吸收）或明列的原生效果條件。`optional: true` 只允許已明確不可用時略過；未知來源、啟動或完成回覆仍停止。`once` 是此階段一次，`each` 是此階段每盤；同一盤可明列多個加速後再次施放的步驟。

可選 `alternatives: [位置]` 依序探查同形態、同技能的備選施放者，只用第一個原生就緒者。`consumeAssist: true` 只用於武裝，須確認消耗後才前進。`loop: true` 只用於不變身的本體技能；首次確認施放後，每盤依原生就緒狀態嘗試，明列同位置動作的階段會接管該次施放。沒有推算冷卻，也不強制滿蓄武裝代替本體。

2026-11-19 待建置來源新增 `requireReady: true`：只允許 `loop: true` 且 `optional: false` 的技能動作，表示這次明列施放必要，不能因先前已啟用循環而略過未就緒技能。後續自動循環仍為原生就緒時才嘗試。階段可用 `holdLoops: [1–6 的位置]` 暫停指定位置的已啟用循環，保留給後續明列動作；不取消該階段明列的技能，不推算冷卻或授權施放。陣列不可為空或重複。

目標動作為 `{kind:"target", target:"part"|"enemy", monster, optional:false, frequency}`，`monster` 是敵方精確 catalog ID，0 不限。必須唯一對應存活目標；普通敵人核對圖鑑記錄指標，部位核對原生模式、HP 與存活擁有者。點選使用單幀輸入並還原，之後回讀原生選取才前進；缺席或有多個候選均停止，不支援 optional target。

r6 待建置來源另支援 `{kind:"target", target:"attribute_absorb", monster:0, color:0–4, optional:false, frequency}`，只可用於有完整 `requireEnemyState` 名單的階段。逐個新鮮本體核對吸收顏色，**已知全部不符**才略過；未知即停止，不使用診斷快取或敵人屬性推測。有多個符合者時保留當前符合的指定，否則採原生順序第一個；只有一個存活本體時不點目標。點選需原生回覆，求路後變更撤銷路線。`color:2` 是木色索引，不是遮罩 2。0.5.57 不支援。

`board` 為 `{policy, assistRecoveryL, maxGestureMS}`。政策為 `one_combo`（精確一組三顆，含確定連鎖仍只有一組）、`six_colors_no_fours`（首消六色，含確定連鎖不得有四顆組）、`six_colors_fours`（首消六色及心四連，優先有效四顆組）、`six_colors`；另有 `l_only`（只消一組五顆 L），此時必須加 `lColors` 攻擊色遮罩。攻擊政策也可加 `lColors`。`maxGestureMS` 為 0–4500；0 使用一般操作時限，非零限制求路長度、取色等待與實際拖珠，不修改遊戲計時。武裝無效需要 L 時使用原生回復證據；盤面政策不保證傷害、回復量、耐久或隨機天降。

r6 待建置來源的 `board.requireSixColorFours` 必須是真正 Boolean，只允許 `six_colors_fours`，不能與 L 回復、`lColors`、不可消回復或 `absorbStall` 混用。為 true 時要求每個相關相位的首批六色各一組四顆；普通、單次及連續取色的搜尋和重播都要遵守。總數六組不等於六個不同色，後續落下不補足首批四顆。缺省保留舊契約，0.5.57 不接受新欄位。

階段可加 `absorbStall: true`。執行已宣告的可選解吸收技能後，如果吸收仍未被我方效果覆蓋，該盤改用六色無四顆存技；條件仍需新鮮原生證據。收尾增傷可用 `when: "attack_ready"`，避免存技時提早耗用。

`repeat` 為 `{minimumPuzzles, maximumPuzzles, until}`。上限 1–999，且不得小於下限。只在觀察到原生轉珠結算與下一個穩定回合後增加盤數。`until.kind` 可為 `puzzles`、`floor_change`、`parts_absent`、`enemy_hp_at_most`（加 `percent:1–100`）、`effects_clear`（加 `effects` 字串陣列）或 `skill_ready`（加 `slot/monster/skill`，查詢原生本體技能就緒狀態）。`puzzles` 下限至少 1；其他原生條件可為 0。HP 門檻只看普通本體，排除部位。最後階段須以 `floor_change` 結束；準備階段提前換層、部位分支離層前未觀察到全部部位破壞、未知證據或達盤數上限仍未滿足均停止。原生結果頁結束本場。

效果名稱為 `damage_absorb/attribute_absorb/damage_void/awoken_bind/member_bind/skill_seal/unmatchable/assist_void/spinner`。`effects_clear` 與技能的效果條件都承認已確認的原生全隊 bypass，不重複處理已覆蓋的吸收／無效。來源網址不授權任何操作；流程只執行通過格式與原生核對的宣告。

下一版來源新增 `board.unmatchableRecovery` 嚴格布林開關（r5，**未建置／未執行測試，0.5.56 不支援**）。只允許 B2 的 `six_colors_fours`，不能與 `assistRecoveryL`、`lColors` 或 `absorbStall` 混用。執行時還須確認巴索羅繆 `200974`、單色一回合不可消、普通珠盤面及新鮮潛覺／TPA 提供者。求路只允許原生第一組火／木四連回復後的既有珠連鎖補色；此分支不支援 fishing、荊棘／釘珠／Combo 珠／特殊珠或首批加算 Combo 契約。詳見[不可消回復](unmatchable-recovery.md)。

0.5.56 來源新增以下原生狀態約束，缺省不改既有計畫；含以下欄位的 r4 需 0.5.56 或更新引擎，0.5.55 不支援。建置／測試結果見交付 `verification.json`：

- 階段 `requireEnemyState`：1–8 項完整本體名單，每項 `{monster, alive?, maximumPercent?}`。`monster` 是大於 0 的精確 catalog ID，不能重複；`alive` 必須是真正 Boolean，`maximumPercent:1–100` 只可與 `alive:true` 一起使用。包含死亡本體，不容許省略、額外敵人、重複 ID 或部位。每項都需新鮮身分、HP 及有效最大 HP，不能以未知當作死亡。每次技能／目標動作及求路輸入仍核對；規劃後的任何 HP／選取變動會撤銷原路徑。
- `until.kind: "enemy_state_or_floor_change"` 搭配 `enemies` 同上：同層需符合完整名單及血線；已確認下一層也可完成此攻擊階段，不會誤判為準備階段提前換層。末階段仍必須是 `floor_change`；其他準備階段原有的提前換層停止規則不變。
- 階段 `requireEffectsClear`：不重複且非空的上述效果名稱陣列。允許本階段先施放對策，全部動作完成後、求路與持珠期間都需確認效果已解除或由原生 bypass 覆蓋；未知或未解除便停止。不能以技能完成回覆代替效果讀數。效果在已放手的原生結算中正常消失，不倒推為上一盤輸入失敗。

固定計畫診斷的 `executionHistory.fixedPlanBeforeMove`、`fixedPlanBeforeRelease` 保存各自時點的效果及敵人 HP／目標；`fixedPlanAfterTurn` 僅在同一盤原生放手且下一個穩定操作回合確認後寫入，依 generation、turn、戰鬥身分綁定且不重寫。尚未收到結算、停止或直接到結果頁時可以沒有該欄位。這些匯出診斷不會被讀回作為動作授權。

`format: "padcheaper-team-plan"`、`schema: 1`、`target: "jp.gungho.pad-ios-23.6.0-106878"`。單份 UI 匯入上限 128 KiB，策略庫 64 份。未識別欄位、動作、版本、格式或錯誤數值拒絕整份匯入；相同 ID 不覆蓋，需先整理原策略或改用另一 ID。

| 欄位 | 內容 |
|---|---|
| id / name / source | 穩定 ID、顯示名稱、來源文字／網址 |
| formation | 現有 schema 1 編隊格式，六格本體、武裝、等級、技能等級、覺醒、加值及潛覺 |
| opening | 最多 24 個依序技能步驟；每步 slot、source（body / assist）、monster、assist、skill、afterMonster；0.5.12另接受可選 consumeAssist、puzzleAfter |
| rotation | 現有最多 12 條循環規則，加上 source、skill、atLeast；0.5.12另支援 intervalMode、startAfter |
| board | quality、mode、goals、recovery，以及可選 Boolean `thornComboPriority`、`allowPoisonWithHeartFour`；0.5.12另支援 `diagonal`；待建置來源新增 `allowThornsWithHeartMatch` |
| dungeon / floor | 地下城／子關卡，0 表示不限；不是樓層 wave |

`afterMonster: 0` 表示施放後本體不變；變身步驟填預期圖鑑編號。之後若同一位置還有開場技能，`monster` 必須接續變身後編號。

2026-10-30 0.5.12 新增開場 Boolean `consumeAssist: true`，只可標在武裝步驟，不能同時宣告本體變身。該步必須確認原生使用次數、原生武裝狀態清零、目前武裝消失，且保存來源武裝仍為原編號，才前進；一般武裝不因缺席而自動判定消耗。後續同位置開場的 `assist` 應為 0，已消耗武裝不能重用。完成開場後及暫時變身仍核對保存的來源與消耗狀態。

可選 `puzzleAfter: true` 只可標在最後一步：完成後先依本策略盤面目標轉此盤，確認下一個原生轉珠回合才接 `rotation`。用於 CD1 供珠角在開場最後才施放、當回合尚未重新蓄好的隊伍。停止重開保留這個待轉盤進度，手動完成並觀察到新回合也會清除。兩個欄位缺省／false 維持舊流程；只接受真正 JSON Boolean。已交付 0.5.11 不支援這些欄位，需 0.5.12 以上。

0.5.8 的 `thornComboPriority: true` 對應「荊棘盤優先 Combo」，缺省／false 保留較少傷害優先。開啟只調整路線排序，所有接觸仍要求首消心四連及目前 HP 大於完整拖珠傷害；不預支回血。可先選隊伍策略，再在轉珠頁切換；會一併保存於該策略與整包匯出。舊版可繼續匯入未加此欄位的範本；含新欄位的匯出檔需 0.5.8 以上。

2026-11-12 待編譯來源新增 `allowThornsWithHeartMatch: true`，對應「荊棘盤心三連即可」。缺省／false 維持心四連；開啟後，荊棘路徑只需每個轉轉相位都有首批有效心珠消除組（三顆以上，仍遵守不可消／最少消珠規則）。接受使用者確認的隊伍回復能力，不自動估算回復倍率或回血量；目前 HP 必須嚴格大於待結算傷害加完整路徑傷害，不預支回血。指定心四連目標及消毒的心四連條件獨立保留。此選項與所選策略一起保存、匯入匯出，僅接受 JSON Boolean；0.5.46 及之前版本不支援此新欄位。多人仍禁止有傷害的荊棘接觸，取色搜尋的荊棘限制未放寬。建置與待驗證狀態見 [開發記錄](development.md)。

0.5.11 的 `allowPoisonWithHeartFour: true` 對應「心四連時允許消一般毒」，預設／缺省為 false。全部轉轉相位均有首消直線心四連時，允許一般毒消除並優先目標與 Combo；採用使用者已確認的隊伍回復能力，不精算回血或毒傷害。猛毒仍不得消除，爆彈仍須首消全消。此欄位與所選策略原子保存，含欄位的分享檔需 0.5.11 以上；舊範本可照常匯入，再為目前隊伍打開選項。

2026-10-30 0.5.12 新增 `diagonal: true` 對應「斜向轉珠」，缺省／false 仍只走上下左右。開啟可用相鄰八方向，消除與形狀保持橫直規則；與目前設定、所選策略及整包匯出一起保存。只接受 JSON Boolean，數字 1、字串或 null 均拒絕。已交付 0.5.11 不支援此欄位，含 `diagonal` 的普通策略／整包分享檔需 0.5.12 以上；舊版會拒絕新欄位，缺省的舊範本仍可由新源碼匯入。建置及真機狀態見 [開發記錄](development.md)。

循環 `source` 預設 `auto`，可選 `body` 或 `assist`；`skill: 0` 表示不限定技能 ID。它們是原生目前技能的約束，不會強制施放未蓄好的武裝或繞過原生冷卻。0.5.8 起，武裝蓄滿而原生允許同進化鏈二選一時，`body` 可選目前本體，仍檢查原生冷卻、珠數與其他使用條件。非同進化鏈會略過本體規則，開場則停在當步；`auto` 保留遊戲預設選擇。需要手動選擇其他子技能的技能仍可能停止等待使用者處理。見 [原生二選一](orb-skill-selection.md)。

循環的樓層保留、輪替組與本場一次條件只作用於 `rotation`，不會取消 `opening` 明列的技能。需要保留的武裝不要列入開場；開場與循環用不同時機，請在預覽時一併核對。

2026-11-12 待建置新增循環來源 `body_or_assist`：「本體優先，滿蓄改用武裝」。`skill` 核對本體 ID；遊戲不能選本體且有效武裝滿蓄時才用武裝，仍須通過原生使用條件。同鏈可選本體但本體條件未滿時不替代。武裝不算本體規則／群組完成，不消耗本體間隔或本場一次；該位置本回合已用，同組其他位置可接續。必要效果未解除仍停止。開場不能使用此來源；既有保存策略不自動更改。詳見[滿蓄調查](orb-assist-overcharge-2026-11-12.md)。

0.5.12 新增兩個循環欄位：`intervalMode` 為 `run`（預設，每次開始重新計算）或 `accepted`（距離同規則／同組上次原生啟動）；`startAfter` 為 0–999 的整數（預設 0）。例如 `every: 2, intervalMode: "accepted", startAfter: 2` 先等兩盤，第 3 盤才啟用，啟動後最早第 5 盤再次嘗試；若當時不可用，後續每盤重查，啟動後重新計算間隔。含這些欄位的檔案需要本批新版；0.5.11 不接受新鍵。

`accepted` 的回合及最後施放紀錄只保留在本程序的同場同策略記憶體；停止重開不改變相位，切換策略身分、隊伍／開場綁定或循環規則才重設。零交換需觀察到拖珠或開場待轉盤，以及結算後穩定的新操作階段；技能動畫、清場換層或未知讀取不能充當新回合。沒有觀察到的回合與手動技能不推估。原生明確不可用不更新最後施放，結果不明則保留防重試紀錄並停止。

群組共用上次施放，提前吸收救援亦算一次；各條規則可有不同 `startAfter`／`every`，讓固定輪替先等兩盤、必要救援立即啟用。含 `accepted` 規則的群組會先查下一位置的各條件，再嘗試上一位；每位置／每組每回合一次與必要救援限制仍適用。只含舊 `run` 規則的群組維持原行為。

`color` 和 `below` 保留原用法；新增 `atLeast`。使用至少九光：`color: 3, below: 0, atLeast: 9`。`below` 與 `atLeast` 必須擇一非零，未指定珠色時兩者均為零。至少條件用全部轉轉相位的最低珠數，不因短暫顯色誤放。

## 開場與停止後恢復

第一次啟動有開場的策略，必須是首層、尚未轉珠與施放技能，六格本體及武裝吻合。每步再次核對隊員、盤面、技能種類與 ID，原生允許後才啟動；看到原生使用次數增加、動畫完結及預期變身後才前進。

開場不執行未列出的循環技能，也不在步驟之間轉珠。開場完成後開始循環；0.5.12的最後一步若標 `puzzleAfter`，先轉此盤才接循環。被加速到就緒的本體可在同一個轉珠回合接續開場武裝；普通循環仍每位置每回合至多一次。

停止或背景不會撤回已啟動的技能。本程序仍能確認其完成時會保存進度，重新按開始從下一步接續。啟動結果不明、使用次數不明、隊伍不符不自動跳步或重放。進度目前只在本程序的本場記憶體，重啟遊戲後不推測已完成武裝；中途加入請手動完成開場並選不含開場的策略。

## 敵方暫時變身

0.5.7 起，已完成開場的流程可繼續經過原生暫時變身。程式核對保存的原本角色、持久變身及原武裝，保留本場開場進度；受影響位置的原角色及未限定角色規則暫停，其他隊員照常判斷，效果結束後接回原循環。明確指定暫時角色圖鑑編號的規則才可嘗試其當前技能。

必要自救規則仍會判斷是否需要救援；需要解除卻沒有可用施放者時停止，不會假設原技能仍在。未完成的開場、無法確認的暫時資料、持久替換或不同底層武裝不能使用此例外。停止後重新開始可接續本程序保存的已完成開場；重啟程序仍不推測進度。

隊長模式使用當前隊長／助人，未支援的隊長條件仍會阻擋求路；手動目標保留並提示原隊長倍率或減傷未必成立。這個處理不會撤銷敵人施加的變身。

## 儲存與匯出

新隊伍庫與策略同在 `config.json`，一次原子寫入成功才更新執行設定。舊版的隊伍庫會在首次儲存時遷入；舊 `formations.json`／`formation.json` 是兼容備份。不要用舊備份判斷目前選隊，也不要用舊 dylib 編輯新格式後覆蓋它。現版遇到未通過驗證的 `config.json`，會在第一次寫入前把原檔另存為 `config.invalid-<epoch>.json`。

選取完整策略後可以按「複製整包 config」。轉珠頁對目標、品質及循環的修改會保存在所選策略，匯出時一併帶出。開場與配裝目前以 JSON 修改再匯入；來源網址僅為文字，不會自動執行或下載指令。

0.5.8 增加「分享策略檔案…」與「從檔案匯入 JSON…」。分享面板可選 AirDrop、儲存到檔案或其他 App；普通策略同樣可分享為單份 `padcheaper-orb-profile`。匯入先預覽，再儲存生效；重複 ID 不覆蓋，檔案不含帳號／API／總開關。詳見 [檔案分享說明](persistence-music-sharing.md#分享與匯入)。

所有戰鬥寫入及自動操作仍只限單人。範本不保證通關；整版毒、特定關卡血線、敵人行動、回復力降低與其他未建模的條件需依關卡調整。

2026-11-15 待編譯來源另新增 `allowThornFishing: true`，對應「荊棘盤允許取色」。預設 false，僅接受 JSON Boolean，與目前策略一起保存及匯入匯出。普通單人的單次／Auto 可嘗試最多六次取色；接觸荊棘仍需每個終盤相位符合 `allowThornsWithHeartMatch` 所選回復政策，且目前 HP 嚴格大於待結算與剩餘路徑傷害。未確認 HP 時不能碰入有傷害的荊棘，鎖珠及多人限制仍保留。0.5.47 已交付版本不含此欄位；[來源設計與待驗證事項](orb-thorn-fishing-2026-11-15.md)。

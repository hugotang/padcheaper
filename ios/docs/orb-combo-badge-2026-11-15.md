# 0.5.47：7 Combo 與消 T 徽章的停轉調查

使用者確認這隊選用「消 T 加 2 Combo」徽章。若它當回合有效，盤面首消 7，加雨龍 2、白哉 4、徽章 2，共 15，應達到此次門檻。

## 裝置紀錄實際證明了甚麼

關卡 7819、floor 2、Auto 第 4 回合的 `enemyCountermeasure` 是 `beforeBonus:15`、`bonusSources:[2,4,0,0]`、`minimumCombo:9`。搜尋路徑首消 7 Combo，光 T 與心四連都完成；因臨時 Combo 目標未達成而停止，沒有送出這條路徑。這不是遊戲把 15 Combo 吸收，而是 Auto 在出手前只認列 6 點加算。

`bonusStateKnown:true` 只表示讀到本版支援的描述，不表示所有效果都獲得 credit。舊 log 沒有徽章的條件與加算原值，不能分辨「原生沒有套入此加算」與「讀到另一個尚未證明的條件」。已啟用 113 個徽章的訊息是全域解鎖狀態，不是此戰鬥已套用消 T 徽章的證據。

## 原生核對結果

只讀核對 JP 23.6.1 / 107553 的 IDA 輸入與 SHA-256；8 個函式的完整 bytes hash、18 個 Combo guard 核對結果及主張保存在[研究紀錄](../research/orb-combo-badge-20261115.json)。沒有發現徽章描述位址偏移：三份記錄仍在 `impl+745476/+745504/+745532`。

徽章 kind 27 的 conjunction 在 `+0` 存 bit mask，T 為 `16`；kind 28 的 repeated-awakening 在 `+0` 存 awakening ID，T 為 `126`。兩者加算皆為 `+22` 的 uint16。原生和現有模型都使用同一組 T 供應者／觸發資訊；若該描述確實是 T +2，既有公式已會把 15 降為首消 7。只靠此 log 不能宣稱找到可修正的漏算分支。

原生有效徽章另受關卡覆寫／禁用及前置徽章影響，詳見[徽章來源](formation-badges-native.md)。本批沒有按儲存徽章 ID、模板或使用者說明直接補 +2，也沒有更改徽章、呼叫遊戲 getter 或寫入遊戲記憶體。

## 已修改的源碼

- 保留三個徽章描述，包括加算為零及條件為零。計算器略過這些空效果，其他有效隊長／徽章 credit 保留。
- `goalPlan.enemyComboBonusEvidence` 與 `searchTrace.enemyCountermeasure.bonusEvidence` 記錄各效果的來源、kind、condition、minimum、nativeCombos、creditedCombos，以及 T 等覺醒供應色罩。原因區分 `credited`、`no-native-bonus`、`no-native-condition`、`not-proven-by-base-goals`、`invalid-or-overflow`。資料是當次有效戰鬥描述，並非徽章選擇 ID。診斷 credit 與規劃共用同一計算結果；總額越界時所有 credit 歸零。
- 連續取色未找到方案時，`minimumExchanges` 現為 `null`。原來的 `7` 是內部「超過六次／未找到」初值；本次 8192 狀態截斷並不證明最少要取七次。保留 `minimumFound`／`minimumKnown`，沒有提高狀態或取色上限。

## 驗證狀態與下一步

已編寫兩種 T 徽章格式、空描述、15−8＝7、不可降低使用者保存目標、總額越界與 Session 診斷案例。solver 案例保存原 log 的盤面與 33 格路徑，檢查 7／9 兩個門檻及轉轉相位；徽章輸入為明確標示的合成資料。Session 使用該路徑的終盤及一個不改色盤的交換，隔離 Auto 接受條件與搜尋速度，並非完整真機重播。

`PHOrbModel.c` 與 `orb_solver_tests.cpp` 的 `-fsyntax-only -Wall -Wextra -Werror` 通過；沒有產生執行檔，沒有執行測試套件、編譯 Objective-C++、派發 CI、升版或打包。先前 0.5.47 的 CI 不代表本批通過。

**停轉根因仍待新的真機徽章明細，尚未宣稱修復。** 下一次明確要求「編譯測試」後執行本批回歸，再用相同消 T 徽章取得新版 log。若徽章有效描述為 condition 16／126、nativeCombos 2、T provider 包含光色 8，應看到 sources `[2,4,2,0]` 和 minimumCombo 7；若 amount 或 condition 為零，需繼續追遊戲實際徽章來源及套用時機。私人 log 與既有交付未修改。

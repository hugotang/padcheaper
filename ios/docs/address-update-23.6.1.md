# iOS 23.6.1 地址遷移與資料相容性

2026-11-06。iOS 23.6.1／107553 的地址表、來源引用與資料相容入口已完成遷移及靜態複查。456 組指紋與新版 executable 的原始 bytes／SHA-256 全部吻合；仍未編譯產品、執行測試套件、派發 CI、安裝或操作裝置，不能當作新版本已可使用的證明。

## 輸入身分

兩份輸入均為 `jp.gungho.pad` 的 `pad`、arm64（CPU `0x0100000c`、subtype `0`）、Mach-O executable（filetype `2`），最低 iOS `15.0`、`cryptid=0`。下列 SHA-256 為解出後 executable 的完整雜湊，並非 IPA 雜湊；地址均為未加 ASLR slide 的 VA。

| 項目 | 舊版 | 新版 |
|---|---|---|
| 版本／iOS build | 23.6.0／106878 | 23.6.1／107553 |
| executable bytes | 21,573,888 | 21,574,784 |
| UUID | `DD332AD4-4C92-3AAF-85D0-3892556A3A0E` | `38A9F704-C562-3BDD-B510-2112DAC084B0` |
| SHA-256 | `611c93f5472fceddfa27254b718c6eff1decdc6814cc927e2faa4c7c250a65ec` | `3ac8c5647e67694b5a3e68cf7049d3860f82f029a7e67f96477e9e2a3f490fd6` |

兩份 metadata 的 `__TEXT` VA／VM size 都是 `0x100000000`／`0x1110000`；`__DATA_CONST`、`__DATA` 的 segment 起點與大小也相同。這不表示段內函式、物件或欄位維持原址；本次逐一核對實際引用，不能套用全域固定差值。完整 metadata、反組譯匯出與 IDA 資料庫保留在本機 `build/ios-address-23.6.1/`，不加入交付。

## 地址表與來源整合結果

目前生成器及 build／test／GUI 入口採用 [PAD target](../targets/pad-23.6.1.json)、[Battle target](../targets/battle-23.6.1.json) 和 [Orb target](../targets/orbs-23.6.1.json)。三份 23.6.0 表保留原樣供歷史證據引用。逐項來源與結果見 [靜態核對資料](../research/address-update-23.6.1.json)。

| 範圍 | 已完成的靜態核對 |
|---|---|
| 指紋 | PAD 17、Battle 35、Orb 404，共 456 組；新版原始 bytes 及 SHA-256 全數吻合 |
| 指紋定位 | 451 個線性程式窗口、3 個另行複查的非線性程式窗口、2 個常數表窗口 |
| 完整函式匯出 | 原 297 組加最後 7 組，共 304 組的完整指令覆蓋與原始 bytes 核對；實際差異另列，並非 304 組行為完全相同 |
| 位址／物件欄位 | 237 項明確基底的欄位與全域位址，其中 139 項數值改變 |
| 原始碼內小欄位 | 選定 100 項；80 項具基底／存取寬度對應，20 項補上局部資料流證據 |

主要變更如下。RVA 相對 image base `0x100000000`；其餘值相對表中所列物件，不能彼此混用。

| 欄位 | 基底／單位 | 23.6.0 | 23.6.1 |
|---|---|---|---|
| Save | image RVA | `0x1b517b0` | `0x1b52010` |
| GameMain | image RVA | `0x2abcdf0` | `0x2abd660` |
| Battle impl 指標 | image RVA | `0x2abce60` | `0x2abd6d0` |
| 輸入 manager 指標 | image RVA | `0x2ac6fd8` | `0x2ac7858` |
| 原生隊伍 storage | image RVA | `0x136d950` | `0x136e120` |
| 原生隊伍 stride | bytes | `0x6d78` | `0x6d88` |
| 徽章 owned bitmap | save offset | `0x52f314` | `0x52f31c` |
| 技能限制 | impl offset | `0xb3a00` | `0xb3a10` |
| 技能冷卻觀察 | impl offset | `0xb39f0` | `0xb3a00` |
| 技能動畫／目標值 | impl offsets | `0xb7728`／`0xb772c` | `0xb7738`／`0xb773c` |
| 待結算荊棘傷害 | impl offset | `0xb7e6c` | `0xb7e7c` |
| 暫停對話框 | impl offset | `0xbe9c8` | `0xbe9d8` |
| 隊員 pool／stride | impl offset／bytes | `0xb11f0`／`0x5f0` | 不變 |

最後另核對波數 mirrors `impl+0x92a10/+0x92988`、隊員 parameter 橋接所使用的 `member+0x4f0/+0x506`、raw touch 的 `record+0xf8`，以及徽章表 pointer／end／count `save+0xa0/+0xb0/+0x24e8`；上述小欄位維持原布局。隊員附加覺醒仍為 `member+0x208`，不能用其他 UI 物件恰好相同的立即數作證。

API／network、徽章、timer、技能動畫及吸收／無效貫通欄位改從 manifest 生成，移除來源內已過時的數值。荊棘傷害說明欄與實際欄位表也已同步，生成器會拒絕不一致值。target 生成器另核對身分組成、欄位集合、符號、VA／RVA／save offset 關係與指紋雜湊。新匯出和診斷共用生成的 `PHTargetID`；未升 dylib 版本。

觀察到的 1,537 組呼叫對應沒有衝突，但其中 118 組未遞迴認證被呼叫函式，不能推論整個遊戲語意等價。另保留原生版本浮點常數約 `23.6→23.61`、`data048.bin` 格式參數 `184→185` 的真實差異；本專案沒有自行序列化該原生 cache，帳號相容決定僅涵蓋下述九數字記錄。

## 隊伍記錄的獨立核對

比較完整函式內的存取寬度、偏移、常數、局部分支目的地與格式字串；地址類差異另列實際 operand 值核對，沒有把所有立即數或結構偏移一律忽略。

| 路徑 | 舊 VA → 新 VA | 靜態結果 |
|---|---|---|
| save_decks request | `0x1006981d0` → `0x100698850` | 各 124 條顯示指令、8 個分支目的地對應一致 |
| record serializer | `0x1006983f8` → `0x100698a78` | 各 92 條、4 個分支一致 |
| record parser | `0x10069cc78` → `0x10069d2f4` | 各 244 條、35 個分支一致 |
| bank parser | `0x10069c5e0` → `0x10069cc5c` | 各 366 條、46 個分支一致 |
| card number search | `0x1004cadfc` → `0x1004cb01c` | 各 42 條、2 個分支一致 |
| badge byte decode | `0x100445c34` → `0x100445da4` | 完整 28 bytes 相同 |

原生 record 仍為 96 bytes：五個卡片欄位在 `0,4,8,12,16`，extra u32 在 `20`，兩個 BGM u16 在 `24,26`，skin u16 在 `28`，badge byte 在 `95`。JSON／wire 九欄順序維持 `card0..4, badge, extra, bgm1+100000*bgm2, skin`。bank 容量仍為 primary 32、secondary 16；徽章位元組仍用 `raw XOR uint8(15*index+97)`，bank 內索引與 request 合併索引的用途保持分開。

18 個相關 C 字串、32-entry 的 `1<<bit` mask table，以及 badge owned 初始化用的 16-byte 常數均逐 byte 相同。卡片編號查詢仍在 `number>9899` 時加 `100`。request 的 `pid` 仍來自 save 的 player ID，但該欄位由 `save+0x83d50` 移至 `save+0x83d58`；這類 save 欄位的位移與記錄內部格式不能混為一談。

本機證據：`deck-format-verification.json`、`deck-asm-delta.json`、`deck-operand-delta.json`、`deck-literal-verification.json`。最早的格式報告保留 `account_store_migration_authorized_by_this_report=false`，表示格式比對本身不足以決定帳號保存可否沿用；下節的決定另以完整保存／回復來源審閱為依據。

## 受保護數值與隊員布局

四個 helper 的非全域 operand 及映射後局部分支相同：`0x10056c990→0x10056cbd4`（28 條）、`0x100570af4→0x100570d94`（23 條）、`0x100570b54→0x100570df4`（28 條）、`0x100570cd4→0x100570f74`（32 條）。新 key1、key2、PRNG VA 分別為 `0x102c6ea50`、`0x101bd5d68`、`0x102c6ea54`。

受保護記錄仍由 offset `0,4,8,12` 四個 word 組成、共 16 bytes；salts `0x766cfc6d`／`0x6a875a8b`、ROR 5／13、LCG 乘數 `214013`／增量 `2531011` 不變。浮點 getter 仍為 bit-cast。遊戲 getter 的多數副本 fallback 沒有放寬本地 reader：本地仍要求所有副本一致。

member rebuild `0x1005a12f0→0x1005a1588` 的 38 個直接 member 存取順序、偏移與寬度相同。已核對的來源／有效身分包括 body `+0x5b0`、assist `+0x5b2`、persistent form `+0x118`、temporary turns `+0x108`、temporary ID `+0xf8`、assist state `+0x1d8`、parameter `+0x4e0`、level `+0x510`、effective assist descriptor `+0x59e`。

89 個局部分支中 88 個目的地對應一致，另一處為新版徽章條件的實際變更：舊式直接比 badge ID 22／23，改為透過 `0x10056d64c` 查詢有效 parameter kind 22／23／31。其 kind getter `0x10056cccc` 會經 owned prerequisite 選出有效 ID；不能把它簡化成「新增 badge ID 31」。現有 reader 讀取遊戲建立的 effective assist descriptor，沒有複製舊的 badge ID 判斷，因此沿用讀取原生結果及既有資格限制。

本機證據：`protected-helper-verification.json`、`protected-member-asm-delta.json`、`member-layout-verification.json`、`member-badge-predicate-verification.json`。這些結果沒有證明所有技能 ID／皮膚／BGM catalog 內容或多人同步行為均不變。

## 僅資料版本相容

[PHDataTarget.h](../src/PHDataTarget.h) 僅接受當前生成的 `PHTargetID` 及確切舊值 `jp.gungho.pad-ios-23.6.0-106878`。它只用於 schema-1 策略、整包隊伍計畫與帳號的語意記錄；不以版本前綴、範圍、Android target 或數值轉換放行。所有原有 schema、欄位集合、型別、內容、數量／大小及重複 ID 校驗保留。

帳號相容決定另審閱了完整 [PHDeckPersistence.m](../src/PHDeckPersistence.m)、[PHCodec.m](../src/PHCodec.m) 與 [PHRuntime.m](../src/PHRuntime.m)。帳號檔只保存已校驗的九個數字，沒有 native pointer、原生記憶體快照或其他不透明格式。載入要求相同的非零 player ID；回復要求相同 account epoch、bank、index、bank count，以及五個卡片欄位加 extra 的 `PHDeckIdentity`。功能啟用、有效當前帳號、遊戲校驗、編輯狀態、原生欄位仍為預設值、徽章資源／資格與寫入 readback 等檢查仍保留。可沿用的是已解碼的欄位值，實際存取只使用當前版本的地址。

九份內建範本保留原本 `23.6.0/106878` target，不改寫歷史來源。匯入既有策略不會啟動自動操作；新增匯出與之後保存的帳號檔使用當前 `PHTargetID`。離線關卡報告 reader 也只列出這兩個確切 target，解析與試算保留來源 target；它不授權任何原生操作。

遊戲執行時的 [PHTarget.m](../src/PHTarget.m) 仍核對當前 bundle、版本、iOS build、UUID 與指令 guards，沒有引用資料相容 helper。單人限制、3／4 人不允許 Auto、8 人單場 Auto 的本地窗口／固定技能來源、每場一次、手動停止後不重啟及不自動入場／重賽等界線保持原樣。

## 待驗證

已補寫但尚未執行的案例包含：九份舊範本、舊策略 roundtrip／重複 ID、錯誤版本與 Android／布林／null target、schema／記錄型別／範圍、相同帳號，以及 bank count／extra 身分不同時拒絕回復。既有 runtime identity 案例另要求舊版遊戲本體不能因資料相容而通過當前版本檢查。

已完成三份地址標頭與編輯器來源生成、變更 Python 檔及 GUI 內嵌 Python 的 AST、三份 JavaScript 的 `node --check`、三個 shell 入口的 `bash -n`、JSON 重複鍵／格式、九份範本原 target、跨表別名／欄位關係、相容 helper 僅有資料入口引用、差異空白及本頁連結檢查。原始碼掃描未發現受檢遷移欄位的舊數值仍用於實際存取。這些是靜態／來源生成結果，沒有執行案例；Objective-C／Objective-C++ 型別與編譯驗證仍待下輪。

待使用者明確要求「編譯測試」後，執行完整原生／solver／Session／設定／編隊回歸、iPhoneOS 編譯、GUI 與新生成資料的一致性檢查。裝置上仍需核對新版啟動／登入、各功能 reader、預設值回復、切換帳號與隊伍、技能來源、單人操作及 8 人同步。所有 device/runtime/server verified 狀態保持未驗證；舊 CI 或先前 solver 結果不能驗證本次遷移。

CI 維持手動 `workflow_dispatch`；本輪沒有產生或修改 `E:/ipa` 交付。

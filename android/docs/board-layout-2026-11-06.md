# Android 盤面布局：局部程式證據與離線來源基礎

2026-11-06，接續 `7f66fc8`，本批研究已保存的 ARM64 runtime 程式擷取，並建立離線盤面解讀與純資料求路預覽的來源基礎。已找到尺寸、格子索引表、物件池、原始色碼與部分動作狀態的讀寫關係；**尚未讀取 live board，也沒有完成 Android 盤面逐格驗證**。

本輪沒有新裝置擷取、遊戲操作、APK／原生測試執行檔建置、測試套件執行、CI 或打包。既有 0.1.1-probe 的交付與驗證保持原樣，不能涵蓋本批新來源。`runtimeLayoutVerified`／`boardReadEnabled`／`actionsEnabled`／`unpackedCodeVerified`／`deviceVerified` 均仍為 false。

## 來源與研究邊界

| 證據 | 身分與範圍 |
|---|---|
| 遊戲版本 | 日服 PAD 23.6.0／230600、arm64-v8a，目標固定於 [版本描述](../targets/pad-23.6.0.json) |
| 原版 libpad | 30,735,496 bytes；SHA-256 `fec9fbc55d3a9b7945817420475a8c03fa608ecc4605535e373ca2d0fc738a8a` |
| 已保存 runtime 擷取 | 17,295,152 bytes，ELF-relative `[0, 0x107e730)`；SHA-256 `43d41b01f7252f269bb3f9728688c12a6c0889a3bb51b71d09f0a81a09248109` |
| 本批 IDA 工作副本 | `build/android/ida-board-20261106/runtime-board.i64`；從已核對的 raw 證據繼續標註，沒有提交資料庫或遊戲 binary |

來源承接 [固定程式區段擷取](runtime-code-capture.md) 與 [離線來源核對](runtime-code-analysis.md)。本批讀取 29,095 筆 dynamic symbol table entry，包含空項，並非 29,095 個有效名稱；當中 23,946 筆為擷取範圍內的已定義函式符號。只對選定布局入口核對 GNU hash chain 並取得一致結果，沒有宣稱全部項目均已驗證。符號名稱與位址只提供研究入口，不能將這些數字當作已驗證函式數或完整解碼證明。

研究對選定入口逐一核對實際 ARM64 指令及讀寫關係，[研究摘要](../research/board-layout-20261106.json) 保存 11 個直接支持布局的入口、函式 hash 與對應指令位置。下表位址均為 ELF-relative VA，**不是目前程序中的絕對位址**。範圍外 GOT、全域物件、可寫段及 heap 內容沒有被原版資料或零值補齊；本批沒有取得 live game pointer。

IDA 工作副本重開時曾重新啟用自動分析，已重新核對原始位元組／範圍並明確停用後才做指定標註；後續重開也需重新檢查，不能只依上次儲存狀態。部分未解析 PLT／GOT 令 pseudocode 簽名不可靠，`__initBlocks` 亦出現區域變數分配警告，因此布局結論依下列實際 load／store 指令，不以偽碼型別作依據。最終工作副本已儲存及關閉，原始位元組 hash 不變；17 個手動標記函式也不等於完整解碼驗證。

## 盤面欄位的局部證據

| 位置／關係 | 核對入口 | 可支持的結論與限制 |
|---|---|---|
| `game + 0x70`、`game + 0x71` | `getBoardWidth` `0x6af034`、`getBoardHeight` `0x6af044`；兩者最後為 `LDRSB` | 分別讀取有號 8-bit 寬、高。前置 `LDR` 使用 `0x10e34c8` 的全域／GOT 位置，該位置超出擷取末端，尚不能解出目前 game 物件。 |
| `game + 0x88` | `getBlockIndex` `0x928a8c`、`swapBlockMain` `0x93cda8` | 讀取 impl 指標。指標值與可讀範圍尚待 live 證據。 |
| `impl + 0xa3558` 的指標表 | `getBlockIndex` `0x928a8c`；`swapBlockMain` `0x93cda8` 亦在同表交換指標 | 每格 8 bytes，表索引為 `y * 16 + x`。盤面行距固定 16，不能使用 `y * width + x` 讀取原生指標表。交換時改表內指標，因此顯示格序不能直接當作物件池順序。 |
| `impl + 0x98958` 的物件池 | `__initBlocks` `0x923f58` | 256 個項目、每項 stride `0xac`；池末端緊接上述指標表。這是相對布局證據，不代表任意外部位址可以安全解參考。 |
| `block + 0x00` | `countBlockType` `0x6c30dc`、`setBurBlockFlag` `0x6af054` | 以 byte 讀取原始類型／色碼。沒有由此證明每個 native 值與 Fire／Water／Wood／Light／Dark／Heart 的逐一映射或所有特殊珠語意。 |
| `block + 0x04` | `countLockedBlocks` `0x6c601c` | 使用 32-bit flags 的 `0x800` 位元作鎖珠計數判斷，且計數另受其他欄位條件影響。不能把單一 mask 推廣成全部效果已解碼，或推定其餘旗標為普通珠。 |
| `block + 0x12` | `swapBlockMain` `0x93ce30` 寫入 3；`calcBlocks` `0x6beaf4` 在動畫後清零；`_checkFlood` `0x928af8` 比較狀態 6 | 這是 uint16 動作／狀態欄位，**不是色碼**。不能採用參考專案把 `+0x12` 當顏色的讀法，也不能只見零值就宣稱整盤已可操作。 |
| `block + 0x14` | flood 與鎖珠計數相關讀取 | 有號 16-bit flood 標記；部分分支使用 `-1`。屬原始狀態證據，不是獨立的可操作判定。 |
| `block + 0x1c`、`block + 0x1e` | `getNearestBlock` `0x9284a8` | 以有號 16-bit 讀取中心位置，交換也更新相關位置欄位。到實際螢幕像素的轉換、縮放、方向與觸控座標仍未驗證。 |

`0x98958 + 0xac * 256 = 0xa3558` 與 initializer、reader、writer 的表位置互相吻合。這能排除「依池位置直接讀 row-major 盤面」的錯誤假設，仍不能代替尺寸、表指標、物件範圍與當前 phase 的即時核對。

## 離線資料與求路預覽

本批分為兩層來源，沒有把未驗證的 native 欄位自動轉成可求路盤面。

第一層是 [離線原生布局解讀](../app/src/main/cpp/BoardLayout.h)：`DecodePad236OfflineRawBoard` 接收呼叫者已持有的片段與明確 game 位址，把這些 guest 位址當作片段中的數值鍵，按已核對的相對偏移解析尺寸、impl、指標表及原始 block 欄位。它不查詢目前程序、不自行尋找 singleton、不讀 `/proc`、不呼叫 ARM 遊戲函式，也不將原始色碼或零狀態宣稱為完整遊戲語意。

離線輸入限制最多 4,096 片段、合計 16 MiB；相鄰片段可共同覆蓋欄位，空片段、位址溢位、重疊或所需資料缺口均拒絕。尺寸限 5×4／6×5／7×6；每個格子指標須非零、在 256 項池內、對齊 `0xac` 且不重複。任何結構錯誤，包括最後一格失敗，均不留下部分 board；格子指標診斷保留來源 slot／所需 8 bytes，讀到但拒絕的指標值另存 `rawPointerValue`。這些界限是本機解析器的限制，不是新的 live 擷取範圍或授權。

結構解析成功也只回 `EvidencePartial`，並附 `RuntimeContextUnverified`。輸出保留 native 行列、池索引、完整 `0x24`-byte 原始前綴、`rawColorByte`、`rawState`、按 offset 命名的 `rawWord04` 及兩個有號位置欄位；不截掉未知色碼或高位狀態、不剝除指標 tag、不轉換螢幕行序或建立 `PHOrbBoard`。這不能證明輸入片段的來源真偽、同時性、phase、settled 或本地操作時窗。

第二層為 [純資料盤面求路預覽](board-preview.md)：`BoardPreview.h/.cpp` 接收由呼叫者擁有的正規化 5×4／6×5／7×6 盤面。六色採明確列舉，顏色、每格效果，以及幾何／消除／起手／移動／盤面效果均預設 Unknown；只有明確 Ordinary 的普通六色子集才交給共用 solver。Unknown 不等於「沒有特殊效果」。這些列舉不是 native 色碼；本批沒有 native decoder 到預覽輸入的橋接。

預覽只產生本機格索引路徑、交換後且消除前的顏色，以及共用核心重播核對的命中／Combo／消除數。Ready 仍可能是安全但未達標候選，必須再看 `goalsMet`；內容完全相同也不能證明是同一回合、資料仍新鮮或存在可操作時窗。尚未連接 JNI、診斷 UI、逐幀讀盤或任何輸入操作。

## 已完成與待驗證

- 已完成上述保存資料的來源／符號與選定入口研究；研究結果只支持局部相對布局。所有完整裝置／布局／操作旗標仍為 false。
- `BoardLayout.cpp`、`BoardPreview.cpp` 與兩份新增 C++ 測試來源已通過 Windows GCC 及 NDK 27／API 26 的 arm64、x86_64 C++17 `-Wall -Wextra -Werror -fsyntax-only`。NDK 首次指出測例 range-loop 複製警告，三處改為 const reference 後各目標重檢通過；未關閉警告。這些都只做語法檢查，沒有產生 object、連結產品或執行新二進位。
- 已寫純資料預覽案例，涵蓋未知／不支援資料、三種尺寸、六色映射、品質額度、偽造統計、非法路徑、斜移、未達標、取消及內容變更，另有普通盤面 solver 整合案例；**案例尚未執行**。不能用上批共用 solver 通過結果作為本批 adapter 的驗證。
- 已寫 [離線布局案例](../tests/board_layout_tests.cpp)，涵蓋三種尺寸、原生行距、池末項／打亂池索引、未知 raw 值、跨片段欄位、稀疏供應、缺口／重疊／溢位、空／越界／未對齊／重複指標、末格失敗及輸入量上限；**尚未執行**。本輪不執行新的布局測試或求路模擬。
- 下一步需取得可核對的 live 全域／game pointer、確認盤面 phase 與受保護資料的解碼、完整珠子／盤面效果，再與畫面逐格比對；之後才接正規化資料、JNI／UI 和前景／請求生命週期。單靠 motion 零值、內容相同、符號或程式擷取不能替代這些證據。
- 下一次明確「編譯測試」後需建置兩種 ABI、執行離線布局與預覽案例及共用核心回歸，再另列裝置驗收。

## 證據保存

可提交的成果只有來源、合成案例、文件及 [研究摘要](../research/board-layout-20261106.json)。原始遊戲程式、IDA 工作庫與完整反組譯保留在本機 `build/android/ida-board-20261106/`，不加入版本控制，也不改既有 `E:/ipa` 交付。

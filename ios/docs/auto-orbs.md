# iOS 自動轉珠原型 — 0.5.0-orbs

這版實作單人「讀盤並求路 → 路徑預覽 → 執行一次／停止」。沒有逐回合自動開始，也不自動使用技能、選關卡或接續結算。CI 驗證的是離線求解、原生記憶體／輸入 fixture 及 iOS 介面；**iPhone 上的真實 PAD 拖珠尚未驗證**。

## 使用

1. 進入普通單人 6×5 關卡，等待操作階段並放開手指。
2. PC 面板 →「自動轉珠 · 讀盤／預覽」→「讀盤並求路」。
3. 核對火、水、木、光、暗、心及強化標記。盤面由上到下、由左到右顯示；滑桿逐步重播，文字標明起點與目前格子。
4. 開啟 PC 功能總開關後可按「執行一次」。頁面收起，等待 0.35 秒後開始。浮動按鈕變成「停止轉珠」。
5. 玩家觸碰遊戲或按停止便中斷。完成後會放開，下一回合需要重新讀盤。

預覽只計算目前珠子的三連消除及既有珠落下的連鎖，不計隨機天降、隊伍傷害、L 字、無效貫通或其他形狀目標。「預計 Combo」不是保證結果或最優解。

第一版只接受 6×5、六種基本珠及非負強化值；7×6、5×4、毒／邪魔／炸彈、鎖定等尚未分類的旗標、轉轉、不可消、固定起點及特殊消除規則會拒絕。core 的 5×4／7×6 離線測試不代表 iOS 已開放這些盤面。多人偵測會鎖定本場，只有離開戰鬥才解除；舊設定的 allowAllMulti 不能覆蓋限制。

## 原生邊界

唯一版本是 iOS PAD 23.6.0／106878，UUID `DD332AD4-4C92-3AAF-85D0-3892556A3A0E`，原始 executable SHA256 `611c93f5472fceddfa27254b718c6eff1decdc6814cc927e2faa4c7c250a65ec`。`orbs-23.6.0.json` 記錄獨立的程式指紋；現有版本／UUID gate 仍生效。

| 位置 | 靜態證據／用途 |
|---|---|
| GameMain +0x70 | 指向目前 battle impl，並與 active impl 及 scene table 比對 |
| GameMain +88/+89；+92/+93 | 行列數、最少消除數及自由移動模式 |
| impl +624832 | 256 個 172-byte block 的物件池 |
| impl +668864 | 16×16 格子指標表；每次重新解析，原生 row 0 位於畫面底部 |
| block +0/+4/+8 | byte 珠型、完整 flags、強化 float 的原始位元 |
| block +18/+24/+26/+28/+30 | 動畫狀態、顯示座標、native nearest 使用的格心；靜止為 state 0 |
| impl +624824／+601668 | 拖珠指標與原生交換次數，用於逐步確認 |
| impl +603024／global +0x1b4ef58 | 原生剩餘操作 frame 與 frame rate |
| impl +738932 | 保護值 fixed-start X；非 -1 時拒絕 |
| impl +739268／+739272 | 原生 match 函式先檢查不可消 effect duration，再按珠色檢查 mask |
| global +0x2ac6fd8 | 2672-byte 觸控 manager，5 筆 raw +5 筆 frame，每筆 264 bytes |

讀盤是純記憶體快照：檢查範圍、池對齊、重複指標、同一 scene/account/entry/wave、受保護值一致性，以及規則、格心與動畫。`_getNearestBlock` 會改 fixed-start latch，因此不拿它當 getter。需要兩份相隔至少 0.15 秒的一致快照才求解。

拖珠沒有改寫珠型、交換表、傷害或操作時間，也沒有建立任意 UITouch、patch 指令或依賴 JIT。只有原始 drawFrame 呼叫期間，把 global input pointer 暫時指向 session 持有的私有 manager。遊戲自己的 raw→frame 更新及玩家輸入函式消費 began/held/ended，原生函式負責交換、扣時間、釋放與消除。`@finally` 立即恢復原始 manager，真實 UIKit 事件始終保存在原始 manager。

這個 manager 使用範圍依據是已檢視的同步原生 frame 與 input lifecycle。iPhone 上的完整 call lifecycle 仍須實測，不能把 fixture 視為真機保證。原生座標轉換函式的基底經求逆與 round-trip 檢查；不假設螢幕像素等於盤面座標。

每一格交換都核對所有 cell tokens、珠型、旗標及原生交換次數，確認後才能前進。預設每步至少 65 ms，最多 32 步；求解依原生可用時間縮短深度，執行另外有 4 秒上限。未接受起點／交換 0.2 秒後中止；恢復指標失敗保留 allocation 與 lease，設 release 並重試，且禁止再次開始。

手動介入時只送自己的 release，真實觸控留待遊戲下一幀處理。場景、樓層、帳號、多人或 input manager 改變時，丟棄意圖，不把舊清理寫進新場景。離開前景會取消工作，回前景不恢復路徑。

## 核心與取消

參考 [pazusoba/core](https://github.com/pazusoba/core) 及 `E:\pad\ultimate` 的新分支。後者的 Python auto 流程確實存在，也已補完 shape 分支；本版沒有把它當成缺 Python 或 shape 空白的 upstream 版本。

iOS 核心是受限的 C++ beam search adaptation；保留 [MIT 授權](../third_party/pazusoba/LICENSE) 及 [來源說明](../third_party/pazusoba/NOTICE.md)。不編譯 Android CLI、OpenMP、x86 BMI、長存 cache 或直接 exit 的路徑。只用一個 serial background worker，預設 beam 256／350 ms，API 上限 512／1000 ms／48 步。候選、路徑與本層 visited 的大小皆有界限。工作持有 immutable board 及取消旗標，換預覽／停止就增加 generation；舊 completion 不得發布新結果。

求解後獨立重播路徑再發布；執行按鈕附帶預覽 generation。45 秒後或真實盤面有變就作廢，避免執行未看過的新路徑。

## 驗證與真機第一輪

- `orb_solver_tests.cpp`：24 個固定種子、三種尺寸；獨立 run/union/gravity oracle；不可變快照、非法跳格、舊 token、取消、時間與 beam/depth 上限。
- `orb_native_tests.mm`：真實布局的記憶體 fixture；上下方向、池重複、旗標／不可消、多人 latch、private input、保留手動輸入、指標恢復失敗及 manager 取代。
- `orb_session_tests.mm`：preview 不注入、每步確認、只執行一次、過期預覽、手動／背景／多人中斷、未接受交換與取消後的舊 completion。
- 原生 GUI probe：6×5 顏色與逐步預覽、generation 轉交、關閉回遊戲；既有編隊 GUI／儲存測試繼續執行。

真機先用普通關卡核對讀盤，再執行一次並測手動停止。若被拒絕或路徑中止，複製 PC 診斷可看到 phase、規則、步數、native move counter、剩餘 frame、solver 耗時與 input fault。未經真機測試前不宣稱操作、實際 Combo 或結算已驗證。

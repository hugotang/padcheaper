# 自動轉珠可行性：pazusoba/core

2026-10-28；研究對象為 [pazusoba/core](https://github.com/pazusoba/core) 的 `493dd14746eb97f275704bef73baa0f8edcf61a8`。`0.4.6-ui` 不含求解器或自動輸入；本文件記錄離線驗證與後續實作條件。

## 結論

**C++ 求路可以移植，建議先做單人「讀盤面 → 顯示路徑」，再驗證「執行一次」。** LiveContainer 無 JIT 並非求解器的障礙：演算法可以用 Xcode 預先編譯成 arm64，再連入現有 dylib。此輪已在 Windows 編譯執行原碼；尚未驗證求解器的 iPhone 效能或自動拖珠。

原 repo 不能原封不動加入遊戲程序。除了需接上 iOS 盤面與輸入，本輪已重現候選陣列分區覆寫、7×6 雜湊越過盤面邊界，以及重用 solver 時的計數污染。另有執行緒競爭及直接 `exit()`，需先整理核心。

## 可重用的部分

| 項目 | 已確認 |
|---|---|
| 語言與依賴 | 核心使用 C++ 標準函式庫及 threads；不需要 Python、OpenCV 或 JIT |
| 授權 | [MIT](https://github.com/pazusoba/core/blob/493dd14746eb97f275704bef73baa0f8edcf61a8/LICENSE)，整合時保留 copyright 及授權文字 |
| 求路 | Beam search，可限制步數與候選數，輸出起點及逐格路徑 |
| 盤面 | 5×4、6×5、7×6；當前只走上下左右 |
| 評分 | combo、指定顏色、連珠數、剩餘珠數及部分形狀評分 |
| 不完整項目 | square／row／column 分支尚空白；不能只憑 enum 名稱宣稱已有無效貫通、橫排及直排策略 |
| 消除模型 | 現有珠的消除與落下連鎖；不包含未生成的隨機天降 |

介面見 [pazusoba.h](https://github.com/pazusoba/core/blob/493dd14746eb97f275704bef73baa0f8edcf61a8/include/pazusoba/pazusoba.h)、[api.cpp](https://github.com/pazusoba/core/blob/493dd14746eb97f275704bef73baa0f8edcf61a8/src/api.cpp)。現有 C API 的結構含 `std::array`，宜另包固定長度的 POD 結果與錯誤碼；ObjC++ 也可以直接呼叫整理後的 C++ 核心。

## 本輪實測與原碼問題

使用未修改原碼、g++ 15.2.0、`-std=c++17 -O2 -pthread`。上游測試程式通過。另以兩份固定盤面各跑五次，深度 25、beam 1000；獨立重播每一步，並另外計算消除／連鎖。結果見 [原始報告](../research/pazusoba-results.json)。

| 固定樣本 | 五次結果 | 桌面耗時，含程序啟動 |
|---|---|---|
| 6×5 | 路徑與結果盤面一致；6 combo | 約 59–73 ms |
| 7×6 | 路徑與結果盤面一致；8 combo | 約 70–82 ms |

這只是兩份盤面的離線樣本，不能據此聲稱找到最佳路徑、各機制皆正確，或 iPhone 也有相同速度。原碼的 `MAX_COMBO` 是估算；執行緒問題未修正，測試成功不表示適合在遊戲內長期執行。

| 問題 | 證據與影響 | 整合前處理 |
|---|---|---|
| 候選輸出分區重疊 | [expand](https://github.com/pazusoba/core/blob/493dd14746eb97f275704bef73baa0f8edcf61a8/src/pazusoba.cpp#L195) 每節點分配三格，卻用方向編號 `i=0..3` 寫 `loc*3+i`。探針確認右移會寫到第四格；相鄰工作者會覆寫同一格，最後分區可能越界 | 用壓縮後的連續輸出索引，或每節點分配四格並只收有效候選 |
| 7×6 雜湊讀到盤面以外 | [hash.h](https://github.com/pazusoba/core/blob/493dd14746eb97f275704bef73baa0f8edcf61a8/include/pazusoba/hash.h#L16) 尋找 NUL，而盤面是 42-byte array。相同 42 顆珠，只改後方配置的 sentinel byte，就得到不同 hash | 傳入明確長度，採 unsigned arithmetic，避免帶符號溢位 |
| 執行緒分工漏算 | [adventure](https://github.com/pazusoba/core/blob/493dd14746eb97f275704bef73baa0f8edcf61a8/src/pazusoba.cpp#L44) 使用整數除法分配，沒有處理餘數。此機 24 workers 的首輪 30 個起點只分到 24 個；workers 為零或大於候選數也未處理 | 第一版使用受控單工作者；後續才加入有上限的固定 worker pool |
| 共用結果資料競爭 | 工作者未同步讀寫 `found_max_combo`／`best_state` | 工作者各自輸出，join 後在單一執行緒選擇結果 |
| solver 重用污染 | 同一 solver 連續 `set_board` 同一盤面，最大 combo 從 8 變 19；`ORB_COUNTER` 未清零，`VISITED` 亦保留 | 每次求解建立獨立物件，並補完整初始化及重入測試 |
| 錯誤直接終止程序 | 無效盤面／profile 等分支呼叫 `exit()`；若直接連入 dylib 會終止 PAD | 封裝所有輸入界限，改傳錯誤，不在函式庫內結束程序 |
| 沒有取消／時間上限 | 目前主要限制步數與 beam；每層還會建立一批 threads | 加 deadline、取消旗標、記憶體上限與可返回的最佳已知結果 |

重現程式為 [pazusoba_probe.cpp](../research/pazusoba_probe.cpp) 和 [pazusoba_benchmark.py](../research/pazusoba_benchmark.py)。探針額外配置 sentinel 空間來檢查越過「邏輯分區」的存取，本身不故意寫到配置範圍之外。這些研究檔案不參與 dylib 編譯。

`adventureEx` 另有先 `set_board`、後 `set_min_erase` 的順序問題；使用四／五顆消除規則時，最大 combo 可能仍依三顆規則建立。形狀與指定顏色條件也應用獨立盤面樣本重驗，不把「最大 combo」當作隊伍傷害最佳化。

## iOS 仍需驗證的兩段

**盤面快照。** 現有 iOS 程式能讀戰鬥階段、觸控狀態及拖珠旗標，尚無經本版 manifest 校驗的完整讀盤面介面。Android 的 `native_probe.c` 有 6×5 讀盤面與簡易求解，但其位址、珠型及座標不能直接用於 iOS。需確認 iOS 陣列、實際行列數、珠型及特殊狀態，並保存戰鬥身分與盤面版本。求解只使用一份不可變快照。

**拖珠執行。** iOS 現有 hook 已辨識 raw/frame 觸控整理與玩家操作流程，見 [原生輸入證據](battle-input.md)。但目前只讀取它們；沒有已驗證的自動 began/moved/ended adapter。UIKit 沒有供一般 app 建立任意 `UITouch` 的公開介面，WebView 的拖曳事件也不會自動成為遊戲觸控。需在 PAD 程序內接上經確認的輸入路徑與座標轉換，逐 frame 推進，並驗證放開／取消會完整結束拖珠。

執行前重新確認單人、同一戰鬥與盤面、操作階段、剩餘操作時間及沒有手動觸控。換場、切樓、變更盤面、跳出視窗、退到背景或使用者介入時，丟棄舊路徑並結束自己建立的輸入。求解不在遊戲主執行緒內阻塞；主執行緒只負責快照及少量逐 frame 操作。

## 建議順序

1. 整理求解核心：修正分區、hash、初始化及錯誤處理；以單 worker、固定種子樣本和獨立重播驗證。補足取消與時間預算。
2. 單人讀盤面及路徑預覽：先支援普通 6×5、六種基本珠，畫出起點、方向、步數及無天降 combo。由玩家確認盤面辨識是否一致。
3. 加入「執行一次」與明確停止：先以短路徑驗證每步座標、時序、手動中斷及放開；通過後才延長路徑。7×6 須額外驗證讀取、座標及 hash 修正。
4. 再評估逐回合自動執行，以及 L 字、多色、無效貫通等策略。轉轉、不可消、雲／遮蔽、炸彈、特殊消除或固定起點在未建模前應停止求解，不能假設普通盤面策略適用。

多人不列入這條實作路線。現有多人停用規則保持適用；單人完整戰鬥與結算仍應分階段真機確認。

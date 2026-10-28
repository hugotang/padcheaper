# 原型實作與待驗證項目

此文件描述本 repo 的可編譯原型，目標為 PAD 23.6.0／106878。2026-10-27 使用者回報 `0.2.0-gui` 在 iOS 27、自訂 LiveContainer、無 JIT 下 GUI 及 Skin／BGM 生效；儲存／重啟、伺服器接受、帳號隔離與計時未回報通過，詳見 [真機測試記錄](device-testing.md)。

`0.4.5-solo` 將戰鬥與計時寫入限定為單人，撤回 [多人套隊及補回](three-player-entry.md)。非零 multi、mode 7、或本場曾偵測多人均在一般過濾規則之前拒絕。舊 config 的 multi 清單仍可解析，但不能授權多人寫入；GUI 移除多人解鎖控制。

多人 readState 只解碼身分、phase、HP、輸入等基本資料；不讀取隊員／bank，不呼叫可能更新狀態的原生 getter 或 digest。Session 清空自動入場、手動套用、補回、還原與租用意圖；引擎在每次公開 mutation 重新讀取 live 狀態，內部還原及參數歸還也有單人檢查。原生呼叫期間若 selector 變更，停止後續寫入，保留未能確認回復的狀態，不在多人釋放舊指標。仍附著的 native allocation 由遊戲場景清理處理。只有確認離開戰鬥或帳號／遊戲映像變更才解除本場停用。

單人操作排入主執行緒 drawFrame，核對帳號、入場 hash、來源、phase、觸控／拖珠、保護值與原生摘要。手動套用每 frame 最多一名，保留充能並按新技能上限裁切；最多追蹤六名隊員。還原或關閉功能會在相同單人戰鬥的可操作階段恢復；縮小 mode／source 清單時使用已驗證的原交易設定還原。帳號、地下城或遊戲接管來源後不覆寫舊快照，不清除原生異常旗標。

自動入場預設關閉。先觀察到地下城外，再逐 frame 等待 wave 0、非恢復、開場旗標及 phase 0，在第一個符合校驗的回呼完成整隊、原生 SB 和有效最大 HP 初始化。無待執行工作的診斷仍保留 250 ms 節流。續關、中途啟用和後續樓層不重新發放 SB。建構時已生成的 7×6 等盤面不重建；詳見 [入場數值與時機](entry-derived-state.md)。單人套隊／SB 已有使用者回報，HP、技能及結算仍需真機核對。

## 已實作

- 建構子只排程初始化；先在識別出 PAD guest 後建立 config／status 和浮動 GUI，通過 Bundle ID、版本、iOS build、arm64、UUID 及五組 64-byte __TEXT 校驗後才安裝 hooks。
- 支援 LiveContainer 改寫 Bundle ID 的模式，僅在 `doUseLCBundleId` 為真時採用 `LCOrignalBundleIdentifier`；其他身份及程式校驗維持不變。
- 先嘗試 LiveContainer 的 getGuestAppHeader，再用 dyld UUID 掃描及 Objective-C 方法 dladdr 後備辨識；不假設影像 index 0，所有路徑仍檢查 UUID。
- UIKit 浮動按鈕與可捲動面板、功能開關、自動儲存、偵測 API 套用、重新檢查和複製診斷；GUI 更新獨立於遊戲 frame，校驗失敗仍可見。
- 浮動 UIWindow 不成為 key；視窗外部 hitTest 穿透，等待可用 scene／guest 視窗並處理背景／旋轉。
- 在 ViewController.drawFrame 及 restoreFromBackground 替換 IMP，保留並呼叫原方法；NSURLSession sharedSession 以實際 class 安裝本地覆寫。
- 使用 Mach API 檢查及讀寫整個記憶體範圍，不改頁面權限，不寫可執行 mapping。
- 128-byte 皮膚／BGM 擁有位圖只設定表中已存在、index < 1024 的 ID；條目 flags 保留其他 bits。
- 複製 save_decks request，原子保存選擇後預設化已啟用欄位，保留未知 body 參數及 Content-Length 一致性。
- 交易綁定 account epoch、原生 API context、request block、bank／slot 和隊伍摘要；網路 completion 不直接改遊戲記憶體。
- frame 原方法返回後，觀察 native request 已移除且 native error 為 0，才回蓋 matching default 值。HTTP 200 及 JSON res=0 另外核對。
- JSON 選擇保存為解碼值，按玩家候選 pid 分檔；同隊未啟用欄位保留、明確選擇 0 可更新。
- `0.2.1-gui` 用同一同步交易狀態處理 save／completion／parse。每次新儲存先取消舊回蓋，包含無改寫、停用及本地保存失敗的分支；過期或被替代的 completion 不改最新狀態。面板及 status.json 顯示本地保存、已追蹤請求的 HTTP／res 成功、原生解析與回蓋結果，不輸出帳號或隊伍內容。
- 徽章驗證參數表長度、初始化終點、11 個保護欄位及相依鏈，再修改有效 ID 的 XOR 擁有位圖；關閉時只移除本模組新增的 bits，登入後重設追蹤。保存與回蓋沿用交易機制及 bank 內索引。
- 計時自動路徑限一般單人、倒數上限大於 0、phase 2／30，設置 alert latch，僅在 paused bit 同時存在時清除 pause／abandon。原生 phase 跳轉只由面板手動撤窗觸發，限定 phase 58／pending 32，保留一般提示窗及正常結算。詳見 [徽章／計時](badge-timer.md)。

## 額外的靜態時序證據

在原設計之外，原型採用下列 IDA 位址；均是這份二進位的未套用 ASLR VA：

| 項目 | 格式 |
|---|---|
| API context 指標 | S+0xF65638 |
| context active request | context+0xE08，指向包含 8-byte header 的 block |
| request ID／flags | block+8 的 u32；block+32 的 u8 |
| context native error | context+0x101748 的 u32 |
| 接收完成 state／phase | H+0x2AE8EE8／H+0x2AE8EEC 的 u32；正常 ready 為 2／6 |
| 編成 dirty | S+0x5305AC 的 u8；非零時不回蓋 |

0x10045B908 確認 state=2 時返回 phase；0x100591470 在 phase=6 呼叫 response dispatcher。0x10058FEB8 清除 request 的 array／response 指標並移除 queue node。0x100687DB0 寫 native error；登入（33）及 save_decks（122）會寫隊伍。

這些欄位提供可用的解析前後觀察，不等於已驗證 iOS 27 上的所有佇列／執行緒分支。

## 原型限制

1. 首次檢查是診斷模式；面板總開關及個別功能需開啟。Skin／BGM 啟用需套用精確 API host／path，且已觀察原生登入成功；浮動按鈕出現不等同所有功能已啟用。
2. 只處理 save_decks，未完整稽核 upload_data、戰鬥、多人或其他 action；不宣稱伺服器永遠只看到預設值。
3. 皮膚／BGM 資源須已存在；dylib 不提供音訊或圖像，也不新增資源下載權限。
4. 原型回蓋以相同 bank、隊伍數量、slot 及五個卡片 ID／extra u32 匹配。隊伍重排、重複隊伍及跨 bank 移動須真機測試，無法匹配就跳過。
5. 如有未結束或並行的 save_decks 交易，仍保存及預設化最新請求，但不猜測立即回蓋；本地檔保留至重新登入時再匹配。這種情境及重試／取消需真機核對。
6. 同一 frame 的 UI／戰鬥快取可能已經讀過回應預設值。原型不呼叫尚未驗證的原生快取重建函式。
7. 遊戲登入可覆寫位圖；登入後重設追蹤。條目表重載／合法 ownership 變化與功能關閉還原需驗證，不能聲稱完全等價 Android。
8. pid 是 save_decks serializer 的玩家身分候選；帳號 A→B→A、重啟及容器隔離需真機驗證。
9. 徽章只開啟參數有效、相依鏈可讀且在 1–255 範圍內的條目；未實作替換無效參數或攔截原生徽章 predicate。編成畫面快取、實際效果、取得新徽章時的 ownership 更新與重啟保留仍須真機驗證。
10. timerFix 的 countdown、phase 與彈窗限制依這份 iOS 二進位核對；不宣稱所有排行榜、固定五分鐘計時、多人、棄權或續關分支均支援。自動路徑不干預 phase，手動撤窗後流程與結算未驗證。

CI 驗證包含 iPhoneOS 編譯、arm64／簽章／Mach-O 檢查、macOS Foundation protocol／身份／設定／儲存交易測試，以及載入相同 runtime 的 iOS 模擬器 GUI 檢查。交易測試涵蓋選擇改回 0、舊 completion、新儲存寫檔失敗、重疊請求、原生錯誤、逾時和帳號切換。模擬器刻意使用不匹配 UUID，確認 GUI 出現而 hooks／寫入保持停用，並驗證開關儲存、API 套用、複製診斷、儲存狀態、觸控穿透和橫向畫面。真機行為不能由 CI 推論；使用者已回報通過的範圍另記錄在真機測試文件。

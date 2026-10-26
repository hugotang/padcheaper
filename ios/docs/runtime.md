# 原型實作與待驗證項目

此文件描述本 repo 的可編譯原型。靜態目標為 PAD 23.6.0／106878；所有真機驗證仍待完成。

## 已實作

- 建構子只排程初始化；通過 Bundle ID、版本、iOS build、arm64、UUID 及五組 64-byte __TEXT 校驗後安裝 hooks。
- 先嘗試 LiveContainer 的 getGuestAppHeader，再用 dyld UUID 掃描；不假設影像 index 0。
- 在 ViewController.drawFrame 及 restoreFromBackground 替換 IMP，保留並呼叫原方法；NSURLSession sharedSession 以實際 class 安裝本地覆寫。
- 使用 Mach API 檢查及讀寫整個記憶體範圍，不改頁面權限，不寫可執行 mapping。
- 128-byte 皮膚／BGM 擁有位圖只設定表中已存在、index < 1024 的 ID；條目 flags 保留其他 bits。
- 複製 save_decks request，原子保存選擇後預設化已啟用欄位，保留未知 body 參數及 Content-Length 一致性。
- 交易綁定 account epoch、原生 API context、request block、bank／slot 和隊伍摘要；網路 completion 不直接改遊戲記憶體。
- frame 原方法返回後，觀察 native request 已移除且 native error 為 0，才回蓋 matching default 值。HTTP 200 及 JSON res=0 另外核對。
- JSON 選擇保存為解碼值，按玩家候選 pid 分檔；同隊未啟用欄位保留、明確選擇 0 可更新。
- 計時實驗只清背景恢復 bit 5，保留 bit 1／6。

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

1. 首次檢查是診斷模式；啟用實驗寫入需設定精確 API host／path，且已觀察原生登入成功。沒有設定畫面。
2. 只處理 save_decks，未完整稽核 upload_data、戰鬥、多人或其他 action；不宣稱伺服器永遠只看到預設值。
3. 皮膚／BGM 資源須已存在；dylib 不提供音訊或圖像，也不新增資源下載權限。
4. 原型回蓋以相同 bank、隊伍數量、slot 及五個卡片 ID／extra u32 匹配。隊伍重排、重複隊伍及跨 bank 移動須真機測試，無法匹配就跳過。
5. 如有未結束或並行的 save_decks 交易，仍保存及預設化最新請求，但不猜測立即回蓋；本地檔保留至重新登入時再匹配。這種情境及重試／取消需真機核對。
6. 同一 frame 的 UI／戰鬥快取可能已經讀過回應預設值。原型不呼叫尚未驗證的原生快取重建函式。
7. 遊戲登入可覆寫位圖；登入後重設追蹤。條目表重載／合法 ownership 變化與功能關閉還原需驗證，不能聲稱完全等價 Android。
8. pid 是 save_decks serializer 的玩家身分候選；帳號 A→B→A、重啟及容器隔離需真機驗證。
9. 徽章未實作，不能只填擁有位圖；參數表及編成 XOR bank mapping 未全部解決。
10. timerFix 僅限目前靜態確認的 GameMain 背景恢復分支；其他計時／棄權／續關情境未驗證。

CI 驗證包含 iPhoneOS 編譯、arm64／簽章／Mach-O 檢查，以及以 macOS Foundation 執行的 protocol 測試。真機載入、伺服器接受、回蓋正確性和帳號安全性不能由 CI 推論。

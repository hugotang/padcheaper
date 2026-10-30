# 徽章保存、BGM 與策略檔案：0.5.8

0.5.8 修正「徽章開關仍開啟，重啟後選擇卻回預設」，並擴查同類保存／還原問題。新增未通關隊伍 BGM 開關與 JSON 分享／檔案匯入。編譯結果以 [開發紀錄](development.md) 為準；新的遊戲內路徑仍待真機驗收。

## 保存問題與修正

| 問題 | 影響與修正 |
|---|---|
| 舊版把 request 122 當作 save_decks | IDA 重查 send／response dispatcher 證實 save_decks 是 **58**，122 是 recv_team。舊版不能配對正常儲存後的 native commit，會顯示等待回蓋逾時。0.5.8 改為精確 request 58 配對。 |
| 原生 serializer 將不通過 hasBadge 的徽章寫成 wire 0 | wire 0 不一定是使用者選預設。只從同一 request 的原始 records 補回，核對卡片／extra、BGM、skin、非零 wire badge 及前後 pointer/count。無法確認時保留同身分隊伍的舊徽章；確認 raw 也為 0 則清掉舊選擇。 |
| 登入後徽章參數尚未載入 | 回蓋改為每欄追蹤。資料或 dirty 尚未就緒時等待，寫入失敗保留重試，成功讀回後才清待辦。新 save、帳號 epoch 改變或明確關閉該功能取消舊待辦。 |
| 帳號編號先截斷再比較 | account 檔案嚴格核對 schema、完整數值、target 與各 bank。拒絕 Boolean、字串、小數及超過 u32 後截斷恰好相同的帳號編號。 |
| 關閉 Skin／BGM 後，還原失敗卻清掉追蹤 | ownership bits 和 descriptor flags 皆保留到寫入讀回確認。暫時無法讀表保留待辦；已換表、ID 改變或原生 flags 改變則放棄舊 lease，避免之後誤覆寫。 |
| JSON 只在讀前檢查檔案大小 | 現在使用有上限的讀取，並在解碼前再查實際長度；不因 metadata 與實際內容不同而讀入任意大小。 |

選擇仍按 account／bank／slot 和五個卡片 ID＋extra 配對；不是靠隊伍名稱猜測。回蓋不改後來已非零的原生選擇。兩個 bank 的持久化徽章用各自 local index 編碼，request records 則用 primary＋secondary 的 global index。

`PHResetAccount` 清掉由登入重建的 ownership bits 追蹤；descriptor flags 屬資源表，僅在表位址、ID 和本模組留下的 flags 仍吻合時保留還原權利，並不盲目用上一帳號資料覆蓋新表。無法觀察到的相同記憶體重用仍屬靜態分析限制。

診斷新增 `Deck restore`，列已回蓋欄位、待重試隊伍、身分不符、無效徽章及本次寫入失敗。新版本無法從一份早已被舊版存成 0 的檔案推回原本徽章；更新後需在遊戲再選擇並正常儲存一次。

## 未通關隊伍 BGM

0.5.9 更正資源表與名稱查找：該表是已註冊曲目，並不表示檔案已載入；原生比對忽略 ASCII 大小寫、以指定名稱作前綴，會接受 `BGM_972` 對 `bgm_972.caf`。詳見 [擴盤與 BGM 修正](opening-resize-music.md)，以下保留 0.5.8 的開發紀錄。

開啟 BGM 後，可另開「未通關也使用隊伍 BGM」，預設關閉。從遊戲目前選定的隊伍讀取道中／頭目曲目，單人穩定操作階段使用已載入的音樂資源，切入頭目波次時依設定換曲。未指定曲目、RND 隨機或尚未載入資源時保持原生音樂；資源就緒後重試。

本功能唯讀辨識通關資料，**不寫通關旗標**。原本已通關的選曲交由遊戲處理。保留 mode 11、固定六人關卡及原生特殊關卡限制；所有非零 multi／mode 7 仍禁止，偵測後本場鎖定。關閉功能時只在同一戰鬥、override／target 仍是本模組的值時復原；退出或新 battle hash 不用舊指標還原。

遊戲的直接音樂 override 或關卡事件音樂接管時，本模組讓回選曲權。已取得的 override 只在未被原生改動時釋放；不在同場反覆搶回。普通更新快取目前音軌索引，但每次比對實際資源名稱後才重用。

IDA 依據：隊伍選曲 `0x1005A4D8C`，最終選曲 `0x1005A5240`，原生 override setter `0x1005A53B8`，refresh `0x1005A4CA8`，通關查詢 `0x10023B184`，已載入音軌查詢 `0x1004EEDD4`。`BGM playback` 顯示啟用、實際接管及原因。替身測試不能證明 iPhone 已載入所有曲目。

## 分享與匯入

在「自動轉珠 → 隊伍／關卡策略」選一份策略，按「分享策略檔案…」開啟 iOS 原生分享面板，使用者可選 AirDrop、「儲存到檔案」或其他 App。普通策略輸出 `padcheaper-orb-profile`；包含隊伍／開場者輸出既有 `padcheaper-team-plan`，兩者都保留循環、目標、品質、低血心珠和荊棘偏好。

接收後按「從檔案匯入 JSON…」，或沿用貼上 JSON。先驗證並放入預覽草稿，按「儲存並選用」成功後才生效。相同 ID 拒絕覆蓋；需保留副本時先複製策略產生新 ID。單檔上限 128 KiB，策略庫最多 64 份；版本／target／格式不符拒絕整份。

分享檔案僅含所選策略，不含 PID、SID、帳號快照、API endpoint、日誌或總功能開關。來源網址是文字，不會執行。臨時檔在完成／取消／視窗釋放時清理；AirDrop 是否可用由裝置與系統決定。CI 只測原生面板、取消及檔案 delegate 讀取，不向其他 App 或人實際傳送檔案。

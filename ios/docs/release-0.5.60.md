# 0.5.60-orbs：B4 六色四消搜尋與舊計畫啟動檢查

**0.5.60 編譯測試與交付完成**：[CI 191](https://github.com/hugotang/padcheaper/actions/runs/44690242863) 在 `71a0e32e14bee22682f1a523803312186cf166f7` 通過完整原生／Session、ASan/UBSan、iPhoneOS／簽章、瀏覽器及四份 iPhone GUI；Windows／macOS 的 B4、B5 各 24/24 通過，獨立共核對 96 路線／576 色相。新交付為 `E:/ipa/PADCheaper-0.5.60-orbs` 與 ZIP，配套 r7。真機傷害、手機時間、冷卻與全程通關仍待驗證；詳見 [開發紀錄](development.md)。

B4 四張歷史原盤在 Windows 原 48 步、800／2000ms 各三次搜尋全部通過（24/24），B5 四盤同樣 24/24。獨立 Python 重播全部路線及 288 種轉轉色相，確認首消六色各四顆。基線 B4 只有 6/24，正式搜尋現加入手持珠位置與恰好四顆稀有色的連續拖曳距離，保留不同端點；建表及搜尋皆計入原預算，沒有原盤辨識或預存答案。

珊瑚開場會檢查 B4／B5 的必要出手條件。舊 r5／r6 或改名副本缺少內容時，在第一個技能、點敵人或轉珠前停止並說明；策略頁直接顯示計畫 ID。舊檔仍可保存與匯出，同場已有動作後的防重播限制維持。

此引擎配合 **coral-fixed-585607-v90-r7**。更新 dylib 後，匯入並選用交付包的 fixed-plan.json，從自行開始且尚未操作的新場 B1 啟動。B5 存技盤、技能順序與所有盤數上限維持。

Windows 可攜回歸與私有搬運／幾何測試通過；完整 macOS／Session、ASan/UBSan、iPhoneOS／簽章、瀏覽器與四份 iPhone GUI 已在上述 CI 通過。B4／B5 普通原盤驗收已納入建置門檻。真機手機預算、傷害、B6→B7 冷卻與全程通關仍未驗證，device_verified=false。完成狀態見 [開發紀錄](development.md)與 [驗證收據](../research/verification-0.5.60.json)。

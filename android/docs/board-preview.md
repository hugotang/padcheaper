# 純資料盤面 → 共用求路器 → 預覽資料

2026-11-06 新增來源基礎；沒有 Android 即時讀盤器、JNI 呼叫入口或顯示按鈕。`BoardPreview.h/.cpp` 接收呼叫者擁有的正規化資料，連接既有 `PHOrbSolve`，輸出可供未來畫面使用的路徑格索引與盤面。此接口沒有原生偏移、遊戲地址、觸控、技能、計時器、封包或其他遊戲操作；既有目標與報告的 verified／enabled 旗標保持 false。

## 接口與拒絕條件

`BoardPreviewSnapshot` 採頂排先、由左至右的格序，支援共用核心已有的 5×4、6×5、7×6。六色以 Fire／Water／Wood／Light／Dark／Heart 明確列舉，再逐項轉成核心的 0–5，**不是宣稱 Android 原生色碼等於這些數值**。每格顏色和效果，以及幾何、消除規則、起手、移動及盤面效果，預設均為 Unknown。只有呼叫者明確填入 Ordinary 才接受；沒有填寫不能當成已確認「無效果」。

第一版只接受普通六色、沒有珠子特殊效果、沒有轉轉／封條／雲等盤面效果、沒有固定或其他起手限制、沒有自由移動或其他移動限制、所有六色可消及三珠消除規則。未知／不支援的規則、色碼、格子效果一律拒絕。盤面以外格子必須保留 Unknown。Unknown／Unsupported 是純資料狀態，不是 native layout、裝置或操作窗口的驗證旗標；未來 reader 必須以 Android 證據填入資料。

`BoardPreviewRequest` 只接受共用目標、Fast／Balanced／Thorough 品質與斜移選擇。使用核心既有的 24／128／120、32／256／350、48／512／800（步數／beam／ms）設定，沒有額外重試。禁止內部的敵人首消政策旗標及非零未使用目標；沒有暴露傷害、心四解毒、釣珠、活動、自動化或任意搜尋時限接口。合法但缺珠的目標可以得到 InsufficientOrbs；GoalsUnmet／NoRoute 不能用來證明盤面無解。

## 求路結果與內容比對

`NormalizeBoardPreview` 在成功前只處理本機值，拒絕時清空輸出。核心需要的 tokens 使用 1-based 本機格識別，座標保持零；它們沒有遊戲地址或畫面位置語意。`SolveBoardPreview` 接收盤面／請求的值拷貝及呼叫者的取消旗標，預期由單一呼叫者工作執行緒執行。沒有在逐幀 hook 或 UI 執行緒自行啟動，也沒有自行管理執行緒或排程。

回傳路徑必須符合請求的步數、相鄰規則、goalCount、狀態／達標一致性及既有搜尋深度／pass／backtrack 上限。`CheckBoardPreview` 再經 `PHOrbReplay`、`PHOrbEvaluateRoute`，核對命中數、Combo、消除數、安全狀態及其他評估欄位；失敗時不留下路徑或預覽盤面。這是對回傳結果的共用核心重播核對，**不是獨立實戰 oracle**。

Ready 表示有通過核對、可供解釋的候選，仍須看 `evaluation.goalsMet` 區分達標與安全但未達標。`draggedColors` 是最後一個交換完成、消除／重力前的盤面；Combo／消除數可包括確定的既有盤面連鎖，不估計隨機天降或遊戲得分。NoRoute 沒有可用 route 統計，另外保存整次求路的 wallMicroseconds；不能從清空的 elapsedMS／deadline 推論未逾時。開始前取消或求路結束時已取消均不回傳路徑，未來 UI 發布前仍須核對自己的取消／請求生命週期。

`BoardPreviewResult` 保存完整盤面與請求。`BoardPreviewMatches` 對每個語意欄位逐項比對，包括所有格子、規則、目標、品質和斜移；不用有碰撞可能的摘要，也不用含 padding 的記憶體比較。它只能確認「內容仍相同」，不能識別不同回合重現同盤面，不能證明新鮮度、單人／多人模式、操作窗口或允許任何遊戲輸入。上述上下文須由未來 reader／session 另行驗證。

## 待驗證

已寫 `android/tests/board_preview_tests.cpp`，涵蓋未知／不支援規則與色碼、非標準尾格、三種尺寸、六色映射、固定品質、請求拒收、偽造／非法路徑、斜移、未達標預覽、完整內容變更、取消及一個普通盤面求路整合案例。GCC C++17 `-Wall -Wextra -Werror -fsyntax-only` 對新 adapter 與測例來源通過；這沒有產生二進位或執行案例，測例仍只代表待執行驗收。本輪沒有建置 APK、執行測試套件、操作裝置、派發 CI 或打包；前版通過結果不驗證這批來源。下次明確「編譯測試」需重建兩種 ABI、執行新 C++ 案例及共用核心回歸，再處理後續 reader／UI 接入。

本輪已有局部 Android getter／writer、寬高、table／pool／state 的[布局證據](board-layout-2026-11-06.md)；即時全域物件、完整特殊珠語意、穩定 phase 與逐格畫面對照仍待驗證。純資料 adapter 與 raw native decoder 尚無自動橋接。程式擷取或符號恢復不能代替剩餘確認。`targets/pad-23.6.0.json`、既有探針、報告與交付物未因純資料接口取得新的讀盤或操作資格。

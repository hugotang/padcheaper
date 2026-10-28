# 編譯及載入 PADCheaper

目標：jp.gungho.pad 23.6.0，CFBundleVersion 106878，arm64。其他版本會停止安裝 hooks。

`0.5.1-orbs` 源碼加入普通單人 6×5／7×6／5×4、求路品質、形狀與部分隊長盤面條件，以及「開始連續轉珠」和可設定技能規則；保留記憶體讀盤、逐步預覽與「執行一次／停止」。技能按位置、回合間隔、樓層、缺珠及四種敵人效果判斷，目標未完整達成時停止轉珠。這批尚未編譯測試，等使用者指示後集中建置。用法見 [自動轉珠](docs/auto-orbs.md) 與 [連續／技能規則](docs/orb-automation.md)，解析界線見 [形狀與隊長盤面](docs/orb-goals.md)，狀態見 [待驗證清單](docs/development.md)。

使用者已於 2026-10-28 回報 `0.5.0-orbs` 測試通過，記錄為單人 6×5 基準版本的真機通過回報。

第六批源碼加入低血時的心珠目標：技能後仍低於門檻才要求 1–3 組心珠，與原有形狀／隊長目標同時滿足；不足或未達標停止。預設關閉，設定隨策略保存，詳見 [低血心珠](docs/orb-recovery.md)。尚待集中編譯測試。

第五批源碼加入有效 HP 門檻、我方異常與必要技能：先讓其他技能或同組替補補救，仍未確認必要施放才停止轉珠，並顯示逐條原因。詳見 [HP 自救與必要技能](docs/orb-survival.md)；新增內容仍待集中編譯、測試與真機確認。

同一未建置批次另加入六組技能輪替、本場一次、位置保留樓層、最多 64 份隊伍／關卡策略，以及鎖珠求路與妨礙盤面觸發條件。轉轉可觸發指定解除技能，效果仍在時停止；尚無時序求路。詳見 [技能循環、策略與妨礙盤面](docs/orb-strategies.md)。

保留手機編隊六人總覽、角色編輯面板、上一格／下一格、本體／武裝切換、整格交換／複製、撤銷及最近使用角色。滿級／滿技顯示實際數字，保留雙指縮放。

儲存成功必須收到原生檔案寫入確認；失敗保留草稿及編輯視窗，不變更下次入場隊伍。「本場已套用」核對實際接受的本體、武裝及培養設定，不會因切換隊伍庫而沿用另一份配方的標記。詳見 [編隊及狀態說明](docs/formation.md)。

本版延續 `0.4.5-solo` 的**多人戰鬥寫入限制**。使用者回報 0.4.4 的隊伍只在本機顯示替換，其他玩家仍見原隊伍、數值未如預期重建，並發生斷線。先前本機替換／函式替身測試不足以證明多人可用，現已撤回多人套隊與同步後補回。斷線的確切服務端原因尚未確認，依據見 [多人限制](docs/three-player-entry.md)。

單人套隊、初始 SB、入場目前 HP 初始化與原生隊伍刷新保留。使用者已確認 0.4.1 的單人入場套隊及 SB 成功；新版 HP、實際技能效果及結算仍需真機核對。用法見 [六人編隊](docs/formation.md)，數值及時機邊界見 [入場數值核對](docs/entry-derived-state.md)。

所有非零 multi 及 mode 7 均拒絕戰鬥套用、變身、還原、SB／HP 寫入及計時干預。舊設定中的 `allowAllMulti: true` 也不能解除限制。一旦偵測多人，本場保持停用，直到確認離開戰鬥。多人診斷只讀取記憶體中的基本狀態，不呼叫原生 bank getter 或 digest。更新時完整關閉遊戲，再替換舊 dylib。

## GitHub Actions

iOS 已採用手動建置流程：推送及 PR 不會啟動 [.github/workflows/ios-dylib.yml](../.github/workflows/ios-dylib.yml)。收到使用者「編譯測試」指示後，在 Actions 選擇 **Build iOS LiveContainer dylib → Run workflow**，選取要驗證的分支；也可使用 `gh workflow run ios-dylib.yml --ref <branch>`。

完成後下載 `PADCheaper-iOS-arm64-<run number>` artifact，內容為：

- PADCheaper.dylib
- config.example.json
- SHA256SUMS、build-info.json
- 本文件、formation.md、feasibility.md、runtime.md、device-testing.md、monster-replacement.md、badge-timer.md、team-presets.md、target.json、battle-target.json、LICENSE

workflow 執行真正的 arm64 iPhoneOS 編譯，並檢查隊伍 JSON、LiveContainer 原始 Bundle ID、設定格式、儲存交易狀態及 iOS 模擬器 GUI。模擬器檢查會載入相同 runtime，驗證校驗失敗時 GUI 仍顯示、設定開關儲存、API 套用、觸控穿透及橫向畫面；另外檢查這份 PAD 使用的舊式 AppDelegate 視窗及 LiveContainer 改寫 Bundle ID 的模式。`PADCheaper-GUI-probe-<run number>` artifact 包含檢查報告和畫面。2026-10-27 使用者回報 `0.2.0-gui` 在 iOS 27／自訂 LiveContainer 無 JIT 下 GUI 正常、Skin／BGM 已生效；CI 不替代儲存／重啟等真機驗證，詳見 [真機測試記錄](docs/device-testing.md)。

## 本機建置

macOS 安裝完整 Xcode 並選取其 Command Line Tools 後，在 repo 根目錄執行：

```bash
bash ios/test.sh
bash ios/build.sh
```

輸出在 build/ios/。預設最低 iOS 15.0，只生成 arm64 slice；無需 Theos、Substrate、JIT、第三方 Hook framework 或 GitHub secrets。

## LiveContainer 安裝

1. 完整關閉 PAD，移除舊版或重複的 PADCheaper.dylib。
2. 在 LiveContainer 的 Tweaks 畫面按 **＋ → Add Tweak**，選擇新下載的 PADCheaper.dylib。若放入子資料夾，須在 PAD 的 app 設定中選取相同 Tweak Folder；dylib 直接放在該資料夾內，方便 LiveContainer 簽署。
3. 確認未啟用 `dontInjectTweakLoader`，dylib／資料夾名稱沒有 `.disabled`，並由 LiveContainer 使用有效憑證重新簽署 tweaks。CI 的 ad-hoc 簽章不是 iPhone 裝置簽章。
4. 啟動 PAD，在遊戲視窗出現後應看到 **PC** 浮動按鈕；可拖動至左右邊緣，點擊開啟面板，按 × 收起。按鈕／面板外的觸控會穿透，遊戲維持 key window。
5. 新版完成建置後，面板標頭會顯示 `0.5.1-orbs`，載入、功能及儲存狀態每秒更新，開關自動儲存。編隊與清單輸入期間使用可輸入的視窗，儲存並關閉後返回原遊戲視窗。

浮動按鈕的含義：

| 狀態 | 含義 |
|---|---|
| 藍色「就緒」 | dylib／GUI 已載入，功能關閉或尚未選擇 |
| 黃色「載入」 | 初始化或等待登入、API、遊戲資源表等條件；點開查看原因 |
| 綠色「啟用」 | 至少一項功能通過啟用條件；面板會標明個別功能及部分啟用狀態 |
| 紅色「錯誤」 | dylib 已載入，但遊戲校驗、hook 或儲存失敗；仍可複製診斷 |
| 紅色「停止自動／停止轉珠」 | 點擊立即取消自動流程；求路、技能動畫及等待回合也可停止 |
| 沒有 PC 按鈕 | 載入或視窗顯示未完成；不能只憑此判定是哪一步失敗 |

已支援 LiveContainer `doUseLCBundleId` 模式：從 `LCAppInfo.plist` 的 `LCOrignalBundleIdentifier` 識別原始 ID，版本／build／UUID／程式校驗仍需匹配。GUI 與設定檔會在識別出 PAD guest 後、精確校驗之前建立。

資料目錄是 **遊戲 guest 容器內**的 `Library/Application Support/PADHelper/`，不是 LiveContainer 本身的外層 HOME。

## 設定與功能

首次啟動會建立 config.json。預設值見 [config.example.json](config.example.json)，所有寫入關閉；可直接用面板設定。

1. 保持診斷模式，在遊戲內儲存一次隊伍，讓面板偵測 API。
2. 點 PC，按 **套用偵測到的 API**；只儲存 host／path，不記錄 sid、key 或完整 query。
3. 開啟 **功能總開關** 和要使用的 **Skin／BGM／隊伍徽章**。面板會顯示「已套用」、有效徽章數量或尚在等待的原因。
4. 要測試背景計時修正，可另外開啟該開關；它不依賴 Skin／BGM 的 API 設定。

PC 面板的「單人入口設定」可修改 mode／source 的全部允許開關及整數清單；多人操作不能由設定開啟。這些選項與功能開關共用 config.json，儲存後每秒重新讀取。也可手動編輯 `battle`、`experimentalWrites`、`features`、`apiHosts`／`apiPaths`。無效設定會停用修改並顯示錯誤，切換開關可重新儲存有效設定。

皮膚／BGM／徽章寫入同時要求 target 校驗、frame／network hook、已觀察的主執行緒 frame、成功登入解析及可用本地儲存。若登入早於 hooks 安裝，重新啟動遊戲以重新觀察登入；不要單純把非零 pid 當成登入完成。

本地選擇保存於 account-<pid>.json。上傳只修改 save_decks 的 decksb／decksbs，將已啟用的皮膚／BGM／徽章欄位設為 0；原生解析完成且交易、帳號、隊伍匹配後，才回蓋本地選擇。徽章只啟用已載入、參數完整且相依鏈有效的 ID；不改參數表，無效或保留條目維持停用。回蓋按各 bank 內的隊伍索引重新編碼。

`0.2.1-gui` 面板另顯示本機保存、伺服器接受及最後回蓋結果，與「功能已啟用」分開。伺服器接受只計算已改寫的 save_decks、HTTP 200 且 JSON res=0；原生解析成功及同一交易仍是回蓋條件。新儲存取消舊回蓋，改回預設時可顯示「此請求無需預設化」。計數每次啟動歸零。下一步按 [儲存與重啟測試](docs/device-testing.md) 驗收。

**戰鬥換怪：** 開啟總開關及「戰鬥隊員替換」，再點「編輯六人隊伍」。每格有本體與武裝，預設本體滿級／普通覺醒全開／+891、武裝 Lv.99／滿覺醒／+297／SLv.1；實際上限按角色資料檢查。隊伍庫提供新增、另存、切換和刪除，最多保存 64 隊。手動「套用本格／整隊」每 frame 操作一名，失敗時停止其餘位置，已成功位置仍可還原。面板原有快速本體替換區已移除。

**入場自動套隊：** 在地下城外先選取並儲存隊伍，開啟「入場自動套用隊伍」。下次新入場會在開場動畫、敵人先制前一次套用選定隊伍，按新隊伍的普通／超／武裝覺醒、潛覺、徽章與關卡規則重算原生 SB。手動替換保留現有充能；中途開啟、續關及切樓層不會補發 SB。新設定 `battle.autoApplyTeam` 預設 false。

隊伍庫保存在 formations.json；舊 formation.json 自動轉入隊伍庫，並繼續作為目前選取隊伍的備份。單人參數租用會追蹤隊長交換與原生變身；多人不保留待套用或待還原的隊員。最多同時保留六名替換紀錄，還原保留目前技能充能並依角色上限裁切。戰鬥紀錄僅在目前程序／地下城有效；離開地下城、帳號改變或資料來源被接管後停止舊紀錄。新角色技能及效果需遊戲資源；單人自動 SB 已有使用者成功回報，其他技能效果及通關結算仍待真機驗證。

**計時控制：** 「背景計時修正」限一般單人、明確設有倒數上限的關卡。自動路徑只處理旗標，不跳轉 phase；若已出現計時退出彈窗，「手動關閉計時退出彈窗」才會可按。操作綁定目前地下城及該彈窗，3 秒逾時或狀態改變即取消。此按鈕會選擇返回操作階段的路徑；實際倒數、切樓層與結算仍待真機驗證。

## 診斷

面板的 **重新檢查** 可重試遊戲／hook 及儲存檢查，不會重複 hook 已成功的方法。**複製診斷** 包含版本、Bundle ID、校驗原因、hook／登入狀態、API host／path、設定路徑、當前隊員圖鑑 ID、技能充能及操作次數，不包含帳號 ID、query、憑證或入場 hash。

若仍沒有 PC 按鈕，先在遊戲 guest 容器查看 `Library/Application Support/PADHelper/status.json`。新版即使精確校驗失敗也會寫入狀態；`loaded` 和 `overlayVisible` 分別表示 runtime 已初始化及浮動視窗已顯示。若檔案也不存在，在 LiveContainer 日誌搜尋 `[PADCheaper] dylib 0.5.1-orbs constructor loaded` 及 `Loaded tweak`／`Failed to load tweaks`，確認實際載入的是新版、所選 Tweak Folder 和簽署狀態。

完成選擇／上傳測試前請先閱讀 [原型限制](docs/runtime.md)。關閉功能或移除 dylib 後，伺服器仍保留已上傳的預設值，本地選擇需依賴本地檔；此原型不宣稱所有網路 action 都已預設化。

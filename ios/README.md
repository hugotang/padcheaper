# 編譯及載入 PADCheaper

目標：jp.gungho.pad 23.6.0，CFBundleVersion 106878，arm64。其他版本會停止安裝 hooks。

`0.3.1-battle` 修正 `mode 1／source 0／multi 0` 的入口誤判，關卡分類 0／1 共用已核對的單人流程。此版提供單名戰鬥隊員替換／還原、有效隊伍徽章解鎖，以及計時旗標修正和手動撤窗。使用者已回報 `0.3.0-battle` 徽章解鎖通過；換怪、計時及通關結算仍需真機驗證。設計見 [換怪分析](docs/monster-replacement.md)、[徽章／計時實作](docs/badge-timer.md) 與 [整隊與滿狀態分析](docs/team-presets.md)。

## GitHub Actions

推送到 main／codex 分支會啟動 [.github/workflows/ios-dylib.yml](../.github/workflows/ios-dylib.yml)，也可在 Actions 選擇 **Build iOS LiveContainer dylib → Run workflow**。

完成後下載 `PADCheaper-iOS-arm64-<run number>` artifact，內容為：

- PADCheaper.dylib
- config.example.json
- SHA256SUMS、build-info.json
- 本文件、feasibility.md、runtime.md、device-testing.md、monster-replacement.md、badge-timer.md、team-presets.md、target.json、battle-target.json、LICENSE

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
5. 面板標頭會顯示 `0.3.1-battle`，載入、功能及儲存狀態每秒更新，開關自動儲存。更新保留原有 config.json／account 檔；舊設定中的新功能預設關閉。

浮動按鈕的含義：

| 狀態 | 含義 |
|---|---|
| 藍色「就緒」 | dylib／GUI 已載入，功能關閉或尚未選擇 |
| 黃色「載入」 | 初始化或等待登入、API、遊戲資源表等條件；點開查看原因 |
| 綠色「啟用」 | 至少一項功能通過啟用條件；面板會標明個別功能及部分啟用狀態 |
| 紅色「錯誤」 | dylib 已載入，但遊戲校驗、hook 或儲存失敗；仍可複製診斷 |
| 沒有 PC 按鈕 | 載入或視窗顯示未完成；不能只憑此判定是哪一步失敗 |

已支援 LiveContainer `doUseLCBundleId` 模式：從 `LCAppInfo.plist` 的 `LCOrignalBundleIdentifier` 識別原始 ID，版本／build／UUID／程式校驗仍需匹配。GUI 與設定檔會在識別出 PAD guest 後、精確校驗之前建立。

資料目錄是 **遊戲 guest 容器內**的 `Library/Application Support/PADHelper/`，不是 LiveContainer 本身的外層 HOME。

## 設定與功能

首次啟動會建立 config.json。預設值見 [config.example.json](config.example.json)，所有寫入關閉；可直接用面板設定。

1. 保持診斷模式，在遊戲內儲存一次隊伍，讓面板偵測 API。
2. 點 PC，按 **套用偵測到的 API**；只儲存 host／path，不記錄 sid、key 或完整 query。
3. 開啟 **功能總開關** 和要使用的 **Skin／BGM／隊伍徽章**。面板會顯示「已套用」、有效徽章數量或尚在等待的原因。
4. 要測試背景計時修正，可另外開啟該開關；它不依賴 Skin／BGM 的 API 設定。

也可手動編輯 config.json 的 `experimentalWrites`、`features`、`apiHosts`／`apiPaths`，每秒重新讀取。無效設定會停用修改並顯示錯誤，切換開關可重新儲存有效設定。

皮膚／BGM／徽章寫入同時要求 target 校驗、frame／network hook、已觀察的主執行緒 frame、成功登入解析及可用本地儲存。若登入早於 hooks 安裝，重新啟動遊戲以重新觀察登入；不要單純把非零 pid 當成登入完成。

本地選擇保存於 account-<pid>.json。上傳只修改 save_decks 的 decksb／decksbs，將已啟用的皮膚／BGM／徽章欄位設為 0；原生解析完成且交易、帳號、隊伍匹配後，才回蓋本地選擇。徽章只啟用已載入、參數完整且相依鏈有效的 ID；不改參數表，無效或保留條目維持停用。回蓋按各 bank 內的隊伍索引重新編碼。

`0.2.1-gui` 面板另顯示本機保存、伺服器接受及最後回蓋結果，與「功能已啟用」分開。伺服器接受只計算已改寫的 save_decks、HTTP 200 且 JSON res=0；原生解析成功及同一交易仍是回蓋條件。新儲存取消舊回蓋，改回預設時可顯示「此請求無需預設化」。計數每次啟動歸零。下一步按 [儲存與重啟測試](docs/device-testing.md) 驗收。

**戰鬥換怪：** 用原隊伍進入一般單人地下城，開啟總開關及「戰鬥隊員替換」，等待面板顯示可操作。選位置 1–6，點「輸入目標圖鑑 ID」並用面板數字鍵盤輸入，再按「套用替換」。按「還原／取消」可還原；關閉功能也會等待相同戰鬥的操作階段還原。一次只支援一個位置，會保留來源隊員的部分資料及目前技能充能，並按新形態上限限制充能。

角色原生變身、換隊員、離開地下城或帳號改變後，舊還原紀錄會失效。此原型不保存戰鬥快照至磁碟，無法跨程序恢復替換。新角色圖片、技能及效果須由遊戲現有資源提供；通關結算能否被接受尚未驗證。

**計時控制：** 「背景計時修正」限一般單人、明確設有倒數上限的關卡。自動路徑只處理旗標，不跳轉 phase；若已出現計時退出彈窗，「手動關閉計時退出彈窗」才會可按。操作綁定目前地下城及該彈窗，3 秒逾時或狀態改變即取消。此按鈕會選擇返回操作階段的路徑；實際倒數、切樓層與結算仍待真機驗證。

## 診斷

面板的 **重新檢查** 可重試遊戲／hook 及儲存檢查，不會重複 hook 已成功的方法。**複製診斷** 包含版本、Bundle ID、校驗原因、hook／登入狀態、API host／path、設定路徑、當前隊員圖鑑 ID、技能充能及操作次數，不包含帳號 ID、query、憑證或入場 hash。

若仍沒有 PC 按鈕，先在遊戲 guest 容器查看 `Library/Application Support/PADHelper/status.json`。新版即使精確校驗失敗也會寫入狀態；`loaded` 和 `overlayVisible` 分別表示 runtime 已初始化及浮動視窗已顯示。若檔案也不存在，在 LiveContainer 日誌搜尋 `[PADCheaper] dylib 0.3.1-battle constructor loaded` 及 `Loaded tweak`／`Failed to load tweaks`，確認實際載入的是新版、所選 Tweak Folder 和簽署狀態。

完成選擇／上傳測試前請先閱讀 [原型限制](docs/runtime.md)。關閉功能或移除 dylib 後，伺服器仍保留已上傳的預設值，本地選擇需依賴本地檔；此原型不宣稱所有網路 action 都已預設化。

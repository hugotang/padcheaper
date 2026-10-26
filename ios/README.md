# 編譯及載入 PADCheaper

目標：jp.gungho.pad 23.6.0，CFBundleVersion 106878，arm64。其他版本會停止安裝 hooks。

## GitHub Actions

推送到 main／codex 分支會啟動 [.github/workflows/ios-dylib.yml](../.github/workflows/ios-dylib.yml)，也可在 Actions 選擇 **Build iOS LiveContainer dylib → Run workflow**。

完成後下載 `PADCheaper-iOS-arm64-<run number>` artifact，內容為：

- PADCheaper.dylib
- config.example.json
- SHA256SUMS、build-info.json
- 本文件、feasibility.md、runtime.md、target.json、LICENSE

workflow 執行真正的 arm64 iPhoneOS 編譯，並檢查隊伍 JSON、LiveContainer 原始 Bundle ID、設定格式及 iOS 模擬器 GUI。模擬器檢查會載入相同 runtime，驗證校驗失敗時 GUI 仍顯示、設定開關儲存、API 套用、觸控穿透及橫向畫面。`PADCheaper-GUI-probe-<run number>` artifact 包含檢查報告和畫面；CI 綠燈不代表 iOS 27／你的自訂 LiveContainer 真機行為已驗證。

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
5. 面板標頭會顯示 `0.2.0-gui`，載入及功能狀態每秒更新，開關自動儲存。

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
3. 開啟 **功能總開關** 和 **Skin／BGM**。面板會顯示「已套用」或尚在等待的原因。
4. 要測試背景計時修正，可另外開啟該開關；它不依賴 Skin／BGM 的 API 設定。

也可手動編輯 config.json 的 `experimentalWrites`、`features`、`apiHosts`／`apiPaths`，每秒重新讀取。無效設定會停用修改並顯示錯誤，切換開關可重新儲存有效設定。

皮膚／BGM 寫入同時要求 target 校驗、frame／network hook、已觀察的主執行緒 frame、成功登入解析及可用本地儲存。若登入早於 hooks 安裝，重新啟動遊戲以重新觀察登入；不要單純把非零 pid 當成登入完成。

本地選擇保存於 account-<pid>.json。上傳只修改 save_decks 的 decksb／decksbs，將已啟用的皮膚／BGM 欄位設為 0；原生解析完成且交易、帳號、隊伍匹配後，才回蓋本地選擇。badge 選項未提供，原型不改徽章擁有或參數表。

timerFix 也是實驗選項，只在 GameMain 背景恢復路徑清除 0x20，保留棄權及其他旗標；完整計時情境未驗證。

## 診斷

面板的 **重新檢查** 可重試遊戲／hook 及儲存檢查，不會重複 hook 已成功的方法。**複製診斷** 包含版本、Bundle ID、校驗原因、hook／登入狀態、API host／path、設定路徑及操作次數，不包含帳號 ID、query 或隊伍資料。

若仍沒有 PC 按鈕，先在遊戲 guest 容器查看 `Library/Application Support/PADHelper/status.json`。新版即使精確校驗失敗也會寫入狀態；`loaded` 和 `overlayVisible` 分別表示 runtime 已初始化及浮動視窗已顯示。若檔案也不存在，在 LiveContainer 日誌搜尋 `[PADCheaper] dylib 0.2.0-gui constructor loaded` 及 `Loaded tweak`／`Failed to load tweaks`，確認實際載入的是新版、所選 Tweak Folder 和簽署狀態。

完成選擇／上傳測試前請先閱讀 [原型限制](docs/runtime.md)。關閉功能或移除 dylib 後，伺服器仍保留已上傳的預設值，本地選擇需依賴本地檔；此原型不宣稱所有網路 action 都已預設化。

# 編譯及載入 PADCheaper

目標：jp.gungho.pad 23.6.0，CFBundleVersion 106878，arm64。其他版本會停止安裝 hooks。

## GitHub Actions

推送到 main／codex 分支會啟動 [.github/workflows/ios-dylib.yml](../.github/workflows/ios-dylib.yml)，也可在 Actions 選擇 **Build iOS LiveContainer dylib → Run workflow**。

完成後下載 `PADCheaper-iOS-arm64-<run number>` artifact，內容為：

- PADCheaper.dylib
- config.example.json
- SHA256SUMS、build-info.json
- 本文件、feasibility.md、runtime.md、target.json、LICENSE

workflow 執行真正的 arm64 iPhoneOS 編譯，以及在 macOS 上運行獨立的隊伍 JSON 測試；CI 綠燈不代表 iPhone 行為已驗證。

## 本機建置

macOS 安裝完整 Xcode 並選取其 Command Line Tools 後，在 repo 根目錄執行：

```bash
bash ios/test.sh
bash ios/build.sh
```

輸出在 build/ios/。預設最低 iOS 15.0，只生成 arm64 slice；無需 Theos、Substrate、JIT、第三方 Hook framework 或 GitHub secrets。

## LiveContainer 安裝

1. 將 PADCheaper.dylib 放在 PAD app 所選的 LCTweakFolder 資料夾內，直接作為該資料夾的子檔案。
2. 確認未啟用 dontInjectTweakLoader，dylib／資料夾名稱沒有 .disabled。
3. 以 LiveContainer 設定的有效憑證重新簽署 tweaks，再啟動 PAD。CI 的 ad-hoc 簽章不是 iPhone 裝置簽章。
4. 在 LiveContainer／裝置日誌搜尋 `[PADCheaper]`，確認版本匹配、frame／network hook 安裝及唯讀條目檢查。
5. 初次保留診斷模式，查看 guest 容器的 `Library/Application Support/PADHelper/status.json`。

資料目錄是 **遊戲 guest 容器內**的 `Library/Application Support/PADHelper/`，不是 LiveContainer 本身的外層 HOME。

## 設定與功能

首次啟動會建立 config.json。預設值見 [config.example.json](config.example.json)，所有寫入關閉。設定每秒讀取一次。

診斷模式觀察到 save_decks 時會建立 endpoint.json，只記錄 host／path，不記錄 sid、key 或完整 query。要測試皮膚／BGM，先把已確認的 host（小寫）和 path 分別放入 config.json 的 apiHosts／apiPaths，再把 experimentalWrites 及想測試的 skin／bgm 開為 true。

皮膚／BGM 寫入同時要求 target 校驗、frame／network hook、已觀察的主執行緒 frame、成功登入解析及可用本地儲存。若登入早於 hooks 安裝，重新啟動遊戲以重新觀察登入；不要單純把非零 pid 當成登入完成。

本地選擇保存於 account-<pid>.json。上傳只修改 save_decks 的 decksb／decksbs，將已啟用的皮膚／BGM 欄位設為 0；原生解析完成且交易、帳號、隊伍匹配後，才回蓋本地選擇。badge 選項未提供，原型不改徽章擁有或參數表。

timerFix 也是實驗選項，只在 GameMain 背景恢復路徑清除 0x20，保留棄權及其他旗標；完整計時情境未驗證。

完成選擇／上傳測試前請先閱讀 [原型限制](docs/runtime.md)。關閉功能或移除 dylib 後，伺服器仍保留已上傳的預設值，本地選擇需依賴本地檔；此原型不宣稱所有網路 action 都已預設化。

# 診斷輸出與版本資料

2026-11-06；可維護性第三批，納入 [0.5.35](release-0.5.35.md)。編譯與驗證結果見 [開發紀錄](development.md)。

## 診斷分工

[`PHOrbDiagnosticPresentation.inc`](../src/PHOrbDiagnosticPresentation.inc) 集中盤面、隊長、技能、8 人隊伍、搜尋結果及讀取失敗的序列化與說明。它與既有 observation／failure presentation 一樣，只使用傳入的快照，不讀遊戲記憶體、不呼叫 engine，也不授權操作。

[`PHOrbSessionDiagnostics.inc`](../src/PHOrbSessionDiagnostics.inc) 管理 Session 的診斷歷史、搜尋／執行紀錄、8 人進度輸出及公開 snapshot。snapshot 仍依原規則保留最近 16 次不同多人就緒狀態；執行歷史上限仍為 3。這不是完全無副作用的 formatter，但副作用僅限原有的診斷紀錄，不啟動 Auto／技能／輸入。

`ReadIssueTrace` 與 `recordReadIssue` 統一三個讀取失敗／改盤等待的欄位組裝點。呼叫端仍提供各自原因與當時快照；Auto 分支保留原本沒有多人 detail 的格式，afterFrame 分支保留 multiplayer detail。phase、openingStep、capturedSearchGeneration 與布林欄位名稱不變，未改 schema 或把未知資料補成成功。

這些 `.inc` 仍屬同一 Session implementation；求路、原生幀調度、停止和放手維持既有呼叫順序。本批沒有另外複製一份 live state，也沒有讓 UI 描述成為操作判斷依據。

## 版本與交付資訊

[`ios/release.json`](../release.json) 是目前來源版本號、預設最低 iOS、平台、功能描述及 CI 摘要選項的共用來源。它描述源碼能力，**不代表已通過編譯、測試或實機驗證**。三批變更納入 0.5.35；每次建置與交付的驗證結果見 [開發紀錄](development.md)。

[`release_metadata.py`](../tools/release_metadata.py) 的用途如下：

| 入口 | 輸入／輸出 |
|---|---|
| `header` | 從 release.json 生成忽略追蹤的 `ios/generated/PHReleaseGenerated.h`，提供原有 `PH_RUNTIME_VERSION`。只生成原始碼，不編譯。 |
| `minimum-ios` | 讀取 release.json 的預設部署版本，供 build.sh 使用。 |
| `finalize` | 在已完成 dylib 編譯與簽署後核對版本標頭，寫入 build-info.json、release.json 副本與 SHA256SUMS；維持原本的交付文件連結整理。 |
| `summary` | 只讀取該次 artifact 內的 build-info.json 與 release.json，核對一致後產生 CI 摘要；不借用之後的工作目錄描述。 |

build.sh、test.sh、gui-test.sh 都在編譯前生成版本標頭；GUI probe 亦加入 generated include 路徑。最低 iOS 若以 `IPHONEOS_DEPLOYMENT_TARGET` 明確覆寫，build-info 會記錄傳入編譯器的值，不再固定寫 15.0。

build-info 保持 schema 1 及既有 82 個平面功能欄位，另加入本次三個可維護性描述與 `release_summary`。摘要按 feature key 選擇同一段描述，不另存另一份文字；版本沿革仍在 CHANGELOG／各版 release 文件。舊版圖形邊界、浮動按鈕尺寸、8 人倒數與遠端退出描述已依現有源碼修正。

`package_delivery.py` 對新格式核對 artifact 內的 release 副本與 build-info；有 `release_summary` 卻缺少 release.json 會拒絕。舊格式仍可包裝，但新舊格式都要求 dylib 的實際 SHA-256 同時符合 build-info 和 SHA256SUMS。既有 CI 成功、同 commit、四份 iPhone GUI 報告與不可覆寫交付目錄限制維持。`device_verified` 固定 false，不從文字描述推導測試通過。

## 待驗證

新增 Python 案例涵蓋重複／保留欄位、版本與摘要一致性、最低 iOS 覆寫、artifact 副本、版本標頭不一致、摘要不借用後續源碼及不宣稱測試成功；Session 案例涵蓋被動讀取失敗、Auto 失敗與改盤等待的 JSON 欄位、快照不增加原生讀取或操作。

初始靜態審查涵蓋 Python AST、JSON／YAML／shell 語法、include／結構及源碼搬移比對。0.5.35 本輪編譯測試再驗證新案例、完整 macOS 回歸、iPhoneOS 編譯、四輪 iPhone GUI、新建置入口、artifact 元資料與交付包裝，最終結果見開發紀錄。真機技能效果及多人同步仍須實機確認。

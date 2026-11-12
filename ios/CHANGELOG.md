# PADCheaper iOS 版本紀錄

每版的完整說明在 `docs/release-*.md`；建置、測試與交付證據（包括失敗的 CI）記錄在 [開發紀錄](docs/development.md)。交付檔位於 `E:/ipa/PADCheaper-<版本>` 與同名 ZIP，已交付的版本不覆寫。所有版本的 `device_verified` 均為 false，除非開發紀錄另有使用者真機回報。

## 最新交付

[0.5.46-orbs](docs/release-0.5.46.md) 已通過 [CI 160](https://github.com/hugotang/padcheaper/actions/runs/43170341092) 完整回歸、ASan/UBSan、arm64 建置／簽章、瀏覽器及四份 iPhone GUI，2026-11-12 交付新資料夾與 ZIP。修正武裝失效後 Auto 讀取誤停，包含恢復後接續案例；`device_verified=false`。

## 版本索引

| 版本 | 說明首次提交 | 內容 |
|---|---|---|
| [0.5.46-orbs](docs/release-0.5.46.md) | 2026-11-12 | 武裝失效後 Auto 讀取誤停修正；已驗證交付 |
| [0.5.45-orbs](docs/release-0.5.45.md) | 2026-11-11 | 九宮格補搜、較短路徑、連續取色快取；已驗證交付 |
| [0.5.44-orbs](docs/release-0.5.44.md) | 2026-11-08 | 單人連續取色逾時重試；已驗證交付 |
| [0.5.43-orbs](docs/release-0.5.43.md) | 2026-11-08 | Himmel 模板迦梨輪替改為非必要；已驗證交付 |
| [0.5.42-orbs](docs/release-0.5.42.md) | 2026-11-08 | 求路閃退修正、Skin／BGM 上傳保護、隊伍庫與設定保全、8 人倒數自動開始改為看倒數；已驗證交付 |
| [0.5.41-orbs](docs/release-0.5.41.md) | 2026-11-07 | 轉轉候選保留、荊棘補搜與單人續玩手動取色；已驗證交付，四轉轉五色仍未解 |
| [0.5.40-orbs](docs/release-0.5.40.md) | 2026-11-07 | 五色＋心／轉轉補搜、3 人手動取色與每隊徽章保存；已驗證交付 |
| [0.5.39-orbs](docs/release-0.5.39.md) | 2026-11-07 | 單人連續多色取珠、多轉轉逐次確認、額外取色改善位置、去循環與成功證據；已驗證交付 |
| [0.5.38-orbs](docs/release-0.5.38.md) | 2026-11-07 | 隊伍／策略配裝同步、失敗紀錄保留、六次回復撈珠離線模型；已驗證交付 |
| [0.5.37-orbs](docs/release-0.5.37.md) | 2026-11-06 | 單次撈珠選色／重試與診斷，雙次取色離線模型；原盤九 Combo 尚未解出 |
| [0.5.36-orbs](docs/release-0.5.36.md) | 2026-11-06 | PAD 23.6.1 位址遷移、舊資料相容、五色與形狀補搜及逐次統計 |
| [0.5.35-orbs](docs/release-0.5.35.md) | 2026-11-06 | Assist 無效關卡換隊、共用隊員狀態、Auto 生命週期及診斷／版本資料整理 |
| [0.5.34-orbs](docs/release-0.5.34.md) | 2026-11-05 | UI 分頁與規則摘要、角色／潛覺圖示、Auto 撈珠與 Combo 加算、部位技能修正 |
| [0.5.33-orbs](docs/release-0.5.33.md) | 2026-11-05 | 求路失敗原因與轉轉診斷修正、GUI 僅驗 iPhone |
| [0.5.32-orbs](docs/release-0.5.32.md) | 2026-11-05 | 單人轉轉撈珠 |
| [0.5.31-orbs](docs/release-0.5.31.md) | 2026-11-04 | 停用轉轉鎖定顏色、全盤鎖定時可穿過轉轉格 |
| [0.5.30-orbs](docs/release-0.5.30.md) | 2026-11-04 | 單人求路失敗延長重試、轉轉鎖定顏色備援（有問題，請改用 0.5.31） |
| [0.5.29-orbs](docs/release-0.5.29.md) | 2026-11-04 | 手動變身接續、8 人斷線不停、倒數自動開始、重開 app 可轉珠 |
| [0.5.28-orbs](docs/release-0.5.28.md) | 2026-11-03 | 精確形狀依實際消除群組、開場手動變身後接續 |
| [0.5.27-orbs](docs/release-0.5.27.md) | 2026-11-03 | 8 人依計分選路與 B6 得分等待回合 |
| [0.5.26-orbs](docs/release-0.5.26.md) | 2026-11-03 | 浮動轉珠列、PC 面板卡片與結構整理 |
| [0.5.25-orbs](docs/release-0.5.25.md) | 2026-11-03 | B6 準備失敗退回輸出、求路加速與轉珠頁改版 |
| [0.5.24-orbs](docs/release-0.5.24.md) | 2026-11-02 | B5 兩技、B6 條件式準備與改盤後救援 |
| [0.5.23-orbs](docs/release-0.5.23.md) | 2026-11-02 | 8 人尾層留珠與轉色輸出 |
| [0.5.22-orbs](docs/release-0.5.22.md) | 2026-11-02 | 按樓層開技與 B1 高 Combo |
| [0.5.21-orbs](docs/release-0.5.21.md) | 2026-11-02 | 8 人開技確認與同場接續 |
| [0.5.20-orbs](docs/release-0.5.20.md) | 2026-11-02 | 首次 Auto 就緒與通關停止 |
| [0.5.19-orbs](docs/release-0.5.19.md) | 2026-11-02 | 8 人本場 Auto |
| [0.5.18-orbs](docs/release-0.5.18.md) | 2026-11-01 | 隊伍模型、主動技能與關卡對比 |
| [0.5.17-orbs](docs/release-0.5.17.md) | 2026-11-01 | 全層關卡匯出與敵人 Combo 對策 |
| [0.5.16-orbs](docs/release-0.5.16.md) | 2026-11-01 | 補 L 規劃停止診斷 |
| [0.5.15-orbs](docs/release-0.5.15.md) | 2026-11-01 | 2026-11-01，CI 90 已通過完整自動回歸、iPhoneOS arm64 編譯及嚴格簽章、瀏覽器與四份 GUI；Windows… |
| [0.5.14-orbs](docs/release-0.5.14.md) | 2026-10-31 | 本版修正 4 人結束後進入 3 人時，上一場的多人模式記錄可能使「轉一次」一直停用；並合併固定隊長解析、手動目標入口及 4 人其他玩家退… |
| [0.5.13-orbs](docs/release-0.5.13.md) | 2026-10-31 | 本版修正 4 人「轉一次」後下一盤浮動按鈕誤停用，加入多人本機隊長／助人盤面解析，以及 CIEL × 尤皮莉絲單劍八獨立配置 |
| [0.5.12-orbs](docs/release-0.5.12.md) | 2026-10-31 | 本版收齊 0.5.11 之後的技能窗口重查、斜向轉珠、雨龍／CIEL 模板、3／4／8 人「轉一次」、浮動列收合與目前狀態提示 |
| [0.5.11-orbs](docs/release-0.5.11.md) | 2026-10-30 | 新增隊伍選項「心四連時允許消一般毒」，處理回復足夠的隊伍為避毒而減少 Combo 的問題 |
| [0.5.10-orbs](docs/release-0.5.10.md) | 2026-10-30 | 改善轉轉／刺珠盤在 800 ms 內找不到光 T＋心四連的求路問題 |
| [0.5.9-orbs](docs/release-0.5.9.md) | 2026-10-30 | 修正開場第 5 步的 7×6 擴盤等待與未通關隊伍 BGM 查找 |
| [0.5.8-orbs](docs/release-0.5.8.md) | 2026-10-30 | 修正徽章重啟保存、功能關閉後還原遺失與同進化鏈本體技能選擇；新增未通關隊伍 BGM、策略檔案分享／匯入及荊棘 Combo 偏好 |
| [0.5.7-orbs](docs/release-0.5.7.md) | 2026-10-30 | 改進形狀求路及敵方暫時變身的整包流程 |
| [0.5.6-orbs](docs/release-0.5.6.md) | 2026-10-29 | 新增可匯入／匯出的「隊伍＋開場流程＋技能循環＋盤面目標」config，附石田雨龍 × 白哉四連／心 L 兩種模板 |
| [0.5.5-orbs](docs/release-0.5.5.md) | 2026-10-29 | 適用日服 PAD iOS 23.6.0／106878，arm64 LiveContainer，僅單人 |
| [0.5.4-orbs](docs/release-0.5.4.md) | 2026-10-29 | 適用日服 PAD 23.6.0／106878、arm64、iOS 15 以上，限定單人 |
| [0.5.3-orbs](docs/release-0.5.3.md) | 2026-10-29 | 適用日服 PAD jp.gungho.pad 23.6.0／build 106878、arm64、最低 iOS 15 |
| [0.5.2-orbs](docs/release-0.5.2.md) | 2026-10-29 | 適用於日服 PAD jp.gungho.pad 23.6.0／build 106878，arm64，最低 iOS 15 |
| [0.5.1-orbs](docs/release-0.5.1.md) | 2026-10-29 | 適用於日服 PAD jp.gungho.pad 23.6.0／iOS build 106878，arm64、最低 iOS 15 |

## 原 README 版本摘要

以下段落原本位於 README 開頭，依原順序保留，內容未改動。

`0.5.25-orbs` 修正 [B6 優先準備失敗時直接停止](docs/release-0.5.25.md)：改為退回攻略輸出；求路在路線不變下加速，轉珠頁改為卡片版面並把狀態、預覽與 8 人進度放在最上方。已通過 [CI 118](https://github.com/hugotang/padcheaper/actions/runs/41495269554) 完整回歸、arm64 編譯與四份 GUI，交付 `E:/ipa/PADCheaper-0.5.25-orbs` 及同名 ZIP；真機穩定性待驗證。

`0.5.24-orbs` 加入 [B5 兩技、B6 條件式準備與改盤後救援](docs/release-0.5.24.md)：攻略先火奈→赤龍女，技能生效後重新考慮先前略過的轉色分支；已有解時，在原剩餘時間內尋找更有利的橫排。B6 在保留珠與 CD 條件有利時優先 1C 再雙轉色，已就緒則直接雙開；診斷增加最新卡住盤面。需重匯活動 JSON。已通過 [CI 116](https://github.com/hugotang/padcheaper/actions/runs/41471306455) 完整回歸、arm64 編譯與四份 GUI，交付 `E:/ipa/PADCheaper-0.5.24-orbs` 及同名 ZIP；真機穩定性待驗證。

`0.5.20-orbs` 修正 [首次 Auto 與通關停止](docs/release-0.5.20.md)：展開 PC 後自動讀取就緒狀態，8 人倒數完成可直接按 Auto；單人／8 人在 Dungeon Clear 或結算立即停止。[CI 108](https://github.com/hugotang/padcheaper/actions/runs/41395842797) 已通過完整回歸、arm64 編譯／簽章、瀏覽器及四份 GUI；426 組指紋符合原始 IPA，真機待驗收。

`0.5.19-orbs` 接入 [8 人本場 Auto](docs/release-0.5.19.md)：原生本機技能、首消目標、每盤手勢與換層，結算後停止。附上可獨立匯入的 [大感謝祭 2026 杯設定](templates/eight-player-thanks-2026/README.md)。[CI 107](https://github.com/hugotang/padcheaper/actions/runs/41390546703) 已通過完整回歸、arm64 編譯／簽章、瀏覽器及四份 GUI；419 組指紋符合原始 IPA，實機同步／計分尚待驗收。

`0.5.18-orbs` 新增 [單人離線隊伍試算](docs/team-model.md)、全部覺醒圖示、主動技能 CD／持續期／效果覆蓋，以及 [關卡對比與可更新攻略庫](docs/team-dungeon.md)。從轉珠面板點「隊伍對比」直接載入擷取紀錄；新關卡與攻略 JSON 無需重編 dylib。32 項隊伍及 13 項關卡模型、完整原生回歸、iPhoneOS 編譯／簽章、瀏覽器及四份 GUI 已由 [CI 98](https://github.com/hugotang/padcheaper/actions/runs/41321037478) 通過；417 組指紋符合原始 IPA，真機待驗收。見 [更新說明](docs/release-0.5.18.md)。Auto 存活放行仍未接通。

2026-11-01，`0.5.17-orbs` 已由 [CI 94](https://github.com/hugotang/padcheaper/actions/runs/41301161254) 通過完整模型／solver／原生／session／規劃／規則／設定／策略回歸、iPhoneOS arm64 編譯與嚴格簽章、編隊瀏覽器及 Scene／舊式視窗首次與重啟共四份 GUI，零失敗。本次全層 JSON 檔案／分享及既有介面截圖已檢視；417 組指紋與原始 IPA 一致。真機仍待正常使用時驗收。新增 [全層關卡 JSON 匯出](docs/dungeon-report.md) 及保守首消 Combo 對策。見 [更新說明](docs/release-0.5.17.md) 與 [開發紀錄](docs/development.md)。省 T 的光 L＋心四、三光三心耐久放行仍未接通。

0.5.12 收齊技能窗口重查、可選斜向轉珠、雨龍首盤保留白哉、CIEL／雙劍八起步時序、明確點擊的 3／4／8 人「轉一次」及預設收合的 PC 浮動入口。完整 [更新說明](docs/release-0.5.12.md)、[多人操作](docs/orb-multiplayer-once.md) 與 [實機測試要點](docs/device-testing.md) 列出行為；[CI 84](https://github.com/hugotang/padcheaper/actions/runs/41171594592) 已通過完整回歸、arm64 編譯、簽章、瀏覽器與四份 GUI，結果見 [開發紀錄](docs/development.md)，多人同步仍待真機驗證。新時序需重新匯入對應模板，替換 dylib 不會改寫已存策略。

0.5.11 增加預設關閉的「心四連時允許消一般毒」，與所選隊伍策略一起保存。全部轉轉相位有首消心四連時可消一般毒，優先目標與 Combo；猛毒仍避開，爆彈須首消全消。三份成功毒盤桌面重複對照均增加 1 Combo；[CI 79](https://github.com/hugotang/padcheaper/actions/runs/41046955701) 已通過完整回歸、arm64 編譯、簽章、瀏覽器及四份 GUI 報告，真機回血與首消仍待驗收。完整 [更新說明](docs/release-0.5.11.md) 列出條件與驗證範圍。

0.5.10 改善轉轉／刺珠盤的多形狀求路：允許中途繼續補心四連，最終仍須通過完整目標與存活限制。四份停止盤中，第二盤已在桌面三次找到光 T＋心四連；其他盤受低血可達範圍、光心配置或缺心限制，詳見 [分析](docs/orb-spinner-thorn-search.md) 與 [更新說明](docs/release-0.5.10.md)。[CI 77](https://github.com/hugotang/padcheaper/actions/runs/41027272995) 已通過完整回歸、arm64 編譯、簽章、瀏覽器及四份 GUI 報告；iPhone 求路及首消／HP 仍待驗收，完整記錄見 [開發紀錄](docs/development.md)。

使用者已確認 0.5.9 開場 6／6 及 BGM 實際正常播放，這次仍保留兩項修正。[CI 76](https://github.com/hugotang/padcheaper/actions/runs/41017566656) 為 0.5.9 的驗證，不代替 0.5.10。問題回報来自 **Dopamine rootless 越獄 iPhone 直接注入**，同一份 dylib 保留目前已可用的 LiveContainer 與 Dopamine rootless 載入方式。

0.5.8 修正徽章重啟選擇及失敗還原待辦，增加未通關隊伍 BGM、策略 JSON 分享／檔案匯入、同進化鏈武裝滿充能時原生本體選擇、荊棘 Combo 偏好與零接觸補搜。[CI 74](https://github.com/hugotang/padcheaper/actions/runs/41011326922) 已通過原生／session、編譯、簽章、瀏覽器及四份模擬器報告，真機驗收待回報；[更新說明](docs/release-0.5.8.md)、[保存與分享](docs/persistence-music-sharing.md)、[AutoCombo 參考](docs/autocombo-review.md) 及 [通關回傳](docs/clear-dungeon-audit.md) 分別列出行為與證據限制。

0.5.7 改進轉轉形狀引導，新增共用原期限的缺項補搜及深入檔有限回溯；完整開場後可保留敵方暫時變身的流程進度，暫停原形態技能並繼續核對必要自救。Windows 回歸及 456 次舊新版對照通過；[CI 72](https://github.com/hugotang/padcheaper/actions/runs/40925229351) 已通過原生／session、編譯、簽章、瀏覽器及四份模擬器報告，真機驗收待回報。詳見 [0.5.7 更新](docs/release-0.5.7.md)。

0.5.6 新增「隊伍＋開場流程＋技能循環＋盤面目標」config 匯入／匯出、本體／武裝及指定技能限制、至少珠數條件、放手前重新驗證與最近三次實際轉珠紀錄。詳見 [0.5.6 更新](docs/release-0.5.6.md) 和 [整包 config](docs/team-plan-config.md)；[CI 70](https://github.com/hugotang/padcheaper/actions/runs/40910345825) 已通過原生／session、編譯、簽章、瀏覽器及四份模擬器報告，真機結果待回報。

0.5.5 新增還原失敗重試與再次套隊修正、雨竜 9 光珠條件診斷、指定不可消珠色、荊棘傷害預算及改尺寸轉轉裁切。詳見 [0.5.5 更新](docs/release-0.5.5.md)；[CI 67](https://github.com/hugotang/padcheaper/actions/runs/40885179006) 已通過原生／session、編譯、簽章、瀏覽器及四份模擬器報告，真機結果待回報。

0.5.4 修正 30 fps 渲染時間造成長路徑中斷、待命隊員殘留技能選擇，以及新路徑混入舊進度。技能拒絕保留當次實際技能／CD／階段，面板顯示規則與位置；不把停止後的充能當成施放判斷當時狀態。詳見 [更新與安裝](docs/release-0.5.4.md) 及 [問題依據](docs/orb-skill-selection.md)。原生拒絕的完整真機原因仍待新版診斷核對。

先前 0.5.4 的 [CI 63](https://github.com/hugotang/padcheaper/actions/runs/40867358095) 已通過原生／session 回歸、arm64 編譯、簽章、瀏覽器與四份模擬器報告。更新只替換 dylib 並重新簽署，保留原有設定及隊伍庫；原關卡與技能結算仍待真機核對。

0.5.3 加入 [轉轉、雲、封條、暗闇及特殊珠適配](docs/orb-effects.md)。轉轉需全部共用相位都達標，不抓指定顏色；路線避開封條及刺珠。既有 0.5.2 遇到轉轉仍停止，夜一規則的舊版限制見 [雨竜／白哉設定](docs/uryu-byakuya-0.5.2.md)。

另加入 [光 T／心四連的判定與搜尋修正](docs/orb-shape-search.md)：四連允許未消同色鄰珠，加入複合布局與多樣候選，失敗訊息列出缺項及求路耗時。IMG_0407 在 Windows／macOS 三檔均達標；[CI 59](https://github.com/hugotang/padcheaper/actions/runs/40843708271) 的原生、arm64 編譯、簽章、瀏覽器及四份模擬器／重啟報告全部通過。下載雜湊及安裝方法見 [0.5.3 更新說明](docs/release-0.5.3.md)，新增效果與真機耗時仍待裝置驗收。

使用者已於 2026-10-28 回報 `0.5.0-orbs` 測試通過，記錄為單人 6×5 基準版本的真機通過回報。

`0.5.2-orbs` 修正部分 iPhone 在起點等待期間重送按下、無法選珠的問題，並加入「技能就緒就放」設定及白哉輪替範例。[CI 56](https://github.com/hugotang/padcheaper/actions/runs/40816242119) 的原生、arm64 編譯、簽章、瀏覽器與四份模擬器／重啟報告均通過，二進位來源 `2c3362d7a928d64e83837dc0f92d9505d0da8a38`。更新與設定保留方式見 [0.5.2 安裝說明](docs/release-0.5.2.md)；真機 iPhone 修復及技能效果仍待回報，順序見 [device-testing.md](docs/device-testing.md)。

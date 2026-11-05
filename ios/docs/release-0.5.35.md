# 0.5.35-orbs：Assist 無效換隊與可維護性整理

- 修正單人 Assist 無效關卡套用隊伍後被誤判失敗而還原：保留設定中的武裝來源，依已確認的關卡限制核對原生空武裝描述。入場 SB 只計有效來源；部分失敗仍整批還原，普通關卡不放寬核對。
- 共用隊員來源、當前／持續／暫時形態及有效武裝的唯讀解碼，讓换隊、技能、覺醒、Combo 與武裝回復使用一致規則。未知或不完整讀取仍停止，8 人特殊角色 ID 保留完整數值。見 [隊員狀態](member-state.md)。
- Auto 的執行模式、流程狀態與啟停集中管理；8 人倒數自動開始的場次紀錄獨立保留。停止或原生開始被拒絕後，設定切換不會使同場自動重試，持珠中的停止仍完成一次放手。見 [Auto 生命週期](auto-session-state.md)。
- 診斷輸出分檔，保留原有 JSON 欄位；版本標頭、build-info 與 CI 摘要改由同一份 release.json 生成，交付時核對版本副本、來源與 dylib 雜湊。見 [診斷與版本資料](diagnostics-release.md)。

## 驗證與使用

本地求路器／8 人及 C 模型、版本資料、隊長資料與 JavaScript 編隊／隊伍／關卡回歸通過。[CI 42063773816／141](https://github.com/hugotang/padcheaper/actions/runs/42063773816) 一次全部通過：完整 macOS 原生／Session 回歸、iPhoneOS arm64 編譯簽章、瀏覽器與四輪 iPhone GUI。來源 `bb7062b70d4c9d7762a0702d17afc41f73545de7`，用時 25 分 52 秒。原版 PAD 的 439 組 Battle／Orb 指紋吻合；最終證據見 [開發紀錄](development.md) 及交付包的 verification.json。

交付 `E:/ipa/PADCheaper-0.5.35-orbs` 與同名 ZIP。dylib SHA-256 `eb19f8c8bff91d5a4bef599678c42b19b1d704c2e1789c6e4501bfb9a757617c`；ZIP SHA-256 `ccc3505c3c5f0eb5fe475421bedf801489f21417e894d583be6200b84c3b6613`。交付內容雜湊及 ZIP 逐檔核對全部通過。

適用日服 PAD 23.6.0／106878，arm64、iOS 15 以上。換隊仍限單人，3／4 人仍只支援既有手動轉一次，8 人仍限目前一場，沒有入場、配對或再戰。

`device_verified=false`；Assist 無效實際入場、變身／技能、停止後放手與多人同步仍須真機確認。完整關閉 PAD 後替換 dylib，保留原有設定與隊伍庫；LiveContainer 沿用原有重新簽署流程。

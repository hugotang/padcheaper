# 0.5.44-orbs：單人連續取色逾時重試

日服 PAD 23.6.1／107553、arm64，最低 iOS 15。[CI 158](https://github.com/hugotang/padcheaper/actions/runs/42490096263) 已完成完整回歸、iPhoneOS 建置／簽章及模擬器 GUI，已交付本版，編譯來源固定為 `6cafc28b58814b0e7f2f2a6262d00721664a4205`；`device_verified=false`。

- 單人盤的連續取色在共用時間內沒有解出，且有方案因時間不足而中斷時，會用既有的單人重試預算 2000 ms 重新嘗試全部轉轉入口一次；重試只搜尋基本取色方案，不再保留時間給第一輪已試過的位置改善方案。0.5.43 真機 log 的雙轉轉盤（水／暗／心）每入口只分到 400 ms 而失敗；離線重播時，每入口 1000 ms 內都能以一次取心達成五色＋心。
- 多人（含 3 人手動取色）維持共用期限，不重試。失敗訊息改為計算不重複的入口數，並註明已延長重試。最壞情況下出手前多等約 2 秒。
- Himmel 模板維持保留規則 4（辛美爾就緒即用），吸收／無效對策空窗仍會停止，詳見[開發紀錄](development.md)。

## 驗證與交付

[CI 157](https://github.com/hugotang/padcheaper/actions/runs/42487331729) 曾在新加的錄製盤回歸失敗：每入口 1000 ms 且含位置改善方案時，macOS runner 有一個入口未解出，未產生 dylib。改為重試只搜基本方案，回歸改為斷言可解、不斷言與機器速度相關的門檻後，由 [CI 158](https://github.com/hugotang/padcheaper/actions/runs/42490096263) 通過，失敗 log 已納入交付。

已交付 `E:/ipa/PADCheaper-0.5.44-orbs` 及同名 ZIP。381 份校驗清單、ZIP CRC／382 項通過。dylib 11,957,440 bytes，SHA-256 `d260cf1d7d110366a4cc64403b77274b2e4b11791d6da6b14cd4e796d4b1571a`；ZIP SHA-256 `ca8b30f18f93eb8e46719a56d4c5d3ee3002cf6f4e8506620e433e5ff1e25c40`。Windows 本機核心回歸在同一來源通過。真機上的重試等待時間與成功率仍待確認。

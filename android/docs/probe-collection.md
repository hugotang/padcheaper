# 首次裝機的接入紀錄核對

`0.1.0-probe` 已完成 APK 建置、工具測試及 `emulator-5568` 裝機擷取。首次擷取、返回前景與完整重開的三份紀錄均通過來源／APK／程序／payload 身分及影格核對；最終結果見 [版本驗證紀錄](release-0.1.0-probe.md)。後續建置仍按 [Android 建置步驟](../README.md) 執行。

## 如何使用

啟用模組並重開遊戲後，點遊戲內「PC 診斷」→「擷取接入狀態」。工具不會代替這個動作，也不會啟動／停止遊戲、修改作用域、寫入裝置檔案、改 root 設定或操作戰鬥。

在 repo 根目錄使用 Python 3.10 以上，指定剛剛安裝的確切模組 APK：

```powershell
python android/tools/collect_probe.py --adb C:/Users/tangk/AppData/Local/Android/Sdk/platform-tools/adb.exe --serial emulator-5568 --apk build/android/app/outputs/apk/debug/app-debug.apk --out build/android/device/entry-01.json
```

每次使用新的輸出名稱；既有檔案不覆寫。預設只接受五分鐘內的擷取，可用 `--max-age-seconds` 設定 1–3600 秒。比較的是裝置 elapsed time，不依賴 Windows／Android 時鐘一致；影格狀態反映按下擷取時的觀察，並非之後持續監控。

工具檢查本機 APK 中的模組入口、作用域、API 102 設定、兩種 ABI 的 ELF header，以及建置身分檔。再讀取目前 PAD 主程序、安裝模組的 SHA-256、該程序的診斷 JSON，最後重新核對程序與已安裝 APK，排除擷取途中重開／更新。KernelSU 提供 read-only `su` 讀取；子命令每個最多 15 秒，proc／JSON／logcat 輸出皆有上限。模組 APK 只支援目前單一 universal APK 的交付方式，split 模組明確拒絕。

MuMu／遊戲可能產生與主程序同名的子程序。工具核對相同 UID、父子關係及 zygote 父程序，只接受唯一主程序及其後代；程序樹不明時拒絕，不以 PID 清單順序或舊紀錄選擇主程序。

遊戲資料路徑由目前程序 UID 對應的 Android user 計算，並不是固定 user 0。只讀本工具的診斷檔與 `PADCheaper` tag／目前 PID 的最多 100 筆 log；不讀遊戲存檔、帳號資料或記憶體內容。原始輸出留在忽略的 build 目錄。

## 結果含義

| 結果 | 含義 |
|---|---|
| `entry-evidence-collected`／exit 0 | 此次紀錄符合 APK／程序身分、時間、原生架構及影格證據要求；仍不是完整裝置驗收或可轉珠證明 |
| `needs-attention`／exit 1 | 取得紀錄，但来源、程序、時間、frame 或原生映像條件不符，原因在 issues |
| `incomplete`／exit 1 | 主程序不存在、權限／檔案不可讀，或擷取途中程序／模組已變動 |

常見原因包括 `module-source-mismatch`、`installed-module-apk-mismatch`、`payload-hash-mismatch`、`process-start-mismatch`、`report-expired-or-invalid-time`、`frames-not-current` 及 `library-image-unverified`。失敗仍保存已取得的資訊與入口 log。若完整 hook 安裝失敗而沒有按鈕，可從入口 log 追查 APK／簽名／deoptimize 失敗；此時缺少 probe 檔屬於預期的不完整證據。

## 身分與 schema 3

Gradle 在建置時，以 repo 相對路徑排序並統一 CRLF→LF，對 Android 產品來源、target JSON、共用 C／C++ 來源及建置設定計算 SHA-256。相同來源的 Windows／Linux 換行不影響識別；文件、log、測試、machine-specific local.properties 不參與。為保守涵蓋 header 相依，目前納入 ios/src 下全部 C／C++／header／inc，因此其中未被核心使用的來源變更也可能更新識別。

同一來源指紋會放入 BuildConfig 和 APK 的 `assets/padcheaper-build.json`；這不是簽章或完整建置可重現性的宣稱。電腦端另以**整個安裝 APK 雜湊**及**實際載入 payload 雜湊**核對二進位。

schema 3 在 schema 2 上新增來源指紋、程序啟動 tick、boot ID、擷取 elapsed time，以及實際載入 payload SHA-256，避免相同版號或 PID 重用造成舊結果被接受。讀取 boot ID／proc stat 受限制時，模組仍保存其他诊斷，但記錄 `processIdentityAvailable=false`；工具不把身分不明的紀錄算作成功。

`deviceVerified`、`runtimeLayoutVerified`、`boardReadEnabled`、`actionsEnabled` 一律保持 false。返回前景及完整程序重開已有成功擷取證據；OpenGL／Vulkan 分別實測、程序內 Activity 重建、arm64 真機、載入失敗介面與多人程序隔離仍待驗證。本工具不授權遊戲操作。

## 離線案例

Java `ProcessIdentityTest` 已隨 12 個 Java 案例通過；Python `test_probe_contract.py` 的 7 個案例亦已通過。集中測試時，除既有 Gradle／CMake 套件外執行：

```powershell
python -m unittest discover -s android/tests -p "test_*.py"
```

案例使用合成資料，涵蓋同版號新來源、PID 重用、開機變更、過期／未來時間、缺失／錯型別欄位、CPU 目錄放錯、重複 JSON 鍵、同名子程序，以及擷取途中程序重開。通過這些案例不代表已驗證所有裝置或遊戲操作。

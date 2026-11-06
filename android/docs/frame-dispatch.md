# Java 逐幀接入：PAD 23.6.0／230600

2026-11-06，針對本機取得的四份 DEX 做靜態反組譯。目標檔、DEX 雜湊與 code item 指令雜湊見 [機器可讀證據](../research/frame-dispatch-20261106.json)。以下類名只適用於已核對的 APK。

## 兩條繪製路徑

| 入口 | 靜態呼叫路徑 |
|---|---|
| OpenGL | `GLSurfaceView.Renderer` → `h12.onDrawFrame(GL10)` → `kk0.OnDrawFrame()` → `AppDelegate.OnDrawFrame()` → native `onDrawFrame()` |
| Vulkan | `m22.setVulkanRenderer(k22)` 建立 `l22` → `l22.run()` → `l22.a()` → 相同 `h12.onDrawFrame(GL10)` → 相同 wrapper／JNI |

`h12` 同時實作 `GLSurfaceView.Renderer` 與 `k22`。其 `onDrawFrame(GL10)` 有 240 個 DEX code units，在 `0xb4` 呼叫 `kk0.OnDrawFrame()` 一次；前面有啟動、安裝與暫停分支，並非每次 renderer callback 都會進入遊戲 native frame。`AppDelegate` 是 `kk0` 的實作，其 wrapper 只有四個 code units：呼叫 native 方法一次，再返回。

Vulkan 的 `m22.setVulkanRenderer` 保存 renderer 並建立 `l22` 執行緒；`l22.a()` 在 Surface 有效時，於 `0x148` 呼叫 `h12.onDrawFrame(GL10)`。這是靜態 Vulkan 覆蓋證據，尚未在指定模擬器切換兩種繪製方式驗證。

## 接入處理

仍只 hook AppDelegate 的 Java wrapper，不額外 hook renderer 來重複計數。先驗證 renderer／Vulkan loop 的類別及方法簽名，安裝五個 observers 後，對 `h12.onDrawFrame(GL10)` 與 `l22.a()` 呼叫框架 `deoptimize`，避免小 wrapper 已內聯在呼叫者內而繞過 hook。原生 JNI／Houdini 分派不變，沒有直接跳入 ARM 位址。

任何簽名或 deoptimize 失敗會記錄 `entry installation failed` 並解除本次 hook handles；已成功 deoptimize 的 Java 方法沒有重新最佳化 API，不宣稱能立即還原其效能狀態，完整重開程序後重試。未做全程序或 Android framework 反最佳化。若實測仍收不到影格，須繼續核對 ART 與繪製路徑，不能將靜態呼叫圖當成執行期保證。

影格計數只在目前前景 Activity 的同一世代接受；記錄累計／本次前景數量、執行緒及時間。診斷可區分背景、沒有影格、超過兩秒未更新、執行緒改變、計時不連續與近期有影格。平均 fps 是同一次前景內的觀察間隔，並非遊戲效能 benchmark；執行緒改變不再回報有效平均值。

## 擷取紀錄

`ProbeCapture` 處理有限的檔案核對、原生载入與保存；`ProbeController` 處理按鈕／彈窗／Activity。成功與失敗都更新私有目錄的 `probe-<pid>.json`，包含時間、失敗階段、native payload 結果及當次影格狀態。若保存本身失敗，畫面及 log 明確提示舊檔可能保留，不能讀到舊成功檔就判定本次成功。

第二批引入 schema 2；第三批已升為 schema 3，加入來源指紋與程序／boot／原生檔案身分，詳見 [紀錄擷取](probe-collection.md)。

- `captureStatus=complete` 只表示擷取流程完成；`library.status` 仍可能是 `not-loaded` 或 `header-read-failed`。
- `frameDispatchPrepared=true` 表示這批已完成 hook／指定呼叫者處理；`frameStatus=observed` 只表示近期收到 callback，兩者不驗證遊戲操作窗口。
- `runtimeLayoutVerified`、`boardReadEnabled`、`actionsEnabled`、`deviceVerified` 固定 false。

第二批來源研究時沒有取得 native 執行期程式段或定位盤面 layout；當時只讀查詢時 PAD 主程序不在，沒有啟動或操作遊戲。其後的 `0.1.0-probe` 集中編譯測試已在指定模擬器取得約 30 fps 的逐幀觀察，以及首次擷取、返回前景、完整程序重開的成功紀錄，詳見 [版本驗證紀錄](release-0.1.0-probe.md)。

本次沒有分別切換 OpenGL／Vulkan，也沒有驗證程序內 Activity 重建或 arm64 真機。有限 ELF header 讀取並不是 native 程式段解碼證據；Android 盤面欄位、保護資料、單人模式與技能／輸入需下一階段另行核對。

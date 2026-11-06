# 形狀補搜改用不同引導：本機修正與驗證

2026-11-06，使用者繼續要求本機優化，授權編譯與模擬，不啟動 CI、不打包。本批在 `1e9cd82` 的共用求路器上修改形狀補搜，接續 [前次形狀模擬](solver-shapes-2026-11-06.md) 已確認的七個窄 beam 漏解。以下是 Windows 候選來源的本機結果；沒有新 dylib／APK，也不代表 macOS Session、Android ABI 或手機已驗證。

## 結果

相同 48 步／beam 128／120 ms／四向設定，原 828 盤由 821 盤達標變為 828 盤達標，七個既有漏解全部解出。另一批先生成、未參考 solver 成敗的 360 盤，窄 beam 由 358 盤達標變為 359 盤達標；仍有一盤未解，並出現兩筆 deadline。Thorough 設定下兩批共 1,188／1,188 盤達標，沒有 deadline。

| 資料與設定 | 修改前 | 修改後 | 修改後 deadline | 修改後單盤最大耗時 |
|---|---:|---:|---:|---:|
| 原 828 盤，48／128／120，四向 | 821／828 | 828／828 | 0 | 55 ms |
| 新 360 盤，48／128／120，四向 | 358／360 | 359／360 | 2：一盤達標、一盤未達標 | 120 ms |
| 原 828 盤，Thorough 48／512／800，四向 | 前次已驗證 828／828 | 828／828 | 0 | 212 ms |
| 新 360 盤，Thorough 48／512／800，四向 | 本批未測此設定 | 360／360 | 0 | 259 ms |
| 原資料中混合／雙形狀 648 盤，固定起點、48／512／800，四向 | 前次已驗證 648／648 | 648／648 | 0 | 121 ms |
| 原資料中 witness ≤24 步子集，Fast 24／128／120、零 backtrack | 前次已驗證 45／45 | 45／45 | 0 | 6 ms |
| 原資料中 witness ≤32 步子集，Balanced 32／256／350、零 backtrack | 前次已驗證 306／306 | 306／306 | 0 | 37 ms |

表內設定依序為步數／beam／毫秒。48／128／120 是保留 48 步、隔離候選寬度的壓力設定，**不是產品 Fast**。本批修改前比較使用 `1e9cd82`，以更新後的 probe 重建及執行；該提交的產品求路器與前次報告 `084f2c7` 相同。候選批次來源標籤為 `worktree-shape-recovery`，它是執行者提供的標籤，不自行證明二進位來源。

[來源與結果摘要](../research/orb-shape-recovery-results-20261106.json) 保存已測來源及各批 probe／資料雜湊；本批 `PHOrbSolver.cpp` 統一換行後 SHA-256 為 `e5869dfb1d5b1ab8d57bda5e07a28de200d456a392cd6de1713188193985327d`。此雜湊識別本次已測內容，後續修改不得沿用本批結果作為新來源的驗證。

所有上述批次的獨立路徑重播、目標命中、Combo／消除數核對均通過。逐一按 ID 對照窄 beam 結果，原 828 盤新增七盤達標、沒有原先達標變成未達標；新 360 盤新增一盤達標、亦沒有此類變化。這只描述這兩批相同盤面的達標狀態，不能擴張成所有模式、路徑品質與耗時都沒有倒退。

環境為 Windows 11、AMD Ryzen 9 9900X（12 核／24 執行緒）、MSYS2 GCC 15.2.0，C++17／`-O2 -Wall -Wextra -Werror`，未關閉測試斷言。不同批次可能受到主機排程影響；單次毫秒差異不作速度提升證據，也不能換算成 iPhone 耗時。原 828 盤含旋轉與換色的相關變體；盤數只是構造資料的覆蓋結果，不是實戰成功率。

## 修改內容與範圍

前次普通形狀的 recovery 可能與主搜尋保持相同排序，將未達標路徑原樣重跑。本批只在 `shapeGoals > 0`、`!board.spinnerMask`、`fishCell < 0`、非活動目標時啟用不同引導，讓既有補搜改變布局偏好：

| 搜尋階段 | 缺格權重 | 供珠距離權重 | 用途 |
|---|---:|---:|---|
| 主搜尋 | 2 | 1 | 保留原排序 |
| 只有一條形狀條件的 recovery | 2 | 2 | 加重合法供珠距離，避免一直追逐近似碎片 |
| 多條形狀條件的 recovery | 4 | 1 | 加重缺格成本，偏向較完整布局 |
| 已保留 frontier 的回訪 | 4 | 1 | 從不同起始候選嘗試較完整布局 |

「一條條件」按目標條目計算；例如同色 T 的 `count=2` 仍是一條，多形狀 L＋十字才是兩條。最多兩份 saved frontier 使用既有數量上限；未增加 pass、beam、步數或總時限，形狀主搜尋原本的 2／3 時限分配保留。轉轉、撈珠與活動專用搜尋不啟用這個新策略。

引導分數只控制探索順序。跨 pass 保留的最佳候選仍按主搜尋尺度比較，必要時重新評分，避免較大的提示分數使未達標候選蓋過原候選。實際形狀、首消、消除數、安全條件與合法移動判定不放寬。

重新評分有成本；`evaluationCalls` 因此可能大於 `evaluated`。前者是評估呼叫數，後者是搜尋候選數，不能再假設兩者必然一對一。這批修改沒有宣稱所有盤面的耗時或搜尋量都不增加。

## 七個既有漏解

以下是相同窄 beam 設定的候選輸出；七盤皆在第二個 pass 達標，零 backtrack、沒有 deadline。它們先前增加時間至 2000 ms 仍失敗，本批在原 120 ms 上限內改用不同探索排序取得解，支持修正原路補搜的方向；仍不能由這七盤推論每一項提示公式的獨立因果。

| 案例 | 實際命中 | 步數 | 耗時 |
|---|---|---:|---:|
| `Square-03-v0` | 正方形 1／1 | 48 | 19 ms |
| `Square-03-v1` | 正方形 1／1 | 42 | 18 ms |
| `Square-03-v2` | 正方形 1／1 | 48 | 19 ms |
| `DoubleT-09-v1` | T 2／2 | 21 | 40 ms |
| `LCross-10-v0` | L 1／1、十字 1／1 | 48 | 44 ms |
| `LCross-10-v1` | L 1／1、十字 1／1 | 46 | 47 ms |
| `LCross-10-v2` | L 1／1、十字 1／1 | 48 | 43 ms |

這七盤另加入完整 solver 回歸，使用 beam 128、1000 ms 以減少主機忙碌對功能驗收的影響。回歸逐一檢查形狀、獨立 Combo／消除數、輸入不變、步數／pass／backtrack 界限，另含短 deadline 與取消案例。此回歸不是 120 ms 性能驗收；上表才是 120 ms 批次結果。

另以原 48／128／120 設定重複七盤三輪，共 21／21 達標、零 deadline，三輪各自最大耗時為 47／58／53 ms。這提供本機重跑穩定性的證據，不保證固定延遲，也不能換算成手機性能。

## 新資料揭露的限制

[360 盤新資料](../research/orb-shape-holdout-20261106.json) 在執行 solver 前生成並保存，生成器沒有查詢求路器，也沒有按成功、初始達標或難度篩選。它涵蓋 5×4 的七種形狀、6×5 橫／直排，以及 7×6 同色 L＋T／L＋十字／T＋十字、三個四連／橫排／直排。共 15 情境，每種六色各四個獨立 seed；每盤使用獨立 seed 生成背景、布局及合法打亂路徑，與原 828 盤分離。

每盤保存 32／40／48 步的存在解；反向重播可還原達標盤面，但 witness 不交給 solver。生成器獨立核對後，另用公開 Python oracle 交叉檢查首消、Combo 與消除數。新資料仍是普通無轉轉、無特殊障礙或活動的構造盤，其中七盤起始已達標，不代表隨機實戰分布。

| 窄 beam 案例 | 修改前 | 修改後 | 解讀 |
|---|---|---|---|
| `Holdout7x6TripleRow-c4-r3` | 橫排 2／3，32 ms、未逾時 | 橫排 3／3，20 ms、未逾時 | 新策略補出第三排 |
| `Holdout7x6SameTCross-c5-r2` | 心 T 0／1、心十字 1／1，69 ms、未逾時 | 相同命中，120 ms、deadline | 窄 beam 仍未找到合法同色 T＋十字解；新策略本次用完額度 |
| `Holdout7x6SameLT-c4-r0` | 暗 L＋T 達標，56 ms、未逾時 | 仍達標，120 ms、deadline | 達標輸出保留，但本次搜尋耗時增加 |

上述兩筆 deadline 必須與成功數一起保留；不能把 359／360 寫成全部未逾時，或由總數增加推論所有指標沒有倒退。Thorough 的新 360 盤全部達標且未逾時，但同時增加 beam 與時間，不能據此把窄 beam 失敗只歸因於某一因素。尚未證明任意同色混合形狀都能在窄 beam 解出。

## 已驗證與待驗證

- 最終 Windows 腳本入口完成 8P model、共用 C／C++ solver 整套回歸（含新增七例）及內部引導分數測試，全部 exit 0；solver 紀錄在 `build/shape-recovery/final-windows/solver.log`。初次與新增七例後紀錄分別保存在 `build/shape-recovery/regression/solver.log`、`build/shape-recovery/new-regression.log`。本機 suite 中五色原盤、換色與鏡像仍找到 5／5，既有轉轉／荊棘、取消、deadline 等案例亦按其原斷言通過。
- core CMake 的 Windows GCC 15.2.0／Release 建置及 CTest 2／2 通過，合計 4.30 秒，紀錄在 `build/shape-recovery/core/Testing/Temporary/LastTest.log`。這是共用核心的 Windows 入口驗證，不是 Android ABI 或 macOS 建置。
- 原 828 盤及新 360 盤的 Thorough、窄 beam，原資料的 648 盤固定起點，以及可容納 witness 的 Fast／Balanced 子集已執行。固定起點與 Fast／Balanced 子集均與原盤重疊，不能另加到 1,188 當作新增獨立資料。
- 內部引導分數專用測試已在 Windows 通過，涵蓋達標分數一致、未達標只改引導而保留實際消除結果、跨 pass 快取、主尺度候選與引導 frontier 分離，以及安全與純顏色目標。此測試直接使用內部 evaluator，屬分數契約核對，不是獨立形狀 oracle。Windows／macOS 測試入口及 core CMake 已掛入該測試；目前只有 Windows 執行結果，不宣稱 macOS 或其他平台已執行。
- 新資料未涵蓋自訂消除門檻、首消限定直排側枝、轉轉、撈珠、活動或裝置時窗。既有 suite 的通過不等同這些條件已做同等規模壓力測試。
- 本次沒有編譯 iPhoneOS dylib、Objective-C++ Session、Android APK／ABI 核心，也沒有裝置／多人同步驗證。iOS CI 保持手動，沒有派發 CI、封裝或修改 `E:/ipa`。

## 重跑與證據

- [本批摘要、逐盤對照與來源雜湊](../research/orb-shape-recovery-results-20261106.json)
- [原 828 盤與已知解](../research/orb-shape-simulation-20261106.json)、[新 360 盤與已知解](../research/orb-shape-holdout-20261106.json)
- [公開 API probe](../research/orb_shape_simulation.cpp)、[獨立 oracle 與批次工具](../research/orb_shape_simulation.py)
- [七盤固定回歸](../tests/orb_alternate_shape_solver_tests.inc)、[內部引導分數契約測試](../tests/orb_shape_guidance_tests.cpp)
- 本機原始結果：`build/shape-recovery/baseline-828-narrow.json`、`candidate-828-narrow.json`、`candidate-828-thorough.json`、`holdout-baseline.json`、`holdout-candidate.json`、`holdout-candidate-thorough.json`、`candidate-fixed648.json`、`candidate-fast.json`、`candidate-balanced.json`、`seven-repeat1.json`、`seven-repeat2.json`、`seven-repeat3.json`。

完整 Windows 回歸使用 `ios/tools/windows-tests.sh`。probe 以相同 C11／C++17、O2、warnings-as-errors 配置重建，連結當次 `PHOrbSolver.cpp` 與三份 C model object。批次工具保存資料／probe hash，並檢查執行前後 hash 一致；來源標籤仍需與實際建置來源另外核對，不沿用來源已變的舊 object。

```text
python ios/research/orb_shape_simulation.py --probe <本次probe.exe> --dataset ios/research/orb-shape-holdout-20261106.json --source-commit <實際來源標籤> --output <新結果.json> --beam 128 --budget 120 --steps 48
```

省略 `--dataset` 使用原 828 盤；Thorough 改為 `--beam 512 --budget 800`。Fast／Balanced 必須先選取 witness 在相應步數內的資料，不能把超出步數的存在解用來宣稱該預算應有解。工具拒絕覆寫已有結果。

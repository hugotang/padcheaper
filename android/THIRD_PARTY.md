# 第三方元件

## Gradle wrapper 8.13

- `gradlew`、`gradlew.bat`、`gradle/wrapper/gradle-wrapper.jar` 來自 [Gradle v8.13.0](https://github.com/gradle/gradle/tree/v8.13.0)，未修改。
- 授權為 Apache License 2.0，全文見 [LICENSE](gradle/wrapper/LICENSE)。原始檔內的著作權聲明及 jar 中 META-INF/LICENSE 保留。
- Wrapper jar SHA-256：`81a82aaea5abcc8ff68b3dfcb58b3c3c429378efd98e7433460610fecd7ae45f`。
- Distribution SHA-256 已固定在 gradle-wrapper.properties。官方此 tag 的根目錄未附 NOTICE。

## libxposed API 102

以 Maven `io.github.libxposed:api:102.0.0` 作 compileOnly 引用；執行期由裝置框架提供，本 repo 與 APK 不另附 API 實作。來源及授權見 [libxposed/api](https://github.com/libxposed/api/tree/79b75b49255a257d38f67bce2e928649dc2fc6c9)。裝置 framework／遊戲 APK／DEX／原生庫僅供本機研究，未加入來源。

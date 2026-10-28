# iOS development workflow

- The user requested batching features on 2026-10-28. Implement and review source changes first. Compile, run the test suites, dispatch GitHub Actions, and package a new dylib only when the user explicitly requests “編譯測試” or otherwise explicitly asks to build/test. Source review, syntax/JSON checks, and writing test cases are allowed before that request.
- Keep the iOS workflow manually dispatched. Commits and pushes save work; they must not automatically start a build. Do not restore automatic push/PR triggers unless the user asks.
- Record unbuilt changes and pending verification in `ios/docs/development.md`. Do not present a written test case or an earlier version's passing CI as validation of new source changes.
- Retain the single-player restriction on battle mutations and automatic input. The user reported peer desynchronization and disconnects in multiplayer; configuration must not override this restriction.
- Delivered version folders and archives under `E:\ipa` are immutable. Create a new version when an authorized build is ready to deliver.

# Native PC main menu

The PC build uses the original main-menu button components. The former Leaderboards slot becomes Options; the former bottom Options slot becomes Quit. Existing fonts, selected/unselected colors and navigation are retained. The main menu milo asset's two authored text tokens are changed to `OPTIONS` and `pause_quit`. The menu script no longer reapplies labels at entry or during polling. Earlier entry-only labeling reverted on focus; polling caused visible flicker and is superseded by the asset patch. Other screens' labels are unaffected.

`scripts/prepare_pc_menu.py` validates the supported original header, menu script and milo hashes, round-trips the DTB, and redirects `ui/gen/main.dtb` and `ui/gen/main.milo_xbox` into a supplemental `private/gen/main_1.ark`. The milo is decompressed, precisely edits two length-prefixed label fields, and is recompressed with raw deflate. It preserves `main_0.ark` and backs up the original header as `main.hdr.before-pc-menu`. Repeated application is idempotent, both earlier menu patches can be upgraded, and unrelated header changes are rejected. Full source builds apply this step automatically. Generated archives and backups remain private and are excluded from source packages.

The native script `exit` handler at `0x8232B7B0` is adapted in the generated SDK-context copy. It returns an integer-zero DataNode and schedules `Window::RequestClose` on the UI thread, using the existing window close/termination path. This also adapts other calls to that script command; ordinary panel exit messages are separate dispatch paths.

Validation: three tests cover menu transformation, preservation of other actions, unsupported input rejection, supplemental archive creation, original archive preservation, repeat application and unrelated-change rejection. The windowed executable builds successfully. The user confirmed the native labels and Options/Quit behavior. No automatic game session is started. A guarded Unlock All entry script was subsequently added without changing those label assets.

Format references: [GameArchives ARK reader](https://github.com/maxton/GameArchives/blob/master/Library/Ark/ArkPackage.cs) and [dtab node format](https://github.com/mtolly/dtab/blob/master/src/Data/DTA/Base.hs).


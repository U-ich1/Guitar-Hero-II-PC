# GH2PC

## Build from your own dump / install source updates

The current native Windows build is playable: the user has completed songs and verified career saves, controller switching/remapping, graphics persistence and the launcher. The research notes below include earlier development checkpoints.

Install Git for Windows and Python 3.11+ on Windows x64, extract the source release into a folder, then run:

```powershell
.\Build.ps1 -Dump "D:\Games\Guitar Hero II.iso"
```

An extracted dump folder can replace the ISO path. Setup downloads checksum-verified tools, fetches pinned dependencies, validates the exact Xbox 360 executable, generates native translations and builds the game and launcher. Open **Guitar Hero 2 Launcher.exe** after completion. No game is started automatically.

For a source ZIP update, close this project's game and launcher normally, then run:

```powershell
.\Update.ps1 -SourceArchive "D:\Downloads\gh2-recomp-source-NEW.zip"
```

The existing dump is reused; source files are backed up, and Documents saves/settings are not replaced. `Build.ps1 -Check` performs preflight and `-Offline` reuses installed caches. Source releases exclude the dump, assets, generated game code, binaries, extracted icon, downloaded tools and personal state.

See **[building and updates](docs/building_and_updates.md)** for prerequisites, supported dump hash, Python alternatives, update behavior, build receipts and source-only packaging. Source and numbered releases are available at [U-ich1/Guitar-Hero-II-PC](https://github.com/U-ich1/Guitar-Hero-II-PC). Download a release source ZIP or pull the repository and rerun `Build.ps1` to update; there is no automatic update service.

## Existing project notes

Experimental native Windows recompilation of the Xbox 360 version of Guitar Hero II.

## Current result — 2026-10-10

The new windowed native build renders GH2's original intro, menus and the “Surrender” note highway at 1280x720 through the Xenos/D3D12 backend. Captures were visually inspected; the user also reached gameplay. The first guitar test exposed a controller identity bug: SDL reported the XInput guitar as a standard gamepad. The updated build uses native XInput and preserves a physical guitar's subtype when keyboard input is active. The connected guitar passed the runtime capability check as subtype 6. Correct fret response in gameplay, calibration and timing still require validation.

Double-click **Guitar Hero 2 Launcher.exe** for the white home screen, then select **Settings** or **Play**. Settings contains Graphics with hover explanations, Controls with XInput/DirectInput selection and click-to-map bindings, Calibration, retail Cheats with five Hyperspeed levels, and Saves with import/export/deletion and backups. It shares the existing Documents settings, launches the game directly, then closes itself. The older `Start-GH2.cmd` remains available. Enter maps to Start, Space to A/confirm, Backspace to B/back, and arrows to the D-pad. Neither launcher has an automatic game-shutdown timer. See `docs/launcher.md`.

Saves now live under **Documents\GH2PC\Saves**, and PC settings under **Documents\GH2PC\gh2.toml**. Existing project saves/settings were copied without changing the originals. Windows' actual Documents location is used, including redirected folders. See `docs/saving.md` for verification and the package layout.

**F6 opens controller mappings:** independent gamepad/guitar profiles, custom fret/strum/Star Power/Pause bindings, whammy-axis routing, raw input monitoring, and a player-one XInput device selector. Native mode preserves working controls. Profiles save in Documents. See `docs/controls.md` for the difference between GH2's auto-strumming gamepad layout and the optional strummed virtual guitar layout.

The F6 device selector now shows Windows controller names. Native DirectInput support runs alongside XInput, with raw button/axis/trigger setup and persistent device selection. XInput duplicates are filtered. DirectInput conversion and controller-name enumeration passed tests; gameplay on a physical DirectInput pad remains to be checked.

**F8 opens PC audio calibration:** saved 0–250 ms added delay, 5 ms adjustments, lower buffering on restart, and a flash/click reference. Zero preserves current timing. GH2's existing video calibration stays in game. See `docs/audio_calibration.md` for usage and verification limits. The user has confirmed that the corrected XInput guitar controls work in gameplay.

**F9 opens cheats:** retail cheats and Hyperspeed levels 1–5, applied when the next song loads or on returning to the main menu. Save remembers preferences in Documents. Unlock All follows the original game's restriction on career and saving. F7 opens achievements. See `docs/cheats.md` for usage and verification limits.

**F5 opens graphics settings:** 1×–4× internal rendering scale, borderless/windowed mode, window size, monitor selection, host-display VSync, FXAA, anisotropic filtering, aspect ratio and presentation FPS. Internal scaling and selected rendering/window settings require a normal restart. Native/2×/3× D3D12 context initialization and saved-settings reload passed checks. See `docs/graphics_settings.md` for limits and validation.

The guitar path passes native XInput button/axis bits through unchanged. The default SDL path must not be used for this guitar: it hardcodes the standard-gamepad subtype. Hardware read checks may need an unsandboxed process; the sandbox probe could not access XInput devices.

GH2-specific compatibility: this disc build checks **subtype 7 only** in its controller poll at `0x8227B3F0`. Native XInput guitar subtypes 6 and 11 are normalized to 7 at the game's capability-import boundary, preserving all buttons/axes. Without this adjustment, native subtype 6 still selects gamepad controls (A/orange, LB/red, and idle LT/green), matching the user's highway report. `tests/check_gh2_guitar_scheme.py` executed the actual translated polling function: baseline subtype 6 reproduced the fallback; 27 fixed guitar polls selected the guitar scheme; ordinary gamepad behavior stayed unchanged. Hands-on highway response remains to be checked after this fix.

The earlier diagnostic executable maps 47,414 native functions and enters GH2's original startup at `0x823B5010`, then stops at the explicit GPU-engine checkpoint. It remains available for runtime regression comparison.

Native startup now initializes loader imports, TLS, virtual heaps, critical sections, read-only disc access, events, semaphores, clocks, and a real archive worker. The worker has its own stack/PCR/TLS and advances archive reads beyond 1.8 MB. GH2 then queries a configured 1280x720/60 Hz video mode allocates physical graphics memory, and stops at `VdInitializeEngines` (caller `0x823D5FD8`). The GPU engine backend is next; no frame or audio has been produced. Physical memory uses shared Windows section views for the A/C/E aliases, including the E-range 4 KB offset.

The initial pass had 5,754 unsupported-instruction occurrences and 882 switch-boundary errors. The patched pass reports zero unsupported instructions, zero switch-boundary errors, zero unresolved direct-call placeholders, and no remaining decoder/condition-register/missed-switch diagnostics. This establishes a cleaner translation and a working native build, not full-game correctness.

Validation completed:

- All 186 emitted game translation units passed compiler syntax checks.
- The entire translation compiled and linked into `build/gh2/gh2_bootstrap.exe`.
- 8,700 executions of actual emitted instruction functions passed an independent Python guest-byte oracle across 87 fixtures, including destination aliasing, packed-vector order, signed arithmetic, saturation, and condition flags.
- 11 function-analysis regressions passed, including bounded reads, conditional returns, loops, tail branches, and recovered switch cases.
- Runtime regressions pass for loader state, TLS, heaps, file reads/EOF and path isolation, events/semaphores, dates and clocks, plus two-worker TLS isolation, suspended start/resume, critical-section contention, join and handle close. Physical-memory tests cover bidirectional alias writes, all three page sizes, bounds, release protection and reuse.
- All eight register helper bodies were checked instruction by instruction against the decoded memory image.

The ISO was left unchanged. Game files are under `private/`, including `default.xex`, archives, and videos. `private/default.exe` is the decoded **console memory image**, not the native Windows application. XEX memory-image section bytes are at their RVAs; PE raw-file offsets must not be used to find helper routines. The first helper scan used the wrong offsets; the corrected scan and configuration remove that mistake.

Disc identification: title `415607E7`, media `69550273`, XEX SHA-256 `4f91842399e5e69085c295bc736a82abf3e2235c8f2143c347d9043cd633e596`.

Tool base: XenonRecomp `ddd128bcca99fe8bfbb99bea583c972351fa6ace`. Project-owned extensions live in `patches/`, `runtime/gh2_ppc_helpers.h`, and `scripts/apply_tool_patches.py`; they are reapplied to the ignored tool checkout. LLVM-MinGW 20261006 (Clang 23.1.3) and CMake 4.4.4 are portable downloads under `tools/`, with no system installation.

Reports: `docs/recomp_diagnostics.json`, `docs/instruction_validation.json`, `docs/compile_validation.json`, `docs/import_inventory.json`, and `docs/startup_validation.json`. The import inventory contains 179 functions and 10 variables that need runtime support. Detailed logs are under `build/`.

## Repeat the pipeline

Use Python with `cryptography` for the executable-image inspection. The bundled Codex Python runtime used for this run already has it.

```powershell
python scripts/inspect_disc.py 'Guitar Hero II (USA, Europe) (En,Fr,De,Es,It).iso' --all
python scripts/inspect_xex.py
python scripts/analyze_image.py
./scripts/build_tools.ps1
./scripts/recompile.ps1
python scripts/summarize_recomp.py
./scripts/build_bootstrap.ps1
./scripts/validate_all.ps1
```

Use `-Python <interpreter-path>` on the PowerShell scripts if Python is not on your path. Instruction-fixture assembly also uses the existing `C:/devkitPro/devkitPPC/bin/powerpc-eabi-as.exe`. `config/gh2.toml` keeps optimizations conservative. `prepare_config.py` validates complete helper bodies and adds the one GH2 switch missed by the upstream scanner, yielding 171 recovered switches.

Run the startup diagnostic:

```powershell
./build/gh2/gh2_bootstrap.exe --trace-entry private/default.exe
```

Exit 2 is the current expected stop at an unsupported runtime service. The startup regression checks that the archive worker executes and the video-mode query and physical allocation precede the GPU-engine blocker. Worker failures propagate to the main diagnostic; shutdown wakes suspended workers, cancels kernel waits, and joins workers before releasing guest memory. Cancellation is cooperative at function entries and imports, so a guest loop with neither still needs additional handling.

## Next milestones

1. Integrate the graphics command processor and shader backend at `VdInitializeEngines`, then implement interrupts, ring-buffer submission and presentation.
2. Continue runtime coverage and integrate audio/input; trace startup to the title screen.
3. Load one song with synchronized audio and video, measure timing, and verify gameplay against the original.
4. Add separate audio/video calibration and remappable guitar, gamepad, and keyboard input.

ReXGlue's pinned runtime is now integrated in a separate SDK-context build. It owns memory, kernel, Xenos rendering, SDL audio and input. The translations use the SDK's actual context type and account for its Windows 4 KB physical-page host offset in both scalar and vector memory accesses. Real alias tests passed across 4 KB, 64 KB and 16 MB pages. Its documented Windows build uses Visual Studio C++/clang-cl; the project supplies guarded LLVM-MinGW compatibility patches. See `docs/graphics_next.md` for build details.

Remaining correctness work includes `setjmp`/`longjmp`, exception handling, indirect targets that arise only at runtime, inherited instruction implementations beyond the tested extension set, and complete vector-status behavior. None of the checks establish song playback or accurate gameplay timing yet.

The ISO, extracted files, decoded image, downloaded tools, and generated game code are excluded from source control and source releases. The earlier diagnostic bootstrap remains available for debugging; its startup-only limitations describe an earlier checkpoint, before the current playable runtime and launcher.

## Release downloads and project information

Releases include a source ZIP, an optional standalone launcher, and SHA256 checksums. GitHub names the launcher download `Guitar.Hero.2.Launcher.exe`; put it in the root of an existing compatible built project. It contains no game and is also produced when building the source.

The source release includes this project's MIT license and original launcher artwork. Original project code uses that license; downloaded dependencies retain their own licenses. Game assets, generated game translations and game binaries are excluded. This is an unofficial community project, not affiliated with or endorsed by the game's rights holders.

The project owner has completed songs and verified career save/reload, controller switching/reconnection, Discord presence and the audience toggle. Performance varies by hardware and scene. Gameplay simulation remains at the original 60 Hz; higher simulation rates and a launcher video-calibration tool remain future work. Video calibration is available in the game's menu. Experimental upscaling options have been removed.

Allow approximately 10 GB free for a build with the ISO stored elsewhere, or 20 GB with the ISO in the project folder. Existing Documents saves and settings are retained by source updates. See [building and updates](docs/building_and_updates.md) for setup and the supported dump hash.

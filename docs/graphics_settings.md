# PC graphics settings

Press **F5** in the updated windowed build. Settings save to `Documents\GH2PC\gh2.toml`.

Internal resolution controls the Xenos renderer's draw/resolve scaling:

| Preset | Approximate resolution from GH2's native 720p output |
|---|---|
| 1× | 1280×720 |
| 2× | 2560×1440 |
| 3× | 3840×2160 (4K) |
| 4× | 5120×2880 |

These are integer renderer scales, not a change to the guest video-mode clock. Original textures and videos do not gain new source detail. Higher settings increase GPU memory and rendering cost. The hardware/backend can clamp unsupported scales; the actual value is logged as `GH2 internal render scale initialized: XxY`.

The panel also offers borderless fullscreen/windowed mode, startup window-size presets, named monitor selection, actual display VSync, FXAA/strong FXAA, anisotropic filtering up to 16×, aspect-ratio preservation and a presentation-FPS overlay. FidelityFX was disabled in this build, so unavailable FSR/DLSS options are not offered. No gameplay-speed or unlocked-FPS switch is included.

**Internal resolution, FXAA, window size and monitor changes require Save and a normal restart.** Fullscreen, texture filtering, aspect ratio, display VSync and the FPS overlay can change during the session. No button automatically closes or restarts the game.

Display VSync is wired to D3D12 swap-chain presentation with sync interval 1 and no tearing flag. Off preserves the prior low-latency interval-0 behavior and supported tearing/VRR flags. It does not modify the SDK's unrelated guest GPU wait flag. The flag combinations follow [Microsoft's Present API](https://learn.microsoft.com/en-us/windows/win32/api/dxgi/nf-dxgi-idxgiswapchain-present) and [tearing requirements](https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/dxgi-present). VSync may increase input latency and does not replace audio/video calibration.

Window size is the host window's logical size; borderless fullscreen uses the desktop display mode. It is independent of internal render resolution. Aspect preservation controls letterboxing versus stretching. The FPS counter estimates presentation/overlay cadence rather than note-judgment tick rate.

The F5 panel now shows startup resolution and anti-aliasing separately from requested settings, and confirms when saved changes need a restart. The overlay says `Display updates` to avoid confusing repeated presentation of an image with new game frames. Monitor names are queried when opening the panel, rather than on every redraw.

**Display FPS limit** accepts a user-entered nonnegative 32-bit integer in the launcher and F5 graphics menu. `0` disables this limit. Save applies it live in F5; preferences persist under `gh2_display_fps_limit`. VSync and monitor refresh may impose a lower rate. This caps host presentation, not the guest vblank/game clock, and does not unlock additional unique game frames or lower the cost of rendering each guest scene.

The D3D12 presenter checks a monotonic deadline before recording/submitting presentation work. Frames presented too early are skipped, and the UI requests its next regular paint tick. The limiter does not sleep on a guest or UI rendering callback. Missed slots are discarded rather than caught up in bursts, and changing the cap or selecting unlimited resets its schedule. Arbitrary caps may have uneven spacing when they are not divisors of the display refresh rate; very low caps also reduce in-game overlay redraw frequency.

`tests/frame_limiter.cpp` checks custom values, ten-second simulated rate accuracy, unlimited, live changes, stall recovery and large positive integers. The graphics persistence test includes a custom 73 FPS cap and rejects negative, fractional and overflowing input.

Controller discovery now runs in background workers for DirectInput and XInput. Gameplay enumeration reads published device lists; it no longer scans Windows HID devices or queries all four XInput slots synchronously on each input request. Device changes remain discoverable within roughly one scan interval (one second plus Windows discovery time). This removes a potential source of periodic stalls; a full-song test is still needed to establish whether it resolves the reported VSync stutter.

## Verification

`tests/check_graphics_settings.py --gpu` loads the actual GPU plugin, validates scale presets and per-axis overrides, AA/filtering registration, DXGI flag combinations, restart metadata and a fresh-process save/reload. It then initializes the full D3D12 render context at 1×, 2× and 3× on the RTX 5070, waiting for GPU-thread initialization via the blocking shader-storage API. The renderer log confirms the actual scale for each run. No GH2 code or game window is launched; Documents settings/saves are untouched.

See `docs/graphics_settings_validation.json` and `build/graphics_runtime_test.log`. Full-song visual quality and performance at the higher presets still need a hands-on check. 4× is exposed but was not included in the hardware initialization sweep.

Additional checks load config before the renderer plugin, matching application startup, and confirm the initialized D3D12 scale is 2×. A connected-guitar discovery probe exercises cached enumeration across background refresh cycles; see `docs/controller_devices_validation.json` for measured polling duration. These checks do not launch GH2 or touch Documents preferences.

## Settings persistence fix

The original SDK serializer placed strings directly inside double quotes without TOML escaping. The launcher's Windows `log_file` path therefore produced invalid TOML (`C:\Users...`), and startup rejected the whole file before logging was initialized. This explains the saved resolution/AA/filtering reverting despite the file containing their chosen values. The earlier graphics reload test omitted Windows paths and did not cover this failure.

Both full-config and category serializers now use toml++ string formatting. Regression fixtures include a Windows path, quotes, backslashes, control characters and UTF-8; the unpatched runtime reproduced the failure and the fixed runtime passed fresh-process reload and actual 2× renderer startup.

Before loading existing preferences, GH2 repairs the recognizable old raw Windows `log_file` line, preserves all other file contents, and retains the original as `gh2.toml.before-path-fix`. Already escaped/new-format paths are left alone. The repair is idempotent and uses a temporary file plus Windows replacement. `tests/config_path_repair.cpp` exercises repair/backup/repeated launch behavior, and independent TOML parsing verifies the recovered values. The normal launcher applies staged executable and runtime-library updates only when GH2 is closed.
# Current update

Upscaling controls and DLSS development diagnostics are removed. Presentation
uses bilinear filtering. **No audience (hide crowd models)** is a live F5 option
and a persistent launcher Graphics option, disabled by default. It skips crowd
rendering while retaining sounds and gameplay. Earlier FSR sections below are
historical. See `performance.md` for validation and measurement limitations.


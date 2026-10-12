# Performance build

## Current performance controls

FSR/CAS upscaling and the F5 DLSS development controls have been retired at the
user's request. The game/runtime/renderer are rebuilt together with the spatial
backend disabled. The display uses ordinary bilinear filtering. Old FSR/CAS
selections are rejected and fall back to bilinear; saving from the launcher
also resets the legacy option and removes the old sharpening parameters.
The historical upscaling sections below describe earlier builds.

**No audience (hide crowd models)** is available in launcher Graphics and F5.
The default is off; F5 changes it immediately and saving retains it. The native
WorldCrowd drawable table points to `sub_822471D0`; the toggle exits that routine
before its render workload. This changes geometry rendering, not crowd audio,
rock meter, score, judgment timing or saved career state. Crowd objects remain
loaded and their animation updates remain active. Removing model allocation or
animation work is a separate change. Crowd shadows and all venue variants still
need in-game observation. Logs report `crowd_draws_skipped` to verify the gate
was exercised, not as a count of individual removed spectators.

The latest inspected session used 2560x1440 internal rendering and 1920x1080
bilinear output. Presentation GPU cost averaged 0.015ms, while renderer
submissions averaged 3.438ms and 4,157 submissions exceeded 16.7ms. Submissions
are not whole game frames; menus/loading and GPU preemption are included. These
numbers justify inspecting scene rendering but do not establish a single cause
for every drop or predict how much disabling the crowd will help.

Draw CPU diagnostics now sample one in 64 calls (`renderer_draw_sampled_cpu`)
instead of performing clock reads and FP environment saves for all ~56 million
draw calls in that session. GPU submission timing and submission/fence/Present
metrics remain complete. Sampled totals and spike counts must not be compared
with the earlier all-draw totals or extrapolated as whole-frame cost.

Discord's observer caches immutable native Symbol identities in a bounded
per-thread table, avoiding repeated VirtualQuery calls for unrelated script
variable lookups. Known presence symbols still refresh their node addresses.
The UI log includes current mode/song/venue/status and crowd state, allowing the
analysis script to associate output samples with window-end scene state.
Windows spanning transitions may contain both scenes/settings.

Validation: isolated execution of the actual translated crowd gate, original
disabled body comparison, settings fresh-process reload, launcher migration/UI,
D3D12 bilinear initialization at 1x/2x/3x, timestamp-ring and sampled-FP checks,
Discord mock protocol and cheat regressions. No game was launched for testing.
Visible crowd removal and a measured gameplay improvement remain unconfirmed.

The earlier windowed build was labelled Release but appended `-O0` after global `-O3`, disabling compiler optimization for translated game functions and the game application. It also recorded function-entry diagnostics on every translated call. These were conservative bring-up settings, not a finished performance configuration.

Release now builds these targets with `-O2`, `-fwrapv`, strict floating-point behavior and disabled strict aliasing. Debug retains `-O0`. No fast-math, guest-clock or note-judgment-speed changes are enabled. The shutdown/termination safe point remains at function entries; detailed tracing is skipped in the windowed build unless `GH2_TRACE_RECOMP` is set. The diagnostic bootstrap retains its original tracing behavior.

The FPS overlay separates **Game output** (guest VdSwap submissions) from **Display updates** (overlay presentation redraw cadence) and reports the worst game-submission interval in its sampling window. These are submission measurements, not guarantees of physical monitor delivery or unique animation frames. Sampling preserves the host floating-point environment.

The game log now includes `GH2 performance:` entries every five seconds and when the display FPS cap changes, even with the overlay hidden. Entries include output rate, display redraw rate, maximum submission interval since the preceding entry, cap, display VSync and configured scale. The configured scale can differ from the running scale after changing a restart-only option. Logging runs on the UI thread; output sampling stays on the guest import path. These measurements do not yet distinguish GPU execution time from CPU stalls. Earlier sessions without these entries cannot be reconstructed from the log.

Optimized fixtures passed 87 instruction variants / 8,700 independent-oracle executions, including destination aliasing, integer edge cases, condition registers and saturation. The actual translated guitar classifier also passed its 27 normalized-guitar cases. These establish focused semantic checks, not full-game equivalence or an FPS improvement. Full songs, save/reload, timing and crowd-heavy scenes still need a hands-on check of the optimized build.

For performance comparison, wait for compilation to finish, keep the same song/settings between runs, and inspect Game output plus worst interval. A steady 60 submissions/second corresponds to roughly 16.7 ms between submissions; occasional longer intervals matter even when average FPS looks acceptable. Start with the display limit disabled to separate its behavior from CPU/GPU throughput, then compare the same scene with a 60 limit.

FSR 1 spatial upscaling and CAS sharpening are now exposed in the launcher and F5 panel through the existing presenter shaders. Temporal FSR and DLSS remain unverified. Upscaling does not remove CPU work in guest game code; generated intermediate frames are not a substitute for reliable real-frame delivery in this rhythm game.

Hidden GH2 panels and an empty achievement toast queue no longer request continuous ImGui repaints. The FPS overlay updates on guest-output paints, while open settings panels, SDK overlays and active achievement notifications retain continuous UI updates. Toggling a GH2 panel explicitly requests a repaint so it can open even when guest output is idle. This removes redundant repaint requests; it does not change guest clocks or prove that all presentation stalls are resolved. The shared SDK UI interface and its consumers must be rebuilt together.


## Spatial upscaling and stall diagnostics

Choose **Graphics > Upscaling > FSR 1** in the launcher (or F5), save, and restart normally. For a lower GPU rendering workload, pair **1x internal resolution** with a larger output window or borderless fullscreen. FSR does not lower internal resolution automatically. Selecting FSR at 2x still draws the stage and highway at 2560x1440. If the output is smaller than the source, the presenter downsamples and sharpens instead of creating additional detail. FSR processes the full game image, including the highway and text. Defaults remain bilinear; no Documents settings are changed during installation.

The GH2 build enables the SDK's existing precompiled spatial shaders and native constants, without fetching the optional temporal FidelityFX runtime. A header-owned switch keeps presenter structures consistent across the runtime DLL, GPU plugin and game. The public setting accepts bilinear, cas and fsr; unverified fsr2/fsr3 selections are rejected. Replace the executable, librexruntime.dll and rexgpu-xenos.dll together after closing the game normally.

New `GH2 timing:` lines report CPU draw translation, submission, pipeline completion, fence checks, presentation preparation/allocator waits, and DXGI Present calls. Separate D3D12 timestamp pairs report renderer GPU submissions and presentation GPU commands. Readback uses the existing completion fences: it never waits for a timing result, and skips a measurement if all 64 slots remain in flight. `GH2 guest swap:` measures the VdSwap import duration with integer sampling on the guest thread; logging remains on the UI thread. `GH2 presentation:` records active input/output dimensions and the selected spatial pass count when dimensions change.

Metrics report sample count, average, maximum, total measured milliseconds, window duration, and samples over 16.7/33.3 ms. Renderer GPU samples cover individual submissions, not complete game frames. CPU scopes can overlap: do not add their times together. These records include loading, menus and open overlays, and do not label song scenes. Sampling windows and guest-swap counters are approximate across threads. GPU time is elapsed command execution and may include GPU preemption; it is not a utilization counter. Do not infer a CPU/GPU bottleneck solely from average FPS.

After a normal play session, run `python scripts/analyze_performance.py` to write `build/performance-report.json`. Keep the same song/settings for comparisons. The next optimization depends on actual spikes: high GPU execution cost favors lower internal resolution with FSR; long pipeline/fence/Present stalls identify different work. No gameplay speedup or removal of crowd-related drops has been established by the fixture tests.

Validation: launcher hidden UI/config fixtures; fresh-process FSR registration, rejection of temporal selections, save/reload and launcher-to-runtime loading; real RTX 5070 D3D12 FSR/CAS pipeline initialization; 64 completed timestamp pairs, full-ring skip and reuse; guest floating-point-environment preservation; source packaging workflow. No user game was launched, stopped or restarted for these checks. See `docs/render_timing_validation.json` and `docs/graphics_settings_validation.json`.

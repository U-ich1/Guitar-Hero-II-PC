# Guitar Hero 2 Launcher

Double-click **Guitar Hero 2 Launcher.exe** in the project folder. It opens a white home screen with a Guitar Hero II PC logo, Play, Settings, Exit and a version footer. No batch file, Python installation or terminal is needed to use it. Keep it beside the project's `tools`, `private` and `build` folders.

The launcher shares `Documents\GH2PC\gh2.toml` with the game:

* **Graphics:** internal resolution, FXAA, filtering, window dimensions, monitor, fullscreen, VSync, a user-entered integer display FPS limit (`0` = unlimited), aspect ratio and the display-update overlay.
* **Controls:** XInput/DirectInput mode selection, named device selection, separate gamepad/guitar modes, fret/strum/Star Power/Pause mappings, whammy-axis selection and keyboard enablement.
  DirectInput raw button, stick-axis and trigger setup is reached within Controls.
* **Calibration:** added delay, buffering and mute. The flash/click reference remains available through F8 while the game is running.
* **Cheats:** Hyperspeed levels 1-5, Air Guitar, Performance Mode, Flaming Head, Eyeball/Monkey Crowd Heads and Unlock All. Unlock All disables career and saving while enabled.
* **Saves:** complete folder export/import and recoverable deletion; previous saves are retained in Documents/GH2PC/Save Backups.

**Save settings** saves your changes. **Play** saves, launches the game directly, then closes the launcher once Windows successfully starts the game. If starting the process fails, the launcher remains open and reports the error. **Reload settings** pulls in settings saved from the game's menus; **Open saves** and **Open logs** open the existing folders.

Click any fret, strum, Star Power or Pause mapping, then press the desired button on the selected controller. Automatic device selection listens to connected controllers; choosing a named device restricts capture to it. Already-held inputs are ignored until released and pressed again. The idle analog trigger signals of native XInput guitars are excluded. Escape or changing tabs cancels, and capture times out after 15 seconds without changing the previous mapping. Right-click clears a mapping. Capturing or clearing a profile binding enables Custom bindings if it was Native, while preserving an existing guitar-with-strum mode. Raw DirectInput button mappings also support click-to-capture; select a DirectInput device for those.

While GH2 runs, Save and Play are disabled to prevent duplicate launches and conflicting settings writes. Controls can still be prepared for the next launch. After closing GH2 normally, reload to see changes made in game. Closing the launcher never terminates GH2.

Only edited settings are merged into a fresh read of the config. Unknown/advanced values are preserved. Internal-resolution changes synchronize the shared and per-axis settings. Strings are formatted with toml++, the existing file is backed up as `gh2.toml.launcher-backup`, and replacement uses a temporary file. Invalid TOML is reported instead of overwritten. The known legacy raw-path defect is repaired using the existing project helper.

Before launch, the executable installs any prepared game/runtime updates while GH2 is closed, retaining previous copies. It starts the real `Guitar Hero 2.exe` with the same project log location and no shutdown timer. It does not call the batch or PowerShell launchers.

## Build and verification

Run `scripts/build_launcher.ps1` to rebuild. Native Win32/common controls provide tabs, keyboard navigation and scrolling; DPI awareness and monitor bounds keep the window usable on scaled displays. The C++ runtime is linked statically, so the launcher imports only Windows system libraries.

The launcher embeds GH2's original Xbox 360 title icon in its executable and window/taskbar icons. `scripts/extract_launcher_icon.py` reads XDBF image `0x8000` from the local decoded game image and wraps its original 64×64 PNG bytes in an ICO without altering the artwork. Run it before the first launcher build. The derived game artwork is excluded from source control.

`Guitar Hero 2 Launcher.exe --self-test` exercises config merge, unknown-setting preservation, Windows-path round trips, resolution-axis synchronization and protection against malformed config, using workspace fixtures. `--ui-test` constructs the five native tabs and validates all 69 editable fields, hover-help registration, Controls navigation and tab scroll reset without displaying the launcher or touching Documents. Reports are `docs/launcher_validation.json` and `docs/launcher_ui_validation.json`. `tests/launcher_capture.cpp` checks all supported logical input sources, held/released input behavior, guitar idle triggers, raw button 128, POV and analog trigger conversion. Physical DirectInput capture still needs a hands-on controller check.

`tests/check_graphics_settings.py --launcher` additionally reads the launcher fixture through the real runtime, including deferred renderer registration, and verifies resolution, anti-aliasing, audio and Hyperspeed. These checks do not launch GH2. The home/settings layout was visually reviewed. The final logo/header revision passes hidden UI tests; its last interactive review was stopped by the user.

Video calibration currently directs you to the game's Options > Calibrate Lag. A working launcher video calibration tool remains a future feature. Graphics titles show explanations on hover. The custom logo is documented in launcher_logo.md.

The game executable is Guitar Hero 2.exe. Its Windows file description and game window title are Guitar Hero 2, and both the executable and running window use the original GH2 title icon. The internal CMake target remains gh2_pc.

Graphics now exposes spatial FSR 1 and CAS. Use 1x internal resolution plus FSR 1 and a larger output to reduce rendering work; changing the upscaler alone does not reduce internal resolution. Save and restart normally.

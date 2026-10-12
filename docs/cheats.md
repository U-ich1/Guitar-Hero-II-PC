# PC cheats menu

Press **F9** to open the PC cheats panel. F7 remains the achievements shortcut. The launcher Cheats tab and F9 expose the retail cheats. **Hyperspeed** has these settings:

* **Original cheat codes** (default): GH2's own fret-code setting decides.
* **Off**: disable Hyperspeed for the next highway loaded.
* **Levels 1-5**: 1.50x, 1.75x, 2.00x, 2.25x or 2.50x highway scrolling. Level 1 matches the original GH2 setting.

Choose before loading a song or practice session. A loaded highway retains the setting it was constructed with; changing the menu does not rebuild the current song. The panel reports the most recently constructed highway's state. **Save cheat settings** persists `gh2_hyperspeed` in `Documents\GH2PC\gh2.toml`. Changes apply for the next song without restarting the application.

The menu uses the original game's Hyperspeed branch, not a host clock or audio-speed adjustment. In this disc build, highway setup at `0x826801F8` obtains the script variable `cheat_enabled_hyper_speed_mode` and compares it to integer 1. The native equality call at `0x826804E0` returns to `0x826804E4`. A guarded translation hook overrides only this comparison result; the original variable lookup, comparison, temporary-node cleanup and subsequent native highway code remain intact. The enabled branch multiplies the highway parameter by the original 1.5 constant at `0x82007C5C`.

`scripts/prepare_rex_runtime.py` applies the hook only to the SDK-context copy of translation 171 and fails if the expected call-site signature changes. The decoded executable, archives and original translations are not modified.

`tests/check_cheats.py` verifies the native opcode, script-variable name and multiplier against the supplied image, then compiles and executes the actual translated equality function and highway branch. All fourteen menu/native combinations pass, including original-state passthrough, forced off and forced on. Fresh-process preference reload passes. Tests do not launch GH2 or modify Documents settings. Full-song visual behavior and judgment timing remain for hands-on validation. See `docs/cheats_validation.json`.

Air Guitar, Performance Mode, Flaming Head, and the Eyeball/Monkey Crowd Head variants are applied through the original script variables. Translated Symbol construction and DataVariable lookup hooks cache their interned symbols and DataNode addresses; updates preserve the native integer/symbol node types. Tests execute these translated functions with fixture variable maps and verify adjacent memory and preserved registers. Full-song visual effects still require user testing.

Unlock All is applied by a guarded main-menu entry and poll script using the original campaign and cheat flags. Entry applies it after initialization, then refreshes the song provider through the native reset_player_settings method, as the original All Access handler does. Poll detects preference changes and refreshes once, so changing On/Off while the main menu is open takes effect without leaving the menu. If changed elsewhere, return to the main menu. It disables career and saving while enabled, matching the retail cheat. Off restores those flags and the normal song list. Original cheat-code mode leaves the native state alone. The native Options/Quit text asset patch remains unchanged.

The prepared translations also replace the guarded multiplier load for the selected Hyperspeed level. The original 1.5 constant is verified against the dump; host timing and audio speed are unchanged.

The override now observes the shared DataVariable-map lookup at 0x823187D0, restricted to the supported global tree at 0x827FE6D4. DTB variable creation bypasses the narrower DataVariable(Symbol) wrapper at 0x82318910. Read-only inspection of the running game confirmed the old hook failure: Unlock All preference 1, observed target address 0, and the actual gh2_pc_unlock_all node value 0. The new translated-function fixture exercises the direct DTB lookup, cached-variable On/Off changes, and isolation from other maps. Hands-on application after this correction is still required.

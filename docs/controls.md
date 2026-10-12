# Controller mappings

Open **F6** after launching the updated build. The Gamepad and Guitar profiles are saved independently in `Documents\GH2PC\gh2.toml`.

Start with **Native GH2 controls**, which leaves all buttons, triggers and axes untouched. Native gamepad frets are green LT, red LB, yellow RB, blue RT and orange A. GH2 automatically strums notes in this mode. The existing native XInput guitar compatibility remains active.

For different buttons, choose **Custom bindings** and select a source for each fret, strum direction, Star Power and Pause. Sources include face/shoulder buttons, triggers, D-pad, stick clicks and stick-up/down thresholds. Unbound disables that action. Trigger activation starts at 128/255. Stick direction activation starts at 16000/32767. Physical guitar analogue fields are preserved.

**Custom guitar with strum** reports a gamepad as a GH2-compatible virtual guitar. It uses your selected fret inputs, but requires a separate strum on D-pad up/down or whichever buttons you assign. This mode does not provide automatic strumming. A standard gamepad's original trigger fields are cleared after translating them into virtual fret buttons, preventing a held trigger from activating a second fret.

Pause before editing. Mapping is applied to the controller state seen by the entire game, so menu buttons also follow the selected layout. In guitar layout, green confirms and red cancels. In custom standard-gamepad layout, the orange action supplies A/confirm and native B still cancels. Start retains its normal function through its configurable binding. Menu left/right remain native.

The Whammy axis selector routes a chosen stick axis into the controller's left-X field. Native axes leaves the working hardware path untouched. Physical tilt axes remain available; stick-up/down can additionally be assigned to Star Power. Whammy/tilt behavior needs hands-on testing with each controller and mode.

If both a guitar and gamepad are connected, **Player 1 device** can select a native XInput slot explicitly. Automatic preserves the SDK's existing assignments. The selected physical device moves to guest player one without also feeding another player. Keyboard input remains available for player one. The **Monitor player** selector displays the raw buttons, triggers, axes and physical subtype being polled before remapping. Guitar trigger fields may represent analogue controls, so a trigger shown as active is not necessarily a held fret.

The selector now displays Windows product names and the input backend, rather than just numbered slots. The connected devices were identified as `Controller (8BitDo Ultimate 2 Wireless Controller for PC) (XInput 1)` and `Guitar (Harmonix Guitar for Xbox 360) (XInput 2)`. XInput product lookup is best effort: when product information is unavailable or ambiguous, the label falls back to `XInput Guitar` or `XInput Gamepad` with its slot number. It never guesses a model name from enumeration order.

Named selection is saved as a device key. DirectInput uses the Windows instance GUID, allowing the selected device to be recognized after a disconnect/reconnect even if enumeration order changes. XInput selection remains tied to its native Windows slot. A disconnected saved selection is shown explicitly; Automatic clears the selection and restores normal assignment.

## DirectInput controllers

Native DirectInput 8 is enabled alongside XInput. Select a named DirectInput device from the same Player 1 dropdown. It initially presents a standard gamepad layout and uses the Gamepad profile; choose the virtual-guitar mode if you want separate frets and strumming.

Expand **DirectInput device setup** to view the raw buttons and axes from the last polled DirectInput controller. Assign its button numbers to A/B/X/Y, shoulders, triggers, navigation and stick clicks. `0` means unbound; `1` through `128` refer to physical button numbers. Select the stick-axis sources and, if needed, analogue trigger axes. Trigger rest/direction options cover full-range axes resting at either end and shared axes resting in the middle. POV D-pads support cardinal and diagonal directions; buttons can also be assigned to D-pad directions. Save persists this DirectInput layout in the same Documents settings file. The raw layout is currently shared by DirectInput devices; select one at a time when configuring different models.

XInput devices can also appear in DirectInput. The driver filters their `IG_` HID paths/product IDs so the same controller isn't processed twice, consistent with [Microsoft's XInput/DirectInput guidance](https://learn.microsoft.com/en-us/windows/win32/xinput/xinput-and-directinput). Native XInput state and guitar subtype are preserved. DirectInput uses a nonexclusive acquisition and reacquires after input loss; force-feedback/rumble and DirectInput keystroke synthesis are not implemented. XInput vibration remains on the original native path.

`tests/check_directinput.py --probe` passes raw conversion, trigger normalization, POV directions, axis inversion, duplicate filtering, named GUID selection, reconnect and saved layout reload. The hardware enumeration found exactly the two connected XInput devices once each. No physical non-XInput controller was connected during this check, so DirectInput gameplay still requires a hands-on test. No test started, closed or restarted GH2.

Use **Save** to persist profiles and device selection. **Restore native profile** resets the selected profile and returns to passthrough controls. Keyboard key bindings are available separately under **F4 → Input/Keybinds**; those keys produce controller inputs that then follow the selected profile.

## Update and validation

If the game is open during an update, a prepared `Guitar Hero 2.next.exe` can be installed by the normal launcher on the next launch, after you close the game normally. The launcher keeps `Guitar Hero 2.previous.exe` and never stops the running game to install an update. No timed game runs are used for these controls checks.

`tests/check_controller_mapping.py` verifies 2,000 unchanged native states, all configurable sources, chords, unbinding, trigger thresholds, tilt/whammy routing, guest endian conversion, packet preservation, failed input queries, selected-slot routing, disconnect/reconnect and fresh-process profile reload. `tests/check_gh2_guitar_scheme.py` still passes the original guitar-recognition regression. These are logic and ABI checks; gameplay feel and the live panel require hands-on verification.

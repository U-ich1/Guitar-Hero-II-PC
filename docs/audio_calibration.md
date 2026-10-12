# Audio calibration

Open the PC calibration panel with **F8**. Pause the song before using its flash/click reference. GH2's existing in-game video calibration remains separate.

- Audio delay ranges from **0 to 250 ms**, with 5 ms adjustment buttons. Zero adds no delay; positive values play audio later.
- Save writes the setting to `Documents\GH2PC\gh2.toml`, shared with the F4 settings panel. It reloads on the next launch. Earlier settings were copied from the project build directory during the Documents migration.
- Change the delay between songs: changing it clears the delay line and briefly inserts silence rather than replaying stale samples.
- “Lower audio buffering” changes the backend queue from eight to four blocks. Save and restart to apply it. This reduces the queue's maximum depth from about 42.67 ms to 21.33 ms, but can cause stuttering. These are queue limits, not measured end-to-end latency.
- The flash/click reference applies the same added delay algorithm to a quiet stereo pulse. It is an approximate output reference: UI frame scheduling, device latency and the game's own queue also affect perceived synchronization. Finish checking in Practice. This is not automatic note-judgment calibration.

This version cannot advance audio beyond the available playback data. To handle sound that is already late, first use zero added delay and try lower buffering. A signed song-clock offset would need separate reverse engineering and validation; it is not silently simulated by dropping song samples.

## Implementation and checks

The delay operates on mixed interleaved floats immediately before SDL submission, including underrun silence so delayed tails can drain. Each stream has its own preallocated ring. At 48 kHz, one millisecond is exactly 48 sample frames. Stereo and six-channel paths preserve channel order. Zero is sample-for-sample passthrough. It does not change guest time, gameplay input, scoring or GH2's video calibration value. Mute is also applied after the delay so buffered sound cannot defeat it.

`audio_offset_ms` is registered in the SDK runtime with atomic storage, range validation and save/config reload. `audio_maxqframes` now has the documented bounds and restart requirement. Project-owned source lives in `runtime/audio_delay.h`, `runtime/audio_calibration_dialog.h`, and `patches/gh2_audio_offset.inl`; `scripts/apply_rexglue_patches.py` reapplies SDK changes.

`python tests/check_audio_delay.py` verifies ten sample-exact stereo/surround combinations, uneven callback sizes and ring wraps, default passthrough, resets, invalid values, concurrent offset reads, and a saved 35 ms setting reloaded in a fresh process. The report is `docs/audio_offset_validation.json`.

The bounded host check opens the panel with 35 ms added delay and starts its reference stream. Logs distinguish panel drawing from successful reference audio submission. Timed checks close only their test process at the limit; that closure is not itself a game crash. Normal `Start-GH2.cmd` launches have no timer.

Panel drawing and game startup with 35 ms added delay passed the bounded host run. The first reference attempt occurred before GH2 initialized SDL audio; the reference now initializes and balances its own SDL audio-subsystem reference. `tests/audio_reference.cpp` subsequently passed initialization, paused stereo data submission and repeated cleanup on the actual audio device, without launching or closing GH2. The visible flash and audible click relationship still needs a manual check. Automatic timed game runs were stopped after the user reported the unexpected closures.

The SDL output API queues a copy of input audio for later consumption; the delay is inserted before that queue. See the official [SDL_PutAudioStreamData documentation](https://wiki.libsdl.org/SDL3/SDL_PutAudioStreamData). Physical display/speaker latency and subjective synchronization still need hands-on calibration.

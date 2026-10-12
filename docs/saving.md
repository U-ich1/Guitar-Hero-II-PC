# Saving on PC

The normal windowed build stores user data in Windows' Documents known folder:

```text
Documents\GH2PC\
    Saves\              GH2 save packages and achievements
    gh2.toml            PC settings, including audio calibration
```

Windows resolves Documents through `SHGetKnownFolderPath(FOLDERID_Documents)`, so redirected or OneDrive Documents folders are supported. On the current machine the root is `C:\Users\Brenno Luiz\Documents\GH2PC`.

The Xbox runtime keeps its package directory structure beneath Saves. The current game's data file is:

```text
Saves\B13EBABEBABEBABE\415607E7\00000001\gh2save\gh2save.dat
```

In GH2, choosing a storage device selects the runtime's local virtual HDD. The resulting save package writes into the Documents tree. The directory name does not need to be chosen manually in game. “Continue without saving” still disables the game's persistence for that session.

“Sign in” is the original Xbox 360 local-profile requirement, not Microsoft account authentication. The PC runtime has one offline profile with the existing save XUID. It starts on guest player one. If you start GH2 with another controller in Automatic assignment, choose Sign in to move that local profile to the controller confirming the prompt. The profile name and XUID queries then succeed for that controller, and the other ports report unsigned. The save directory/ownership does not change. This is a single-profile PC policy; separate co-op profiles are not implemented.

The former SDK handler only broadcast a player-one sign-in notification without changing the profile/controller binding, so accepting the prompt from another port did nothing. The GH2 import adapters now track new button presses, bind the existing offline profile and send a notification covering the old/new ports. Held buttons and guitar analogue idle values do not change the chosen port.

`tests/local_profile.cpp` exercises the actual SDK sign-in-state/name/XUID functions for all four controller ports, verifies that only one owns the profile at a time, and confirms the original save identity is retained. The report is `docs/local_profile_validation.json`. No game launch or Documents save write is required for that test.

The previous user data from `build/windowed-state/user` and the PC settings were copied on migration. The originals remain intact. Existing Documents files are never overwritten during migration; `.legacy-import-complete` prevents old project data from being imported again over newer progress. Logs, shader cache and diagnostic frame captures remain in the project build directory.

## Verification

- A test package was created through the real SDK content manager and guest filesystem, then reopened in a separate process with the same bytes (`docs/save_runtime_validation.json`). Real GH2 title metadata was loaded; the game itself was not launched.
- The existing 60,715-byte GH2 save was migrated and reopened through the guest filesystem at the new Documents location.
- SHA-256 values matched for all copied legacy save/header/achievement/settings files (`docs/documents_save_validation.json`).
- Migration tests verified that newer destination files and original files survive, settings are copied, and subsequent launches cannot re-import stale data.

These checks establish file persistence and runtime access. Career progress and scores should also be checked in the game after its next normal launch; the tests do not interpret GH2's proprietary save contents. No timed game runs or automatic restarts were used for this migration.

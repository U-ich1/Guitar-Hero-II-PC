# Guitar Hero II PC 0.1.0

First numbered community release of the Windows recompilation project and its
launcher. Build the game from your own supported Xbox 360 dump.

## Latest changes

- Original GH2 PC wordmark and matching multi-size launcher icon replace the
  previous artwork derived from the official logo. Launcher footer and Windows
  file metadata now show version 0.1.0.
- Source packages and updates include the original artwork and project MIT
  license. Extracted game assets, generated game translations and game binaries
  are excluded.
- Graphics includes **No audience** to hide crowd rendering, available in the
  launcher and F5. Crowd sound and gameplay timing remain intact.
- Renderer draw diagnostics use sampled CPU timing to reduce measurement work;
  Discord state observation caches repeated symbol lookups.
- Experimental upscaling options are removed. Presentation uses bilinear
  filtering; this release provides no DLSS or FSR option.

## Included features

- White launcher with Play, Settings and Exit, hover help, original artwork,
  saved settings and automatic launcher close when starting the game.
- XInput guitars/gamepads and DirectInput devices, named device selection,
  separate mappings, click-to-map capture and controller reconnection.
- Graphics settings for internal resolution, anti-aliasing, filtering,
  window/display mode, VSync and a numeric presentation FPS limit.
- Documents-based saves and settings, plus save import/export/deletion with
  backups. Career persistence has been tested by the project owner.
- Retail cheats and five Hyperspeed levels. Unlock All retains the original
  game's restrictions on career and saving.
- Audio delay calibration; video calibration remains in the game's menu.
- Discord Rich Presence for mode, song, difficulty and venue imagery.
- In-game Quit option that exits to desktop.
- Pinned build dependencies and source update tooling using each player's own dump.

## Downloads and setup

- **gh2pc-source-0.1.0.zip**: project source, original launcher artwork, license
  and build/update scripts. No game content or generated game code.
- **Guitar Hero 2 Launcher.exe**: optional standalone launcher. Place it in the
  root of an existing compatible built project; it does not contain the game.
  Building the source already produces this launcher.
- **SHA256SUMS.txt**: checksums for both downloads.

For a first build, install Git for Windows and Python 3.11+ on Windows x64,
extract the source ZIP, and run:

```powershell
.\Build.ps1 -Dump "D:\Games\Guitar Hero II.iso"
```

An extracted dump folder can replace the ISO path. Setup downloads the pinned
tools and checks the supported game executable revision. For an existing build,
close the game and launcher normally and run:

```powershell
.\Update.ps1 -SourceArchive "D:\Downloads\gh2pc-source-0.1.0.zip"
```

Existing Documents saves and settings are retained. See
[building and updates](building_and_updates.md) for requirements and supported
dump hash. Allow approximately 10 GB free for a build with the ISO stored
elsewhere, or 20 GB with the ISO in the project folder.

## Validation and current limits

The owner has completed songs, saved/reloaded a career, tested controller
switching/reconnection, Discord presence and the audience toggle. Launcher
UI/config checks and source packaging/update tests pass. Performance remains
hardware- and scene-dependent; this release does not promise a locked frame
rate. Gameplay simulation remains at the original 60 Hz. Launcher video
calibration and higher simulation rates are future work.

Unofficial community project, not affiliated with or endorsed by the game's
rights holders. Original project code is provided under the included MIT
license; dependencies retain their own licenses. Game content is not included.

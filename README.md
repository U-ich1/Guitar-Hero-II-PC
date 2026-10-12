<p align="center">
  <img src="https://raw.githubusercontent.com/U-ich1/Guitar-Hero-II-PC/main/docs/images/project-logo.svg" alt="GH2 PC project logo" width="480">
</p>

# Guitar Hero II PC

An independent project bringing the Xbox 360 version of **Guitar Hero II** to Windows, with a launcher and PC settings. Play using your own game dump and build your own copy on your computer.

**[Download the latest release](https://github.com/U-ich1/Guitar-Hero-II-PC/releases/latest)** · **[Report a problem](https://github.com/U-ich1/Guitar-Hero-II-PC/issues)**

This project has **no connection to GH2 Deluxe**. It is not based on GH2 Deluxe and will remain a separate, independent effort.

## What you can do today

- Play songs and Career mode, with progress saved in your Documents folder.
- Use guitars and gamepads, choose your controller, and remap buttons by pressing them on the device. Both XInput and DirectInput controllers are supported.
- Change resolution, anti-aliasing, filtering, window mode, VSync and the FPS limit.
- Hide the audience through **No audience** in Graphics to reduce crowd rendering.
- Adjust audio delay and use the game's video calibration.
- Enable cheats and choose between five Hyperspeed levels.
- Import, export and manage saves from the launcher.
- Show your mode, song, difficulty and venue in Discord Rich Presence.
- Start the game from the launcher and quit to desktop from the game's menu.

## Screenshots

Saved captures from the project's earlier working build.

![Surrender gameplay with the band, stage and note highway](https://raw.githubusercontent.com/U-ich1/Guitar-Hero-II-PC/main/docs/images/surrender-gameplay.png)

![Surrender song intro in the Battle of the Bands venue](https://raw.githubusercontent.com/U-ich1/Guitar-Hero-II-PC/main/docs/images/surrender-intro.png)

## Install and build your own copy

### 1. Get what you need

- A Windows 10 or 11 PC, 64-bit, with DirectX 12 graphics support. [Detailed requirements](docs/building_and_updates.md) are available separately.
- Your own **Guitar Hero II Xbox 360** game dump, as an ISO or an extracted folder. The current build supports a specific retail Xbox 360 version; PS2 dumps and modified game files are not supported. The setup checks your dump for you.
- [Git for Windows](https://git-scm.com/install/windows) and [Python 3.12, 64-bit](https://www.python.org/downloads/release/python-31210/). Python 3.12 is the version used for the verified build.
- Internet access for the first build, and about **10 GB free** if your ISO is stored elsewhere, or **20 GB** if you keep it in the project folder.

During Python installation, enable **Add python.exe to PATH** so the build can find it.

The downloads here contain the project tools and source, **not the game**. Everyone supplies their own dump and builds their own copy.

### 2. Download the project

Open the [latest release](https://github.com/U-ich1/Guitar-Hero-II-PC/releases/latest) and download the file named **gh2pc-source-VERSION.zip**. For 0.1.0, that is `gh2pc-source-0.1.0.zip`.

Extract the ZIP into a folder such as `C:\Games\GH2PC`. Open that folder and check that `Build.ps1` is there. The launcher download on its own is for an existing built copy; it does not install the game.

### 3. Build it from your dump

Open PowerShell or Windows Terminal. Go to the folder you extracted:

```powershell
cd "C:\Games\GH2PC"
```

Check that both installed tools are available:

```powershell
python --version
git --version
```

Then run the build, replacing the example path with the location of **your own ISO**:

```powershell
.\Build.ps1 -Dump "D:\Games\Guitar Hero II.iso"
```

If your dump is already extracted, use its folder instead:

```powershell
.\Build.ps1 -Dump "D:\Games\My GH2 Dump"
```

The first build downloads the tools it needs and creates your playable copy. It can take a while; leave the terminal open until it finishes. You do not need to install a separate compiler or manually copy the game's files.

If Windows blocks the PowerShell script, use this command in the same folder:

```powershell
python scripts/build_project.py --dump "D:\Games\Guitar Hero II.iso"
```

### 4. Set up and play

Open **Guitar Hero 2 Launcher.exe** in your project folder. Use **Settings → Controls** to select and map your controller, adjust Graphics if needed, then select **Play**.

Keep the launcher beside the project's `tools`, `private` and `build` folders. Your saves are stored in `Documents\GH2PC\Saves`, and settings are stored in `Documents\GH2PC`.

## Update an existing copy

Close the game and launcher normally. Download the newer **gh2pc-source-VERSION.zip**, then open a terminal in your existing project folder and run:

```powershell
.\Update.ps1 -SourceArchive "D:\Downloads\gh2pc-source-NEW-VERSION.zip"
```

Replace the example filename with the ZIP you downloaded. This updates and rebuilds your copy using the dump you already imported. Your Documents saves and settings are kept.

## Future plans

- Smoother performance, especially in busy venues.
- More testing and fixes across different PCs and controllers.
- Video calibration directly in the launcher, alongside audio calibration.
- Further improvements to the launcher, settings and overall PC experience.
- Research into higher frame rates while keeping song timing accurate.
- Explore the possibility of **online multiplayer**. It is a future idea to investigate, not an available feature or a confirmed release commitment.

## Current status

Songs, Career saves, controller switching and reconnection, Discord presence and the audience toggle have been tested during development. Performance is still being improved and can vary between PCs. Experimental upscaling options have been removed.

If something goes wrong, [open an issue](https://github.com/U-ich1/Guitar-Hero-II-PC/issues) with what happened, your PC and controller details, and any relevant logs. Never attach your ISO or game files.

## Credits and license

Built with [XenonRecomp](https://github.com/hedge-dev/XenonRecomp) and [ReXGlue](https://github.com/rexglue/rexglue-sdk).

Original project code is available under the [MIT license](LICENSE). Dependencies keep their own licenses. Guitar Hero II and its game content belong to their respective rights holders. This is an unofficial community project and is not affiliated with or endorsed by them. Gameplay screenshots are shown to illustrate the project.

For development details, see the [build guide](docs/building_and_updates.md) and [earlier development notes](docs/development_history.md).

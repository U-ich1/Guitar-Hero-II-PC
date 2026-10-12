# Graphical updater

Open **Guitar Hero 2 Updater.exe** from your existing project folder. It is a
separate application; the launcher keeps its current layout.

The latest published release is selected by default. Choose another version
from the list if preferred, and read its release notes before installing.
Enable **Show preview releases** to include preview builds.

Close the game and launcher normally, then select **Install selected version**.
The updater downloads and verifies the source package, installs it and rebuilds
the game using your existing dump. No manual ZIP extraction is needed. Keep the
window open while the build runs; progress and build output appear in the window.

Your Documents saves and settings are retained. Updated source files are backed
up under `build/source-update-backups`. The updater does not stop the game or
launcher for you. You can cancel a download; a build already in progress must
finish before closing the updater.

Use **Open log** if an update fails. Downloads and logs are kept under
`build/updater`. Failed downloads are not installed, and a failed build is not
recorded as a completed update.

For an older installation that does not include the updater, download the
standalone updater from a release and put it beside `Build.ps1`, `Update.ps1`
and the launcher. The project must already have been built from your own dump.
The updater uses the Python installation from that build.

Releases that require different build dependency revisions need a separate
installation folder. See the [build guide](building_and_updates.md) for first
installation and manual update commands.

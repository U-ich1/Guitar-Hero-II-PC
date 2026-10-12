# Building from your dump and installing source updates

Source releases contain the project code, patches and build configuration. Each recipient generates native game code from their own supported Xbox 360 dump. Game assets, generated translations and binaries are not included.

## First build

Requirements: Windows 10/11 x64, an AVX-capable CPU, compatible Direct3D 12 GPU, Git for Windows, Python 3.11+, internet access for initial setup, and storage for tools, the dump and build outputs. The verified build uses Python 3.12.

Extract the source ZIP into a folder, open PowerShell there, then run:

```powershell
.\Build.ps1 -Dump "D:\Games\Guitar Hero II.iso"
```

An extracted dump folder containing `default.xex`, `gen` and `videos` can replace the ISO path. If a local PowerShell script is blocked by your execution policy, the Python entry point runs the same workflow:

```powershell
python scripts/build_project.py --dump "D:\Games\Guitar Hero II.iso"
```

Child build scripts use a process-local execution-policy option; no machine policy is changed. Nonstandard Python/Git paths can be passed as `-Python` and `-Git` to the PowerShell entry point.

The workflow downloads checksum-verified LLVM-MinGW/CMake archives, fetches exact XenonRecomp/ReXGlue commits, initializes required submodules at their gitlink revisions, creates a project-local Python environment with pinned packages, validates/imports the dump, applies patches, generates translations, builds the game/renderer/runtime, extracts the icon from the local game and builds the launcher. No game is launched automatically.

Open **Guitar Hero 2 Launcher.exe** after completion. Keep the project layout intact: the executable belongs beside `tools`, `private` and `build`. Saves and preferences remain in `Documents\GH2PC`, including redirected Documents locations. Build/setup does not write them.

## Supported dump

The retail Xbox 360 executable has title ID `415607E7`, media ID `69550273`, and SHA256:

```text
4f91842399e5e69085c295bc736a82abf3e2235c8f2143c347d9043cd633e596
```

The hash is authoritative. Other revisions, modified executables and PS2 dumps are rejected before translation. Modified archives have not been validated. The decoded image is independently checked against the hash in `dependencies.lock.json`.

## Updates

Close this project's game and launcher normally, then install a source ZIP from the maintainer:

```powershell
.\Update.ps1 -SourceArchive "D:\Downloads\gh2-recomp-source-NEW.zip"
```

The updater verifies every file against the archive manifest, restricts writes to project source files, backs up overwritten sources in `build/source-update-backups`, applies the update and rebuilds from the existing dump. Invalid archives are rejected before writing source files; write failures restore changes made by that operation. Dumps, generated data, tools, Documents settings and saves are excluded from source updates.

For a Git checkout, pull the project's source changes and rerun `Build.ps1`. No hosted project repository or release server has been published by these scripts.

```powershell
git pull --ff-only
.\Build.ps1
```

Building refuses to replace binaries while this project's game or launcher runs; no processes are killed. A different project copy does not block an isolated build. `-Offline` reuses installed caches after initial online setup. Missing cached prerequisites produce an error. `-LauncherOnly` is for changes explicitly identified as launcher-only.

If a future release changes a vendor dependency pin, a different existing vendor revision is not reset automatically. Preserve your vendor edits, move that checkout aside, then rerun online setup. Ordinary project-code updates using the same pins rebuild in place.

## Preflight and identity

```powershell
.\Build.ps1 -Check
```

Preflight reports source identity, pins, tool checksums and whether an existing imported executable matches, without downloading or compiling. Successful builds write `build/build-receipt.json`: source-content fingerprint, input/dependency pins, UTC build time and output hashes. Source identity normalizes LF/CRLF line endings so Windows preparation does not change the release identity. Archive manifests still verify exact file bytes. This identifies inputs and outputs; it does not promise bit-for-bit identical binaries across machine paths.

`dependencies.lock.json` pins the official [LLVM-MinGW release](https://github.com/mstorsjo/llvm-mingw/releases/tag/20261006), [CMake release](https://github.com/Kitware/CMake/releases/tag/v4.4.4), repository commits and required submodules. Python package versions are in `requirements-build.txt`. Optional Vulkan, FidelityFX, profiling and SDK tests are disabled.

## Share a source-only release

```powershell
python scripts/package_source.py
```

The ZIP and SHA256 sidecar go to `build/releases`. An explicit allowlist includes project sources, patch files, configuration, scripts, tests and Markdown documentation. It excludes ISO files, extracted assets, generated translations, extracted artwork/icon, binaries, downloaded tools, build products, validation logs, saves and settings. Do not share the whole working project directory. Upstream notices in patches and downloaded dependencies should be retained.

`SOURCE-MANIFEST.json` records each source file's checksum. This detects corruption/unexpected content; it is not a publisher signature. Obtain updates through a trusted source.

## Verification

The pristine-source audit exports pinned dependency sources and required submodules from local Git objects, applies patches twice and compares the result with the working sources. It found no uncaptured changes. The isolated full build used fresh translation and CMake output with verified local dependency/tool caches and read-only game assets. This verifies clean-source reproduction on the current Windows machine; another-machine hardware compatibility remains to be checked.

`tests/build_workflow.py` covers source-only allowlists, update backup/preservation, tampered manifests, path traversal, cached-tool checksum rejection and unsupported executable rejection, using workspace fixtures. These checks do not modify Documents data. Audit results are recorded under `docs/*validation.json` and `docs/reproducibility_audit.json`, but those reports are excluded from source releases.

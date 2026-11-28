# Locus

A multichannel spatializer: the **Locus** app renders up to 16 sources to up to 32 speakers on your audio interface, and the **Locus Send** plugin (VST3, plus AU on macOS) streams each DAW track to it. Full specification: [BUILD_PROMPT.md](BUILD_PROMPT.md).

Status: milestone 6 of 7. The engine, the plugin link and the full app UI work: room canvas (drag sources; Shift = elevation, Alt = orientation, Ctrl/Cmd = aperture; wheel to zoom), source list, Source / Axis / Omni / Room / Speakers tabs, output strip with meters, a Settings dialog for the audio device, the custom Speaker Editor (Speakers › Edit custom…), and session Load/Save in the top bar. The session is also saved on quit and restored on launch. Still to come: the performance pass (milestone 7). **Debug builds** of the app play a test signal on source 1 while no Send plugin holds slot 1.

## Try it in a DAW (Windows)

1. Build Release (below).
2. Make the plugin visible to the DAW, either way:
   - copy `build\send\LocusSend_artefacts\Release\VST3\Locus Send.vst3` to `C:\Program Files\Common Files\VST3` (needs admin), or
   - **Reaper**: Options › Preferences › Plug-ins › VST, add `C:\Users\<you>\SpatMultichannelClone\build\send\LocusSend_artefacts\Release\VST3` to the VST3 paths and click Re-scan. **Ableton Live** only scans the standard VST3 folder (or its custom VST3 folder setting under Plug-ins).
3. Start `build\app\Locus_artefacts\Release\Locus.exe`.
4. Put **Locus Send** on a track and play. The plugin's status line should read "Connected · Slot 1". The track itself goes silent; its sound comes out of the app.
5. Automate Azimuth/Distance/Elevation in the DAW, or turn the plugin's knobs, and the sound moves.

Windows note: the DAW and the app both play audio. Most ASIO drivers allow only one program at a time, so either give the DAW a different driver/device (e.g. WASAPI) or use a multi-client ASIO driver. Pick the app's device, sample rate, buffer size and output channels in **Settings** (top right); the choice is remembered.

## Checks

- `build\tests\LocusTests_artefacts\Release\LocusTests.exe`: unit tests (also run by `ctest`), including the layout check of every tab at min and default window size.
- `LocusTests.exe --snapshots <folder>`: renders the app window (every tab), the Settings panel and the Send editor to PNG files.
- `build\tests\LocusPluginCheck_artefacts\Release\LocusPluginCheck.exe "build\send\LocusSend_artefacts\Release\VST3\Locus Send.vst3"`: loads the built plugin like a DAW and checks the whole link end to end. Close the Locus app first.

## Build

Requirements: CMake 3.22+, git, and Visual Studio 2022 (Windows) or Xcode (macOS). JUCE is downloaded automatically on first configure.

Windows (use a CMake that has the Visual Studio generators: the one in a "Developer PowerShell for VS 2022", or `C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe`; an MSYS2 `cmake` on PATH will not work):

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

macOS:

```sh
cmake -S . -B build -G Xcode
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

## Outputs (Release)

- App: `build/app/Locus_artefacts/Release/Locus.exe` (or `Locus.app`)
- Plugin: `build/send/LocusSend_artefacts/Release/VST3/Locus Send.vst3` (AU under `AU/` on macOS)
- Tests: `build/tests/LocusTests_artefacts/Release/LocusTests.exe`

To install the plugin, copy `Locus Send.vst3` to `C:\Program Files\Common Files\VST3` (Windows) or `~/Library/Audio/Plug-Ins/VST3` (macOS).

## Windows installer (for sharing)

After a Release build, with [Inno Setup 6](https://jrsoftware.org/isinfo.php):

```powershell
& "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" installer\Locus.iss
```

This writes `dist\Locus-<version>-Windows-Setup.exe`. It installs the app into Program Files and the plugin into `C:\Program Files\Common Files\VST3`, adds a Start menu entry and an uninstaller, and includes [FRIENDS_README.txt](FRIENDS_README.txt) as "Read me". The binaries link the Visual C++ runtime statically, so nothing else needs installing. They are not code-signed, so Windows SmartScreen warns on first run ("More info" › "Run anyway").

The app and the plugin must come from the same build: the shared-memory name carries a layout version (`LocusShm_v2`), so mismatched versions simply don't see each other.

## macOS package (built on GitHub Actions)

No Mac is needed: [.github/workflows/macos.yml](.github/workflows/macos.yml) builds the Mac version on a `macos-latest` runner on every push (and on demand: Actions › macOS build › Run workflow). It:

1. configures with `cmake -S . -B build -G Xcode` (universal arm64 + x86_64, macOS 12+, set in the root `CMakeLists.txt`) and builds Release;
2. ad-hoc signs `Locus.app`, `Locus Send.vst3` and `Locus Send.component` (Apple Silicon refuses to run unsigned code; this is not a Developer ID signature);
3. runs `ctest`, then `LocusPluginCheck` against the built VST3;
4. runs [pluginval](https://github.com/Tracktion/pluginval) at strictness 10 on the VST3, and `auval` plus pluginval strictness 10 on the AU (installed into `~/Library/Audio/Plug-Ins/Components` on the runner);
5. runs [installer/build-mac-pkg.sh](installer/build-mac-pkg.sh), which writes `dist/Locus-<version>-macOS.pkg`, and uploads it as the **Locus-macOS** artifact (pluginval logs go to **pluginval-logs**).

Download the artifact from the run's Summary page (it arrives zipped; unzip to get the `.pkg`). The package installs `Locus.app` into `/Applications`, `Locus Send.vst3` into `/Library/Audio/Plug-Ins/VST3` and `Locus Send.component` into `/Library/Audio/Plug-Ins/Components`. On a Mac, `bash installer/build-mac-pkg.sh` after a Release Xcode build makes the same package locally.

Nothing is signed with an Apple Developer ID or notarized, so Gatekeeper blocks the package and the app on first open; [FRIENDS_README.txt](FRIENDS_README.txt) explains how to open them anyway. On macOS the shared memory is a POSIX `shm_open` object named `/LocusShm_v2` (macOS caps these names at 31 characters).

## License

Locus is licensed under the GNU Affero General Public License v3 ([LICENSE](LICENSE)), matching JUCE's free license. The Windows build includes the Steinberg ASIO SDK headers (`third_party/asiosdk`), used under the SDK's GPLv3 option; see `third_party/asiosdk/LICENSE.txt`. ASIO is a trademark of Steinberg Media Technologies GmbH.

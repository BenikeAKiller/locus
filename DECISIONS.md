# Decisions

Interpretations of BUILD_PROMPT.md, one line each.

- JUCE pinned to tag 8.0.9 (latest JUCE 8 release at setup time).
- `locus_core` is a CMake INTERFACE library, not STATIC: its sources compile into each target alongside that target's JUCE modules, which avoids duplicate JUCE module symbols when the app, plugin and tests all link JUCE.
- Locus Send is built with `COPY_PLUGIN_AFTER_BUILD FALSE`, so building never needs admin rights; install by copying the .vst3 manually (see README).
- The Inter font is added in milestone 5 (UI), when it is first used.

## Milestone 2: layouts and panners

- Circular presets put speaker 1 at 0° and go clockwise; the opposite speaker of an even layout sits at +180° (the range is (−180°, 180°]).
- 3D VBAP adds a virtual zenith as well as a virtual nadir when no real speaker is above +10°, so any 3D custom layout covers the whole sphere.
- A virtual speaker's energy is shared equally (in power) among the real speakers next to it in the triangulation, rather than thrown away. Throwing it away leaves directions near the pole silent (all gains zero); sharing it still sends no sound to a speaker that doesn't exist.
- Coplanar hull faces (e.g. the four 45° top speakers) are merged into one polygon and fan-triangulated, so triangles never overlap. If the hull fails the 2V − 4 triangle check, or every speaker lies in one plane, 3D VBAP falls back to 2D pairwise panning on azimuth.
- 2D VBAP pairs 170° or wider (only possible with very sparse custom layouts) use the sine/cosine law instead of the VBAP inverse, which turns negative near 180°.
- HOA is implemented as a point-source pattern (addition theorem: Σ (2n+1) wₙ Pₙ(cos γ) in 3D, w₀ + 2 Σ wₘ cos mγ in 2D) evaluated at the AllRAD virtual points. This is mathematically identical to encoding to ACN/SN3D and decoding, but there is no ambisonic bus, so the channel convention never shows up.
- The HOA "power within ±1 dB" test measures the decoder output *before* the final normalisation; after normalisation it would always hold trivially.
- Spread for 2D MDAP: the ring directions are projected to the horizontal plane, so the 8 extra points land at azimuth offsets within ±spread × 90°.

## Milestone 3: source DSP, room and output

- HOA is now computed in the harmonic domain (decode matrix = AllRAD projected onto orthonormal real spherical/circular harmonics). It gives the same result as the milestone 2 version (same test numbers: 0.067 dB, 2.15°) at about 1/15 of the cost per call.
- Processing runs in 32-sample chunks: smoothed parameters, filter coefficients and panning gains update once per chunk, and gains are interpolated per sample within it.
- Panning type, speaker setup and HD changes use a 15 ms fade-out, swap, 15 ms fade-in (30 ms total), like the original device ("short fade-in/fade-out"), instead of running old and new renders in parallel for a 30 ms crossfade. Same click-free result at half the CPU.
- Orientation is relative to the listener: 0° means the source faces the listener, so the directivity angle θ equals |Orientation|.
- Stereo inputs: early taps alternate between the L and R channels (even taps from L, odd from R); cluster taps alternate the same way. The late-reverb send is (L + R) × 0.707.
- Early taps: positions are jittered by a fixed table (±0.3 of a slot) so reflections aren't evenly spaced. Early Shape tilts the tap gains linearly; total early energy is then normalised.
- Cluster directions: the 4 diffuse positions (±45°, ±135°) are matched to the 4 packed positions in order of angle relative to the source, so they never cross while Pan Rev moves.
- Early and cluster stages pan with 50 % spread; the late reverb pans its line directions with VBAP at 30 % spread (spec).
- Early/cluster panning is refreshed at most every 4 chunks (2.7 ms at 48 kHz) while a source moves; direct sound every chunk. This halved the CPU cost of moving sources with Angular panning.
- Room-stage calibration relative to the direct sound at the near radius: early energy 0.5 (−3 dB), cluster energy 0.35 (−4.6 dB), late output gain 0.35. These set the default balance; the Early/Cluster/Reverb levels adjust from there.
- FDN line lengths (ms at 2500 m³) are fixed tables for 8 and 16 lines, scaled by cbrt(roomSize / 2500), rounded to distinct primes, and ramped over 20 ms when Room Size changes (read with linear interpolation, so no clicks).
- Late-reverb predelay = late onset (100 ms × scale + Offset) minus the shortest line, so late energy starts at the specified onset.
- A source keeps rendering for 1 s after its input stops, so its room tail finishes; then it sleeps until audio arrives again (and snaps to its current parameters on wake).
- The limiter has no lookahead (spec), so a hard clamp at the ceiling catches the first millisecond of a transient.
- Axis/Omni EQ accuracy: with neighbouring bands at most 24 dB apart, the response is within 0.12 dB of the set gains at 20 Hz, 1 kHz and 20 kHz. With a full 48 dB swing (e.g. Low +24, Mid −24), the 2nd-order S = 1 shelves required by the spec leave 1.76 dB at 1 kHz, so the spec's ±0.5 dB check can't pass for that case. The test asserts ±0.5 dB up to 24 dB steps and logs the 48 dB cases. Steeper (4th-order) shelves would fix it but change the spec's filter. **Confirmed by the user (2026-11-26): keep the spec's 2nd-order shelves.**
- The debug-only test signal is noise bursts (easier to localise than a sine) on source 1, circling at 45°/s. Default speaker setup in the app is 5.0 with Align on, until the UI exists.

## Milestone 4: IPC and the Send plugin

- Slots carry two heartbeats instead of one: `aliveBeat` (the plugin's timer: the owner exists) and `audioBeat` (processBlock: audio is flowing). A stopped transport keeps a slot owned but idle; only a missing `aliveBeat` for 2 s lets another plugin take the slot over (e.g. after a DAW crash).
- App → plugin edits carry a change counter per parameter, so the plugin applies only what the app changed and never reverts host automation with stale app values.
- Plugin → app values are accepted only once the plugin has applied every app edit (it echoes the last app sequence it saw) and only from the slot's current owner. This stops a value from jumping back during a drag in the app.
- The Send plugin publishes parameters from its 30 Hz timer (one writer, the message thread); automation on the audio thread only sets a flag. It also republishes once a second as a keep-alive.
- Seqlock reads give up after 10,000 attempts instead of spinning forever, so a plugin that crashed mid-write can't hang the app.
- The ring writer drops a block when the ring is full (the app isn't reading); the app flushes the ring when a slot becomes active and buffers up to the target before playing. If a slot gets more than 3× the target ahead (e.g. after the app stalled), the app skips forward instead of slowly draining.
- Drift control: PI controller (Kp 2e-6 per frame, Ki 2e-7 per frame-second) on the buffer fill smoothed over 0.25 s; ratio correction limited to ±0.5 % (spec). With a 48 kHz plugin into a 44.1 kHz app and 0.05 % clock drift, 30 s ran with no underruns and the fill stayed at 909–1488 frames (target 1024).
- The VST3 wrapper adds a host Bypass parameter (JUCE does this for plugins that don't define one). Bypassing the Send in a DAW passes the track's dry audio through and stops streaming, which is the DAW's normal bypass behaviour.
- Status line has one state beyond the spec's four: "Cannot connect to the app" when the shared memory can't be opened or holds an incompatible layout version.
- The Send editor uses a minimal flat look with the spec's colour tokens until the shared theme and Inter font arrive in milestone 5.
- `LocusPluginCheck` (tests/PluginCheck.cpp) hosts the built VST3 like a DAW and plays the app's role, to check audio, parameter sync, host gestures, state, offline rendering and slot release end to end. It is not run by ctest because it uses the real shared memory; don't run it while the Locus app is open.

## Milestone 5: app UI

- Inter 4.1 (Regular and SemiBold, OFL) is embedded from the official release (github.com/rsms/inter); `resources/fonts/Inter-LICENSE.txt` carries the licence. Font sizes are point heights (11 / 13 / 15).
- The theme, look-and-feel, Knob, ToggleDot and LayoutChecker live in a shared `uikit/` library (spec said `app/ui/Theme.h`) because the Send plugin uses the same theme and controls.
- The app's code (everything except `main()`) is an INTERFACE library, `locus_app_lib`, so the unit tests build the real main window headless (no audio device, a private shared-memory name).
- Every control refreshes from the parameter stores at 30 Hz (values also change from plugins and host automation); there are no change broadcasters.
- The spec's per-tab "Reset" restores that tab's parameters; Speakers › Reset also returns the setup to 5.0 (with Radius 2 m and Align on).
- The Speakers tab labels the Speaker Radius knob "Radius" (as the original does): "Speaker Radius" doesn't fit an 80 px cell at 13 pt, and the tab already says Speakers.
- Session Load/Save (top bar) and "Edit custom…" (Speakers tab) are shown but disabled until milestone 6.
- The Gain slider in Axis/Omni spans two cells (168 px); the room mix levels are vertical sliders in 80 × 84 cells with their on/off dot as the cell label.
- Canvas: speaker labels are only drawn where they touch nothing (other labels, speakers, meters, sources), so dense layouts (e.g. 32ch zoomed out) drop some labels instead of overlapping them. The output meters drop speaker labels, then channel numbers, when space runs out.
- Canvas drag in the front view moves the source straight up or down at its current horizontal distance (sets elevation and distance, keeps azimuth). In 2D setups Shift-drag does a normal position drag, since elevation is disabled.
- The Room plot uses a square-root time axis so the early structure stays visible next to a tail of up to 20 s; early taps, cluster and late onset come from the same `computeRoomTimes()` the engine uses.
- The plugin's parameters display with the app's formatting in the DAW (e.g. "2.10 m", "5.66 kHz").
- `LocusTests --snapshots <dir>` renders every tab at min and default size, a 3D Top+Front view, the Settings panel and the Send editor to PNG offscreen, for layout review without screen captures.

## Milestone 6: Speaker Editor and sessions

- **Confirmed by the user (2026-11-26):** the Speaker Editor replaces the canvas while open, not "in place inside the tab area" as the spec says. The 11-column table and two plots cannot fit the 376 × 380 px tab area at the minimum window size. The Speakers tab's "Edit custom…" button becomes "Done" while editing.
- Opening the editor switches the setup to Custom, starting from a copy of the current setup the first time. Setup › Custom does the same without opening the editor.
- Edits apply live when valid. Table edits apply on Enter; plot drags apply on release (applying on every drag step would trigger the 30 ms setup fade continuously). While invalid, the first problem shows above the table ("… Not applied until fixed."), duplicate or out-of-range Output cells and out-of-range Distance cells are highlighted red, and the last valid setup keeps playing.
- The table shows Delay and Gain corrections whether or not Align is on; Align decides whether they are applied.
- The plots have a "Cartesian" toggle for the grid (polar rings and 30° spokes by default), a scale bar, and auto-fit zoom that freezes during a drag so the view doesn't move under the mouse. Speaker numbers keep a 3 px gap from everything (here and on the main canvas) so adjacent numbers can't read as one.
- Speaker files (`.locusspk`) store x / y / z rounded to the millimetre.
- Session files (`.locussession`) store parameters by id, so they survive parameter reordering; missing values use defaults and out-of-range values are clamped. A session whose custom setup fails validation is refused as a whole.
- Sessions store the spec's §10 contents only (setup, radius, Align, room, output, known sources with names and parameters), not canvas view options.
- On load, connected Send plugins' own values replace the session's stored values on their next publish (within a second), because the plugin is the source of truth (spec §10).
- Autosave: `<app data>/Locus/Autosave.locussession`, written on quit, read on launch; a missing or unreadable file starts fresh.

## After milestone 6: fixes from real use

- Choppy audio from DAWs with large blocks (found in Bitwig at 2048 frames / 44.1 kHz): the plugin delivers audio in bursts of one host block, so a 1024-frame target ran dry between bursts. The app now measures each plugin's block size from the shared-memory counters (frames written ÷ blocks written) and keeps at least one plugin block + one app block + 512 frames buffered, whatever the Link buffer setting; the skip-ahead threshold scales with it. Test: 2048-frame plugin blocks into a 48 kHz / 512 app run 30 s with no underruns (it had 4 before the fix). No plugin change, so no reinstall.

## Release preparation

- Windows builds link the Visual C++ runtime statically (`CMAKE_MSVC_RUNTIME_LIBRARY`), so Locus and Locus Send need only Windows system DLLs, with no Visual C++ Redistributable install.
- The ring buffer grows from the spec's 16384 to 32768 frames per channel, so DAWs with 8192-frame blocks don't drop out (the test of 8192-frame blocks had an underrun at 16384). The shared-memory name becomes `LocusShm_v2` and the layout version 2, so an old plugin and a new app never share memory; they simply don't see each other.

## 0.1.1: first feedback from users

- ASIO is enabled on Windows (`JUCE_ASIO=1`). Reported: an RME Fireface UFX couldn't be selected via ASIO and showed only 6 channels through Windows Audio (WASAPI shared mode follows the Windows speaker configuration, e.g. 5.1 = 6 channels). The Steinberg ASIO SDK 2.3.4 `common/` headers are vendored in `third_party/asiosdk` under the SDK's GPLv3 option; Locus is now explicitly AGPLv3 (`LICENSE`), which is GPLv3-compatible and matches JUCE's free license.
- Output channel mapping: a device callback receives only the active output channels, packed. The app now maps them back to device channel numbers, so switching an output off in Settings no longer shifts every later speaker onto the wrong output; a speaker on an inactive channel renders into a scratch buffer and is silent.
- Reported: "it only moves between two speakers" (a speaker layout on a device with fewer outputs). The top bar now says "Outputs N–M not available on this device (see Settings)" in the warning colour whenever the layout uses outputs the open device can't play.

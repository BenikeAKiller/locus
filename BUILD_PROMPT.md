# Build prompt: "Locus", a multichannel spatializer (JUCE)

You are building a desktop spatial audio product in C++ with the JUCE framework. Its feature set comes from IRCAM's **SPAT Spatial MC** Max for Live device (from Ableton's "Spat Multichannel" pack). Its architecture and single-window layout take after **FLUX:: SPAT Revolution**. Build exactly what this document specifies. Do not add features that are not listed here. If something is ambiguous, choose the simplest interpretation, write it down in `DECISIONS.md`, and carry on. If something cannot be done as written, stop and ask the user.

"Locus" is a working name. Do not use "SPAT", "IRCAM" or "FLUX" in any product name, UI text, or binary name.

---

## 1. Product summary

Locus has two parts:

1. **Locus** (standalone desktop app). This is the rendering engine. It owns the multichannel audio interface. It holds up to **16 sources** in one shared virtual room, pans them to a loudspeaker setup of up to **32 speakers**, and outputs straight to the interface's channels.
2. **Locus Send** (VST3 on Windows and macOS, plus AU on macOS). The user puts one instance on each DAW track they want to spatialize. It streams that track's mono or stereo audio to the app over local shared memory. It also exposes that source's parameters as DAW-automatable parameters, synced in both directions with the app.

The setup has to work in any DAW, including Ableton Live, because the Send plugin only ever needs mono or stereo I/O.

Platforms are Windows 10+ (x64) and macOS 12+ (universal binary). There is **no** real-time return of the rendered audio into the DAW and **no** offline bounce. The spatial mix only exists at the app's audio output.

---

## 2. Explicit non-goals (do NOT build)

- The perceptual "Fact" tab (Source Presence, Warmth, Brilliance, Room Presence, Envelopment, Running Reverberance).
- Separate Room MC or Multiverb MC products, and the 30-band drawable decay editor.
- Copy/Paste/Send buttons between instances. They are unnecessary because there is one app with one shared room and speaker setup.
- The A/B stereo-pair routing panel. It is replaced by an output-channel column per speaker.
- A dummy LFE channel, bass management, or any LFE feed. The LFE output channel is simply left unused.
- Venue-specific presets (IRCAM studio domes, 360RA, Auro-3D).
- A floating or always-on-top panner window.
- A zoom control for the reverb view.
- A Return plugin, offline bounce support, binaural/headphone rendering, head tracking, OSC, MIDI control, multiple rooms, source groups, snapshots/cues, hardware inputs as sources, undo/redo in the app, and preset browsers.
- Gradients, drop shadows, glows, skeuomorphic knobs, bitmap skins, and splash screens.

---

## 3. Tech stack and repository layout

- C++20, CMake ≥ 3.22, and JUCE 8 pulled with `FetchContent`, pinned to a specific release tag. No other third-party libraries.
- Embed the **Inter** font (OFL licence) through `juce_add_binary_data`, so text metrics are identical on Windows and macOS.
- Targets:
  - `locus_core`: static library with DSP, speaker layouts, parameter definitions, and IPC. It has no GUI code.
  - `Locus`: a `juce_add_gui_app` target. It links `locus_core`.
  - `LocusSend`: a `juce_add_plugin` target (VST3, plus AU on macOS). It links `locus_core`.
  - `LocusTests`: a console app that runs `juce::UnitTestRunner`.
- Layout:
  ```
  CMakeLists.txt
  core/      dsp/ layouts/ params/ ipc/
  app/       ui/ (components), engine/ (audio callback, source manager)
  send/      plugin processor + editor
  tests/
  resources/ fonts/
  DECISIONS.md, README.md (build + usage)
  ```
- Real-time rules apply in every audio callback: no allocation, no locks, no logging, and no blocking I/O. When the speaker setup, panning type or HD mode changes, rebuild the DSP objects on the message thread and swap them in through an atomic pointer. The old and new renders then crossfade over **30 ms** so nothing clicks. The original device does the same.
- Smooth every continuous parameter: 20 ms linear ramps, and per-speaker gains interpolated across each block.

---

## 4. Coordinates and conventions

- x points right, y points front, z points up. The listener sits at the origin.
- Azimuth: 0° is front, and **positive angles go to the right (clockwise seen from above)**. The range is (−180°, 180°].
- Elevation: 0° is the horizontal plane, +90° is straight up, and −90° is straight down.
- Distance is in metres. The speed of sound is 343 m/s.

---

## 5. Speaker setups

### 5.1 Presets
Every preset speaker sits at **Speaker Radius** (default 2.0 m, range 0.5–20 m). Output channels are the defaults shown below and can be edited. Channel 4 is skipped wherever the standard layout reserves it for LFE.

| Group | Name | Speakers (label: azimuth / elevation → output channel) |
|---|---|---|
| 2D | 4.0 | L −45/0→1, R 45/0→2, Ls −135/0→3, Rs 135/0→4 |
| 2D | 5.0 | L −30→1, R 30→2, C 0→3, Ls −110→5, Rs 110→6 (all el 0) |
| 2D | 7.0 | L −30→1, R 30→2, C 0→3, Lss −90→5, Rss 90→6, Lrs −150→7, Rrs 150→8 |
| 3D | 5.0.2 | 5.0 + Ltm −90/45→7, Rtm 90/45→8 |
| 3D | 5.0.4 | 5.0 + Ltf −45/45→7, Rtf 45/45→8, Ltr −135/45→9, Rtr 135/45→10 |
| 3D | 7.0.2 | 7.0 + Ltm −90/45→9, Rtm 90/45→10 |
| 3D | 7.0.4 | 7.0 + Ltf −45/45→9, Rtf 45/45→10, Ltr −135/45→11, Rtr 135/45→12 |
| 3D | 9.0.4 | 7.0 + Lw −60/0→9, Rw 60/0→10, Ltf −45/45→11, Rtf 45/45→12, Ltr −135/45→13, Rtr 135/45→14 |
| 3D | 9.0.6 | 9.0.4 + Ltm −90/45→15, Rtm 90/45→16 |
| Circular | 4ch … 32ch | N speakers at azimuth `i·360/N` (i = 0…N−1, wrapped to ±180), el 0, labels "1"…"N", outputs 1…N |
| — | Custom | Edited in the Speaker Editor (§5.3) |

These angles are Locus defaults. Do not describe them as standards-compliant anywhere in the UI or docs.

A layout counts as **3D** if any speaker has a non-zero elevation. For 2D layouts, the Elevation control and the Top+Front view are disabled. Panning then uses azimuth only.

### 5.2 Align
Align is a toggle, on by default. When the speakers are not all at the same distance, it compensates each speaker `i`:
- delay `(d_max − d_i) / 343` seconds
- gain `20·log10(d_i / d_max)` dB

The Speaker Editor shows both values read-only.

### 5.3 Speaker Editor (inside the app's Speakers tab)
- Table with one row per speaker and these columns: `#`, Label (editable, max 8 characters), X, Y, Z (m), Azimuth (°), Elevation (°), Distance (m), Output (1–32), Delay corr. (ms, read-only), Gain corr. (dB, read-only). Editing either the Cartesian or the polar columns updates the other set.
- Buttons: **Add**, **Remove**, **Start from preset ▾** (copies any preset into Custom), **Load…**, **Save…**. Files are JSON with the extension `.locusspk`, containing labels, xyz and output.
- Below the table there are two plots: Top (x/y) and Front (x/z), each with polar or cartesian grid lines and a scale bar in metres. Dragging a speaker moves it, and its coordinates show next to it while dragging.
- Limits: 1–32 speakers, distances 0.5–50 m, and output channels must be unique. If they are not, the duplicate cell is highlighted and the setup is not applied until it is fixed.
- Edits to Custom apply live with the 30 ms crossfade. Speaker Radius does not apply to Custom.

---

## 6. Per-source parameters (16 slots)

These live in `locus_core/params` as a single table. The Send plugin's automatable parameters and the app's UI are both generated from this table. Units and defaults are fixed.

**Source tab**

| ID | Name | Range | Default | Notes |
|---|---|---|---|---|
| azimuth | Azimuth | −180…180 ° | 0 | |
| distance | Distance | 0…50 m | 1.0 | |
| elevation | Elevation | −90…90 ° | 0 | Disabled for 2D layouts |
| doppler | Doppler | off/on | off | |
| orientation | Orientation | −180…180 ° | 0 | 0 = source faces the listener |
| aperture | Aperture | 10…360 ° | 80 | 360 = omnidirectional |
| width | Width | 0…180 ° | 30 | Disabled for mono sources |
| spread | Spread | 0…100 % | 0 | |
| panRev | Pan Rev | 0…100 % | 0 | |
| panning | Panning | Angular / Nearest / Ambisonics | Angular | Crossfades on change |
| air | Air | off/on | on | |
| airFreq | Air Freq | 1–20 kHz | 10 kHz | |
| nearRadius | Near Radius | 0.1…10 m | 1.0 | Shown as "Near Radius" so it is not confused with Speaker Radius |
| dropMode | Drop Mode | Lin / Log | Log | |
| drop | Drop | 0…24 dB | 6.0 | |

**Axis tab** (EQ on the direct sound) and **Omni tab** (EQ on the signal sent to the room) have identical parameter sets:

| Name | Range | Default |
|---|---|---|
| Low | −24…+24 dB | 0 |
| Mid | −24…+24 dB | 0 |
| High | −24…+24 dB | 0 |
| Freq Low | 20–1000 Hz | 177 |
| Freq High | 1–20 kHz | 5660 |
| Gain | −60…+12 dB | 0 |
| Bypass | off/on | off |
| Mute | off/on | off |

Each tab has a **Reset** button that restores that tab's parameters to their defaults.

---

## 7. DSP

### 7.1 Per-source chain
```
input (mono or stereo, from the Send ring buffer)
 → [Doppler delay]
 → split ─┬─ Direct: Air LPF → Directivity shelf → Axis EQ → Axis gain → distance gain → panner(az, el, width, spread)
          ├─ Early:  Omni EQ → room gain → Early taps (§7.4) → panner
          ├─ Cluster: Omni EQ → room gain → Cluster (§7.4) → panner
          └─ Late send: Omni EQ → room gain → shared late-reverb bus (§7.4)
```
- **Distance gain.** Let `d' = max(d, nearRadius)`.
  - Log mode: `gain_dB = −drop · log2(d' / nearRadius)`.
  - Lin mode: `gain_dB = −drop · (d' − nearRadius) / nearRadius`.
  - Gain never goes above 0 dB, because it is clipped inside Near Radius. The direct path takes the full distance gain. Early takes half of it in dB. Cluster and late take none, since room energy is roughly independent of distance.
- **Doppler.** A fractional delay line (cubic Lagrange) with delay `d / 343` s, a 50 m maximum, and the delay-time smoothing set by the distance ramp. Turning Doppler on or off crossfades over 30 ms.
- **Air.** A 2nd-order low-pass with cutoff `airFreq · nearRadius / d'`, clamped to 500 Hz–20 kHz. Off means the filter is bypassed.
- **Directivity.** θ is the angle between the source's facing direction and the direction from the source to the listener. If θ ≤ aperture/2, there is no correction. Otherwise, a high shelf at 3 kHz drops linearly from 0 dB down to −12 dB as θ goes from aperture/2 to 180°.
- **Stereo width.** A stereo input becomes two point sources at `az ± width/2`, rotated with azimuth and elevation. A mono input is a single point.
- **Double-shelving EQ (Axis/Omni).** Overall gain = Mid. Add a 2nd-order low shelf at Freq Low with gain (Low − Mid) and a 2nd-order high shelf at Freq High with gain (High − Mid). Use RBJ shelving with S = 1, so boost and cut curves mirror each other in dB on a log-frequency axis. Bypass makes the EQ flat but keeps Gain. Mute silences that path.

### 7.2 Panners
All three panners output N speaker gains and normalise them to **constant power** (Σg² = 1).

- **Angular (VBAP).**
  - 2D: pairwise between adjacent speakers sorted by azimuth.
  - 3D: triplets from the convex hull of the speaker directions (a small quickhull, N ≤ 32). If the layout has no speaker below −10°, add a virtual nadir speaker whose gain is thrown away before normalisation.
  - Spread uses MDAP: 8 extra virtual directions on a ring at `spread% · 90°` around the target, with the gains summed and then normalised.
- **Nearest (KNN).** `k = 2` (2D) or `3` (3D), plus `round(spread% · (N − k))`. Weight is `1 / (angle + 1°)²` over the k nearest speakers, then normalised.
- **Ambisonics (HOA).** ACN ordering, SN3D normalisation, with max-rE weights.
  - Order is automatic: 2D gets `min(floor((N−1)/2), 7)`, circular harmonics only. 3D gets `min(floor(sqrt(N)) − 1, 3)`.
  - Decoding uses AllRAD: decode to a dense virtual layout (2D: 72 points every 5°; 3D: a 240-point Fibonacci sphere), then VBAP those points onto the real speakers.
  - Spread blends the order weights toward order 1 (at 0 % the full order is used; at 100 % only order 1).
  - A stereo source is encoded as two points.

### 7.3 Panning switch
Changing the panning type builds the new panner off the audio thread and crossfades over 30 ms.

### 7.4 Room (one shared room; global parameters)
The time structure follows the original: direct at 0 ms, early reflections 20–40 ms, cluster 40–100 ms, late reverb from 100 ms. All of these times scale by `cbrt(roomSize / 2500)` and then shift by Offset.

- **Early (per source).** 8 taps (16 with HD) spread across the early window.
  - Tap gains follow Early Shape. At 0 % gains fall off linearly. At 50 % they are flat. At 100 % they rise linearly.
  - Taps alternate between `az − width/2 − 20°` and `az + width/2 + 20°`, panned with the source's own panner at 50 % spread.
- **Cluster (per source).** 4 delay lines across the cluster window, each passing through 2 allpasses (g = 0.6). The outputs are panned to 4 virtual directions. At Pan Rev 0 those directions are fixed at ±45° and ±135° (diffuse). At Pan Rev 100 they are packed around the source azimuth within ±(width/2 + 30°). Directions between those are interpolated.
- **Late (shared).** An FDN with 8 lines (16 with HD) and a Hadamard mixing matrix. Delay lengths are mutually prime and scaled by room size.
  - Each line has its own 3-band absorption (low shelf and high shelf at Freq Low and Freq High), so the decay times are `T60_low = decay · low`, `T60_mid = decay`, `T60_high = decay · high`.
  - Infinite sets the effective T60 to 1000 s.
  - Each FDN output goes to a fixed virtual direction: equally spaced around the circle in 2D, or a Fibonacci sphere over the upper hemisphere in 3D. These are panned with VBAP at 30 % spread onto the speakers.
  - Room Air is a 1st-order low-pass at Air Freq on the FDN input.
- Changing HD crossfades over 30 ms.

**Room parameters** (global, Room tab):

| Name | Range | Default |
|---|---|---|
| Room Size | 10–100 000 m³ (log) | 2500 |
| Offset | 0–100 ms | 0 |
| Early Shape | 0–100 % | 50 |
| Decay | 0.1–10 s (log) | 2.5 |
| Infinite | off/on | off |
| Low (decay ×) | 0.1–2.0 | 1.0 |
| High (decay ×) | 0.1–2.0 | 0.5 |
| Freq Low | 50–1000 Hz | 250 |
| Freq High | 1–16 kHz | 8000 |
| HD | off/on | off |
| Air | off/on | on |
| Air Freq | 1–20 kHz | 10 000 |
| Early / Cluster / Reverb level | −60…+12 dB | 0 each |
| Early / Cluster / Reverb enable | on/off | on each |

The Room tab has a **Reset** button.

### 7.5 Output (global)
- Master Volume: −60…+12 dB, default 0.
- Limiter (on by default): a linked peak limiter across all active outputs, ceiling −0.1 dBFS, 1 ms attack, 100 ms release, no lookahead.
- Per-speaker Align delay and gain (§5.2) are applied before the master stage.
- Speaker outputs are written to the device channels given by each speaker's Output column.

---

## 8. IPC between Send and App

Write a small wrapper for named shared memory: `CreateFileMappingW`/`MapViewOfFile` on `Local\LocusShm_v1` on Windows, and `shm_open`/`mmap` on macOS. Everything else uses JUCE.

**Layout.** A header holds magic, version, app PID and an app heartbeat counter. It is followed by 16 slots. Each slot holds:
- `std::atomic<uint64_t> owner` (a random instance ID, 0 = free) and `std::atomic<uint32_t> heartbeat`
- `sampleRate`, `numChannels` (1 or 2), and `name[64]` (UTF-8)
- Two parameter blocks (plugin→app and app→plugin), each a seqlock (`atomic<uint32_t> seq` plus a `float` array in §6 table order) with a gesture flag
- A stereo SPSC ring buffer of `float[2][16384]` with atomic write and read indices

**Send plugin.**
- Slot parameter: Auto (takes the first free slot) or 1–16. If the chosen slot is already owned, the status reads "Slot N in use", nothing is sent, and the plugin retries once a second.
- `processBlock` writes the input into the ring, bumps the heartbeat, and **outputs silence**.
- When `isNonRealtime()` is true, the plugin writes nothing, and its UI and status read "Offline render not supported".
- It publishes its parameters on change, and its name from `updateTrackProperties` when the host provides one (otherwise "Source N").
- A 30 Hz message-thread timer applies app→plugin edits using `beginChangeGesture` / `setValueNotifyingHost` / `endChangeGesture`, driven by the gesture flag, so touch and latch automation recording works. Values that come from the app must not be echoed back.
- It releases its slot in the destructor.

**App.**
- A slot is **active** when its heartbeat has changed within the last 200 ms. An inactive slot is silent and its ring is flushed, but its parameters stay as they were.
- On the plugin side, the plugin's parameters are the source of truth on connect. App UI edits are written to the app→plugin block and also applied locally right away.
- Reading audio:
  - A target fill level (Settings: 256 / 512 / 1024 / 2048 frames, default 1024).
  - Drift compensation per slot: a PI controller on the fill error drives a resampling ratio limited to ±0.5 %, using `juce::LagrangeInterpolator`.
  - If the slot's sample rate differs from the app's, the ratio also includes the rate conversion.
  - An underrun outputs silence for that source and increments a counter shown in the source list.
- If the app is not running, the Send status reads "App not running".

---

## 9. App UI

### 9.1 Visual rules (mandatory)
- Flat dark theme with **solid fills only**: no gradients, shadows or blur. Corner radius is 4 px for panels and 2 px for controls. Strokes are 1 px.
- Colour tokens, defined once in `ui/Theme.h`:
  - `bg #16181B`, `panel #1E2125`, `stroke #2C3036`
  - `text #E6E8EB`, `textDim #9AA1A9`
  - `accentSource #F2A93B` (source and position controls)
  - `accentRoom #4FC3D9` (room controls)
  - `warn #E5534B`
  - Disabled controls are drawn at 40 % opacity.
  - Source colours are a fixed palette of 16 flat colours assigned by slot number.
- Font is Inter only, at 3 sizes: 11 px (values, small labels), 13 px (control labels, tabs), 15 px (section titles).
- Spacing sits on an 8 px grid.
- Knobs are 44 px: a 270° arc track plus a value arc and a pointer line. The label goes **above** the knob (13 px) and the value **below** it (11 px, with units), in a fixed 80 × 84 px cell.
- Interaction:
  - Dragging a knob changes it, double-click lets you type a value, and Alt-click resets it to default.
  - Toggles are 16 px dots with a text label (filled = on), as in the original.
  - Buttons always have text labels; there are no icon-only buttons.
- **No overlapping or clipped text, ever.** Never call `setMinimumHorizontalScale` below 1.0, and never let text squash. If a label might not fit, the layout has to make room for it. Add a debug-only `LayoutChecker` that walks the component tree and asserts three things: each child lies inside its parent, visible siblings do not intersect (unless they are explicitly marked as overlays), and every label's or button's text width fits its bounds. Run it from a unit test that builds the main window headless at the minimum size and at the default size, and fail the test on any violation.

### 9.2 Window layout
The default window is 1280 × 800, the minimum is 1024 × 680, and it is resizable.
```
┌───────────────────────────────────────────────────────────────────────┐
│ Top bar (40): Locus · Session: Load / Save · Device 48 kHz · CPU 12%   │
│                                                     Settings          │
├──────────────────────────────────────────┬────────────────────────────┤
│ View: [Top | Top+Front]  ● Labels  ● VUs │ Sources (list, 16 rows max,│
│                                          │ only connected/known ones) │
│                                          ├────────────────────────────┤
│              Canvas                      │ Tabs: Source Axis Omni     │
│      (room view, fills remaining)        │       Room Speakers        │
│                                          │ (panel width fixed 400 px) │
├──────────────────────────────────────────┴────────────────────────────┤
│ Output strip (64): Volume  ● Limiter  ● VUs   meters for used channels │
└───────────────────────────────────────────────────────────────────────┘
```
- **Source list rows** (24 px high): a colour swatch, the slot number, the name (elided with "…" if too long), a status (Active / Idle), and an underrun count when it is above 0. Clicking a row selects that source. The Source, Axis and Omni tabs always edit the selected source, and their header shows its name in its colour.
- **Source tab.** Two rows of knobs, matching the original's grouping:
  - Row 1: Orientation, Aperture, Width, Spread.
  - Row 2: Azimuth, Distance, Elevation, Pan Rev.
  - Toggles: Doppler, Air. Controls: Air Freq, Panning dropdown, Near Radius, Drop Mode dropdown, Drop.
  - Reset button.
- **Axis and Omni tabs.** A frequency-response plot (20 Hz–20 kHz log, ±24 dB, filled area in a solid colour at 35 % opacity, plus vertical lines at Freq Low and Freq High), then knobs for Low, Mid, High, Freq Low and Freq High, a Gain slider, Bypass and Mute toggles, and a Reset button. Axis uses `accentSource`; Omni uses `accentRoom`.
- **Room tab.**
  - A static time-structure plot drawn as solid strokes: the direct impulse, early taps, the cluster block, and three decay lines for low, mid and high. All of it updates live from the parameters.
  - Knobs: Room Size, Offset, Early Shape, Decay (with an Infinite toggle), Low, High, Freq Low, Freq High.
  - Toggles: HD, Air with Air Freq.
  - A Mix row with Early, Cluster and Reverb, each having an enable dot and a vertical level slider.
  - Reset button.
- **Speakers tab.** A Setup dropdown (grouped as in §5.1), Speaker Radius, Align, an **Edit custom…** button that opens the editor (§5.3) in place inside the tab area, and a Reset button.
- **Output strip.** A horizontal Volume slider, Limiter and VUs dots, and one thin vertical meter per used output channel, labelled with the channel number and grouped by speaker label. Meters are peak with a 1.5 s hold.

### 9.3 Canvas
- **Top view.**
  - Concentric distance rings every 1 m (every 5 m when zoomed out), two diagonal guide lines, and the listener as a filled circle at the centre.
  - The Speaker Radius ring is drawn in `stroke`. The selected source's Near Radius is drawn as a filled disc in `panel`.
  - Speakers are 14 px squares with an optional label underneath and an optional VU bar beside them.
  - Sources are 18 px circles in their slot colour with the slot number inside. A stereo source also gets two 6 px dots at its L and R positions. The selected source also shows its orientation and aperture as an outline wedge.
- **Top+Front.** Splits the canvas into two equal halves. The front view (x/z) shows elevation. This is only enabled for 3D layouts.
- **Mouse (top view).**
  - Drag a source to set azimuth and distance.
  - **Shift-drag** sets elevation.
  - **Alt-drag** sets orientation.
  - **Ctrl-drag** (Cmd on macOS) sets aperture.
  - The mouse wheel or a trackpad pinch zooms, and double-clicking empty space resets the zoom.
  - Dragging a source in the front view sets elevation and distance.
  - Drag gestures are sent to the Send plugin with begin/end gesture flags (§8).
- While dragging, show a small readout next to the source: `az 32.0°  dist 2.10 m  el 0.0°`. Place it so it stays inside the canvas.

### 9.4 Settings dialog
`juce::AudioDeviceSelectorComponent` (device type, device, sample rate, buffer size, output channels), the IPC target buffer size, and the canvas default zoom. These settings live in `juce::PropertiesFile`.

### 9.5 Send plugin editor
The editor is 320 × 160 and not resizable. It has the same theme and contains:
- the Slot dropdown (Auto / 1–16)
- the source name, as read-only text
- a status line: Connected · Slot N, App not running, Slot N in use, or Offline render not supported
- Azimuth, Distance and Elevation knobs

Every other parameter is reachable through host automation and through the app.

---

## 10. State

- **App session** (`.locussession`, JSON): speaker setup (preset name, or the full custom data), Speaker Radius, Align, room parameters, output parameters, and the last known per-slot parameters with their names. The app autosaves to the user app-data folder on quit and restores it on launch. Load and Save are in the top bar.
- **Send plugin state**: APVTS state plus the Slot choice, via `getStateInformation`.
- When a plugin connects, its parameters overwrite the app's stored values for that slot.

---

## 11. Milestones (build and test after each one; never continue with a failing build or failing tests)

1. CMake skeleton with all four targets building on Windows (MSVC) and macOS (Xcode or Ninja); an empty window; an empty plugin; the test runner.
2. Layouts and panners (§5.1, §5.2, §7.2) with unit tests.
3. Per-source DSP, room and output (§7) with unit tests. Drive it in the app from a built-in test tone **in debug builds only**.
4. IPC and the Send plugin (§8). Test the SPSC ring and the seqlock. Test manually in Reaper and Ableton Live on Windows.
5. App UI (§9.1–9.4) and the LayoutChecker test.
6. Speaker Editor (§5.3) and session save/load (§10).
7. Performance pass and README.

**Required unit tests**
- VBAP gives Σg² = 1, and a source placed exactly on a speaker gives that speaker gain 1.
- KNN normalises correctly, and k grows with spread.
- For every Circular layout, HOA total power across a 360° sweep stays within ±1 dB.
- 3D VBAP triangulation covers the sphere for every 3D preset (no direction yields all-zero gains).
- Distance law: with Log mode and Drop 6, doubling the distance beyond Near Radius gives −6.0 dB ± 0.01. Inside Near Radius the gain is 0 dB.
- Align values match a hand-computed example.
- Double-shelf EQ: response at 20 Hz, 1 kHz and 20 kHz is within ±0.5 dB of the set gains for extreme settings.
- Ring buffer: 10⁶ frames pushed across two threads arrive intact.
- Every preset has unique output channels.
- The LayoutChecker passes at the minimum and default window sizes, with the Source, Axis, Omni, Room and Speakers tabs each shown.

**Performance target:** 16 stereo sources on 7.0.4 with HD off, at 48 kHz with a 256-sample buffer, should use under 15 % of one core on a recent desktop CPU in a Release build. Measure it and report the number in the README.

---

## 12. Working rules for you (the implementing agent)

- Implement only what this document specifies. If you think a feature is missing, ask the user. Do not add it.
- Record any interpretation you make in `DECISIONS.md`, one line each.
- Do not copy assets, text or graphics from IRCAM, Ableton or FLUX.
- Before you call a milestone done, build in Release, run `LocusTests`, and state the results honestly, including any failures.

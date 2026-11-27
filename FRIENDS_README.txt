LOCUS 0.1.0 - multichannel spatializer
=====================================

Locus places up to 16 tracks from your DAW around a speaker setup (stereo to 32 speakers).
It has two parts:

  - Locus (the app): owns your audio interface and plays the spatialized sound.
  - Locus Send (a VST3 plugin, plus an Audio Unit on Mac): put it on each DAW track you
    want to place. The track's sound goes to the app instead of the DAW's output.


INSTALL (Windows 10 or 11, 64-bit)
----------------------------------
1. Run Locus-0.1.0-Windows-Setup.exe.
   Windows may say "Windows protected your PC", because the installer isn't signed with a
   paid certificate. Click "More info", then "Run anyway".
2. The installer puts:
     - the app in        C:\Program Files\Locus
     - the plugin in     C:\Program Files\Common Files\VST3\Locus Send.vst3
   Nothing else is needed (no Visual C++ or other runtimes).
3. In your DAW, rescan plugins if Locus Send doesn't show up:
     - Ableton Live: Settings > Plug-Ins > turn on "Use VST3 Plug-in System Folders", Rescan.
     - Bitwig: it rescans by itself; give it a minute, then search "Locus".
     - Reaper: Options > Preferences > Plug-ins > VST > Re-scan.
     - FL Studio: Options > Manage plugins > Find more plugins.

To uninstall: Settings > Apps > Locus > Uninstall.


FIRST USE
---------
1. Start Locus (Start menu > Locus).
2. Click Settings (top right) and pick your audio interface, sample rate and buffer size,
   and turn on the output channels you use.
3. Pick your speaker layout in the Speakers tab (or build your own with "Edit custom...").
4. In the DAW, put Locus Send on a track and press play. The plugin should say
   "Connected · Slot 1", and the track appears in Locus's source list.
5. Move the source by dragging it on the canvas in Locus, or with the plugin's Azimuth /
   Distance / Elevation knobs (these can be automated in the DAW).

Start Locus before or after the DAW, either works. Your setup is saved when you quit Locus
and restored next time. Session > Save / Load keeps several setups.


IF SOMETHING DOESN'T WORK
-------------------------
Plugin says "App not running"
    Start Locus.

Plugin says "Cannot connect to the app"
    Locus and the DAW must run as the same user. Don't run the DAW (or Locus) "as
    administrator". Close both and start them normally.

No sound from Locus, or Locus can't open the audio interface
    Most ASIO drivers let only ONE program use the interface. If the DAW uses it through
    ASIO, set the DAW to another device or driver (e.g. WASAPI / Windows Audio, or your
    computer's built-in output), so Locus can have the interface. Check Settings in Locus.

Sound is choppy or crackles
    In Locus, Settings > Link buffer: pick a bigger value (2048 frames). Also try a bigger
    buffer size for the audio interface in the same window.

Plugin says "All slots in use" / "Slot N in use"
    Locus takes 16 tracks at once. Remove a Send from another track, or pick another slot
    in the plugin.

Plugin says "Offline render not supported"
    Locus plays in real time, so the DAW's "export / bounce faster than real time" can't go
    through it. Record Locus's output instead, or export in real time.

The window is too big for the screen
    Locus opens to fit your screen; the smallest size is 1024 x 640.

Antivirus warns about Locus
    It's new and unsigned, so some antivirus tools are cautious. It only talks to your audio
    interface and to the Locus Send plugins on the same computer; it doesn't use the internet.


MAC (macOS 12 Monterey or newer, Apple Silicon or Intel)
---------------------------------------------------------
INSTALL
1. Double-click Locus-0.1.0-macOS.pkg.
   The package isn't signed with a paid Apple Developer ID, so macOS says it "cannot be
   opened" / "can't check it for malicious software". Instead: right-click (or Ctrl-click)
   the .pkg > Open > Open.
   On macOS 15 Sequoia or newer right-click > Open no longer works: try to open it once,
   then go to System Settings > Privacy & Security, scroll down and click "Open Anyway".
2. The installer puts:
     - the app in                 /Applications/Locus.app
     - the VST3 plugin in         /Library/Audio/Plug-Ins/VST3/Locus Send.vst3
     - the Audio Unit (AU) in     /Library/Audio/Plug-Ins/Components/Locus Send.component
3. Start Locus the first time with right-click > Open > Open (or "Open Anyway" in
   Privacy & Security, as above). After that it opens normally.
4. Rescan plugins in the DAW (Logic: Settings > Plug-in Manager > Reset & Rescan Selection;
   Ableton Live: Settings > Plug-Ins > Rescan). If Logic still doesn't list Locus Send, log
   out and back in, or restart the Mac.

IF THE DAW REFUSES THE PLUGIN ("damaged", "can't be opened", or it never shows up)
   Open Terminal (Applications > Utilities) and paste these two lines, then rescan:
     sudo xattr -dr com.apple.quarantine "/Library/Audio/Plug-Ins/VST3/Locus Send.vst3"
     sudo xattr -dr com.apple.quarantine "/Library/Audio/Plug-Ins/Components/Locus Send.component"
   (It asks for your Mac password; nothing is shown while you type.) The same works for the
   app if it says "damaged":
     sudo xattr -dr com.apple.quarantine /Applications/Locus.app

MAC NOTES
   - On a Mac, the DAW and Locus can use the same audio interface at the same time (Core Audio
     is shared), so the ASIO tip above doesn't apply.
   - Logic Pro / GarageBand on Apple Silicon may run plugins in a separate, sandboxed process
     that can't reach the Locus app; the plugin then says "Cannot connect to the app". Use
     the VST3 in another DAW (Ableton Live, Reaper, Bitwig...) if that happens.

To uninstall: drag /Applications/Locus.app to the Bin, and delete the two Locus Send files
listed in step 2.

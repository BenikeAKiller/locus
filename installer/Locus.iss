; Windows installer for Locus (the app) and Locus Send (the VST3 plugin).
; Build:  "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" installer\Locus.iss
; after a Release build of the whole project. Output: dist\Locus-<version>-Windows-Setup.exe

#define AppName "Locus"
#define AppVersion "0.1.1"
#define BuildDir "..\build"

[Setup]
; Never change AppId: Windows uses it to find the installed copy for upgrades and uninstalling.
AppId={{2AE25D99-5297-49BF-A834-0FEB8DFA95ED}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher=Locus
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
UninstallDisplayIcon={app}\Locus.exe
OutputDir=..\dist
OutputBaseFilename=Locus-{#AppVersion}-Windows-Setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
; 64-bit only, like the binaries.
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
; The plugin goes into the system-wide VST3 folder, which needs administrator rights.
PrivilegesRequired=admin
; Ask to close Locus, or a DAW that has the plugin loaded, before replacing files.
CloseApplications=yes
RestartApplications=no

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "{#BuildDir}\app\Locus_artefacts\Release\Locus.exe"; DestDir: "{app}"; Flags: ignoreversion
; The whole VST3 bundle folder, into C:\Program Files\Common Files\VST3, where every DAW looks.
Source: "{#BuildDir}\send\LocusSend_artefacts\Release\VST3\Locus Send.vst3\*"; DestDir: "{commoncf64}\VST3\Locus Send.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "..\FRIENDS_README.txt"; DestDir: "{app}"; DestName: "Read me.txt"; Flags: ignoreversion isreadme

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\Locus.exe"
Name: "{group}\Read me"; Filename: "{app}\Read me.txt"
Name: "{group}\Uninstall {#AppName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\Locus.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\Locus.exe"; Description: "{cm:LaunchProgram,{#AppName}}"; Flags: nowait postinstall skipifsilent runasoriginaluser
; runasoriginaluser: started from the (elevated) installer, Locus would otherwise run as
; administrator, and Send plugins in a normally started DAW could not reach it.

[UninstallDelete]
; The bundle folder itself (files inside are removed automatically).
Type: dirifempty; Name: "{commoncf64}\VST3\Locus Send.vst3\Contents\Resources"
Type: dirifempty; Name: "{commoncf64}\VST3\Locus Send.vst3\Contents\x86_64-win"
Type: dirifempty; Name: "{commoncf64}\VST3\Locus Send.vst3\Contents"
Type: dirifempty; Name: "{commoncf64}\VST3\Locus Send.vst3"

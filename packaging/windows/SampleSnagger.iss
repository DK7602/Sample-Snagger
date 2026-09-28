; Sample Snagger - Windows installer (Inno Setup 6)
;
;   iscc /DAppVersion=1.0.0 /DArtefacts="C:\path\to\build\SampleSnagger_artefacts\Release" packaging\windows\SampleSnagger.iss
;
; Installs:
;   VST3        -> C:\Program Files\Common Files\VST3\Sample Snagger.vst3
;   Standalone  -> C:\Program Files\Sample Snagger\Sample Snagger.exe  (+ Start menu / desktop shortcut)

#ifndef AppVersion
  #define AppVersion "1.0.0"
#endif
#ifndef Artefacts
  #define Artefacts "..\..\build\SampleSnagger_artefacts\Release"
#endif

[Setup]
AppId={{6E2B8B1C-5C7B-4B3E-9E1B-5A9F1D2C7A11}
AppName=Sample Snagger
AppVersion={#AppVersion}
AppPublisher=Snagger Audio
DefaultDirName={autopf}\Sample Snagger
DefaultGroupName=Sample Snagger
DisableProgramGroupPage=yes
OutputBaseFilename=Sample-Snagger-{#AppVersion}-Windows-Setup
SetupIconFile=icon.ico
UninstallDisplayIcon={app}\Sample Snagger.exe
; tells Explorer to refresh its icon cache, so an update shows the new icon straight away
ChangesAssociations=yes
Compression=lzma2/max
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
WizardStyle=modern

[Types]
Name: "full";   Description: "Plug-in + standalone app"
Name: "custom"; Description: "Choose components"; Flags: iscustom

[Components]
Name: "vst3"; Description: "VST3 plug-in (Ableton Live, FL Studio, Cubase, Studio One, Reaper, Bitwig ...)"; Types: full custom
Name: "app";  Description: "Standalone app"; Types: full custom

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; Components: app

[Files]
Source: "{#Artefacts}\VST3\Sample Snagger.vst3\*"; DestDir: "{commoncf64}\VST3\Sample Snagger.vst3"; Components: vst3; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#Artefacts}\Standalone\Sample Snagger.exe"; DestDir: "{app}"; Components: app; Flags: ignoreversion

[Icons]
Name: "{group}\Sample Snagger"; Filename: "{app}\Sample Snagger.exe"; Components: app
Name: "{autodesktop}\Sample Snagger"; Filename: "{app}\Sample Snagger.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\Sample Snagger.exe"; Description: "Launch Sample Snagger"; Flags: nowait postinstall skipifsilent; Components: app

[Messages]
FinishedLabel=Sample Snagger is installed.%n%nRescan plug-ins in your DAW, then open Settings (gear icon) inside Sample Snagger once to install the free helper tools for HQ downloads and AI stems.

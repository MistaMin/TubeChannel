; Tube Channel Windows installer (Inno Setup 6)
; Built by installer\build_installer_win.ps1, which stages the license bundle and
; passes /DMyAppVersion from the VERSION file.

#ifndef MyAppVersion
  #define MyAppVersion "1.2.0"
#endif

#define AppName "Tube Channel"
#define AppPublisher "OpenGrid"
#define Root ".."
#define Artefacts Root + "\build\TubeChannel_artefacts\Release"
#define Stage Root + "\dist\stage"

[Setup]
AppId={{553C552A-0EDC-47D9-81D8-18DF680F5EF4}
AppName={#AppName}
AppVersion={#MyAppVersion}
AppVerName={#AppName} {#MyAppVersion}
AppPublisher={#AppPublisher}
VersionInfoVersion={#MyAppVersion}
VersionInfoDescription={#AppName} audio plug-in
DefaultDirName={autopf}\{#AppPublisher}\{#AppName}
DisableDirPage=yes
OutputDir={#Root}\dist
OutputBaseFilename=TubeChannel-{#MyAppVersion}-win
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64os
ArchitecturesInstallIn64BitMode=x64os
PrivilegesRequired=admin
DefaultGroupName={#AppName}
LicenseFile={#Stage}\licenses\Tube Channel\LICENSE_BINARY.txt
UninstallDisplayName={#AppName}

[Types]
Name: "full"; Description: "Full installation"
Name: "custom"; Description: "Custom installation"; Flags: iscustom

; LV2 installs to the per-user LV2 discovery folder even in admin mode (per the
; LV2 spec on Windows, hosts check %APPDATA%\LV2).

[Components]
Name: "vst3"; Description: "VST3 plug-in"; Types: full custom; Flags: checkablealone
Name: "clap"; Description: "CLAP plug-in"; Types: full custom; Flags: checkablealone
Name: "lv2"; Description: "LV2 plug-in"; Types: full custom; Flags: checkablealone
Name: "standalone"; Description: "Standalone application"; Types: full custom; Flags: checkablealone
Name: "docs"; Description: "User guide and license texts"; Types: full custom; Flags: fixed

[Files]
; VST3 bundle -> C:\Program Files\Common Files\VST3
Source: "{#Artefacts}\VST3\Tube Channel.vst3\*"; DestDir: "{commoncf64}\VST3\Tube Channel.vst3"; \
    Flags: ignoreversion recursesubdirs createallsubdirs; Components: vst3
; CLAP -> C:\Program Files\Common Files\CLAP
Source: "{#Artefacts}\CLAP\Tube Channel.clap"; DestDir: "{commoncf64}\CLAP"; \
    Flags: ignoreversion; Components: clap
; LV2 bundle -> %APPDATA%\LV2 (per the LV2 spec on Windows)
Source: "{#Artefacts}\LV2\Tube Channel.lv2\*"; DestDir: "{userappdata}\LV2\Tube Channel.lv2"; \
    Flags: ignoreversion recursesubdirs createallsubdirs; Components: lv2
; Standalone app
Source: "{#Artefacts}\Standalone\Tube Channel.exe"; DestDir: "{app}"; \
    Flags: ignoreversion; Components: standalone
; User guide and license texts (always installed)
Source: "{#Stage}\licenses\Tube Channel\*"; DestDir: "{commonappdata}\Tube Channel"; \
    Flags: ignoreversion recursesubdirs createallsubdirs; Components: docs

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\Tube Channel.exe"; Components: standalone
Name: "{group}\User Guide"; Filename: "{commonappdata}\Tube Channel\Manual\USER_MANUAL.md"

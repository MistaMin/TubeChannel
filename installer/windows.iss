; Windows installer for Tube Channel: VST3, CLAP and LV2, one choice per format.
; Built by .github/workflows/build.yml:  iscc /DVersion=2.0.0 /DDist=<dist> /DRoot=<repo> installer\windows.iss
#ifndef Version
  #define Version "0.0.0"
#endif
#ifndef Dist
  #define Dist "..\dist-windows"
#endif
#ifndef Root
  #define Root ".."
#endif

[Setup]
AppId={{74066731-D5EB-4994-8932-8CC0CFCE562A}
AppName=Tube Channel
AppVersion={#Version}
AppPublisher=OpenGrid
AppPublisherURL=https://opengrid.studio
DefaultDirName={commonpf64}\OpenGrid\Tube Channel
DisableDirPage=yes
DisableProgramGroupPage=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
PrivilegesRequired=admin
LicenseFile={#Root}\BINARY_LICENSE.txt
OutputBaseFilename=Tube-Channel-{#Version}-windows-x64-setup
OutputDir={#Dist}\..
Compression=lzma2
SolidCompression=yes
UninstallDisplayName=Tube Channel {#Version}

[Types]
Name: "full"; Description: "VST3, CLAP and LV2"
Name: "custom"; Description: "Choose formats"; Flags: iscustom

[Components]
Name: "vst3"; Description: "VST3 plug-in"; Types: full
Name: "clap"; Description: "CLAP plug-in"; Types: full
Name: "lv2"; Description: "LV2 plug-in"; Types: full

[Files]
Source: "{#Dist}\Tube Channel.vst3\*"; DestDir: "{commoncf64}\VST3\Tube Channel.vst3"; Components: vst3; Flags: recursesubdirs ignoreversion
Source: "{#Dist}\Tube Channel.clap"; DestDir: "{commoncf64}\CLAP"; Components: clap; Flags: ignoreversion
Source: "{#Dist}\TubeChannel.lv2\*"; DestDir: "{commoncf64}\LV2\TubeChannel.lv2"; Components: lv2; Flags: recursesubdirs ignoreversion
Source: "{#Root}\LICENSE"; DestDir: "{app}\Licenses"; DestName: "LICENSE.txt"
Source: "{#Root}\BINARY_LICENSE.txt"; DestDir: "{app}\Licenses"
Source: "{#Root}\THIRD_PARTY_LICENSES.md"; DestDir: "{app}\Licenses"
Source: "{#Root}\Licenses\*"; DestDir: "{app}\Licenses"
Source: "{#Root}\docs\USER_MANUAL.md"; DestDir: "{app}"

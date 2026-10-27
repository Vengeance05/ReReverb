; Inno Setup script for the Windows installer (VST3, 64-bit).
; Built by CI:  iscc /DAppVersion=1.0.0 /DBuildDir=..\..\build installer\windows\rereverb.iss

#ifndef AppVersion
  #define AppVersion "1.0.0"
#endif
#ifndef BuildDir
  #define BuildDir "..\..\build"
#endif

[Setup]
AppId={{6D0B7E52-5C1E-4C3B-9F0A-7B1E2A9C4D11}
AppName=Rereverb (Septima + Vista Rooms)
AppVersion={#AppVersion}
AppPublisher=Rereverb
AppPublisherURL=https://vengeance.uk
DefaultDirName={commonpf64}\Rereverb
DisableDirPage=yes
DisableProgramGroupPage=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
OutputDir=..\..\dist
OutputBaseFilename=Rereverb-{#AppVersion}-Windows-Setup
Compression=lzma2
SolidCompression=yes
#if FileExists(AddBackslash(SourcePath) + "..\..\LICENSE")
LicenseFile=..\..\LICENSE
#endif
UninstallDisplayName=Rereverb (Septima + Vista Rooms)

[Types]
Name: "full"; Description: "Septima and Vista Rooms"
Name: "custom"; Description: "Choose plugins"; Flags: iscustom

[Components]
Name: "septima"; Description: "Septima (convolution reverb, VST3)"; Types: full custom
Name: "vista"; Description: "Vista Rooms (algorithmic reverb, VST3)"; Types: full custom

[Files]
Source: "{#BuildDir}\Septima_artefacts\Release\VST3\Septima.vst3\*"; DestDir: "{commoncf64}\VST3\Septima.vst3"; Components: septima; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#BuildDir}\VistaRooms_artefacts\Release\VST3\Vista Rooms.vst3\*"; DestDir: "{commoncf64}\VST3\Vista Rooms.vst3"; Components: vista; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "..\..\README.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\..\docs\*"; DestDir: "{app}\docs"; Flags: ignoreversion recursesubdirs

[Dirs]
; Default IR folder that Septima searches automatically. Users copy the
; downloaded impulse responses here, or choose any folder with "IR Library...".
Name: "{commonappdata}\Rereverb\Septima\IRs"; Components: septima; Permissions: users-modify

[Icons]
Name: "{commonappdata}\Rereverb\Septima\IRs\Get the free IR library"; Filename: "https://samplicity.com/downloads/"; Components: septima

[Run]
Filename: "https://samplicity.com/downloads/"; Description: "Open the IR library download page (needed for Septima)"; Flags: postinstall shellexec skipifsilent unchecked; Components: septima
Filename: "explorer.exe"; Parameters: """{commonappdata}\Rereverb\Septima\IRs"""; Description: "Open Septima's IR folder"; Flags: postinstall nowait skipifsilent unchecked; Components: septima

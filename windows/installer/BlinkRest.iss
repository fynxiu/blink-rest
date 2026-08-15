#ifndef MyAppVersion
  #error MyAppVersion must be defined
#endif
#ifndef SourceExe
  #error SourceExe must be defined
#endif
#ifndef SourceReadme
  #error SourceReadme must be defined
#endif
#ifndef OutputDir
  #define OutputDir "."
#endif

#define MyAppName "Blink Rest"
#define MyAppExeName "BlinkRest.exe"

[Setup]
AppId={{98DD3C4D-6EE7-42F9-8D66-4C62AF1D15A3}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher=Blink Rest
DefaultDirName={localappdata}\Programs\BlinkRest
DefaultGroupName=Blink Rest
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64os
ArchitecturesInstallIn64BitMode=x64os
OutputDir={#OutputDir}
OutputBaseFilename=BlinkRest-v{#MyAppVersion}-windows-x64-setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayIcon={app}\{#MyAppExeName}
CloseApplications=yes
RestartApplications=no
SetupLogging=yes

[Files]
Source: "{#SourceExe}"; DestDir: "{app}"; DestName: "{#MyAppExeName}"; Flags: ignoreversion
Source: "{#SourceReadme}"; DestDir: "{app}"; DestName: "README.md"; Flags: ignoreversion

[Icons]
Name: "{group}\Blink Rest"; Filename: "{app}\{#MyAppExeName}"

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "Launch Blink Rest"; Flags: nowait postinstall skipifsilent

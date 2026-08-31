; Inno Setup script for Film Lab.
; Build the deploy folder first (desktop/packaging/deploy.ps1), then compile this
; with Inno Setup 6:   ISCC.exe desktop\packaging\FilmLab.iss
; Output: desktop\out\dist\FilmLab-Setup.exe

#define AppName "Film Lab"
#ifndef AppVersion
  #define AppVersion "0.1.0-beta.1"
#endif
#define AppExe "FilmLab.exe"

[Setup]
; Keep this AppId stable across versions so upgrades replace cleanly.
AppId={{9E5C7A34-2F1B-4D6E-9C3A-7B8F0A1D2E4C}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher={#AppName}
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
UsePreviousAppDir=yes
CloseApplications=yes
CloseApplicationsFilter={#AppExe}
RestartApplications=no
Uninstallable=yes
CreateUninstallRegKey=yes
UninstallDisplayIcon={app}\{#AppExe}
OutputDir=..\out\dist
OutputBaseFilename=FilmLab-Setup
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible

[InstallDelete]
; The install directory contains only staged runtime files. Clear it during an
; upgrade so removed Qt plugins or engine DLLs cannot survive into the new build.
; User settings, logs, and presets are stored under %LOCALAPPDATA%, never here.
Type: filesandordirs; Name: "{app}\*"

[Files]
; The staged, self-contained deploy folder produced by deploy.ps1.
Source: "..\out\dist\FilmLab\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs ignoreversion
Source: "README-install.txt"; DestDir: "{app}"; Flags: isreadme

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#AppExe}"
Name: "{group}\{#AppName} — Lightroom setup"; Filename: "{app}\README-install.txt"
Name: "{group}\Uninstall {#AppName}"; Filename: "{uninstallexe}"

[Run]
Filename: "{app}\README-install.txt"; Description: "Show Lightroom setup steps"; \
    Flags: postinstall shellexec skipifsilent nowait

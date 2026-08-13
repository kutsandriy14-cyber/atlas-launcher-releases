; Atlas Launcher — portable-folder Inno Setup script
; Put this .iss file directly into the unpacked AtlasLauncher-win64 folder,
; then open it in Inno Setup and choose Build -> Compile.

#define MyAppName "Atlas Launcher"
#define MyAppVersion "0.2.7"
#define MyAppPublisher "Atlas"
#define MyAppExeName "AtlasLauncher.exe"

; The source files are in the same folder as this script.
#define SourceDir "."
; The generated installer is kept outside the installed file set.
#define InstallerOutputDir ".\AtlasLauncher-Setup-output"

[Setup]
AppId={{8F7B803A-6C18-486D-9120-5CFC89B808F3}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={userappdata}\atlaslauncher
DisableDirPage=no
UsePreviousAppDir=yes
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=no
OutputDir={#InstallerOutputDir}
OutputBaseFilename=AtlasLauncher-Setup-{#MyAppVersion}-x64
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=6.1sp1
UninstallDisplayIcon={app}\{#MyAppExeName}
VersionInfoVersion={#MyAppVersion}
VersionInfoCompany={#MyAppPublisher}
VersionInfoDescription=Atlas Launcher Setup
CloseApplications=yes
RestartApplications=no

[Languages]
Name: "russian"; MessagesFile: "compiler:Languages\Russian.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Создать ярлык на рабочем столе"; GroupDescription: "Ярлыки:"; Flags: unchecked

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Excludes: "AtlasLauncher-Setup-output\*"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; WorkingDir: "{app}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "Запустить {#MyAppName}"; Flags: nowait postinstall skipifsilent

[Code]
function InitializeSetup(): Boolean;
begin
  Result := True;
  if not IsWin64 then
  begin
    MsgBox('Atlas Launcher собирается как 64-битное приложение. Используйте 64-битную Windows.', mbError, MB_OK);
    Result := False;
  end;
end;

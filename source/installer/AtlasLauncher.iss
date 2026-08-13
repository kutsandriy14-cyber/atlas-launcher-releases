; Atlas Launcher — Inno Setup 6 script
; Установка и данные пользователя: %APPDATA%\atlaslauncher.
; Перед компиляцией установите ATLAS_DEPLOY_DIR на папку,
; содержащую AtlasLauncher.exe и файлы, подготовленные windeployqt.

#define MyAppName "Atlas Launcher"
#define MyAppVersion "0.3.1"
#define MyAppPublisher "Atlas"
#define MyAppExeName "AtlasLauncher.exe"

#ifndef SourceDir
  #define SourceDir "..\dist\AtlasLauncher-win64"
#endif

[Setup]
AppId={{8F7B803A-6C18-486D-9120-5CFC89B808F3}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
; Пользователь всегда видит страницу выбора папки первой ручной установки.
DefaultDirName={userappdata}\atlaslauncher
DisableDirPage=no
UsePreviousAppDir=yes

; Ярлык в меню «Пуск» создаётся по умолчанию; ярлык на рабочем столе можно выбрать.
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=no
OutputDir=..\dist\installer
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
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
; Основной ярлык в меню «Пуск» и необязательный ярлык на рабочем столе.
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

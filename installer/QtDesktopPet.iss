#define AppName "QtDesktopPet"
#ifndef AppVersion
#define AppVersion "0.1.0"
#endif
#ifndef SourceDir
#define SourceDir "..\dist\QtDesktopPet-0.1.0-windows-x64"
#endif
#ifndef OutputDir
#define OutputDir "..\dist\installer"
#endif

[Setup]
AppId={{B37D912A-7354-4D67-AF75-5EFA7E80D605}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher=Make
DefaultDirName={localappdata}\Programs\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
OutputDir={#OutputDir}
OutputBaseFilename=QtDesktopPet-{#AppVersion}-windows-x64-setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
SetupIconFile=..\resources\icons\app_icon.ico
UninstallDisplayIcon={app}\QtDesktopPet.exe

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Additional shortcuts:"; Flags: unchecked

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\QtDesktopPet"; Filename: "{app}\QtDesktopPet.exe"; WorkingDir: "{app}"
Name: "{group}\Uninstall QtDesktopPet"; Filename: "{uninstallexe}"
Name: "{userdesktop}\QtDesktopPet"; Filename: "{app}\QtDesktopPet.exe"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{app}\QtDesktopPet.exe"; Description: "Launch QtDesktopPet"; Flags: nowait postinstall skipifsilent

[Code]
var
  DeleteUserDataAfterUninstall: Boolean;

procedure DeleteDirIfExists(Path: String);
begin
  if DirExists(Path) then
  begin
    DelTree(Path, True, True, True);
  end;
end;

function InitializeUninstall(): Boolean;
var
  Answer: Integer;
begin
  Result := True;
  DeleteUserDataAfterUninstall := False;
  Answer := MsgBox(
    '是否同时删除 QtDesktopPet 的用户配置、导入角色、聊天记录和日志？' + #13#10 + #13#10 +
    '这会删除当前用户的 QtDesktopPet 数据，操作不可恢复。',
    mbConfirmation,
    MB_YESNO);

  if Answer = IDYES then
  begin
    DeleteUserDataAfterUninstall := True;
  end;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if (CurUninstallStep = usPostUninstall) and DeleteUserDataAfterUninstall then
  begin
    DeleteDirIfExists(ExpandConstant('{userappdata}\QtDesktopPet\QtDesktopPet'));
    DeleteDirIfExists(ExpandConstant('{localappdata}\QtDesktopPet\QtDesktopPet'));
  end;
end;

#define AppName "QtDesktopPet"
#ifndef AppVersion
#define AppVersion "0.3.1"
#endif
#ifndef SourceDir
#define SourceDir "..\dist\QtDesktopPet-0.3.1-windows-x64"
#endif
#ifndef OutputDir
#define OutputDir "..\dist\installer"
#endif

#if !FileExists(SourceDir + "\QtDesktopPetBridge.exe") || !FileExists(SourceDir + "\deployment-report.json")
  #error "Run tools/package_release.ps1 first: verified bridge and deployment report are required."
#endif
#if !FileExists(SourceDir + "\platforms\qwindows.dll") || !FileExists(SourceDir + "\tls\qschannelbackend.dll")
  #error "Qt platform/TLS plugins are missing from the release package."
#endif
#if !FileExists(SourceDir + "\examples\agent_bridge\dsh-cordis-plugin.js") || !FileExists(SourceDir + "\使用说明.md")
  #error "Offline user guide or DSH plugin is missing from the release package."
#endif

[Setup]
AppId={{B37D912A-7354-4D67-AF75-5EFA7E80D605}
AppName={#AppName}
AppVersion={#AppVersion}
VersionInfoVersion={#AppVersion}
AppPublisher=Make
DefaultDirName={localappdata}\Programs\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
OutputDir={#OutputDir}
OutputBaseFilename=QtDesktopPet-{#AppVersion}-windows-x64-setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
ShowLanguageDialog=no
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
SetupIconFile=..\resources\icons\app_icon.ico
UninstallDisplayIcon={app}\QtDesktopPet.exe

[Languages]
Name: "chinesesimplified"; MessagesFile: ".\languages\ChineseSimplified.isl"

[Tasks]
Name: "desktopicon"; Description: "创建桌面快捷方式"; GroupDescription: "附加快捷方式："; Flags: unchecked
Name: "startup"; Description: "开机自启动"; GroupDescription: "附加选项："; Flags: unchecked

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Registry]
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "{#AppName}"; ValueData: """{app}\QtDesktopPet.exe"""; Flags: uninsdeletevalue; Tasks: startup

[Icons]
Name: "{group}\QtDesktopPet"; Filename: "{app}\QtDesktopPet.exe"; WorkingDir: "{app}"
Name: "{group}\使用说明"; Filename: "{sys}\notepad.exe"; Parameters: """{app}\使用说明.md"""
Name: "{group}\卸载 QtDesktopPet"; Filename: "{uninstallexe}"
Name: "{userdesktop}\QtDesktopPet"; Filename: "{app}\QtDesktopPet.exe"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{app}\QtDesktopPet.exe"; Description: "启动 QtDesktopPet"; Flags: nowait postinstall skipifsilent

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

  if CurUninstallStep = usPostUninstall then
  begin
    RegDeleteValue(HKCU, 'Software\Microsoft\Windows\CurrentVersion\Run', '{#AppName}');
  end;
end;

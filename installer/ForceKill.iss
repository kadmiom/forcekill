; ForceKill installer (Inno Setup 6.3 or newer).
; The architecture directives use x64compatible, which replaced the deprecated
; x64 alias in 6.3 and also covers Arm64 Windows 11 running x64 via emulation.
;
; Build after compiling ForceKill.exe:
;   iscc /DBinDir=build\Release installer\ForceKill.iss
; The output lands in dist\. BinDir is relative to the repository root, or an
; absolute path; see BinSource below for why that needs adjusting.

#ifndef BinDir
  #define BinDir "build\Release"
#endif

; ISCC anchors relative Source paths at this file's directory, but BinDir is
; documented relative to the repository root, so step up one level first. A
; /DBinDir with a drive letter is absolute and is used as given.
#if Pos(":", BinDir) == 0
  #define BinSource "..\" + BinDir
#else
  #define BinSource BinDir
#endif

#define AppName "ForceKill"
#define AppVersion "1.0.0"
#define AppExeName "ForceKill.exe"
#define AppPublisher "ForceKill"

[Setup]
AppId={{3F6C2F3B-9B4E-4C1C-9D3A-7C2B5F0E8A41}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#AppPublisher}
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
UninstallDisplayIcon={app}\{#AppExeName}
OutputDir=..\dist
OutputBaseFilename={#AppName}Setup-{#AppVersion}
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
SetupIconFile=..\src\forcekill.ico
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.17763
CloseApplications=yes
RestartApplications=no
VersionInfoVersion={#AppVersion}.0
VersionInfoDescription={#AppName} setup

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "autostart"; Description: "Start {#AppName} when I sign in"; GroupDescription: "Startup:"; Flags: checkedonce

[Files]
Source: "{#BinSource}\{#AppExeName}"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\{#AppExeName}"

[Registry]
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; \
    ValueName: "{#AppName}"; ValueData: """{app}\{#AppExeName}"""; Flags: uninsdeletevalue; Tasks: autostart

[Run]
Filename: "{app}\{#AppExeName}"; Description: "Start {#AppName} now"; Flags: nowait postinstall skipifsilent

[UninstallRun]
Filename: "{sys}\taskkill.exe"; Parameters: "/IM {#AppExeName} /F"; Flags: runhidden; RunOnceId: "StopForceKill"

[Code]
var
  DeleteUserData: Boolean;

procedure StopRunningInstance;
var
  ResultCode: Integer;
begin
  Exec(ExpandConstant('{sys}\taskkill.exe'), '/IM {#AppExeName} /F', '', SW_HIDE,
       ewWaitUntilTerminated, ResultCode);
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  StopRunningInstance;
  Result := '';
end;

{ The [Registry] entry only cleans up its own value when the "autostart" task was
  selected at install time. A user can also switch it on later from the tray
  menu, so delete the value by name on every uninstall. Deleting a missing value
  just returns False, and this ignores the value's type, so no existence check is
  needed. }
procedure RemoveAutostartEntry;
begin
  RegDeleteValue(HKEY_CURRENT_USER,
                 'Software\Microsoft\Windows\CurrentVersion\Run',
                 '{#AppName}');
end;

procedure RemoveUserData;
begin
  DelTree(ExpandConstant('{localappdata}\ForceKill'), True, True, True);
end;

{ Asked before the progress window appears. "No" (keep the settings and log) is
  the default button, and silent uninstalls always keep them. }
function InitializeUninstall(): Boolean;
var
  DataDir: String;
begin
  Result := True;
  DeleteUserData := False;
  DataDir := ExpandConstant('{localappdata}\ForceKill');
  { If the app never ran there is nothing to delete, so don't ask about it. }
  if (not UninstallSilent) and DirExists(DataDir) then
    DeleteUserData := MsgBox('Also delete ForceKill''s settings and log file?' + #13#10#13#10 +
                             DataDir + #13#10#13#10 +
                             'Choose No to keep them in case you reinstall later.',
                             mbConfirmation, MB_YESNO or MB_DEFBUTTON2) = IDYES;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usUninstall then
  begin
    RemoveAutostartEntry;
    { Stop the app first: a running instance keeps the log file open, and the
      delete would fail with a sharing violation. }
    if DeleteUserData then
    begin
      StopRunningInstance;
      RemoveUserData;
    end;
  end;
end;

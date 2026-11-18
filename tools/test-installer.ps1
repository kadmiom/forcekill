# Installs and silently uninstalls the built ForceKill installer, then checks the two
# promises the .iss makes that compiling it cannot prove:
#
#   * the Run value is removed even when the tray toggle wrote it rather than the
#     installer's "autostart" task;
#   * %LOCALAPPDATA%\ForceKill (config.ini and the log) is kept, because a silent
#     uninstall always keeps user data.
#
# Every installer-side name - AppName, AppExeName, the AppId - is read from
# installer/ForceKill.iss, so renaming the app there cannot leave the checks quietly
# watching the old names. Only the app's own settings directory stays literal: it
# comes from src/settings.cpp, which the .iss repeats in [Code].
#
# Those checks need the real settings folder and the real HKCU Run value, so the
# script remembers the value and renames the folder to
# %LOCALAPPDATA%\<AppName>.installer-test-backup, then puts both back from a finally
# block - also when a check fails or you press Ctrl+C. Nothing is deleted: the real
# folder is only renamed inside %LOCALAPPDATA%, so a hard kill at worst leaves it
# under the backup name, which the next run reports.
#
# A test install shares the AppId of the real one, so on a machine where the app is
# installed it would take over that installation's uninstall entry and Start Menu
# shortcut. The script therefore refuses there unless -AllowInstalled is passed, and
# with that switch it saves and restores both.
#
# Run it from the repository root after building the installer:
#
#   pwsh -File tools\test-installer.ps1 -Setup dist\ForceKillSetup-1.0.0.exe
#
# Windows PowerShell 5.1 and PowerShell 7 both work. Exits non-zero if a check fails.

[CmdletBinding()]
param(
    # The installer to exercise. Required unless -ShowConfig is used.
    [string]$Setup = '',

    # A path that needs no elevation, so the result does not depend on how Setup
    # resolves install mode. Empty means %LOCALAPPDATA%\Programs\<AppName>CI; the
    # default cannot be written here because the name is only known after the .iss
    # has been read.
    [string]$InstallDir = '',

    # Run even where the app is installed. The uninstall entry and Start Menu shortcut
    # that the test install would take over are saved and put back, but you are on a
    # machine whose real installation is briefly replaced, so it is a deliberate
    # choice rather than the default.
    [switch]$AllowInstalled,

    # Print the names and paths read from installer/ForceKill.iss and exit without
    # installing, uninstalling or changing anything.
    [switch]$ShowConfig
)

$ErrorActionPreference = 'Stop'

$InstallArgs = '/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /SP-'
$UninstallArgs = '/VERYSILENT /SUPPRESSMSGBOXES /NORESTART'

$script:Failures = 0
$script:Saved = $false
$script:RunValueSaved = $false
$script:RunValueBefore = ''
$script:ArtifactsSaved = $false

# ---------------------------------------------------------------------------
# Names, read from the .iss
# ---------------------------------------------------------------------------

$IssPath = Join-Path $PSScriptRoot '..\installer\ForceKill.iss'
if (-not (Test-Path -LiteralPath $IssPath)) {
    throw "Cannot read $IssPath, so the names this test checks cannot be derived."
}
$Iss = Get-Content -LiteralPath $IssPath -Raw

function Get-IssDefine {
    param([string]$Name)

    $match = [regex]::Match($Iss, "#define\s+$Name\s+`"(?<value>[^`"]+)`"")
    if (-not $match.Success) {
        throw "No '#define $Name ""...""' line in $IssPath."
    }
    return $match.Groups['value'].Value
}

$AppName = Get-IssDefine 'AppName'
$AppExeName = Get-IssDefine 'AppExeName'

# AppId={{GUID}: the doubled brace is Inno's escape for a literal "{".
$appIdMatch = [regex]::Match($Iss, 'AppId\s*=\s*\{\{(?<id>[0-9A-Fa-f-]{36})\}')
if (-not $appIdMatch.Success) {
    throw "No AppId={{GUID} directive in $IssPath."
}
$AppId = $appIdMatch.Groups['id'].Value

# A per-user install registers under HKCU; an elevated one under HKLM.
$UninstallKeys = @(
    "HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\{$AppId}_is1",
    "HKLM:\Software\Microsoft\Windows\CurrentVersion\Uninstall\{$AppId}_is1"
)
$RunKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run'
$ValueName = $AppName
$ProcessName = [System.IO.Path]::GetFileNameWithoutExtension($AppExeName)
# DefaultDirName={autopf}\{#AppName}, which is %LOCALAPPDATA%\Programs for a
# PrivilegesRequired=lowest install.
$RealInstallDir = Join-Path $env:LOCALAPPDATA "Programs\$AppName"
# [Icons] Name: "{autoprograms}\{#AppName}".
$Shortcut = Join-Path $env:APPDATA "Microsoft\Windows\Start Menu\Programs\$AppName.lnk"
$ArtifactDir = Join-Path ([System.IO.Path]::GetTempPath()) "$AppName-installer-test"

# The app's own directory (src/settings.cpp), which the installer only repeats.
$SettingsDir = Join-Path $env:LOCALAPPDATA 'ForceKill'
$SavedDir = "$SettingsDir.installer-test-backup"

if ($InstallDir -eq '') {
    $InstallDir = Join-Path $env:LOCALAPPDATA "Programs\${AppName}CI"
}
$ExePath = Join-Path $InstallDir $AppExeName
$Uninstaller = Join-Path $InstallDir 'unins000.exe'

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

function Check {
    param([string]$What, [bool]$Condition, [string]$Detail)

    if ($Condition) {
        Write-Host "  pass  $What"
        return
    }
    Write-Host "  FAIL  $What" -ForegroundColor Red
    if ($Detail) {
        Write-Host "        $Detail" -ForegroundColor Red
    }
    $script:Failures++
}

function Format-Found {
    param([string]$Path)

    # A key this process cannot read is as good as absent for a status marker;
    # without -ErrorAction here, $ErrorActionPreference = 'Stop' would abort
    # -ShowConfig on an access-denied uninstall key instead of printing [absent].
    try {
        if (Test-Path -LiteralPath $Path -ErrorAction SilentlyContinue) {
            return 'found'
        }
    } catch {
        # Treated as absent below.
    }
    return 'absent'
}

# '' means "no value"; the autostart entry always holds a quoted exe path.
function Get-RunValue {
    $item = Get-ItemProperty -Path $RunKey -Name $ValueName -ErrorAction SilentlyContinue
    if ($null -eq $item) {
        return ''
    }
    return [string]$item.$ValueName
}

function Set-RunValue {
    param([string]$Value)

    New-ItemProperty -Path $RunKey -Name $ValueName -Value $Value -PropertyType String -Force |
        Out-Null
}

function Clear-RunValue {
    Remove-ItemProperty -Path $RunKey -Name $ValueName -ErrorAction SilentlyContinue
}

function Invoke-Setup {
    param([string]$Arguments, [string]$What)

    $process = Start-Process -FilePath $Setup -ArgumentList $Arguments -Wait -PassThru
    Check "$What finished with exit code 0" ($process.ExitCode -eq 0) `
        "exit code: $($process.ExitCode)"
}

function Wait-For {
    param([scriptblock]$Condition, [int]$TimeoutSeconds = 60)

    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    while ((Get-Date) -lt $deadline) {
        if (& $Condition) {
            return $true
        }
        Start-Sleep -Milliseconds 500
    }
    return [bool](& $Condition)
}

function Invoke-Uninstall {
    # The uninstaller copies itself to %TEMP% and hands over, so its exit code says
    # nothing; wait for the install directory itself to go away instead.
    Start-Process -FilePath $Uninstaller -ArgumentList $UninstallArgs -Wait | Out-Null
    Check 'the uninstaller finished' (Wait-For { -not (Test-Path -LiteralPath $InstallDir) }) `
        "still present: $InstallDir"
}

function Save-UserState {
    # Remember the Run value and rename the real settings folder aside. A rename in
    # the same folder cannot half-succeed the way copy-then-delete can.
    if (Test-Path -LiteralPath $SavedDir) {
        throw "A previous run left settings at $SavedDir. Move that folder back to $SettingsDir (or delete it if it is only test data), then run again."
    }

    $script:RunValueBefore = Get-RunValue
    $script:RunValueSaved = $true
    if ($script:RunValueBefore -ne '') {
        Write-Host "  saved the existing Run value: $($script:RunValueBefore)"
    }

    if (-not (Test-Path -LiteralPath $SettingsDir)) {
        Write-Host "  there is no $SettingsDir yet"
        return
    }

    Move-Item -LiteralPath $SettingsDir -Destination $SavedDir
    $script:Saved = $true
    Write-Host "  moved $SettingsDir to $SavedDir"
}

function Save-InstallState {
    # Only with -AllowInstalled, and only what a per-user test install would replace:
    # the HKCU uninstall entry and the Start Menu shortcut. An elevated installation
    # keeps its HKLM entry, because the test never writes there.
    if (-not $AllowInstalled) {
        return
    }

    # Parenthesised: in argument position PowerShell would pass the whole array
    # followed by the literal text "[0]".
    $hasEntry = Test-Path -LiteralPath ($UninstallKeys[0])
    $hasShortcut = Test-Path -LiteralPath $Shortcut
    if (-not ($hasEntry -or $hasShortcut)) {
        return
    }

    New-Item -ItemType Directory -Path $ArtifactDir | Out-Null

    # Nothing has been touched yet, so a failure here just drops the partial copy and
    # leaves the real entries alone.
    try {
        if ($hasEntry) {
            # reg.exe rather than the registry provider, so value types and the key's
            # own contents survive the round trip exactly.
            $regFile = Join-Path $ArtifactDir 'uninstall.reg'
            & reg.exe export ($UninstallKeys[0] -replace '^HKCU:', 'HKCU') $regFile /y | Out-Null
            if ($LASTEXITCODE -ne 0) {
                throw "reg export of $($UninstallKeys[0]) failed with exit code $LASTEXITCODE."
            }
            if ((Get-Item -LiteralPath $regFile).Length -eq 0) {
                throw 'reg export produced an empty file.'
            }
        }
        if ($hasShortcut) {
            Copy-Item -LiteralPath $Shortcut -Destination (Join-Path $ArtifactDir "$AppName.lnk")
        }
    } catch {
        Remove-Item -LiteralPath $ArtifactDir -Recurse -Force -ErrorAction SilentlyContinue
        throw
    }

    $script:ArtifactsSaved = $true
    Write-Host "  saved the existing install state in $ArtifactDir"
}

function Restore-InstallState {
    if (-not $script:ArtifactsSaved) {
        return
    }

    # Each entry is its own step, so a failing reg import cannot also cost you the
    # shortcut. The folder is only removed once both are back.
    $restored = $true

    $regFile = Join-Path $ArtifactDir 'uninstall.reg'
    if (Test-Path -LiteralPath $regFile) {
        try {
            & reg.exe import $regFile | Out-Null
            if ($LASTEXITCODE -ne 0) {
                throw "reg import failed with exit code $LASTEXITCODE."
            }
        } catch {
            Write-Host "  could not put the uninstall entry back: $($_.Exception.Message)" -ForegroundColor Red
            $restored = $false
        }
    }

    $savedShortcut = Join-Path $ArtifactDir "$AppName.lnk"
    if (Test-Path -LiteralPath $savedShortcut) {
        try {
            Copy-Item -LiteralPath $savedShortcut -Destination $Shortcut -Force
        } catch {
            Write-Host "  could not put the Start Menu shortcut back: $($_.Exception.Message)" -ForegroundColor Red
            $restored = $false
        }
    }

    if (-not $restored) {
        Write-Host "  the saved install state is still in $ArtifactDir" -ForegroundColor Red
        $script:Failures++
        return
    }

    try {
        Remove-Item -LiteralPath $ArtifactDir -Recurse -Force
        $script:ArtifactsSaved = $false
        Write-Host '  put the existing install state back'
    } catch {
        Write-Host "  put the entries back but could not remove $ArtifactDir; delete it before the next run" -ForegroundColor Red
    }
}

function Restore-UserState {
    # Called from finally. Each step is independent, so one failure cannot skip the
    # others.
    try {
        if (Test-Path -LiteralPath $SettingsDir) {
            Remove-Item -LiteralPath $SettingsDir -Recurse -Force    # the test fixture
        }
    } catch {
        Write-Host "  could not delete the test fixture: $($_.Exception.Message)" -ForegroundColor Red
    }

    if ($script:Saved) {
        # Never move the folder into a slot that is still occupied: a backup renamed
        # into the fixture would be worse than a backup left in place.
        if ((Test-Path -LiteralPath $SavedDir) -and -not (Test-Path -LiteralPath $SettingsDir)) {
            try {
                Move-Item -LiteralPath $SavedDir -Destination $SettingsDir
                $script:Saved = $false
                Write-Host "  moved your settings back to $SettingsDir"
            } catch {
                Write-Host "  could not move your settings back: $($_.Exception.Message)" -ForegroundColor Red
            }
        }
        if ($script:Saved) {
            Write-Host "  YOUR SETTINGS ARE AT $SavedDir - move that folder back to $SettingsDir" -ForegroundColor Red
            $script:Failures++
        }
    }

    if ($script:RunValueSaved) {
        try {
            if ($script:RunValueBefore -ne '') {
                Set-RunValue $script:RunValueBefore
            } else {
                Clear-RunValue
            }
        } catch {
            Write-Host "  could not restore the Run value: $($_.Exception.Message)" -ForegroundColor Red
            $script:Failures++
        }
    }
}

function New-UserData {
    # What the app leaves behind after one run.
    New-Item -ItemType Directory -Path $SettingsDir -Force | Out-Null
    Set-Content -LiteralPath (Join-Path $SettingsDir 'config.ini') -NoNewline `
        -Value "[hotkey]`r`nctrl=1`r`nalt=1`r`nshift=0`r`nwin=0`r`nkey=F4`r`n[safety]`r`nprotected=explorer.exe;dwm.exe`r`n"
    Set-Content -LiteralPath (Join-Path $SettingsDir 'forcekill.log') -NoNewline `
        -Value 'ForceKill starting (elevated=no).'
}

# ---------------------------------------------------------------------------
# -ShowConfig: report what was derived, then stop
# ---------------------------------------------------------------------------

if ($ShowConfig) {
    Write-Host "Derived from $IssPath"
    Write-Host ('  {0,-18} {1}' -f 'AppName', $AppName)
    Write-Host ('  {0,-18} {1}' -f 'AppExeName', $AppExeName)
    Write-Host ('  {0,-18} {1}' -f 'AppId', $AppId)
    Write-Host ('  {0,-18} {1}' -f 'Run value name', $ValueName)
    Write-Host ('  {0,-18} {1} [{2}]' -f 'Install path', $RealInstallDir, (Format-Found $RealInstallDir))
    Write-Host ('  {0,-18} {1} [{2}]' -f 'Start Menu link', $Shortcut, (Format-Found $Shortcut))
    foreach ($key in $UninstallKeys) {
        Write-Host ('  {0,-18} {1} [{2}]' -f 'Uninstall entry', $key, (Format-Found $key))
    }

    $runNow = Get-RunValue
    if ($runNow -eq '') {
        $runNow = "$ValueName = (absent)"
    } else {
        $runNow = "$ValueName = $runNow"
    }
    Write-Host ('  {0,-18} {1}' -f 'Run value', $runNow)

    Write-Host ''
    Write-Host 'Test paths (read only):'
    Write-Host ('  {0,-18} {1} [{2}]' -f 'Scratch install', $InstallDir, (Format-Found $InstallDir))
    Write-Host ('  {0,-18} {1} [{2}]' -f 'Settings folder', $SettingsDir, (Format-Found $SettingsDir))
    Write-Host ('  {0,-18} {1} [{2}]' -f 'Settings backup', $SavedDir, (Format-Found $SavedDir))
    Write-Host ('  {0,-18} {1} [{2}]' -f 'Test artifacts', $ArtifactDir, (Format-Found $ArtifactDir))

    Write-Host ''
    Write-Host 'Nothing was installed, uninstalled or changed.'
    exit 0
}

# ---------------------------------------------------------------------------
# Pre-flight: everything here must pass before any state is touched
# ---------------------------------------------------------------------------

if ($Setup -eq '') {
    throw 'Pass -Setup <path to the built ForceKillSetup-*.exe>, or -ShowConfig to just print the derived names.'
}
if (-not (Test-Path -LiteralPath $Setup)) {
    throw "Installer not found: $Setup"
}
$Setup = (Resolve-Path -LiteralPath $Setup).Path
$SetupArgs = "$InstallArgs /DIR=`"$InstallDir`""

# The test deletes this directory before and after it runs.
if ((Split-Path -Leaf $InstallDir) -ieq $AppName) {
    throw "Refusing -InstallDir $InstallDir: it looks like a real installation, and this test deletes it."
}

if ($AllowInstalled -and (Test-Path -LiteralPath $ArtifactDir)) {
    throw "A previous run left saved install state in $ArtifactDir. Put it back or delete the folder, then run again."
}

$installedAt = @()
if (Test-Path -LiteralPath $RealInstallDir) {
    $installedAt += $RealInstallDir
}
foreach ($key in $UninstallKeys) {
    if (Test-Path -LiteralPath $key) {
        $installedAt += $key
    }
}
if (($installedAt.Count -gt 0) -and -not $AllowInstalled) {
    throw "$AppName looks installed here ($($installedAt -join '; ')), and a test install would take over that installation's uninstall entry and Start Menu shortcut. Pass -AllowInstalled to run anyway: it saves and restores both."
}

$running = @(Get-Process -Name $ProcessName -ErrorAction SilentlyContinue)
if ($running.Count -gt 0) {
    $pids = ($running | ForEach-Object { $_.Id }) -join ', '
    throw "$AppExeName is running (PID $pids); quit it from the tray menu first, because the uninstaller terminates it."
}

# ---------------------------------------------------------------------------
# The two rounds
# ---------------------------------------------------------------------------

Write-Host '== Saving the real user data =='

try {
    Save-UserState
    Save-InstallState

    Clear-RunValue
    if (Test-Path -LiteralPath $InstallDir) {
        Remove-Item -LiteralPath $InstallDir -Recurse -Force
    }

    Write-Host '== Round 1: autostart switched on from the tray =='
    Invoke-Setup "$SetupArgs /MERGETASKS=`"!autostart`"" 'install with the autostart task unchecked'
    Check 'the uninstaller was installed' (Test-Path -LiteralPath $Uninstaller) "missing: $Uninstaller"
    Check 'no Run value while the autostart task is unchecked' ((Get-RunValue) -eq '') `
        "value: $(Get-RunValue)"

    # The tray toggle writes the Run value itself; uninsdeletevalue cannot cover it.
    Set-RunValue "`"$ExePath`""
    New-UserData
    $configBefore = Get-Content -LiteralPath (Join-Path $SettingsDir 'config.ini') -Raw

    Invoke-Uninstall
    Check 'the tray-written Run value was removed' ((Get-RunValue) -eq '') "value: $(Get-RunValue)"
    Check 'the settings folder was kept' `
        (Test-Path -LiteralPath (Join-Path $SettingsDir 'config.ini')) "missing: $SettingsDir\config.ini"
    Check 'config.ini was kept unchanged' `
        ((Get-Content -LiteralPath (Join-Path $SettingsDir 'config.ini') -Raw) -eq $configBefore) `
        'the file is missing or its contents changed'
    Check 'forcekill.log was kept' (Test-Path -LiteralPath (Join-Path $SettingsDir 'forcekill.log')) `
        'the log file was deleted'

    Write-Host '== Round 2: autostart selected in the installer =='
    Clear-RunValue
    Invoke-Setup "$SetupArgs /TASKS=`"autostart`"" 'install with the autostart task'
    Check 'the Run value points at the installed exe' ((Get-RunValue) -eq "`"$ExePath`"") `
        "value: $(Get-RunValue)"
    Check 'the settings folder survived the reinstall' `
        (Test-Path -LiteralPath (Join-Path $SettingsDir 'config.ini')) 'config.ini is missing'

    Invoke-Uninstall
    Check 'the installer-written Run value was removed' ((Get-RunValue) -eq '') `
        "value: $(Get-RunValue)"
    Check 'the settings folder was kept again' `
        (Test-Path -LiteralPath (Join-Path $SettingsDir 'config.ini')) 'config.ini is missing'
} finally {
    Restore-InstallState
    Restore-UserState
    if (Test-Path -LiteralPath $InstallDir) {
        Remove-Item -LiteralPath $InstallDir -Recurse -Force -ErrorAction SilentlyContinue
    }
}

Write-Host ''
if ($script:Failures -ne 0) {
    Write-Host "$($script:Failures) installer check(s) failed." -ForegroundColor Red
    exit 1
}
Write-Host 'All installer checks passed; the original settings and Run value are back.'

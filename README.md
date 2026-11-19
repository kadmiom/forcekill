# ForceKill

[![build](https://github.com/kadmiom/forcekill/actions/workflows/build.yml/badge.svg)](https://github.com/kadmiom/forcekill/actions/workflows/build.yml)
[![license: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![platform: Windows 10/11 x64](https://img.shields.io/badge/platform-Windows%2010%2F11%20x64-0078d4.svg)](README.md)

A tiny Windows 10/11 tray utility: press a hotkey and the foreground window's
process is terminated, even when the app has stopped responding. Default hotkey
is **Ctrl+Alt+F4**; it is fully configurable.

ForceKill is a single native Win32 executable with no runtime dependencies
(no .NET, no Windhawk, no installer framework DLLs). The installer is one
`ForceKillSetup-1.0.1.exe`.

The app icon is generated rather than committed: run `python tools/make_icon.py`
once in a fresh checkout before the first build, or let CI do it for you.

## Install

1. Open [Releases](../../releases) and download `ForceKillSetup-1.0.1.exe` from
the latest release's assets (the "Source code" archives are not the app).
2. Run it. The build is unsigned, so SmartScreen shows "Windows protected your
PC" - click **More info**, then **Run anyway**.
3. The installer is per-user: no admin prompt and nothing system-wide. It copies
`ForceKill.exe` to `%LOCALAPPDATA%\Programs\ForceKill`, adds a Start Menu
shortcut, and offers "Start ForceKill when I sign in". Your settings and log
live in `%LOCALAPPDATA%\ForceKill`.
4. ForceKill starts in the notification area. Press **Ctrl+Alt+F4**, or
right-click the icon.

To remove it, use Settings > Apps > Installed apps > ForceKill. The uninstaller
asks whether to delete `%LOCALAPPDATA%\ForceKill`; **No** (the default) keeps
your hotkey, protected-apps list and log for a later reinstall, **Yes** deletes
them.

## Features

- Configurable hotkey: click the box, press the combination. Modifiers and key
  are stored in `%LOCALAPPDATA%\ForceKill\config.ini`.
- **Protected apps**: a list of exe names or full paths ForceKill refuses to
  kill. Seeded with `explorer.exe` and `dwm.exe`.
- Tray feedback: every attempt reports what happened ("Terminated
  notepad.exe (PID 1234)", "Access denied - try Restart as administrator").
- Child processes of the target are terminated too.
- Optional **Start the app again right after killing it**, ticked in Settings or
  as **Start apps again after killing them** in the tray menu: the terminated
  program is launched again in its own folder, without the arguments it was
  originally started with. Useful for an app you want restarted rather than
  left closed.
- Optional **Start with Windows** toggle in the tray menu: writes the per-user
  Run key for the running exe, so the standalone binary autostarts without the
  installer. The menu item is checked while that entry is active.
- No prompt while killing: the hotkey acts immediately.

## Safety guards

These are always enforced, regardless of settings:

- The desktop/shell window and the desktop itself are never targets.
- PID 0 and PID 4 (kernel) are refused.
- ForceKill never terminates itself.
- Windows system processes (`csrss`, `wininit`, `winlogon`, `services`,
  `lsass`, `smss`, `lsaiso`, `fontdrvhost`, `memcompression`, `System`,
  `Registry`) are refused.
- Any process Windows reports as critical (`IsProcessCritical`) is refused.
- Processes matching the protected list are refused.
- A process running at a higher integrity level cannot be killed by a
  non-elevated ForceKill; the result is reported as "access denied" and the
  tray menu offers **Restart as administrator**.

## Usage

1. ForceKill starts in the notification area after installing.
2. Press **Ctrl+Alt+F4** to kill the active window's process.
3. Right-click the tray icon for Settings, the protected-apps list, the log
   file, pause, **Start with Windows**, **Start apps again after killing
   them**, restart-as-admin and exit. Double-click opens Settings.

The log is `%LOCALAPPDATA%\ForceKill\forcekill.log` (rotated at 1 MB).

## Building

### Visual Studio 2022 or newer (recommended on Windows)

```bat
cmake -S . -B build -A x64
cmake --build build --config Release
```

Leaving out `-G` lets CMake pick the newest Visual Studio it finds, which is what
the CI does because its runner image ships Visual Studio 2026. To pin an older
install, add `-G "Visual Studio 17 2022"`.

### MinGW-w64 cross compile from Linux

```sh
cmake -S . -B build-mingw -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-toolchain.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build-mingw
```

The binary is `build-mingw/ForceKill.exe`, statically linked against the MinGW
runtime. If your toolchain is not on `PATH`, set `MINGW_PREFIX=/path/to/mingw64`.

### Installer

Inno Setup 6.3 or newer is required (the CI installs 6.7 via Chocolatey). The
architecture directives use `x64compatible`, which replaced the deprecated
`x64` alias in 6.3:

```bat
iscc /DBinDir=build\Release installer\ForceKill.iss
```

Output: `dist\ForceKillSetup-1.0.1.exe`. `BinDir` may also be set through the
`#define` at the top of `installer/ForceKill.iss`.

### CI

`.github/workflows/build.yml` builds the exe and the installer on
`windows-latest` and attaches both to a GitHub Release for tags matching `v*`.
Push a tag to publish:

```sh
git tag v1.0.1 && git push origin v1.0.1
```

#### Releasing

Pushing any new `v*` tag is the whole release: the workflow rebuilds both
`ForceKill.exe` and the installer, then creates or updates the GitHub Release for
that tag with both files attached. Bump `AppVersion` in `installer/ForceKill.iss`
and the version in `CMakeLists.txt` before tagging, so the installer asset is
named after the new version.

The `installer-smoke` job goes further than compiling: it builds the installer,
then runs `tools/test-installer.ps1`, which silently installs and uninstalls it
twice and asserts that the `HKCU` Run value disappears both when the installer's
"Start when I sign in" task wrote it and when the tray toggle did, and that
`%LOCALAPPDATA%\ForceKill` survives a silent uninstall. Reproduce it locally on
Windows after building the installer, with ForceKill itself quit from the tray (the
uninstaller would kill a running copy):

```powershell
pwsh -File tools\test-installer.ps1 -Setup dist\ForceKillSetup-1.0.1.exe
```

To see what the script derives from `installer/ForceKill.iss` before running any of
it, pass `-ShowConfig`: it prints the app name, exe name, `AppId`, Run value name,
install path, Start Menu shortcut and uninstall entry, marks which of them exist
right now, and exits without installing, uninstalling or changing anything.

```powershell
pwsh -File tools\test-installer.ps1 -ShowConfig
```

Because the checks have to use the real `%LOCALAPPDATA%\ForceKill` and the real
`HKCU` Run value, the script remembers the Run value and renames the folder to
`%LOCALAPPDATA%\ForceKill.installer-test-backup`, then puts both back when it
finishes - also when a check fails or you press Ctrl+C. Nothing is deleted: the real
folder is only renamed, so the worst a crash can do is leave it under the backup
name, where the next run reports it. If a restore fails anyway, the script prints
the path and exits non-zero. `AppName`, `AppExeName` and the `AppId` are all read
from `installer/ForceKill.iss`, so the Run value, Start Menu shortcut, install path
and uninstall entry it inspects follow a rename there instead of quietly watching
the old names; only the app's own settings directory stays literal, because that
name comes from `src/settings.cpp`. It refuses to run while ForceKill is running,
and on a machine where ForceKill is installed, since a test install shares that
installation's `AppId` and would take over its uninstall entry and Start Menu
shortcut. Where you want to smoke-test anyway, add `-AllowInstalled`: the script
then saves the uninstall entry and the shortcut and puts them back afterwards.

#### Running the installer test from the GitHub UI

The workflow also has a `workflow_dispatch` trigger, and a manual run defaults to the
installer smoke test alone, so nothing has to be pushed or tagged to exercise the
script on `windows-latest`:

1. Open the repository on GitHub and click **Actions**.
2. Click **build** in the left sidebar.
3. Click **Run workflow** on the right, keep the branch, leave the job selector on
   `installer-smoke` (pick `everything` to run the full build and installer jobs as
   well), then click the green **Run workflow** button.
4. Refresh the run list, open the new run, and click the **installer smoke test** job
   on the left. Its steps are *Configure*, *Build*, *Install Inno Setup*,
   *Compile installer* and *Install, uninstall and assert*.

The last step prints both rounds and fourteen `pass` lines, then a summary:

```
== Saving the real user data ==
  there is no C:\Users\runneradmin\AppData\Local\ForceKill yet
== Round 1: autostart switched on from the tray ==
  pass  install with the autostart task unchecked finished with exit code 0
  pass  the uninstaller was installed
  pass  no Run value while the autostart task is unchecked
  pass  the uninstaller finished
  pass  the tray-written Run value was removed
  pass  the settings folder was kept
  pass  config.ini was kept unchanged
  pass  forcekill.log was kept
== Round 2: autostart selected in the installer ==
  pass  install with the autostart task finished with exit code 0
  pass  the Run value points at the installed exe
  pass  the settings folder survived the reinstall
  pass  the uninstaller finished
  pass  the installer-written Run value was removed
  pass  the settings folder was kept again

All installer checks passed; the original settings and Run value are back.
```

A red `FAIL` line names the check that broke and the step exits 1, so the run goes
red and the summary reads `<n> installer check(s) failed.`; **Re-run all jobs** on
the run page tries again. The runner is a fresh VM each time, so a manual run never
needs `-AllowInstalled` - nothing is installed there to clash with.

## Notes

- The executable and installer are unsigned. Windows SmartScreen will show an
  "unrecognised app" prompt until they are signed with a code-signing
  certificate; that is a certificate purchase, not a code change.
- The icon is generated by `tools/make_icon.py` (standard library only) and is
  not committed: the build and CI create `src/forcekill.ico` before compiling, so
  run `python tools/make_icon.py` once in a fresh checkout.
- Configuration lives per user. Autostart is available two ways: the
  installer's "Start when I sign in" option, or the tray menu's **Start with
  Windows** toggle for an exe you copied somewhere yourself. Both use the same
  `ForceKill` value in `HKCU\Software\Microsoft\Windows\CurrentVersion\Run`,
  and the toggle is checked only when that value points at the running exe - so
  moving or renaming the exe turns the check mark off rather than lying.
- Uninstalling removes that Run value whether autostart was enabled by the
  installer or from the tray menu, so no stale entry is left pointing at a
  deleted exe. If `%LOCALAPPDATA%\ForceKill` exists, the uninstaller also asks
  whether to delete it (your `config.ini` and `forcekill.log`). Keeping
  them is the default answer, and a silent uninstall always keeps them, so a
  reinstall picks up your hotkey and protected-apps list again.

## License

MIT. See `LICENSE` and `NOTICE` for attribution to the two prior utilities
that inspired this one.

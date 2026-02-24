# Windows Setup and Launch

## Requirements

- Windows 10/11
- PowerShell 5.1+ or PowerShell 7+
- CMake 3.28+
- Ninja (for `ninja-*` presets)
- C++ compiler (Visual Studio Community/Build Tools with `Desktop development with C++`)
- SQLite3 development files available to CMake

Optional:
- PortAudio (`portaudio-2.0`) for real microphone input
- RtMidi (`rtmidi`) for server-side MIDI backend

## Quick Start

Use this exact flow:

1. Open **x64 Native Tools Command Prompt for VS** (Start menu).
2. Run `powershell`.
3. Run:

```bat
.\scripts\run_tuxdmx_windows.cmd
```

This guarantees `cl.exe` is the x64 toolchain (`Hostx64\x64`), which is required by the launcher.

The launcher:
- starts the PowerShell script with per-process execution policy bypass (no permanent policy change)
- checks required tools and versions
- validates you are in an x64 VS compiler environment
- auto-detects `vcpkg` (`C:\vcpkg` or `vcpkg.exe` on PATH) and sets `VCPKG_ROOT`/`CMAKE_TOOLCHAIN_FILE` when possible
- checks that `sqlite3` is installed for the active vcpkg triplet
- prints install guidance for missing dependencies
- configures/builds with CMake presets
- starts server and waits for `/api/state`
- opens browser (unless disabled)

## Multiple DMX Devices

If more than one compatible DMX interface is connected:
- open the web UI `Connection` card
- click `Rescan` to refresh detected devices
- choose `Auto Select` or a specific device and click `Use`

The preferred device selection is persisted in SQLite and restored on next startup.

## Launcher Options

```text
-Bind <ip>                default: 0.0.0.0
-Port <port>              default: 18181
-ConfigurePreset <name>   default: ninja-debug
-BuildPreset <name>       default: build-debug
-TestPreset <name>        default: test-debug
-RunTests                 run tests before launch
-DbPath <path>            default: .\data\tuxdmx.sqlite
-WebRoot <path>           default: .\web
-LogFile <path>           default: .\data\tuxdmx.log
-ReadyTimeoutSec <sec>    default: 30
-NoOpen                   do not auto-open browser
```

Example:

```bat
.\scripts\run_tuxdmx_windows.cmd -RunTests -Port 8090 -Bind 0.0.0.0 -NoOpen
```

## Direct PowerShell Launch (Advanced)

If you want to run the `.ps1` directly:

```powershell
.\scripts\run_tuxdmx_windows.ps1
```

If direct `.ps1` execution is blocked, either use the `.cmd` launcher above, or run:

```powershell
Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass
```

## Common Configure Failure: SQLite3 Not Found

If vcpkg/sqlite3 is missing:

```powershell
git clone https://github.com/microsoft/vcpkg C:\vcpkg
C:\vcpkg\bootstrap-vcpkg.bat
C:\vcpkg\vcpkg.exe install sqlite3:x64-windows
setx CMAKE_TOOLCHAIN_FILE C:\vcpkg\scripts\buildsystems\vcpkg.cmake
```

Then close all terminals, open **x64 Native Tools Command Prompt for VS**, run `powershell`, and run the launcher again.

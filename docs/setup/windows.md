# Windows Setup and Launch

## Requirements

- Windows 10/11
- PowerShell 5.1+ or PowerShell 7+
- CMake 3.28+
- Ninja (for `ninja-*` presets)
- C++ compiler (recommended: Visual Studio 2022 Build Tools, Desktop C++)
- SQLite3 development files available to CMake

Optional:
- PortAudio (`portaudio-2.0`) for real microphone input
- RtMidi (`rtmidi`) for server-side MIDI backend

## Quick Start

```powershell
.\scripts\run_tuxdmx_windows.ps1
```

The script:
- checks required tools and versions
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
-Port <port>              default: 8080
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

```powershell
.\scripts\run_tuxdmx_windows.ps1 -RunTests -Port 8090 -Bind 0.0.0.0 -NoOpen
```

## If Script Execution Is Blocked

```powershell
Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass
```

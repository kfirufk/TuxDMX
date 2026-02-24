# macOS Setup and Launch

## Requirements

- CMake 3.28+
- C++23-capable compiler (Apple Clang from Xcode Command Line Tools)
- Ninja
- SQLite3 development package

Optional:
- PortAudio (`portaudio-2.0`) for real microphone input
- RtMidi (`rtmidi`) for server-side MIDI backend
- If RtMidi is not available, CoreMIDI fallback is used automatically

## Quick Start

```bash
./scripts/run_tuxdmx_mac.sh
```

The script:
- configures/builds with CMake presets
- starts server and waits for `/api/state`
- opens browser (unless disabled)

## Multiple DMX Devices

If more than one compatible DMX interface is connected:
- open the web UI `Connection` card
- click `Rescan` to refresh detected devices
- choose `Auto Select` or a specific device and click `Use`

The preferred device selection is persisted in SQLite and restored on next startup.

## USB Stability Monitoring (macOS)

When disconnects happen, check the `Connection` card and transport line in the UI:
- `lastErrorKind` and `lastErrorHint` are now reported
- `possible usb power/hub issue` is flagged for likely brownout/reset patterns

You can also watch runtime logs:

```bash
tail -f ./data/tuxdmx.log
```

Useful quick checks while reproducing:

```bash
ls /dev/cu.usbserial* /dev/tty.usbserial* 2>/dev/null
```

```bash
ioreg -p IOUSB -l | rg -i "enttec|ftdi|usb"
```

If `/dev/cu.usbserial*` disappears and reappears during drops, that is usually a USB path reset (cable/hub/power), not an app-level DMX logic issue.

## Launcher Options

```text
--bind <ip>              default: 0.0.0.0
--port <port>            default: 18181
--configure-preset <p>   default: ninja-debug
--build-preset <p>       default: build-debug
--db <path>              default: ./data/tuxdmx.sqlite
--web-root <path>        default: ./web
--no-open                do not auto-open browser
```

Example:

```bash
./scripts/run_tuxdmx_mac.sh --port 8090 --bind 0.0.0.0 --no-open
```

## Package Hint (Homebrew)

```bash
brew install cmake ninja sqlite
```

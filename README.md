# TuxDMX

Cross-platform C++23 DMX control server for **ENTTEC/DMX USB Pro-compatible interfaces** with a responsive web UI for desktop/tablet/phone.

The app follows a server/client model: the DMX engine runs on one host machine, and any device on the same network can open the web UI and control lights through that server.

## Project Status

**Experimental (active development).**

This project is usable, but not production-stable yet. Active work is ongoing, including fixture-template tuning and intermittent device stability investigations on some hardware/cabling combinations.

## Why This Project Exists

TuxDMX started as a personal project to build a **simpler, faster-to-use DMX workflow** than many available free and paid tools. The focus is practical live control: patch fixtures quickly, define custom fixture templates (including unknown fixtures), save/recall scenes, and run music-reactive looks without navigating overly complex interfaces.

Initial development and testing were centered on a **NETTEC/ENTTEC DMX USB Pro-style interface**, with emphasis on reducing setup friction for small and medium live sessions.

The Learn/Template workflow is designed so you can test channels live while building a fixture profile. This is useful for low-cost fixtures, undocumented fixtures, or fixtures where the manual/channel table is missing.

## Maintainer

- **Kfir Ozer**  
  Email: [ufkfir@icloud.com](mailto:ufkfir@icloud.com)

## Implemented Features

- C++23 backend with CMake + presets.
- SQLite persistence for:
  - fixture templates
  - channel definitions and value ranges
  - fixture patching (universe/start/channel count)
  - live channel values
  - fixture groups
  - scenes (saved lighting looks)
  - MIDI mappings + MIDI input mode
- Startup seed templates:
  - **AliExpress 60x3W RGB PAR**
  - **Mira Dye** (A-mode 13ch layout)
  - automatic one-time migration: if an older `Mira Dye` layout is detected, it is preserved as **Mira Dye (D Mode Legacy)** and `Mira Dye` is updated to A-mode
- ENTTEC DMX USB Pro discovery and status reporting:
  - serial-port scan
  - serial probe (`0x7E 0x0A 0x00 0x00 0xE7`)
  - firmware and serial surfaced in UI
- Multi-universe engine model:
  - stores/updates multiple universes
  - routes selected output universe to DMX USB Pro
  - explicit create-universe API/UI for quick setup
- Live control UI:
  - fixture cards with per-channel sliders (default)
  - optional knob mode toggle in toolbar
  - server-side MIDI learn/clear per live control node (fixture channels, group controls, scene recall, reactive toggle)
  - performance hold buttons with smooth attack/release:
    - all-on lift
    - blackout
    - rotate for pan/tilt fixtures
    - strobe hit
  - one-click panic blackout button (forces zero DMX and disables music-reactive mode)
  - per-effect intensity controls (all-on / blackout / rotate / strobe)
  - adjustable fade seconds (decimal support)
  - value-range chips
  - icon mapping for channel types and mode/range labels
  - fast blank-channel generator for unknown fixtures (manual missing workflow)
  - clean view menu (Live / Patch / Templates / Groups / Learn)
  - scenes panel: save current state, label it, update/capture, delete, and recall with morph time
  - auto-refresh-safe form selections (pending choices are preserved until applied)
  - auto-refresh pauses while editing form fields (resumes automatically after short idle)
  - safe shutdown blackout: Ctrl+C writes zero values to fixtures and DMX output to prevent restart motion
- Audio input routing UI:
  - lists available input devices
  - shows default/selected/active input
  - allows switching microphone source live
  - MIDI mapping for reactive mode start/stop
- Group UI + API:
  - create/delete groups
  - assign fixtures to groups
  - control channel kinds for full group (dimmer/rgb/strobe/speed/etc.)
  - apply group mode labels (Jump/Gradient/Pulse/Voice, etc.)
- Template portability:
  - export templates as JSON
  - import templates from JSON file
- Audio reactive engine:
  - real microphone analysis via PortAudio when available
  - adaptive energy + bass/treble + beat + BPM estimation
  - fallback simulated analyzer when PortAudio is unavailable
  - selectable reactive profiles:
    - `balanced` (default musical behavior)
    - `volume_blackout` (no light output below threshold)
  - live volume meter + adjustable minimum energy threshold slider (ignore low background noise)
  - reactive playback maps to channel kinds and labeled ranges with documented formulas in code
  - quiet-room movement guard for pan/tilt fixtures to prevent random drift in near-silence

## Build

### Prerequisites

- CMake 3.28+
- C++23 compiler
- SQLite3 dev package
- Optional: PortAudio (`portaudio-2.0`) for real microphone input
- Optional: RtMidi (`rtmidi`) for server-side MIDI input (cross-platform backend)
- macOS fallback: CoreMIDI backend is used automatically when RtMidi is not installed

### OS-Specific Setup Guides

- [Linux guide](./docs/setup/linux.md)
- [macOS guide](./docs/setup/macos.md)
- [Windows guide](./docs/setup/windows.md)

### Linux/macOS (Ninja)

```bash
cmake --preset ninja-debug
cmake --build --preset build-debug
./build/debug/tuxdmx --bind 0.0.0.0 --port 8080
```

### macOS One-Command Run Script

```bash
./scripts/run_tuxdmx_mac.sh
```

The script will:
- configure + build with CMake presets
- start the TuxDMX server
- wait for `/api/state` to respond
- open `http://127.0.0.1:8080` in your browser
- print a LAN URL for phone/tablet access

Optional flags:

```bash
./scripts/run_tuxdmx_mac.sh --port 8090 --bind 0.0.0.0 --no-open
```

### Linux One-Command Run Script

```bash
./scripts/run_tuxdmx_linux.sh
```

Optional flags:

```bash
./scripts/run_tuxdmx_linux.sh --run-tests --port 8090 --bind 0.0.0.0 --no-open
```

### Windows (MSVC)

```powershell
cmake -S . -B build\debug -G "Ninja" -DCMAKE_BUILD_TYPE=Debug
cmake --build build\debug
.\build\debug\tuxdmx.exe --bind 0.0.0.0 --port 8080
```

Open `http://<server-ip>:8080` from phone/desktop browser.

### Windows One-Command Run Script

```powershell
.\scripts\run_tuxdmx_windows.ps1
```

The script will:
- verify required tools are available (`cmake`, `ninja` for ninja presets, and a C++ compiler)
- print install/download guidance if something is missing
- configure + build with CMake presets
- start the server and wait for `/api/state`
- open `http://127.0.0.1:8080` in your browser
- print local/LAN URL and log file path for debugging

Optional flags:

```powershell
.\scripts\run_tuxdmx_windows.ps1 -Port 8090 -Bind 0.0.0.0 -RunTests -NoOpen
```

If PowerShell blocks script execution, run once in that shell:

```powershell
Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass
```

## Tests

```bash
cmake --preset ninja-debug
cmake --build --preset build-debug
ctest --preset test-debug
```

## CLI options

```text
--bind <ip>          default: 0.0.0.0
--port <port>        default: 8080
--db <path>          default: data/tuxdmx.sqlite
--web-root <path>    default: ./web
--log-file <path>    default: data/tuxdmx.log
```

## Key API Endpoints

- `GET /api/state` full UI state payload
- `POST /api/templates` create template
- `POST /api/templates/{id}/channels`
- `POST /api/channels/{id}/ranges`
- `POST /api/fixtures` patch fixture
- `POST /api/fixtures/{id}/channels/{channel}` set channel value
- `POST /api/dmx/output-universe` set active output universe
- `POST /api/dmx/universes` create/ensure a universe exists
- `POST /api/dmx/patches` apply temporary direct DMX patches (`universe:address:value,...`)
- `POST /api/dmx/blackout` panic blackout (set known universes and fixture channels to zero, reactive off)
- `POST /api/dmx/write-retry-limit` set DMX write retries (`retries` in range `1..200`)
- `POST /api/audio/reactive` toggle music-reactive mode
- `POST /api/audio/reactive-threshold` set minimum reactive energy threshold (`threshold` in range `0..1`)
- `POST /api/audio/reactive-profile` set reactive profile (`profile`: `balanced` or `volume_blackout`)
- `POST /api/audio/input-device` select active audio input device (`-1` = default)
- `POST /api/groups` create group
- `POST /api/groups/{id}/fixtures` set group members
- `POST /api/groups/{id}/kinds/{kind}` apply value by channel kind
- `POST /api/groups/{id}/mode` apply mode by label
- `POST /api/scenes` create scene from current fixture state
- `POST /api/scenes/{id}/update` rename/update default transition seconds
- `POST /api/scenes/{id}/capture` overwrite scene values from current fixture state
- `POST /api/scenes/{id}/recall` morph to scene values (`transition_seconds` optional)
- `POST /api/scenes/{id}/delete` delete scene
- `GET /api/midi` get server MIDI status/mappings
- `POST /api/midi/input-mode` set MIDI source mode (`all` or input id)
- `POST /api/midi/learn/start` arm server MIDI learn (`control_id`)
- `POST /api/midi/learn/cancel` cancel MIDI learn
- `POST /api/midi/mappings/clear` clear one mapping (`control_id`)
- `GET /api/logs` get recent server logs
- `POST /api/logs/clear` clear in-memory debug log buffer
- `GET /api/templates/export` export templates JSON

## Notes

- ENTTEC DMX USB Pro is a single physical DMX output. This app supports multiple universes in software and lets you select which universe is routed to the hardware output.
- MIDI mapping is handled in the server (RtMidi when available, CoreMIDI fallback on macOS), so control is not tied to browser Web MIDI support.
- Reactive formulas are intentionally commented in `/Volumes/extreme-ssd/projects/dmx512/tuxdmx/src/app/app_controller.cpp` and `/Volumes/extreme-ssd/projects/dmx512/tuxdmx/src/audio/audio_engine.cpp` so you can tune behavior quickly.

## Open Source Project Files

- [Contributing guide](./CONTRIBUTING.md)
- [Known issues](./KNOWN_ISSUES.md)
- [Changelog](./CHANGELOG.md)
- [Credits](./CREDITS.md)
- [Documentation index](./docs/README.md)
- [API guide for custom UIs](./docs/api/README.md)

## Acknowledgements

Initial architecture and a significant part of the current implementation were developed with AI assistance using **OpenAI Codex**, with technical direction and ongoing maintenance by the project maintainer.

OpenAI is not affiliated with, and does not endorse, this project.

## License

This project is licensed under the [MIT License](./LICENSE).

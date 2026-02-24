# DMX Backend Development Guide

This project now uses a pluggable DMX output backend layer.

You can add support for another interface/protocol (USB or network) without changing the app API, fixture model, audio engine, or UI state model.

## Current Backend Architecture

- Backend interface: `/Volumes/extreme-ssd/projects/dmx512/tuxdmx/src/include/dmx_output_backend.hpp`
- Backend factory: `/Volumes/extreme-ssd/projects/dmx512/tuxdmx/src/include/dmx_backend_factory.hpp`
- Factory implementation: `/Volumes/extreme-ssd/projects/dmx512/tuxdmx/src/dmx/dmx_backend_factory.cpp`
- DMX engine (backend-agnostic): `/Volumes/extreme-ssd/projects/dmx512/tuxdmx/src/dmx/dmx_engine.cpp`
- Existing backend example: `/Volumes/extreme-ssd/projects/dmx512/tuxdmx/src/dmx/enttec_dmx_pro.cpp`

## Contract to Implement

Implement a class derived from `DmxOutputBackend`:

- `backendName()`: stable backend id string, for example `artnet` or `ola`.
- `discoverAndConnect()`: probe and connect if possible.
- `disconnect()`: close sockets/handles and mark status disconnected.
- `sendUniverse(...)`: transmit one 512-channel frame.
- `setWriteRetryLimit(...)`: support runtime retry tuning from UI/API.
- `writeRetryLimit()`: return current limit.
- `devices()`: return current discovered output-device candidates.
- `refreshDevices()`: refresh candidate cache (typically while disconnected).
- `setPreferredDeviceId(...)`: set auto/manual selection target (empty = auto).
- `preferredDeviceId()`: return current preferred target id.
- `status()`: return the backend status snapshot.

## Status Semantics

Populate `DmxDeviceStatus` consistently:

- `backend`: backend id string (required).
- `connected`: transport connection state.
- `endpoint`: best endpoint string for the backend (`COM4`, `/dev/ttyUSB0`, `192.168.1.50:6454`, etc.).
- `activeDeviceId`: currently connected candidate id when available.
- `preferredDeviceId`: user-selected target id (empty means auto-select).
- `lastError`: human-readable last transport error.
- `writeRetryLimit` and `consecutiveWriteFailures`: used by UI and diagnostics.
- `port`, `serial`, `firmwareMajor`, `firmwareMinor`: optional legacy/hardware fields. Set when meaningful.

## Step-by-Step: Add a New Backend

1. Add backend class files:
   - Header in `src/include/`
   - Implementation in `src/dmx/`
2. Implement `DmxOutputBackend` methods with internal locking where needed.
3. Register backend in factory (`dmx_backend_factory.cpp`):
   - Add canonical name normalization aliases.
   - Add support in `isSupportedDmxBackendName(...)`.
   - Include name in `supportedDmxBackendNames()`.
   - Instantiate in `createDmxOutputBackend(...)`.
4. Add the new `.cpp` file to `TUXDMX_SOURCES` in `CMakeLists.txt`.
5. Build and test:
   - `cmake --build --preset build-debug`
   - `ctest --preset test-debug`
6. Verify CLI:
   - `./build/debug/tuxdmx --help`
   - Confirm backend appears in the supported list.
7. Run with your backend:
   - `./build/debug/tuxdmx --dmx-backend <backend-name>`

## Minimal Backend Skeleton

```cpp
class MyBackend final : public tuxdmx::DmxOutputBackend {
 public:
  std::string backendName() const override { return "my-backend"; }
  bool discoverAndConnect() override;
  void disconnect() override;
  bool sendUniverse(const std::array<std::uint8_t, 512>& channels) override;
  void setWriteRetryLimit(int limit) override;
  int writeRetryLimit() const override;
  std::vector<tuxdmx::DmxOutputDevice> devices() const override;
  void refreshDevices() override;
  void setPreferredDeviceId(std::string deviceId) override;
  std::string preferredDeviceId() const override;
  tuxdmx::DmxDeviceStatus status() const override;
};
```

## Design Notes

- The engine sends one universe frame about every 33 ms.
- Backends should avoid blocking for long periods in `sendUniverse(...)`.
- On repeated write failures, mark disconnected and let the engine re-probe.
- Keep backend errors clear because users rely on `/api/status` and UI diagnostics.

## Known Scope Limits

- Backend selection is currently startup-time (`--dmx-backend`), not hot-swapped at runtime.
- Backend-specific settings (for example fixed IP target or serial override) are not exposed yet. Add per-backend settings only when needed.

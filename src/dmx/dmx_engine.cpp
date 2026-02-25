#include "dmx_engine.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <sstream>
#include <utility>

#include "dmx_backend_factory.hpp"
#include "logger.hpp"
#include "utils.hpp"

namespace tuxdmx {

namespace {

std::array<std::uint8_t, 512> makeZeroUniverse() {
  std::array<std::uint8_t, 512> out{};
  out.fill(0);
  return out;
}

int clampFrameIntervalMs(int intervalMs) { return std::clamp(intervalMs, 5, 1000); }

int clampReconnectBaseMs(int baseMs) { return std::clamp(baseMs, 100, 30000); }

std::int64_t unixMs(std::chrono::system_clock::time_point timePoint) {
  if (timePoint.time_since_epoch().count() <= 0) {
    return 0;
  }
  return std::chrono::duration_cast<std::chrono::milliseconds>(timePoint.time_since_epoch()).count();
}

int computeReconnectBackoffMs(int baseMs, int attempt, std::mt19937& rng) {
  const int safeAttempt = std::max(1, attempt);
  const int exponent = std::clamp(safeAttempt - 1, 0, 8);

  std::int64_t scaled = baseMs;
  for (int i = 0; i < exponent; ++i) {
    scaled *= 2;
  }
  scaled = std::clamp<std::int64_t>(scaled, baseMs, 60000);

  std::uniform_real_distribution<double> jitter(0.88, 1.14);
  const auto jittered = static_cast<int>(std::llround(static_cast<double>(scaled) * jitter(rng)));
  return std::clamp(jittered, baseMs, 60000);
}

std::string formatChangedChannelValues(const std::vector<std::size_t>& changed,
                                       const std::array<std::uint8_t, 512>& frame,
                                       const std::array<std::uint8_t, 512>* previousFrame) {
  constexpr std::size_t kMaxPrintedChannels = 24;

  std::ostringstream ss;
  std::size_t printed = 0;
  for (std::size_t index : changed) {
    if (printed >= kMaxPrintedChannels) {
      break;
    }
    if (printed > 0) {
      ss << ", ";
    }
    ss << "CH" << (index + 1) << '=';
    if (previousFrame != nullptr) {
      ss << static_cast<int>((*previousFrame)[index]) << "->";
    }
    ss << static_cast<int>(frame[index]);
    ++printed;
  }

  if (changed.size() > printed) {
    ss << ", +" << (changed.size() - printed) << " more";
  }

  return ss.str();
}

}  // namespace

DmxEngine::DmxEngine(std::string backendName) : reconnectRng_(std::random_device{}()) {
  std::string error;
  backend_ = createDmxOutputBackend(backendName, error);
  universes_.emplace(1, makeZeroUniverse());
}

DmxEngine::DmxEngine(std::unique_ptr<DmxOutputBackend> backend)
    : reconnectRng_(std::random_device{}()), backend_(std::move(backend)) {
  universes_.emplace(1, makeZeroUniverse());
}

DmxEngine::~DmxEngine() { stop(); }

void DmxEngine::start() {
  bool expected = false;
  if (!running_.compare_exchange_strong(expected, true)) {
    return;
  }

  {
    std::scoped_lock lock(transportMutex_);
    transportState_ = "disconnected";
    reconnectAttempt_ = 0;
    reconnectBackoffMs_ = 0;
    nextReconnectAt_ = {};
    lastConnectAttemptAt_ = {};
    lastSuccessfulFrameAt_ = {};
    debugFrameCounter_ = 0;
    debugUnchangedFrames_ = 0;
    debugHasLastFrame_ = false;
  }

  worker_ = std::thread([this] { workerLoop(); });
}

void DmxEngine::stop() {
  bool expected = true;
  if (!running_.compare_exchange_strong(expected, false)) {
    return;
  }

  if (worker_.joinable()) {
    worker_.join();
  }

  // Some interfaces can keep transmitting the last frame after host exit.
  // Write explicit blackout frames so fixtures do not remain in active/macro states.
  if (backend_) {
    const auto blackout = makeZeroUniverse();
    for (int i = 0; i < 3; ++i) {
      if (!backend_->sendUniverse(blackout)) {
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(12));
    }

    backend_->disconnect();
  }

  {
    std::scoped_lock lock(transportMutex_);
    transportState_ = "disconnected";
    reconnectAttempt_ = 0;
    reconnectBackoffMs_ = 0;
    nextReconnectAt_ = {};
    debugFrameCounter_ = 0;
    debugUnchangedFrames_ = 0;
    debugHasLastFrame_ = false;
  }
}

void DmxEngine::setChannel(int universe, int absoluteAddress, int value) {
  if (universe < 1 || absoluteAddress < 1 || absoluteAddress > 512) {
    return;
  }

  std::scoped_lock lock(mutex_);
  auto [it, _inserted] = universes_.try_emplace(universe, makeZeroUniverse());
  it->second[static_cast<std::size_t>(absoluteAddress - 1)] = static_cast<std::uint8_t>(clampDmx(value));
}

void DmxEngine::replaceUniverse(int universe, const std::array<std::uint8_t, 512>& data) {
  if (universe < 1) {
    return;
  }

  std::scoped_lock lock(mutex_);
  universes_[universe] = data;
}

void DmxEngine::clearUniverse(int universe) {
  if (universe < 1) {
    return;
  }

  std::scoped_lock lock(mutex_);
  universes_[universe] = makeZeroUniverse();
}

void DmxEngine::ensureUniverse(int universe) {
  if (universe < 1) {
    return;
  }

  std::scoped_lock lock(mutex_);
  universes_.try_emplace(universe, makeZeroUniverse());
}

void DmxEngine::setOutputUniverse(int universe) {
  if (universe < 1) {
    return;
  }

  std::scoped_lock lock(mutex_);
  outputUniverse_ = universe;
  universes_.try_emplace(universe, makeZeroUniverse());
}

int DmxEngine::outputUniverse() const {
  std::scoped_lock lock(mutex_);
  return outputUniverse_;
}

void DmxEngine::setWriteRetryLimit(int limit) {
  if (!backend_) {
    return;
  }
  backend_->setWriteRetryLimit(limit);
}

int DmxEngine::writeRetryLimit() const {
  if (!backend_) {
    return DmxDeviceStatus{}.writeRetryLimit;
  }
  return backend_->writeRetryLimit();
}

void DmxEngine::setFrameIntervalMs(int intervalMs) {
  std::scoped_lock lock(transportMutex_);
  frameIntervalMs_ = clampFrameIntervalMs(intervalMs);
}

int DmxEngine::frameIntervalMs() const {
  std::scoped_lock lock(transportMutex_);
  return frameIntervalMs_;
}

void DmxEngine::setReconnectBaseMs(int baseMs) {
  std::scoped_lock lock(transportMutex_);
  reconnectBaseMs_ = clampReconnectBaseMs(baseMs);
}

int DmxEngine::reconnectBaseMs() const {
  std::scoped_lock lock(transportMutex_);
  return reconnectBaseMs_;
}

void DmxEngine::setFrameDebugLogging(bool enabled) {
  std::scoped_lock lock(transportMutex_);
  frameDebugLogging_ = enabled;
  debugFrameCounter_ = 0;
  debugUnchangedFrames_ = 0;
  debugHasLastFrame_ = false;
}

bool DmxEngine::frameDebugLogging() const {
  std::scoped_lock lock(transportMutex_);
  return frameDebugLogging_;
}

void DmxEngine::setProbeTimeoutMs(int timeoutMs) {
  if (!backend_) {
    return;
  }
  backend_->setProbeTimeoutMs(timeoutMs);
}

int DmxEngine::probeTimeoutMs() const {
  if (!backend_) {
    return DmxDeviceStatus{}.probeTimeoutMs;
  }
  return backend_->probeTimeoutMs();
}

void DmxEngine::setSerialReadTimeoutMs(int timeoutMs) {
  if (!backend_) {
    return;
  }
  backend_->setSerialReadTimeoutMs(timeoutMs);
}

int DmxEngine::serialReadTimeoutMs() const {
  if (!backend_) {
    return DmxDeviceStatus{}.serialReadTimeoutMs;
  }
  return backend_->serialReadTimeoutMs();
}

void DmxEngine::setStrictPreferredDevice(bool strict) {
  if (!backend_) {
    return;
  }
  backend_->setStrictPreferredDevice(strict);
}

bool DmxEngine::strictPreferredDevice() const {
  if (!backend_) {
    return DmxDeviceStatus{}.strictPreferredDevice;
  }
  return backend_->strictPreferredDevice();
}

std::string DmxEngine::backendName() const {
  if (!backend_) {
    return "unknown";
  }
  return backend_->backendName();
}

std::vector<DmxOutputDevice> DmxEngine::devices() const {
  if (!backend_) {
    return {};
  }
  return backend_->devices();
}

void DmxEngine::refreshDevices() {
  if (!backend_) {
    return;
  }
  backend_->refreshDevices();
}

void DmxEngine::setPreferredDeviceId(std::string deviceId) {
  if (!backend_) {
    return;
  }
  backend_->setPreferredDeviceId(std::move(deviceId));
}

std::string DmxEngine::preferredDeviceId() const {
  if (!backend_) {
    return {};
  }
  return backend_->preferredDeviceId();
}

void DmxEngine::forceReconnect() {
  if (!backend_) {
    return;
  }
  backend_->disconnect();

  std::scoped_lock lock(transportMutex_);
  transportState_ = "disconnected";
  reconnectAttempt_ = 0;
  reconnectBackoffMs_ = 0;
  nextReconnectAt_ = {};
  debugFrameCounter_ = 0;
  debugUnchangedFrames_ = 0;
  debugHasLastFrame_ = false;
}

std::vector<int> DmxEngine::knownUniverses() const {
  std::scoped_lock lock(mutex_);
  std::vector<int> universes;
  universes.reserve(universes_.size());
  for (const auto& [universe, _] : universes_) {
    universes.push_back(universe);
  }
  std::sort(universes.begin(), universes.end());
  return universes;
}

DmxDeviceStatus DmxEngine::status() const {
  DmxDeviceStatus out;
  if (backend_) {
    out = backend_->status();
  } else {
    out.lastError = "No DMX backend available";
  }

  {
    std::scoped_lock lock(transportMutex_);
    out.transportState = transportState_;
    out.reconnectAttempt = reconnectAttempt_;
    out.reconnectBackoffMs = reconnectBackoffMs_;
    out.reconnectBaseMs = reconnectBaseMs_;
    out.frameIntervalMs = frameIntervalMs_;
    out.frameDebugLogging = frameDebugLogging_;
    if (out.lastConnectAttemptUnixMs <= 0) {
      out.lastConnectAttemptUnixMs = unixMs(lastConnectAttemptAt_);
    }
    if (out.lastSuccessfulFrameUnixMs <= 0) {
      out.lastSuccessfulFrameUnixMs = unixMs(lastSuccessfulFrameAt_);
    }
  }

  if (backend_) {
    out.probeTimeoutMs = backend_->probeTimeoutMs();
    out.serialReadTimeoutMs = backend_->serialReadTimeoutMs();
    out.strictPreferredDevice = backend_->strictPreferredDevice();
  }

  return out;
}

void DmxEngine::workerLoop() {
  while (running_.load()) {
    const auto nowSteady = std::chrono::steady_clock::now();

    auto backendStatus = status();
    if (!backendStatus.connected) {
      bool shouldProbe = false;
      {
        std::scoped_lock lock(transportMutex_);
        if (nextReconnectAt_.time_since_epoch().count() == 0 || nowSteady >= nextReconnectAt_) {
          shouldProbe = true;
        }
      }

      if (shouldProbe && backend_) {
        {
          std::scoped_lock lock(transportMutex_);
          transportState_ = "probing";
          lastConnectAttemptAt_ = std::chrono::system_clock::now();
          nextReconnectAt_ = {};
        }

        const bool connected = backend_->discoverAndConnect();
        backendStatus = backend_->status();

        std::scoped_lock lock(transportMutex_);
        if (connected && backendStatus.connected) {
          transportState_ = "connected";
          reconnectAttempt_ = 0;
          reconnectBackoffMs_ = 0;
          nextReconnectAt_ = {};
        } else {
          ++reconnectAttempt_;
          reconnectBackoffMs_ = computeReconnectBackoffMs(reconnectBaseMs_, reconnectAttempt_, reconnectRng_);
          nextReconnectAt_ = nowSteady + std::chrono::milliseconds(reconnectBackoffMs_);
          transportState_ = "disconnected";
        }
      }
    } else {
      std::array<std::uint8_t, 512> snapshot = makeZeroUniverse();
      int activeUniverse = 1;
      {
        std::scoped_lock lock(mutex_);
        activeUniverse = outputUniverse_;
        if (auto it = universes_.find(activeUniverse); it != universes_.end()) {
          snapshot = it->second;
        }
      }

      bool sent = false;
      if (backend_) {
        sent = backend_->sendUniverse(snapshot);
        backendStatus = backend_->status();
      }

      std::string debugLogLine;
      LogLevel debugLogLevel = LogLevel::Debug;
      {
        std::scoped_lock lock(transportMutex_);
        if (sent) {
          transportState_ = "connected";
          reconnectAttempt_ = 0;
          reconnectBackoffMs_ = 0;
          nextReconnectAt_ = {};
          lastSuccessfulFrameAt_ = std::chrono::system_clock::now();

          if (frameDebugLogging_) {
            ++debugFrameCounter_;
            const bool baseline = !debugHasLastFrame_ || debugLastUniverse_ != activeUniverse;
            std::vector<std::size_t> changedChannels;
            changedChannels.reserve(32);
            for (std::size_t i = 0; i < snapshot.size(); ++i) {
              if (baseline) {
                if (snapshot[i] != 0) {
                  changedChannels.push_back(i);
                }
              } else if (snapshot[i] != debugLastFrame_[i]) {
                changedChannels.push_back(i);
              }
            }

            if (baseline) {
              debugUnchangedFrames_ = 0;
              debugLogLine =
                  "DMX TX baseline U" + std::to_string(activeUniverse) + " frame#" + std::to_string(debugFrameCounter_)
                  + " changed=" + std::to_string(changedChannels.size()) + " (CH=value 0-255): "
                  + (changedChannels.empty() ? std::string("all channels are 0")
                                             : formatChangedChannelValues(changedChannels, snapshot, nullptr));
            } else if (!changedChannels.empty()) {
              debugUnchangedFrames_ = 0;
              debugLogLine = "DMX TX U" + std::to_string(activeUniverse) + " frame#" + std::to_string(debugFrameCounter_)
                             + " changed=" + std::to_string(changedChannels.size()) + " (CH=old->new 0-255): "
                             + formatChangedChannelValues(changedChannels, snapshot, &debugLastFrame_);
            } else {
              ++debugUnchangedFrames_;
              if (debugUnchangedFrames_ % 60 == 0) {
                debugLogLine = "DMX TX U" + std::to_string(activeUniverse) + " unchanged for "
                               + std::to_string(debugUnchangedFrames_) + " frames";
              }
            }

            debugLastFrame_ = snapshot;
            debugLastUniverse_ = activeUniverse;
            debugHasLastFrame_ = true;
          }
        } else {
          transportState_ = "degraded";
          if (!backendStatus.connected) {
            if (nextReconnectAt_.time_since_epoch().count() == 0 || nowSteady >= nextReconnectAt_) {
              ++reconnectAttempt_;
              reconnectBackoffMs_ = computeReconnectBackoffMs(reconnectBaseMs_, reconnectAttempt_, reconnectRng_);
              nextReconnectAt_ = nowSteady + std::chrono::milliseconds(reconnectBackoffMs_);
            }
            debugHasLastFrame_ = false;
            debugUnchangedFrames_ = 0;
            if (frameDebugLogging_) {
              debugLogLevel = LogLevel::Warn;
              debugLogLine = "DMX TX send failed on U" + std::to_string(activeUniverse) + ", backend disconnected";
            }
          }
        }
      }

      if (!debugLogLine.empty()) {
        logMessage(debugLogLevel, "dmx-tx", debugLogLine);
      }
    }

    int intervalMs = 33;
    {
      std::scoped_lock lock(transportMutex_);
      intervalMs = frameIntervalMs_;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(intervalMs));
  }
}

}  // namespace tuxdmx

#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "dmx_output_backend.hpp"

namespace tuxdmx {

class DmxEngine {
 public:
  explicit DmxEngine(std::string backendName);
  explicit DmxEngine(std::unique_ptr<DmxOutputBackend> backend);
  ~DmxEngine();

  DmxEngine(const DmxEngine&) = delete;
  DmxEngine& operator=(const DmxEngine&) = delete;

  void start();
  void stop();

  void setChannel(int universe, int absoluteAddress, int value);
  void replaceUniverse(int universe, const std::array<std::uint8_t, 512>& data);
  void clearUniverse(int universe);
  void ensureUniverse(int universe);
  void setOutputUniverse(int universe);
  int outputUniverse() const;
  void setWriteRetryLimit(int limit);
  int writeRetryLimit() const;
  void setFrameIntervalMs(int intervalMs);
  int frameIntervalMs() const;
  void setReconnectBaseMs(int baseMs);
  int reconnectBaseMs() const;
  void setProbeTimeoutMs(int timeoutMs);
  int probeTimeoutMs() const;
  void setSerialReadTimeoutMs(int timeoutMs);
  int serialReadTimeoutMs() const;
  void setStrictPreferredDevice(bool strict);
  bool strictPreferredDevice() const;
  std::string backendName() const;
  std::vector<DmxOutputDevice> devices() const;
  void refreshDevices();
  void setPreferredDeviceId(std::string deviceId);
  std::string preferredDeviceId() const;
  void forceReconnect();
  std::vector<int> knownUniverses() const;

  DmxDeviceStatus status() const;

 private:
  void workerLoop();

  mutable std::mutex mutex_;
  mutable std::mutex transportMutex_;
  std::unordered_map<int, std::array<std::uint8_t, 512>> universes_;
  int outputUniverse_ = 1;
  std::string transportState_ = "disconnected";
  int reconnectAttempt_ = 0;
  int reconnectBackoffMs_ = 0;
  int reconnectBaseMs_ = 800;
  int frameIntervalMs_ = 33;
  std::chrono::steady_clock::time_point nextReconnectAt_{};
  std::chrono::system_clock::time_point lastConnectAttemptAt_{};
  std::chrono::system_clock::time_point lastSuccessfulFrameAt_{};
  std::mt19937 reconnectRng_;
  std::unique_ptr<DmxOutputBackend> backend_;
  std::atomic<bool> running_{false};
  std::thread worker_;
};

}  // namespace tuxdmx

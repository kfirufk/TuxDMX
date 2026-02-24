#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "dmx_engine.hpp"

namespace {

std::int64_t nowUnixMs() {
  const auto now = std::chrono::system_clock::now();
  return std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
}

bool waitUntil(const std::function<bool()>& predicate, std::chrono::milliseconds timeout) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (predicate()) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return predicate();
}

class MockDmxOutputBackend final : public tuxdmx::DmxOutputBackend {
 public:
  MockDmxOutputBackend() {
    status_.backend = backendName();
    status_.probeTimeoutMs = probeTimeoutMs_;
    status_.serialReadTimeoutMs = serialReadTimeoutMs_;
    status_.strictPreferredDevice = strictPreferredDevice_;
    status_.writeRetryLimit = writeRetryLimit_;
  }

  std::string backendName() const override { return "mock-backend"; }

  bool discoverAndConnect() override {
    std::scoped_lock lock(mutex_);
    ++discoverAttempts_;
    status_.lastConnectAttemptUnixMs = nowUnixMs();

    if (failDiscoverAttempts_ > 0) {
      --failDiscoverAttempts_;
      status_.connected = false;
      status_.transportState = "disconnected";
      status_.lastError = "Mock discovery failure";
      return false;
    }

    if (devices_.empty()) {
      status_.connected = false;
      status_.transportState = "disconnected";
      status_.lastError = "No devices";
      return false;
    }

    tuxdmx::DmxOutputDevice selected = devices_.front();
    if (!preferredDeviceId_.empty()) {
      const auto it = std::find_if(devices_.begin(), devices_.end(), [this](const tuxdmx::DmxOutputDevice& device) {
        return device.id == preferredDeviceId_;
      });
      if (it == devices_.end()) {
        if (strictPreferredDevice_) {
          status_.connected = false;
          status_.transportState = "disconnected";
          status_.lastError = "Preferred device not found";
          status_.lastErrorStage = "selection";
          return false;
        }
      } else {
        selected = *it;
      }
    }

    status_.connected = true;
    status_.transportState = "connected";
    status_.activeDeviceId = selected.id;
    status_.endpoint = selected.endpoint;
    status_.serial = selected.serial;
    status_.lastError.clear();
    status_.lastErrorStage.clear();
    return true;
  }

  void disconnect() override {
    std::scoped_lock lock(mutex_);
    status_.connected = false;
    status_.transportState = "disconnected";
    status_.activeDeviceId.clear();
  }

  bool sendUniverse(const std::array<std::uint8_t, 512>& channels) override {
    std::scoped_lock lock(mutex_);
    if (!status_.connected) {
      return false;
    }
    if (failSendAttempts_ > 0) {
      --failSendAttempts_;
      status_.connected = false;
      status_.transportState = "degraded";
      status_.lastError = "Mock send failure";
      status_.lastErrorStage = "write";
      return false;
    }

    lastFrame_ = channels;
    status_.lastSuccessfulFrameUnixMs = nowUnixMs();
    status_.transportState = "connected";
    return true;
  }

  void setWriteRetryLimit(int limit) override {
    std::scoped_lock lock(mutex_);
    writeRetryLimit_ = std::clamp(limit, 1, 200);
    status_.writeRetryLimit = writeRetryLimit_;
  }

  int writeRetryLimit() const override {
    std::scoped_lock lock(mutex_);
    return writeRetryLimit_;
  }

  void setProbeTimeoutMs(int timeoutMs) override {
    std::scoped_lock lock(mutex_);
    probeTimeoutMs_ = std::clamp(timeoutMs, 50, 5000);
    status_.probeTimeoutMs = probeTimeoutMs_;
  }

  int probeTimeoutMs() const override {
    std::scoped_lock lock(mutex_);
    return probeTimeoutMs_;
  }

  void setSerialReadTimeoutMs(int timeoutMs) override {
    std::scoped_lock lock(mutex_);
    serialReadTimeoutMs_ = std::clamp(timeoutMs, 20, 5000);
    status_.serialReadTimeoutMs = serialReadTimeoutMs_;
  }

  int serialReadTimeoutMs() const override {
    std::scoped_lock lock(mutex_);
    return serialReadTimeoutMs_;
  }

  void setStrictPreferredDevice(bool strict) override {
    std::scoped_lock lock(mutex_);
    strictPreferredDevice_ = strict;
    status_.strictPreferredDevice = strictPreferredDevice_;
  }

  bool strictPreferredDevice() const override {
    std::scoped_lock lock(mutex_);
    return strictPreferredDevice_;
  }

  std::vector<tuxdmx::DmxOutputDevice> devices() const override {
    std::scoped_lock lock(mutex_);
    auto out = devices_;
    for (auto& device : out) {
      device.connected = status_.connected && device.id == status_.activeDeviceId;
    }
    return out;
  }

  void refreshDevices() override {}

  void setPreferredDeviceId(std::string deviceId) override {
    std::scoped_lock lock(mutex_);
    preferredDeviceId_ = std::move(deviceId);
    status_.preferredDeviceId = preferredDeviceId_;
  }

  std::string preferredDeviceId() const override {
    std::scoped_lock lock(mutex_);
    return preferredDeviceId_;
  }

  tuxdmx::DmxDeviceStatus status() const override {
    std::scoped_lock lock(mutex_);
    return status_;
  }

  void setDevices(std::vector<tuxdmx::DmxOutputDevice> devices) {
    std::scoped_lock lock(mutex_);
    devices_ = std::move(devices);
  }

  void setFailDiscoverAttempts(int count) {
    std::scoped_lock lock(mutex_);
    failDiscoverAttempts_ = std::max(0, count);
  }

  int discoverAttempts() const {
    std::scoped_lock lock(mutex_);
    return discoverAttempts_;
  }

 private:
  mutable std::mutex mutex_;
  tuxdmx::DmxDeviceStatus status_;
  std::vector<tuxdmx::DmxOutputDevice> devices_;
  std::array<std::uint8_t, 512> lastFrame_{};

  std::string preferredDeviceId_;
  int discoverAttempts_ = 0;
  int failDiscoverAttempts_ = 0;
  int failSendAttempts_ = 0;

  int writeRetryLimit_ = 10;
  int probeTimeoutMs_ = 350;
  int serialReadTimeoutMs_ = 250;
  bool strictPreferredDevice_ = true;
};

tuxdmx::DmxOutputDevice makeDevice(std::string id, std::string endpoint, std::string serial) {
  tuxdmx::DmxOutputDevice device;
  device.id = std::move(id);
  device.name = device.id;
  device.endpoint = std::move(endpoint);
  device.serial = std::move(serial);
  return device;
}

}  // namespace

int main() {
  {
    auto backend = std::make_unique<MockDmxOutputBackend>();
    auto* backendPtr = backend.get();
    backend->setDevices({makeDevice("dev-a", "mock:a", "A001")});
    backend->setFailDiscoverAttempts(2);

    tuxdmx::DmxEngine engine(std::move(backend));
    engine.setFrameIntervalMs(12);
    engine.setReconnectBaseMs(120);
    engine.start();

    const bool connected = waitUntil(
        [&engine] {
          const auto status = engine.status();
          return status.connected && status.transportState == "connected";
        },
        std::chrono::milliseconds(1400));
    assert(connected);
    assert(backendPtr->discoverAttempts() >= 3);

    const auto status = engine.status();
    assert(status.reconnectAttempt == 0);
    assert(status.reconnectBaseMs == 120);
    assert(status.frameIntervalMs == 12);

    engine.stop();
  }

  {
    auto backend = std::make_unique<MockDmxOutputBackend>();
    backend->setDevices({makeDevice("dev-a", "mock:a", "A001"), makeDevice("dev-b", "mock:b", "B001")});

    tuxdmx::DmxEngine engine(std::move(backend));
    engine.setFrameIntervalMs(15);
    engine.start();

    assert(waitUntil(
        [&engine] {
          const auto status = engine.status();
          return status.connected;
        },
        std::chrono::milliseconds(600)));

    engine.setPreferredDeviceId("dev-b");
    engine.forceReconnect();
    assert(waitUntil(
        [&engine] {
          const auto status = engine.status();
          return status.connected && status.activeDeviceId == "dev-b";
        },
        std::chrono::milliseconds(900)));

    engine.setPreferredDeviceId("dev-a");
    engine.forceReconnect();
    assert(waitUntil(
        [&engine] {
          const auto status = engine.status();
          return status.connected && status.activeDeviceId == "dev-a";
        },
        std::chrono::milliseconds(900)));

    engine.stop();
  }

  return 0;
}

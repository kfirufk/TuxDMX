#include "dmx_backend_factory.hpp"

#include <algorithm>
#include <array>
#include <mutex>
#include <utility>

#include "enttec_dmx_pro.hpp"
#include "utils.hpp"

namespace tuxdmx {

namespace {

class NullDmxOutputBackend final : public DmxOutputBackend {
 public:
  NullDmxOutputBackend(std::string backendName, std::string error) {
    status_.backend = std::move(backendName);
    if (status_.backend.empty()) {
      status_.backend = "unknown";
    }
    status_.connected = false;
    status_.lastError = std::move(error);
  }

  std::string backendName() const override {
    std::scoped_lock lock(mutex_);
    return status_.backend;
  }

  bool discoverAndConnect() override { return false; }

  void disconnect() override {
    std::scoped_lock lock(mutex_);
    status_.connected = false;
  }

  bool sendUniverse(const std::array<std::uint8_t, 512>&) override { return false; }

  void setWriteRetryLimit(int limit) override {
    std::scoped_lock lock(mutex_);
    status_.writeRetryLimit = std::clamp(limit, 1, 200);
    status_.consecutiveWriteFailures = 0;
  }

  int writeRetryLimit() const override {
    std::scoped_lock lock(mutex_);
    return status_.writeRetryLimit;
  }

  DmxDeviceStatus status() const override {
    std::scoped_lock lock(mutex_);
    return status_;
  }

 private:
  mutable std::mutex mutex_;
  DmxDeviceStatus status_;
};

std::string canonicalizeDmxBackendName(std::string_view backendName) {
  const std::string normalized = toLower(trim(backendName));
  if (normalized == "enttec-usb-pro" || normalized == "enttec" || normalized == "usb-pro" || normalized == "dmxusbpro") {
    return std::string(kDefaultDmxBackendName);
  }
  return normalized;
}

}  // namespace

std::string normalizeDmxBackendName(std::string_view backendName) { return canonicalizeDmxBackendName(backendName); }

bool isSupportedDmxBackendName(std::string_view backendName) {
  const std::string normalized = canonicalizeDmxBackendName(backendName);
  return normalized == kDefaultDmxBackendName;
}

std::vector<std::string> supportedDmxBackendNames() { return {std::string(kDefaultDmxBackendName)}; }

std::unique_ptr<DmxOutputBackend> createDmxOutputBackend(std::string_view backendName, std::string& error) {
  const std::string normalized = canonicalizeDmxBackendName(backendName);

  if (normalized == kDefaultDmxBackendName) {
    error.clear();
    return std::make_unique<EnttecDmxPro>();
  }

  error = "Unsupported DMX backend '" + std::string(backendName) + "'. Supported: " + std::string(kDefaultDmxBackendName);
  return std::make_unique<NullDmxOutputBackend>(normalized, error);
}

}  // namespace tuxdmx

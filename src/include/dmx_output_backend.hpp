#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "models.hpp"

namespace tuxdmx {

class DmxOutputBackend {
 public:
  virtual ~DmxOutputBackend() = default;

  virtual std::string backendName() const = 0;
  virtual bool discoverAndConnect() = 0;
  virtual void disconnect() = 0;
  virtual bool sendUniverse(const std::array<std::uint8_t, 512>& channels) = 0;
  virtual void setWriteRetryLimit(int limit) = 0;
  virtual int writeRetryLimit() const = 0;
  virtual std::vector<DmxOutputDevice> devices() const = 0;
  virtual void refreshDevices() = 0;
  virtual void setPreferredDeviceId(std::string deviceId) = 0;
  virtual std::string preferredDeviceId() const = 0;
  virtual DmxDeviceStatus status() const = 0;
};

}  // namespace tuxdmx

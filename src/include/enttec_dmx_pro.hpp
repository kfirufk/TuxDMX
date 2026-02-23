#pragma once

#include <array>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "models.hpp"

namespace tuxdmx {

class EnttecDmxPro {
 public:
  EnttecDmxPro();
  ~EnttecDmxPro();

  EnttecDmxPro(const EnttecDmxPro&) = delete;
  EnttecDmxPro& operator=(const EnttecDmxPro&) = delete;

  bool discoverAndConnect();
  void disconnect();

  bool sendUniverse(const std::array<std::uint8_t, 512>& channels);
  void setWriteRetryLimit(int limit);
  int writeRetryLimit() const;
  DmxDeviceStatus status() const;

 private:
  bool probePort(const std::string& port, std::string& serial, int& fwMajor, int& fwMinor, std::string& error);
  std::vector<std::string> candidatePorts() const;

  bool writeBytes(const std::uint8_t* data, std::size_t size);
  bool readFrame(std::uint8_t expectedLabel, std::vector<std::uint8_t>& payload, int timeoutMs);

  mutable std::mutex mutex_;
  DmxDeviceStatus status_;
  int consecutiveWriteFailures_ = 0;
  int writeRetryLimit_ = 10;

#ifdef _WIN32
  void* handle_ = nullptr;
#else
  int fd_ = -1;
#endif
};

}  // namespace tuxdmx

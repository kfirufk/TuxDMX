#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "dmx_output_backend.hpp"

namespace tuxdmx {

class DmxEngine {
 public:
  explicit DmxEngine(std::string backendName);
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
  std::string backendName() const;
  std::vector<int> knownUniverses() const;

  DmxDeviceStatus status() const;

 private:
  void workerLoop();

  mutable std::mutex mutex_;
  std::unordered_map<int, std::array<std::uint8_t, 512>> universes_;
  int outputUniverse_ = 1;
  std::unique_ptr<DmxOutputBackend> backend_;
  std::atomic<bool> running_{false};
  std::thread worker_;
};

}  // namespace tuxdmx

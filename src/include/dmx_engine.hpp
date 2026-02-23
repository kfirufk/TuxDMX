#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

#include "enttec_dmx_pro.hpp"

namespace tuxdmx {

class DmxEngine {
 public:
  DmxEngine();
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
  std::vector<int> knownUniverses() const;

  DmxDeviceStatus status() const;

 private:
  void workerLoop();

  mutable std::mutex mutex_;
  std::unordered_map<int, std::array<std::uint8_t, 512>> universes_;
  int outputUniverse_ = 1;
  EnttecDmxPro device_;
  std::atomic<bool> running_{false};
  std::thread worker_;
};

}  // namespace tuxdmx

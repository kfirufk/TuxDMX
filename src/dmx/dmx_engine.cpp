#include "dmx_engine.hpp"

#include <algorithm>
#include <chrono>

#include "dmx_backend_factory.hpp"
#include "utils.hpp"

namespace tuxdmx {

namespace {

std::array<std::uint8_t, 512> makeZeroUniverse() {
  std::array<std::uint8_t, 512> out{};
  out.fill(0);
  return out;
}

}  // namespace

DmxEngine::DmxEngine(std::string backendName) {
  std::string error;
  backend_ = createDmxOutputBackend(backendName, error);
  universes_.emplace(1, makeZeroUniverse());
}

DmxEngine::~DmxEngine() { stop(); }

void DmxEngine::start() {
  bool expected = false;
  if (!running_.compare_exchange_strong(expected, true)) {
    return;
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

std::string DmxEngine::backendName() const {
  if (!backend_) {
    return "unknown";
  }
  return backend_->backendName();
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
  if (!backend_) {
    DmxDeviceStatus fallback;
    fallback.lastError = "No DMX backend available";
    return fallback;
  }
  return backend_->status();
}

void DmxEngine::workerLoop() {
  auto lastDiscover = std::chrono::steady_clock::time_point{};

  while (running_.load()) {
    const auto now = std::chrono::steady_clock::now();

    auto status = this->status();
    if (!status.connected && (lastDiscover.time_since_epoch().count() == 0 || now - lastDiscover > std::chrono::seconds(3))) {
      if (backend_) {
        backend_->discoverAndConnect();
      }
      lastDiscover = now;
      status = this->status();
    }

    if (status.connected) {
      std::array<std::uint8_t, 512> snapshot = makeZeroUniverse();
      {
        std::scoped_lock lock(mutex_);
        if (auto it = universes_.find(outputUniverse_); it != universes_.end()) {
          snapshot = it->second;
        }
      }
      if (backend_) {
        backend_->sendUniverse(snapshot);
      }
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(33));
  }
}

}  // namespace tuxdmx

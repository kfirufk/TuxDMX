#pragma once

#include <atomic>
#include <chrono>
#include <functional>
#include <mutex>
#include <random>
#include <string>
#include <thread>

#include "models.hpp"

namespace tuxdmx {

class AudioEngine {
 public:
  using TickCallback = std::function<void(const AudioMetrics& metrics)>;

  AudioEngine();
  ~AudioEngine();

  AudioEngine(const AudioEngine&) = delete;
  AudioEngine& operator=(const AudioEngine&) = delete;

  void start();
  void stop();

  void setReactiveMode(bool enabled);
  bool reactiveMode() const;

  void setTickCallback(TickCallback callback);
  std::string backendName() const;
  AudioMetrics currentMetrics() const;

 private:
  void loop();
  void analyzeFrame(const float* monoSamples, std::size_t sampleCount);

#ifdef TUXDMX_WITH_PORTAUDIO
  bool initPortAudio();
  void shutdownPortAudio();
#endif

  std::atomic<bool> running_{false};
  std::atomic<bool> reactiveMode_{false};

  mutable std::mutex callbackMutex_;
  TickCallback callback_;

  mutable std::mutex metricsMutex_;
  AudioMetrics lastMetrics_{};

  mutable std::mutex backendMutex_;
  std::string backendName_ = "simulated-energy";

  std::thread worker_;

  std::mt19937 rng_;
  std::uniform_real_distribution<float> dist_{0.0F, 1.0F};

  float noiseFloor_ = 0.01F;
  float energyEnvelope_ = 0.0F;
  float lowPassState_ = 0.0F;
  float bpmEstimate_ = 120.0F;
  bool hasBeatTimestamp_ = false;
  std::chrono::steady_clock::time_point lastBeatTimestamp_{};

#ifdef TUXDMX_WITH_PORTAUDIO
  void* stream_ = nullptr;
  bool portAudioReady_ = false;
#endif
};

}  // namespace tuxdmx

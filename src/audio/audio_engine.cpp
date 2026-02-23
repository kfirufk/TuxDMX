#include "audio_engine.hpp"

#include <algorithm>
#include <array>
#include <cmath>

#ifdef TUXDMX_WITH_PORTAUDIO
#include <portaudio.h>
#endif

namespace tuxdmx {

AudioEngine::AudioEngine() {
  std::random_device rd;
  rng_.seed(rd());
}

AudioEngine::~AudioEngine() { stop(); }

void AudioEngine::start() {
  bool expected = false;
  if (!running_.compare_exchange_strong(expected, true)) {
    return;
  }

  worker_ = std::thread([this] { loop(); });
}

void AudioEngine::stop() {
  bool expected = true;
  if (!running_.compare_exchange_strong(expected, false)) {
    return;
  }

  if (worker_.joinable()) {
    worker_.join();
  }

#ifdef TUXDMX_WITH_PORTAUDIO
  shutdownPortAudio();
#endif
}

void AudioEngine::setReactiveMode(bool enabled) { reactiveMode_.store(enabled); }

bool AudioEngine::reactiveMode() const { return reactiveMode_.load(); }

void AudioEngine::setTickCallback(TickCallback callback) {
  std::scoped_lock lock(callbackMutex_);
  callback_ = std::move(callback);
}

std::string AudioEngine::backendName() const {
  std::scoped_lock lock(backendMutex_);
  return backendName_;
}

AudioMetrics AudioEngine::currentMetrics() const {
  std::scoped_lock lock(metricsMutex_);
  return lastMetrics_;
}

#ifdef TUXDMX_WITH_PORTAUDIO
bool AudioEngine::initPortAudio() {
  if (portAudioReady_) {
    return true;
  }

  if (Pa_Initialize() != paNoError) {
    return false;
  }

  const PaDeviceIndex inputDevice = Pa_GetDefaultInputDevice();
  if (inputDevice == paNoDevice) {
    Pa_Terminate();
    return false;
  }

  const PaDeviceInfo* info = Pa_GetDeviceInfo(inputDevice);
  if (info == nullptr) {
    Pa_Terminate();
    return false;
  }

  PaStreamParameters inputParams{};
  inputParams.device = inputDevice;
  inputParams.channelCount = 1;
  inputParams.sampleFormat = paFloat32;
  inputParams.suggestedLatency = info->defaultLowInputLatency;
  inputParams.hostApiSpecificStreamInfo = nullptr;

  PaStream* stream = nullptr;
  constexpr double kSampleRate = 48000.0;
  constexpr unsigned long kFramesPerBuffer = 256;

  const PaError openErr =
      Pa_OpenStream(&stream, &inputParams, nullptr, kSampleRate, kFramesPerBuffer, paNoFlag, nullptr, nullptr);
  if (openErr != paNoError || stream == nullptr) {
    Pa_Terminate();
    return false;
  }

  const PaError startErr = Pa_StartStream(stream);
  if (startErr != paNoError) {
    Pa_CloseStream(stream);
    Pa_Terminate();
    return false;
  }

  stream_ = stream;
  portAudioReady_ = true;

  {
    std::scoped_lock lock(backendMutex_);
    backendName_ = std::string("portaudio/") + info->name;
  }

  return true;
}

void AudioEngine::shutdownPortAudio() {
  if (!portAudioReady_) {
    return;
  }

  PaStream* stream = static_cast<PaStream*>(stream_);
  if (stream != nullptr) {
    if (Pa_IsStreamActive(stream) == 1) {
      Pa_StopStream(stream);
    }
    Pa_CloseStream(stream);
  }

  stream_ = nullptr;
  portAudioReady_ = false;
  Pa_Terminate();
}
#endif

void AudioEngine::analyzeFrame(const float* monoSamples, std::size_t sampleCount) {
  if (monoSamples == nullptr || sampleCount == 0) {
    return;
  }

  float sumSquares = 0.0F;
  float bassAccum = 0.0F;
  float trebleAccum = 0.0F;

  // Low-pass/high-pass split for rough "bass" and "treble" energy buckets.
  // This keeps the math simple enough to tweak in one place.
  constexpr float kLowPassAlpha = 0.04F;
  for (std::size_t i = 0; i < sampleCount; ++i) {
    const float sample = monoSamples[i];
    sumSquares += sample * sample;

    lowPassState_ += kLowPassAlpha * (sample - lowPassState_);
    const float bass = std::fabs(lowPassState_);
    const float treble = std::fabs(sample - lowPassState_);

    bassAccum += bass;
    trebleAccum += treble;
  }

  const float invCount = 1.0F / static_cast<float>(sampleCount);
  const float rawRms = std::sqrt(sumSquares * invCount);
  const float bassLevel = std::clamp((bassAccum * invCount) * 2.2F, 0.0F, 1.0F);
  const float trebleLevel = std::clamp((trebleAccum * invCount) * 2.2F, 0.0F, 1.0F);

  // Adaptive floor tracks room noise slowly. The control energy then becomes
  // "how much louder than the background are we right now".
  noiseFloor_ = noiseFloor_ * 0.995F + rawRms * 0.005F;
  const float normalizedEnergy = std::clamp((rawRms - noiseFloor_) * 7.5F, 0.0F, 1.0F);

  // Envelope + transient gives a stable beat trigger with fewer false positives.
  energyEnvelope_ = energyEnvelope_ * 0.92F + normalizedEnergy * 0.08F;
  const float transient = std::max(0.0F, normalizedEnergy - energyEnvelope_);
  const float beatThreshold = 0.09F + (energyEnvelope_ * 0.24F);

  const auto now = std::chrono::steady_clock::now();
  const bool cooldownOver = !hasBeatTimestamp_ || (now - lastBeatTimestamp_) > std::chrono::milliseconds(170);
  const bool beat = cooldownOver && transient > beatThreshold;

  if (beat) {
    if (hasBeatTimestamp_) {
      const float deltaMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastBeatTimestamp_).count();
      if (deltaMs > 150.0F) {
        const float instantBpm = 60000.0F / deltaMs;
        if (instantBpm >= 40.0F && instantBpm <= 220.0F) {
          bpmEstimate_ = bpmEstimate_ * 0.82F + instantBpm * 0.18F;
        }
      }
    }

    hasBeatTimestamp_ = true;
    lastBeatTimestamp_ = now;
  }

  AudioMetrics metrics;
  metrics.energy = normalizedEnergy;
  metrics.bass = bassLevel;
  metrics.treble = trebleLevel;
  metrics.bpm = bpmEstimate_;
  metrics.beat = beat;

  {
    std::scoped_lock lock(metricsMutex_);
    lastMetrics_ = metrics;
  }
}

void AudioEngine::loop() {
#ifdef TUXDMX_WITH_PORTAUDIO
  if (!initPortAudio()) {
    std::scoped_lock lock(backendMutex_);
    backendName_ = "simulated-energy";
  }
#endif

  constexpr std::size_t kFramesPerTick = 256;
  std::array<float, kFramesPerTick> monoBuffer{};

  float simulationPhase = 0.0F;

  while (running_.load()) {
    bool hadAudioInput = false;

#ifdef TUXDMX_WITH_PORTAUDIO
    if (portAudioReady_) {
      PaStream* stream = static_cast<PaStream*>(stream_);
      if (stream != nullptr) {
        const PaError err = Pa_ReadStream(stream, monoBuffer.data(), kFramesPerTick);
        if (err == paNoError || err == paInputOverflowed) {
          analyzeFrame(monoBuffer.data(), monoBuffer.size());
          hadAudioInput = true;
        } else {
          shutdownPortAudio();
          std::scoped_lock lock(backendMutex_);
          backendName_ = "simulated-energy";
        }
      }
    }
#endif

    if (!hadAudioInput) {
      // Fallback oscillator + noise. This preserves a useful reactive mode even
      // when no microphone backend is available.
      for (float& sample : monoBuffer) {
        simulationPhase += 0.17F;
        if (simulationPhase > 6.2831853F) {
          simulationPhase -= 6.2831853F;
        }

        const float carrier = std::sin(simulationPhase * 2.2F);
        const float rhythm = std::sin(simulationPhase * 0.45F);
        const float noise = (dist_(rng_) - 0.5F) * 0.12F;
        sample = carrier * (0.2F + 0.3F * std::max(0.0F, rhythm)) + noise;
      }

      analyzeFrame(monoBuffer.data(), monoBuffer.size());
      std::this_thread::sleep_for(std::chrono::milliseconds(24));
    }

    if (reactiveMode_.load()) {
      TickCallback callbackCopy;
      {
        std::scoped_lock lock(callbackMutex_);
        callbackCopy = callback_;
      }
      if (callbackCopy) {
        callbackCopy(currentMetrics());
      }
    }
  }
}

}  // namespace tuxdmx

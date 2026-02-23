#pragma once

#include <atomic>
#include <chrono>
#include <filesystem>
#include <mutex>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

#include "audio_engine.hpp"
#include "database.hpp"
#include "dmx_engine.hpp"
#include "http_server.hpp"

namespace tuxdmx {

class AppController {
 public:
  AppController(std::string dbPath, std::string webRoot);
  ~AppController();

  bool initialize(std::string& error);
  void shutdown();

  HttpResponse handleRequest(const HttpRequest& request);

 private:
  HttpResponse handleApi(const HttpRequest& request);
  HttpResponse serveStatic(const std::string& path);

  HttpResponse jsonError(int status, const std::string& message);
  HttpResponse jsonOk(const std::string& payload, int status = 200);

  std::string buildStatusJson();
  std::string buildStateJson();

  void rebuildAllUniversesFromDatabase();
  void persistBlackoutToDatabase();
  void onAudioMetrics(const AudioMetrics& metrics);

  struct FixtureResolvedView {
    FixtureInstance fixture;
    std::vector<TemplateChannel> templateChannels;
  };

  struct ReactiveFixtureState {
    float hue = 0.0F;
    float smoothedEnergy = 0.0F;
    std::size_t modeCursor = 0;
  };

  std::vector<FixtureResolvedView> resolveFixtures(std::string& error);

  static std::vector<int> sortedUniverseList(const std::vector<FixtureInstance>& fixtures,
                                             const std::vector<int>& knownFromEngine);

  std::string dbPath_;
  std::filesystem::path webRoot_;

  Database db_;
  DmxEngine dmx_;
  AudioEngine audio_;

  std::mutex reactiveMutex_;
  std::mt19937 reactiveRng_;
  std::unordered_map<int, ReactiveFixtureState> reactiveStates_;
  std::chrono::steady_clock::time_point lastReactiveApply_{};
  std::atomic<float> reactiveVolumeThreshold_{0.12F};
  std::atomic<int> reactiveProfile_{0};  // 0=balanced, 1=volume_blackout
};

}  // namespace tuxdmx

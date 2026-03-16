#pragma once

#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include <sqlite3.h>

#include "models.hpp"

namespace tuxdmx {

class Database {
 public:
  explicit Database(std::string dbPath);
  ~Database();

  Database(const Database&) = delete;
  Database& operator=(const Database&) = delete;

  bool initialize(std::string& error);

  int createTemplate(const std::string& name, const std::string& description, std::string& error);
  bool resetTemplateDefinition(int templateId, const std::string& name, const std::string& description,
                               std::string& error);
  int addTemplateChannel(int templateId, int channelIndex, const std::string& name, const std::string& kind,
                         int defaultValue, std::string& error);
  int addChannelRange(int channelId, int startValue, int endValue, const std::string& label, std::string& error);
  bool updateTemplateChannel(int channelId, const std::string& name, const std::string& kind, int defaultValue,
                             std::string& error);
  bool clearChannelRanges(int channelId, std::string& error);

  std::vector<FixtureTemplate> listTemplates(std::string& error);
  std::optional<FixtureTemplate> findTemplate(int templateId, std::string& error);

  struct CreateFixtureResult {
    bool ok = false;
    int fixtureId = 0;
    int channelCount = 0;
    std::string error;
  };

  CreateFixtureResult createFixture(const std::string& name, int templateId, int universe, int startAddress,
                                    int channelCountOverride, bool allowOverlap = false);

  std::vector<FixtureInstance> listFixtures(std::string& error);
  bool storeFixtureChannelValue(int fixtureId, int channelIndex, int value, std::string& error);
  bool updateFixtureChannelValue(int fixtureId, int channelIndex, int value, ChannelPatch& patch, std::string& error);
  bool setFixtureEnabled(int fixtureId, bool enabled, std::string& error);
  bool deleteFixture(int fixtureId, std::string& error);
  bool reorderFixtures(const std::vector<int>& fixtureIds, std::string& error);

  std::vector<SceneDefinition> listScenes(std::string& error);
  int createScene(const std::string& name, float transitionSeconds, std::string& error);
  bool updateScene(int sceneId, const std::string& name, float transitionSeconds, std::string& error);
  bool captureScene(int sceneId, std::string& error);
  bool deleteScene(int sceneId, std::string& error);
  std::vector<ScenePatch> loadScenePatches(int sceneId, std::string& error);

  std::vector<MidiMapping> listMidiMappings(std::string& error);
  bool upsertMidiMapping(const MidiMapping& mapping, std::string& error);
  bool deleteMidiMapping(const std::string& controlId, std::string& error);
  bool setSetting(const std::string& key, const std::string& value, std::string& error);
  bool getSetting(const std::string& key, std::string& value, std::string& error);

  int createGroup(const std::string& name, std::string& error);
  bool setGroupFixtures(int groupId, const std::vector<int>& fixtureIds, std::string& error);
  std::vector<FixtureGroup> listGroups(std::string& error);
  bool deleteGroup(int groupId, std::string& error);

  std::vector<ChannelPatch> loadUniversePatch(int universe, std::string& error);

  bool seedAliExpressRgbPar(std::string& error);
  bool seedMiraDye(std::string& error);

 private:
  bool runMigrations(std::string& error);
  bool beginTransaction(std::string& error);
  bool commitTransaction(std::string& error);
  void rollbackTransaction();

  std::string dbPath_;
  sqlite3* db_ = nullptr;
  std::mutex mutex_;
};

}  // namespace tuxdmx

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
  bool updateFixtureChannelValue(int fixtureId, int channelIndex, int value, ChannelPatch& patch, std::string& error);
  bool setFixtureEnabled(int fixtureId, bool enabled, std::string& error);

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

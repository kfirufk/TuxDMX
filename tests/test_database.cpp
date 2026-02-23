#include <cassert>
#include <filesystem>
#include <iostream>

#include "database.hpp"

int main() {
  const auto dbPath = std::filesystem::temp_directory_path() / "tuxdmx_db_test.sqlite";
  std::error_code ec;
  std::filesystem::remove(dbPath, ec);

  tuxdmx::Database db(dbPath.string());
  std::string error;

  assert(db.initialize(error));
  assert(db.seedAliExpressRgbPar(error));

  auto templates = db.listTemplates(error);
  assert(error.empty());
  assert(!templates.empty());

  const int templateId = db.createTemplate("Unit Test Fixture", "fixture for db tests", error);
  assert(templateId > 0);

  const int ch1 = db.addTemplateChannel(templateId, 1, "Dimmer", "dimmer", 0, error);
  const int ch2 = db.addTemplateChannel(templateId, 2, "Mode", "mode", 0, error);
  assert(ch1 > 0 && ch2 > 0);

  const int rangeId = db.addChannelRange(ch2, 0, 50, "Manual", error);
  assert(rangeId > 0);

  auto fixtureResult = db.createFixture("Fixture A", templateId, 1, 1, 0, false);
  assert(fixtureResult.ok);
  assert(fixtureResult.channelCount == 2);

  auto overlapResult = db.createFixture("Fixture B", templateId, 1, 2, 0, false);
  assert(!overlapResult.ok);

  auto okOverlap = db.createFixture("Fixture C", templateId, 1, 2, 0, true);
  assert(okOverlap.ok);

  const int groupId = db.createGroup("Front Wash", error);
  assert(groupId > 0);
  assert(db.setGroupFixtures(groupId, {fixtureResult.fixtureId, okOverlap.fixtureId}, error));

  auto groups = db.listGroups(error);
  assert(error.empty());
  assert(!groups.empty());
  assert(groups[0].name == "Front Wash");
  assert(groups[0].fixtureIds.size() == 2);

  tuxdmx::ChannelPatch patch;
  assert(db.updateFixtureChannelValue(fixtureResult.fixtureId, 2, 123, patch, error));
  assert(patch.absoluteAddress == 2);
  assert(patch.value == 123);

  auto fixtures = db.listFixtures(error);
  assert(error.empty());
  assert(fixtures.size() >= 2);

  auto patches = db.loadUniversePatch(1, error);
  assert(error.empty());
  assert(!patches.empty());

  assert(db.deleteGroup(groupId, error));

  std::cout << "tuxdmx_db_tests passed\n";
  return 0;
}

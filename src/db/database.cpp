#include "database.hpp"

#include <algorithm>
#include <filesystem>
#include <sstream>
#include <tuple>

#include "utils.hpp"

namespace tuxdmx {

namespace {

bool execSql(sqlite3* db, const char* sql, std::string& error) {
  char* rawError = nullptr;
  const int rc = sqlite3_exec(db, sql, nullptr, nullptr, &rawError);
  if (rc != SQLITE_OK) {
    error = rawError != nullptr ? rawError : "sqlite exec failed";
    sqlite3_free(rawError);
    return false;
  }
  return true;
}

struct Statement {
  sqlite3_stmt* stmt = nullptr;

  ~Statement() {
    if (stmt != nullptr) {
      sqlite3_finalize(stmt);
    }
  }
};

bool prepare(sqlite3* db, const char* sql, Statement& statement, std::string& error) {
  const int rc = sqlite3_prepare_v2(db, sql, -1, &statement.stmt, nullptr);
  if (rc != SQLITE_OK) {
    error = sqlite3_errmsg(db);
    return false;
  }
  return true;
}

int columnInt(sqlite3_stmt* stmt, int col) {
  return sqlite3_column_int(stmt, col);
}

std::string columnText(sqlite3_stmt* stmt, int col) {
  const auto* text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, col));
  return text == nullptr ? "" : std::string(text);
}

bool tableHasColumn(sqlite3* db, const char* tableName, const char* columnName, std::string& error) {
  Statement stmt;
  const std::string sql = std::string("PRAGMA table_info(") + tableName + ");";
  if (!prepare(db, sql.c_str(), stmt, error)) {
    return false;
  }

  while (sqlite3_step(stmt.stmt) == SQLITE_ROW) {
    if (columnText(stmt.stmt, 1) == columnName) {
      return true;
    }
  }

  return false;
}

bool normalizeFixtureSortOrder(sqlite3* db, std::string& error) {
  Statement selectStmt;
  if (!prepare(db, "SELECT id FROM fixtures ORDER BY sort_order, id;", selectStmt, error)) {
    return false;
  }

  std::vector<int> ids;
  while (sqlite3_step(selectStmt.stmt) == SQLITE_ROW) {
    ids.push_back(columnInt(selectStmt.stmt, 0));
  }

  Statement updateStmt;
  if (!prepare(db, "UPDATE fixtures SET sort_order = ? WHERE id = ?;", updateStmt, error)) {
    return false;
  }

  int sortOrder = 1;
  for (int id : ids) {
    sqlite3_reset(updateStmt.stmt);
    sqlite3_clear_bindings(updateStmt.stmt);
    sqlite3_bind_int(updateStmt.stmt, 1, sortOrder++);
    sqlite3_bind_int(updateStmt.stmt, 2, id);
    if (sqlite3_step(updateStmt.stmt) != SQLITE_DONE) {
      error = sqlite3_errmsg(db);
      return false;
    }
  }

  return true;
}

struct SeedRange {
  int startValue = 0;
  int endValue = 0;
  const char* label = "";
};

struct SeedChannel {
  int idx = 1;
  const char* name = "";
  const char* kind = "generic";
  int defaultValue = 0;
  std::vector<SeedRange> ranges;
};

int findTemplateIdByName(sqlite3* db, const char* name, std::string& error) {
  Statement stmt;
  if (!prepare(db, "SELECT id FROM fixture_templates WHERE name = ?;", stmt, error)) {
    return 0;
  }
  sqlite3_bind_text(stmt.stmt, 1, name, -1, SQLITE_TRANSIENT);
  if (sqlite3_step(stmt.stmt) == SQLITE_ROW) {
    return columnInt(stmt.stmt, 0);
  }
  return 0;
}

bool updateTemplateMetadata(sqlite3* db, int templateId, const std::string& description, int footprint,
                            std::string& error) {
  Statement stmt;
  if (!prepare(db, "UPDATE fixture_templates SET description = ?, footprint_channels = ? WHERE id = ?;", stmt, error)) {
    return false;
  }
  sqlite3_bind_text(stmt.stmt, 1, description.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int(stmt.stmt, 2, footprint);
  sqlite3_bind_int(stmt.stmt, 3, templateId);
  if (sqlite3_step(stmt.stmt) != SQLITE_DONE) {
    error = sqlite3_errmsg(db);
    return false;
  }
  return true;
}

bool clearTemplateChannels(sqlite3* db, int templateId, std::string& error) {
  Statement stmt;
  if (!prepare(db, "DELETE FROM template_channels WHERE template_id = ?;", stmt, error)) {
    return false;
  }
  sqlite3_bind_int(stmt.stmt, 1, templateId);
  if (sqlite3_step(stmt.stmt) != SQLITE_DONE) {
    error = sqlite3_errmsg(db);
    return false;
  }
  return true;
}

bool insertTemplateChannels(sqlite3* db, int templateId, const std::vector<SeedChannel>& channels, std::string& error) {
  Statement channelStmt;
  if (!prepare(db,
               "INSERT INTO template_channels(template_id, channel_index, name, kind, default_value) VALUES(?, ?, ?, ?, ?);",
               channelStmt, error)) {
    return false;
  }

  Statement rangeStmt;
  if (!prepare(db, "INSERT INTO template_channel_ranges(channel_id, start_value, end_value, label) VALUES(?, ?, ?, ?);",
               rangeStmt, error)) {
    return false;
  }

  int maxChannel = 0;
  for (const auto& c : channels) {
    sqlite3_reset(channelStmt.stmt);
    sqlite3_clear_bindings(channelStmt.stmt);
    sqlite3_bind_int(channelStmt.stmt, 1, templateId);
    sqlite3_bind_int(channelStmt.stmt, 2, c.idx);
    sqlite3_bind_text(channelStmt.stmt, 3, c.name, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(channelStmt.stmt, 4, c.kind, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(channelStmt.stmt, 5, clampDmx(c.defaultValue));
    if (sqlite3_step(channelStmt.stmt) != SQLITE_DONE) {
      error = sqlite3_errmsg(db);
      return false;
    }

    maxChannel = std::max(maxChannel, c.idx);
    const int channelId = static_cast<int>(sqlite3_last_insert_rowid(db));
    for (const auto& r : c.ranges) {
      sqlite3_reset(rangeStmt.stmt);
      sqlite3_clear_bindings(rangeStmt.stmt);
      sqlite3_bind_int(rangeStmt.stmt, 1, channelId);
      sqlite3_bind_int(rangeStmt.stmt, 2, clampDmx(r.startValue));
      sqlite3_bind_int(rangeStmt.stmt, 3, clampDmx(r.endValue));
      sqlite3_bind_text(rangeStmt.stmt, 4, r.label, -1, SQLITE_TRANSIENT);
      if (sqlite3_step(rangeStmt.stmt) != SQLITE_DONE) {
        error = sqlite3_errmsg(db);
        return false;
      }
    }
  }

  Statement footprintStmt;
  if (!prepare(db, "UPDATE fixture_templates SET footprint_channels = ? WHERE id = ?;", footprintStmt, error)) {
    return false;
  }
  sqlite3_bind_int(footprintStmt.stmt, 1, maxChannel);
  sqlite3_bind_int(footprintStmt.stmt, 2, templateId);
  if (sqlite3_step(footprintStmt.stmt) != SQLITE_DONE) {
    error = sqlite3_errmsg(db);
    return false;
  }

  return true;
}

bool templateChannelsMatch(sqlite3* db, int templateId, const std::vector<SeedChannel>& channels, std::string& error) {
  Statement channelStmt;
  if (!prepare(db,
               "SELECT id, channel_index, name, kind, default_value FROM template_channels "
               "WHERE template_id = ? ORDER BY channel_index;",
               channelStmt, error)) {
    return false;
  }
  sqlite3_bind_int(channelStmt.stmt, 1, templateId);

  std::size_t channelCursor = 0;
  while (sqlite3_step(channelStmt.stmt) == SQLITE_ROW) {
    if (channelCursor >= channels.size()) {
      return false;
    }

    const auto& expected = channels[channelCursor];
    const int channelId = columnInt(channelStmt.stmt, 0);
    const int channelIndex = columnInt(channelStmt.stmt, 1);
    const std::string channelName = columnText(channelStmt.stmt, 2);
    const std::string channelKind = columnText(channelStmt.stmt, 3);
    const int channelDefault = columnInt(channelStmt.stmt, 4);
    if (channelIndex != expected.idx || channelName != expected.name || channelKind != expected.kind
        || channelDefault != clampDmx(expected.defaultValue)) {
      return false;
    }

    Statement rangeStmt;
    if (!prepare(db,
                 "SELECT start_value, end_value, label FROM template_channel_ranges WHERE channel_id = ? "
                 "ORDER BY start_value, end_value, id;",
                 rangeStmt, error)) {
      return false;
    }
    sqlite3_bind_int(rangeStmt.stmt, 1, channelId);

    std::size_t rangeCursor = 0;
    while (sqlite3_step(rangeStmt.stmt) == SQLITE_ROW) {
      if (rangeCursor >= expected.ranges.size()) {
        return false;
      }

      const auto& expectedRange = expected.ranges[rangeCursor];
      const int startValue = columnInt(rangeStmt.stmt, 0);
      const int endValue = columnInt(rangeStmt.stmt, 1);
      const std::string label = columnText(rangeStmt.stmt, 2);
      if (startValue != clampDmx(expectedRange.startValue) || endValue != clampDmx(expectedRange.endValue)
          || label != expectedRange.label) {
        return false;
      }
      ++rangeCursor;
    }

    if (rangeCursor != expected.ranges.size()) {
      return false;
    }

    ++channelCursor;
  }

  return channelCursor == channels.size();
}

}  // namespace

Database::Database(std::string dbPath) : dbPath_(std::move(dbPath)) {}

Database::~Database() {
  if (db_ != nullptr) {
    sqlite3_close(db_);
    db_ = nullptr;
  }
}

bool Database::initialize(std::string& error) {
  std::scoped_lock lock(mutex_);

  const auto dbFile = std::filesystem::path(dbPath_);
  if (!dbFile.parent_path().empty()) {
    std::error_code ec;
    std::filesystem::create_directories(dbFile.parent_path(), ec);
    if (ec) {
      error = "Failed to create DB directory: " + ec.message();
      return false;
    }
  }

  const int rc = sqlite3_open(dbPath_.c_str(), &db_);
  if (rc != SQLITE_OK || db_ == nullptr) {
    error = db_ != nullptr ? sqlite3_errmsg(db_) : "sqlite open failed";
    return false;
  }

  if (!execSql(db_, "PRAGMA foreign_keys = ON;", error)) {
    return false;
  }
  if (!execSql(db_, "PRAGMA journal_mode = WAL;", error)) {
    return false;
  }

  return runMigrations(error);
}

bool Database::runMigrations(std::string& error) {
  static constexpr const char* kSchemaSql = R"SQL(
CREATE TABLE IF NOT EXISTS fixture_templates (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  name TEXT NOT NULL UNIQUE,
  description TEXT NOT NULL DEFAULT '',
  footprint_channels INTEGER NOT NULL DEFAULT 0,
  created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE IF NOT EXISTS template_channels (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  template_id INTEGER NOT NULL,
  channel_index INTEGER NOT NULL,
  name TEXT NOT NULL,
  kind TEXT NOT NULL DEFAULT 'generic',
  default_value INTEGER NOT NULL DEFAULT 0,
  created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
  UNIQUE(template_id, channel_index),
  FOREIGN KEY (template_id) REFERENCES fixture_templates(id) ON DELETE CASCADE
);

CREATE TABLE IF NOT EXISTS template_channel_ranges (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  channel_id INTEGER NOT NULL,
  start_value INTEGER NOT NULL,
  end_value INTEGER NOT NULL,
  label TEXT NOT NULL,
  created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
  FOREIGN KEY (channel_id) REFERENCES template_channels(id) ON DELETE CASCADE
);

CREATE TABLE IF NOT EXISTS fixtures (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  name TEXT NOT NULL,
  template_id INTEGER NOT NULL,
  universe INTEGER NOT NULL DEFAULT 1,
  start_address INTEGER NOT NULL,
  channel_count INTEGER NOT NULL,
  sort_order INTEGER NOT NULL DEFAULT 0,
  enabled INTEGER NOT NULL DEFAULT 1,
  created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
  FOREIGN KEY (template_id) REFERENCES fixture_templates(id)
);

CREATE TABLE IF NOT EXISTS fixture_channel_values (
  fixture_id INTEGER NOT NULL,
  channel_index INTEGER NOT NULL,
  value INTEGER NOT NULL DEFAULT 0,
  PRIMARY KEY(fixture_id, channel_index),
  FOREIGN KEY (fixture_id) REFERENCES fixtures(id) ON DELETE CASCADE
);

CREATE TABLE IF NOT EXISTS fixture_groups (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  name TEXT NOT NULL UNIQUE,
  created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE IF NOT EXISTS fixture_group_members (
  group_id INTEGER NOT NULL,
  fixture_id INTEGER NOT NULL,
  PRIMARY KEY(group_id, fixture_id),
  FOREIGN KEY (group_id) REFERENCES fixture_groups(id) ON DELETE CASCADE,
  FOREIGN KEY (fixture_id) REFERENCES fixtures(id) ON DELETE CASCADE
);

CREATE TABLE IF NOT EXISTS scenes (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  name TEXT NOT NULL UNIQUE,
  transition_seconds REAL NOT NULL DEFAULT 1.0,
  created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE IF NOT EXISTS scene_values (
  scene_id INTEGER NOT NULL,
  fixture_id INTEGER NOT NULL,
  channel_index INTEGER NOT NULL,
  value INTEGER NOT NULL,
  PRIMARY KEY(scene_id, fixture_id, channel_index),
  FOREIGN KEY (scene_id) REFERENCES scenes(id) ON DELETE CASCADE,
  FOREIGN KEY (fixture_id) REFERENCES fixtures(id) ON DELETE CASCADE
);

CREATE TABLE IF NOT EXISTS app_settings (
  key TEXT PRIMARY KEY,
  value TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS midi_mappings (
  control_id TEXT PRIMARY KEY,
  source TEXT NOT NULL DEFAULT 'all',
  input_id TEXT NOT NULL DEFAULT '',
  type TEXT NOT NULL,
  channel INTEGER NOT NULL,
  number INTEGER NOT NULL,
  created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE INDEX IF NOT EXISTS idx_fixtures_universe_start ON fixtures(universe, start_address);
CREATE INDEX IF NOT EXISTS idx_template_channels_template ON template_channels(template_id);
CREATE INDEX IF NOT EXISTS idx_fixture_channel_values_fixture ON fixture_channel_values(fixture_id);
CREATE INDEX IF NOT EXISTS idx_scene_values_scene ON scene_values(scene_id);
)SQL";

  if (!execSql(db_, kSchemaSql, error)) {
    return false;
  }

  const bool hasSortOrder = tableHasColumn(db_, "fixtures", "sort_order", error);
  if (!error.empty()) {
    return false;
  }

  if (!hasSortOrder) {
    if (!execSql(db_, "ALTER TABLE fixtures ADD COLUMN sort_order INTEGER NOT NULL DEFAULT 0;", error)) {
      return false;
    }
  }

  // Backfill/compact sort order in deterministic fixture creation order.
  return normalizeFixtureSortOrder(db_, error);
}

bool Database::beginTransaction(std::string& error) {
  return execSql(db_, "BEGIN IMMEDIATE TRANSACTION;", error);
}

bool Database::commitTransaction(std::string& error) {
  return execSql(db_, "COMMIT;", error);
}

void Database::rollbackTransaction() {
  std::string ignore;
  execSql(db_, "ROLLBACK;", ignore);
}

int Database::createTemplate(const std::string& name, const std::string& description, std::string& error) {
  std::scoped_lock lock(mutex_);

  Statement stmt;
  if (!prepare(db_, "INSERT INTO fixture_templates(name, description) VALUES(?, ?);", stmt, error)) {
    return 0;
  }

  sqlite3_bind_text(stmt.stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt.stmt, 2, description.c_str(), -1, SQLITE_TRANSIENT);

  if (sqlite3_step(stmt.stmt) != SQLITE_DONE) {
    error = sqlite3_errmsg(db_);
    return 0;
  }

  return static_cast<int>(sqlite3_last_insert_rowid(db_));
}

bool Database::resetTemplateDefinition(int templateId, const std::string& name, const std::string& description,
                                       std::string& error) {
  std::scoped_lock lock(mutex_);

  error.clear();
  if (!beginTransaction(error)) {
    return false;
  }

  Statement updateStmt;
  if (!prepare(db_, "UPDATE fixture_templates SET name = ?, description = ?, footprint_channels = 0 WHERE id = ?;",
               updateStmt, error)) {
    rollbackTransaction();
    return false;
  }

  sqlite3_bind_text(updateStmt.stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(updateStmt.stmt, 2, description.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int(updateStmt.stmt, 3, templateId);

  if (sqlite3_step(updateStmt.stmt) != SQLITE_DONE) {
    error = sqlite3_errmsg(db_);
    rollbackTransaction();
    return false;
  }

  if (sqlite3_changes(db_) == 0) {
    error = "Template not found";
    rollbackTransaction();
    return false;
  }

  Statement clearStmt;
  if (!prepare(db_, "DELETE FROM template_channels WHERE template_id = ?;", clearStmt, error)) {
    rollbackTransaction();
    return false;
  }

  sqlite3_bind_int(clearStmt.stmt, 1, templateId);
  if (sqlite3_step(clearStmt.stmt) != SQLITE_DONE) {
    error = sqlite3_errmsg(db_);
    rollbackTransaction();
    return false;
  }

  if (!commitTransaction(error)) {
    rollbackTransaction();
    return false;
  }

  return true;
}

int Database::addTemplateChannel(int templateId, int channelIndex, const std::string& name, const std::string& kind,
                                 int defaultValue, std::string& error) {
  std::scoped_lock lock(mutex_);

  Statement insertStmt;
  if (!prepare(db_,
               "INSERT INTO template_channels(template_id, channel_index, name, kind, default_value) VALUES(?, ?, ?, ?, ?);",
               insertStmt, error)) {
    return 0;
  }

  sqlite3_bind_int(insertStmt.stmt, 1, templateId);
  sqlite3_bind_int(insertStmt.stmt, 2, channelIndex);
  sqlite3_bind_text(insertStmt.stmt, 3, name.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(insertStmt.stmt, 4, kind.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int(insertStmt.stmt, 5, clampDmx(defaultValue));

  if (sqlite3_step(insertStmt.stmt) != SQLITE_DONE) {
    error = sqlite3_errmsg(db_);
    return 0;
  }

  Statement updateStmt;
  if (!prepare(db_,
               "UPDATE fixture_templates SET footprint_channels = MAX(footprint_channels, ?) WHERE id = ?;",
               updateStmt, error)) {
    return 0;
  }

  sqlite3_bind_int(updateStmt.stmt, 1, channelIndex);
  sqlite3_bind_int(updateStmt.stmt, 2, templateId);

  if (sqlite3_step(updateStmt.stmt) != SQLITE_DONE) {
    error = sqlite3_errmsg(db_);
    return 0;
  }

  return static_cast<int>(sqlite3_last_insert_rowid(db_));
}

int Database::addChannelRange(int channelId, int startValue, int endValue, const std::string& label, std::string& error) {
  std::scoped_lock lock(mutex_);

  Statement stmt;
  if (!prepare(db_,
               "INSERT INTO template_channel_ranges(channel_id, start_value, end_value, label) VALUES(?, ?, ?, ?);",
               stmt, error)) {
    return 0;
  }

  sqlite3_bind_int(stmt.stmt, 1, channelId);
  sqlite3_bind_int(stmt.stmt, 2, clampDmx(startValue));
  sqlite3_bind_int(stmt.stmt, 3, clampDmx(endValue));
  sqlite3_bind_text(stmt.stmt, 4, label.c_str(), -1, SQLITE_TRANSIENT);

  if (sqlite3_step(stmt.stmt) != SQLITE_DONE) {
    error = sqlite3_errmsg(db_);
    return 0;
  }

  return static_cast<int>(sqlite3_last_insert_rowid(db_));
}

bool Database::updateTemplateChannel(int channelId, const std::string& name, const std::string& kind, int defaultValue,
                                     std::string& error) {
  std::scoped_lock lock(mutex_);

  Statement stmt;
  if (!prepare(db_, "UPDATE template_channels SET name = ?, kind = ?, default_value = ? WHERE id = ?;", stmt, error)) {
    return false;
  }

  sqlite3_bind_text(stmt.stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt.stmt, 2, kind.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int(stmt.stmt, 3, clampDmx(defaultValue));
  sqlite3_bind_int(stmt.stmt, 4, channelId);

  if (sqlite3_step(stmt.stmt) != SQLITE_DONE) {
    error = sqlite3_errmsg(db_);
    return false;
  }

  if (sqlite3_changes(db_) == 0) {
    error = "Template channel not found";
    return false;
  }

  return true;
}

bool Database::clearChannelRanges(int channelId, std::string& error) {
  std::scoped_lock lock(mutex_);

  Statement stmt;
  if (!prepare(db_, "DELETE FROM template_channel_ranges WHERE channel_id = ?;", stmt, error)) {
    return false;
  }

  sqlite3_bind_int(stmt.stmt, 1, channelId);
  if (sqlite3_step(stmt.stmt) != SQLITE_DONE) {
    error = sqlite3_errmsg(db_);
    return false;
  }

  return true;
}

std::vector<FixtureTemplate> Database::listTemplates(std::string& error) {
  std::scoped_lock lock(mutex_);

  std::vector<FixtureTemplate> templates;

  Statement templateStmt;
  if (!prepare(db_,
               "SELECT id, name, description, footprint_channels FROM fixture_templates ORDER BY name COLLATE NOCASE;",
               templateStmt, error)) {
    return templates;
  }

  while (sqlite3_step(templateStmt.stmt) == SQLITE_ROW) {
    FixtureTemplate t;
    t.id = columnInt(templateStmt.stmt, 0);
    t.name = columnText(templateStmt.stmt, 1);
    t.description = columnText(templateStmt.stmt, 2);
    t.footprintChannels = columnInt(templateStmt.stmt, 3);
    templates.push_back(std::move(t));
  }

  for (auto& t : templates) {
    Statement channelStmt;
    if (!prepare(db_,
                 "SELECT id, channel_index, name, kind, default_value FROM template_channels WHERE template_id = ? "
                 "ORDER BY channel_index;",
                 channelStmt, error)) {
      return {};
    }
    sqlite3_bind_int(channelStmt.stmt, 1, t.id);

    while (sqlite3_step(channelStmt.stmt) == SQLITE_ROW) {
      TemplateChannel c;
      c.id = columnInt(channelStmt.stmt, 0);
      c.channelIndex = columnInt(channelStmt.stmt, 1);
      c.name = columnText(channelStmt.stmt, 2);
      c.kind = columnText(channelStmt.stmt, 3);
      c.defaultValue = columnInt(channelStmt.stmt, 4);
      t.channels.push_back(std::move(c));
    }

    for (auto& c : t.channels) {
      Statement rangeStmt;
      if (!prepare(db_,
                   "SELECT id, start_value, end_value, label FROM template_channel_ranges WHERE channel_id = ? "
                   "ORDER BY start_value;",
                   rangeStmt, error)) {
        return {};
      }
      sqlite3_bind_int(rangeStmt.stmt, 1, c.id);

      while (sqlite3_step(rangeStmt.stmt) == SQLITE_ROW) {
        ChannelRange r;
        r.id = columnInt(rangeStmt.stmt, 0);
        r.startValue = columnInt(rangeStmt.stmt, 1);
        r.endValue = columnInt(rangeStmt.stmt, 2);
        r.label = columnText(rangeStmt.stmt, 3);
        c.ranges.push_back(std::move(r));
      }
    }
  }

  return templates;
}

std::optional<FixtureTemplate> Database::findTemplate(int templateId, std::string& error) {
  auto templates = listTemplates(error);
  if (!error.empty()) {
    return std::nullopt;
  }
  for (auto& t : templates) {
    if (t.id == templateId) {
      return t;
    }
  }
  return std::nullopt;
}

Database::CreateFixtureResult Database::createFixture(const std::string& name, int templateId, int universe, int startAddress,
                                                      int channelCountOverride, bool allowOverlap) {
  std::scoped_lock lock(mutex_);

  CreateFixtureResult result;

  if (universe < 1) {
    result.error = "Universe must be >= 1";
    return result;
  }

  if (startAddress < 1 || startAddress > 512) {
    result.error = "Start address must be between 1 and 512";
    return result;
  }

  int footprint = 0;
  {
    Statement stmt;
    if (!prepare(db_, "SELECT footprint_channels FROM fixture_templates WHERE id = ?;", stmt, result.error)) {
      return result;
    }
    sqlite3_bind_int(stmt.stmt, 1, templateId);
    if (sqlite3_step(stmt.stmt) == SQLITE_ROW) {
      footprint = columnInt(stmt.stmt, 0);
    } else {
      result.error = "Template not found";
      return result;
    }
  }

  const int channelCount = channelCountOverride > 0 ? channelCountOverride : footprint;
  if (channelCount <= 0) {
    result.error = "Channel count must be greater than zero";
    return result;
  }

  const int endAddress = startAddress + channelCount - 1;
  if (endAddress > 512) {
    result.error = "Fixture footprint exceeds DMX universe (end address > 512)";
    return result;
  }

  int nextSortOrder = 1;
  {
    Statement orderStmt;
    if (!prepare(db_, "SELECT COALESCE(MAX(sort_order), 0) + 1 FROM fixtures;", orderStmt, result.error)) {
      return result;
    }
    if (sqlite3_step(orderStmt.stmt) == SQLITE_ROW) {
      nextSortOrder = std::max(1, columnInt(orderStmt.stmt, 0));
    }
  }

  if (!allowOverlap) {
    Statement overlapStmt;
    if (!prepare(db_,
                 "SELECT COUNT(*) FROM fixtures WHERE universe = ? AND enabled = 1 "
                 "AND NOT (? > start_address + channel_count - 1 OR ? < start_address);",
                 overlapStmt, result.error)) {
      return result;
    }
    sqlite3_bind_int(overlapStmt.stmt, 1, universe);
    sqlite3_bind_int(overlapStmt.stmt, 2, startAddress);
    sqlite3_bind_int(overlapStmt.stmt, 3, endAddress);

    if (sqlite3_step(overlapStmt.stmt) == SQLITE_ROW) {
      if (columnInt(overlapStmt.stmt, 0) > 0) {
        result.error = "Address range overlaps another enabled fixture";
        return result;
      }
    }
  }

  if (!beginTransaction(result.error)) {
    return result;
  }

  int fixtureId = 0;
  do {
    Statement insertFixtureStmt;
    if (!prepare(db_,
                 "INSERT INTO fixtures(name, template_id, universe, start_address, channel_count, sort_order, enabled) "
                 "VALUES(?, ?, ?, ?, ?, ?, 1);",
                 insertFixtureStmt, result.error)) {
      break;
    }

    sqlite3_bind_text(insertFixtureStmt.stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(insertFixtureStmt.stmt, 2, templateId);
    sqlite3_bind_int(insertFixtureStmt.stmt, 3, universe);
    sqlite3_bind_int(insertFixtureStmt.stmt, 4, startAddress);
    sqlite3_bind_int(insertFixtureStmt.stmt, 5, channelCount);
    sqlite3_bind_int(insertFixtureStmt.stmt, 6, nextSortOrder);

    if (sqlite3_step(insertFixtureStmt.stmt) != SQLITE_DONE) {
      result.error = sqlite3_errmsg(db_);
      break;
    }

    fixtureId = static_cast<int>(sqlite3_last_insert_rowid(db_));

    Statement defaultsStmt;
    if (!prepare(db_, "SELECT channel_index, default_value FROM template_channels WHERE template_id = ?;", defaultsStmt,
                 result.error)) {
      break;
    }
    sqlite3_bind_int(defaultsStmt.stmt, 1, templateId);

    std::vector<int> defaults(channelCount + 1, 0);
    while (sqlite3_step(defaultsStmt.stmt) == SQLITE_ROW) {
      const int idx = columnInt(defaultsStmt.stmt, 0);
      if (idx >= 1 && idx <= channelCount) {
        defaults[idx] = clampDmx(columnInt(defaultsStmt.stmt, 1));
      }
    }

    Statement insertValueStmt;
    if (!prepare(db_,
                 "INSERT INTO fixture_channel_values(fixture_id, channel_index, value) VALUES(?, ?, ?);",
                 insertValueStmt, result.error)) {
      break;
    }

    for (int i = 1; i <= channelCount; ++i) {
      sqlite3_reset(insertValueStmt.stmt);
      sqlite3_clear_bindings(insertValueStmt.stmt);
      sqlite3_bind_int(insertValueStmt.stmt, 1, fixtureId);
      sqlite3_bind_int(insertValueStmt.stmt, 2, i);
      sqlite3_bind_int(insertValueStmt.stmt, 3, defaults[i]);
      if (sqlite3_step(insertValueStmt.stmt) != SQLITE_DONE) {
        result.error = sqlite3_errmsg(db_);
        break;
      }
    }

    if (!result.error.empty()) {
      break;
    }

    if (!commitTransaction(result.error)) {
      break;
    }

    result.ok = true;
    result.fixtureId = fixtureId;
    result.channelCount = channelCount;
    return result;

  } while (false);

  rollbackTransaction();
  return result;
}

std::vector<FixtureInstance> Database::listFixtures(std::string& error) {
  std::scoped_lock lock(mutex_);

  std::vector<FixtureInstance> fixtures;

  Statement fixtureStmt;
  if (!prepare(db_,
               "SELECT f.id, f.name, f.template_id, t.name, f.universe, f.start_address, f.channel_count, "
               "f.sort_order, f.enabled "
               "FROM fixtures f JOIN fixture_templates t ON t.id = f.template_id "
               "ORDER BY f.sort_order, f.id;",
               fixtureStmt, error)) {
    return fixtures;
  }

  while (sqlite3_step(fixtureStmt.stmt) == SQLITE_ROW) {
    FixtureInstance f;
    f.id = columnInt(fixtureStmt.stmt, 0);
    f.name = columnText(fixtureStmt.stmt, 1);
    f.templateId = columnInt(fixtureStmt.stmt, 2);
    f.templateName = columnText(fixtureStmt.stmt, 3);
    f.universe = columnInt(fixtureStmt.stmt, 4);
    f.startAddress = columnInt(fixtureStmt.stmt, 5);
    f.channelCount = columnInt(fixtureStmt.stmt, 6);
    f.sortOrder = columnInt(fixtureStmt.stmt, 7);
    f.enabled = columnInt(fixtureStmt.stmt, 8) != 0;
    fixtures.push_back(std::move(f));
  }

  for (auto& fixture : fixtures) {
    Statement valuesStmt;
    if (!prepare(db_,
                 "SELECT channel_index, value FROM fixture_channel_values WHERE fixture_id = ? ORDER BY channel_index;",
                 valuesStmt, error)) {
      return {};
    }

    sqlite3_bind_int(valuesStmt.stmt, 1, fixture.id);
    while (sqlite3_step(valuesStmt.stmt) == SQLITE_ROW) {
      const int idx = columnInt(valuesStmt.stmt, 0);
      const int value = columnInt(valuesStmt.stmt, 1);
      fixture.channelValues[idx] = value;
    }
  }

  return fixtures;
}

bool Database::updateFixtureChannelValue(int fixtureId, int channelIndex, int value, ChannelPatch& patch, std::string& error) {
  std::scoped_lock lock(mutex_);

  int universe = 1;
  int startAddress = 1;
  int channelCount = 0;

  {
    Statement fixtureStmt;
    if (!prepare(db_, "SELECT universe, start_address, channel_count FROM fixtures WHERE id = ?;", fixtureStmt, error)) {
      return false;
    }
    sqlite3_bind_int(fixtureStmt.stmt, 1, fixtureId);
    if (sqlite3_step(fixtureStmt.stmt) != SQLITE_ROW) {
      error = "Fixture not found";
      return false;
    }

    universe = columnInt(fixtureStmt.stmt, 0);
    startAddress = columnInt(fixtureStmt.stmt, 1);
    channelCount = columnInt(fixtureStmt.stmt, 2);
  }

  if (channelIndex < 1 || channelIndex > channelCount) {
    error = "Channel index out of fixture range";
    return false;
  }

  Statement updateStmt;
  if (!prepare(db_, "UPDATE fixture_channel_values SET value = ? WHERE fixture_id = ? AND channel_index = ?;", updateStmt,
               error)) {
    return false;
  }

  sqlite3_bind_int(updateStmt.stmt, 1, clampDmx(value));
  sqlite3_bind_int(updateStmt.stmt, 2, fixtureId);
  sqlite3_bind_int(updateStmt.stmt, 3, channelIndex);

  if (sqlite3_step(updateStmt.stmt) != SQLITE_DONE) {
    error = sqlite3_errmsg(db_);
    return false;
  }

  if (sqlite3_changes(db_) == 0) {
    error = "Channel value row not found";
    return false;
  }

  patch.universe = universe;
  patch.absoluteAddress = startAddress + channelIndex - 1;
  patch.value = clampDmx(value);

  return true;
}

bool Database::setFixtureEnabled(int fixtureId, bool enabled, std::string& error) {
  std::scoped_lock lock(mutex_);

  Statement stmt;
  if (!prepare(db_, "UPDATE fixtures SET enabled = ? WHERE id = ?;", stmt, error)) {
    return false;
  }

  sqlite3_bind_int(stmt.stmt, 1, enabled ? 1 : 0);
  sqlite3_bind_int(stmt.stmt, 2, fixtureId);

  if (sqlite3_step(stmt.stmt) != SQLITE_DONE) {
    error = sqlite3_errmsg(db_);
    return false;
  }

  if (sqlite3_changes(db_) == 0) {
    error = "Fixture not found";
    return false;
  }

  return true;
}

bool Database::deleteFixture(int fixtureId, std::string& error) {
  std::scoped_lock lock(mutex_);

  if (!beginTransaction(error)) {
    return false;
  }

  do {
    Statement stmt;
    if (!prepare(db_, "DELETE FROM fixtures WHERE id = ?;", stmt, error)) {
      break;
    }
    sqlite3_bind_int(stmt.stmt, 1, fixtureId);
    if (sqlite3_step(stmt.stmt) != SQLITE_DONE) {
      error = sqlite3_errmsg(db_);
      break;
    }
    if (sqlite3_changes(db_) == 0) {
      error = "Fixture not found";
      break;
    }

    if (!normalizeFixtureSortOrder(db_, error)) {
      break;
    }

    if (!commitTransaction(error)) {
      break;
    }
    return true;
  } while (false);

  rollbackTransaction();
  return false;
}

bool Database::reorderFixtures(const std::vector<int>& fixtureIds, std::string& error) {
  std::scoped_lock lock(mutex_);

  std::vector<int> existingIds;
  {
    Statement stmt;
    if (!prepare(db_, "SELECT id FROM fixtures ORDER BY sort_order, id;", stmt, error)) {
      return false;
    }
    while (sqlite3_step(stmt.stmt) == SQLITE_ROW) {
      existingIds.push_back(columnInt(stmt.stmt, 0));
    }
  }

  if (existingIds.empty()) {
    return true;
  }

  std::vector<int> requested = fixtureIds;
  std::sort(requested.begin(), requested.end());
  requested.erase(std::unique(requested.begin(), requested.end()), requested.end());

  std::vector<int> expected = existingIds;
  std::sort(expected.begin(), expected.end());

  if (requested != expected || fixtureIds.size() != expected.size()) {
    error = "Fixture reorder list must include all fixture ids exactly once";
    return false;
  }

  if (!beginTransaction(error)) {
    return false;
  }

  do {
    Statement updateStmt;
    if (!prepare(db_, "UPDATE fixtures SET sort_order = ? WHERE id = ?;", updateStmt, error)) {
      break;
    }

    int sortOrder = 1;
    for (int fixtureId : fixtureIds) {
      sqlite3_reset(updateStmt.stmt);
      sqlite3_clear_bindings(updateStmt.stmt);
      sqlite3_bind_int(updateStmt.stmt, 1, sortOrder++);
      sqlite3_bind_int(updateStmt.stmt, 2, fixtureId);
      if (sqlite3_step(updateStmt.stmt) != SQLITE_DONE) {
        error = sqlite3_errmsg(db_);
        break;
      }
      if (sqlite3_changes(db_) == 0) {
        error = "Fixture id " + std::to_string(fixtureId) + " not found";
        break;
      }
    }

    if (!error.empty()) {
      break;
    }

    if (!commitTransaction(error)) {
      break;
    }
    return true;
  } while (false);

  rollbackTransaction();
  return false;
}

std::vector<SceneDefinition> Database::listScenes(std::string& error) {
  std::scoped_lock lock(mutex_);

  std::vector<SceneDefinition> scenes;

  Statement stmt;
  if (!prepare(db_,
               "SELECT s.id, s.name, s.transition_seconds, "
               "(SELECT COUNT(*) FROM scene_values sv WHERE sv.scene_id = s.id) AS value_count "
               "FROM scenes s ORDER BY s.name COLLATE NOCASE;",
               stmt, error)) {
    return scenes;
  }

  while (sqlite3_step(stmt.stmt) == SQLITE_ROW) {
    SceneDefinition scene;
    scene.id = columnInt(stmt.stmt, 0);
    scene.name = columnText(stmt.stmt, 1);
    scene.transitionSeconds = static_cast<float>(sqlite3_column_double(stmt.stmt, 2));
    scene.valueCount = columnInt(stmt.stmt, 3);
    scenes.push_back(std::move(scene));
  }

  return scenes;
}

int Database::createScene(const std::string& name, float transitionSeconds, std::string& error) {
  std::scoped_lock lock(mutex_);

  const float clampedSeconds = std::clamp(transitionSeconds, 0.0F, 60.0F);

  if (!beginTransaction(error)) {
    return 0;
  }

  int sceneId = 0;
  do {
    Statement sceneStmt;
    if (!prepare(db_, "INSERT INTO scenes(name, transition_seconds) VALUES(?, ?);", sceneStmt, error)) {
      break;
    }
    sqlite3_bind_text(sceneStmt.stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(sceneStmt.stmt, 2, static_cast<double>(clampedSeconds));
    if (sqlite3_step(sceneStmt.stmt) != SQLITE_DONE) {
      error = sqlite3_errmsg(db_);
      break;
    }

    sceneId = static_cast<int>(sqlite3_last_insert_rowid(db_));

    Statement copyStmt;
    if (!prepare(
            db_,
            "INSERT INTO scene_values(scene_id, fixture_id, channel_index, value) "
            "SELECT ?, v.fixture_id, v.channel_index, v.value "
            "FROM fixture_channel_values v JOIN fixtures f ON f.id = v.fixture_id;",
            copyStmt, error)) {
      break;
    }
    sqlite3_bind_int(copyStmt.stmt, 1, sceneId);
    if (sqlite3_step(copyStmt.stmt) != SQLITE_DONE) {
      error = sqlite3_errmsg(db_);
      break;
    }

    if (!commitTransaction(error)) {
      break;
    }
    return sceneId;
  } while (false);

  rollbackTransaction();
  return 0;
}

bool Database::updateScene(int sceneId, const std::string& name, float transitionSeconds, std::string& error) {
  std::scoped_lock lock(mutex_);

  Statement stmt;
  if (!prepare(db_, "UPDATE scenes SET name = ?, transition_seconds = ? WHERE id = ?;", stmt, error)) {
    return false;
  }

  sqlite3_bind_text(stmt.stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_double(stmt.stmt, 2, static_cast<double>(std::clamp(transitionSeconds, 0.0F, 60.0F)));
  sqlite3_bind_int(stmt.stmt, 3, sceneId);

  if (sqlite3_step(stmt.stmt) != SQLITE_DONE) {
    error = sqlite3_errmsg(db_);
    return false;
  }

  if (sqlite3_changes(db_) == 0) {
    error = "Scene not found";
    return false;
  }

  return true;
}

bool Database::captureScene(int sceneId, std::string& error) {
  std::scoped_lock lock(mutex_);

  if (!beginTransaction(error)) {
    return false;
  }

  do {
    Statement existsStmt;
    if (!prepare(db_, "SELECT id FROM scenes WHERE id = ?;", existsStmt, error)) {
      break;
    }
    sqlite3_bind_int(existsStmt.stmt, 1, sceneId);
    if (sqlite3_step(existsStmt.stmt) != SQLITE_ROW) {
      error = "Scene not found";
      break;
    }

    Statement deleteStmt;
    if (!prepare(db_, "DELETE FROM scene_values WHERE scene_id = ?;", deleteStmt, error)) {
      break;
    }
    sqlite3_bind_int(deleteStmt.stmt, 1, sceneId);
    if (sqlite3_step(deleteStmt.stmt) != SQLITE_DONE) {
      error = sqlite3_errmsg(db_);
      break;
    }

    Statement copyStmt;
    if (!prepare(
            db_,
            "INSERT INTO scene_values(scene_id, fixture_id, channel_index, value) "
            "SELECT ?, v.fixture_id, v.channel_index, v.value "
            "FROM fixture_channel_values v JOIN fixtures f ON f.id = v.fixture_id;",
            copyStmt, error)) {
      break;
    }
    sqlite3_bind_int(copyStmt.stmt, 1, sceneId);
    if (sqlite3_step(copyStmt.stmt) != SQLITE_DONE) {
      error = sqlite3_errmsg(db_);
      break;
    }

    if (!commitTransaction(error)) {
      break;
    }
    return true;
  } while (false);

  rollbackTransaction();
  return false;
}

bool Database::deleteScene(int sceneId, std::string& error) {
  std::scoped_lock lock(mutex_);

  Statement stmt;
  if (!prepare(db_, "DELETE FROM scenes WHERE id = ?;", stmt, error)) {
    return false;
  }

  sqlite3_bind_int(stmt.stmt, 1, sceneId);
  if (sqlite3_step(stmt.stmt) != SQLITE_DONE) {
    error = sqlite3_errmsg(db_);
    return false;
  }

  if (sqlite3_changes(db_) == 0) {
    error = "Scene not found";
    return false;
  }

  return true;
}

std::vector<ScenePatch> Database::loadScenePatches(int sceneId, std::string& error) {
  std::scoped_lock lock(mutex_);

  std::vector<ScenePatch> patches;

  Statement stmt;
  if (!prepare(
          db_,
          "SELECT sv.fixture_id, sv.channel_index, sv.value, f.universe, f.start_address, f.channel_count, f.enabled "
          "FROM scene_values sv JOIN fixtures f ON f.id = sv.fixture_id WHERE sv.scene_id = ? "
          "ORDER BY f.sort_order, sv.fixture_id, sv.channel_index;",
          stmt, error)) {
    return patches;
  }
  sqlite3_bind_int(stmt.stmt, 1, sceneId);

  while (sqlite3_step(stmt.stmt) == SQLITE_ROW) {
    const int fixtureId = columnInt(stmt.stmt, 0);
    const int channelIndex = columnInt(stmt.stmt, 1);
    const int value = clampDmx(columnInt(stmt.stmt, 2));
    const int universe = columnInt(stmt.stmt, 3);
    const int startAddress = columnInt(stmt.stmt, 4);
    const int channelCount = columnInt(stmt.stmt, 5);
    const bool enabled = columnInt(stmt.stmt, 6) != 0;

    if (!enabled || channelIndex < 1 || channelIndex > channelCount) {
      continue;
    }

    ScenePatch patch;
    patch.fixtureId = fixtureId;
    patch.channelIndex = channelIndex;
    patch.universe = universe;
    patch.absoluteAddress = startAddress + channelIndex - 1;
    patch.value = value;

    if (patch.absoluteAddress < 1 || patch.absoluteAddress > 512) {
      continue;
    }

    patches.push_back(std::move(patch));
  }

  return patches;
}

std::vector<MidiMapping> Database::listMidiMappings(std::string& error) {
  std::scoped_lock lock(mutex_);

  std::vector<MidiMapping> mappings;

  Statement stmt;
  if (!prepare(db_,
               "SELECT control_id, source, input_id, type, channel, number FROM midi_mappings "
               "ORDER BY control_id COLLATE NOCASE;",
               stmt, error)) {
    return mappings;
  }

  while (sqlite3_step(stmt.stmt) == SQLITE_ROW) {
    MidiMapping mapping;
    mapping.controlId = columnText(stmt.stmt, 0);
    mapping.source = columnText(stmt.stmt, 1);
    mapping.inputId = columnText(stmt.stmt, 2);
    mapping.type = columnText(stmt.stmt, 3);
    mapping.channel = std::clamp(columnInt(stmt.stmt, 4), 1, 16);
    mapping.number = std::clamp(columnInt(stmt.stmt, 5), 0, 127);
    mappings.push_back(std::move(mapping));
  }

  return mappings;
}

bool Database::upsertMidiMapping(const MidiMapping& mapping, std::string& error) {
  std::scoped_lock lock(mutex_);

  const std::string source = mapping.source == "specific" ? "specific" : "all";
  const std::string type = mapping.type == "note" ? "note" : "cc";
  const int channel = std::clamp(mapping.channel, 1, 16);
  const int number = std::clamp(mapping.number, 0, 127);
  const std::string inputId = source == "specific" ? mapping.inputId : "";

  Statement stmt;
  if (!prepare(db_,
               "INSERT INTO midi_mappings(control_id, source, input_id, type, channel, number) "
               "VALUES(?, ?, ?, ?, ?, ?) "
               "ON CONFLICT(control_id) DO UPDATE SET "
               "source=excluded.source, input_id=excluded.input_id, type=excluded.type, "
               "channel=excluded.channel, number=excluded.number;",
               stmt, error)) {
    return false;
  }

  sqlite3_bind_text(stmt.stmt, 1, mapping.controlId.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt.stmt, 2, source.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt.stmt, 3, inputId.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt.stmt, 4, type.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int(stmt.stmt, 5, channel);
  sqlite3_bind_int(stmt.stmt, 6, number);

  if (sqlite3_step(stmt.stmt) != SQLITE_DONE) {
    error = sqlite3_errmsg(db_);
    return false;
  }

  return true;
}

bool Database::deleteMidiMapping(const std::string& controlId, std::string& error) {
  std::scoped_lock lock(mutex_);

  Statement stmt;
  if (!prepare(db_, "DELETE FROM midi_mappings WHERE control_id = ?;", stmt, error)) {
    return false;
  }
  sqlite3_bind_text(stmt.stmt, 1, controlId.c_str(), -1, SQLITE_TRANSIENT);

  if (sqlite3_step(stmt.stmt) != SQLITE_DONE) {
    error = sqlite3_errmsg(db_);
    return false;
  }

  return true;
}

bool Database::setSetting(const std::string& key, const std::string& value, std::string& error) {
  std::scoped_lock lock(mutex_);

  Statement stmt;
  if (!prepare(db_,
               "INSERT INTO app_settings(key, value) VALUES(?, ?) "
               "ON CONFLICT(key) DO UPDATE SET value=excluded.value;",
               stmt, error)) {
    return false;
  }

  sqlite3_bind_text(stmt.stmt, 1, key.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt.stmt, 2, value.c_str(), -1, SQLITE_TRANSIENT);

  if (sqlite3_step(stmt.stmt) != SQLITE_DONE) {
    error = sqlite3_errmsg(db_);
    return false;
  }

  return true;
}

bool Database::getSetting(const std::string& key, std::string& value, std::string& error) {
  std::scoped_lock lock(mutex_);

  Statement stmt;
  if (!prepare(db_, "SELECT value FROM app_settings WHERE key = ?;", stmt, error)) {
    return false;
  }
  sqlite3_bind_text(stmt.stmt, 1, key.c_str(), -1, SQLITE_TRANSIENT);

  if (sqlite3_step(stmt.stmt) == SQLITE_ROW) {
    value = columnText(stmt.stmt, 0);
    return true;
  }

  value.clear();
  return false;
}

int Database::createGroup(const std::string& name, std::string& error) {
  std::scoped_lock lock(mutex_);

  Statement stmt;
  if (!prepare(db_, "INSERT INTO fixture_groups(name) VALUES(?);", stmt, error)) {
    return 0;
  }
  sqlite3_bind_text(stmt.stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);

  if (sqlite3_step(stmt.stmt) != SQLITE_DONE) {
    error = sqlite3_errmsg(db_);
    return 0;
  }

  return static_cast<int>(sqlite3_last_insert_rowid(db_));
}

bool Database::setGroupFixtures(int groupId, const std::vector<int>& fixtureIds, std::string& error) {
  std::scoped_lock lock(mutex_);

  Statement groupStmt;
  if (!prepare(db_, "SELECT id FROM fixture_groups WHERE id = ?;", groupStmt, error)) {
    return false;
  }
  sqlite3_bind_int(groupStmt.stmt, 1, groupId);
  if (sqlite3_step(groupStmt.stmt) != SQLITE_ROW) {
    error = "Group not found";
    return false;
  }

  if (!beginTransaction(error)) {
    return false;
  }

  do {
    Statement deleteStmt;
    if (!prepare(db_, "DELETE FROM fixture_group_members WHERE group_id = ?;", deleteStmt, error)) {
      break;
    }
    sqlite3_bind_int(deleteStmt.stmt, 1, groupId);
    if (sqlite3_step(deleteStmt.stmt) != SQLITE_DONE) {
      error = sqlite3_errmsg(db_);
      break;
    }

    std::vector<int> deduped = fixtureIds;
    std::sort(deduped.begin(), deduped.end());
    deduped.erase(std::unique(deduped.begin(), deduped.end()), deduped.end());

    Statement fixtureExistsStmt;
    if (!prepare(db_, "SELECT id FROM fixtures WHERE id = ?;", fixtureExistsStmt, error)) {
      break;
    }

    Statement insertStmt;
    if (!prepare(db_, "INSERT INTO fixture_group_members(group_id, fixture_id) VALUES(?, ?);", insertStmt, error)) {
      break;
    }

    for (int fixtureId : deduped) {
      sqlite3_reset(fixtureExistsStmt.stmt);
      sqlite3_clear_bindings(fixtureExistsStmt.stmt);
      sqlite3_bind_int(fixtureExistsStmt.stmt, 1, fixtureId);
      if (sqlite3_step(fixtureExistsStmt.stmt) != SQLITE_ROW) {
        error = "Fixture id " + std::to_string(fixtureId) + " does not exist";
        break;
      }

      sqlite3_reset(insertStmt.stmt);
      sqlite3_clear_bindings(insertStmt.stmt);
      sqlite3_bind_int(insertStmt.stmt, 1, groupId);
      sqlite3_bind_int(insertStmt.stmt, 2, fixtureId);
      if (sqlite3_step(insertStmt.stmt) != SQLITE_DONE) {
        error = sqlite3_errmsg(db_);
        break;
      }
    }

    if (!error.empty()) {
      break;
    }

    if (!commitTransaction(error)) {
      break;
    }

    return true;
  } while (false);

  rollbackTransaction();
  return false;
}

std::vector<FixtureGroup> Database::listGroups(std::string& error) {
  std::scoped_lock lock(mutex_);

  std::vector<FixtureGroup> groups;

  Statement groupStmt;
  if (!prepare(db_, "SELECT id, name FROM fixture_groups ORDER BY name COLLATE NOCASE;", groupStmt, error)) {
    return groups;
  }

  while (sqlite3_step(groupStmt.stmt) == SQLITE_ROW) {
    FixtureGroup group;
    group.id = columnInt(groupStmt.stmt, 0);
    group.name = columnText(groupStmt.stmt, 1);
    groups.push_back(std::move(group));
  }

  for (auto& group : groups) {
    Statement memberStmt;
    if (!prepare(db_, "SELECT fixture_id FROM fixture_group_members WHERE group_id = ? ORDER BY fixture_id;", memberStmt,
                 error)) {
      return {};
    }
    sqlite3_bind_int(memberStmt.stmt, 1, group.id);

    while (sqlite3_step(memberStmt.stmt) == SQLITE_ROW) {
      group.fixtureIds.push_back(columnInt(memberStmt.stmt, 0));
    }
  }

  return groups;
}

bool Database::deleteGroup(int groupId, std::string& error) {
  std::scoped_lock lock(mutex_);

  Statement stmt;
  if (!prepare(db_, "DELETE FROM fixture_groups WHERE id = ?;", stmt, error)) {
    return false;
  }

  sqlite3_bind_int(stmt.stmt, 1, groupId);
  if (sqlite3_step(stmt.stmt) != SQLITE_DONE) {
    error = sqlite3_errmsg(db_);
    return false;
  }

  if (sqlite3_changes(db_) == 0) {
    error = "Group not found";
    return false;
  }

  return true;
}

std::vector<ChannelPatch> Database::loadUniversePatch(int universe, std::string& error) {
  std::scoped_lock lock(mutex_);

  std::vector<ChannelPatch> patches;

  Statement stmt;
  if (!prepare(db_,
               "SELECT f.start_address, v.channel_index, v.value FROM fixtures f "
               "JOIN fixture_channel_values v ON v.fixture_id = f.id "
               "WHERE f.universe = ? AND f.enabled = 1;",
               stmt, error)) {
    return patches;
  }
  sqlite3_bind_int(stmt.stmt, 1, universe);

  while (sqlite3_step(stmt.stmt) == SQLITE_ROW) {
    ChannelPatch patch;
    patch.universe = universe;
    patch.absoluteAddress = columnInt(stmt.stmt, 0) + columnInt(stmt.stmt, 1) - 1;
    patch.value = clampDmx(columnInt(stmt.stmt, 2));
    patches.push_back(std::move(patch));
  }

  return patches;
}

bool Database::seedAliExpressRgbPar(std::string& error) {
  std::scoped_lock lock(mutex_);

  Statement checkStmt;
  if (!prepare(db_, "SELECT id FROM fixture_templates WHERE name = ?;", checkStmt, error)) {
    return false;
  }

  sqlite3_bind_text(checkStmt.stmt, 1, "AliExpress 60x3W RGB PAR", -1, SQLITE_TRANSIENT);
  if (sqlite3_step(checkStmt.stmt) == SQLITE_ROW) {
    return true;
  }

  if (!beginTransaction(error)) {
    return false;
  }

  do {
    Statement templateStmt;
    if (!prepare(db_, "INSERT INTO fixture_templates(name, description, footprint_channels) VALUES(?, ?, 7);",
                 templateStmt, error)) {
      break;
    }
    sqlite3_bind_text(templateStmt.stmt, 1, "AliExpress 60x3W RGB PAR", -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(templateStmt.stmt, 2,
                      "3W*60 LED RGB 3-in-1 PAR fixture profile based on manual channel mapping.", -1,
                      SQLITE_TRANSIENT);

    if (sqlite3_step(templateStmt.stmt) != SQLITE_DONE) {
      error = sqlite3_errmsg(db_);
      break;
    }

    const int templateId = static_cast<int>(sqlite3_last_insert_rowid(db_));

    struct SeedChannel {
      int idx;
      const char* name;
      const char* kind;
      int defaultValue;
      std::vector<std::tuple<int, int, const char*>> ranges;
    };

    const std::vector<SeedChannel> channels = {
        {1, "Master Dimmer", "dimmer", 255, {{0, 255, "Master dimmer for RGB output"}}},
        {2, "Red", "red", 0, {{0, 255, "Red intensity"}}},
        {3, "Green", "green", 0, {{0, 255, "Green intensity"}}},
        {4, "Blue", "blue", 0, {{0, 255, "Blue intensity"}}},
        {5, "Strobe", "strobe", 0, {{0, 3, "No flicker"}, {4, 255, "Strobe slow to fast"}}},
        {6,
         "Program Mode",
         "mode",
         0,
         {{0, 50, "Manual CH1-CH6"},
          {51, 100, "Jump"},
          {101, 150, "Gradient"},
          {151, 200, "Variable Pulse"},
          {201, 255, "Voice"}}},
        {7, "Effect Speed", "speed", 0, {{0, 255, "Effect speed adjustment"}}},
    };

    Statement channelStmt;
    if (!prepare(db_,
                 "INSERT INTO template_channels(template_id, channel_index, name, kind, default_value) VALUES(?, ?, ?, ?, ?);",
                 channelStmt, error)) {
      break;
    }

    Statement rangeStmt;
    if (!prepare(db_,
                 "INSERT INTO template_channel_ranges(channel_id, start_value, end_value, label) VALUES(?, ?, ?, ?);",
                 rangeStmt, error)) {
      break;
    }

    for (const auto& c : channels) {
      sqlite3_reset(channelStmt.stmt);
      sqlite3_clear_bindings(channelStmt.stmt);
      sqlite3_bind_int(channelStmt.stmt, 1, templateId);
      sqlite3_bind_int(channelStmt.stmt, 2, c.idx);
      sqlite3_bind_text(channelStmt.stmt, 3, c.name, -1, SQLITE_TRANSIENT);
      sqlite3_bind_text(channelStmt.stmt, 4, c.kind, -1, SQLITE_TRANSIENT);
      sqlite3_bind_int(channelStmt.stmt, 5, c.defaultValue);
      if (sqlite3_step(channelStmt.stmt) != SQLITE_DONE) {
        error = sqlite3_errmsg(db_);
        break;
      }

      const int channelId = static_cast<int>(sqlite3_last_insert_rowid(db_));
      for (const auto& r : c.ranges) {
        sqlite3_reset(rangeStmt.stmt);
        sqlite3_clear_bindings(rangeStmt.stmt);
        sqlite3_bind_int(rangeStmt.stmt, 1, channelId);
        sqlite3_bind_int(rangeStmt.stmt, 2, std::get<0>(r));
        sqlite3_bind_int(rangeStmt.stmt, 3, std::get<1>(r));
        sqlite3_bind_text(rangeStmt.stmt, 4, std::get<2>(r), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(rangeStmt.stmt) != SQLITE_DONE) {
          error = sqlite3_errmsg(db_);
          break;
        }
      }

      if (!error.empty()) {
        break;
      }
    }

    if (!error.empty()) {
      break;
    }

    if (!commitTransaction(error)) {
      break;
    }

    return true;

  } while (false);

  rollbackTransaction();
  return false;
}

bool Database::seedMiraDye(std::string& error) {
  std::scoped_lock lock(mutex_);

  static constexpr const char* kMiraTemplateName = "Mira Dye";
  static constexpr const char* kMiraADescription =
      "Mira Dye A-mode profile (13 channels): pan/tilt, RGBW, strip effects, built-in color effects, and macro.";

  const std::vector<SeedChannel> aModeChannels = {
      {1, "X axle", "pan", 128, {{0, 255, "0-360 degree rotation"}}},
      {2, "Y axle", "tilt", 128, {{0, 255, "0-180 degree rotation"}}},
      {3, "X-axis speed", "pan_speed", 255, {{0, 255, "Speed from fast to slow"}}},
      {4, "Total light control", "dimmer", 255, {{0, 255, "Linear dimming from dark to light"}}},
      {5, "Stroboflash", "strobe", 0, {{0, 9, "NF"}, {10, 255, "Flicker from slow to fast"}}},
      {6, "Stained Red", "red", 0, {{0, 255, "Red linear dimming 0-100%"}}},
      {7, "Tinted Green", "green", 0, {{0, 255, "Green linear dimming 0-100%"}}},
      {8, "Dye blue", "blue", 0, {{0, 255, "Blue linear dimming 0-100%"}}},
      {9, "Stained White", "white", 0, {{0, 255, "White linear dimming 0-100%"}}},
      {10,
       "Fancy Light Strips",
       "strip_effect",
       0,
       {{0, 13, "NF"}, {13, 75, "7 fixed color options"}, {76, 255, "20 self-drive effect options"}}},
      {11, "Speed of the light band", "speed", 0, {{0, 255, "The effect goes from slow to fast"}}},
      {12,
       "Built-in color effect",
       "effect_mode",
       0,
       {{0, 15, "NF"}, {16, 95, "Color Shift"}, {96, 177, "Tint Gradients"}, {178, 255, "Chromatic aberration"}}},
      {13,
       "Macro function controls",
       "macro",
       0,
       {{0, 50, "NF"},
        {51, 150, "Random walk"},
        {151, 250, "Voice activated self-propelled"},
        {251, 255, "Wait 3 seconds, then machine reset"}}},
  };

  int templateId = findTemplateIdByName(db_, kMiraTemplateName, error);
  if (!error.empty()) {
    return false;
  }

  // New install: create Mira Dye directly as the known A-mode layout.
  if (templateId == 0) {
    if (!beginTransaction(error)) {
      return false;
    }

    do {
      Statement templateStmt;
      if (!prepare(db_, "INSERT INTO fixture_templates(name, description, footprint_channels) VALUES(?, ?, 13);",
                   templateStmt, error)) {
        break;
      }

      sqlite3_bind_text(templateStmt.stmt, 1, kMiraTemplateName, -1, SQLITE_TRANSIENT);
      sqlite3_bind_text(templateStmt.stmt, 2, kMiraADescription, -1, SQLITE_TRANSIENT);
      if (sqlite3_step(templateStmt.stmt) != SQLITE_DONE) {
        error = sqlite3_errmsg(db_);
        break;
      }

      const int newTemplateId = static_cast<int>(sqlite3_last_insert_rowid(db_));
      if (!insertTemplateChannels(db_, newTemplateId, aModeChannels, error)) {
        break;
      }

      if (!commitTransaction(error)) {
        break;
      }
      return true;
    } while (false);

    rollbackTransaction();
    return false;
  }

  const bool alreadyAMode = templateChannelsMatch(db_, templateId, aModeChannels, error);
  if (!error.empty()) {
    return false;
  }

  if (alreadyAMode) {
    return updateTemplateMetadata(db_, templateId, kMiraADescription, 13, error);
  }

  // Existing install with a different Mira layout: replace in place with A-mode.
  if (!beginTransaction(error)) {
    return false;
  }

  do {
    if (!clearTemplateChannels(db_, templateId, error)) {
      break;
    }
    if (!updateTemplateMetadata(db_, templateId, kMiraADescription, 13, error)) {
      break;
    }
    if (!insertTemplateChannels(db_, templateId, aModeChannels, error)) {
      break;
    }

    if (!commitTransaction(error)) {
      break;
    }
    return true;
  } while (false);

  rollbackTransaction();
  return false;
}

}  // namespace tuxdmx

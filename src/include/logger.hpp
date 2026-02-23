#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace tuxdmx {

enum class LogLevel {
  Debug,
  Info,
  Warn,
  Error,
};

struct LogEntry {
  std::string timestamp;
  std::string level;
  std::string scope;
  std::string message;
};

bool initializeLogger(const std::string& filePath, std::string& error);
void shutdownLogger();

void logMessage(LogLevel level, std::string_view scope, std::string_view message);
std::vector<LogEntry> recentLogs(std::size_t limit = 200);
void clearRecentLogs();

}  // namespace tuxdmx


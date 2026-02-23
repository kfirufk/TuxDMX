#include "logger.hpp"

#include <chrono>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>

namespace tuxdmx {

namespace {

std::mutex gLogMutex;
std::ofstream gLogFile;
std::deque<LogEntry> gRecentLogs;
constexpr std::size_t kMaxRecentLogs = 1000;

std::string levelName(LogLevel level) {
  switch (level) {
    case LogLevel::Debug:
      return "DEBUG";
    case LogLevel::Info:
      return "INFO";
    case LogLevel::Warn:
      return "WARN";
    case LogLevel::Error:
      return "ERROR";
  }
  return "INFO";
}

std::string nowTimestamp() {
  using namespace std::chrono;
  const auto now = system_clock::now();
  const auto secs = system_clock::to_time_t(now);
  const auto ms = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;

  std::tm tm{};
#ifdef _WIN32
  localtime_s(&tm, &secs);
#else
  localtime_r(&secs, &tm);
#endif

  std::ostringstream ss;
  ss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S") << '.' << std::setw(3) << std::setfill('0') << ms.count();
  return ss.str();
}

}  // namespace

bool initializeLogger(const std::string& filePath, std::string& error) {
  std::scoped_lock lock(gLogMutex);

  if (!filePath.empty()) {
    std::error_code ec;
    const auto path = std::filesystem::path(filePath);
    if (!path.parent_path().empty()) {
      std::filesystem::create_directories(path.parent_path(), ec);
      if (ec) {
        error = "Failed to create log directory: " + ec.message();
        return false;
      }
    }

    gLogFile.open(filePath, std::ios::out | std::ios::app);
    if (!gLogFile.is_open()) {
      error = "Failed to open log file: " + filePath;
      return false;
    }
  }

  return true;
}

void shutdownLogger() {
  std::scoped_lock lock(gLogMutex);
  if (gLogFile.is_open()) {
    gLogFile.flush();
    gLogFile.close();
  }
}

void logMessage(LogLevel level, std::string_view scope, std::string_view message) {
  const std::string ts = nowTimestamp();
  const std::string levelText = levelName(level);

  std::ostringstream line;
  line << ts << " [" << levelText << "] "
       << (!scope.empty() ? std::string(scope) : std::string("app"))
       << ": " << message;
  const std::string output = line.str();

  std::scoped_lock lock(gLogMutex);

  std::cout << output << '\n';
  std::cout.flush();

  if (gLogFile.is_open()) {
    gLogFile << output << '\n';
    gLogFile.flush();
  }

  LogEntry entry;
  entry.timestamp = ts;
  entry.level = levelText;
  entry.scope = scope.empty() ? "app" : std::string(scope);
  entry.message = std::string(message);

  gRecentLogs.push_back(std::move(entry));
  while (gRecentLogs.size() > kMaxRecentLogs) {
    gRecentLogs.pop_front();
  }
}

std::vector<LogEntry> recentLogs(std::size_t limit) {
  std::scoped_lock lock(gLogMutex);
  if (limit == 0 || gRecentLogs.empty()) {
    return {};
  }

  const std::size_t count = std::min(limit, gRecentLogs.size());
  std::vector<LogEntry> out;
  out.reserve(count);

  const auto startIt = gRecentLogs.end() - static_cast<std::ptrdiff_t>(count);
  for (auto it = startIt; it != gRecentLogs.end(); ++it) {
    out.push_back(*it);
  }

  return out;
}

void clearRecentLogs() {
  std::scoped_lock lock(gLogMutex);
  gRecentLogs.clear();
}

}  // namespace tuxdmx


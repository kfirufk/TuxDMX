#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "app_controller.hpp"
#include "dmx_backend_factory.hpp"
#include "http_server.hpp"
#include "logger.hpp"
#include "utils.hpp"

namespace {
std::atomic<bool> gStopRequested = false;

void handleSignal(int) { gStopRequested.store(true); }

void printUsage(const char* argv0) {
  const auto backends = tuxdmx::supportedDmxBackendNames();
  std::cout << "Usage: " << argv0
            << " [--bind 0.0.0.0] [--port 18181] [--db data/tuxdmx.sqlite] [--web-root ./web] [--log-file "
               "data/tuxdmx.log] [--dmx-backend enttec-usb-pro]\n";
  std::cout << "Supported DMX backends:";
  for (const auto& backend : backends) {
    std::cout << ' ' << backend;
  }
  std::cout << "\n";
}

}  // namespace

int main(int argc, char** argv) {
  std::string bindAddress = "0.0.0.0";
  int port = 18181;
  std::string dbPath = "data/tuxdmx.sqlite";
  std::string webRoot = TUXDMX_DEFAULT_WEB_ROOT;
  std::string logFilePath = "data/tuxdmx.log";
  std::string dmxBackend = std::string(tuxdmx::kDefaultDmxBackendName);

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--bind" && i + 1 < argc) {
      bindAddress = argv[++i];
    } else if (arg == "--port" && i + 1 < argc) {
      if (!tuxdmx::parseInt(argv[++i], port) || port <= 0 || port > 65535) {
        std::cerr << "Invalid --port value\n";
        return 1;
      }
    } else if (arg == "--db" && i + 1 < argc) {
      dbPath = argv[++i];
    } else if (arg == "--web-root" && i + 1 < argc) {
      webRoot = argv[++i];
    } else if (arg == "--log-file" && i + 1 < argc) {
      logFilePath = argv[++i];
    } else if (arg == "--dmx-backend" && i + 1 < argc) {
      dmxBackend = argv[++i];
    } else if (arg == "--help" || arg == "-h") {
      printUsage(argv[0]);
      return 0;
    } else {
      std::cerr << "Unknown argument: " << arg << "\n";
      printUsage(argv[0]);
      return 1;
    }
  }

  dmxBackend = tuxdmx::normalizeDmxBackendName(dmxBackend);
  if (!tuxdmx::isSupportedDmxBackendName(dmxBackend)) {
    std::cerr << "Invalid --dmx-backend value: " << dmxBackend << "\n";
    printUsage(argv[0]);
    return 1;
  }

  std::signal(SIGINT, handleSignal);
  std::signal(SIGTERM, handleSignal);

  std::string loggerError;
  if (!tuxdmx::initializeLogger(logFilePath, loggerError)) {
    std::cerr << "Failed to initialize logger: " << loggerError << "\n";
    return 1;
  }

  tuxdmx::logMessage(tuxdmx::LogLevel::Info, "server", "Starting tuxdmx");
  tuxdmx::logMessage(tuxdmx::LogLevel::Info, "server", std::string("Log file: ") + logFilePath);

  tuxdmx::AppController app(dbPath, webRoot, dmxBackend);
  std::string error;
  if (!app.initialize(error)) {
    std::cerr << "Failed to initialize app: " << error << "\n";
    tuxdmx::logMessage(tuxdmx::LogLevel::Error, "server", "Failed to initialize app: " + error);
    tuxdmx::shutdownLogger();
    return 1;
  }

  tuxdmx::HttpServer server(bindAddress, port, [&app](const tuxdmx::HttpRequest& request) {
    return app.handleRequest(request);
  });

  if (!server.start(error)) {
    std::cerr << "Failed to start HTTP server: " << error << "\n";
    tuxdmx::logMessage(tuxdmx::LogLevel::Error, "server", "Failed to start HTTP server: " + error);
    app.shutdown();
    tuxdmx::shutdownLogger();
    return 1;
  }

  std::cout << "tuxdmx started\n";
  std::cout << "HTTP UI: http://" << bindAddress << ':' << port << "\n";
  std::cout << "Press Ctrl+C to stop\n";
  tuxdmx::logMessage(tuxdmx::LogLevel::Info, "server",
                     std::string("HTTP UI: http://") + bindAddress + ":" + std::to_string(port));

  while (!gStopRequested.load()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
  }

  tuxdmx::logMessage(tuxdmx::LogLevel::Info, "server", "Shutdown requested");
  server.stop();
  app.shutdown();
  tuxdmx::logMessage(tuxdmx::LogLevel::Info, "server", "Shutdown complete");
  tuxdmx::shutdownLogger();

  return 0;
}

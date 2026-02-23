#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <string>
#include <thread>

#include "app_controller.hpp"
#include "http_server.hpp"
#include "utils.hpp"

namespace {
std::atomic<bool> gStopRequested = false;

void handleSignal(int) { gStopRequested.store(true); }

void printUsage(const char* argv0) {
  std::cout << "Usage: " << argv0 << " [--bind 0.0.0.0] [--port 8080] [--db data/tuxdmx.sqlite] [--web-root ./web]\n";
}

}  // namespace

int main(int argc, char** argv) {
  std::string bindAddress = "0.0.0.0";
  int port = 8080;
  std::string dbPath = "data/tuxdmx.sqlite";
  std::string webRoot = TUXDMX_DEFAULT_WEB_ROOT;

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
    } else if (arg == "--help" || arg == "-h") {
      printUsage(argv[0]);
      return 0;
    } else {
      std::cerr << "Unknown argument: " << arg << "\n";
      printUsage(argv[0]);
      return 1;
    }
  }

  std::signal(SIGINT, handleSignal);
  std::signal(SIGTERM, handleSignal);

  tuxdmx::AppController app(dbPath, webRoot);
  std::string error;
  if (!app.initialize(error)) {
    std::cerr << "Failed to initialize app: " << error << "\n";
    return 1;
  }

  tuxdmx::HttpServer server(bindAddress, port, [&app](const tuxdmx::HttpRequest& request) {
    return app.handleRequest(request);
  });

  if (!server.start(error)) {
    std::cerr << "Failed to start HTTP server: " << error << "\n";
    app.shutdown();
    return 1;
  }

  std::cout << "tuxdmx started\n";
  std::cout << "HTTP UI: http://" << bindAddress << ':' << port << "\n";
  std::cout << "Press Ctrl+C to stop\n";

  while (!gStopRequested.load()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
  }

  server.stop();
  app.shutdown();

  return 0;
}

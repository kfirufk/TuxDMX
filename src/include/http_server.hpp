#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace tuxdmx {

struct HttpRequest {
  std::string method;
  std::string target;
  std::string path;
  std::unordered_map<std::string, std::string> query;
  std::unordered_map<std::string, std::string> headers;
  std::string body;
};

struct HttpResponse {
  int status = 200;
  std::string contentType = "text/plain; charset=utf-8";
  std::string body;
  std::vector<std::pair<std::string, std::string>> headers;
};

class HttpServer {
 public:
  using Handler = std::function<HttpResponse(const HttpRequest&)>;

  HttpServer(std::string bindAddress, int port, Handler handler);
  ~HttpServer();

  HttpServer(const HttpServer&) = delete;
  HttpServer& operator=(const HttpServer&) = delete;

  bool start(std::string& error);
  void stop();

 private:
  void acceptLoop();

  bool bindSocket(std::string& error);

  std::string bindAddress_;
  int port_ = 18181;
  Handler handler_;

  std::atomic<bool> running_{false};
  std::thread acceptThread_;

  std::intptr_t listenSocket_ = -1;
};

}  // namespace tuxdmx

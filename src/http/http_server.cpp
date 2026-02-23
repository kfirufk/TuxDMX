#include "http_server.hpp"

#include <array>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <thread>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include "utils.hpp"

namespace tuxdmx {

namespace {

#ifdef _WIN32
using SocketType = SOCKET;
constexpr SocketType kInvalidSocket = INVALID_SOCKET;
void closeSocket(SocketType socket) {
  if (socket != INVALID_SOCKET) {
    closesocket(socket);
  }
}
int socketError() { return WSAGetLastError(); }
#else
using SocketType = int;
constexpr SocketType kInvalidSocket = -1;
void closeSocket(SocketType socket) {
  if (socket >= 0) {
    close(socket);
  }
}
int socketError() { return errno; }
#endif

std::string toStatusText(int status) {
  switch (status) {
    case 200:
      return "OK";
    case 201:
      return "Created";
    case 204:
      return "No Content";
    case 400:
      return "Bad Request";
    case 404:
      return "Not Found";
    case 405:
      return "Method Not Allowed";
    case 409:
      return "Conflict";
    case 422:
      return "Unprocessable Entity";
    case 500:
      return "Internal Server Error";
    default:
      return "OK";
  }
}

std::unordered_map<std::string, std::string> parseHeaders(const std::string& headerBlob) {
  std::unordered_map<std::string, std::string> headers;
  std::istringstream ss(headerBlob);
  std::string line;

  std::getline(ss, line);
  while (std::getline(ss, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    if (line.empty()) {
      continue;
    }
    const auto colon = line.find(':');
    if (colon == std::string::npos) {
      continue;
    }

    auto key = toLower(trim(line.substr(0, colon)));
    auto value = trim(line.substr(colon + 1));
    headers[std::move(key)] = std::move(value);
  }

  return headers;
}

bool parseRequest(const std::string& raw, HttpRequest& request) {
  const auto headerEnd = raw.find("\r\n\r\n");
  if (headerEnd == std::string::npos) {
    return false;
  }

  const std::string headers = raw.substr(0, headerEnd);
  request.body = raw.substr(headerEnd + 4);

  std::istringstream headerStream(headers);
  std::string startLine;
  if (!std::getline(headerStream, startLine)) {
    return false;
  }
  if (!startLine.empty() && startLine.back() == '\r') {
    startLine.pop_back();
  }

  std::istringstream startLineStream(startLine);
  std::string httpVersion;
  if (!(startLineStream >> request.method >> request.target >> httpVersion)) {
    return false;
  }

  request.path = stripQuery(request.target);
  request.query = parseQuery(request.target);
  request.headers = parseHeaders(headers);

  return true;
}

bool receiveHttpRequest(SocketType socket, HttpRequest& request) {
  constexpr std::size_t kMaxRequest = 1024 * 1024;
  std::string raw;
  raw.reserve(4096);

  std::array<char, 4096> buffer{};
  std::size_t expectedTotal = std::string::npos;

  while (raw.size() < kMaxRequest) {
#ifdef _WIN32
    const int received = recv(socket, buffer.data(), static_cast<int>(buffer.size()), 0);
#else
    const int received = static_cast<int>(recv(socket, buffer.data(), buffer.size(), 0));
#endif
    if (received <= 0) {
      return false;
    }

    raw.append(buffer.data(), static_cast<std::size_t>(received));

    const auto headerEnd = raw.find("\r\n\r\n");
    if (headerEnd != std::string::npos && expectedTotal == std::string::npos) {
      HttpRequest tmp;
      if (!parseRequest(raw, tmp)) {
        return false;
      }

      std::size_t contentLength = 0;
      auto it = tmp.headers.find("content-length");
      if (it != tmp.headers.end()) {
        try {
          contentLength = static_cast<std::size_t>(std::stoul(it->second));
        } catch (...) {
          return false;
        }
      }
      expectedTotal = headerEnd + 4 + contentLength;
    }

    if (expectedTotal != std::string::npos && raw.size() >= expectedTotal) {
      break;
    }
  }

  return parseRequest(raw, request);
}

void sendHttpResponse(SocketType socket, const HttpResponse& response) {
  std::ostringstream ss;
  ss << "HTTP/1.1 " << response.status << ' ' << toStatusText(response.status) << "\r\n";
  ss << "Content-Type: " << response.contentType << "\r\n";
  ss << "Content-Length: " << response.body.size() << "\r\n";
  ss << "Connection: close\r\n";

  for (const auto& [k, v] : response.headers) {
    ss << k << ": " << v << "\r\n";
  }

  ss << "\r\n";
  ss << response.body;

  const std::string payload = ss.str();
  std::size_t sentTotal = 0;
  while (sentTotal < payload.size()) {
#ifdef _WIN32
    const int sent = send(socket, payload.data() + sentTotal, static_cast<int>(payload.size() - sentTotal), 0);
#else
    const int sent = static_cast<int>(send(socket, payload.data() + sentTotal, payload.size() - sentTotal, 0));
#endif
    if (sent <= 0) {
      break;
    }
    sentTotal += static_cast<std::size_t>(sent);
  }
}

}  // namespace

HttpServer::HttpServer(std::string bindAddress, int port, Handler handler)
    : bindAddress_(std::move(bindAddress)), port_(port), handler_(std::move(handler)) {}

HttpServer::~HttpServer() { stop(); }

bool HttpServer::bindSocket(std::string& error) {
#ifdef _WIN32
  WSADATA wsaData{};
  if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
    error = "WSAStartup failed";
    return false;
  }
#endif

  SocketType sock = ::socket(AF_INET, SOCK_STREAM, 0);
  if (sock == kInvalidSocket) {
    error = "Socket create failed: " + std::to_string(socketError());
    return false;
  }

  int reuse = 1;
  setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(static_cast<uint16_t>(port_));

  if (bindAddress_ == "0.0.0.0" || bindAddress_ == "*") {
    addr.sin_addr.s_addr = INADDR_ANY;
  } else {
    if (inet_pton(AF_INET, bindAddress_.c_str(), &addr.sin_addr) != 1) {
      closeSocket(sock);
      error = "Invalid bind address";
      return false;
    }
  }

  if (::bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
    error = "Bind failed: " + std::to_string(socketError());
    closeSocket(sock);
    return false;
  }

  if (::listen(sock, 32) != 0) {
    error = "Listen failed: " + std::to_string(socketError());
    closeSocket(sock);
    return false;
  }

  listenSocket_ = static_cast<std::intptr_t>(sock);
  return true;
}

bool HttpServer::start(std::string& error) {
  bool expected = false;
  if (!running_.compare_exchange_strong(expected, true)) {
    return true;
  }

  if (!bindSocket(error)) {
    running_.store(false);
    return false;
  }

  acceptThread_ = std::thread([this] { acceptLoop(); });
  return true;
}

void HttpServer::stop() {
  bool expected = true;
  if (!running_.compare_exchange_strong(expected, false)) {
    return;
  }

  auto socket = static_cast<SocketType>(listenSocket_);
  closeSocket(socket);
  listenSocket_ = -1;

  if (acceptThread_.joinable()) {
    acceptThread_.join();
  }

#ifdef _WIN32
  WSACleanup();
#endif
}

void HttpServer::acceptLoop() {
  while (running_.load()) {
    auto serverSocket = static_cast<SocketType>(listenSocket_);
    if (serverSocket == kInvalidSocket) {
      break;
    }

    sockaddr_in clientAddr{};
#ifdef _WIN32
    int clientLen = sizeof(clientAddr);
#else
    socklen_t clientLen = sizeof(clientAddr);
#endif

    SocketType clientSocket = accept(serverSocket, reinterpret_cast<sockaddr*>(&clientAddr), &clientLen);
    if (clientSocket == kInvalidSocket) {
      if (!running_.load()) {
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      continue;
    }

    std::thread([this, clientSocket]() {
      HttpRequest request;
      HttpResponse response;
      if (receiveHttpRequest(clientSocket, request)) {
        response = handler_(request);
      } else {
        response.status = 400;
        response.contentType = "application/json; charset=utf-8";
        response.body = R"({"ok":false,"error":"Malformed HTTP request"})";
      }

      sendHttpResponse(clientSocket, response);
      closeSocket(clientSocket);
    }).detach();
  }
}

}  // namespace tuxdmx

#pragma once

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace tuxdmx {

inline std::string trim(std::string_view input) {
  std::size_t start = 0;
  while (start < input.size() && std::isspace(static_cast<unsigned char>(input[start])) != 0) {
    ++start;
  }
  std::size_t end = input.size();
  while (end > start && std::isspace(static_cast<unsigned char>(input[end - 1])) != 0) {
    --end;
  }
  return std::string(input.substr(start, end - start));
}

inline std::string toLower(std::string_view input) {
  std::string out;
  out.reserve(input.size());
  for (char c : input) {
    out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
  }
  return out;
}

inline std::string jsonEscape(std::string_view value) {
  std::string out;
  out.reserve(value.size() + 8);
  for (unsigned char c : value) {
    switch (c) {
      case '"':
        out += "\\\"";
        break;
      case '\\':
        out += "\\\\";
        break;
      case '\b':
        out += "\\b";
        break;
      case '\f':
        out += "\\f";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        if (c < 0x20) {
          char buf[7] = {};
          std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned int>(c));
          out += buf;
        } else {
          out.push_back(static_cast<char>(c));
        }
        break;
    }
  }
  return out;
}

inline std::string urlDecode(std::string_view input) {
  std::string out;
  out.reserve(input.size());
  for (std::size_t i = 0; i < input.size(); ++i) {
    char c = input[i];
    if (c == '+') {
      out.push_back(' ');
      continue;
    }
    if (c == '%' && i + 2 < input.size()) {
      auto hex = input.substr(i + 1, 2);
      int value = 0;
      std::istringstream ss{std::string(hex)};
      ss >> std::hex >> value;
      if (!ss.fail()) {
        out.push_back(static_cast<char>(value));
        i += 2;
        continue;
      }
    }
    out.push_back(c);
  }
  return out;
}

inline std::unordered_map<std::string, std::string> parseFormEncoded(std::string_view body) {
  std::unordered_map<std::string, std::string> out;
  std::size_t pos = 0;
  while (pos < body.size()) {
    auto amp = body.find('&', pos);
    if (amp == std::string_view::npos) {
      amp = body.size();
    }
    auto pair = body.substr(pos, amp - pos);
    auto eq = pair.find('=');
    if (eq == std::string_view::npos) {
      out[urlDecode(pair)] = "";
    } else {
      out[urlDecode(pair.substr(0, eq))] = urlDecode(pair.substr(eq + 1));
    }
    pos = amp + 1;
  }
  return out;
}

inline std::unordered_map<std::string, std::string> parseQuery(std::string_view target) {
  auto qPos = target.find('?');
  if (qPos == std::string_view::npos || qPos + 1 >= target.size()) {
    return {};
  }
  return parseFormEncoded(target.substr(qPos + 1));
}

inline std::string stripQuery(std::string_view target) {
  auto qPos = target.find('?');
  if (qPos == std::string_view::npos) {
    return std::string(target);
  }
  return std::string(target.substr(0, qPos));
}

inline bool parseInt(std::string_view text, int& out) {
  try {
    std::size_t idx = 0;
    const int value = std::stoi(std::string(text), &idx);
    if (idx != text.size()) {
      return false;
    }
    out = value;
    return true;
  } catch (...) {
    return false;
  }
}

inline std::vector<std::string> splitPath(std::string_view path) {
  std::vector<std::string> parts;
  std::string current;
  for (char c : path) {
    if (c == '/') {
      if (!current.empty()) {
        parts.push_back(current);
        current.clear();
      }
    } else {
      current.push_back(c);
    }
  }
  if (!current.empty()) {
    parts.push_back(current);
  }
  return parts;
}

inline std::string guessMimeType(const std::filesystem::path& file) {
  const auto ext = toLower(file.extension().string());
  if (ext == ".html") {
    return "text/html; charset=utf-8";
  }
  if (ext == ".css") {
    return "text/css; charset=utf-8";
  }
  if (ext == ".js") {
    return "application/javascript; charset=utf-8";
  }
  if (ext == ".json") {
    return "application/json; charset=utf-8";
  }
  if (ext == ".svg") {
    return "image/svg+xml";
  }
  if (ext == ".png") {
    return "image/png";
  }
  if (ext == ".jpg" || ext == ".jpeg") {
    return "image/jpeg";
  }
  if (ext == ".ico") {
    return "image/x-icon";
  }
  return "application/octet-stream";
}

inline int clampDmx(int value) {
  if (value < 0) {
    return 0;
  }
  if (value > 255) {
    return 255;
  }
  return value;
}

}  // namespace tuxdmx

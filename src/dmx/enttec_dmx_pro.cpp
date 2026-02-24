#include "enttec_dmx_pro.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#else
#include <errno.h>
#include <fcntl.h>
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>
#endif

#include "logger.hpp"
#include "utils.hpp"

namespace tuxdmx {

namespace {

constexpr std::uint8_t kStartByte = 0x7E;
constexpr std::uint8_t kEndByte = 0xE7;
constexpr std::uint8_t kLabelGetWidgetParams = 0x03;
constexpr std::uint8_t kLabelSetDmx = 0x06;
constexpr std::uint8_t kLabelGetSerial = 0x0A;

#ifndef _WIN32
bool readBytesWithTimeout(int fd, std::uint8_t* dst, std::size_t size, int timeoutMs) {
  auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
  std::size_t offset = 0;

  while (offset < size) {
    const auto now = std::chrono::steady_clock::now();
    if (now >= deadline) {
      return false;
    }

    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();

    fd_set readSet;
    FD_ZERO(&readSet);
    FD_SET(fd, &readSet);

    timeval timeout{};
    timeout.tv_sec = static_cast<long>(remaining / 1000);
    timeout.tv_usec = static_cast<long>((remaining % 1000) * 1000);

    const int rc = select(fd + 1, &readSet, nullptr, nullptr, &timeout);
    if (rc > 0 && FD_ISSET(fd, &readSet)) {
      const auto chunk = read(fd, dst + offset, size - offset);
      if (chunk == 0) {
        continue;
      }
      if (chunk < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
          continue;
        }
        return false;
      }
      offset += static_cast<std::size_t>(chunk);
      continue;
    }

    if (rc == 0) {
      return false;
    }

    if (errno != EINTR) {
      return false;
    }
  }

  return true;
}

bool configureSerialPort(int fd) {
  termios tty{};
  if (tcgetattr(fd, &tty) != 0) {
    return false;
  }

  cfsetispeed(&tty, B57600);
  cfsetospeed(&tty, B57600);

  tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8;
  tty.c_iflag = 0;
  tty.c_oflag = 0;
  tty.c_lflag = 0;
  tty.c_cflag |= CLOCAL | CREAD;
  tty.c_cflag &= ~(PARENB | PARODD);
  tty.c_cflag &= ~CSTOPB;
  tty.c_cflag &= ~CRTSCTS;
  tty.c_cc[VMIN] = 0;
  tty.c_cc[VTIME] = 1;

  if (tcsetattr(fd, TCSANOW, &tty) != 0) {
    return false;
  }

  tcflush(fd, TCIOFLUSH);
  return true;
}
#endif

std::string parseSerialHex(const std::vector<std::uint8_t>& payload) {
  if (payload.empty()) {
    return {};
  }

  constexpr char hex[] = "0123456789ABCDEF";
  std::string out;
  out.reserve(payload.size() * 2);
  for (auto it = payload.rbegin(); it != payload.rend(); ++it) {
    const auto byte = *it;
    out.push_back(hex[(byte >> 4U) & 0x0F]);
    out.push_back(hex[byte & 0x0F]);
  }
  return out;
}

std::string makeDeviceName(std::string_view endpoint, std::string_view serial) {
  const std::string trimmedSerial = trim(serial);
  const std::string trimmedEndpoint = trim(endpoint);
  if (!trimmedSerial.empty() && !trimmedEndpoint.empty()) {
    return "ENTTEC DMX USB Pro " + trimmedSerial + " @ " + trimmedEndpoint;
  }
  if (!trimmedSerial.empty()) {
    return "ENTTEC DMX USB Pro " + trimmedSerial;
  }
  if (!trimmedEndpoint.empty()) {
    return "ENTTEC DMX USB Pro @ " + trimmedEndpoint;
  }
  return "ENTTEC DMX USB Pro";
}

}  // namespace

EnttecDmxPro::EnttecDmxPro() {
  status_.backend = backendName();
  status_.preferredDeviceId = preferredDeviceId_;
}

EnttecDmxPro::~EnttecDmxPro() { disconnect(); }

std::string EnttecDmxPro::backendName() const { return "enttec-usb-pro"; }

std::string EnttecDmxPro::deviceIdFor(std::string_view port, std::string_view serial) {
  const std::string trimmedSerial = trim(serial);
  if (!trimmedSerial.empty()) {
    return "serial:" + toLower(trimmedSerial);
  }
  const std::string trimmedPort = trim(port);
  if (!trimmedPort.empty()) {
    return "port:" + toLower(trimmedPort);
  }
  return {};
}

bool EnttecDmxPro::deviceIdMatches(const DmxOutputDevice& device, std::string_view preferredId) {
  const std::string preferred = toLower(trim(preferredId));
  if (preferred.empty() || preferred == "auto") {
    return true;
  }

  if (preferred == toLower(device.id)) {
    return true;
  }
  if (!device.serial.empty() && preferred == toLower(device.serial)) {
    return true;
  }
  if (!device.serial.empty() && preferred == "serial:" + toLower(device.serial)) {
    return true;
  }
  if (!device.endpoint.empty() && preferred == toLower(device.endpoint)) {
    return true;
  }
  if (!device.endpoint.empty() && preferred == "port:" + toLower(device.endpoint)) {
    return true;
  }
  return false;
}

std::vector<std::string> EnttecDmxPro::candidatePorts() const {
  std::vector<std::string> candidates;

#ifdef _WIN32
  for (int i = 1; i <= 64; ++i) {
    candidates.push_back("COM" + std::to_string(i));
  }
#else
  const std::array<std::string, 6> prefixes = {
      "ttyUSB", "ttyACM", "cu.usbserial", "tty.usbserial", "cu.usbmodem", "tty.usbmodem"};

  std::error_code ec;
  for (const auto& entry : std::filesystem::directory_iterator("/dev", ec)) {
    if (ec) {
      break;
    }
    const auto name = entry.path().filename().string();
    for (const auto& prefix : prefixes) {
      if (name.rfind(prefix, 0) == 0) {
        candidates.push_back(entry.path().string());
      }
    }
  }

  const auto byIdPath = std::filesystem::path("/dev/serial/by-id");
  if (std::filesystem::exists(byIdPath, ec) && std::filesystem::is_directory(byIdPath, ec)) {
    for (const auto& entry : std::filesystem::directory_iterator(byIdPath, ec)) {
      if (ec) {
        break;
      }
      candidates.push_back(entry.path().string());
    }
  }
#endif

  std::sort(candidates.begin(), candidates.end());
  candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());
  return candidates;
}

bool EnttecDmxPro::writeBytes(const std::uint8_t* data, std::size_t size) {
#ifdef _WIN32
  if (handle_ == nullptr) {
    return false;
  }

  DWORD bytesWritten = 0;
  const BOOL ok = WriteFile(static_cast<HANDLE>(handle_), data, static_cast<DWORD>(size), &bytesWritten, nullptr);
  return ok == TRUE && bytesWritten == size;
#else
  if (fd_ < 0) {
    return false;
  }

  std::size_t offset = 0;
  while (offset < size) {
    const auto written = write(fd_, data + offset, size - offset);
    if (written < 0) {
      if (errno == EINTR) {
        continue;
      }
      return false;
    }
    offset += static_cast<std::size_t>(written);
  }

  return true;
#endif
}

bool EnttecDmxPro::readFrame(std::uint8_t expectedLabel, std::vector<std::uint8_t>& payload, int timeoutMs) {
  payload.clear();

#ifdef _WIN32
  if (handle_ == nullptr) {
    return false;
  }

  auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
  std::uint8_t byte = 0;
  while (std::chrono::steady_clock::now() < deadline) {
    DWORD read = 0;
    if (!ReadFile(static_cast<HANDLE>(handle_), &byte, 1, &read, nullptr)) {
      return false;
    }
    if (read == 1 && byte == kStartByte) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  if (byte != kStartByte) {
    return false;
  }

  std::array<std::uint8_t, 3> header{};
  DWORD read = 0;
  if (!ReadFile(static_cast<HANDLE>(handle_), header.data(), static_cast<DWORD>(header.size()), &read, nullptr) ||
      read != header.size()) {
    return false;
  }

  const auto label = header[0];
  const auto length = static_cast<std::size_t>(header[1] | (header[2] << 8U));
  payload.resize(length);

  if (length > 0) {
    if (!ReadFile(static_cast<HANDLE>(handle_), payload.data(), static_cast<DWORD>(length), &read, nullptr) ||
        read != length) {
      return false;
    }
  }

  std::uint8_t end = 0;
  if (!ReadFile(static_cast<HANDLE>(handle_), &end, 1, &read, nullptr) || read != 1 || end != kEndByte) {
    return false;
  }

  return label == expectedLabel;
#else
  if (fd_ < 0) {
    return false;
  }

  auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
  std::uint8_t start = 0;

  while (std::chrono::steady_clock::now() < deadline) {
    if (!readBytesWithTimeout(fd_, &start, 1, 25)) {
      continue;
    }
    if (start == kStartByte) {
      break;
    }
  }

  if (start != kStartByte) {
    return false;
  }

  std::array<std::uint8_t, 3> header{};
  if (!readBytesWithTimeout(fd_, header.data(), header.size(), timeoutMs)) {
    return false;
  }

  const auto label = header[0];
  const auto length = static_cast<std::size_t>(header[1] | (header[2] << 8U));
  payload.resize(length);

  if (length > 0 && !readBytesWithTimeout(fd_, payload.data(), length, timeoutMs)) {
    return false;
  }

  std::uint8_t end = 0;
  if (!readBytesWithTimeout(fd_, &end, 1, timeoutMs) || end != kEndByte) {
    return false;
  }

  return label == expectedLabel;
#endif
}

bool EnttecDmxPro::probePort(const std::string& port, std::string& serial, int& fwMajor, int& fwMinor,
                             std::string& error) {
#ifdef _WIN32
  const std::string path = "\\\\.\\" + port;
  HANDLE handle = CreateFileA(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
  if (handle == INVALID_HANDLE_VALUE) {
    error = "Port open failed";
    return false;
  }

  DCB dcb{};
  dcb.DCBlength = sizeof(DCB);
  if (!GetCommState(handle, &dcb)) {
    CloseHandle(handle);
    error = "Failed to read serial settings";
    return false;
  }

  dcb.BaudRate = CBR_57600;
  dcb.ByteSize = 8;
  dcb.Parity = NOPARITY;
  dcb.StopBits = ONESTOPBIT;
  if (!SetCommState(handle, &dcb)) {
    CloseHandle(handle);
    error = "Failed to apply serial settings";
    return false;
  }

  COMMTIMEOUTS timeouts{};
  timeouts.ReadIntervalTimeout = 20;
  timeouts.ReadTotalTimeoutConstant = 50;
  timeouts.ReadTotalTimeoutMultiplier = 5;
  timeouts.WriteTotalTimeoutConstant = 50;
  timeouts.WriteTotalTimeoutMultiplier = 5;
  SetCommTimeouts(handle, &timeouts);

  {
    std::scoped_lock lock(mutex_);
    if (handle_ != nullptr) {
      CloseHandle(static_cast<HANDLE>(handle_));
    }
    handle_ = handle;
  }
#else
  const int fd = open(port.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
  if (fd < 0) {
    error = "Port open failed";
    return false;
  }

  if (!configureSerialPort(fd)) {
    close(fd);
    error = "Failed to configure serial port";
    return false;
  }

  if (fcntl(fd, F_SETFL, 0) != 0) {
    close(fd);
    error = "Failed to set serial blocking mode";
    return false;
  }

  {
    std::scoped_lock lock(mutex_);
    if (fd_ >= 0) {
      close(fd_);
    }
    fd_ = fd;
  }
#endif

  const std::array<std::uint8_t, 5> serialReq = {kStartByte, kLabelGetSerial, 0x00, 0x00, kEndByte};
  if (!writeBytes(serialReq.data(), serialReq.size())) {
    error = "Failed to write serial request";
    disconnect();
    return false;
  }

  std::vector<std::uint8_t> serialPayload;
  if (!readFrame(kLabelGetSerial, serialPayload, 300)) {
    error = "No valid serial response";
    disconnect();
    return false;
  }

  const std::array<std::uint8_t, 5> widgetReq = {kStartByte, kLabelGetWidgetParams, 0x00, 0x00, kEndByte};
  writeBytes(widgetReq.data(), widgetReq.size());

  std::vector<std::uint8_t> widgetPayload;
  if (readFrame(kLabelGetWidgetParams, widgetPayload, 200) && widgetPayload.size() >= 2) {
    fwMajor = static_cast<int>(widgetPayload[0]);
    fwMinor = static_cast<int>(widgetPayload[1]);
  } else {
    fwMajor = 0;
    fwMinor = 0;
  }

  serial = parseSerialHex(serialPayload);
  error.clear();
  return true;
}

void EnttecDmxPro::refreshDevicesUnlocked(std::string& error) {
  error.clear();
  std::vector<DmxOutputDevice> discovered;

  const auto ports = candidatePorts();
  if (ports.empty()) {
    std::scoped_lock lock(mutex_);
    devices_.clear();
    error = "No candidate serial ports found";
    return;
  }

  std::string firstProbeError;
  for (const auto& port : ports) {
    std::string serial;
    std::string probeError;
    int fwMajor = 0;
    int fwMinor = 0;

    if (probePort(port, serial, fwMajor, fwMinor, probeError)) {
      DmxOutputDevice device;
      device.id = deviceIdFor(port, serial);
      device.name = makeDeviceName(port, serial);
      device.endpoint = port;
      device.serial = serial;
      device.firmwareMajor = fwMajor;
      device.firmwareMinor = fwMinor;
      discovered.push_back(std::move(device));
      disconnect();
      continue;
    }

    if (firstProbeError.empty() && !probeError.empty()) {
      firstProbeError = probeError;
    }
  }

  std::sort(discovered.begin(), discovered.end(),
            [](const DmxOutputDevice& a, const DmxOutputDevice& b) { return a.endpoint < b.endpoint; });
  discovered.erase(std::unique(discovered.begin(), discovered.end(),
                               [](const DmxOutputDevice& a, const DmxOutputDevice& b) {
                                 return !a.id.empty() && !b.id.empty() && a.id == b.id;
                               }),
                   discovered.end());

  {
    std::scoped_lock lock(mutex_);
    devices_ = discovered;
  }

  if (discovered.empty()) {
    error = firstProbeError.empty() ? "No compatible DMX USB Pro devices found" : firstProbeError;
  }
}

bool EnttecDmxPro::discoverAndConnect() {
  std::scoped_lock discoveryLock(discoveryMutex_);
  {
    std::scoped_lock lock(mutex_);
    if (status_.connected) {
      return true;
    }
  }

  std::string scanError;
  refreshDevicesUnlocked(scanError);

  std::vector<DmxOutputDevice> availableDevices;
  std::string preferredId;
  {
    std::scoped_lock lock(mutex_);
    availableDevices = devices_;
    preferredId = preferredDeviceId_;
  }

  if (availableDevices.empty()) {
    std::scoped_lock lock(mutex_);
    status_.backend = backendName();
    status_.connected = false;
    status_.endpoint.clear();
    status_.activeDeviceId.clear();
    status_.preferredDeviceId = preferredDeviceId_;
    status_.lastError = scanError.empty() ? "No compatible DMX USB Pro devices found" : scanError;
    return false;
  }

  DmxOutputDevice selected = availableDevices.front();
  if (!trim(preferredId).empty()) {
    const auto it = std::find_if(availableDevices.begin(), availableDevices.end(),
                                 [&preferredId](const DmxOutputDevice& device) { return deviceIdMatches(device, preferredId); });
    if (it == availableDevices.end()) {
      std::scoped_lock lock(mutex_);
      status_.backend = backendName();
      status_.connected = false;
      status_.endpoint.clear();
      status_.activeDeviceId.clear();
      status_.preferredDeviceId = preferredDeviceId_;
      status_.lastError = "Preferred DMX device not found: " + preferredId;
      return false;
    }
    selected = *it;
  }

  std::string serial;
  std::string probeError;
  int fwMajor = 0;
  int fwMinor = 0;
  if (!probePort(selected.endpoint, serial, fwMajor, fwMinor, probeError)) {
    std::scoped_lock lock(mutex_);
    status_.backend = backendName();
    status_.connected = false;
    status_.endpoint.clear();
    status_.activeDeviceId.clear();
    status_.preferredDeviceId = preferredDeviceId_;
    status_.lastError = probeError.empty() ? "Port probe failed" : probeError;
    return false;
  }

  const std::string activeDeviceId = deviceIdFor(selected.endpoint, serial);
  {
    std::scoped_lock lock(mutex_);
    status_.backend = backendName();
    status_.connected = true;
    status_.endpoint = selected.endpoint;
    status_.activeDeviceId = activeDeviceId;
    status_.preferredDeviceId = preferredDeviceId_;
    status_.port = selected.endpoint;
    status_.serial = serial;
    status_.firmwareMajor = fwMajor;
    status_.firmwareMinor = fwMinor;
    status_.lastError.clear();
    consecutiveWriteFailures_ = 0;

    bool foundConnected = false;
    for (auto& device : devices_) {
      const bool isConnected =
          deviceIdMatches(device, activeDeviceId) || (!serial.empty() && !device.serial.empty() && device.serial == serial);
      device.connected = isConnected;
      foundConnected = foundConnected || isConnected;
    }
    if (!foundConnected) {
      DmxOutputDevice current;
      current.id = activeDeviceId;
      current.name = makeDeviceName(selected.endpoint, serial);
      current.endpoint = selected.endpoint;
      current.serial = serial;
      current.firmwareMajor = fwMajor;
      current.firmwareMinor = fwMinor;
      current.connected = true;
      devices_.push_back(std::move(current));
    }
  }

  logMessage(LogLevel::Info, "dmx", "Connected to ENTTEC DMX USB Pro on " + selected.endpoint + " (serial " + serial + ")");
  return true;
}

void EnttecDmxPro::disconnect() {
  std::scoped_lock lock(mutex_);

#ifdef _WIN32
  if (handle_ != nullptr) {
    CloseHandle(static_cast<HANDLE>(handle_));
    handle_ = nullptr;
  }
#else
  if (fd_ >= 0) {
    close(fd_);
    fd_ = -1;
  }
#endif

  status_.connected = false;
  status_.endpoint.clear();
  status_.activeDeviceId.clear();
  status_.preferredDeviceId = preferredDeviceId_;
  status_.port.clear();
  status_.serial.clear();
  status_.firmwareMajor = 0;
  status_.firmwareMinor = 0;
  for (auto& device : devices_) {
    device.connected = false;
  }
  consecutiveWriteFailures_ = 0;
}

bool EnttecDmxPro::sendUniverse(const std::array<std::uint8_t, 512>& channels) {
  std::scoped_lock lock(mutex_);
  if (!status_.connected) {
    return false;
  }

  std::array<std::uint8_t, 518> frame{};
  frame[0] = kStartByte;
  frame[1] = kLabelSetDmx;
  frame[2] = 0x01;
  frame[3] = 0x02;
  frame[4] = 0x00;
  for (std::size_t i = 0; i < channels.size(); ++i) {
    frame[5 + i] = channels[i];
  }
  frame[517] = kEndByte;

  if (!writeBytes(frame.data(), frame.size())) {
    ++consecutiveWriteFailures_;
    if (consecutiveWriteFailures_ < writeRetryLimit_) {
      status_.lastError =
          "DMX write failed (" + std::to_string(consecutiveWriteFailures_) + "/" + std::to_string(writeRetryLimit_)
          + "), retrying";
      logMessage(LogLevel::Warn, "dmx", status_.lastError);
      return false;
    }

    status_.connected = false;
    status_.activeDeviceId.clear();
    status_.lastError =
        "DMX write failed repeatedly (" + std::to_string(consecutiveWriteFailures_) + " tries), reconnecting";
    logMessage(LogLevel::Error, "dmx", status_.lastError);
#ifdef _WIN32
    if (handle_ != nullptr) {
      CloseHandle(static_cast<HANDLE>(handle_));
      handle_ = nullptr;
    }
#else
    if (fd_ >= 0) {
      close(fd_);
      fd_ = -1;
    }
#endif
    for (auto& device : devices_) {
      device.connected = false;
    }
    consecutiveWriteFailures_ = 0;
    return false;
  }

  consecutiveWriteFailures_ = 0;
  return true;
}

void EnttecDmxPro::setWriteRetryLimit(int limit) {
  std::scoped_lock lock(mutex_);
  writeRetryLimit_ = std::clamp(limit, 1, 200);
  status_.lastError.clear();
}

int EnttecDmxPro::writeRetryLimit() const {
  std::scoped_lock lock(mutex_);
  return writeRetryLimit_;
}

std::vector<DmxOutputDevice> EnttecDmxPro::devices() const {
  std::scoped_lock lock(mutex_);
  std::vector<DmxOutputDevice> out = devices_;
  if (out.empty() && status_.connected) {
    DmxOutputDevice current;
    current.id = status_.activeDeviceId;
    current.name = makeDeviceName(status_.endpoint, status_.serial);
    current.endpoint = status_.endpoint;
    current.serial = status_.serial;
    current.firmwareMajor = status_.firmwareMajor;
    current.firmwareMinor = status_.firmwareMinor;
    current.connected = true;
    out.push_back(std::move(current));
    return out;
  }

  for (auto& device : out) {
    const bool isConnected = status_.connected
                             && (deviceIdMatches(device, status_.activeDeviceId)
                                 || (!status_.serial.empty() && !device.serial.empty() && status_.serial == device.serial));
    device.connected = isConnected;
  }
  return out;
}

void EnttecDmxPro::refreshDevices() {
  std::scoped_lock discoveryLock(discoveryMutex_);
  bool connected = false;
  {
    std::scoped_lock lock(mutex_);
    connected = status_.connected;
  }

  if (connected) {
    return;
  }

  std::string error;
  refreshDevicesUnlocked(error);
  if (!error.empty()) {
    std::scoped_lock lock(mutex_);
    if (!status_.connected) {
      status_.lastError = error;
    }
  }
}

void EnttecDmxPro::setPreferredDeviceId(std::string deviceId) {
  const std::string normalized = toLower(trim(deviceId));
  std::scoped_lock lock(mutex_);
  preferredDeviceId_ = normalized == "auto" ? "" : normalized;
  status_.preferredDeviceId = preferredDeviceId_;
}

std::string EnttecDmxPro::preferredDeviceId() const {
  std::scoped_lock lock(mutex_);
  return preferredDeviceId_;
}

DmxDeviceStatus EnttecDmxPro::status() const {
  std::scoped_lock lock(mutex_);
  DmxDeviceStatus out = status_;
  out.backend = backendName();
  if (out.endpoint.empty()) {
    out.endpoint = out.port;
  }
  out.preferredDeviceId = preferredDeviceId_;
  out.writeRetryLimit = writeRetryLimit_;
  out.consecutiveWriteFailures = consecutiveWriteFailures_;
  return out;
}

}  // namespace tuxdmx

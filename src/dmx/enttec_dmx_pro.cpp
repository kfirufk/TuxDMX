#include "enttec_dmx_pro.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <thread>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <setupapi.h>
#include <winreg.h>
#else
#include <cerrno>
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
constexpr char kFtdiPrefix[] = "ftdi:";

std::int64_t nowUnixMs() {
  const auto now = std::chrono::system_clock::now();
  return std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
}

#ifndef _WIN32
bool readBytesWithTimeout(int fd, std::uint8_t* dst, std::size_t size, int timeoutMs) {
  auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
  std::size_t offset = 0;

  while (offset < size) {
    const auto now = std::chrono::steady_clock::now();
    if (now >= deadline) {
      errno = ETIMEDOUT;
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
      errno = ETIMEDOUT;
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

#else
using FtStatus = unsigned long;
using FtHandle = void*;
using FtUlong = unsigned long;
using FtUchar = unsigned char;
using FtUshort = unsigned short;

constexpr FtStatus kFtOk = 0;
constexpr DWORD kFtOpenBySerialNumber = 1;
constexpr FtUlong kFtPurgeRx = 1;
constexpr FtUlong kFtPurgeTx = 2;

struct FtdiApi {
  HMODULE module = nullptr;
  bool loadAttempted = false;
  bool loaded = false;

  FtStatus(__stdcall* createDeviceInfoList)(DWORD*) = nullptr;
  FtStatus(__stdcall* getDeviceInfoDetail)(DWORD, DWORD*, DWORD*, DWORD*, DWORD*, char*, char*, FtHandle*) = nullptr;
  FtStatus(__stdcall* open)(int, FtHandle*) = nullptr;
  FtStatus(__stdcall* openEx)(void*, DWORD, FtHandle*) = nullptr;
  FtStatus(__stdcall* close)(FtHandle) = nullptr;
  FtStatus(__stdcall* read)(FtHandle, void*, DWORD, DWORD*) = nullptr;
  FtStatus(__stdcall* write)(FtHandle, void*, DWORD, DWORD*) = nullptr;
  FtStatus(__stdcall* setBaudRate)(FtHandle, FtUlong) = nullptr;
  FtStatus(__stdcall* setDataCharacteristics)(FtHandle, FtUchar, FtUchar, FtUchar) = nullptr;
  FtStatus(__stdcall* setFlowControl)(FtHandle, FtUshort, FtUchar, FtUchar) = nullptr;
  FtStatus(__stdcall* setTimeouts)(FtHandle, FtUlong, FtUlong) = nullptr;
  FtStatus(__stdcall* purge)(FtHandle, FtUlong) = nullptr;

  bool ensureLoaded() {
    if (loadAttempted) {
      return loaded;
    }
    loadAttempted = true;

    module = LoadLibraryA("ftd2xx.dll");
    if (module == nullptr) {
      return false;
    }

    createDeviceInfoList = reinterpret_cast<decltype(createDeviceInfoList)>(GetProcAddress(module, "FT_CreateDeviceInfoList"));
    getDeviceInfoDetail = reinterpret_cast<decltype(getDeviceInfoDetail)>(GetProcAddress(module, "FT_GetDeviceInfoDetail"));
    open = reinterpret_cast<decltype(open)>(GetProcAddress(module, "FT_Open"));
    openEx = reinterpret_cast<decltype(openEx)>(GetProcAddress(module, "FT_OpenEx"));
    close = reinterpret_cast<decltype(close)>(GetProcAddress(module, "FT_Close"));
    read = reinterpret_cast<decltype(read)>(GetProcAddress(module, "FT_Read"));
    write = reinterpret_cast<decltype(write)>(GetProcAddress(module, "FT_Write"));
    setBaudRate = reinterpret_cast<decltype(setBaudRate)>(GetProcAddress(module, "FT_SetBaudRate"));
    setDataCharacteristics =
        reinterpret_cast<decltype(setDataCharacteristics)>(GetProcAddress(module, "FT_SetDataCharacteristics"));
    setFlowControl = reinterpret_cast<decltype(setFlowControl)>(GetProcAddress(module, "FT_SetFlowControl"));
    setTimeouts = reinterpret_cast<decltype(setTimeouts)>(GetProcAddress(module, "FT_SetTimeouts"));
    purge = reinterpret_cast<decltype(purge)>(GetProcAddress(module, "FT_Purge"));

    loaded = createDeviceInfoList != nullptr && getDeviceInfoDetail != nullptr && open != nullptr && openEx != nullptr
             && close != nullptr && read != nullptr && write != nullptr && setBaudRate != nullptr
             && setDataCharacteristics != nullptr && setFlowControl != nullptr && setTimeouts != nullptr && purge != nullptr;
    return loaded;
  }
};

FtdiApi& ftdiApi() {
  static FtdiApi api;
  return api;
}

bool isFtdiEndpoint(std::string_view endpoint) { return toLower(trim(endpoint)).rfind(kFtdiPrefix, 0) == 0; }

std::string ftdiEndpointSerial(std::string_view endpoint) {
  const std::string text = toLower(trim(endpoint));
  if (text.rfind(kFtdiPrefix, 0) != 0) {
    return {};
  }
  return trim(std::string(endpoint.substr(std::string_view(kFtdiPrefix).size())));
}

std::vector<std::string> enumerateWindowsComPortsFromRegistry() {
  std::vector<std::string> ports;

  HKEY key = nullptr;
  if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "HARDWARE\\DEVICEMAP\\SERIALCOMM", 0, KEY_READ, &key) != ERROR_SUCCESS) {
    return ports;
  }

  DWORD valueCount = 0;
  DWORD maxValueNameLen = 0;
  DWORD maxValueLen = 0;
  if (RegQueryInfoKeyA(key, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, &valueCount, &maxValueNameLen, &maxValueLen,
                       nullptr, nullptr) != ERROR_SUCCESS) {
    RegCloseKey(key);
    return ports;
  }

  std::vector<char> valueName(maxValueNameLen + 2, '\0');
  std::vector<std::uint8_t> valueData(maxValueLen + 2, 0);

  for (DWORD index = 0; index < valueCount; ++index) {
    DWORD valueNameLen = static_cast<DWORD>(valueName.size() - 1);
    DWORD valueLen = static_cast<DWORD>(valueData.size());
    DWORD type = 0;
    const LONG rc = RegEnumValueA(key, index, valueName.data(), &valueNameLen, nullptr, &type, valueData.data(), &valueLen);
    if (rc != ERROR_SUCCESS) {
      continue;
    }
    if (type != REG_SZ && type != REG_EXPAND_SZ) {
      continue;
    }
    if (valueLen == 0) {
      continue;
    }

    const char* raw = reinterpret_cast<const char*>(valueData.data());
    const std::string port = trim(raw);
    if (port.empty()) {
      continue;
    }
    if (toLower(port).rfind("com", 0) == 0) {
      ports.push_back(port);
    }
  }

  RegCloseKey(key);
  std::sort(ports.begin(), ports.end());
  ports.erase(std::unique(ports.begin(), ports.end()), ports.end());
  return ports;
}

std::vector<std::string> enumerateWindowsComPorts() {
  std::vector<std::string> ports;

  DWORD requiredGuids = 0;
  if (!SetupDiClassGuidsFromNameA("Ports", nullptr, 0, &requiredGuids) || requiredGuids == 0) {
    return ports;
  }

  std::vector<GUID> guids(requiredGuids);
  if (!SetupDiClassGuidsFromNameA("Ports", guids.data(), static_cast<DWORD>(guids.size()), &requiredGuids)
      || requiredGuids == 0) {
    return ports;
  }

  for (DWORD guidIndex = 0; guidIndex < requiredGuids; ++guidIndex) {
    HDEVINFO deviceInfoSet = SetupDiGetClassDevsA(&guids[guidIndex], nullptr, nullptr, DIGCF_PRESENT);
    if (deviceInfoSet == INVALID_HANDLE_VALUE) {
      continue;
    }

    SP_DEVINFO_DATA devInfo{};
    devInfo.cbSize = sizeof(SP_DEVINFO_DATA);

    for (DWORD deviceIndex = 0; SetupDiEnumDeviceInfo(deviceInfoSet, deviceIndex, &devInfo); ++deviceIndex) {
      HKEY deviceKey = SetupDiOpenDevRegKey(deviceInfoSet, &devInfo, DICS_FLAG_GLOBAL, 0, DIREG_DEV, KEY_QUERY_VALUE);
      if (deviceKey == INVALID_HANDLE_VALUE) {
        continue;
      }

      char portName[256] = {};
      DWORD type = 0;
      DWORD size = static_cast<DWORD>(sizeof(portName));
      const LONG query = RegQueryValueExA(deviceKey, "PortName", nullptr, &type, reinterpret_cast<LPBYTE>(portName), &size);
      RegCloseKey(deviceKey);

      if (query != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ)) {
        continue;
      }

      const std::string trimmed = trim(portName);
      if (trimmed.empty()) {
        continue;
      }
      if (toLower(trimmed).rfind("com", 0) == 0) {
        ports.push_back(trimmed);
      }
    }

    SetupDiDestroyDeviceInfoList(deviceInfoSet);
  }

  std::sort(ports.begin(), ports.end());
  ports.erase(std::unique(ports.begin(), ports.end()), ports.end());

  // Add registry-reported serial ports (can include devices not surfaced by SetupAPI path).
  {
    auto regPorts = enumerateWindowsComPortsFromRegistry();
    ports.insert(ports.end(), regPorts.begin(), regPorts.end());
  }

  // Fallback/augmentation for environments where SetupAPI class enumeration
  // misses some serial devices. Query DOS namespace for COM1..COM256.
  std::array<char, 512> devicePath{};
  for (int i = 1; i <= 256; ++i) {
    const std::string portName = "COM" + std::to_string(i);
    const DWORD rc = QueryDosDeviceA(portName.c_str(), devicePath.data(), static_cast<DWORD>(devicePath.size()));
    if (rc != 0) {
      ports.push_back(portName);
    }
  }

  std::sort(ports.begin(), ports.end());
  ports.erase(std::unique(ports.begin(), ports.end()), ports.end());
  return ports;
}

std::vector<std::string> enumerateWindowsFtdiEndpoints() {
  std::vector<std::string> endpoints;
  auto& api = ftdiApi();
  if (!api.ensureLoaded()) {
    static bool warnedMissingFtdi = false;
    if (!warnedMissingFtdi) {
      warnedMissingFtdi = true;
      logMessage(LogLevel::Info, "dmx", "FTDI D2XX runtime not found (ftd2xx.dll). FTDI fallback scan disabled.");
    }
    return endpoints;
  }

  DWORD count = 0;
  if (api.createDeviceInfoList(&count) != kFtOk || count == 0) {
    return endpoints;
  }

  for (DWORD i = 0; i < count; ++i) {
    DWORD flags = 0;
    DWORD type = 0;
    DWORD id = 0;
    DWORD locId = 0;
    char serial[64] = {};
    char desc[128] = {};
    FtHandle tmp = nullptr;
    const FtStatus rc = api.getDeviceInfoDetail(i, &flags, &type, &id, &locId, serial, desc, &tmp);
    if (rc != kFtOk) {
      continue;
    }
    const std::string serialText = trim(serial);
    if (serialText.empty()) {
      continue;
    }
    endpoints.push_back(std::string(kFtdiPrefix) + serialText);
  }

  std::sort(endpoints.begin(), endpoints.end());
  endpoints.erase(std::unique(endpoints.begin(), endpoints.end()), endpoints.end());
  return endpoints;
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

int nativeErrorCode() {
#ifdef _WIN32
  return static_cast<int>(GetLastError());
#else
  return errno;
#endif
}

#ifndef _WIN32
struct PosixSerialErrorDiagnosis {
  std::string kind;
  std::string hint;
  bool likelyUsbPower = false;
};

std::string errnoText(int code) {
  if (code <= 0) {
    return {};
  }
  const char* text = std::strerror(code);
  return text == nullptr ? std::string{} : std::string(text);
}

bool serialEndpointExists(std::string_view endpoint) {
  const std::string path = trim(endpoint);
  if (path.empty()) {
    return false;
  }
  std::error_code ec;
  return std::filesystem::exists(path, ec) && !ec;
}

PosixSerialErrorDiagnosis diagnosePosixSerialError(std::string_view stage, int code, std::string_view endpoint) {
  PosixSerialErrorDiagnosis diagnosis;
  diagnosis.kind = "unknown";

  switch (code) {
    case ETIMEDOUT:
      diagnosis.kind = "timeout";
      diagnosis.hint = "No response from the DMX interface before timeout.";
      break;
    case ENOENT:
    case ENXIO:
    case ENODEV:
      diagnosis.kind = "disconnected";
      diagnosis.hint = "Serial endpoint is not available; device may be unplugged or reset.";
      break;
    case EPIPE:
      diagnosis.kind = "disconnected";
      diagnosis.hint = "Broken serial pipe; device likely disconnected.";
      break;
    case EIO:
      diagnosis.kind = "io";
      diagnosis.hint = "Low-level USB I/O error. On macOS this can indicate cable/hub power instability.";
      diagnosis.likelyUsbPower = true;
      break;
    case EBUSY:
      diagnosis.kind = "busy";
      diagnosis.hint = "Serial endpoint is busy (another app may be using it).";
      break;
    case EACCES:
    case EPERM:
      diagnosis.kind = "permission";
      diagnosis.hint = "Permission denied while accessing serial endpoint.";
      break;
    default:
      break;
  }

  if (!trim(endpoint).empty() && !serialEndpointExists(endpoint)) {
    diagnosis.kind = "endpoint_missing";
    diagnosis.hint = "Serial endpoint disappeared from /dev; likely unplugged or USB hub power drop/reset.";
    diagnosis.likelyUsbPower = true;
  } else if (diagnosis.kind == "unknown" && stage == "probe") {
    diagnosis.hint = "Probe failed; verify cable, FTDI driver, and USB connection stability.";
  }

  return diagnosis;
}
#endif

std::string formatNativeErrorMessage(std::string_view base, int code, std::string_view stage, std::string_view endpoint,
                                     std::string* kind = nullptr, std::string* hint = nullptr,
                                     bool* likelyUsbPower = nullptr) {
  std::string message = trim(base);
#ifdef _WIN32
  if (code != 0) {
    if (!message.empty()) {
      message += ' ';
    }
    message += "(code " + std::to_string(code) + ")";
  }
  if (kind != nullptr) {
    kind->clear();
  }
  if (hint != nullptr) {
    hint->clear();
  }
  if (likelyUsbPower != nullptr) {
    *likelyUsbPower = false;
  }
#else
  const auto diagnosis = diagnosePosixSerialError(stage, code, endpoint);
  if (kind != nullptr) {
    *kind = diagnosis.kind;
  }
  if (hint != nullptr) {
    *hint = diagnosis.hint;
  }
  if (likelyUsbPower != nullptr) {
    *likelyUsbPower = diagnosis.likelyUsbPower;
  }

  if (code != 0) {
    const std::string codeText = errnoText(code);
    if (!message.empty()) {
      message += ' ';
    }
    message += "(errno " + std::to_string(code);
    if (!codeText.empty()) {
      message += ": " + codeText;
    }
    message += ")";
  }

  if (!diagnosis.hint.empty()) {
    if (!message.empty()) {
      message += ". ";
    }
    message += diagnosis.hint;
  }
#endif
  return message;
}

}  // namespace

EnttecDmxPro::EnttecDmxPro() {
  status_.backend = backendName();
  status_.preferredDeviceId = preferredDeviceId_;
  status_.writeRetryLimit = writeRetryLimit_;
  status_.probeTimeoutMs = probeTimeoutMs_;
  status_.serialReadTimeoutMs = serialReadTimeoutMs_;
  status_.strictPreferredDevice = strictPreferredDevice_;
  status_.transportState = "disconnected";
}

EnttecDmxPro::~EnttecDmxPro() { disconnect(); }

std::string EnttecDmxPro::backendName() const { return "enttec-usb-pro"; }

void EnttecDmxPro::setLastErrorUnlocked(std::string stage, std::string message, int code, std::string endpoint,
                                        std::string kind, std::string hint, bool likelyUsbPower) {
  status_.lastErrorStage = std::move(stage);
  status_.lastError = std::move(message);
  status_.lastErrorCode = code;
  status_.lastErrorEndpoint = std::move(endpoint);
  status_.lastErrorKind = std::move(kind);
  status_.lastErrorHint = std::move(hint);
  status_.lastErrorLikelyUsbPower = likelyUsbPower;
#ifndef _WIN32
  const auto diagnosis =
      diagnosePosixSerialError(status_.lastErrorStage, status_.lastErrorCode, status_.lastErrorEndpoint);
  if (status_.lastErrorKind.empty()) {
    status_.lastErrorKind = diagnosis.kind;
  }
  if (status_.lastErrorHint.empty()) {
    status_.lastErrorHint = diagnosis.hint;
  }
  status_.lastErrorLikelyUsbPower = status_.lastErrorLikelyUsbPower || diagnosis.likelyUsbPower;
#endif
  status_.lastErrorUnixMs = nowUnixMs();
}

void EnttecDmxPro::clearLastErrorUnlocked() {
  status_.lastError.clear();
  status_.lastErrorStage.clear();
  status_.lastErrorKind.clear();
  status_.lastErrorHint.clear();
  status_.lastErrorLikelyUsbPower = false;
  status_.lastErrorCode = 0;
  status_.lastErrorEndpoint.clear();
  status_.lastErrorUnixMs = 0;
}

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
  candidates = enumerateWindowsComPorts();
  {
    auto ftdiCandidates = enumerateWindowsFtdiEndpoints();
    candidates.insert(candidates.end(), ftdiCandidates.begin(), ftdiCandidates.end());
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

  if (usingFtdi_) {
    auto& api = ftdiApi();
    if (!api.ensureLoaded()) {
      return false;
    }
    DWORD bytesWritten = 0;
    const FtStatus rc = api.write(static_cast<FtHandle>(handle_), const_cast<std::uint8_t*>(data), static_cast<DWORD>(size),
                                  &bytesWritten);
    return rc == kFtOk && bytesWritten == size;
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

  if (usingFtdi_) {
    auto& api = ftdiApi();
    if (!api.ensureLoaded()) {
      return false;
    }

    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    auto readExact = [&](std::uint8_t* dst, std::size_t len, int chunkTimeoutMs) -> bool {
      std::size_t offset = 0;
      while (offset < len && std::chrono::steady_clock::now() < deadline) {
        const auto remainingMs =
            static_cast<int>(std::max<std::int64_t>(1, std::chrono::duration_cast<std::chrono::milliseconds>(deadline
                                                                                                                - std::chrono::steady_clock::now())
                                                           .count()));
        api.setTimeouts(static_cast<FtHandle>(handle_), static_cast<FtUlong>(std::min(remainingMs, chunkTimeoutMs)),
                        static_cast<FtUlong>(std::min(remainingMs, chunkTimeoutMs)));
        DWORD got = 0;
        const FtStatus rc = api.read(static_cast<FtHandle>(handle_), dst + offset, static_cast<DWORD>(len - offset), &got);
        if (rc != kFtOk) {
          return false;
        }
        if (got == 0) {
          continue;
        }
        offset += static_cast<std::size_t>(got);
      }
      return offset == len;
    };

    std::uint8_t start = 0;
    while (std::chrono::steady_clock::now() < deadline) {
      if (!readExact(&start, 1, 25)) {
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
    if (!readExact(header.data(), header.size(), timeoutMs)) {
      return false;
    }

    const auto label = header[0];
    const auto length = static_cast<std::size_t>(header[1] | (header[2] << 8U));
    payload.resize(length);
    if (length > 0 && !readExact(payload.data(), length, timeoutMs)) {
      return false;
    }

    std::uint8_t end = 0;
    if (!readExact(&end, 1, timeoutMs) || end != kEndByte) {
      return false;
    }
    return label == expectedLabel;
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
  if (!ReadFile(static_cast<HANDLE>(handle_), header.data(), static_cast<DWORD>(header.size()), &read, nullptr)
      || read != header.size()) {
    return false;
  }

  const auto label = header[0];
  const auto length = static_cast<std::size_t>(header[1] | (header[2] << 8U));
  payload.resize(length);

  if (length > 0) {
    if (!ReadFile(static_cast<HANDLE>(handle_), payload.data(), static_cast<DWORD>(length), &read, nullptr) || read != length) {
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
    errno = ETIMEDOUT;
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

bool EnttecDmxPro::probePort(const std::string& port, std::string& serial, int& fwMajor, int& fwMinor, int& errorCode,
                             std::string& error) {
  int probeTimeoutMs = 350;
  int serialReadTimeoutMs = 250;
  {
    std::scoped_lock lock(mutex_);
    probeTimeoutMs = probeTimeoutMs_;
    serialReadTimeoutMs = serialReadTimeoutMs_;
  }

#ifdef _WIN32
  if (isFtdiEndpoint(port)) {
    auto& api = ftdiApi();
    if (!api.ensureLoaded()) {
      errorCode = ERROR_MOD_NOT_FOUND;
      error = formatNativeErrorMessage("ftd2xx.dll not available", errorCode, "probe", port);
      return false;
    }

    const std::string serialKey = ftdiEndpointSerial(port);
    if (serialKey.empty()) {
      errorCode = ERROR_INVALID_PARAMETER;
      error = formatNativeErrorMessage("Invalid FTDI endpoint", errorCode, "probe", port);
      return false;
    }

    FtHandle ftHandle = nullptr;
    std::string serialArg = serialKey;
    const FtStatus openRc = api.openEx(const_cast<char*>(serialArg.c_str()), kFtOpenBySerialNumber, &ftHandle);
    if (openRc != kFtOk || ftHandle == nullptr) {
      errorCode = static_cast<int>(openRc);
      error = formatNativeErrorMessage("FTDI open failed", errorCode, "probe", port);
      return false;
    }

    api.setBaudRate(ftHandle, 57600);
    api.setDataCharacteristics(ftHandle, 8, 0, 0);
    api.setFlowControl(ftHandle, 0, 0, 0);
    api.setTimeouts(ftHandle, static_cast<FtUlong>(std::max(20, serialReadTimeoutMs)),
                    static_cast<FtUlong>(std::max(20, serialReadTimeoutMs)));
    api.purge(ftHandle, kFtPurgeRx | kFtPurgeTx);

    {
      std::scoped_lock lock(mutex_);
      if (handle_ != nullptr) {
        if (usingFtdi_) {
          api.close(static_cast<FtHandle>(handle_));
        } else {
          CloseHandle(static_cast<HANDLE>(handle_));
        }
      }
      handle_ = ftHandle;
      usingFtdi_ = true;
    }
  } else {
  const std::string path = "\\\\.\\" + port;
  HANDLE handle = CreateFileA(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
  if (handle == INVALID_HANDLE_VALUE) {
    errorCode = static_cast<int>(GetLastError());
    error = formatNativeErrorMessage("Port open failed", errorCode, "probe", port);
    return false;
  }

  DCB dcb{};
  dcb.DCBlength = sizeof(DCB);
  if (!GetCommState(handle, &dcb)) {
    errorCode = static_cast<int>(GetLastError());
    CloseHandle(handle);
    error = formatNativeErrorMessage("Failed to read serial settings", errorCode, "probe", port);
    return false;
  }

  dcb.BaudRate = CBR_57600;
  dcb.ByteSize = 8;
  dcb.Parity = NOPARITY;
  dcb.StopBits = ONESTOPBIT;
  if (!SetCommState(handle, &dcb)) {
    errorCode = static_cast<int>(GetLastError());
    CloseHandle(handle);
    error = formatNativeErrorMessage("Failed to apply serial settings", errorCode, "probe", port);
    return false;
  }

  COMMTIMEOUTS timeouts{};
  timeouts.ReadIntervalTimeout = static_cast<DWORD>(std::clamp(serialReadTimeoutMs / 5, 5, 60));
  timeouts.ReadTotalTimeoutConstant = static_cast<DWORD>(std::max(20, serialReadTimeoutMs));
  timeouts.ReadTotalTimeoutMultiplier = 3;
  timeouts.WriteTotalTimeoutConstant = static_cast<DWORD>(std::max(20, serialReadTimeoutMs));
  timeouts.WriteTotalTimeoutMultiplier = 3;
  SetCommTimeouts(handle, &timeouts);

  {
    std::scoped_lock lock(mutex_);
    if (handle_ != nullptr) {
      if (usingFtdi_) {
        auto& api = ftdiApi();
        if (api.ensureLoaded()) {
          api.close(static_cast<FtHandle>(handle_));
        }
      } else {
        CloseHandle(static_cast<HANDLE>(handle_));
      }
    }
    handle_ = handle;
    usingFtdi_ = false;
  }
  }
#else
  const int fd = open(port.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
  if (fd < 0) {
    errorCode = errno;
    error = formatNativeErrorMessage("Port open failed", errorCode, "probe", port);
    return false;
  }

  if (!configureSerialPort(fd)) {
    errorCode = errno;
    close(fd);
    error = formatNativeErrorMessage("Failed to configure serial port", errorCode, "probe", port);
    return false;
  }

  if (fcntl(fd, F_SETFL, 0) != 0) {
    errorCode = errno;
    close(fd);
    error = formatNativeErrorMessage("Failed to set serial blocking mode", errorCode, "probe", port);
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
    errorCode = nativeErrorCode();
    error = formatNativeErrorMessage("Failed to write serial request", errorCode, "probe", port);
    disconnect();
    return false;
  }

  std::vector<std::uint8_t> serialPayload;
  if (!readFrame(kLabelGetSerial, serialPayload, probeTimeoutMs)) {
    errorCode = nativeErrorCode();
    error = formatNativeErrorMessage("No valid serial response", errorCode, "probe", port);
    disconnect();
    return false;
  }

  const std::array<std::uint8_t, 5> widgetReq = {kStartByte, kLabelGetWidgetParams, 0x00, 0x00, kEndByte};
  writeBytes(widgetReq.data(), widgetReq.size());

  std::vector<std::uint8_t> widgetPayload;
  if (readFrame(kLabelGetWidgetParams, widgetPayload, std::max(80, probeTimeoutMs / 2)) && widgetPayload.size() >= 2) {
    fwMajor = static_cast<int>(widgetPayload[0]);
    fwMinor = static_cast<int>(widgetPayload[1]);
  } else {
    fwMajor = 0;
    fwMinor = 0;
  }

  serial = parseSerialHex(serialPayload);
  errorCode = 0;
  error.clear();
  return true;
}

void EnttecDmxPro::refreshDevicesUnlocked(std::string& error) {
  error.clear();
  std::vector<DmxOutputDevice> discovered;

  const auto ports = candidatePorts();
  if (ports.empty()) {
    logMessage(LogLevel::Warn, "dmx", "ENTTEC scan found 0 candidate serial ports");
  } else {
    std::string joined;
    for (std::size_t i = 0; i < ports.size(); ++i) {
      if (i > 0) {
        joined += ", ";
      }
      joined += ports[i];
    }
    logMessage(LogLevel::Info, "dmx",
               "ENTTEC scan candidates (" + std::to_string(ports.size()) + "): " + joined);
  }
  if (ports.empty()) {
    std::scoped_lock lock(mutex_);
    devices_.clear();
#ifdef _WIN32
    error = "No candidate serial ports found (check FTDI/USB driver exposes a COM port)";
#else
#if defined(__APPLE__)
    error = "No candidate serial ports found. Check cable/hub power and that /dev/cu.usbserial* is present.";
#else
    error = "No candidate serial ports found";
#endif
#endif
    return;
  }

  std::string firstProbeError;
  for (const auto& port : ports) {
    std::string serial;
    std::string probeError;
    int fwMajor = 0;
    int fwMinor = 0;
    int probeCode = 0;

    if (probePort(port, serial, fwMajor, fwMinor, probeCode, probeError)) {
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

    const std::string resolvedProbeError =
        probeError.empty() ? formatNativeErrorMessage("Probe failed", probeCode, "probe", port) : probeError;
    logMessage(LogLevel::Warn, "dmx", "Probe failed on " + port + ": " + resolvedProbeError);

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
    status_.lastConnectAttemptUnixMs = nowUnixMs();
  }

  std::string scanError;
  refreshDevicesUnlocked(scanError);

  std::vector<DmxOutputDevice> availableDevices;
  std::string preferredId;
  bool strictPreferred = true;
  {
    std::scoped_lock lock(mutex_);
    availableDevices = devices_;
    preferredId = preferredDeviceId_;
    strictPreferred = strictPreferredDevice_;
  }

  if (availableDevices.empty()) {
    std::scoped_lock lock(mutex_);
    status_.backend = backendName();
    status_.connected = false;
    status_.endpoint.clear();
    status_.activeDeviceId.clear();
    status_.preferredDeviceId = preferredDeviceId_;
    status_.transportState = "disconnected";
    setLastErrorUnlocked("scan", scanError.empty() ? "No compatible DMX USB Pro devices found" : scanError);
    return false;
  }

  DmxOutputDevice selected = availableDevices.front();
  if (!trim(preferredId).empty()) {
    const auto it = std::find_if(availableDevices.begin(), availableDevices.end(),
                                 [&preferredId](const DmxOutputDevice& device) { return deviceIdMatches(device, preferredId); });
    if (it == availableDevices.end()) {
      if (strictPreferred) {
        std::scoped_lock lock(mutex_);
        status_.backend = backendName();
        status_.connected = false;
        status_.endpoint.clear();
        status_.activeDeviceId.clear();
        status_.preferredDeviceId = preferredDeviceId_;
        status_.transportState = "disconnected";
        setLastErrorUnlocked("selection", "Preferred DMX device not found: " + preferredId);
        return false;
      }
      logMessage(LogLevel::Warn, "dmx", "Preferred device not found, falling back to first detected device: " + preferredId);
    } else {
      selected = *it;
    }
  }

  std::string serial;
  std::string probeError;
  int fwMajor = 0;
  int fwMinor = 0;
  int probeCode = 0;
  if (!probePort(selected.endpoint, serial, fwMajor, fwMinor, probeCode, probeError)) {
    std::scoped_lock lock(mutex_);
    status_.backend = backendName();
    status_.connected = false;
    status_.endpoint.clear();
    status_.activeDeviceId.clear();
    status_.preferredDeviceId = preferredDeviceId_;
    status_.transportState = "disconnected";
    setLastErrorUnlocked("probe", probeError.empty() ? "Port probe failed" : probeError, probeCode, selected.endpoint);
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
    status_.transportState = "connected";
    clearLastErrorUnlocked();
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
    if (usingFtdi_) {
      auto& api = ftdiApi();
      if (api.ensureLoaded()) {
        api.close(static_cast<FtHandle>(handle_));
      }
    } else {
      CloseHandle(static_cast<HANDLE>(handle_));
    }
    handle_ = nullptr;
    usingFtdi_ = false;
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
  status_.transportState = "disconnected";
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
    const int errorCode = nativeErrorCode();
    std::string errorKind;
    std::string errorHint;
    bool likelyUsbPower = false;
    const std::string errorDetail =
        formatNativeErrorMessage("DMX write failure", errorCode, "write", status_.endpoint, &errorKind, &errorHint,
                                 &likelyUsbPower);

    if (consecutiveWriteFailures_ < writeRetryLimit_) {
      const std::string error = "DMX write failed (" + std::to_string(consecutiveWriteFailures_) + "/"
                                + std::to_string(writeRetryLimit_) + "), retrying. " + errorDetail;
      setLastErrorUnlocked("write", error, errorCode, status_.endpoint, errorKind, errorHint, likelyUsbPower);
      logMessage(LogLevel::Warn, "dmx", error);
      status_.transportState = "degraded";
      return false;
    }

    const std::string error =
        "DMX write failed repeatedly (" + std::to_string(consecutiveWriteFailures_) + " tries), reconnecting. "
        + errorDetail;
    setLastErrorUnlocked("write", error, errorCode, status_.endpoint, errorKind, errorHint, likelyUsbPower);
    logMessage(LogLevel::Error, "dmx", error);

    status_.connected = false;
    status_.activeDeviceId.clear();
    status_.transportState = "degraded";
#ifdef _WIN32
    if (handle_ != nullptr) {
      if (usingFtdi_) {
        auto& api = ftdiApi();
        if (api.ensureLoaded()) {
          api.close(static_cast<FtHandle>(handle_));
        }
      } else {
        CloseHandle(static_cast<HANDLE>(handle_));
      }
      handle_ = nullptr;
      usingFtdi_ = false;
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
  status_.lastSuccessfulFrameUnixMs = nowUnixMs();
  status_.transportState = "connected";
  if (status_.lastErrorStage == "write") {
    clearLastErrorUnlocked();
  }
  return true;
}

void EnttecDmxPro::setWriteRetryLimit(int limit) {
  std::scoped_lock lock(mutex_);
  writeRetryLimit_ = std::clamp(limit, 1, 200);
  status_.writeRetryLimit = writeRetryLimit_;
}

int EnttecDmxPro::writeRetryLimit() const {
  std::scoped_lock lock(mutex_);
  return writeRetryLimit_;
}

void EnttecDmxPro::setProbeTimeoutMs(int timeoutMs) {
  std::scoped_lock lock(mutex_);
  probeTimeoutMs_ = std::clamp(timeoutMs, 50, 5000);
  status_.probeTimeoutMs = probeTimeoutMs_;
}

int EnttecDmxPro::probeTimeoutMs() const {
  std::scoped_lock lock(mutex_);
  return probeTimeoutMs_;
}

void EnttecDmxPro::setSerialReadTimeoutMs(int timeoutMs) {
  std::scoped_lock lock(mutex_);
  serialReadTimeoutMs_ = std::clamp(timeoutMs, 20, 5000);
  status_.serialReadTimeoutMs = serialReadTimeoutMs_;
}

int EnttecDmxPro::serialReadTimeoutMs() const {
  std::scoped_lock lock(mutex_);
  return serialReadTimeoutMs_;
}

void EnttecDmxPro::setStrictPreferredDevice(bool strict) {
  std::scoped_lock lock(mutex_);
  strictPreferredDevice_ = strict;
  status_.strictPreferredDevice = strictPreferredDevice_;
}

bool EnttecDmxPro::strictPreferredDevice() const {
  std::scoped_lock lock(mutex_);
  return strictPreferredDevice_;
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
      setLastErrorUnlocked("scan", error);
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
  out.probeTimeoutMs = probeTimeoutMs_;
  out.serialReadTimeoutMs = serialReadTimeoutMs_;
  out.strictPreferredDevice = strictPreferredDevice_;
  return out;
}

}  // namespace tuxdmx

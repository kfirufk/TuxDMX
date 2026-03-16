#pragma once

#include <array>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include "dmx_output_backend.hpp"

namespace tuxdmx {

class EnttecDmxPro final : public DmxOutputBackend {
 public:
  EnttecDmxPro();
  ~EnttecDmxPro();

  EnttecDmxPro(const EnttecDmxPro&) = delete;
  EnttecDmxPro& operator=(const EnttecDmxPro&) = delete;

  std::string backendName() const override;
  bool discoverAndConnect() override;
  void disconnect() override;

  bool sendUniverse(const std::array<std::uint8_t, 512>& channels) override;
  void setWriteRetryLimit(int limit) override;
  int writeRetryLimit() const override;
  void setProbeTimeoutMs(int timeoutMs) override;
  int probeTimeoutMs() const override;
  void setSerialReadTimeoutMs(int timeoutMs) override;
  int serialReadTimeoutMs() const override;
  void setStrictPreferredDevice(bool strict) override;
  bool strictPreferredDevice() const override;
  std::vector<DmxOutputDevice> devices() const override;
  void refreshDevices() override;
  void setPreferredDeviceId(std::string deviceId) override;
  std::string preferredDeviceId() const override;
  DmxDeviceStatus status() const override;

 private:
  bool probePort(const std::string& port, std::string& serial, int& fwMajor, int& fwMinor, int& errorCode,
                 std::string& error);
  void refreshDevicesUnlocked(std::string& error);
  void setLastErrorUnlocked(std::string stage, std::string message, int code = 0, std::string endpoint = {},
                            std::string kind = {}, std::string hint = {}, bool likelyUsbPower = false);
  void clearLastErrorUnlocked();
  bool reopenCurrentEndpointLocked(int& errorCode, std::string& error);
  static std::string deviceIdFor(std::string_view port, std::string_view serial);
  static bool deviceIdMatches(const DmxOutputDevice& device, std::string_view preferredId);
  std::vector<std::string> candidatePorts() const;

  bool writeBytes(const std::uint8_t* data, std::size_t size);
  bool readFrame(std::uint8_t expectedLabel, std::vector<std::uint8_t>& payload, int timeoutMs);

  mutable std::mutex mutex_;
  mutable std::mutex discoveryMutex_;
  DmxDeviceStatus status_;
  std::vector<DmxOutputDevice> devices_;
  std::string preferredDeviceId_;
  int consecutiveWriteFailures_ = 0;
  int writeRetryLimit_ = 10;
  int probeTimeoutMs_ = 350;
  int serialReadTimeoutMs_ = 250;
  bool strictPreferredDevice_ = true;
  std::string lastKnownEndpoint_;
  std::string lastKnownSerial_;
  std::string lastKnownDeviceId_;
  int lastKnownFirmwareMajor_ = 0;
  int lastKnownFirmwareMinor_ = 0;
  std::string lastScanSignature_;

#ifdef _WIN32
  void* handle_ = nullptr;
  bool usingFtdi_ = false;
#else
  int fd_ = -1;
#endif
};

}  // namespace tuxdmx

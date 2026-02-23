#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace tuxdmx {

struct ChannelRange {
  int id = 0;
  int startValue = 0;
  int endValue = 0;
  std::string label;
};

struct TemplateChannel {
  int id = 0;
  int channelIndex = 1;
  std::string name;
  std::string kind;
  int defaultValue = 0;
  std::vector<ChannelRange> ranges;
};

struct FixtureTemplate {
  int id = 0;
  std::string name;
  std::string description;
  int footprintChannels = 0;
  std::vector<TemplateChannel> channels;
};

struct FixtureInstance {
  int id = 0;
  std::string name;
  int templateId = 0;
  std::string templateName;
  int universe = 1;
  int startAddress = 1;
  int channelCount = 0;
  bool enabled = true;
  std::map<int, int> channelValues;
};

struct FixtureGroup {
  int id = 0;
  std::string name;
  std::vector<int> fixtureIds;
};

struct DmxDeviceStatus {
  bool connected = false;
  std::string port;
  std::string serial;
  int firmwareMajor = 0;
  int firmwareMinor = 0;
  std::string lastError;
};

struct AudioMetrics {
  float energy = 0.0F;
  float bass = 0.0F;
  float treble = 0.0F;
  float bpm = 0.0F;
  bool beat = false;
};

struct ChannelPatch {
  int universe = 1;
  int absoluteAddress = 1;
  int value = 0;
};

}  // namespace tuxdmx

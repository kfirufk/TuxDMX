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
  int sortOrder = 0;
  bool enabled = true;
  std::map<int, int> channelValues;
};

struct FixtureGroup {
  int id = 0;
  std::string name;
  std::vector<int> fixtureIds;
};

struct DmxOutputDevice {
  std::string id;
  std::string name;
  std::string endpoint;
  std::string serial;
  int firmwareMajor = 0;
  int firmwareMinor = 0;
  bool connected = false;
};

struct DmxDeviceStatus {
  std::string backend = "unknown";
  bool connected = false;
  std::string endpoint;
  std::string activeDeviceId;
  std::string preferredDeviceId;
  std::string port;
  std::string serial;
  int firmwareMajor = 0;
  int firmwareMinor = 0;
  std::string lastError;
  int writeRetryLimit = 10;
  int consecutiveWriteFailures = 0;
};

struct AudioMetrics {
  float energy = 0.0F;
  float bass = 0.0F;
  float treble = 0.0F;
  float bpm = 0.0F;
  bool beat = false;
};

struct AudioInputDevice {
  int id = -1;
  std::string name;
  bool isDefault = false;
};

struct ChannelPatch {
  int universe = 1;
  int absoluteAddress = 1;
  int value = 0;
};

struct SceneDefinition {
  int id = 0;
  std::string name;
  float transitionSeconds = 1.0F;
  int valueCount = 0;
};

struct ScenePatch {
  int fixtureId = 0;
  int channelIndex = 1;
  int universe = 1;
  int absoluteAddress = 1;
  int value = 0;
};

struct MidiInputPort {
  std::string id;
  std::string name;
};

struct MidiMapping {
  std::string controlId;
  std::string source = "all";  // all | specific
  std::string inputId;         // empty if source == all
  std::string type = "cc";     // cc | note
  int channel = 1;
  int number = 0;
};

struct MidiMessage {
  std::string inputId;
  std::string inputName;
  std::string type = "cc";
  int channel = 1;
  int number = 0;
  int value = 0;
  bool on = false;
  int mappedValue = 0;
};

}  // namespace tuxdmx

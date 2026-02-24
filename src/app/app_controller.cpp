#include "app_controller.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <optional>
#include <set>
#include <sstream>
#include <thread>
#include <unordered_map>
#include <unordered_set>

#include "logger.hpp"
#include "utils.hpp"

namespace tuxdmx {

namespace {

bool getRequiredInt(const std::unordered_map<std::string, std::string>& form, const std::string& key, int& value,
                    std::string& error) {
  auto it = form.find(key);
  if (it == form.end()) {
    error = "Missing field: " + key;
    return false;
  }
  if (!parseInt(it->second, value)) {
    error = "Invalid integer field: " + key;
    return false;
  }
  return true;
}

bool getRequiredFloat(const std::unordered_map<std::string, std::string>& form, const std::string& key, float& value,
                      std::string& error) {
  auto it = form.find(key);
  if (it == form.end()) {
    error = "Missing field: " + key;
    return false;
  }
  try {
    std::size_t idx = 0;
    const float parsed = std::stof(it->second, &idx);
    if (idx != it->second.size()) {
      error = "Invalid float field: " + key;
      return false;
    }
    value = parsed;
  } catch (...) {
    error = "Invalid float field: " + key;
    return false;
  }
  return true;
}

bool parseFloatStrict(std::string_view text, float& out) {
  try {
    std::size_t idx = 0;
    const float value = std::stof(std::string(text), &idx);
    if (idx != text.size()) {
      return false;
    }
    out = value;
    return true;
  } catch (...) {
    return false;
  }
}

bool parseBoolLike(const std::unordered_map<std::string, std::string>& form, const std::string& key) {
  auto it = form.find(key);
  if (it == form.end()) {
    return false;
  }
  const auto lowered = toLower(it->second);
  return lowered == "1" || lowered == "true" || lowered == "on" || lowered == "yes";
}

bool parseBoolText(std::string_view raw, bool& value) {
  const auto lowered = toLower(trim(raw));
  if (lowered == "1" || lowered == "true" || lowered == "on" || lowered == "yes") {
    value = true;
    return true;
  }
  if (lowered == "0" || lowered == "false" || lowered == "off" || lowered == "no") {
    value = false;
    return true;
  }
  return false;
}

bool getRequiredBool(const std::unordered_map<std::string, std::string>& form, const std::string& key, bool& value,
                     std::string& error) {
  auto it = form.find(key);
  if (it == form.end()) {
    error = "Missing field: " + key;
    return false;
  }
  if (!parseBoolText(it->second, value)) {
    error = "Invalid boolean field: " + key;
    return false;
  }
  return true;
}

std::string jsonBool(bool value) { return value ? "true" : "false"; }

int midpoint(const ChannelRange& range) {
  const int lo = clampDmx(range.startValue);
  const int hi = clampDmx(range.endValue);
  return lo + ((hi - lo) / 2);
}

bool containsKeyword(const std::string& text, const std::string& keyword) {
  return toLower(text).find(toLower(keyword)) != std::string::npos;
}

std::optional<ChannelRange> pickRangeByKeywords(const std::vector<ChannelRange>& ranges,
                                                const std::vector<std::string>& keywords) {
  for (const auto& keyword : keywords) {
    for (const auto& range : ranges) {
      if (containsKeyword(range.label, keyword)) {
        return range;
      }
    }
  }
  return std::nullopt;
}

std::string reactiveProfileName(int profile) {
  return profile == 1 ? "volume_blackout" : "balanced";
}

int reactiveProfileFromName(std::string_view raw) {
  const std::string lowered = toLower(raw);
  if (lowered == "volume_blackout" || lowered == "volume-blackout" || lowered == "volume blackout") {
    return 1;
  }
  return 0;
}

int rangeLowValue(const ChannelRange& range) {
  return clampDmx(std::min(range.startValue, range.endValue));
}

int rangeMidValue(const ChannelRange& range) {
  return midpoint(range);
}

int neutralRangeValue(const std::vector<ChannelRange>& ranges) {
  const auto neutral = pickRangeByKeywords(ranges, {"nf", "off", "none", "manual", "static", "stop", "disable"});
  if (neutral.has_value()) {
    return rangeLowValue(*neutral);
  }
  return 0;
}

std::vector<int> parseCsvInts(std::string_view csv) {
  std::vector<int> values;
  std::size_t start = 0;
  while (start <= csv.size()) {
    auto comma = csv.find(',', start);
    if (comma == std::string_view::npos) {
      comma = csv.size();
    }

    auto token = trim(csv.substr(start, comma - start));
    int parsed = 0;
    if (!token.empty() && parseInt(token, parsed)) {
      values.push_back(parsed);
    }

    if (comma == csv.size()) {
      break;
    }
    start = comma + 1;
  }

  return values;
}

std::vector<std::string> splitBy(std::string_view text, char delimiter) {
  std::vector<std::string> parts;
  std::size_t start = 0;
  while (start <= text.size()) {
    const auto pos = text.find(delimiter, start);
    if (pos == std::string_view::npos) {
      const auto token = text.substr(start);
      if (!token.empty()) {
        parts.push_back(std::string(token));
      }
      break;
    }

    const auto token = text.substr(start, pos - start);
    if (!token.empty()) {
      parts.push_back(std::string(token));
    }
    start = pos + 1;
  }
  return parts;
}

struct DmxDirectPatch {
  int universe = 1;
  int address = 1;
  int value = 0;
};

std::vector<DmxDirectPatch> parseDmxPatchTriples(std::string_view csv) {
  std::vector<DmxDirectPatch> patches;

  std::size_t start = 0;
  while (start <= csv.size()) {
    auto comma = csv.find(',', start);
    if (comma == std::string_view::npos) {
      comma = csv.size();
    }

    const auto token = trim(csv.substr(start, comma - start));
    if (!token.empty()) {
      const auto firstColon = token.find(':');
      const auto secondColon = firstColon == std::string_view::npos ? std::string_view::npos : token.find(':', firstColon + 1);

      if (firstColon != std::string_view::npos && secondColon != std::string_view::npos) {
        const auto universeToken = trim(token.substr(0, firstColon));
        const auto addressToken = trim(token.substr(firstColon + 1, secondColon - firstColon - 1));
        const auto valueToken = trim(token.substr(secondColon + 1));

        int universe = 0;
        int address = 0;
        int value = 0;
        if (parseInt(universeToken, universe) && parseInt(addressToken, address) && parseInt(valueToken, value)) {
          DmxDirectPatch patch;
          patch.universe = universe;
          patch.address = address;
          patch.value = value;
          patches.push_back(patch);
        }
      }
    }

    if (comma == csv.size()) {
      break;
    }
    start = comma + 1;
  }

  return patches;
}

std::array<int, 3> hsvToRgb(float hueDeg, float saturation, float value) {
  const float h = std::fmod(std::max(hueDeg, 0.0F), 360.0F) / 60.0F;
  const float c = value * saturation;
  const float x = c * (1.0F - std::fabs(std::fmod(h, 2.0F) - 1.0F));
  const float m = value - c;

  float r = 0.0F;
  float g = 0.0F;
  float b = 0.0F;

  if (h < 1.0F) {
    r = c;
    g = x;
  } else if (h < 2.0F) {
    r = x;
    g = c;
  } else if (h < 3.0F) {
    g = c;
    b = x;
  } else if (h < 4.0F) {
    g = x;
    b = c;
  } else if (h < 5.0F) {
    r = x;
    b = c;
  } else {
    r = c;
    b = x;
  }

  return {
      clampDmx(static_cast<int>((r + m) * 255.0F)),
      clampDmx(static_cast<int>((g + m) * 255.0F)),
      clampDmx(static_cast<int>((b + m) * 255.0F)),
  };
}

void appendTemplateJson(std::ostringstream& ss, const FixtureTemplate& t) {
  ss << '{';
  ss << "\"id\":" << t.id << ',';
  ss << "\"name\":\"" << jsonEscape(t.name) << "\",";
  ss << "\"description\":\"" << jsonEscape(t.description) << "\",";
  ss << "\"footprintChannels\":" << t.footprintChannels << ',';
  ss << "\"channels\":[";

  bool firstChannel = true;
  for (const auto& c : t.channels) {
    if (!firstChannel) {
      ss << ',';
    }
    firstChannel = false;

    ss << '{';
    ss << "\"id\":" << c.id << ',';
    ss << "\"channelIndex\":" << c.channelIndex << ',';
    ss << "\"name\":\"" << jsonEscape(c.name) << "\",";
    ss << "\"kind\":\"" << jsonEscape(c.kind) << "\",";
    ss << "\"defaultValue\":" << c.defaultValue << ',';
    ss << "\"ranges\":[";

    bool firstRange = true;
    for (const auto& r : c.ranges) {
      if (!firstRange) {
        ss << ',';
      }
      firstRange = false;
      ss << '{';
      ss << "\"id\":" << r.id << ',';
      ss << "\"startValue\":" << r.startValue << ',';
      ss << "\"endValue\":" << r.endValue << ',';
      ss << "\"label\":\"" << jsonEscape(r.label) << "\"";
      ss << '}';
    }

    ss << ']';
    ss << '}';
  }

  ss << ']';
  ss << '}';
}

void appendTemplateArrayJson(std::ostringstream& ss, const std::vector<FixtureTemplate>& templates) {
  ss << '[';
  bool first = true;
  for (const auto& t : templates) {
    if (!first) {
      ss << ',';
    }
    first = false;
    appendTemplateJson(ss, t);
  }
  ss << ']';
}

void appendFixtureArrayJson(std::ostringstream& ss, const std::vector<FixtureInstance>& fixtures,
                            const std::unordered_map<int, FixtureTemplate>& templateMap) {
  ss << '[';
  bool firstFixture = true;
  for (const auto& f : fixtures) {
    if (!firstFixture) {
      ss << ',';
    }
    firstFixture = false;

    ss << '{';
    ss << "\"id\":" << f.id << ',';
    ss << "\"name\":\"" << jsonEscape(f.name) << "\",";
    ss << "\"templateId\":" << f.templateId << ',';
    ss << "\"templateName\":\"" << jsonEscape(f.templateName) << "\",";
    ss << "\"universe\":" << f.universe << ',';
    ss << "\"startAddress\":" << f.startAddress << ',';
    ss << "\"channelCount\":" << f.channelCount << ',';
    ss << "\"sortOrder\":" << f.sortOrder << ',';
    ss << "\"enabled\":" << jsonBool(f.enabled) << ',';
    ss << "\"channels\":[";

    auto templateIt = templateMap.find(f.templateId);

    bool firstChannel = true;
    for (int idx = 1; idx <= f.channelCount; ++idx) {
      if (!firstChannel) {
        ss << ',';
      }
      firstChannel = false;

      std::string channelName = "Channel " + std::to_string(idx);
      std::string channelKind = "generic";
      int channelId = 0;
      std::vector<ChannelRange> ranges;

      if (templateIt != templateMap.end()) {
        for (const auto& tc : templateIt->second.channels) {
          if (tc.channelIndex == idx) {
            channelId = tc.id;
            channelName = tc.name;
            channelKind = tc.kind;
            ranges = tc.ranges;
            break;
          }
        }
      }

      int value = 0;
      if (auto valueIt = f.channelValues.find(idx); valueIt != f.channelValues.end()) {
        value = valueIt->second;
      }

      ss << '{';
      ss << "\"channelId\":" << channelId << ',';
      ss << "\"channelIndex\":" << idx << ',';
      ss << "\"name\":\"" << jsonEscape(channelName) << "\",";
      ss << "\"kind\":\"" << jsonEscape(channelKind) << "\",";
      ss << "\"value\":" << value << ',';
      ss << "\"ranges\":[";

      bool firstRange = true;
      for (const auto& range : ranges) {
        if (!firstRange) {
          ss << ',';
        }
        firstRange = false;
        ss << '{';
        ss << "\"startValue\":" << range.startValue << ',';
        ss << "\"endValue\":" << range.endValue << ',';
        ss << "\"label\":\"" << jsonEscape(range.label) << "\"";
        ss << '}';
      }

      ss << ']';
      ss << '}';
    }

    ss << ']';
    ss << '}';
  }

  ss << ']';
}

void appendGroupArrayJson(std::ostringstream& ss, const std::vector<FixtureGroup>& groups) {
  ss << '[';

  bool firstGroup = true;
  for (const auto& group : groups) {
    if (!firstGroup) {
      ss << ',';
    }
    firstGroup = false;

    ss << '{';
    ss << "\"id\":" << group.id << ',';
    ss << "\"name\":\"" << jsonEscape(group.name) << "\",";
    ss << "\"fixtureIds\":[";

    bool firstFixture = true;
    for (int fixtureId : group.fixtureIds) {
      if (!firstFixture) {
        ss << ',';
      }
      firstFixture = false;
      ss << fixtureId;
    }

    ss << ']';
    ss << '}';
  }

  ss << ']';
}

void appendAudioInputDevicesJson(std::ostringstream& ss, const std::vector<AudioInputDevice>& devices) {
  ss << '[';
  bool first = true;
  for (const auto& device : devices) {
    if (!first) {
      ss << ',';
    }
    first = false;

    ss << '{';
    ss << "\"id\":" << device.id << ',';
    ss << "\"name\":\"" << jsonEscape(device.name) << "\",";
    ss << "\"isDefault\":" << jsonBool(device.isDefault);
    ss << '}';
  }
  ss << ']';
}

void appendDmxOutputDevicesJson(std::ostringstream& ss, const std::vector<DmxOutputDevice>& devices) {
  ss << '[';
  bool first = true;
  for (const auto& device : devices) {
    if (!first) {
      ss << ',';
    }
    first = false;
    ss << '{';
    ss << "\"id\":\"" << jsonEscape(device.id) << "\",";
    ss << "\"name\":\"" << jsonEscape(device.name) << "\",";
    ss << "\"endpoint\":\"" << jsonEscape(device.endpoint) << "\",";
    ss << "\"serial\":\"" << jsonEscape(device.serial) << "\",";
    ss << "\"firmwareMajor\":" << device.firmwareMajor << ',';
    ss << "\"firmwareMinor\":" << device.firmwareMinor << ',';
    ss << "\"connected\":" << jsonBool(device.connected);
    ss << '}';
  }
  ss << ']';
}

void appendLogArrayJson(std::ostringstream& ss, const std::vector<LogEntry>& logs) {
  ss << '[';
  bool first = true;
  for (const auto& entry : logs) {
    if (!first) {
      ss << ',';
    }
    first = false;
    ss << '{';
    ss << "\"timestamp\":\"" << jsonEscape(entry.timestamp) << "\",";
    ss << "\"level\":\"" << jsonEscape(entry.level) << "\",";
    ss << "\"scope\":\"" << jsonEscape(entry.scope) << "\",";
    ss << "\"message\":\"" << jsonEscape(entry.message) << "\"";
    ss << '}';
  }
  ss << ']';
}

void appendSceneArrayJson(std::ostringstream& ss, const std::vector<SceneDefinition>& scenes) {
  ss << '[';
  bool first = true;
  for (const auto& scene : scenes) {
    if (!first) {
      ss << ',';
    }
    first = false;
    ss << '{';
    ss << "\"id\":" << scene.id << ',';
    ss << "\"name\":\"" << jsonEscape(scene.name) << "\",";
    ss << "\"transitionSeconds\":" << scene.transitionSeconds << ',';
    ss << "\"valueCount\":" << scene.valueCount;
    ss << '}';
  }
  ss << ']';
}

void appendMidiInputArrayJson(std::ostringstream& ss, const std::vector<MidiInputPort>& inputs) {
  ss << '[';
  bool first = true;
  for (const auto& input : inputs) {
    if (!first) {
      ss << ',';
    }
    first = false;
    ss << '{';
    ss << "\"id\":\"" << jsonEscape(input.id) << "\",";
    ss << "\"name\":\"" << jsonEscape(input.name) << "\"";
    ss << '}';
  }
  ss << ']';
}

void appendMidiMappingArrayJson(std::ostringstream& ss, const std::vector<MidiMapping>& mappings) {
  ss << '[';
  bool first = true;
  for (const auto& mapping : mappings) {
    if (!first) {
      ss << ',';
    }
    first = false;
    ss << '{';
    ss << "\"controlId\":\"" << jsonEscape(mapping.controlId) << "\",";
    ss << "\"source\":\"" << jsonEscape(mapping.source) << "\",";
    ss << "\"inputId\":\"" << jsonEscape(mapping.inputId) << "\",";
    ss << "\"type\":\"" << jsonEscape(mapping.type) << "\",";
    ss << "\"channel\":" << mapping.channel << ',';
    ss << "\"number\":" << mapping.number;
    ss << '}';
  }
  ss << ']';
}

}  // namespace

AppController::AppController(std::string dbPath, std::string webRoot, std::string dmxBackendName)
    : dbPath_(std::move(dbPath)),
      webRoot_(std::move(webRoot)),
      db_(dbPath_),
      dmx_(std::move(dmxBackendName)) {
  std::random_device rd;
  reactiveRng_.seed(rd());
}

AppController::~AppController() { shutdown(); }

bool AppController::initialize(std::string& error) {
  if (!db_.initialize(error)) {
    return false;
  }

  if (!db_.seedAliExpressRgbPar(error)) {
    return false;
  }

  if (!db_.seedMiraDye(error)) {
    return false;
  }

  {
    std::string preferredDeviceId;
    std::string settingError;
    if (db_.getSetting("dmx.preferred_device_id", preferredDeviceId, settingError)) {
      dmx_.setPreferredDeviceId(preferredDeviceId);
      const std::string normalized = trim(preferredDeviceId);
      logMessage(LogLevel::Info, "dmx",
                 normalized.empty() ? "DMX device selection: auto" : ("DMX preferred device: " + normalized));
    } else if (!settingError.empty()) {
      logMessage(LogLevel::Warn, "dmx", "Failed to load preferred DMX device: " + settingError);
    }
  }

  {
    std::string settingValue;
    std::string settingError;

    if (db_.getSetting("dmx.write_retry_limit", settingValue, settingError)) {
      int retries = 0;
      if (parseInt(trim(settingValue), retries)) {
        dmx_.setWriteRetryLimit(retries);
      } else {
        logMessage(LogLevel::Warn, "dmx", "Ignoring invalid dmx.write_retry_limit setting: " + settingValue);
      }
    } else if (!settingError.empty()) {
      logMessage(LogLevel::Warn, "dmx", "Failed to load dmx.write_retry_limit: " + settingError);
    }

    settingValue.clear();
    settingError.clear();
    if (db_.getSetting("dmx.frame_interval_ms", settingValue, settingError)) {
      int value = 0;
      if (parseInt(trim(settingValue), value)) {
        dmx_.setFrameIntervalMs(value);
      } else {
        logMessage(LogLevel::Warn, "dmx", "Ignoring invalid dmx.frame_interval_ms setting: " + settingValue);
      }
    } else if (!settingError.empty()) {
      logMessage(LogLevel::Warn, "dmx", "Failed to load dmx.frame_interval_ms: " + settingError);
    }

    settingValue.clear();
    settingError.clear();
    if (db_.getSetting("dmx.reconnect_base_ms", settingValue, settingError)) {
      int value = 0;
      if (parseInt(trim(settingValue), value)) {
        dmx_.setReconnectBaseMs(value);
      } else {
        logMessage(LogLevel::Warn, "dmx", "Ignoring invalid dmx.reconnect_base_ms setting: " + settingValue);
      }
    } else if (!settingError.empty()) {
      logMessage(LogLevel::Warn, "dmx", "Failed to load dmx.reconnect_base_ms: " + settingError);
    }

    settingValue.clear();
    settingError.clear();
    if (db_.getSetting("dmx.probe_timeout_ms", settingValue, settingError)) {
      int value = 0;
      if (parseInt(trim(settingValue), value)) {
        dmx_.setProbeTimeoutMs(value);
      } else {
        logMessage(LogLevel::Warn, "dmx", "Ignoring invalid dmx.probe_timeout_ms setting: " + settingValue);
      }
    } else if (!settingError.empty()) {
      logMessage(LogLevel::Warn, "dmx", "Failed to load dmx.probe_timeout_ms: " + settingError);
    }

    settingValue.clear();
    settingError.clear();
    if (db_.getSetting("dmx.serial_read_timeout_ms", settingValue, settingError)) {
      int value = 0;
      if (parseInt(trim(settingValue), value)) {
        dmx_.setSerialReadTimeoutMs(value);
      } else {
        logMessage(LogLevel::Warn, "dmx", "Ignoring invalid dmx.serial_read_timeout_ms setting: " + settingValue);
      }
    } else if (!settingError.empty()) {
      logMessage(LogLevel::Warn, "dmx", "Failed to load dmx.serial_read_timeout_ms: " + settingError);
    }

    settingValue.clear();
    settingError.clear();
    if (db_.getSetting("dmx.strict_preferred_device", settingValue, settingError)) {
      bool strict = true;
      if (parseBoolText(settingValue, strict)) {
        dmx_.setStrictPreferredDevice(strict);
      } else {
        logMessage(LogLevel::Warn, "dmx", "Ignoring invalid dmx.strict_preferred_device setting: " + settingValue);
      }
    } else if (!settingError.empty()) {
      logMessage(LogLevel::Warn, "dmx", "Failed to load dmx.strict_preferred_device: " + settingError);
    }
  }

  dmx_.refreshDevices();
  rebuildAllUniversesFromDatabase();

  logMessage(LogLevel::Info, "dmx", "Selected DMX backend: " + dmx_.backendName());
  dmx_.start();

  audio_.setTickCallback([this](const AudioMetrics& metrics) { onAudioMetrics(metrics); });
  audio_.start();

  {
    std::string inputMode;
    std::string settingError;
    if (db_.getSetting("midi.input_mode", inputMode, settingError) && !trim(inputMode).empty()) {
      std::scoped_lock lock(midiMutex_);
      midiInputMode_ = trim(inputMode);
    } else if (!settingError.empty()) {
      logMessage(LogLevel::Warn, "midi", "Failed to load input mode: " + settingError);
    }
  }

  refreshMidiMappingsFromDatabase();
  midi_.setMessageCallback([this](const MidiMessage& message) { onMidiMessage(message); });
  midi_.start();
  logMessage(LogLevel::Info, "midi",
             std::string("Server MIDI backend: ") + midi_.backendName() + (midi_.supported() ? "" : " (disabled)"));

  return true;
}

void AppController::shutdown() {
  sceneTransitionToken_.fetch_add(1);
  midi_.stop();
  audio_.stop();
  persistBlackoutToDatabase();
  dmx_.stop();
}

HttpResponse AppController::jsonError(int status, const std::string& message) {
  if (status >= 500) {
    logMessage(LogLevel::Error, "api", std::to_string(status) + " " + message);
  } else if (status >= 400 && status != 404) {
    logMessage(LogLevel::Warn, "api", std::to_string(status) + " " + message);
  }

  HttpResponse response;
  response.status = status;
  response.contentType = "application/json; charset=utf-8";
  response.body = "{\"ok\":false,\"error\":\"" + jsonEscape(message) + "\"}";
  return response;
}

HttpResponse AppController::jsonOk(const std::string& payload, int status) {
  HttpResponse response;
  response.status = status;
  response.contentType = "application/json; charset=utf-8";
  response.body = payload;
  return response;
}

std::vector<int> AppController::sortedUniverseList(const std::vector<FixtureInstance>& fixtures,
                                                   const std::vector<int>& knownFromEngine) {
  std::set<int> universes;
  for (int universe : knownFromEngine) {
    if (universe >= 1) {
      universes.insert(universe);
    }
  }
  for (const auto& fixture : fixtures) {
    if (fixture.universe >= 1) {
      universes.insert(fixture.universe);
    }
  }
  if (universes.empty()) {
    universes.insert(1);
  }

  return {universes.begin(), universes.end()};
}

std::vector<AppController::FixtureResolvedView> AppController::resolveFixtures(std::string& error) {
  const auto fixtures = db_.listFixtures(error);
  if (!error.empty()) {
    return {};
  }

  const auto templates = db_.listTemplates(error);
  if (!error.empty()) {
    return {};
  }

  std::unordered_map<int, std::vector<TemplateChannel>> templateChannels;
  for (const auto& t : templates) {
    templateChannels[t.id] = t.channels;
  }

  std::vector<FixtureResolvedView> resolved;
  resolved.reserve(fixtures.size());

  for (const auto& fixture : fixtures) {
    FixtureResolvedView view;
    view.fixture = fixture;
    if (auto it = templateChannels.find(fixture.templateId); it != templateChannels.end()) {
      view.templateChannels = it->second;
    }
    resolved.push_back(std::move(view));
  }

  return resolved;
}

std::string AppController::buildStatusJson() {
  const auto dmxStatus = dmx_.status();
  const auto dmxDevices = dmx_.devices();
  const auto metrics = audio_.currentMetrics();
  const float reactiveVolumeThreshold = std::clamp(reactiveVolumeThreshold_.load(), 0.0F, 1.0F);
  const std::string reactiveProfile = reactiveProfileName(reactiveProfile_.load());
  const auto inputDevices = audio_.inputDevices();
  const int defaultInputDeviceId = audio_.defaultInputDeviceId();
  const int selectedInputDeviceId = audio_.selectedInputDeviceId();
  const int activeInputDeviceId = audio_.activeInputDeviceId();
  const auto midiInputs = midi_.inputPorts();
  std::vector<MidiMapping> midiMappings;
  std::string midiInputMode;
  std::string midiLearningControlId;
  {
    std::scoped_lock lock(midiMutex_);
    midiInputMode = midiInputMode_;
    midiLearningControlId = midiLearningControlId_;
    midiMappings.reserve(midiMappings_.size());
    for (const auto& [_, mapping] : midiMappings_) {
      midiMappings.push_back(mapping);
    }
  }
  std::sort(midiMappings.begin(), midiMappings.end(),
            [](const MidiMapping& a, const MidiMapping& b) { return a.controlId < b.controlId; });

  std::ostringstream ss;
  ss << "{\"ok\":true,\"dmx\":{";
  ss << "\"backend\":\"" << jsonEscape(dmxStatus.backend) << "\",";
  ss << "\"connected\":" << jsonBool(dmxStatus.connected) << ',';
  ss << "\"transportState\":\"" << jsonEscape(dmxStatus.transportState) << "\",";
  ss << "\"reconnectAttempt\":" << dmxStatus.reconnectAttempt << ',';
  ss << "\"reconnectBackoffMs\":" << dmxStatus.reconnectBackoffMs << ',';
  ss << "\"reconnectBaseMs\":" << dmxStatus.reconnectBaseMs << ',';
  ss << "\"frameIntervalMs\":" << dmxStatus.frameIntervalMs << ',';
  ss << "\"probeTimeoutMs\":" << dmxStatus.probeTimeoutMs << ',';
  ss << "\"serialReadTimeoutMs\":" << dmxStatus.serialReadTimeoutMs << ',';
  ss << "\"strictPreferredDevice\":" << jsonBool(dmxStatus.strictPreferredDevice) << ',';
  ss << "\"endpoint\":\"" << jsonEscape(dmxStatus.endpoint) << "\",";
  ss << "\"activeDeviceId\":\"" << jsonEscape(dmxStatus.activeDeviceId) << "\",";
  ss << "\"preferredDeviceId\":\"" << jsonEscape(dmxStatus.preferredDeviceId) << "\",";
  ss << "\"selectionMode\":\"" << jsonEscape(dmxStatus.preferredDeviceId.empty() ? "auto" : "manual") << "\",";
  ss << "\"devices\":";
  appendDmxOutputDevicesJson(ss, dmxDevices);
  ss << ',';
  ss << "\"port\":\"" << jsonEscape(dmxStatus.port) << "\",";
  ss << "\"serial\":\"" << jsonEscape(dmxStatus.serial) << "\",";
  ss << "\"firmwareMajor\":" << dmxStatus.firmwareMajor << ',';
  ss << "\"firmwareMinor\":" << dmxStatus.firmwareMinor << ',';
  ss << "\"lastError\":\"" << jsonEscape(dmxStatus.lastError) << "\",";
  ss << "\"lastErrorStage\":\"" << jsonEscape(dmxStatus.lastErrorStage) << "\",";
  ss << "\"lastErrorCode\":" << dmxStatus.lastErrorCode << ',';
  ss << "\"lastErrorEndpoint\":\"" << jsonEscape(dmxStatus.lastErrorEndpoint) << "\",";
  ss << "\"lastErrorUnixMs\":" << dmxStatus.lastErrorUnixMs << ',';
  ss << "\"lastConnectAttemptUnixMs\":" << dmxStatus.lastConnectAttemptUnixMs << ',';
  ss << "\"lastSuccessfulFrameUnixMs\":" << dmxStatus.lastSuccessfulFrameUnixMs << ',';
  ss << "\"writeRetryLimit\":" << dmxStatus.writeRetryLimit << ',';
  ss << "\"consecutiveWriteFailures\":" << dmxStatus.consecutiveWriteFailures << ',';
  ss << "\"outputUniverse\":" << dmx_.outputUniverse() << ',';
  ss << "\"knownUniverses\":[";

  const auto knownUniverses = dmx_.knownUniverses();
  bool firstUniverse = true;
  for (int universe : knownUniverses) {
    if (!firstUniverse) {
      ss << ',';
    }
    firstUniverse = false;
    ss << universe;
  }
  ss << ']';

  ss << "},\"audio\":{";
  ss << "\"reactiveMode\":" << jsonBool(audio_.reactiveMode()) << ',';
  ss << "\"backend\":\"" << jsonEscape(audio_.backendName()) << "\",";
  ss << "\"defaultInputDeviceId\":" << defaultInputDeviceId << ',';
  ss << "\"selectedInputDeviceId\":" << selectedInputDeviceId << ',';
  ss << "\"activeInputDeviceId\":" << activeInputDeviceId << ',';
  ss << "\"inputDevices\":";
  appendAudioInputDevicesJson(ss, inputDevices);
  ss << ',';
  ss << "\"energy\":" << metrics.energy << ',';
  ss << "\"bass\":" << metrics.bass << ',';
  ss << "\"treble\":" << metrics.treble << ',';
  ss << "\"bpm\":" << metrics.bpm << ',';
  ss << "\"beat\":" << jsonBool(metrics.beat) << ',';
  ss << "\"reactiveVolumeThreshold\":" << reactiveVolumeThreshold << ',';
  ss << "\"reactiveProfile\":\"" << jsonEscape(reactiveProfile) << "\"";
  ss << "},\"midi\":{";
  ss << "\"supported\":" << jsonBool(midi_.supported()) << ',';
  ss << "\"backend\":\"" << jsonEscape(midi_.backendName()) << "\",";
  ss << "\"inputMode\":\"" << jsonEscape(midiInputMode) << "\",";
  ss << "\"learningControlId\":\"" << jsonEscape(midiLearningControlId) << "\",";
  ss << "\"inputs\":";
  appendMidiInputArrayJson(ss, midiInputs);
  ss << ",\"mappings\":";
  appendMidiMappingArrayJson(ss, midiMappings);
  ss << "}}";

  return ss.str();
}

std::string AppController::buildStateJson() {
  std::string error;
  const auto templates = db_.listTemplates(error);
  if (!error.empty()) {
    return "{\"ok\":false,\"error\":\"" + jsonEscape(error) + "\"}";
  }

  const auto fixtures = db_.listFixtures(error);
  if (!error.empty()) {
    return "{\"ok\":false,\"error\":\"" + jsonEscape(error) + "\"}";
  }

  const auto groups = db_.listGroups(error);
  if (!error.empty()) {
    return "{\"ok\":false,\"error\":\"" + jsonEscape(error) + "\"}";
  }

  const auto scenes = db_.listScenes(error);
  if (!error.empty()) {
    return "{\"ok\":false,\"error\":\"" + jsonEscape(error) + "\"}";
  }

  std::unordered_map<int, FixtureTemplate> templateMap;
  for (const auto& t : templates) {
    templateMap[t.id] = t;
  }

  const auto dmxStatus = dmx_.status();
  const auto dmxDevices = dmx_.devices();
  const auto metrics = audio_.currentMetrics();
  const float reactiveVolumeThreshold = std::clamp(reactiveVolumeThreshold_.load(), 0.0F, 1.0F);
  const std::string reactiveProfile = reactiveProfileName(reactiveProfile_.load());
  const auto inputDevices = audio_.inputDevices();
  const int defaultInputDeviceId = audio_.defaultInputDeviceId();
  const int selectedInputDeviceId = audio_.selectedInputDeviceId();
  const int activeInputDeviceId = audio_.activeInputDeviceId();
  const auto outputUniverse = dmx_.outputUniverse();
  const auto universes = sortedUniverseList(fixtures, dmx_.knownUniverses());
  const auto midiInputs = midi_.inputPorts();
  std::vector<MidiMapping> midiMappings;
  std::string midiInputMode;
  std::string midiLearningControlId;
  {
    std::scoped_lock lock(midiMutex_);
    midiInputMode = midiInputMode_;
    midiLearningControlId = midiLearningControlId_;
    midiMappings.reserve(midiMappings_.size());
    for (const auto& [_, mapping] : midiMappings_) {
      midiMappings.push_back(mapping);
    }
  }
  std::sort(midiMappings.begin(), midiMappings.end(),
            [](const MidiMapping& a, const MidiMapping& b) { return a.controlId < b.controlId; });

  std::ostringstream ss;
  ss << "{\"ok\":true";

  ss << ",\"dmx\":{";
  ss << "\"backend\":\"" << jsonEscape(dmxStatus.backend) << "\",";
  ss << "\"connected\":" << jsonBool(dmxStatus.connected) << ',';
  ss << "\"transportState\":\"" << jsonEscape(dmxStatus.transportState) << "\",";
  ss << "\"reconnectAttempt\":" << dmxStatus.reconnectAttempt << ',';
  ss << "\"reconnectBackoffMs\":" << dmxStatus.reconnectBackoffMs << ',';
  ss << "\"reconnectBaseMs\":" << dmxStatus.reconnectBaseMs << ',';
  ss << "\"frameIntervalMs\":" << dmxStatus.frameIntervalMs << ',';
  ss << "\"probeTimeoutMs\":" << dmxStatus.probeTimeoutMs << ',';
  ss << "\"serialReadTimeoutMs\":" << dmxStatus.serialReadTimeoutMs << ',';
  ss << "\"strictPreferredDevice\":" << jsonBool(dmxStatus.strictPreferredDevice) << ',';
  ss << "\"endpoint\":\"" << jsonEscape(dmxStatus.endpoint) << "\",";
  ss << "\"activeDeviceId\":\"" << jsonEscape(dmxStatus.activeDeviceId) << "\",";
  ss << "\"preferredDeviceId\":\"" << jsonEscape(dmxStatus.preferredDeviceId) << "\",";
  ss << "\"selectionMode\":\"" << jsonEscape(dmxStatus.preferredDeviceId.empty() ? "auto" : "manual") << "\",";
  ss << "\"devices\":";
  appendDmxOutputDevicesJson(ss, dmxDevices);
  ss << ',';
  ss << "\"port\":\"" << jsonEscape(dmxStatus.port) << "\",";
  ss << "\"serial\":\"" << jsonEscape(dmxStatus.serial) << "\",";
  ss << "\"firmwareMajor\":" << dmxStatus.firmwareMajor << ',';
  ss << "\"firmwareMinor\":" << dmxStatus.firmwareMinor << ',';
  ss << "\"lastError\":\"" << jsonEscape(dmxStatus.lastError) << "\",";
  ss << "\"lastErrorStage\":\"" << jsonEscape(dmxStatus.lastErrorStage) << "\",";
  ss << "\"lastErrorCode\":" << dmxStatus.lastErrorCode << ',';
  ss << "\"lastErrorEndpoint\":\"" << jsonEscape(dmxStatus.lastErrorEndpoint) << "\",";
  ss << "\"lastErrorUnixMs\":" << dmxStatus.lastErrorUnixMs << ',';
  ss << "\"lastConnectAttemptUnixMs\":" << dmxStatus.lastConnectAttemptUnixMs << ',';
  ss << "\"lastSuccessfulFrameUnixMs\":" << dmxStatus.lastSuccessfulFrameUnixMs << ',';
  ss << "\"writeRetryLimit\":" << dmxStatus.writeRetryLimit << ',';
  ss << "\"consecutiveWriteFailures\":" << dmxStatus.consecutiveWriteFailures << ',';
  ss << "\"outputUniverse\":" << outputUniverse << ',';
  ss << "\"knownUniverses\":[";

  bool firstUniverse = true;
  for (int universe : universes) {
    if (!firstUniverse) {
      ss << ',';
    }
    firstUniverse = false;
    ss << universe;
  }
  ss << ']';
  ss << '}';

  ss << ",\"audio\":{";
  ss << "\"reactiveMode\":" << jsonBool(audio_.reactiveMode()) << ',';
  ss << "\"backend\":\"" << jsonEscape(audio_.backendName()) << "\",";
  ss << "\"defaultInputDeviceId\":" << defaultInputDeviceId << ',';
  ss << "\"selectedInputDeviceId\":" << selectedInputDeviceId << ',';
  ss << "\"activeInputDeviceId\":" << activeInputDeviceId << ',';
  ss << "\"inputDevices\":";
  appendAudioInputDevicesJson(ss, inputDevices);
  ss << ',';
  ss << "\"energy\":" << metrics.energy << ',';
  ss << "\"bass\":" << metrics.bass << ',';
  ss << "\"treble\":" << metrics.treble << ',';
  ss << "\"bpm\":" << metrics.bpm << ',';
  ss << "\"beat\":" << jsonBool(metrics.beat) << ',';
  ss << "\"reactiveVolumeThreshold\":" << reactiveVolumeThreshold << ',';
  ss << "\"reactiveProfile\":\"" << jsonEscape(reactiveProfile) << "\"";
  ss << '}';

  ss << ",\"midi\":{";
  ss << "\"supported\":" << jsonBool(midi_.supported()) << ',';
  ss << "\"backend\":\"" << jsonEscape(midi_.backendName()) << "\",";
  ss << "\"inputMode\":\"" << jsonEscape(midiInputMode) << "\",";
  ss << "\"learningControlId\":\"" << jsonEscape(midiLearningControlId) << "\",";
  ss << "\"inputs\":";
  appendMidiInputArrayJson(ss, midiInputs);
  ss << ",\"mappings\":";
  appendMidiMappingArrayJson(ss, midiMappings);
  ss << '}';

  ss << ",\"templates\":";
  appendTemplateArrayJson(ss, templates);

  ss << ",\"fixtures\":";
  appendFixtureArrayJson(ss, fixtures, templateMap);

  ss << ",\"groups\":";
  appendGroupArrayJson(ss, groups);

  ss << ",\"scenes\":";
  appendSceneArrayJson(ss, scenes);

  ss << ",\"logs\":";
  appendLogArrayJson(ss, recentLogs(300));

  ss << '}';
  return ss.str();
}

void AppController::rebuildAllUniversesFromDatabase() {
  std::string error;
  const auto fixtures = db_.listFixtures(error);
  if (!error.empty()) {
    return;
  }

  std::set<int> universes;
  for (const auto& fixture : fixtures) {
    if (fixture.universe >= 1) {
      universes.insert(fixture.universe);
    }
  }
  universes.insert(dmx_.outputUniverse());

  for (int universe : universes) {
    auto patches = db_.loadUniversePatch(universe, error);
    if (!error.empty()) {
      return;
    }

    std::array<std::uint8_t, 512> frame{};
    frame.fill(0);

    for (const auto& patch : patches) {
      if (patch.absoluteAddress >= 1 && patch.absoluteAddress <= 512) {
        frame[static_cast<std::size_t>(patch.absoluteAddress - 1)] = static_cast<std::uint8_t>(clampDmx(patch.value));
      }
    }

    dmx_.replaceUniverse(universe, frame);
  }
}

void AppController::persistBlackoutToDatabase() {
  std::string error;
  const auto fixtures = db_.listFixtures(error);
  if (!error.empty()) {
    return;
  }

  for (const auto& fixture : fixtures) {
    for (int channelIndex = 1; channelIndex <= fixture.channelCount; ++channelIndex) {
      if (auto it = fixture.channelValues.find(channelIndex); it != fixture.channelValues.end() && it->second == 0) {
        continue;
      }

      ChannelPatch patch;
      std::string patchError;
      if (!db_.updateFixtureChannelValue(fixture.id, channelIndex, 0, patch, patchError)) {
        continue;
      }
      dmx_.setChannel(patch.universe, patch.absoluteAddress, 0);
    }
  }
}

void AppController::onAudioMetrics(const AudioMetrics& metrics) {
  if (!audio_.reactiveMode()) {
    return;
  }

  const auto now = std::chrono::steady_clock::now();
  if (!metrics.beat && lastReactiveApply_.time_since_epoch().count() != 0 &&
      now - lastReactiveApply_ < std::chrono::milliseconds(45)) {
    return;
  }
  lastReactiveApply_ = now;

  std::scoped_lock lock(reactiveMutex_);

  std::string error;
  const auto fixtures = resolveFixtures(error);
  if (!error.empty()) {
    return;
  }

  rebuildAllUniversesFromDatabase();

  std::uniform_real_distribution<float> hueJitter(-4.0F, 4.0F);
  const float gate = std::clamp(reactiveVolumeThreshold_.load(), 0.0F, 1.0F);
  const bool volumeBlackoutProfile = reactiveProfile_.load() == 1;
  const bool simulatedAudioSource = toLower(audio_.backendName()).find("simulated-energy") != std::string::npos;

  for (const auto& resolved : fixtures) {
    const auto& fixture = resolved.fixture;
    if (!fixture.enabled || fixture.startAddress < 1 || fixture.startAddress > 512) {
      continue;
    }

    auto& state = reactiveStates_[fixture.id];

    const float rawEnergy = std::clamp(metrics.energy, 0.0F, 1.0F);

    // Volume blackout needs faster release than balanced mode so fixtures stop quickly after a transient.
    if (volumeBlackoutProfile) {
      const float alpha = rawEnergy > state.smoothedEnergy ? 0.32F : 0.55F;
      state.smoothedEnergy = state.smoothedEnergy * (1.0F - alpha) + rawEnergy * alpha;
    } else {
      state.smoothedEnergy = state.smoothedEnergy * 0.82F + rawEnergy * 0.18F;
    }

    // In volume blackout we do not allow beat markers below threshold to wake movement.
    const float beatGate = volumeBlackoutProfile ? std::max(gate, 0.20F) : std::max(0.05F, gate * 0.85F);
    bool strongBeat = metrics.beat && (rawEnergy >= beatGate);
    bool nearSilence = volumeBlackoutProfile ? (rawEnergy < gate) : (!strongBeat && rawEnergy < gate);
    if (volumeBlackoutProfile && simulatedAudioSource) {
      // In this profile, synthetic fallback audio should never drive fixtures.
      nearSilence = true;
    }
    if (nearSilence) {
      state.smoothedEnergy *= volumeBlackoutProfile ? 0.30F : 0.75F;
      if (state.smoothedEnergy < 0.005F) {
        state.smoothedEnergy = 0.0F;
      }
    }

    float activity = gate >= 0.99F
                         ? 0.0F
                         : std::clamp((state.smoothedEnergy - gate) / std::max(0.01F, 1.0F - gate), 0.0F, 1.0F);
    if (volumeBlackoutProfile && activity < 0.08F) {
      // Prevent tiny background fluctuations from waking movement/effects.
      nearSilence = true;
      strongBeat = false;
      activity = 0.0F;
      state.smoothedEnergy *= 0.35F;
      if (state.smoothedEnergy < 0.005F) {
        state.smoothedEnergy = 0.0F;
      }
    }

    float motionActivity = volumeBlackoutProfile ? std::clamp((activity - 0.18F) / 0.82F, 0.0F, 1.0F) : activity;
    if (volumeBlackoutProfile && nearSilence) {
      motionActivity = 0.0F;
    }

    // Hue motion follows meaningful audio activity. When quiet, it barely changes.
    float hueStep = volumeBlackoutProfile && nearSilence ? 0.0F : (0.06F + (activity * 2.4F));
    if (strongBeat) {
      hueStep += 0.9F;
    }
    if (metrics.bpm > 20.0F && activity > 0.05F) {
      hueStep += metrics.bpm / 140.0F;
    }
    state.hue += hueStep;
    const float jitterScale = volumeBlackoutProfile && nearSilence ? 0.0F : (0.12F + (activity * 0.88F));
    state.hue += hueJitter(reactiveRng_) * jitterScale;
    if (state.hue < 0.0F) {
      state.hue += 360.0F;
    }
    if (state.hue >= 360.0F) {
      state.hue -= 360.0F;
    }

    const float colorFloor = volumeBlackoutProfile ? 0.0F : 0.26F;
    const float colorValue = volumeBlackoutProfile
                                 ? (nearSilence ? 0.0F : std::clamp(activity * 1.05F, 0.0F, 1.0F))
                                 : std::clamp(colorFloor + state.smoothedEnergy * 0.74F, 0.0F, 1.0F);
    const auto rgb = hsvToRgb(state.hue + (strongBeat ? 12.0F : 0.0F), 0.9F, colorValue);

    std::vector<ChannelPatch> patches;

    for (const auto& channel : resolved.templateChannels) {
      if (channel.channelIndex < 1 || channel.channelIndex > fixture.channelCount) {
        continue;
      }

      const auto kind = toLower(channel.kind);
      int nextValue = -1;

      const float phase = state.hue * 0.0174532925F;

      if (kind == "dimmer") {
        // Master dimmer combines overall loudness and bass accents.
        if (volumeBlackoutProfile) {
          if (nearSilence || activity < 0.02F) {
            nextValue = 0;
          } else {
            const float dimmer = (activity * 215.0F) + (strongBeat ? 20.0F : 0.0F);
            nextValue = clampDmx(static_cast<int>(std::round(dimmer)));
          }
        } else {
          const float dimmer = 30.0F + (state.smoothedEnergy * 170.0F) + (metrics.bass * 46.0F) + (strongBeat ? 18.0F : 0.0F);
          nextValue = clampDmx(static_cast<int>(std::round(dimmer)));
        }
      } else if (kind == "pan") {
        // Pan stays parked at center when audio activity is very low.
        if (motionActivity < 0.03F || (volumeBlackoutProfile && nearSilence)) {
          nextValue = 128;
        } else {
          const float amplitude = 16.0F + (motionActivity * 106.0F);
          const float pan = 128.0F + std::sin(phase * 1.13F) * amplitude;
          nextValue = clampDmx(static_cast<int>(std::round(pan)));
        }
      } else if (kind == "tilt") {
        // Tilt follows pan behavior and remains still in near-silence.
        if (motionActivity < 0.03F || (volumeBlackoutProfile && nearSilence)) {
          nextValue = 128;
        } else {
          const float amplitude = 9.0F + (motionActivity * 66.0F);
          const float tilt = 128.0F + std::cos((phase * 0.93F) + 0.8F) * amplitude;
          nextValue = clampDmx(static_cast<int>(std::round(tilt)));
        }
      } else if (kind == "pan_speed") {
        // This channel is documented as 0=fast, 255=slow, so we invert energy.
        // High music energy => lower DMX value => faster movement.
        float speedValue = 255.0F;
        if (volumeBlackoutProfile && nearSilence) {
          // In blackout profile, settle to center quickly when audio drops.
          speedValue = 0.0F;
        } else if (!(nearSilence || (volumeBlackoutProfile && motionActivity < 0.03F))) {
          speedValue = 240.0F - (motionActivity * 200.0F) - (strongBeat ? 16.0F : 0.0F);
        }
        if (volumeBlackoutProfile && nearSilence) {
          speedValue = std::clamp(speedValue, 0.0F, 255.0F);
        } else {
          speedValue = std::clamp(speedValue, 15.0F, 255.0F);
        }
        nextValue = static_cast<int>(std::round(speedValue));
      } else if (kind == "red") {
        nextValue = volumeBlackoutProfile && nearSilence ? 0 : rgb[0];
      } else if (kind == "green") {
        nextValue = volumeBlackoutProfile && nearSilence ? 0 : rgb[1];
      } else if (kind == "blue") {
        nextValue = volumeBlackoutProfile && nearSilence ? 0 : rgb[2];
      } else if (kind == "white") {
        // White is used as a sparkle accent on transients/treble.
        if (volumeBlackoutProfile) {
          if (nearSilence || activity < 0.05F) {
            nextValue = 0;
          } else {
            const float white = (activity * 120.0F) + (strongBeat ? 70.0F : 0.0F);
            nextValue = clampDmx(static_cast<int>(std::round(white)));
          }
        } else {
          const float white = 8.0F + (metrics.treble * 130.0F) + (strongBeat ? 70.0F : 0.0F);
          nextValue = clampDmx(static_cast<int>(std::round(white)));
        }
      } else if (kind == "speed") {
        // Effect speed scales with measured BPM and overall energy.
        if (volumeBlackoutProfile && nearSilence) {
          nextValue = 0;
        } else {
          const float speed = 14.0F + (metrics.bpm * 0.55F) + (state.smoothedEnergy * 86.0F);
          nextValue = clampDmx(static_cast<int>(std::round(speed)));
        }
      } else if (kind == "strobe") {
        std::optional<ChannelRange> targetRange;
        if (!nearSilence && (strongBeat || metrics.energy > std::max(0.72F, gate + 0.40F))) {
          targetRange = pickRangeByKeywords(channel.ranges, {"strobe", "flash", "flicker"});
        } else {
          targetRange = pickRangeByKeywords(channel.ranges, {"no", "off", "manual", "static"});
        }

        if (targetRange.has_value()) {
          nextValue = nearSilence ? rangeLowValue(*targetRange) : rangeMidValue(*targetRange);
        } else {
          nextValue = strongBeat ? 160 : 0;
        }
      } else if (kind == "mode" || kind == "strip_effect" || kind == "effect") {
        if (nearSilence) {
          nextValue = neutralRangeValue(channel.ranges);
          continue;
        }

        std::vector<std::string> desired;

        if (kind == "strip_effect" || kind == "effect") {
          if (state.smoothedEnergy < 0.22F) {
            desired = {"nf", "off", "none"};
          } else if (metrics.beat || state.smoothedEnergy > 0.62F) {
            desired = {"self-drive", "self drive", "auto", "effect"};
          } else {
            desired = {"fixed", "color"};
          }
        } else {
          // Mode selection priorities use the labels you define in each range.
          // Tweak keyword sets below to map your own fixture vocabulary.
          if (metrics.bass > std::max(0.75F, gate + 0.35F)) {
            desired = {"voice", "sound", "music"};
          } else if (strongBeat && metrics.energy > std::max(0.65F, gate + 0.28F)) {
            desired = {"jump", "flash"};
          } else if (metrics.energy > std::max(0.50F, gate + 0.20F)) {
            desired = {"pulse", "variable"};
          } else if (metrics.energy > std::max(0.30F, gate + 0.10F)) {
            desired = {"gradient", "fade"};
          } else {
            desired = {"manual", "static"};
          }
        }

        auto range = pickRangeByKeywords(channel.ranges, desired);

        if (!range.has_value() && !channel.ranges.empty()) {
          if (strongBeat) {
            state.modeCursor = (state.modeCursor + 1) % channel.ranges.size();
          }
          range = channel.ranges[state.modeCursor % channel.ranges.size()];
        }

        if (range.has_value()) {
          nextValue = rangeMidValue(*range);
        } else {
          nextValue = 0;
        }
      } else if (kind == "effect_mode") {
        if (nearSilence) {
          nextValue = neutralRangeValue(channel.ranges);
          continue;
        }

        std::vector<std::string> desired;

        if (metrics.treble > std::max(0.68F, gate + 0.28F)) {
          desired = {"chromatic", "aberration", "prism"};
        } else if (state.smoothedEnergy > std::max(0.44F, gate + 0.16F)) {
          desired = {"gradient", "fade", "tint"};
        } else if (state.smoothedEnergy > std::max(0.20F, gate + 0.06F)) {
          desired = {"shift", "color"};
        } else {
          desired = {"nf", "off", "none"};
        }

        auto range = pickRangeByKeywords(channel.ranges, desired);
        if (!range.has_value() && !channel.ranges.empty()) {
          range = channel.ranges[static_cast<std::size_t>(state.modeCursor % channel.ranges.size())];
        }

        if (range.has_value()) {
          nextValue = rangeMidValue(*range);
        } else {
          nextValue = 0;
        }
      } else if (kind == "macro") {
        if (nearSilence) {
          nextValue = neutralRangeValue(channel.ranges);
          continue;
        }

        std::vector<std::string> desired;

        // Macro logic intentionally avoids reset ranges during autoplay.
        if (strongBeat && metrics.bass > std::max(0.72F, gate + 0.32F)) {
          desired = {"voice", "sound", "audio"};
        } else if (state.smoothedEnergy > std::max(0.38F, gate + 0.14F)) {
          desired = {"random", "walk"};
        } else {
          desired = {"nf", "off", "none", "manual"};
        }

        auto range = pickRangeByKeywords(channel.ranges, desired);

        if (!range.has_value() && !channel.ranges.empty()) {
          for (const auto& candidate : channel.ranges) {
            if (!containsKeyword(candidate.label, "reset")) {
              range = candidate;
              break;
            }
          }
        }

        if (range.has_value()) {
          nextValue = rangeMidValue(*range);
        } else {
          nextValue = 0;
        }
      }

      if (nextValue < 0) {
        continue;
      }

      ChannelPatch patch;
      if (!db_.updateFixtureChannelValue(fixture.id, channel.channelIndex, nextValue, patch, error)) {
        continue;
      }
      patches.push_back(patch);
    }

    for (const auto& patch : patches) {
      dmx_.setChannel(patch.universe, patch.absoluteAddress, patch.value);
    }
  }
}

void AppController::refreshMidiMappingsFromDatabase() {
  std::string error;
  const auto mappings = db_.listMidiMappings(error);
  if (!error.empty()) {
    logMessage(LogLevel::Warn, "midi", "Failed to load mappings: " + error);
    return;
  }

  std::scoped_lock lock(midiMutex_);
  midiMappings_.clear();
  midiLastAppliedValues_.clear();
  for (const auto& mapping : mappings) {
    midiMappings_[mapping.controlId] = mapping;
  }
}

bool AppController::applyMidiControl(const std::string& controlId, int value, bool on, std::string& error) {
  const auto parts = splitBy(controlId, ':');
  if (parts.empty()) {
    error = "Invalid control id";
    return false;
  }

  if (parts.size() == 4 && parts[0] == "fixture" && parts[2] == "ch") {
    int fixtureId = 0;
    int channelIndex = 0;
    if (!parseInt(parts[1], fixtureId) || !parseInt(parts[3], channelIndex)) {
      error = "Invalid fixture control id";
      return false;
    }

    ChannelPatch patch;
    if (!db_.updateFixtureChannelValue(fixtureId, channelIndex, clampDmx(value), patch, error)) {
      return false;
    }
    dmx_.setChannel(patch.universe, patch.absoluteAddress, patch.value);
    return true;
  }

  if (parts.size() == 4 && parts[0] == "group" && parts[2] == "kind") {
    int groupId = 0;
    if (!parseInt(parts[1], groupId)) {
      error = "Invalid group control id";
      return false;
    }

    const std::string kind = toLower(parts[3]);
    std::string dbError;
    const auto groups = db_.listGroups(dbError);
    if (!dbError.empty()) {
      error = dbError;
      return false;
    }

    auto it = std::find_if(groups.begin(), groups.end(), [groupId](const FixtureGroup& g) { return g.id == groupId; });
    if (it == groups.end()) {
      error = "Group not found";
      return false;
    }

    const auto resolved = resolveFixtures(dbError);
    if (!dbError.empty()) {
      error = dbError;
      return false;
    }

    std::unordered_set<int> members(it->fixtureIds.begin(), it->fixtureIds.end());
    for (const auto& fixtureView : resolved) {
      if (!members.contains(fixtureView.fixture.id)) {
        continue;
      }
      for (const auto& channel : fixtureView.templateChannels) {
        if (toLower(channel.kind) != kind) {
          continue;
        }

        ChannelPatch patch;
        if (!db_.updateFixtureChannelValue(fixtureView.fixture.id, channel.channelIndex, clampDmx(value), patch, dbError)) {
          continue;
        }
        dmx_.setChannel(patch.universe, patch.absoluteAddress, patch.value);
      }
    }
    return true;
  }

  if (controlId == "audio:reactive") {
    const bool enabled = on || value > 0;
    audio_.setReactiveMode(enabled);
    if (!enabled) {
      rebuildAllUniversesFromDatabase();
    }
    return true;
  }

  if (parts.size() == 3 && parts[0] == "scene" && parts[2] == "recall") {
    int sceneId = 0;
    if (!parseInt(parts[1], sceneId)) {
      error = "Invalid scene control id";
      return false;
    }
    if (!(on || value > 0)) {
      return true;
    }
    return startSceneTransition(sceneId, -1.0F, error);
  }

  error = "Unsupported MIDI control: " + controlId;
  return false;
}

void AppController::onMidiMessage(const MidiMessage& message) {
  std::vector<MidiMapping> candidates;
  std::string learningControlId;
  std::string inputMode;
  {
    std::scoped_lock lock(midiMutex_);
    inputMode = midiInputMode_;
    if (inputMode != "all" && inputMode != message.inputId) {
      return;
    }

    learningControlId = midiLearningControlId_;
    if (learningControlId.empty()) {
      candidates.reserve(midiMappings_.size());
      for (const auto& [_, mapping] : midiMappings_) {
        candidates.push_back(mapping);
      }
    }
  }

  if (!learningControlId.empty()) {
    MidiMapping mapping;
    mapping.controlId = learningControlId;
    mapping.source = inputMode == "all" ? "all" : "specific";
    mapping.inputId = mapping.source == "specific" ? inputMode : "";
    mapping.type = message.type == "note" ? "note" : "cc";
    mapping.channel = std::clamp(message.channel, 1, 16);
    mapping.number = std::clamp(message.number, 0, 127);

    std::string error;
    if (!db_.upsertMidiMapping(mapping, error)) {
      logMessage(LogLevel::Warn, "midi", "Failed to save mapping: " + error);
      return;
    }

    {
      std::scoped_lock lock(midiMutex_);
      midiMappings_[mapping.controlId] = mapping;
      midiLearningControlId_.clear();
      midiLastAppliedValues_.erase(mapping.controlId);
    }

    logMessage(LogLevel::Info, "midi",
               "Mapped " + mapping.controlId + " <- " + mapping.type + " ch" + std::to_string(mapping.channel)
                   + " #" + std::to_string(mapping.number));
    return;
  }

  for (const auto& mapping : candidates) {
    if (mapping.type != message.type) {
      continue;
    }
    if (mapping.channel != message.channel || mapping.number != message.number) {
      continue;
    }
    if (mapping.source == "specific" && mapping.inputId != message.inputId) {
      continue;
    }

    const bool isToggle = mapping.controlId == "audio:reactive"
                          || (mapping.controlId.rfind("scene:", 0) == 0 && mapping.controlId.find(":recall") != std::string::npos);
    const int nextValue = isToggle ? (message.on ? 1 : 0) : clampDmx(message.mappedValue);

    bool shouldApply = true;
    {
      std::scoped_lock lock(midiMutex_);
      const auto it = midiLastAppliedValues_.find(mapping.controlId);
      if (it != midiLastAppliedValues_.end() && it->second == nextValue) {
        shouldApply = false;
      } else {
        midiLastAppliedValues_[mapping.controlId] = nextValue;
      }
    }

    if (!shouldApply) {
      continue;
    }

    std::string error;
    if (!applyMidiControl(mapping.controlId, nextValue, message.on, error) && !error.empty()) {
      if (!error.starts_with("Unsupported MIDI control")) {
        logMessage(LogLevel::Warn, "midi", error);
      }
    }
  }
}

bool AppController::startSceneTransition(int sceneId, float transitionSeconds, std::string& error) {
  float durationSeconds = transitionSeconds;
  auto scenes = db_.listScenes(error);
  if (!error.empty()) {
    return false;
  }

  auto sceneIt = std::find_if(scenes.begin(), scenes.end(), [sceneId](const SceneDefinition& s) { return s.id == sceneId; });
  if (sceneIt == scenes.end()) {
    error = "Scene not found";
    return false;
  }

  if (durationSeconds < 0.0F) {
    durationSeconds = sceneIt->transitionSeconds;
  }
  durationSeconds = std::clamp(durationSeconds, 0.0F, 60.0F);

  auto patches = db_.loadScenePatches(sceneId, error);
  if (!error.empty()) {
    return false;
  }
  if (patches.empty()) {
    error = "Scene has no active fixture values";
    return false;
  }

  const auto fixtures = db_.listFixtures(error);
  if (!error.empty()) {
    return false;
  }

  struct MorphPatch {
    ScenePatch patch;
    int startValue = 0;
    int targetValue = 0;
  };

  std::unordered_map<std::string, int> currentValues;
  for (const auto& fixture : fixtures) {
    for (const auto& [channelIndex, value] : fixture.channelValues) {
      currentValues[std::to_string(fixture.id) + ":" + std::to_string(channelIndex)] = clampDmx(value);
    }
  }

  std::vector<MorphPatch> morphPatches;
  morphPatches.reserve(patches.size());
  for (const auto& patch : patches) {
    MorphPatch morph;
    morph.patch = patch;
    morph.targetValue = clampDmx(patch.value);
    const auto key = std::to_string(patch.fixtureId) + ":" + std::to_string(patch.channelIndex);
    if (const auto it = currentValues.find(key); it != currentValues.end()) {
      morph.startValue = clampDmx(it->second);
    } else {
      morph.startValue = 0;
    }
    morphPatches.push_back(std::move(morph));
  }

  const std::uint64_t token = sceneTransitionToken_.fetch_add(1) + 1;

  if (durationSeconds <= 0.001F) {
    for (const auto& morph : morphPatches) {
      dmx_.setChannel(morph.patch.universe, morph.patch.absoluteAddress, morph.targetValue);
    }

    for (const auto& morph : morphPatches) {
      ChannelPatch patch;
      std::string persistError;
      db_.updateFixtureChannelValue(morph.patch.fixtureId, morph.patch.channelIndex, morph.targetValue, patch, persistError);
    }
    return true;
  }

  std::thread([this, token, durationSeconds, morphPatches = std::move(morphPatches)]() mutable {
    const auto start = std::chrono::steady_clock::now();

    while (true) {
      if (sceneTransitionToken_.load() != token) {
        return;
      }

      const auto now = std::chrono::steady_clock::now();
      const float elapsedSeconds =
          std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count() / 1000.0F;
      const float t = std::clamp(elapsedSeconds / durationSeconds, 0.0F, 1.0F);

      for (const auto& morph : morphPatches) {
        const float blended = static_cast<float>(morph.startValue)
                              + static_cast<float>(morph.targetValue - morph.startValue) * t;
        dmx_.setChannel(morph.patch.universe, morph.patch.absoluteAddress, clampDmx(static_cast<int>(std::lround(blended))));
      }

      if (t >= 1.0F) {
        break;
      }

      std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }

    for (const auto& morph : morphPatches) {
      ChannelPatch patch;
      std::string persistError;
      if (!db_.updateFixtureChannelValue(morph.patch.fixtureId, morph.patch.channelIndex, morph.targetValue, patch,
                                         persistError)) {
        continue;
      }
      dmx_.setChannel(patch.universe, patch.absoluteAddress, patch.value);
    }
  }).detach();

  return true;
}

HttpResponse AppController::serveStatic(const std::string& rawPath) {
  std::string path = rawPath;
  if (path.empty() || path == "/") {
    path = "/index.html";
  }

  if (!path.empty() && path.front() == '/') {
    path.erase(path.begin());
  }

  const auto relative = std::filesystem::path(path).lexically_normal();
  if (relative.empty() || relative.string().find("..") != std::string::npos) {
    return jsonError(404, "File not found");
  }

  auto filePath = webRoot_ / relative;
  if (!std::filesystem::exists(filePath)) {
    if (relative.extension().empty()) {
      filePath = webRoot_ / "index.html";
    } else {
      return jsonError(404, "File not found");
    }
  }

  std::ifstream file(filePath, std::ios::binary);
  if (!file) {
    return jsonError(404, "Unable to open static file");
  }

  std::ostringstream buffer;
  buffer << file.rdbuf();

  HttpResponse response;
  response.status = 200;
  response.contentType = guessMimeType(filePath);
  response.body = buffer.str();
  return response;
}

HttpResponse AppController::handleApi(const HttpRequest& request) {
  const auto segments = splitPath(request.path);

  if (request.method == "GET" && request.path == "/api/status") {
    return jsonOk(buildStatusJson());
  }

  if (request.method == "GET" && request.path == "/api/state") {
    return jsonOk(buildStateJson());
  }

  if (request.method == "GET" && request.path == "/api/midi") {
    const auto midiInputs = midi_.inputPorts();
    std::vector<MidiMapping> midiMappings;
    std::string midiInputMode;
    std::string midiLearningControlId;
    {
      std::scoped_lock lock(midiMutex_);
      midiInputMode = midiInputMode_;
      midiLearningControlId = midiLearningControlId_;
      midiMappings.reserve(midiMappings_.size());
      for (const auto& [_, mapping] : midiMappings_) {
        midiMappings.push_back(mapping);
      }
    }
    std::sort(midiMappings.begin(), midiMappings.end(),
              [](const MidiMapping& a, const MidiMapping& b) { return a.controlId < b.controlId; });

    std::ostringstream ss;
    ss << "{\"ok\":true,\"midi\":{";
    ss << "\"supported\":" << jsonBool(midi_.supported()) << ',';
    ss << "\"backend\":\"" << jsonEscape(midi_.backendName()) << "\",";
    ss << "\"inputMode\":\"" << jsonEscape(midiInputMode) << "\",";
    ss << "\"learningControlId\":\"" << jsonEscape(midiLearningControlId) << "\",";
    ss << "\"inputs\":";
    appendMidiInputArrayJson(ss, midiInputs);
    ss << ",\"mappings\":";
    appendMidiMappingArrayJson(ss, midiMappings);
    ss << "}}";
    return jsonOk(ss.str());
  }

  if (request.method == "GET" && request.path == "/api/logs") {
    std::ostringstream ss;
    ss << "{\"ok\":true,\"logs\":";
    appendLogArrayJson(ss, recentLogs(500));
    ss << '}';
    return jsonOk(ss.str());
  }

  if (request.method == "POST" && request.path == "/api/logs/clear") {
    clearRecentLogs();
    logMessage(LogLevel::Info, "server", "Debug log cleared from UI");
    return jsonOk("{\"ok\":true}");
  }

  if (request.method == "GET" && request.path == "/api/templates") {
    std::string error;
    const auto templates = db_.listTemplates(error);
    if (!error.empty()) {
      return jsonError(500, error);
    }

    std::ostringstream ss;
    ss << "{\"ok\":true,\"templates\":";
    appendTemplateArrayJson(ss, templates);
    ss << '}';
    return jsonOk(ss.str());
  }

  if (request.method == "GET" && request.path == "/api/templates/export") {
    std::string error;
    const auto templates = db_.listTemplates(error);
    if (!error.empty()) {
      return jsonError(500, error);
    }

    std::ostringstream ss;
    ss << "{\"ok\":true,\"version\":1,\"templates\":";
    appendTemplateArrayJson(ss, templates);
    ss << '}';
    return jsonOk(ss.str());
  }

  if (request.method == "GET" && request.path == "/api/fixtures") {
    std::string error;
    const auto fixtures = db_.listFixtures(error);
    if (!error.empty()) {
      return jsonError(500, error);
    }

    const auto templates = db_.listTemplates(error);
    if (!error.empty()) {
      return jsonError(500, error);
    }

    std::unordered_map<int, FixtureTemplate> templateMap;
    for (const auto& t : templates) {
      templateMap[t.id] = t;
    }

    std::ostringstream ss;
    ss << "{\"ok\":true,\"fixtures\":";
    appendFixtureArrayJson(ss, fixtures, templateMap);
    ss << '}';
    return jsonOk(ss.str());
  }

  if (request.method == "GET" && request.path == "/api/groups") {
    std::string error;
    const auto groups = db_.listGroups(error);
    if (!error.empty()) {
      return jsonError(500, error);
    }

    std::ostringstream ss;
    ss << "{\"ok\":true,\"groups\":";
    appendGroupArrayJson(ss, groups);
    ss << '}';
    return jsonOk(ss.str());
  }

  if (request.method == "GET" && request.path == "/api/scenes") {
    std::string error;
    const auto scenes = db_.listScenes(error);
    if (!error.empty()) {
      return jsonError(500, error);
    }

    std::ostringstream ss;
    ss << "{\"ok\":true,\"scenes\":";
    appendSceneArrayJson(ss, scenes);
    ss << '}';
    return jsonOk(ss.str());
  }

  if (request.method == "POST" && request.path == "/api/templates") {
    auto form = parseFormEncoded(request.body);

    auto nameIt = form.find("name");
    if (nameIt == form.end() || trim(nameIt->second).empty()) {
      return jsonError(422, "Template name is required");
    }

    const auto descriptionIt = form.find("description");
    const auto description = descriptionIt == form.end() ? "" : descriptionIt->second;

    std::string error;
    const int templateId = db_.createTemplate(trim(nameIt->second), description, error);
    if (templateId <= 0) {
      if (error.find("UNIQUE") != std::string::npos) {
        return jsonError(409, "Template name already exists");
      }
      return jsonError(500, error);
    }

    std::ostringstream ss;
    ss << "{\"ok\":true,\"templateId\":" << templateId << '}';
    return jsonOk(ss.str(), 201);
  }

  if (request.method == "POST" && segments.size() == 4 && segments[0] == "api" && segments[1] == "templates" &&
      segments[3] == "replace") {
    int templateId = 0;
    if (!parseInt(segments[2], templateId)) {
      return jsonError(400, "Invalid template id");
    }

    auto form = parseFormEncoded(request.body);
    auto nameIt = form.find("name");
    if (nameIt == form.end() || trim(nameIt->second).empty()) {
      return jsonError(422, "Template name is required");
    }
    const auto descriptionIt = form.find("description");
    const std::string description = descriptionIt == form.end() ? "" : trim(descriptionIt->second);

    std::string error;
    if (!db_.resetTemplateDefinition(templateId, trim(nameIt->second), description, error)) {
      if (error.find("UNIQUE") != std::string::npos) {
        return jsonError(409, "Template name already exists");
      }
      return jsonError(422, error);
    }

    return jsonOk("{\"ok\":true}");
  }

  if (request.method == "POST" && segments.size() == 4 && segments[0] == "api" && segments[1] == "templates" &&
      segments[3] == "channels") {
    int templateId = 0;
    if (!parseInt(segments[2], templateId)) {
      return jsonError(400, "Invalid template id");
    }

    auto form = parseFormEncoded(request.body);
    std::string error;
    int channelIndex = 0;
    if (!getRequiredInt(form, "channel_index", channelIndex, error)) {
      return jsonError(422, error);
    }

    auto nameIt = form.find("name");
    if (nameIt == form.end() || trim(nameIt->second).empty()) {
      return jsonError(422, "Channel name is required");
    }

    auto kindIt = form.find("kind");
    std::string kind = kindIt == form.end() ? "generic" : trim(kindIt->second);
    if (kind.empty()) {
      kind = "generic";
    }

    int defaultValue = 0;
    if (auto dv = form.find("default_value"); dv != form.end() && !dv->second.empty()) {
      if (!parseInt(dv->second, defaultValue)) {
        return jsonError(422, "default_value must be an integer");
      }
    }

    const int channelId = db_.addTemplateChannel(templateId, channelIndex, trim(nameIt->second), kind, defaultValue, error);
    if (channelId <= 0) {
      if (error.find("UNIQUE") != std::string::npos) {
        return jsonError(409, "Channel index already exists for this template");
      }
      return jsonError(500, error);
    }

    std::ostringstream ss;
    ss << "{\"ok\":true,\"channelId\":" << channelId << '}';
    return jsonOk(ss.str(), 201);
  }

  if (request.method == "POST" && segments.size() == 4 && segments[0] == "api" && segments[1] == "channels" &&
      segments[3] == "ranges") {
    int channelId = 0;
    if (!parseInt(segments[2], channelId)) {
      return jsonError(400, "Invalid channel id");
    }

    auto form = parseFormEncoded(request.body);
    std::string error;
    int startValue = 0;
    int endValue = 0;

    if (!getRequiredInt(form, "start_value", startValue, error) || !getRequiredInt(form, "end_value", endValue, error)) {
      return jsonError(422, error);
    }

    auto labelIt = form.find("label");
    if (labelIt == form.end() || trim(labelIt->second).empty()) {
      return jsonError(422, "Range label is required");
    }

    if (endValue < startValue) {
      std::swap(startValue, endValue);
    }

    const int rangeId = db_.addChannelRange(channelId, startValue, endValue, trim(labelIt->second), error);
    if (rangeId <= 0) {
      return jsonError(500, error);
    }

    std::ostringstream ss;
    ss << "{\"ok\":true,\"rangeId\":" << rangeId << '}';
    return jsonOk(ss.str(), 201);
  }

  if (request.method == "POST" && segments.size() == 4 && segments[0] == "api" && segments[1] == "channels" &&
      segments[3] == "update") {
    int channelId = 0;
    if (!parseInt(segments[2], channelId)) {
      return jsonError(400, "Invalid channel id");
    }

    auto form = parseFormEncoded(request.body);
    auto nameIt = form.find("name");
    if (nameIt == form.end() || trim(nameIt->second).empty()) {
      return jsonError(422, "Channel name is required");
    }

    auto kindIt = form.find("kind");
    std::string kind = kindIt == form.end() ? "generic" : trim(kindIt->second);
    if (kind.empty()) {
      kind = "generic";
    }

    int defaultValue = 0;
    std::string error;
    if (auto defaultIt = form.find("default_value"); defaultIt != form.end() && !defaultIt->second.empty()) {
      if (!parseInt(defaultIt->second, defaultValue)) {
        return jsonError(422, "default_value must be an integer");
      }
    }

    if (!db_.updateTemplateChannel(channelId, trim(nameIt->second), kind, defaultValue, error)) {
      return jsonError(422, error);
    }

    return jsonOk("{\"ok\":true}");
  }

  if (request.method == "POST" && segments.size() == 5 && segments[0] == "api" && segments[1] == "channels" &&
      segments[3] == "ranges" && segments[4] == "clear") {
    int channelId = 0;
    if (!parseInt(segments[2], channelId)) {
      return jsonError(400, "Invalid channel id");
    }

    std::string error;
    if (!db_.clearChannelRanges(channelId, error)) {
      return jsonError(422, error);
    }

    return jsonOk("{\"ok\":true}");
  }

  if (request.method == "POST" && request.path == "/api/fixtures") {
    auto form = parseFormEncoded(request.body);

    auto nameIt = form.find("name");
    if (nameIt == form.end() || trim(nameIt->second).empty()) {
      return jsonError(422, "Fixture name is required");
    }

    int templateId = 0;
    int universe = 1;
    int startAddress = 0;
    int channelCount = 0;
    std::string error;

    if (!getRequiredInt(form, "template_id", templateId, error) || !getRequiredInt(form, "start_address", startAddress, error)) {
      return jsonError(422, error);
    }

    if (auto universeIt = form.find("universe"); universeIt != form.end() && !universeIt->second.empty()) {
      if (!parseInt(universeIt->second, universe)) {
        return jsonError(422, "Universe must be an integer");
      }
    }

    if (auto channelIt = form.find("channel_count"); channelIt != form.end() && !channelIt->second.empty()) {
      if (!parseInt(channelIt->second, channelCount)) {
        return jsonError(422, "channel_count must be an integer");
      }
    }

    const bool allowOverlap = parseBoolLike(form, "allow_overlap");

    auto result = db_.createFixture(trim(nameIt->second), templateId, universe, startAddress, channelCount, allowOverlap);
    if (!result.ok) {
      if (result.error.find("overlaps") != std::string::npos) {
        return jsonError(409, result.error);
      }
      return jsonError(422, result.error);
    }

    rebuildAllUniversesFromDatabase();

    std::ostringstream ss;
    ss << "{\"ok\":true,\"fixtureId\":" << result.fixtureId << ",\"channelCount\":" << result.channelCount
       << '}';
    return jsonOk(ss.str(), 201);
  }

  if (request.method == "POST" && segments.size() == 5 && segments[0] == "api" && segments[1] == "fixtures" &&
      segments[3] == "channels") {
    int fixtureId = 0;
    int channelIndex = 0;

    if (!parseInt(segments[2], fixtureId) || !parseInt(segments[4], channelIndex)) {
      return jsonError(400, "Invalid fixture/channel path");
    }

    auto form = parseFormEncoded(request.body);
    std::string error;
    int value = 0;

    if (!getRequiredInt(form, "value", value, error)) {
      return jsonError(422, error);
    }

    ChannelPatch patch;
    if (!db_.updateFixtureChannelValue(fixtureId, channelIndex, value, patch, error)) {
      return jsonError(422, error);
    }

    dmx_.setChannel(patch.universe, patch.absoluteAddress, patch.value);

    std::ostringstream ss;
    ss << "{\"ok\":true,\"absoluteAddress\":" << patch.absoluteAddress << ",\"value\":" << patch.value << '}';
    return jsonOk(ss.str());
  }

  if (request.method == "POST" && segments.size() == 4 && segments[0] == "api" && segments[1] == "fixtures" &&
      segments[3] == "enabled") {
    int fixtureId = 0;
    if (!parseInt(segments[2], fixtureId)) {
      return jsonError(400, "Invalid fixture id");
    }

    auto form = parseFormEncoded(request.body);
    const bool enabled = parseBoolLike(form, "enabled");

    std::string error;
    if (!db_.setFixtureEnabled(fixtureId, enabled, error)) {
      return jsonError(422, error);
    }

    rebuildAllUniversesFromDatabase();
    return jsonOk("{\"ok\":true}");
  }

  if (request.method == "POST" && request.path == "/api/fixtures/reorder") {
    auto form = parseFormEncoded(request.body);
    auto idsIt = form.find("fixture_ids");
    if (idsIt == form.end() || trim(idsIt->second).empty()) {
      return jsonError(422, "fixture_ids is required");
    }

    const auto fixtureIds = parseCsvInts(idsIt->second);
    std::string error;
    if (!db_.reorderFixtures(fixtureIds, error)) {
      return jsonError(422, error);
    }

    return jsonOk("{\"ok\":true}");
  }

  if (request.method == "POST" && segments.size() == 4 && segments[0] == "api" && segments[1] == "fixtures" &&
      segments[3] == "delete") {
    int fixtureId = 0;
    if (!parseInt(segments[2], fixtureId)) {
      return jsonError(400, "Invalid fixture id");
    }

    std::string error;
    if (!db_.deleteFixture(fixtureId, error)) {
      return jsonError(422, error);
    }

    rebuildAllUniversesFromDatabase();
    return jsonOk("{\"ok\":true}");
  }

  if (request.method == "POST" && request.path == "/api/scenes") {
    auto form = parseFormEncoded(request.body);
    auto nameIt = form.find("name");
    if (nameIt == form.end() || trim(nameIt->second).empty()) {
      return jsonError(422, "Scene name is required");
    }

    float transitionSeconds = 1.0F;
    if (auto transitionIt = form.find("transition_seconds"); transitionIt != form.end() && !trim(transitionIt->second).empty()) {
      if (!parseFloatStrict(trim(transitionIt->second), transitionSeconds)) {
        return jsonError(422, "transition_seconds must be a float");
      }
    }

    std::string error;
    const int sceneId = db_.createScene(trim(nameIt->second), transitionSeconds, error);
    if (sceneId <= 0) {
      if (error.find("UNIQUE") != std::string::npos) {
        return jsonError(409, "Scene name already exists");
      }
      return jsonError(422, error);
    }

    logMessage(LogLevel::Info, "scene", "Created scene " + trim(nameIt->second));

    std::ostringstream ss;
    ss << "{\"ok\":true,\"sceneId\":" << sceneId << '}';
    return jsonOk(ss.str(), 201);
  }

  if (request.method == "POST" && segments.size() == 4 && segments[0] == "api" && segments[1] == "scenes" &&
      segments[3] == "update") {
    int sceneId = 0;
    if (!parseInt(segments[2], sceneId)) {
      return jsonError(400, "Invalid scene id");
    }

    auto form = parseFormEncoded(request.body);
    auto nameIt = form.find("name");
    if (nameIt == form.end() || trim(nameIt->second).empty()) {
      return jsonError(422, "Scene name is required");
    }

    float transitionSeconds = 1.0F;
    std::string parseError;
    if (!getRequiredFloat(form, "transition_seconds", transitionSeconds, parseError)) {
      return jsonError(422, parseError);
    }

    std::string error;
    if (!db_.updateScene(sceneId, trim(nameIt->second), transitionSeconds, error)) {
      if (error.find("UNIQUE") != std::string::npos) {
        return jsonError(409, "Scene name already exists");
      }
      return jsonError(422, error);
    }

    return jsonOk("{\"ok\":true}");
  }

  if (request.method == "POST" && segments.size() == 4 && segments[0] == "api" && segments[1] == "scenes" &&
      segments[3] == "capture") {
    int sceneId = 0;
    if (!parseInt(segments[2], sceneId)) {
      return jsonError(400, "Invalid scene id");
    }

    std::string error;
    if (!db_.captureScene(sceneId, error)) {
      return jsonError(422, error);
    }

    return jsonOk("{\"ok\":true}");
  }

  if (request.method == "POST" && segments.size() == 4 && segments[0] == "api" && segments[1] == "scenes" &&
      segments[3] == "recall") {
    int sceneId = 0;
    if (!parseInt(segments[2], sceneId)) {
      return jsonError(400, "Invalid scene id");
    }

    auto form = parseFormEncoded(request.body);
    float transitionSeconds = -1.0F;
    if (auto it = form.find("transition_seconds"); it != form.end() && !trim(it->second).empty()) {
      if (!parseFloatStrict(trim(it->second), transitionSeconds)) {
        return jsonError(422, "transition_seconds must be a float");
      }
    }

    std::string error;
    if (!startSceneTransition(sceneId, transitionSeconds, error)) {
      return jsonError(422, error);
    }

    return jsonOk("{\"ok\":true}");
  }

  if (request.method == "POST" && segments.size() == 4 && segments[0] == "api" && segments[1] == "scenes" &&
      segments[3] == "delete") {
    int sceneId = 0;
    if (!parseInt(segments[2], sceneId)) {
      return jsonError(400, "Invalid scene id");
    }

    std::string error;
    if (!db_.deleteScene(sceneId, error)) {
      return jsonError(422, error);
    }

    return jsonOk("{\"ok\":true}");
  }

  if (request.method == "POST" && request.path == "/api/midi/input-mode") {
    auto form = parseFormEncoded(request.body);
    auto modeIt = form.find("mode");
    if (modeIt == form.end() || trim(modeIt->second).empty()) {
      return jsonError(422, "mode is required");
    }

    std::string mode = trim(modeIt->second);
    if (mode != "all") {
      const auto inputs = midi_.inputPorts();
      const bool exists = std::any_of(inputs.begin(), inputs.end(), [&mode](const MidiInputPort& input) {
        return input.id == mode;
      });
      if (!exists) {
        return jsonError(422, "Unknown MIDI input id");
      }
    }

    std::string error;
    if (!db_.setSetting("midi.input_mode", mode, error)) {
      return jsonError(500, error);
    }

    {
      std::scoped_lock lock(midiMutex_);
      midiInputMode_ = mode;
    }

    std::ostringstream ss;
    ss << "{\"ok\":true,\"mode\":\"" << jsonEscape(mode) << "\"}";
    return jsonOk(ss.str());
  }

  if (request.method == "POST" && request.path == "/api/midi/learn/start") {
    auto form = parseFormEncoded(request.body);
    auto controlIt = form.find("control_id");
    if (controlIt == form.end() || trim(controlIt->second).empty()) {
      return jsonError(422, "control_id is required");
    }

    {
      std::scoped_lock lock(midiMutex_);
      midiLearningControlId_ = trim(controlIt->second);
    }

    return jsonOk("{\"ok\":true}");
  }

  if (request.method == "POST" && request.path == "/api/midi/learn/cancel") {
    {
      std::scoped_lock lock(midiMutex_);
      midiLearningControlId_.clear();
    }
    return jsonOk("{\"ok\":true}");
  }

  if (request.method == "POST" && request.path == "/api/midi/mappings/clear") {
    auto form = parseFormEncoded(request.body);
    auto controlIt = form.find("control_id");
    if (controlIt == form.end() || trim(controlIt->second).empty()) {
      return jsonError(422, "control_id is required");
    }

    const std::string controlId = trim(controlIt->second);
    std::string error;
    if (!db_.deleteMidiMapping(controlId, error)) {
      return jsonError(500, error);
    }

    {
      std::scoped_lock lock(midiMutex_);
      midiMappings_.erase(controlId);
      midiLastAppliedValues_.erase(controlId);
      if (midiLearningControlId_ == controlId) {
        midiLearningControlId_.clear();
      }
    }

    return jsonOk("{\"ok\":true}");
  }

  if (request.method == "POST" && request.path == "/api/groups") {
    auto form = parseFormEncoded(request.body);
    auto nameIt = form.find("name");
    if (nameIt == form.end() || trim(nameIt->second).empty()) {
      return jsonError(422, "Group name is required");
    }

    std::string error;
    const int groupId = db_.createGroup(trim(nameIt->second), error);
    if (groupId <= 0) {
      if (error.find("UNIQUE") != std::string::npos) {
        return jsonError(409, "Group name already exists");
      }
      return jsonError(500, error);
    }

    std::ostringstream ss;
    ss << "{\"ok\":true,\"groupId\":" << groupId << '}';
    return jsonOk(ss.str(), 201);
  }

  if (request.method == "POST" && segments.size() == 4 && segments[0] == "api" && segments[1] == "groups" &&
      segments[3] == "fixtures") {
    int groupId = 0;
    if (!parseInt(segments[2], groupId)) {
      return jsonError(400, "Invalid group id");
    }

    auto form = parseFormEncoded(request.body);
    auto fixtureIdsIt = form.find("fixture_ids");
    const auto fixtureIds = fixtureIdsIt == form.end() ? std::vector<int>{} : parseCsvInts(fixtureIdsIt->second);

    std::string error;
    if (!db_.setGroupFixtures(groupId, fixtureIds, error)) {
      return jsonError(422, error);
    }

    return jsonOk("{\"ok\":true}");
  }

  if (request.method == "POST" && segments.size() == 4 && segments[0] == "api" && segments[1] == "groups" &&
      segments[3] == "delete") {
    int groupId = 0;
    if (!parseInt(segments[2], groupId)) {
      return jsonError(400, "Invalid group id");
    }

    std::string error;
    if (!db_.deleteGroup(groupId, error)) {
      return jsonError(422, error);
    }

    return jsonOk("{\"ok\":true}");
  }

  if (request.method == "POST" && segments.size() == 5 && segments[0] == "api" && segments[1] == "groups" &&
      segments[3] == "kinds") {
    int groupId = 0;
    if (!parseInt(segments[2], groupId)) {
      return jsonError(400, "Invalid group id");
    }
    const std::string targetKind = toLower(segments[4]);

    auto form = parseFormEncoded(request.body);
    int value = 0;
    std::string error;
    if (!getRequiredInt(form, "value", value, error)) {
      return jsonError(422, error);
    }

    const auto groups = db_.listGroups(error);
    if (!error.empty()) {
      return jsonError(500, error);
    }

    auto groupIt = std::find_if(groups.begin(), groups.end(), [groupId](const FixtureGroup& g) { return g.id == groupId; });
    if (groupIt == groups.end()) {
      return jsonError(404, "Group not found");
    }

    const auto resolved = resolveFixtures(error);
    if (!error.empty()) {
      return jsonError(500, error);
    }

    std::unordered_set<int> members(groupIt->fixtureIds.begin(), groupIt->fixtureIds.end());
    int updated = 0;

    for (const auto& fixtureView : resolved) {
      if (!members.contains(fixtureView.fixture.id)) {
        continue;
      }

      for (const auto& channel : fixtureView.templateChannels) {
        if (toLower(channel.kind) != targetKind) {
          continue;
        }

        ChannelPatch patch;
        if (!db_.updateFixtureChannelValue(fixtureView.fixture.id, channel.channelIndex, value, patch, error)) {
          continue;
        }

        dmx_.setChannel(patch.universe, patch.absoluteAddress, patch.value);
        ++updated;
      }
    }

    std::ostringstream ss;
    ss << "{\"ok\":true,\"updated\":" << updated << '}';
    return jsonOk(ss.str());
  }

  if (request.method == "POST" && segments.size() == 4 && segments[0] == "api" && segments[1] == "groups" &&
      segments[3] == "mode") {
    int groupId = 0;
    if (!parseInt(segments[2], groupId)) {
      return jsonError(400, "Invalid group id");
    }

    auto form = parseFormEncoded(request.body);
    auto labelIt = form.find("label");
    if (labelIt == form.end() || trim(labelIt->second).empty()) {
      return jsonError(422, "Mode label is required");
    }

    const std::string requestedLabel = trim(labelIt->second);
    std::string error;

    const auto groups = db_.listGroups(error);
    if (!error.empty()) {
      return jsonError(500, error);
    }

    auto groupIt = std::find_if(groups.begin(), groups.end(), [groupId](const FixtureGroup& g) { return g.id == groupId; });
    if (groupIt == groups.end()) {
      return jsonError(404, "Group not found");
    }

    const auto resolved = resolveFixtures(error);
    if (!error.empty()) {
      return jsonError(500, error);
    }

    std::unordered_set<int> members(groupIt->fixtureIds.begin(), groupIt->fixtureIds.end());
    int updated = 0;

    for (const auto& fixtureView : resolved) {
      if (!members.contains(fixtureView.fixture.id)) {
        continue;
      }

      for (const auto& channel : fixtureView.templateChannels) {
        if (toLower(channel.kind) != "mode") {
          continue;
        }

        std::optional<ChannelRange> selected;
        for (const auto& range : channel.ranges) {
          if (containsKeyword(range.label, requestedLabel)) {
            selected = range;
            break;
          }
        }

        if (!selected.has_value() && !channel.ranges.empty()) {
          selected = channel.ranges.front();
        }

        if (!selected.has_value()) {
          continue;
        }

        ChannelPatch patch;
        if (!db_.updateFixtureChannelValue(fixtureView.fixture.id, channel.channelIndex, midpoint(*selected), patch,
                                           error)) {
          continue;
        }

        dmx_.setChannel(patch.universe, patch.absoluteAddress, patch.value);
        ++updated;
      }
    }

    std::ostringstream ss;
    ss << "{\"ok\":true,\"updated\":" << updated << '}';
    return jsonOk(ss.str());
  }

  if (request.method == "POST" && request.path == "/api/dmx/patches") {
    auto form = parseFormEncoded(request.body);
    auto patchesIt = form.find("patches");
    if (patchesIt == form.end() || trim(patchesIt->second).empty()) {
      return jsonError(422, "patches is required");
    }

    const auto patches = parseDmxPatchTriples(patchesIt->second);
    if (patches.empty()) {
      return jsonError(422, "No valid DMX patches provided");
    }

    int applied = 0;
    for (const auto& patch : patches) {
      if (patch.universe < 1 || patch.address < 1 || patch.address > 512) {
        continue;
      }

      dmx_.setChannel(patch.universe, patch.address, patch.value);
      ++applied;
    }

    std::ostringstream ss;
    ss << "{\"ok\":true,\"applied\":" << applied << '}';
    return jsonOk(ss.str());
  }

  if (request.method == "POST" && request.path == "/api/dmx/blackout") {
    // Panic blackout: stop reactive playback, persist fixture values to zero, and clear every known universe frame.
    audio_.setReactiveMode(false);
    persistBlackoutToDatabase();

    std::set<int> universes;
    for (int universe : dmx_.knownUniverses()) {
      if (universe >= 1) {
        universes.insert(universe);
      }
    }
    universes.insert(dmx_.outputUniverse());

    for (int universe : universes) {
      dmx_.clearUniverse(universe);
    }

    {
      std::scoped_lock lock(reactiveMutex_);
      reactiveStates_.clear();
      lastReactiveApply_ = {};
    }

    return jsonOk("{\"ok\":true,\"reactiveMode\":false}");
  }

  if (request.method == "POST" && request.path == "/api/dmx/write-retry-limit") {
    auto form = parseFormEncoded(request.body);
    std::string error;
    int retries = 10;
    if (!getRequiredInt(form, "retries", retries, error)) {
      return jsonError(422, error);
    }

    retries = std::clamp(retries, 1, 200);
    dmx_.setWriteRetryLimit(retries);

    if (!db_.setSetting("dmx.write_retry_limit", std::to_string(retries), error)) {
      return jsonError(500, error);
    }

    logMessage(LogLevel::Info, "dmx", "Write retry limit set to " + std::to_string(retries));

    std::ostringstream ss;
    ss << "{\"ok\":true,\"retries\":" << retries << '}';
    return jsonOk(ss.str());
  }

  if (request.method == "POST" && request.path == "/api/dmx/transport-settings") {
    auto form = parseFormEncoded(request.body);
    std::string error;

    int frameIntervalMs = 33;
    int reconnectBaseMs = 800;
    int probeTimeoutMs = 350;
    int serialReadTimeoutMs = 250;
    bool strictPreferred = true;

    if (!getRequiredInt(form, "frame_interval_ms", frameIntervalMs, error)) {
      return jsonError(422, error);
    }
    if (!getRequiredInt(form, "reconnect_base_ms", reconnectBaseMs, error)) {
      return jsonError(422, error);
    }
    if (!getRequiredInt(form, "probe_timeout_ms", probeTimeoutMs, error)) {
      return jsonError(422, error);
    }
    if (!getRequiredInt(form, "serial_read_timeout_ms", serialReadTimeoutMs, error)) {
      return jsonError(422, error);
    }
    if (!getRequiredBool(form, "strict_preferred_device", strictPreferred, error)) {
      return jsonError(422, error);
    }

    dmx_.setFrameIntervalMs(frameIntervalMs);
    dmx_.setReconnectBaseMs(reconnectBaseMs);
    dmx_.setProbeTimeoutMs(probeTimeoutMs);
    dmx_.setSerialReadTimeoutMs(serialReadTimeoutMs);
    dmx_.setStrictPreferredDevice(strictPreferred);

    if (!db_.setSetting("dmx.frame_interval_ms", std::to_string(frameIntervalMs), error)) {
      return jsonError(500, error);
    }
    if (!db_.setSetting("dmx.reconnect_base_ms", std::to_string(reconnectBaseMs), error)) {
      return jsonError(500, error);
    }
    if (!db_.setSetting("dmx.probe_timeout_ms", std::to_string(probeTimeoutMs), error)) {
      return jsonError(500, error);
    }
    if (!db_.setSetting("dmx.serial_read_timeout_ms", std::to_string(serialReadTimeoutMs), error)) {
      return jsonError(500, error);
    }
    if (!db_.setSetting("dmx.strict_preferred_device", strictPreferred ? "true" : "false", error)) {
      return jsonError(500, error);
    }

    const auto status = dmx_.status();
    std::ostringstream ss;
    ss << "{\"ok\":true,";
    ss << "\"frameIntervalMs\":" << status.frameIntervalMs << ',';
    ss << "\"reconnectBaseMs\":" << status.reconnectBaseMs << ',';
    ss << "\"probeTimeoutMs\":" << status.probeTimeoutMs << ',';
    ss << "\"serialReadTimeoutMs\":" << status.serialReadTimeoutMs << ',';
    ss << "\"strictPreferredDevice\":" << jsonBool(status.strictPreferredDevice);
    ss << '}';
    return jsonOk(ss.str());
  }

  if (request.method == "POST" && request.path == "/api/dmx/output-universe") {
    auto form = parseFormEncoded(request.body);
    std::string error;
    int universe = 1;
    if (!getRequiredInt(form, "universe", universe, error)) {
      return jsonError(422, error);
    }

    if (universe < 1) {
      return jsonError(422, "Universe must be >= 1");
    }

    dmx_.setOutputUniverse(universe);
    rebuildAllUniversesFromDatabase();

    std::ostringstream ss;
    ss << "{\"ok\":true,\"outputUniverse\":" << universe << '}';
    return jsonOk(ss.str());
  }

  if (request.method == "POST" && request.path == "/api/dmx/universes") {
    auto form = parseFormEncoded(request.body);
    std::string error;
    int universe = 1;
    if (!getRequiredInt(form, "universe", universe, error)) {
      return jsonError(422, error);
    }

    if (universe < 1) {
      return jsonError(422, "Universe must be >= 1");
    }

    dmx_.ensureUniverse(universe);

    std::ostringstream ss;
    ss << "{\"ok\":true,\"universe\":" << universe << '}';
    return jsonOk(ss.str(), 201);
  }

  if (request.method == "GET" && request.path == "/api/dmx/devices") {
    const auto dmxStatus = dmx_.status();
    const auto devices = dmx_.devices();
    std::ostringstream ss;
    ss << "{\"ok\":true,";
    ss << "\"backend\":\"" << jsonEscape(dmxStatus.backend) << "\",";
    ss << "\"selectionMode\":\"" << jsonEscape(dmxStatus.preferredDeviceId.empty() ? "auto" : "manual") << "\",";
    ss << "\"preferredDeviceId\":\"" << jsonEscape(dmxStatus.preferredDeviceId) << "\",";
    ss << "\"activeDeviceId\":\"" << jsonEscape(dmxStatus.activeDeviceId) << "\",";
    ss << "\"devices\":";
    appendDmxOutputDevicesJson(ss, devices);
    ss << '}';
    return jsonOk(ss.str());
  }

  if (request.method == "POST" && request.path == "/api/dmx/devices/select") {
    auto form = parseFormEncoded(request.body);
    std::string mode = toLower(trim(form["mode"]));
    std::string deviceId = trim(form["device_id"]);

    if (mode.empty()) {
      mode = deviceId.empty() ? "auto" : "manual";
    }
    if (mode != "auto" && mode != "manual") {
      return jsonError(422, "mode must be auto or manual");
    }

    if (mode == "auto") {
      deviceId.clear();
    } else if (deviceId.empty()) {
      return jsonError(422, "device_id is required when mode=manual");
    }

    dmx_.setPreferredDeviceId(deviceId);
    dmx_.forceReconnect();
    dmx_.refreshDevices();

    std::string writeSettingError;
    if (!db_.setSetting("dmx.preferred_device_id", deviceId, writeSettingError)) {
      return jsonError(500, writeSettingError);
    }

    logMessage(LogLevel::Info, "dmx",
               deviceId.empty() ? "DMX device selection mode set to auto"
                                : ("DMX preferred device set to " + deviceId));

    std::ostringstream ss;
    ss << "{\"ok\":true,";
    ss << "\"selectionMode\":\"" << jsonEscape(deviceId.empty() ? "auto" : "manual") << "\",";
    ss << "\"preferredDeviceId\":\"" << jsonEscape(deviceId) << "\"";
    ss << '}';
    return jsonOk(ss.str());
  }

  if (request.method == "POST" && request.path == "/api/dmx/devices/scan") {
    dmx_.forceReconnect();
    dmx_.refreshDevices();
    const auto dmxStatus = dmx_.status();
    const auto devices = dmx_.devices();
    std::ostringstream ss;
    ss << "{\"ok\":true,";
    ss << "\"connected\":" << jsonBool(dmxStatus.connected) << ',';
    ss << "\"selectionMode\":\"" << jsonEscape(dmxStatus.preferredDeviceId.empty() ? "auto" : "manual") << "\",";
    ss << "\"preferredDeviceId\":\"" << jsonEscape(dmxStatus.preferredDeviceId) << "\",";
    ss << "\"activeDeviceId\":\"" << jsonEscape(dmxStatus.activeDeviceId) << "\",";
    ss << "\"devices\":";
    appendDmxOutputDevicesJson(ss, devices);
    ss << '}';
    return jsonOk(ss.str());
  }

  if (request.method == "POST" && request.path == "/api/audio/reactive") {
    auto form = parseFormEncoded(request.body);
    const bool enabled = parseBoolLike(form, "enabled");
    audio_.setReactiveMode(enabled);
    logMessage(LogLevel::Info, "audio", std::string("Reactive mode ") + (enabled ? "enabled" : "disabled"));

    if (!enabled) {
      rebuildAllUniversesFromDatabase();
    }

    return jsonOk("{\"ok\":true}");
  }

  if (request.method == "POST" && request.path == "/api/audio/reactive-threshold") {
    auto form = parseFormEncoded(request.body);
    auto thresholdIt = form.find("threshold");
    if (thresholdIt == form.end()) {
      return jsonError(422, "Missing field: threshold");
    }

    float threshold = 0.0F;
    if (!parseFloatStrict(thresholdIt->second, threshold)) {
      return jsonError(422, "Invalid float field: threshold");
    }

    threshold = std::clamp(threshold, 0.0F, 1.0F);
    reactiveVolumeThreshold_.store(threshold);
    logMessage(LogLevel::Info, "audio", "Reactive threshold set to " + std::to_string(threshold));

    std::ostringstream ss;
    ss << "{\"ok\":true,\"reactiveVolumeThreshold\":" << threshold << '}';
    return jsonOk(ss.str());
  }

  if (request.method == "POST" && request.path == "/api/audio/reactive-profile") {
    auto form = parseFormEncoded(request.body);
    auto profileIt = form.find("profile");
    if (profileIt == form.end() || trim(profileIt->second).empty()) {
      return jsonError(422, "Missing field: profile");
    }

    const int profile = reactiveProfileFromName(profileIt->second);
    reactiveProfile_.store(profile);
    logMessage(LogLevel::Info, "audio", "Reactive profile set to " + reactiveProfileName(profile));

    std::ostringstream ss;
    ss << "{\"ok\":true,\"reactiveProfile\":\"" << jsonEscape(reactiveProfileName(profile)) << "\"}";
    return jsonOk(ss.str());
  }

  if (request.method == "POST" && request.path == "/api/audio/input-device") {
    auto form = parseFormEncoded(request.body);
    std::string error;
    int deviceId = -1;
    if (!getRequiredInt(form, "device_id", deviceId, error)) {
      return jsonError(422, error);
    }

    if (!audio_.selectInputDevice(deviceId, error)) {
      return jsonError(422, error);
    }
    logMessage(LogLevel::Info, "audio", "Selected input device id " + std::to_string(audio_.selectedInputDeviceId()));

    std::ostringstream ss;
    ss << "{\"ok\":true,\"selectedInputDeviceId\":" << audio_.selectedInputDeviceId() << '}';
    return jsonOk(ss.str());
  }

  return jsonError(404, "Unknown API route");
}

HttpResponse AppController::handleRequest(const HttpRequest& request) {
  if (request.path.rfind("/api/", 0) == 0 || request.path == "/api") {
    return handleApi(request);
  }
  return serveStatic(request.path);
}

}  // namespace tuxdmx

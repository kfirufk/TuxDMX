#include "midi_engine.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <tuple>
#include <utility>

#ifdef TUXDMX_WITH_RTMIDI
#if __has_include(<RtMidi.h>)
#include <RtMidi.h>
#elif __has_include(<rtmidi/RtMidi.h>)
#include <rtmidi/RtMidi.h>
#else
#error "TUXDMX_WITH_RTMIDI is enabled, but RtMidi header was not found."
#endif
#endif
#ifdef TUXDMX_WITH_COREMIDI
#include <CoreFoundation/CoreFoundation.h>
#include <CoreMIDI/CoreMIDI.h>
#endif

#include "logger.hpp"

namespace tuxdmx {

namespace {

int clampMidiByte(int value) { return std::max(0, std::min(127, value)); }

#ifdef TUXDMX_WITH_COREMIDI

std::string cfStringToStd(CFStringRef value) {
  if (value == nullptr) {
    return {};
  }

  const CFIndex length = CFStringGetLength(value);
  const CFIndex maxSize = CFStringGetMaximumSizeForEncoding(length, kCFStringEncodingUTF8) + 1;
  std::string out(static_cast<std::size_t>(std::max<CFIndex>(1, maxSize)), '\0');
  if (!CFStringGetCString(value, out.data(), maxSize, kCFStringEncodingUTF8)) {
    return {};
  }
  out.resize(std::strlen(out.c_str()));
  return out;
}

std::string midiEndpointName(MIDIEndpointRef endpoint) {
  CFStringRef cfName = nullptr;
  const OSStatus status = MIDIObjectGetStringProperty(endpoint, kMIDIPropertyDisplayName, &cfName);
  if (status != noErr || cfName == nullptr) {
    if (cfName != nullptr) {
      CFRelease(cfName);
    }
    return "MIDI Input";
  }

  std::string name = cfStringToStd(cfName);
  CFRelease(cfName);
  if (name.empty()) {
    return "MIDI Input";
  }
  return name;
}

std::string midiEndpointId(MIDIEndpointRef endpoint) {
  SInt32 uniqueId = 0;
  if (MIDIObjectGetIntegerProperty(endpoint, kMIDIPropertyUniqueID, &uniqueId) != noErr) {
    return std::to_string(static_cast<std::uint64_t>(endpoint));
  }
  return std::to_string(static_cast<int>(uniqueId));
}

struct CoreMidiSourceContext {
  MidiEngine* engine = nullptr;
  std::string id;
  std::string name;
};

void coreMidiReadProc(const MIDIPacketList* packetList, void* readProcRefCon, void* srcConnRefCon) {
  (void)readProcRefCon;
  if (packetList == nullptr || srcConnRefCon == nullptr) {
    return;
  }

  auto* context = static_cast<CoreMidiSourceContext*>(srcConnRefCon);
  if (context->engine == nullptr) {
    return;
  }

  const MIDIPacket* packet = &packetList->packet[0];
  for (UInt32 packetIndex = 0; packetIndex < packetList->numPackets; ++packetIndex) {
    std::size_t cursor = 0;
    while (cursor + 1 < packet->length) {
      const unsigned char status = packet->data[cursor];
      const unsigned char command = static_cast<unsigned char>(status & 0xF0);

      std::size_t messageSize = 0;
      if (command == 0x80 || command == 0x90 || command == 0xA0 || command == 0xB0 || command == 0xE0) {
        messageSize = 3;
      } else if (command == 0xC0 || command == 0xD0) {
        messageSize = 2;
      } else {
        cursor += 1;
        continue;
      }

      if (cursor + messageSize > packet->length) {
        break;
      }

      std::vector<unsigned char> raw;
      raw.reserve(messageSize);
      for (std::size_t i = 0; i < messageSize; ++i) {
        raw.push_back(packet->data[cursor + i]);
      }
      context->engine->dispatchRawMessage(context->id, context->name, raw);
      cursor += messageSize;
    }

    packet = MIDIPacketNext(packet);
  }
}

#endif

}  // namespace

struct MidiEngine::Impl {
#ifdef TUXDMX_WITH_RTMIDI
  struct InputHandle {
    std::string id;
    std::string name;
    std::unique_ptr<RtMidiIn> midi;
  };

  std::vector<InputHandle> handles;
#endif
#ifdef TUXDMX_WITH_COREMIDI
  MIDIClientRef client = 0;
  MIDIPortRef inputPort = 0;

  struct SourceConnection {
    MIDIEndpointRef endpoint = 0;
    std::unique_ptr<CoreMidiSourceContext> context;
  };

  std::vector<SourceConnection> sources;
#endif
};

MidiEngine::MidiEngine() : impl_(std::make_unique<Impl>()) {
#ifdef TUXDMX_WITH_RTMIDI
  backendName_ = "rtmidi";
#elif defined(TUXDMX_WITH_COREMIDI)
  backendName_ = "coremidi";
#else
  backendName_ = "unavailable";
#endif
}

MidiEngine::~MidiEngine() { stop(); }

void MidiEngine::start() {
#if !defined(TUXDMX_WITH_RTMIDI) && !defined(TUXDMX_WITH_COREMIDI)
  return;
#else
  bool expected = false;
  if (!running_.compare_exchange_strong(expected, true)) {
    return;
  }

  worker_ = std::thread([this] { loop(); });
#endif
}

void MidiEngine::stop() {
#if !defined(TUXDMX_WITH_RTMIDI) && !defined(TUXDMX_WITH_COREMIDI)
  return;
#else
  bool expected = true;
  if (!running_.compare_exchange_strong(expected, false)) {
    return;
  }

  if (worker_.joinable()) {
    worker_.join();
  }

  std::scoped_lock lock(stateMutex_);
#ifdef TUXDMX_WITH_RTMIDI
  impl_->handles.clear();
#endif
#ifdef TUXDMX_WITH_COREMIDI
  for (auto& source : impl_->sources) {
    if (impl_->inputPort != 0 && source.endpoint != 0) {
      MIDIPortDisconnectSource(impl_->inputPort, source.endpoint);
    }
  }
  impl_->sources.clear();
  if (impl_->inputPort != 0) {
    MIDIPortDispose(impl_->inputPort);
    impl_->inputPort = 0;
  }
  if (impl_->client != 0) {
    MIDIClientDispose(impl_->client);
    impl_->client = 0;
  }
#endif
  ports_.clear();
#endif
}

bool MidiEngine::supported() const {
#if defined(TUXDMX_WITH_RTMIDI) || defined(TUXDMX_WITH_COREMIDI)
  return true;
#else
  return false;
#endif
}

std::string MidiEngine::backendName() const {
  std::scoped_lock lock(stateMutex_);
  return backendName_;
}

std::vector<MidiInputPort> MidiEngine::inputPorts() const {
  std::scoped_lock lock(stateMutex_);
  return ports_;
}

void MidiEngine::setMessageCallback(MessageCallback callback) {
  std::scoped_lock lock(callbackMutex_);
  callback_ = std::move(callback);
}

void MidiEngine::dispatchRawMessage(const std::string& inputId, const std::string& inputName,
                                    const std::vector<unsigned char>& raw) {
  if (raw.size() < 2) {
    return;
  }

  const int status = clampMidiByte(static_cast<int>(raw[0]));
  const int data1 = clampMidiByte(static_cast<int>(raw[1]));
  const int data2 = raw.size() >= 3 ? clampMidiByte(static_cast<int>(raw[2])) : 0;
  const int command = status & 0xF0;
  const int channel = (status & 0x0F) + 1;

  MidiMessage message;
  message.inputId = inputId;
  message.inputName = inputName;
  message.channel = channel;
  message.number = data1;
  message.value = data2;

  if (command == 0xB0) {
    message.type = "cc";
    message.on = data2 >= 64;
    message.mappedValue = static_cast<int>(std::lround((static_cast<double>(data2) / 127.0) * 255.0));
  } else if (command == 0x90) {
    message.type = "note";
    message.on = data2 > 0;
    message.mappedValue = message.on ? 255 : 0;
  } else if (command == 0x80) {
    message.type = "note";
    message.on = false;
    message.value = 0;
    message.mappedValue = 0;
  } else {
    return;
  }

  MessageCallback callbackCopy;
  {
    std::scoped_lock lock(callbackMutex_);
    callbackCopy = callback_;
  }
  if (callbackCopy) {
    callbackCopy(message);
  }
}

void MidiEngine::loop() {
#if defined(TUXDMX_WITH_RTMIDI)
  auto lastRefresh = std::chrono::steady_clock::time_point{};

  while (running_.load()) {
    const auto now = std::chrono::steady_clock::now();

    if (lastRefresh.time_since_epoch().count() == 0 || now - lastRefresh > std::chrono::milliseconds(1200)) {
      std::string refreshError;
      try {
        RtMidiIn probe;
        const unsigned int portCount = probe.getPortCount();

        std::vector<MidiInputPort> discovered;
        discovered.reserve(portCount);
        for (unsigned int idx = 0; idx < portCount; ++idx) {
          MidiInputPort port;
          port.id = std::to_string(idx);
          port.name = probe.getPortName(idx);
          discovered.push_back(std::move(port));
        }

        bool changed = discovered.size() != ports_.size();
        if (!changed) {
          for (std::size_t i = 0; i < discovered.size(); ++i) {
            if (discovered[i].id != ports_[i].id || discovered[i].name != ports_[i].name) {
              changed = true;
              break;
            }
          }
        }

        if (changed) {
          std::vector<Impl::InputHandle> nextHandles;
          std::vector<MidiInputPort> nextPorts;
          nextHandles.reserve(discovered.size());
          nextPorts.reserve(discovered.size());

          for (unsigned int idx = 0; idx < portCount; ++idx) {
            try {
              Impl::InputHandle handle;
              handle.id = std::to_string(idx);
              handle.name = probe.getPortName(idx);
              handle.midi = std::make_unique<RtMidiIn>();
              handle.midi->ignoreTypes(true, true, true);
              handle.midi->openPort(idx, "tuxdmx-midi-" + std::to_string(idx));

              MidiInputPort port;
              port.id = handle.id;
              port.name = handle.name;
              nextPorts.push_back(std::move(port));
              nextHandles.push_back(std::move(handle));
            } catch (const std::exception& ex) {
              logMessage(LogLevel::Warn, "midi", "Failed to open MIDI input " + std::to_string(idx) + ": " + ex.what());
            }
          }

          {
            std::scoped_lock lock(stateMutex_);
            impl_->handles = std::move(nextHandles);
            ports_ = std::move(nextPorts);
          }
        }
      } catch (const std::exception& ex) {
        refreshError = ex.what();
      }

      if (!refreshError.empty()) {
        logMessage(LogLevel::Warn, "midi", "Port refresh failed: " + refreshError);
      }

      lastRefresh = now;
    }

    std::vector<std::tuple<std::string, std::string, std::vector<unsigned char>>> pending;
    {
      std::scoped_lock lock(stateMutex_);
      for (auto& handle : impl_->handles) {
        if (!handle.midi) {
          continue;
        }

        std::vector<unsigned char> raw;
        while (running_.load()) {
          raw.clear();
          try {
            handle.midi->getMessage(&raw);
          } catch (const std::exception& ex) {
            logMessage(LogLevel::Warn, "midi", "Read error on " + handle.name + ": " + ex.what());
            break;
          }

          if (raw.empty()) {
            break;
          }

          pending.emplace_back(handle.id, handle.name, raw);
        }
      }
    }

    for (const auto& [inputId, inputName, raw] : pending) {
      dispatchRawMessage(inputId, inputName, raw);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(6));
  }
#elif defined(TUXDMX_WITH_COREMIDI)
  auto lastRefresh = std::chrono::steady_clock::time_point{};

  while (running_.load()) {
    const auto now = std::chrono::steady_clock::now();

    if (lastRefresh.time_since_epoch().count() == 0 || now - lastRefresh > std::chrono::milliseconds(1200)) {
      std::string refreshError;
      {
        std::scoped_lock lock(stateMutex_);

        if (impl_->client == 0) {
          if (MIDIClientCreate(CFSTR("tuxdmx-midi-client"), nullptr, nullptr, &impl_->client) != noErr) {
            refreshError = "MIDIClientCreate failed";
          }
        }

        if (refreshError.empty() && impl_->inputPort == 0) {
          if (MIDIInputPortCreate(impl_->client, CFSTR("tuxdmx-midi-input"), coreMidiReadProc, this, &impl_->inputPort)
              != noErr) {
            refreshError = "MIDIInputPortCreate failed";
          }
        }

        if (refreshError.empty()) {
          const ItemCount sourceCount = MIDIGetNumberOfSources();
          std::vector<MidiInputPort> discovered;
          discovered.reserve(static_cast<std::size_t>(sourceCount));

          for (ItemCount idx = 0; idx < sourceCount; ++idx) {
            const MIDIEndpointRef source = MIDIGetSource(idx);
            if (source == 0) {
              continue;
            }

            MidiInputPort port;
            port.id = midiEndpointId(source);
            port.name = midiEndpointName(source);
            discovered.push_back(std::move(port));
          }

          bool changed = discovered.size() != ports_.size();
          if (!changed) {
            for (std::size_t i = 0; i < discovered.size(); ++i) {
              if (discovered[i].id != ports_[i].id || discovered[i].name != ports_[i].name) {
                changed = true;
                break;
              }
            }
          }

          if (changed) {
            for (auto& source : impl_->sources) {
              MIDIPortDisconnectSource(impl_->inputPort, source.endpoint);
            }
            impl_->sources.clear();
            ports_.clear();

            for (ItemCount idx = 0; idx < sourceCount; ++idx) {
              const MIDIEndpointRef source = MIDIGetSource(idx);
              if (source == 0) {
                continue;
              }

              auto context = std::make_unique<CoreMidiSourceContext>();
              context->engine = this;
              context->id = midiEndpointId(source);
              context->name = midiEndpointName(source);

              if (MIDIPortConnectSource(impl_->inputPort, source, context.get()) != noErr) {
                logMessage(LogLevel::Warn, "midi", "Failed to connect CoreMIDI source: " + context->name);
                continue;
              }

              Impl::SourceConnection connection;
              connection.endpoint = source;
              connection.context = std::move(context);
              impl_->sources.push_back(std::move(connection));

              MidiInputPort port;
              port.id = impl_->sources.back().context->id;
              port.name = impl_->sources.back().context->name;
              ports_.push_back(std::move(port));
            }
          }
        }
      }

      if (!refreshError.empty()) {
        logMessage(LogLevel::Warn, "midi", "CoreMIDI refresh failed: " + refreshError);
      }

      lastRefresh = now;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
#else
  return;
#endif
}

}  // namespace tuxdmx

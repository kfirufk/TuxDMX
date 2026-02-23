#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "models.hpp"

namespace tuxdmx {

class MidiEngine {
 public:
  using MessageCallback = std::function<void(const MidiMessage&)>;

  MidiEngine();
  ~MidiEngine();

  MidiEngine(const MidiEngine&) = delete;
  MidiEngine& operator=(const MidiEngine&) = delete;

  void start();
  void stop();

  bool supported() const;
  std::string backendName() const;
  std::vector<MidiInputPort> inputPorts() const;

  void setMessageCallback(MessageCallback callback);
  void dispatchRawMessage(const std::string& inputId, const std::string& inputName, const std::vector<unsigned char>& raw);

 private:
  void loop();

  std::atomic<bool> running_{false};
  std::thread worker_;

  mutable std::mutex callbackMutex_;
  MessageCallback callback_;

  mutable std::mutex stateMutex_;
  std::string backendName_ = "unavailable";
  std::vector<MidiInputPort> ports_;
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace tuxdmx

#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "dmx_output_backend.hpp"

namespace tuxdmx {

inline constexpr std::string_view kDefaultDmxBackendName = "enttec-usb-pro";

std::string normalizeDmxBackendName(std::string_view backendName);
bool isSupportedDmxBackendName(std::string_view backendName);
std::vector<std::string> supportedDmxBackendNames();
std::unique_ptr<DmxOutputBackend> createDmxOutputBackend(std::string_view backendName, std::string& error);

}  // namespace tuxdmx

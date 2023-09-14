#pragma once

#include <span>
#include "state.hpp"

namespace fs = std::filesystem;

int processInput(std::span<const char*> args) noexcept;

void processMainConfigFile(ConfigState &state, const fs::path &config, const fs::path &configParent);

void processVoiceFiles(ConfigState &state, const fs::path &parent, const fs::path &configFile, const uint16_t &datID, fs::path filePath);
#pragma once

#include <filesystem>
#include "gcaxArchive.hpp"

namespace fs = std::filesystem;

void processVoiceFiles(const fs::path &parent, const fs::path &configFile, const uint16_t &id, const std::string &fileName);
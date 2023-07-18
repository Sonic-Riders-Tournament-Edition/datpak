#pragma once

#include <filesystem>
#include "gcaxArchive.hpp"

namespace fs = std::filesystem;

bool processVoiceFiles(const fs::path &parent, const fs::path &configFile, const uint16_t &id, fs::path filePath);
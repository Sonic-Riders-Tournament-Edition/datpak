#pragma once

#include <filesystem>
#include "gcaxArchive.hpp"

namespace fs = std::filesystem;

void getMainConfig(fs::path& path, const std::vector<std::string> &args);

fs::path getFolderConfig(const std::string &folderName);

void processVoiceFiles(const fs::path &parent, const fs::path &configFile, const uint16_t &id, const std::string &fileName);
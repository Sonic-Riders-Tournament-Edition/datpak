#pragma once

import <filesystem>;
#include "gcaxArchive.hpp"

namespace fs = std::filesystem;

bool processVoiceFiles(const fs::path &parent, const fs::path &configFile, const uint16_t &id, fs::path filePath);
void makeHeader(const fs::path &headerPath);

struct HeaderEntry{
	std::string name;
	uint32_t spec1;
	uint32_t spec2;
};
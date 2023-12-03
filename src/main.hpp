#pragma once

#include <span>

#include "state.hpp"

namespace fs = std::filesystem;

enum class return_code : std::int8_t{
	GeneralException = -1,
	Ok,
	CxxoptException,
	FilesystemException,
	HelpShown,
};

return_code processInput(std::span<const char*> args) noexcept;

void processMainConfigFile(ConfigState &state, const fs::path &config, const fs::path &configParent);

void processVoiceFiles(ConfigState &state, const fs::path &parent, const fs::path &configFile, const uint16_t &datID, fs::path filePath);
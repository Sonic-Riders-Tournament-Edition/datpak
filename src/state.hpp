#pragma once

#include <atomic>
#include <cxxopts.hpp>
#include <filesystem>
#include <list>
#include <mutex>

#include "gcaxArchive.hpp"

namespace fs = std::filesystem;

struct ProgramState{
	cxxopts::ParseResult result;

	mutable std::mutex printLock;

	[[nodiscard]] auto verbose() const noexcept{
		return result["verbose"].count();
	}

	[[nodiscard]] auto force() const noexcept{
		return static_cast<bool>(result["force"].count());
	}

	[[nodiscard]] const auto& config() const{
		return result["config"].as<std::vector<fs::path>>();
	}

	[[nodiscard]] const auto& output() const{
		return result["output"].as<fs::path>();
	}
};

extern ProgramState programState; // NOLINT(*-avoid-non-const-global-variables)

struct ConfigState{
	std::list<DatPak::GCAXArchive> archives;

	std::atomic<uint_fast8_t> errors = 0;
	[[maybe_unused]] std::atomic<uint_fast8_t> warnings = 0;
	std::atomic<uint_fast8_t> generated = 0;
	std::atomic<uint_fast8_t> skipped = 0;
};
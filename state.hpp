#pragma once

#include <filesystem>
#include <list>
#include <cxxopts.hpp>
#include <mutex>
#include <atomic>
#include "gcaxArchive.hpp"

namespace fs = std::filesystem;

struct ProgramState{
	cxxopts::ParseResult result;

	mutable std::mutex printLock;

	[[nodiscard]] inline auto verbose() const noexcept{
		return result["verbose"].count();
	}

	[[nodiscard]] inline auto force() const noexcept{
		return static_cast<bool>(result["force"].count());
	}

	[[nodiscard]] inline const auto& config() const{
		return result["config"].as<std::vector<fs::path>>();
	}

	[[nodiscard]] inline const auto& output() const{
		return result["output"].as<fs::path>();
	}
};

extern ProgramState programState; // NOLINT(*-avoid-non-const-global-variables)

struct ConfigState{
	std::list<DatPak::GCAXArchive> archives = std::list<DatPak::GCAXArchive>();

	std::atomic<uint_fast8_t> errors = 0;
	[[maybe_unused]] std::atomic<uint_fast8_t> warnings = 0;
	std::atomic<uint_fast8_t> generated = 0;
	std::atomic<uint_fast8_t> skipped = 0;
};
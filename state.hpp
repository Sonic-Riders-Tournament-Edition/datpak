#pragma once

#include <filesystem>
#include <list>
#include <cxxopts.hpp>
#include <mutex>
#include <atomic>
#include "gcaxArchive.hpp"

namespace fs = std::filesystem;

struct State{
	std::list<DatPak::GCAXArchive> archives = std::list<DatPak::GCAXArchive>();
	cxxopts::ParseResult result;

	mutable std::mutex printLock;

	std::atomic<uint_fast8_t> errors = 0;
	[[maybe_unused]] std::atomic<uint_fast8_t> warnings = 0;
	std::atomic<uint_fast8_t> generated = 0;
	std::atomic<uint_fast8_t> skipped = 0;

	[[nodiscard]] inline auto verbose() const noexcept{
		return result["verbose"].count();
	}

	[[nodiscard]] inline auto force() const noexcept{
		return static_cast<bool>(result["force"].count());
	}

	[[nodiscard]] inline const auto& config() const{
		return result["config"].as<fs::path>();
	}

	[[nodiscard]] inline const auto& output() const{
		return result["output"].as<fs::path>();
	}
};
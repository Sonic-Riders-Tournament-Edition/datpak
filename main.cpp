#include <fstream>
#include <map>
#include <list>

#include <fmt/core.h>
#include <fmt/color.h>

#include <cxxopts.hpp>

#include <cassert>

#include "main.hpp"

auto archives = std::list<DatPak::GCAXArchive>();

size_t verbose;

int main(int argc, const char *argv[]) {
	try {
		cxxopts::Options options("DatPak", "Creates GCAX sound archives to be used by Sonic Riders");
		options.add_options()
				("c,config", "Config File path", cxxopts::value<fs::path>()->default_value("config.txt"))
				("o,output", "Directory to write to", cxxopts::value<fs::path>()->default_value("Output/"))
				("v,verbose", "Verbose output"); // Implicitly bool
		options.parse_positional({"config", "output", "_"});
		auto result = options.parse(argc, argv);

		verbose = result["verbose"].count();

		fs::path config = result["config"].as<fs::path>();
		fs::path output = result["output"].as<fs::path>();

		fs::path configParent;
		if (fs::is_directory(config)) {
			configParent = config;
			config.append("config.txt");
		} else {
			configParent = config.parent_path();
		}

		if (verbose > 0) {
			fmt::print("Loaded config {}, outputting to {}\n", config.string(), output.string());
		}

		fs::create_directory(output); // Create directory if it doesn't exist

		std::ifstream input(config);

		if (!input) {
			throw std::runtime_error("Main config file stream failed to open");
		}

		while (input.good()) {
			auto peek = input.peek();
			while (peek == '\r' || peek == '\n' || peek == ' ' ||
				   peek == '\t') { // ignore whitespace and empty newlines
				input.ignore();
				peek = input.peek();
			}
			if (peek == '#') {
				input.ignore(std::numeric_limits<std::streamsize>::max(), '\n'); // Go to next line
				if (verbose > 1) {
					fmt::print("Skipping comment, going to next line\n");
				}
				continue; // Skip comments
			}

			fs::path bankConf;
			std::string idStr;
			input >> std::ws >> bankConf >> idStr;

			if (input.fail()) break;
			if (bankConf.is_relative()) bankConf = configParent / bankConf;

			auto id = static_cast<uint16_t>(std::stoi(idStr, nullptr, 0));
			assert(id != 0);

			try {
				if (fs::is_directory(bankConf)) bankConf.append("config.txt");

				fs::path bankDir = bankConf.parent_path();

				input.clear();

				std::string outputFilename = bankDir.filename().string();
				int ret = input.peek();
				if (ret != '\n' && ret != '\r' && ret != ' ') {
					input >> outputFilename;
				}

				processVoiceFiles(bankDir, bankConf, id, outputFilename);
			} catch (std::exception &err) {
				fmt::print(fg(fmt::color::crimson) | fmt::emphasis::bold, "{}\n", err.what());
			}

			input.ignore(std::numeric_limits<std::streamsize>::max(), '\n'); // Go to next line
		}

		for (auto &archive: archives) {
			archive.WriteFile(output);
		}
	} catch (cxxopts::OptionException &err) {
		fmt::print(fg(fmt::color::crimson) | fmt::emphasis::bold, "{}\n", err.what());
		return 1;
	} catch (fs::filesystem_error &err) {
		fmt::print(fg(fmt::color::crimson) | fmt::emphasis::bold, "{}\n", err.what());
		return 2;
	} catch (std::exception &err) {
		fmt::print(fg(fmt::color::crimson) | fmt::emphasis::bold, "{}\n", err.what());
		return -1;
	}
}

void
processVoiceFiles(const fs::path &parent, const fs::path &configFile, const uint16_t &id, const std::string &fileName) {
	std::ifstream config = std::ifstream(configFile);

	auto fileMap = std::make_unique < std::map < uint8_t, fs::path>>();
	std::map <uint8_t, fs::path> &files = *fileMap;

	while (config.good()) {
		auto peek = config.peek();
		while (peek == '\r' || peek == '\n' || peek == ' ' || peek == '\t') { // ignore whitespace and empty newlines
			config.ignore();
			peek = config.peek();
		}
		if (peek == '#') {
			config.ignore(std::numeric_limits<std::streamsize>::max(), '\n'); // Go to next line
			continue; // Skip comments
		}

		std::string indexStr, soundPath;
		config >> std::ws >> indexStr >> std::ws;
		std::getline(config, soundPath);
		if (soundPath[soundPath.size() - 1] == '\r') {
			soundPath.erase(
					soundPath.length() - 1); // Remove carriage return from path if reading a Windows file on linux
		}


		if (!config) break;

		uint8_t index = std::stoi(indexStr, nullptr, 0);
		fs::path sound = parent / soundPath;
		if (!fs::exists(sound)) {
			fmt::print(fg(fmt::color::crimson) | fmt::emphasis::bold, "{} isn't a valid file, skipping\n",
					   sound.string());
			continue;
		}

		if (files.count(index)) {
			fmt::print(fg(fmt::color::crimson) | fmt::emphasis::bold,
					   "Warning: ID '0x{:2X}' is replacing '{}' with '{}'\n", +index, files[index].string(), soundPath);
		}

		files[index] = sound; // Will overwrite
		//files.insert({index, sound}); // Doesn't overwrite
	}

	archives.emplace_back(id, fileName, std::move(fileMap));

	if (verbose > 0) {
		fmt::print("Files from '{}': \n", configFile.string());
		for (auto iter = files.begin(); iter != files.end(); iter++) {
			fmt::print("\t0x{:02X}: {}\n", +iter->first, iter->second.string());
		}
	}
}
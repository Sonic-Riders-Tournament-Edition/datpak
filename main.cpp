#include <fstream>
#include <map>
#include <list>

#include <fmt/core.h>
#include <fmt/color.h>
#include <fmt/os.h>

#include <cxxopts.hpp>

//#include <cassert>

#include "main.hpp"

auto archives = std::list<DatPak::GCAXArchive>();

auto datFileInfo = std::vector<HeaderEntry>();

size_t verbose;
bool force;

int main(int argc, const char *argv[]) {
	uint_fast8_t errors = 0, warnings = 0, generated = 0, skipped = 0;
	try {
		cxxopts::Options options("DatPak", "Creates GCAX sound archives to be used by Sonic Riders");
		options.add_options()
				("v,verbose", "Verbose output") // Implicitly bool
				("f,force", "Force generation")
				("c,config", "Config File path", cxxopts::value<fs::path>()->default_value("config.txt"))
				("h,header", "Generated header output path (Not Currently Functional)", cxxopts::value<fs::path>())
				("o,output", "Directory to write to", cxxopts::value<fs::path>()->default_value("Output/"));
		options.parse_positional({"config", "output", "_"});
		auto result = options.parse(argc, argv);

		verbose = result["verbose"].count();
		force = static_cast<bool>(result["force"].count());

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
			//assert(id != 0);

			try {
				if (fs::is_directory(bankConf)) bankConf.append("config.txt");

				if(!fs::exists(bankConf))
					throw std::runtime_error(fmt::format("config file {} does not exist", bankConf.string()));

				fs::path bankDir = bankConf.parent_path();

				input.clear();

				std::string outputFilePath = bankDir.filename().string();
				int ret = input.peek();
				if (ret != '\n' && ret != '\r' && ret != ' ') {
					input >> outputFilePath;
				}

				outputFilePath += ".DAT";

				if(processVoiceFiles(bankDir, bankConf, id, output / outputFilePath)){
					generated++;
				} else {
					skipped++;
				}
			} catch (std::exception &err) {
				fmt::print(fg(fmt::color::crimson) | fmt::emphasis::bold, "{}\n", err.what());
				errors++;
			}

			input.ignore(std::numeric_limits<std::streamsize>::max(), '\n'); // Go to next line
		}

		for (auto &archive: archives) {
			archive.WriteFile();
			if(archive.getWarningCount()){
				warnings++;
				generated--;
			}
		}
		makeHeader(result["header"].as<fs::path>());
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
	if(errors){
		fmt::print(fg(fmt::color::crimson) | fmt::emphasis::bold, "\nFailed to generate {} files\n", errors);
	}
	if(warnings){
		fmt::print(fg(fmt::color::yellow) | fmt::emphasis::bold, "\nGenerated {} files with issues\n", warnings);
	}
	if(verbose > 0){
		if(skipped){
			fmt::print(fg(fmt::color::green), "{} files were unmodified\n", skipped);
		}
		if(generated){
			fmt::print(fg(fmt::color::green), "Successfully generated {} files\n", generated);
		}
	}
}

bool processVoiceFiles(const fs::path &parent, const fs::path &configFile, const uint16_t &id, fs::path filePath) {
	std::error_code ec;
	fs::file_time_type fileTime = fs::last_write_time(filePath, ec);
	bool modified = force;
	if(ec){
		modified = true;
	}
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
		if(config.eof()) break;

		std::string indexStr, soundPath;
		config >> std::ws >> indexStr >> std::ws;
		std::getline(config, soundPath);

		if (!config) break;

		if (soundPath[soundPath.size() - 1] == '\r') {
			soundPath.erase(
					soundPath.length() - 1); // Remove carriage return from path if reading a Windows file on linux
		}

		uint8_t index = std::stoi(indexStr, nullptr, 0);
		fs::path sound = parent / soundPath;
		if (!fs::exists(sound)) {
			fmt::print(fg(fmt::color::crimson) | fmt::emphasis::bold, "{} isn't a valid file, skipping\n",
					   sound.string());
			continue;
		}

		if (files.count(index)) {
			fmt::print(fg(fmt::color::crimson) | fmt::emphasis::bold,
					   "Warning: ID '0x{:02X}' is replacing '{}' with '{}'\n", +index, files[index].string(), soundPath);
		}

		if(fs::last_write_time(sound) > fileTime){
			modified = true;
		}

		files[index] = sound; // Will overwrite
		//files.insert({index, sound}); // Doesn't overwrite
	}

	if(!modified) return false;

	if (verbose > 0) {
		fmt::print("Files from '{}': \n", configFile.string());
		for (auto iter = files.begin(); iter != files.end(); iter++) {
			fmt::print("\t0x{0:02X} ({0:}): {1:}\n", +iter->first, iter->second.filename().string());
		}
	}

	archives.emplace_back(id, std::move(filePath), std::move(fileMap));

	return true;
}

void makeHeader(const fs::path &headerPath) {
	if (!exists(headerPath))
		throw std::runtime_error(fmt::format("header path {} does not exist", headerPath.string()));

	auto header = fmt::output_file(headerPath.string());

	header.print("struct DATFile {}[TotalCharacterAmount] = {{\n", headerPath.stem().string());
	for(auto& entry : datFileInfo){
		header.print("\t{{\"{}\", 0x{:X}, 0x{:X}, 0x0}},\n", entry.name, entry.spec1, entry.spec2);
	}
	header.print("}};\n");
}
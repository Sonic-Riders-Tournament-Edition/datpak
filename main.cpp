#include <iostream>
#include <fstream>
#include <map>
#include <algorithm>
#include <list>

#include <cassert>

#include "main.hpp"

auto archives = std::list<DatPak::GCAXArchive>();

int main(int argc, const char *argv[]) {
	fs::path mainConf;
	std::vector<std::string> args;
	for (int i = 0; i < argc; i++) {
		args.emplace_back(argv[i]);
	}
	getMainConfig(mainConf, args);

	fs::path outputDir("Output/");
	fs::create_directory(outputDir); // Create directory if it doesn't exist

	std::ifstream input(mainConf);

	std::cout << std::hex << std::uppercase;

	while (input.good()) {
		auto peek = input.peek();
		while (peek == '\r' || peek == '\n' || peek == ' ' || peek == '\t'){ // ignore whitespace and empty newlines
			input.ignore();
			peek = input.peek();
		}
		if (peek == '#'){
			input.ignore(std::numeric_limits<std::streamsize>::max(), '\n'); // Go to next line
			continue; // Skip comments
		}

		std::string folderName, idStr;
		input >> std::ws >> folderName >> idStr;

		if (input.fail()) break;

		auto id = static_cast<uint16_t>(std::stoi(idStr, nullptr, 0));
		assert(id != 0);

		try{
			fs::path folderConf = getFolderConfig(folderName);

			fs::path parent = folderConf.parent_path();

			input.clear();

			std::string outputFilename = parent.filename().string();
			int ret = input.peek();
			if (ret != '\n' && ret != '\r' && ret != ' ') {
				input >> outputFilename;
				if (input.good()) {

				}
			}

			processVoiceFiles(parent, folderConf, id, outputFilename);
		} catch (std::exception &err){
			std::cerr << err.what() << std::endl;
		}

		input.ignore(std::numeric_limits<std::streamsize>::max(), '\n'); // Go to next line
	}

	archives.front().CompareFile("/home/lil-g/workspace/sonicriderste/data/files/10METAL.dat");

	for (auto &archive : archives) {
		archive.WriteFile();
	}
}

void getMainConfig(fs::path &path, const std::vector<std::string> &args) {
	if (args.size() > 1) {
		fs::path argPath(args[1]);
		switch (fs::status(argPath).type()) {
			case fs::file_type::regular:
				path = argPath;
				return;
			case fs::file_type::directory:
				fs::current_path(argPath);
				[[fallthrough]];
			default:
				break;
		}
	}
	path = fs::path("config.txt");
}

fs::path getFolderConfig(const std::string &folderName) {
	fs::path folder = fs::path(folderName);
	switch (fs::status(folder).type()) {
		case fs::file_type::directory:
			folder.append("config.txt");
			if (fs::status(folder).type() == fs::file_type::regular) break;
			[[fallthrough]];
		case fs::file_type::not_found:
			throw fs::filesystem_error("Unable to get folder config file", folder, std::make_error_code(std::errc::no_such_file_or_directory));
		default:
			break;
	}
	return std::move(folder);
}

void processVoiceFiles(const fs::path &parent, const fs::path &configFile, const uint16_t &id, const std::string &fileName) {
	std::ifstream config = std::ifstream(configFile);

	auto fileMap = std::make_unique<std::map<uint8_t, fs::path>>();
	std::map<uint8_t, fs::path> &files = *fileMap;

	while (config.good()) {
		auto peek = config.peek();
		while (peek == '\r' || peek == '\n' || peek == ' ' || peek == '\t'){ // ignore whitespace and empty newlines
			config.ignore();
			peek = config.peek();
		}
		if (peek == '#'){
			config.ignore(std::numeric_limits<std::streamsize>::max(), '\n'); // Go to next line
			continue; // Skip comments
		}

		std::string indexStr, soundPath;
		config >> std::ws >> indexStr >> std::ws;
		std::getline(config, soundPath);
		if(soundPath[soundPath.size()-1] == '\r'){
			soundPath.erase(soundPath.length()-1); // Remove carriage return from path if reading a windows file on linux
		}

		//config.ignore(std::numeric_limits<std::streamsize>::max(), '\n'); // Go to next line

		if (!config) break;

		uint8_t index = std::stoi(indexStr, nullptr, 0);
		fs::path sound = parent / soundPath;
		if (!fs::exists(sound)) {
			std::cerr << sound << " isn't a valid file, skipping" << std::endl;
			continue;
		}

		if (files.count(index)) {
			std::cerr << "Warning: ID '0x" << +index << "' is replacing '" << files[index] << "' with '"
					  << soundPath << "'" << std::endl;
		}

		files[index] = sound; // Will overwrite
		//files.insert({index, sound}); // Doesn't overwrite
	}

	archives.emplace_back(id, fileName, std::move(fileMap));

#ifndef NDEBUG
	std::cout << "Files from '" << configFile.string() << "': " << std::endl;
	for (auto iter = files.begin(); iter != files.end(); iter++) {
		std::cout << "\t0x" << +iter->first << ": " << iter->second << std::endl;
	}
#endif
}
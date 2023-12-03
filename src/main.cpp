#include <cxxopts.hpp>
#include <fstream>
#include <list>
#include <map>
#include <thread>
#include <fmt/color.h>
#include <fmt/core.h>

#include "main.hpp"
#include "state.hpp"

ProgramState programState; // NOLINT(*-avoid-non-const-global-variables)

int main(const int argc, const char *argv[]){
	using std::chrono::high_resolution_clock;
	using std::chrono::duration_cast;
	using std::chrono::duration;
	using std::chrono::milliseconds;

	const auto start_time = high_resolution_clock::now();
	const auto returnValue = processInput({argv, static_cast<size_t>(argc)});
	const auto end_time = high_resolution_clock::now();

	if(returnValue == return_code::Ok && programState.verbose() != 0u) {
		/* Getting number of milliseconds as a double. */
		const duration<double, std::milli> ms_double = end_time - start_time;

		fmt::print("\n{}ms\n", ms_double.count());
	}
	return std::to_underlying(returnValue);
}

return_code processInput(std::span<const char*> args) noexcept{
	auto &[result, printLock] = programState;
	std::atomic<uint_fast8_t> errors = 0;
	std::atomic<uint_fast8_t> warnings = 0;
	std::atomic<uint_fast8_t> generated = 0;
	std::atomic<uint_fast8_t> skipped = 0;
	try{
		cxxopts::Options options("DatPak", "Creates GCAX sound archives to be used by Sonic Riders");
		options.add_options()
						("h,help", "Show help.")
						("v,verbose", "Verbose output.") // Implicitly bool
						("f,force", "Force generation.")
						("c,config", "Config File path.", cxxopts::value<std::vector<fs::path>>())
						("o,output", "Directory to write to.", cxxopts::value<fs::path>()->default_value("Output/"));
		options.parse_positional({"config"});
		result = options.parse(static_cast<int>(args.size()), args.data());
		if(result.count("help") != 0 || result.count("config") == 0) {
			const std::scoped_lock writeLock{printLock};
			fmt::print("{}", options.help());
			return return_code::HelpShown;
		}

		// ReSharper disable once CppLocalVariableMayBeConst
		std::vector<fs::path> configs = programState.config();
		const fs::path &output = programState.output();

		fs::create_directory(output); // Create output directory if it doesn't exist

		std::vector<std::jthread> threads;

		for(auto &config : configs){
			threads.emplace_back([&]{
				ConfigState configState;
				fs::path configParent;
				if(fs::is_directory(config)){
					configParent = config;
					config.append("config.txt");
				}else{
					configParent = config.parent_path();
				}

				if(programState.verbose() > 0){
					const std::scoped_lock writeLock{printLock};
					fmt::print("Loaded config {}, outputting to {}\n", config.string(), output.string());
				}

				processMainConfigFile(configState, config, configParent);

				for(const auto &archive: configState.archives){
					archive.WriteFile(config);
					if(archive.getWarningCount() != 0u){
						++configState.warnings;
						--configState.generated;
					}
				}
				errors += configState.errors;
				warnings += configState.warnings;
				generated += configState.generated;
				skipped += configState.skipped;
			});
		}
	}catch(cxxopts::exceptions::exception &err){
		const std::scoped_lock writeLock{printLock};
		fmt::print(errorColors, "{}\n", err.what());
		return return_code::CxxoptException;
	}catch(fs::filesystem_error &err){
		const std::scoped_lock writeLock{printLock};
		fmt::print(errorColors, "{}\n", err.what());
		return return_code::FilesystemException;
	}catch(std::exception &err){
		const std::scoped_lock writeLock{printLock};
		fmt::print(errorColors, "{}\n", err.what());
		return return_code::GeneralException;
	}
	const std::scoped_lock writeLock{printLock};
	if(errors != 0u){
		fmt::print(errorColors, "\nFailed to generate {} files\n", errors.load());
	}
	if(warnings != 0u){
		fmt::print(warningColors, "\nGenerated {} files with issues\n", warnings.load());
	}
	if(programState.verbose() > 0){
		if(skipped != 0u){
			fmt::print(okColors, "{} files were unmodified\n", skipped.load());
		}
		if(generated != 0u){
			fmt::print(okColors, "Successfully generated {} files\n", generated.load());
		}
	}
	return return_code::Ok;
}

void processMainConfigFile(ConfigState &state, const fs::path &config, const fs::path &configParent){
	std::ifstream mainConfigFile(config);

	if(!mainConfigFile){
		throw std::runtime_error("Main config file stream failed to open");
	}

	std::vector<std::jthread> threads;

	while(mainConfigFile.good()){
		auto peek = mainConfigFile.peek();
		while(peek == '\r' || peek == '\n' || peek == ' ' || peek == '\t'){ // ignore space and empty newlines
			mainConfigFile.ignore();
			peek = mainConfigFile.peek();
		}
		if(peek == '#'){
			mainConfigFile.ignore(std::numeric_limits<std::streamsize>::max(), '\n'); // Go to the next line
			if(programState.verbose() > 1){
				const std::scoped_lock writeLock{programState.printLock};
				fmt::print("Skipping comment, going to next line\n");
			}
			continue; // Skip comments
		}

		fs::path bankConf;
		std::string idStr;
		mainConfigFile >> std::ws >> bankConf >> idStr;

		if(mainConfigFile.fail()){
			break;
		}
		if(bankConf.is_relative()){
			bankConf = configParent / bankConf;
		}

		auto datID = static_cast<uint16_t>(std::stoi(idStr, nullptr, 0));
		// assert(id != 0);

		try{
			if(fs::is_directory(bankConf)){
				bankConf.append("config.txt");
			}

			if(!fs::exists(bankConf)){
				throw std::runtime_error(fmt::format("config file {} does not exist", bankConf.string()));
			}

			const auto bankDir = bankConf.parent_path();

			mainConfigFile.clear();

			std::string outputFilePath = bankDir.filename().string();
			const auto ret = mainConfigFile.peek();
			if(ret != '\n' && ret != '\r' && ret != ' '){
				mainConfigFile >> outputFilePath;
			}

			outputFilePath += ".DAT";

			threads.emplace_back(processVoiceFiles, std::ref(state), bankDir, bankConf, datID, programState.output() / outputFilePath);
		}catch(std::exception &err){
			const std::scoped_lock writeLock{programState.printLock};
			fmt::print(errorColors, "{}\n", err.what());
			++state.errors;
		}

		mainConfigFile.ignore(std::numeric_limits<std::streamsize>::max(), '\n'); // Go to the next line
	}

	// threads destructor calls jthread destructor which joins so no manual joining needed
}

void processVoiceFiles(ConfigState &state, const fs::path &parent, const fs::path &configFile, const uint16_t &datID, fs::path filePath){
	std::error_code errorCode;
	const fs::file_time_type fileTime = fs::last_write_time(filePath, errorCode);
	bool modified = programState.force();
	if(errorCode){
		modified = true;
	}
	auto config = std::ifstream(configFile);

	std::map<uint8_t, fs::path> files;
	bool issueOccurred = false;

	while(config.good()){
		auto peek = config.peek();
		if(peek == '\r' || peek == '\n' || peek == ' ' || peek == '\t'){ // ignore space and empty newlines
			config.ignore();
			continue;
		}
		if(peek == '#'){
			config.ignore(std::numeric_limits<std::streamsize>::max(), '\n'); // Go to the next line
			continue; // Skip comments
		}

		std::string indexStr;
		std::string soundPathStr;
		config >> std::ws >> indexStr >> std::ws;
		std::getline(config, soundPathStr);

		if(!config){
			break;
		}

		if(soundPathStr[soundPathStr.size() - 1] == '\r'){
			soundPathStr.erase(soundPathStr.length() - 1); // Remove a carriage return from the path if reading a Windows file on linux
		}
		soundPathStr = soundPathStr.substr(0, soundPathStr.find('#'));

		const uint8_t index = std::stoi(indexStr, nullptr, 0);
		const fs::path sound = parent / soundPathStr;
		if(!fs::exists(sound)){
			const std::scoped_lock writeLock{programState.printLock};
			fmt::print(errorColors, "{} isn't a valid file, skipping\n", sound.string());
			issueOccurred = true;
			modified = true;
			continue;
		}
		if(files.contains(index)){
			const std::scoped_lock writeLock{programState.printLock};
			fmt::print(errorColors, "Warning: ID '0x{:02X}' is replacing '{}' with '{}'\n",
			           +index, files[index].string(), soundPathStr);
		}

		if(fs::last_write_time(sound) > fileTime){
			modified = true;
		}

		files[index] = sound; // Will overwrite
		// files.insert({index, sound}); // Doesn't overwrite
	}

	if(!modified){
		++state.skipped;
		return;
	}

	if(programState.verbose() > 0){
		const std::scoped_lock writeLock{programState.printLock};
		fmt::print("Files from '{}': \n", configFile.string());
		for(auto &file: files){
			fmt::print("\t0x{0:02X} ({0:}): {1:}\n", +file.first, file.second.filename().string());
		}
	}

	auto &archive = state.archives.emplace_back(datID, std::move(filePath), std::move(files), programState.printLock);
	if(issueOccurred){
		archive.incrementWarning();
	}

	++state.generated;
}

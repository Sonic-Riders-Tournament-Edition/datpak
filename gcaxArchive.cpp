#include <istream>
#include <fstream>
#include <memory>
#include <utility>
#include <vector>
#include <span>
#include <spanstream>
#include <limits>

#include <fmt/color.h>
#include <fmt/core.h>

#include "gcaxArchive.hpp"
#include "include/dsptool.h"
#include "state.hpp"

namespace DatPak {

#include "data/EmptySound.inc"

#include "data/templateDataHeader.inc"

#include "data/templateDataStruct.inc"

#include "data/templateMainBody.inc"

} // namespace DatPak

void DatPak::GCAXArchive::WriteFile(const State &state, const fs::path &config) const{
	if(Warnings != 0u){
		const std::scoped_lock writeLock{state.printLock};
		fmt::print(warningColors,
		           "Writing file with issues: {}\n\t0x{:X}\t0x{:X}\n",
		           fs::absolute(FilePath).string(), spec1, spec2
		);
	}else if(state.verbose() >= 1){
		const std::scoped_lock writeLock{state.printLock};
		fmt::print("Writing file: {}\n\t0x{:X}, 0x{:X}\n", fs::absolute(FilePath).string(), spec1, spec2);
	}
	std::basic_ofstream<std::byte> out(FilePath, std::ios_base::binary | std::ios_base::out);
	const std::span datBytes = as_bytes(std::span(Dat));
	out.write(datBytes.data(), static_cast<std::streamsize>(datBytes.size()));
	out.close(); // Make sure this is closed for Windows systems
	if(Warnings != 0u){
		// Clear the modified time if there were any issues, so it will always be regenerated
		// const auto &emptyTime = fs::file_time_type::min();
		const auto &emptyTime = fs::last_write_time(config)--; // For some reason, windows hates empty dates???
		fs::last_write_time(FilePath, emptyTime);
	}
}

const uint_fast8_t &DatPak::GCAXArchive::getWarningCount() const{
	return Warnings;
}

[[maybe_unused]] void DatPak::GCAXArchive::CompareFile(std::mutex &printLock, const fs::path &file) const{
	if(fs::status(file).type() != fs::file_type::regular){
		const std::scoped_lock writeLock{printLock};
		fmt::print(errorColors, "{} does not exist\n", file.string());
		return;
	}

	std::basic_ifstream<uint8_t> compareFile(file);
	decltype(Dat) compare;
	auto fileSize = fs::file_size(file);
	compare.resize(fileSize);
	compareFile.read(
			compare.data(),
			static_cast<std::streamsize>(fileSize)
	);

	size_t differences = 0;
	// const int addrWidth = 8, valWidth = 10;
	for(size_t i = 0; i < fileSize; i++){
		unsigned short val1 = std::numeric_limits<unsigned short>::max();
		unsigned short val2 = std::numeric_limits<unsigned short>::max();
		if(Dat.size() > i){
			val1 = Dat[i];
		}
		if(compare.size() > i){
			val2 = compare[i];
		}

		if(val1 == val2){
			continue;
		}
		const std::scoped_lock writeLock{printLock};
		if(differences == 0){
			fmt::print("{:^7}|{:^7}|{:^7}\n", "Address", "Created", "Compare");
		}

		fmt::print("{:^7X}|{:^7X}|{:^7X}\n", i, val1, val2);
		differences++;
	}
	const std::scoped_lock writeLock{printLock};
	if(differences == 0){
		fmt::print("No differences detected.\n");
	}else{
		fmt::print("{} differences detected.\n", differences);
	}
}

template<>
void DatPak::PushBytes<std::string>(std::vector<uint8_t> &vector, const std::string &val){
	vector.insert(vector.end(), val.begin(), val.end());
}

bool DatPak::verifyWavFormat(std::mutex &printLock, const fs::path &wavFilePath, std::ifstream &wavFile){
	wavFile.seekg(0);
	std::array<char, 4> buf{};
	const std::string_view bufStr(buf.data(), 4);
	wavFile.read(buf.data(), 4);
	if(bufStr.compare(0, 4, "RIFF") != 0){
		const std::scoped_lock writeLock{printLock};
		fmt::print(errorColors,
		           "Invalid WAV file: {}. Needs to be encoded in the RIFF format. Replacing with empty file.\n",
		           wavFilePath.string());
		return false;
	}

	wavFile.seekg(0x8);

	wavFile.read(buf.data(), 4);
	if(bufStr.compare(0, 4, "WAVE") != 0){
		const std::scoped_lock writeLock{printLock};
		fmt::print(errorColors,
		           "Invalid WAV file: {}. This is not a .wav file. Replacing with empty file.\n",
		           wavFilePath.string());
		return false;
	}

	wavFile.seekg(0x14);

	uint16_t format{};
	wavFile.read(reinterpret_cast<char *>(&format), 2);
	if(format != 1){
		const std::scoped_lock writeLock{printLock};
		fmt::print(errorColors,
		           "Invalid WAV file: {}. This is not formatted using PCM. Replacing with empty file.\n",
		           wavFilePath.string());
		return false;
	}

	wavFile.read(reinterpret_cast<char *>(&format), 2);
	if(format != 1){
		const std::scoped_lock writeLock{printLock};
		fmt::print(errorColors,
		           "Invalid WAV file: {}. This is not formatted as Mono. Replacing with empty file.\n",
		           wavFilePath.string());
		return false;
	}

	return true;
}

DatPak::GCAXArchive::GCAXArchive(
		std::mutex &printLock, const uint16_t &datID, fs::path &&filePath,
		std::unique_ptr<std::map<uint8_t, fs::path>> &&files
) : ID(datID), FilePath(std::move(filePath)), Files(std::move(files)),
    Warnings(0){
	if(Files->empty()){
		throw std::invalid_argument(fmt::format("List of files for archive \"{}\" was empty", FilePath.string()));
	}

	// First, we copy the data template over
	std::vector<uint8_t> template_main_body(templateMainBody.begin(), templateMainBody.end());
	const uint8_t file_count = Files->size();
	const uint8_t delta_file_count = file_count - 1;

	// Next, we add this archive's ID and index of the last file, plus some magic numbers
	PushBytes(template_main_body, swap_to_big_endian(ID));
	PushBytes(template_main_body, swap_to_big_endian<uint16_t>(0x08));
	PushBytes(template_main_body, swap_to_big_endian(delta_file_count));
	PushBytes(template_main_body, std::array<uint8_t, 3>{0});

	// Now add the offsets for each entry in the file table
	// todo: comment this better
	for(unsigned int i = 0, sndfile_table_offset = (file_count * 4u) + 0xCu;
	    i < file_count; i++, sndfile_table_offset += 6u){
		PushBytes(template_main_body, swap_to_big_endian(sndfile_table_offset));
	}

	// Add more magic numbers and the index for each file?
	for(uint8_t i = 0; i < file_count; i++){
		PushBytes(template_main_body, swap_to_big_endian<uint16_t>(0xC0DF));
		template_main_body.push_back(i);
		PushBytes(template_main_body, swap_to_big_endian<uint16_t>(0x7F80));
		template_main_body.push_back(0xFF);
	}

	// Now we align our data to a 4-bit boundary
	alignContainer<4>(template_main_body);

	// Copy over the audio header template data and assign the correct last file index
	std::vector<uint8_t> audio_info_data(templateDataHeader.begin(), templateDataHeader.end());
	audio_info_data[0x11] = delta_file_count;
	// Now copy over the audio info data and assign the index for each file?
	for(uint8_t i = 0; i < file_count; i++){
		std::vector<uint8_t> audio_info_struct(templateDataStruct.begin(), templateDataStruct.end());
		audio_info_struct[0x0] = i;
		audio_info_struct[0x3] = i;
		audio_info_data.insert(audio_info_data.end(), audio_info_struct.begin(), audio_info_struct.end());
	}

	// Swap the endian and add the index of the last file
	std::vector<uint8_t> file_entry_data;
	PushBytes(file_entry_data, swap_to_big_endian<uint32_t>(delta_file_count));

	// Add more magic numbers
	std::vector<uint8_t> audio_data;
	PushBytes(audio_data, std::string("gcaxPCMD"));
	PushBytes(audio_data, swap_to_big_endian<uint32_t>(0x024a0100));
	audio_data.insert(audio_data.end(), 20, '\0');

	// Go to the last file in our (sorted) map and get the last ID that's specified
	const int maxId = std::prev(Files->end())->first;
	auto iter = Files->begin();
	for(int i = 0; i <= maxId; i++){
		const fs::path &wavFilePath = iter->second;
		std::unique_ptr<std::basic_istream<char>> wavFile = std::make_unique<std::ifstream>(wavFilePath, std::ios_base::in | std::ios_base::binary);

		{
			const bool emptyFile = Files->count(i) == 0;

			// Check and make sure this wav file is valid
			if(emptyFile){
				const std::scoped_lock writeLock{printLock};
				fmt::print(warningColors,
				           "Warning: File for ID '0x{:02X}' is empty, Replacing with empty file\n", i);

			} else {
				// Check and make sure we have a valid entry in the map
				if(iter != Files->end()){
					iter++;
				}
			}
			if(emptyFile || !verifyWavFormat(printLock, wavFilePath, *dynamic_cast<std::ifstream *>(wavFile.get()))){
				// Replace the invalid wav file with an empty one
				const std::span<const char> emptySpan = EmptySound;
				wavFile = std::make_unique<std::ispanstream>(emptySpan);
				Warnings++;
			}
		}

		uint32_t sample_rate = 0;
		uint32_t data_length = 0;

		wavFile->seekg(0x18);

		wavFile->read(reinterpret_cast<char *>(&sample_rate), sizeof(sample_rate));
		if(sample_rate != 44100){
			const std::scoped_lock writeLock{printLock};
			fmt::print(warningColors,
			           "Warning: File for ID '0x{:02X}' has a sample rate of {}. "
			           "Game will play this sound at 44100 Hz leading to pitch issues\n",
			           i, sample_rate);
			Warnings++;
		}
		wavFile->seekg(0x28);
		wavFile->read(reinterpret_cast<char *>(&data_length), sizeof(data_length));

		std::vector<int16_t> inWav; inWav.resize(data_length);
		wavFile->read(reinterpret_cast<char *>(inWav.data()), data_length);

		// Samples are stored as signed 16-bit, so the sample count is half of the available data
		const uint32_t sample_count = data_length / 2;
		const uint32_t adpcm_byte_count = getBytesForAdpcmBuffer(sample_count);

		std::vector<uint8_t> outPCM; outPCM.resize(adpcm_byte_count);
		ADPCMINFO info;

		// Encode our wav data into ADPCM
		encode(inWav.data(), outPCM.data(), &info, sample_count);

		FileEntry fileEntry{
				.start_offset = swap_to_big_endian<uint32_t>(audio_data.size()),
				.unk = swap_to_big_endian(2),
				.shifted_size = swap_to_big_endian((adpcm_byte_count << 1) - 1),
				.coefficient{},
				.unk2 = {0, 0, 0},
				.unk3 = swap_to_big_endian(0x200),
				.sample_rate = swap_to_big_endian<uint16_t>(sample_rate),
				.data_size = swap_to_big_endian(adpcm_byte_count)
		};

		// There's no way to copy an array during initialization, so we have to do it here
		for(size_t x = 0; x < 16; x++){
			fileEntry.coefficient[x] = swap_to_big_endian(info.coef[x]);
		}

		// Add our file entry header data
		PushBytes(file_entry_data, fileEntry);

		// now the audio data itself
		audio_data.insert(audio_data.end(), outPCM.begin(), outPCM.end());

		// Now we align to an 8-bit boundary
		alignContainer<8>(audio_data);

		// Now continue looping
	}

	// This time we align to a 32-bit boundary
	const size_t aligned_length = align<32>(audio_data.size());
	audio_data.resize(aligned_length);

	// Go back and fix the length, also swaps the endian
	replaceIntBytearray(audio_data, 0xC, aligned_length);

	// Get the end of our headers and align that to a 32-bit boundary
	const size_t end_of_info = align<32>(template_main_body.size() + audio_info_data.size() + file_entry_data.size());

	const size_t eoi_msb = findMsbPosition(end_of_info);

	const size_t audio_data_start_offset = 1 << (eoi_msb + 1);

	// align the size of the full file to 256-bits
	const size_t full_file_length = align<256>(audio_data_start_offset + audio_data.size());

	// Now we go back and fix a couple of things
	replaceIntBytearray(template_main_body, 0xC, full_file_length);

	// Making sure we save this info for later when we use this archive
	spec1 = end_of_info + 0x20;
	spec2 = audio_data.size() + 0x20;
	replaceIntBytearray(template_main_body, 0x10, spec1);
	replaceIntBytearray(template_main_body, 0x18, spec2);

	replaceIntBytearray(template_main_body, 0x1C, audio_data_start_offset);
	replaceIntBytearray(template_main_body, 0xA8, template_main_body.size());
	replaceIntBytearray(template_main_body, 0xB8, template_main_body.size() + audio_info_data.size());
	replaceIntBytearray(template_main_body, 0xBC, end_of_info);

	// Since we have an idea what the size should be, go ahead and reserve it
	Dat.reserve(full_file_length);

	Dat.insert(Dat.end(), template_main_body.begin(), template_main_body.end());
	Dat.insert(Dat.end(), audio_info_data.begin(), audio_info_data.end());
	Dat.insert(Dat.end(), file_entry_data.begin(), file_entry_data.end());

	// Align to the start offset
	Dat.resize(audio_data_start_offset);
	Dat.insert(Dat.end(), audio_data.begin(), audio_data.end());

	// Align to the final length
	Dat.resize(full_file_length);
}

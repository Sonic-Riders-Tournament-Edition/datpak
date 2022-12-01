#include <memory>
#include <utility>
#include <vector>
#include <fstream>

#include <fmt/core.h>
#include <fmt/color.h>

#include "gcaxArchive.hpp"
#include "include/dsptool.h"

namespace DatPak {

#include "data/templateDataHeader.inc"
#include "data/templateDataStruct.inc"
#include "data/templateMainBody.inc"
#include "data/EmptySound.inc"

} // DatPak

DatPak::GCAXArchive::GCAXArchive(
		const uint16_t &id,
		std::string fileName,
		std::unique_ptr<std::map<uint8_t, fs::path>> &&files)
		: ID(id),
		  Files(std::move(files)),
		  FileName(std::move(fileName).append(".dat")) {
	if (Files->empty()) throw std::invalid_argument("List of files for archive was empty");

	// First we copy the data template over
	std::vector<uint8_t> template_main_body(templateMainBody.begin(), templateMainBody.end());
	uint8_t file_count = Files->size();
	uint8_t delta_file_count = file_count - 1;

	// Next we add this archives ID and index of the last file, plus some magic numbers
	PushBytes(template_main_body, swap_endian(ID));
	PushBytes(template_main_body, swap_endian<uint16_t>(0x08));
	PushBytes(template_main_body, swap_endian(delta_file_count));
	PushBytes(template_main_body, std::array<uint8_t, 3>{0});

	// Now add the offsets for each entry in the file table
	//todo: comment this better
	for (unsigned int i = 0, sndfile_table_offset = (file_count * 4) + 0xC;
		 i < file_count;
		 i++, sndfile_table_offset += 6) {
		PushBytes(template_main_body, swap_endian(sndfile_table_offset));
	}

	// Add more magic numbers and the index for each file?
	for (uint8_t i = 0; i < file_count; i++) {
		PushBytes(template_main_body, swap_endian<uint16_t>(0xC0DF));
		template_main_body.push_back(i);
		PushBytes(template_main_body, swap_endian<uint16_t>(0x7F80));
		template_main_body.push_back(0xFF);
	}

	// Now we align our data to a 4-bit boundry
	alignContainer<4>(template_main_body);

	// Copy over the audio header template data and assign the correct last file index
	std::vector<uint8_t> audio_info_data(templateDataHeader.begin(), templateDataHeader.end());
	audio_info_data[0x11] = delta_file_count;
	// Now copy over the audio info data and assign the index for each file?
	for (uint8_t i = 0; i < file_count; i++) {
		std::vector<uint8_t> audio_info_struct(templateDataStruct.begin(), templateDataStruct.end());
		audio_info_struct[0x0] = i;
		audio_info_struct[0x3] = i;
		audio_info_data.insert(audio_info_data.end(), audio_info_struct.begin(), audio_info_struct.end());
	}

	// Swap the endian and add the index of the last file
	std::vector<uint8_t> file_entry_data;
	PushBytes(file_entry_data, swap_endian<uint32_t>(delta_file_count));

	// Add more magic numbers
	std::vector<uint8_t> audio_data;
	PushBytes(audio_data, std::string("gcaxPCMD"));
	PushBytes(audio_data, swap_endian<uint32_t>(0x024a0100));
	audio_data.insert(audio_data.end(), 20, '\0');

	// Go to the last file in our (sorted) map and get the last ID that's specified
	int maxId = std::prev(Files->end())->first;
	auto iter = Files->begin();
	for (int i = 0; i <= maxId; i++) {
		fs::path &wavFilePath = iter->second;
		std::ifstream wavFile(wavFilePath, std::ios_base::in | std::ios_base::binary);

		// These both check and make sure we have a valid entry in the map
		if (Files->count(i) != 0) {
			if (iter != Files->end()) iter++;

			// Check and make sure this wav file is valid, todo: get rid of this goto
			if (!verifyWavFormat(wavFilePath, wavFile)) goto UseEmpty;

			uint32_t sample_rate;
			uint32_t data_length;

			wavFile.read(reinterpret_cast<char *>(&sample_rate), 4);
			wavFile.seekg(0x28);
			wavFile.read(reinterpret_cast<char *>(&data_length), 4);

			// Make this a unique pointer, so it will always free itself when out of scope
			std::unique_ptr<int16_t[]> inwav(new int16_t[data_length]);
			wavFile.read(reinterpret_cast<char *>(inwav.get()), data_length);

			// Samples are stored as signed 16-bit, so the sample count is half of the available data
			uint32_t sample_count = data_length / 2;
			uint32_t adpcm_byte_count = getBytesForAdpcmBuffer(sample_count);

			std::unique_ptr<uint8_t[]> outpcm(new uint8_t[adpcm_byte_count]);
			ADPCMINFO info;

			// Encode our wav data into ADPCM
			encode(inwav.get(), outpcm.get(), &info, sample_count);

			FileEntry fileentry{
					.start_offset = swap_endian<uint32_t>(audio_data.size()),
					.unk = swap_endian(2),
					.shifted_size = swap_endian((adpcm_byte_count << 1) - 1),
					.unk2 = {0, 0, 0},
					.unk3 = swap_endian(0x200),
					.sample_rate = swap_endian<uint16_t>(sample_rate),
					.data_size = swap_endian(adpcm_byte_count)
			};

			// There's no way to copy an array during initialization, so we have to do it here
			for (int x = 0; x < 16; x++) {
				fileentry.coef[x] = swap_endian(info.coef[x]);
			}

			// Add our file entry header data
			PushBytes(file_entry_data, fileentry);

			// now the audio data itself
			audio_data.insert(audio_data.end(), outpcm.get(), outpcm.get() + adpcm_byte_count);

			// Now we align to an 8-bit boundary
			alignContainer<8>(audio_data);

			// Now continue looping
			continue;
		}
		UseEmpty:

		// If we run into an error with the file, or if this entry has no file, use empty data
		int16_t sample_data[] = {1, 0};
		uint32_t sample_count = 2;
		uint32_t adpcm_byte_count = getBytesForAdpcmBuffer(sample_count);
		uint8_t outpcm[adpcm_byte_count];
		ADPCMINFO info;
		encode(sample_data, outpcm, &info, sample_count);
		FileEntry fileentry{
				.start_offset = swap_endian<uint32_t>(audio_data.size()),
				.unk = swap_endian(2),
				.shifted_size = swap_endian((adpcm_byte_count << 1) - 1),
				.unk2 = {0, 0, 0},
				.unk3 = swap_endian(0x200),
				.sample_rate = swap_endian<uint16_t>(44100),
				.data_size = swap_endian(adpcm_byte_count)
		};
		for (int x = 0; x < 16; x++) {
			fileentry.coef[x] = swap_endian(info.coef[x]);
		}

		// Add our file entry header data
		PushBytes(file_entry_data, fileentry);

		// now the empty audio data
		audio_data.insert(audio_data.end(), outpcm, outpcm + adpcm_byte_count);

		// Now we align to an 8-bit boundary
		alignContainer<8>(audio_data);
	}

	// This time we align to a 32-bit boundary
	size_t aligned_length = align<32>(audio_data.size());
	audio_data.resize(aligned_length);

	// Go back and fix the length, also swaps the endian
	replaceIntBytearray(audio_data, 0xC, aligned_length);

	// Get the end of our headers and align that to a 32-bit boundary
	size_t end_of_info = align<32>(template_main_body.size() + audio_info_data.size() + file_entry_data.size());

	size_t eoi_msb = findMsbPosition(end_of_info);

	size_t audio_data_start_offset = 1 << (eoi_msb + 1);

	// align the size of the full file to 256-bits
	size_t full_file_length = align<256>(audio_data_start_offset + audio_data.size());

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

DatPak::GCAXArchive::~GCAXArchive() = default;

void DatPak::GCAXArchive::WriteFile(const fs::path &filePath, bool overRideFileName) {
	auto &path = overRideFileName ? filePath : filePath / FileName;
#ifndef NDEBUG
	fmt::print("Writing file: {}\n", path.string());
#endif
	std::ofstream out(path, std::ios_base::binary | std::ios_base::out);
	out.write(reinterpret_cast<const char *>(Dat.data()), Dat.size());
}

void DatPak::GCAXArchive::CompareFile(const fs::path &file) {
	if (fs::status(file).type() != fs::file_type::regular) {
		fmt::print(fg(fmt::color::crimson) | fmt::emphasis::bold, "{} does not exist\n", file.string());
		return;
	}

	std::ifstream compareFile(file);
	std::vector<uint8_t> compare;
	auto fileSize = fs::file_size(file);
	compare.resize(fileSize);
	compareFile.read(reinterpret_cast<std::ifstream::char_type *>(&compare.front()), fileSize);

	size_t differences = 0;
	//const int addrWidth = 8, valWidth = 10;
	for (size_t i = 0; i < fileSize; i++) {
		unsigned short val1 = -1, val2 = -1;
		if (Dat.size() > i) val1 = Dat[i];
		if (compare.size() > i) val2 = compare[i];

		if (val1 == val2) continue;
		if (!differences) {
			fmt::print("{:^7}|{:^7}|{:^7}\n", "Address", "Created", "Compare");
		}

		fmt::print("{:^7X}|{:^7X}|{:^7X}\n", i, val1, val2);
		differences++;
	}
	if (!differences) {
		fmt::print("No differences detected.\n");
	} else {
		fmt::print("{} differences detected.\n", differences);
	}
}

template<>
void DatPak::PushBytes<std::string>(std::vector<uint8_t> &vector, const std::string &val) {
	vector.insert(vector.end(), val.begin(), val.end());
}

template<>
void DatPak::PushBytes<std::string_view>(std::vector<uint8_t> &vector, const std::string_view &val) {
	vector.insert(vector.end(), val.begin(), val.end());
}

bool DatPak::verifyWavFormat(const fs::path &wavFilePath, std::ifstream &wavFile) {
	wavFile.seekg(0);
	char buf[4];
	std::string_view bufStr(buf, 4);
	wavFile.read(buf, 4);
	if (bufStr.compare(0, 4, "RIFF")) {
		fmt::print(fg(fmt::color::crimson) | fmt::emphasis::bold,
				   "Invalid WAV file: {}. Needs to be encoded in the RIFF format. Replacing with empty file.\n",
				   wavFilePath.string());
		return false;
	}

	wavFile.seekg(0x8);

	wavFile.read(buf, 4);
	if (bufStr.compare(0, 4, "WAVE")) {
		fmt::print(fg(fmt::color::crimson) | fmt::emphasis::bold,
				   "Invalid WAV file: {}. This is not a .wav file. Replacing with empty file.\n",
				   wavFilePath.string());
		return false;
	}

	wavFile.seekg(0x14);

	uint16_t format;
	wavFile.read(reinterpret_cast<char *>(&format), 2);
	if (format != 1) {
		fmt::print(fg(fmt::color::crimson) | fmt::emphasis::bold,
				   "Invalid WAV file: {}. This is not formatted using PCM. Replacing with empty file.\n",
				   wavFilePath.string());
		return false;
	}

	wavFile.read(reinterpret_cast<char *>(&format), 2);
	if (format != 1) {
		fmt::print(fg(fmt::color::crimson) | fmt::emphasis::bold,
				   "Invalid WAV file: {}. This is not formatted as Mono. Replacing with empty file.\n",
				   wavFilePath.string());
		return false;
	}

	return true;
}
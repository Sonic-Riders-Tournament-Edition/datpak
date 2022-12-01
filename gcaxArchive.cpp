#include <memory>
#include <utility>
#include <vector>
#include <fstream>
#include <iostream>

#include "gcaxArchive.hpp"
#include "include/dsptool.h"

namespace DatPak {

#include "data/templateDataHeader.inc"
#include "data/templateDataStruct.inc"
#include "data/templateMainBody.inc"
#include "data/EmptySound.inc"

	GCAXArchive::GCAXArchive(const uint16_t &id, std::string fileName,
							 std::unique_ptr<std::map<uint8_t, fs::path>> &&files) : ID(id),
																					 Files(std::move(files)),
																					 FileName(std::move(fileName).append(".dat")) {
		if(Files->empty()) throw std::invalid_argument("List of files for archive was empty");

		std::vector<uint8_t> template_main_body(templateMainBody.begin(), templateMainBody.end());
		uint8_t file_count = Files->size();
		uint8_t delta_file_count = file_count - 1;

		// template_main_body += struct.pack('>2HB3x', file_identificator, 0x8, delta_file_count)
		PushBytes(template_main_body, swap_endian(ID));
		PushBytes(template_main_body, swap_endian<uint16_t>(0x08));
		PushBytes(template_main_body, swap_endian(delta_file_count));
		PushBytes(template_main_body, std::array<uint8_t, 3>{0});

		//sndfile_table_offset = (file_count * 4) + 0xC
		//for _ in range(file_count):
		//	template_main_body += struct.pack('>I', sndfile_table_offset)
		//	sndfile_table_offset += 6
		for (unsigned int i = 0, sndfile_table_offset = (file_count * 4) + 0xC;
			 i < file_count;
			 i++, sndfile_table_offset += 6) {
			PushBytes(template_main_body, swap_endian(sndfile_table_offset));
		}

		//for f in range(file_count):
		//	template_main_body += struct.pack('>HBHB', 0xC0DF, f, 0x7F80, 0xFF)
		for (uint8_t i = 0; i < file_count; i++) {
			PushBytes(template_main_body, swap_endian<uint16_t>(0xC0DF));
			template_main_body.push_back(i);
			PushBytes(template_main_body, swap_endian<uint16_t>(0x7F80));
			template_main_body.push_back(0xFF);
		}

		//while len(template_main_body) & 0x3:  # 4 bit alignment
		//    template_main_body += b'\x00'
		while (template_main_body.size() & 0x3)
			template_main_body.push_back(0);

		//audio_info_data = template_data_header
		//audio_info_data[0x11] = delta_file_count
		//for f in range(file_count):
		//    audio_info_struct = template_data_struct
		//    audio_info_struct[0x0] = f
		//    audio_info_struct[0x3] = f
		//    audio_info_data += audio_info_struct
		std::vector<uint8_t> audio_info_data(templateDataHeader.begin(), templateDataHeader.end());
		audio_info_data[0x11] = delta_file_count;
		for (uint8_t i = 0; i < file_count; i++) {
			std::vector<uint8_t> audio_info_struct(templateDataStruct.begin(), templateDataStruct.end());
			audio_info_struct[0x0] = i;
			audio_info_struct[0x3] = i;
			audio_info_data.insert(audio_info_data.end(), audio_info_struct.begin(), audio_info_struct.end());
		}

		//file_entry_data = struct.pack('>I', delta_file_count)
		std::vector<uint8_t> file_entry_data;
		PushBytes(file_entry_data, swap_endian<uint32_t>(delta_file_count));

		//audio_data = bytearray(struct.pack('>8sI20x', bytes('gcaxPCMD', 'ascii'), 0x024a0100))
		std::vector<uint8_t> audio_data;
		PushBytes(audio_data, std::string_view("gcaxPCMD"));
		PushBytes(audio_data, swap_endian<uint32_t>(0x024a0100));
		audio_data.insert(audio_data.end(), 0x10, '\0');

		//for wavfilepath in files:
		int maxId = std::prev(Files->end())->first;
		auto iter = Files->begin();
		for (int i = 0; i <= maxId; i++) {
			uint32_t sample_rate;
			uint32_t data_length;
			std::unique_ptr<int16_t[]> wav_data;
			if (Files->count(i) != 0) {
				fs::path &wavFilePath = iter->second;
				std::ifstream wavFile(wavFilePath, std::ios_base::in | std::ios_base::binary);
				if (iter != Files->end()) iter++;

				//if wavfile.read(4).decode('ASCII') != 'RIFF':
				//	raise Exception("Invalid WAV file: {}. Needs to be encoded in the RIFF format.".format(
				//		os.path.basename(fullwavfilepath)))
				{
					char buf[4];
					std::string_view bufStr(buf, 4);
					wavFile.read(buf, 4);
					if (bufStr.compare(0, 4, "RIFF")) {
						std::cerr << "Invalid WAV file: " << wavFilePath
								  << ". Needs to be encoded in the RIFF format. Replacing with empty file."
								  << std::endl;
						goto UseEmpty;
					}
				}
				//wavfile.seek(0x8)
				wavFile.seekg(0x8);

				//if wavfile.read(4).decode('ASCII') != 'WAVE':
				//	raise Exception("Invalid WAV file: {}. This is not a .wav file.".format(os.path.basename(fullwavfilepath)))
				{
					char buf[4];
					std::string_view bufStr(buf, 4);
					wavFile.read(buf, 4);
					if (bufStr.compare(0, 4, "WAVE")) {
						std::cerr << "Invalid WAV file: " << wavFilePath
								  << ". This is not a .wav file. Replacing with empty file." << std::endl;
						goto UseEmpty;
					}
				}

				//wavfile.seek(0x14)
				wavFile.seekg(0x14);

				//if struct.unpack('<H', wavfile.read(2))[0] != 1:  # if not formatted in PCM
				//	raise Exception("WAV file {} is not formatted using PCM.".format(os.path.basename(fullwavfilepath)))
				{
					uint16_t format;
					wavFile.read(reinterpret_cast<char *>(&format), 2);
					if (format != 1) {
						std::cerr << "WAV file " << wavFilePath
								  << " is not formatted using PCM. Replacing with empty file." << std::endl;
						goto UseEmpty;
					}
				}

				//wavfile.seek(0x16) // Automatically incremented

				//if struct.unpack('<H', wavfile.read(2))[0] != 1:  # if not mono channel
				//	raise Exception("WAV file {} is not in mono channel.".format(os.path.basename(fullwavfilepath)))
				{
					uint16_t format;
					wavFile.read(reinterpret_cast<char *>(&format), 2);
					if (format != 1) {
						std::cerr << "WAV file " << wavFilePath << " is not mono. Replacing with empty file."
								  << std::endl;
						goto UseEmpty;
					}
				}

				//sample_rate = struct.unpack('<I', wavfile.read(4))[0]
				wavFile.read(reinterpret_cast<char *>(&sample_rate), 4);

				//wavfile.seek(0x28)
				wavFile.seekg(0x28);

				//data_length = struct.unpack('<I', wavfile.read(4))[0]
				wavFile.read(reinterpret_cast<char *>(&data_length), 4);

				//wav = wavfile.read(data_length)
				//wav_data = [struct.unpack('<h', wav[i:i + 2])[0] for i in range(0, len(wav), 2)]
				wav_data.reset(new int16_t[data_length]);
				wavFile.read(reinterpret_cast<char *>(wav_data.get()), data_length);
				/*
				for(int x = 0; x < (data_length / 2); x++){
					wavFile.read(reinterpret_cast<char*>(&wav_data[x]), 2);
				}
				*/

				//sample_count = len(wav_data)
				//c_sample_count = c_uint32(sample_count)
				uint32_t sample_count = data_length / 2;

				//adpcm_byte_count = dsptooldll.getBytesForAdpcmBuffer(c_sample_count)
				uint32_t adpcm_byte_count = getBytesForAdpcmBuffer(sample_count);

				//outpcm = (c_uint8 * adpcm_byte_count)()
				uint8_t outpcm[adpcm_byte_count];

				//inwav = (c_int16 * len(wav_data))(*wav_data)
				//info = ADPCMINFO()
				ADPCMINFO info;

				//dsptooldll.encode(pointer(inwav), pointer(outpcm), pointer(info), c_sample_count)
				encode(wav_data.get(), outpcm, &info, sample_count);

				//coefs = (c_int16.__ctype_be__ * 16)(*info.coef)
				//fileentry = FileEntry(len(audio_data), 2, (adpcm_byte_count << 1) - 1, coefs, (0, 0, 0), 0x200, sample_rate,
				//                          adpcm_byte_count)
				FileEntry fileentry{
						.start_offset = swap_endian<uint32_t>(audio_data.size()),
						.unk = swap_endian(2),
						.shifted_size = swap_endian((adpcm_byte_count << 1) - 1),
						.unk2 = {0, 0, 0},
						.unk3 = swap_endian<uint16_t>(0x200),
						.sample_rate = swap_endian<uint16_t>(sample_rate),
						.data_size = swap_endian(adpcm_byte_count)
				};
				for (int x = 0; x < 16; x++) {
					fileentry.coef[x] = swap_endian(info.coef[x]);
				}

				//file_entry_data += fileentry
				PushBytes(file_entry_data, fileentry);

				//audio_data += outpcm
				audio_data.insert(audio_data.end(), outpcm, outpcm + adpcm_byte_count);

				//aligned_length = align_8bit(len(audio_data))
				//while len(audio_data) != aligned_length:
				//	audio_data += b'\x00'
				size_t aligned_length = align<8>(audio_data.size());
				while (audio_data.size() < aligned_length) {
					audio_data.push_back(0);
				}
				continue;
			}
			UseEmpty:

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
					.unk3 = swap_endian<uint16_t>(0x200),
					.sample_rate = swap_endian<uint16_t>(44100),
					.data_size = swap_endian(adpcm_byte_count)
			};
			for (int x = 0; x < 16; x++) {
				fileentry.coef[x] = swap_endian(info.coef[x]);
			}
			PushBytes(file_entry_data, fileentry);
			audio_data.insert(audio_data.end(), outpcm, outpcm + adpcm_byte_count);
			size_t aligned_length = align<8>(audio_data.size());
			while (audio_data.size() < aligned_length) {
				audio_data.push_back(0);
			}
		}

		//aligned_length = align_32bit(len(audio_data))
		size_t aligned_length = align<32>(audio_data.size());

		//while len(audio_data) != aligned_length:
		//    audio_data += b'\x00'
		while (audio_data.size() < aligned_length) {
			audio_data.push_back(0);
		}

		//replace_int_bytearray(audio_data, 0xC, aligned_length)
		replaceIntBytearray(audio_data, 0xC, aligned_length);

		//end_of_info = align_32bit(len(template_main_body) + len(audio_info_data) + len(file_entry_data))
		size_t end_of_info = align<32>(template_main_body.size() + audio_info_data.size() + file_entry_data.size());

		//eoi_msb = find_msb_position(end_of_info)
		size_t eoi_msb = findMsbPosition(end_of_info);

		//audio_data_start_offset = 1 << eoi_msb + 1
		size_t audio_data_start_offset = 1 << (eoi_msb + 1);

		//full_file_length = align_256bit(audio_data_start_offset + len(audio_data))
		size_t full_file_length = align<256>(audio_data_start_offset + audio_data.size());

		//replace_int_bytearray(template_main_body, 0xC, full_file_length)
		replaceIntBytearray(template_main_body, 0xC, full_file_length);

		//replace_int_bytearray(template_main_body, 0x10, end_of_info + 0x20)
		spec1 = end_of_info + 0x20;
		replaceIntBytearray(template_main_body, 0x10, spec1);

		//replace_int_bytearray(template_main_body, 0x18, len(audio_data) + 0x20)
		spec2 = audio_data.size() + 0x20;
		replaceIntBytearray(template_main_body, 0x18, spec2);

		//replace_int_bytearray(template_main_body, 0x1C, audio_data_start_offset)
		replaceIntBytearray(template_main_body, 0x1C, audio_data_start_offset);

		//replace_int_bytearray(template_main_body, 0xA8, len(template_main_body))
		replaceIntBytearray(template_main_body, 0xA8, template_main_body.size());

		//replace_int_bytearray(template_main_body, 0xB8, len(template_main_body) + len(audio_info_data))
		replaceIntBytearray(template_main_body, 0xB8, template_main_body.size() + audio_info_data.size());

		//replace_int_bytearray(template_main_body, 0xBC, end_of_info)
		replaceIntBytearray(template_main_body, 0xBC, end_of_info);

		Dat.reserve(full_file_length);

		//outfile.write(template_main_body)
		Dat.insert(Dat.end(), template_main_body.begin(), template_main_body.end());
		//outfile.write(audio_info_data)
		Dat.insert(Dat.end(), audio_info_data.begin(), audio_info_data.end());
		//outfile.write(file_entry_data)
		Dat.insert(Dat.end(), file_entry_data.begin(), file_entry_data.end());
		//while outfile.tell() != audio_data_start_offset:
		//    outfile.write(b'\x00')
		Dat.resize(audio_data_start_offset);
		//outfile.write(audio_data)
		Dat.insert(Dat.end(), audio_data.begin(), audio_data.end());
		//while outfile.tell() != full_file_length:
		//    outfile.write(b'\x00')
		Dat.resize(full_file_length);
	}

	GCAXArchive::~GCAXArchive() = default;

	void GCAXArchive::WriteFile(const fs::path &filePath, bool overRideFileName) {
		auto &path = overRideFileName ? filePath : filePath / FileName;
#ifndef NDEBUG
		std::cout << "Writing file: " << path << std::endl;
#endif
		std::ofstream out(path, std::ios_base::binary | std::ios_base::out);
		out.write(reinterpret_cast<const char *>(Dat.data()), Dat.size());
	}

	void GCAXArchive::CompareFile(const fs::path &file) {
		if(fs::status(file).type() != fs::file_type::regular){
			std::cerr << file << " does not exist" << std::endl;
			return;
		}

		std::ifstream compareFile(file);
		std::vector<uint8_t> compare;
		auto fileSize = fs::file_size(file);
		compare.resize(fileSize);
		compareFile.read(reinterpret_cast<std::ifstream::char_type*>(&compare.front()), fileSize);

		size_t differences = 0;
		const int addrWidth = 8, valWidth = 10;
		for(size_t i = 0; i < fileSize; i++){
			unsigned short val1 = -1, val2 = -1;
			if(Dat.size() > i ) val1 = Dat[i];
			if(compare.size() > i ) val2 = compare[i];

			if(val1 == val2) continue;
			if(!differences){
				std::cout << std::left << std::setw(addrWidth) 	<< std::setfill(' ') << "Address" << '|';
				std::cout << std::left << std::setw(valWidth) 	<< std::setfill(' ') << "Created" << '|';
				std::cout << std::left << std::setw(valWidth) 	<< std::setfill(' ') << "Compare" << std::endl;
			}
			differences++;
			continue;

			std::cout << std::hex	<< std::left << std::setw(addrWidth)	<< std::setfill(' ') << i		<< '|';
			std::cout 				<< std::left << std::setw(valWidth)		<< std::setfill(' ') << val1	<< '|';
			std::cout 				<< std::left << std::setw(valWidth)		<< std::setfill(' ') << val2	<< std::endl;
		}
		if(!differences){
			std::cout << "No differences detected." << std::endl;
		}
	}

	template<>
	void PushBytes<std::string>(std::vector<uint8_t> &vector, const std::string &val) {
		vector.insert(vector.end(), val.begin(), val.end());
	}

	template<>
	void PushBytes<std::string_view>(std::vector<uint8_t> &vector, const std::string_view &val) {
		vector.insert(vector.end(), val.begin(), val.end());
	}

} // DatPak
#pragma once

#include <array>
#include <filesystem>
#include <map>
#include <memory>
#include <type_traits>
#include <vector>
#include <bit>

#include "gcem.hpp"

#include "dsptool.h"

constexpr auto errorColors = fg(fmt::color::crimson) | fmt::emphasis::bold;
constexpr auto warningColors = fg(fmt::color::yellow) | fmt::emphasis::bold;
constexpr auto okColors = fg(fmt::color::green);

namespace fs = std::filesystem;

struct ConfigState;

namespace DatPak {
	class GCAXArchive{
		uint16_t ID; // Read only
		fs::path FilePath; // Read only
		std::unique_ptr<std::map<uint8_t, fs::path>> Files;
		std::vector<uint8_t> Dat;
		uint_fast8_t Warnings;

		uint32_t spec1; // Todo: Give these a real name. For now, they match the DATFile struct names
		uint32_t spec2;
	public:
		GCAXArchive(std::mutex &printLock, const uint16_t &datID, fs::path &&filePath, std::unique_ptr<std::map<uint8_t, fs::path>> &&files);

		[[nodiscard]] const uint_fast8_t &getWarningCount() const;

		void WriteFile(const fs::path &config) const;

		[[maybe_unused]] void CompareFile(std::mutex &printLock, const fs::path &file) const;
	};

	struct FileEntry{
		[[maybe_unused]] uint32_t start_offset;
		[[maybe_unused]] int32_t unk;
		[[maybe_unused]] uint32_t shifted_size;
		std::array<int16_t, 16> coefficient;
		std::array<int32_t, 3> unk2;
		[[maybe_unused]] int32_t unk3;
		[[maybe_unused]] uint16_t sample_rate;
		[[maybe_unused]] uint32_t data_size;
	};

	bool verifyWavFormat(std::mutex &printLock, const fs::path &wavFilePath, std::ifstream &wavFile);

/** Check if a number is a power of 2 or not.
 *  IF n is power of 2, return true, else return false.
 */
	constexpr bool powerOfTwo(long n){
		if(n == 0){
			return false;
		}
		return (n & (n - 1)) == 0;
	}

	template<int alignTo, typename T>
	requires std::integral<T>
	constexpr size_t align(T num){
		static_assert(powerOfTwo(alignTo)); // Make sure this is actually a power of two
		const size_t shift = alignTo - 1;

		return (num + shift) & ~shift;
	}

	template<class T>
	concept ResizableArray = requires(T container){
		container.size();
		container.resize(std::size_t{0});
	};

	template<int alignTo, typename T>
	requires ResizableArray<T>
	void alignContainer(T &container){
		size_t aligned = align<alignTo>(container.size());
		container.resize(aligned);
	}

	constexpr size_t findMsbPosition(size_t n){ return static_cast<std::size_t>(gcem::log2(n)); }

	template<typename T, size_t size = sizeof(T)>
	union TypeToBytes{
		T u;
		std::array<uint8_t, size> u8;
	};

	template<typename T>
	inline void replaceIntBytearray(T &arr, const size_t &offset, const uint32_t &n){
		TypeToBytes<uint32_t> nb{.u = n};
		if constexpr(std::endian::native == std::endian::little){ // NOLINT
			arr[offset] = nb.u8[3];
			arr[offset + 1] = nb.u8[2];
			arr[offset + 2] = nb.u8[1];
			arr[offset + 3] = nb.u8[0];
		}else{
			arr[offset] = nb.u8[0];
			arr[offset + 1] = nb.u8[1];
			arr[offset + 2] = nb.u8[2];
			arr[offset + 3] = nb.u8[3];
		}
	}

	template<typename T, size_t size = sizeof(T)>
	constexpr T swap_to_big_endian(const T &u){
		static_assert(CHAR_BIT == 8, "CHAR_BIT != 8");
		if(size == 1){
			return u;
		}
		if constexpr(std::endian::native == std::endian::little){ // NOLINT
			TypeToBytes<T> source;
			TypeToBytes<T> dest;

			source.u = u;

			for(size_t i = 0; i < size; i++){
				dest.u8[i] = source.u8[size - i - 1];
			}

			return dest.u;
		}else{
			return u;
		}
	}

	template<typename T>
	void PushBytes(std::vector<uint8_t> &vector, const T &val){
		TypeToBytes<T> valBytes{.u = val};

		vector.insert(vector.end(), valBytes.u8.begin(), valBytes.u8.end());
	}

	template<>
	void PushBytes<std::string>(std::vector<uint8_t> &vector, const std::string &val);

} // namespace DatPak
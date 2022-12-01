#pragma once

#include <cstdint>
#include <streambuf>

class membuf : public std::basic_streambuf<char> {
public:
	membuf(const uint8_t *p, std::size_t l);
};

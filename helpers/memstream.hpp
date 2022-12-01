#pragma once

#include <istream>
#include "membuf.hpp"

class memstream : public std::istream {
public:
	memstream(const uint8_t *p, size_t l);

private:
	membuf _buffer;
};
